"""Tools the tutor can call: dictionary lookup and a spaced-repetition word notebook."""
import asyncio
import logging
import sqlite3
import time

import aiohttp

from .config import DATA, load_settings

log = logging.getLogger("tools")
DB_PATH = DATA / "vocab.db"
DAY = 86400

SCHEMAS = [
    {"type": "function", "function": {
        "name": "lookup_word",
        "description": "Look up an English word or phrase: phonetics, part of speech, definitions, examples.",
        "parameters": {"type": "object", "properties": {
            "word": {"type": "string", "description": "The English word or phrase"}},
            "required": ["word"]}}},
    {"type": "function", "function": {
        "name": "save_word",
        "description": "Save a word to the learner's notebook for later review.",
        "parameters": {"type": "object", "properties": {
            "word": {"type": "string"},
            "meaning": {"type": "string", "description": "Short Vietnamese meaning"},
            "example": {"type": "string", "description": "English example sentence"}},
            "required": ["word", "meaning"]}}},
    {"type": "function", "function": {
        "name": "next_review_word",
        "description": "Get the next notebook word that is due for review. Do not reveal the answer before the learner replies.",
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": "learn_new_words",
        "description": ("Get the next NEW word to teach from the learner's vocabulary deck (one word per call). It is "
                        "added to the notebook automatically for later review. Call it when the learner wants to "
                        "learn new words, and again each time they are ready for the next word."),
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": "start_offline_study",
        "description": ("Switch the device to offline flashcard study (works without internet). Use when the learner "
                        "asks to study offline, without internet, or with flashcards on the device."),
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": "grade_word",
        "description": "Record the review result. quality: 0 wrong/forgotten, 3 correct but hard, 4 correct, 5 very easy.",
        "parameters": {"type": "object", "properties": {
            "word": {"type": "string"},
            "quality": {"type": "integer", "enum": [0, 3, 4, 5]}},
            "required": ["word", "quality"]}}},
]


def _db() -> sqlite3.Connection:
    """Open vocab.db. The deck tables are created and filled by the admin page (same DDL there)."""
    con = sqlite3.connect(DB_PATH, timeout=10)
    con.executescript("""
        CREATE TABLE IF NOT EXISTS vocab(
            device TEXT, word TEXT, meaning TEXT, example TEXT,
            ease REAL DEFAULT 2.5, interval REAL DEFAULT 0, reps INTEGER DEFAULT 0,
            due INTEGER, created INTEGER, PRIMARY KEY(device, word));
        CREATE TABLE IF NOT EXISTS decks(
            id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE, description TEXT DEFAULT '',
            position INTEGER DEFAULT 0, words INTEGER DEFAULT 0);
        CREATE TABLE IF NOT EXISTS deck_words(
            deck_id INTEGER, rank INTEGER, word TEXT, ipa TEXT, meaning TEXT, example TEXT,
            PRIMARY KEY(deck_id, rank));
        CREATE TABLE IF NOT EXISTS device_deck(device TEXT PRIMARY KEY, deck_id INTEGER);""")
    return con


async def lookup_word(device: str, word: str) -> str:
    word = (word or "").strip().lower()
    if not word:
        return "No word given."
    try:
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=6)) as s:
            async with s.get(f"https://api.dictionaryapi.dev/api/v2/entries/en/{word}") as r:
                if r.status == 404:
                    return f"'{word}' was not found in the dictionary. Explain it from your own knowledge."
                r.raise_for_status()
                entry = (await r.json())[0]
    except Exception as e:
        log.warning("dictionary lookup failed: %s", e)
        return f"Dictionary unavailable. Explain '{word}' from your own knowledge."
    lines = [f"Word: {entry.get('word', word)}"]
    phonetic = entry.get("phonetic") or next(
        (p["text"] for p in entry.get("phonetics", []) if p.get("text")), "")
    if phonetic:
        lines.append(f"Phonetic (do not read aloud): {phonetic}")
    for m in entry.get("meanings", [])[:3]:
        lines.append(f"[{m.get('partOfSpeech', '')}]")
        for d in m.get("definitions", [])[:2]:
            lines.append(f"- {d.get('definition', '')}")
            if d.get("example"):
                lines.append(f"  example: {d['example']}")
    return "\n".join(lines)


def _save_word(device: str, word: str, meaning: str, example: str = "") -> str:
    word, now = word.strip().lower(), int(time.time())
    with _db() as con:
        con.execute(
            "INSERT INTO vocab(device,word,meaning,example,due,created) VALUES(?,?,?,?,?,?) "
            "ON CONFLICT(device,word) DO UPDATE SET meaning=excluded.meaning, example=excluded.example",
            (device, word, meaning, example, now, now))
    return f"Saved '{word}'."


def _next_review_word(device: str) -> str:
    now = int(time.time())
    with _db() as con:
        row = con.execute(
            "SELECT word, meaning, example FROM vocab WHERE device=? AND due<=? ORDER BY due LIMIT 1",
            (device, now)).fetchone()
        left = con.execute(
            "SELECT COUNT(*) FROM vocab WHERE device=? AND due<=?", (device, now)).fetchone()[0]
    if not row:
        return "No words are due. Praise the learner and suggest learning new words."
    word, meaning, example = row
    return (f"Word to review: {word}\nAnswer (secret, only for grading): {meaning}\n"
            f"Example: {example}\n{left} words are due. Say the English word and ask for its meaning.")


def _deck_for(con: sqlite3.Connection, device: str):
    """The deck assigned to this device, else the default deck, else the first deck."""
    row = con.execute("SELECT deck_id FROM device_deck WHERE device=?", (device,)).fetchone()
    deck_id = row[0] if row else load_settings().get("default_deck") or 0
    deck = con.execute("SELECT id, name FROM decks WHERE id=?", (deck_id,)).fetchone() if deck_id else None
    return deck or con.execute("SELECT id, name FROM decks ORDER BY position, id LIMIT 1").fetchone()


# Very common function words: not worth teaching as "new vocabulary".
SKIP_WORDS = set("""the and for not you she him his her they them their our your its who whom whose what which
when where why how that this these those there here with from into onto about over under than then also just
can could will would shall should may might must have has had been being was were are does did done but nor yet
any all some each every both either neither one two three four five six seven eight nine ten""".split())


def _learn_new_words(device: str, count: int = 1) -> str:
    count = max(1, min(3, int(count or 1)))
    now = int(time.time())
    with _db() as con:
        deck = _deck_for(con, device)
        if deck is None:
            return "No vocabulary deck is configured. Teach a few useful words from your own knowledge instead."
        rows = con.execute(
            "SELECT w.word, w.ipa, w.meaning, w.example FROM deck_words w WHERE w.deck_id=? AND "
            "NOT EXISTS (SELECT 1 FROM vocab v WHERE v.device=? AND v.word=lower(w.word)) "
            "ORDER BY w.rank LIMIT ?", (deck[0], device, count * 4 + 400)).fetchall()
        picked, seen = [], set()
        for word, ipa, meaning, example in rows:
            key = word.strip().lower()
            if key in seen or len(picked) >= count or key in SKIP_WORDS or len(key) <= 2:
                continue
            seen.add(key)
            example_en = example.split(" — ")[0].strip()
            picked.append((key, meaning.strip()[:200], example_en))
            con.execute(
                "INSERT OR IGNORE INTO vocab(device,word,meaning,example,due,created) VALUES(?,?,?,?,?,?)",
                (device, key, meaning.strip()[:200], example_en, now + 600, now))
    if not picked:
        return (f"The learner has already studied every word in the deck '{deck[1]}'. "
                "Congratulate them and suggest another deck or conversation practice.")
    lines = [f"New word from the deck '{deck[1]}' (already added to the notebook, first review in about 10 minutes):"]
    for i, (word, meaning, example) in enumerate(picked, 1):
        lines.append(f"{i}. {word} | dictionary meaning: {meaning} | example: {example or '(none)'}")
    lines.append("Teach this word now and only this word. The dictionary meaning is raw and may list rare senses "
                 "first: choose the most common one and say it briefly in Vietnamese. Say the English word, the "
                 "Vietnamese meaning, then one short English example (make one up if there is none), and ask the "
                 "learner to repeat it. When they are ready for another word, call learn_new_words again; never "
                 "pick a word yourself.")
    return "\n".join(lines)


def _start_offline(device: str) -> str:
    from . import store                     # imported here: store does not depend on tools, keep it that way
    with store.connect() as con:
        con.execute("INSERT INTO commands(device,cmd,ts) VALUES(?,?,?)", (device, "offline", int(time.time())))
    return ("The device switches to offline flashcard study in a few seconds. Say one short sentence to wish the "
            "learner good studying and explain: one click shows the meaning, then one click = remembered, "
            "two clicks = forgot, hold = easy. Keep it under three sentences.")


def apply_grade(con: sqlite3.Connection, device: str, word: str, quality: int, at: int | None = None):
    """SM-2 step for one review. Returns the new interval in days, or None when the word is not in the notebook.

    The device firmware (main/offline/sm2.cc) implements exactly the same rules so online and offline
    progress stay consistent.
    """
    word, q = word.strip().lower(), max(0, min(5, int(quality)))
    at = at or int(time.time())
    row = con.execute("SELECT ease, interval, reps FROM vocab WHERE device=? AND word=?", (device, word)).fetchone()
    if not row:
        return None
    ease, interval, reps = row
    if q < 3:
        reps, interval = 0, 10 / 1440          # try again in 10 minutes
    else:
        reps += 1
        interval = 1 if reps == 1 else 3 if reps == 2 else interval * ease
        ease = max(1.3, ease + 0.1 - (5 - q) * (0.08 + (5 - q) * 0.02))
    con.execute("UPDATE vocab SET ease=?, interval=?, reps=?, due=? WHERE device=? AND word=?",
                (ease, interval, reps, int(at + interval * DAY), device, word))
    return interval


def _grade_word(device: str, word: str, quality: int) -> str:
    with _db() as con:
        interval = apply_grade(con, device, word, quality)
    if interval is None:
        return f"'{word.strip().lower()}' is not in the notebook."
    return f"Recorded. '{word.strip().lower()}' will come back in {interval:.1f} days."


async def run_tool(device: str, name: str, args: dict) -> str:
    """Execute a tool call and return its text result."""
    try:
        if name == "lookup_word":
            return await lookup_word(device, args.get("word", ""))
        loop = asyncio.get_running_loop()
        if name == "save_word":
            return await loop.run_in_executor(None, lambda: _save_word(
                device, args["word"], args["meaning"], args.get("example", "")))
        if name == "next_review_word":
            return await loop.run_in_executor(None, _next_review_word, device)
        if name == "start_offline_study":
            return await loop.run_in_executor(None, _start_offline, device)
        if name == "learn_new_words":
            return await loop.run_in_executor(None, _learn_new_words, device, 1)
        if name == "grade_word":
            return await loop.run_in_executor(None, _grade_word, device, args["word"], args["quality"])
        return f"Unknown tool {name}."
    except Exception as e:
        log.exception("tool %s failed", name)
        return f"Tool error: {e}"
