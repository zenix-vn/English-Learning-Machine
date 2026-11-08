"""Builds an offline study pack for the device (text + one short spoken clip per word).

Run inside the backend container, normally started from the admin page:
    python -m app.packbuild --deck 1 --count 2000 --bitrate 16k

Pack layout (all integers little-endian), files in data/packs/<pack_id>/:
    cards.bin   records "word\\0ipa\\0meaning\\0example\\0" back to back (UTF-8)
    cards.idx   uint32 offset of every record, plus one final entry = size of cards.bin
    audio.bin   one OGG/Opus clip per word, back to back
    audio.idx   per card: uint32 offset, uint32 length into audio.bin (length 0 = no audio)
    ranks.bin   server only: uint32 deck rank of every card (maps device progress back to words)
    manifest.json   description and sha256 of the files the device downloads
The device plays a clip by handing the bytes to AudioService::PlaySound().
"""
import argparse
import asyncio
import hashlib
import json
import logging
import shutil
import struct
import time

import edge_tts

from .config import DATA
from .tools import SKIP_WORDS, _db

log = logging.getLogger("packbuild")
PACKS = DATA / "packs"
DEVICE_FILES = ["cards.bin", "cards.idx", "audio.bin", "audio.idx"]
VOICE = "en-US-AriaNeural"
TRIM = ("silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse,"
        "silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse")


def select_words(deck_id: int, count: int) -> list[tuple]:
    """First `count` teachable words of the deck, in deck order: (rank, word, ipa, meaning, example)."""
    out, seen = [], set()
    with _db() as con:
        for rank, word, ipa, meaning, example in con.execute(
                "SELECT rank, word, ipa, meaning, example FROM deck_words WHERE deck_id=? ORDER BY rank", (deck_id,)):
            key = word.strip().lower()
            if key in seen or key in SKIP_WORDS or len(key) <= 2 or not key.replace("-", "").replace("'", "").isalpha():
                continue
            seen.add(key)
            out.append((rank, key, ipa.strip(), meaning.strip()[:240], example.strip()[:160]))
            if len(out) >= count:
                break
    return out


async def speak(word: str, voice: str, bitrate: str) -> bytes:
    """Word -> OGG/Opus bytes (mono, 16 kHz, 60 ms frames, silence trimmed). Empty bytes on failure."""
    for attempt in range(3):
        try:
            mp3 = bytearray()
            async for chunk in edge_tts.Communicate(word, voice).stream():
                if chunk["type"] == "audio":
                    mp3 += chunk["data"]
            if not mp3:
                continue
            proc = await asyncio.create_subprocess_exec(
                "ffmpeg", "-v", "error", "-i", "pipe:0", "-af", TRIM, "-ac", "1", "-ar", "16000",
                "-c:a", "libopus", "-b:a", bitrate, "-vbr", "on", "-application", "voip",
                "-frame_duration", "60", "-f", "ogg", "pipe:1",
                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
            ogg, _ = await proc.communicate(bytes(mp3))
            if proc.returncode == 0 and ogg:
                return ogg
        except Exception as e:
            log.warning("tts/encode failed for %r (attempt %d): %s", word, attempt + 1, e)
        await asyncio.sleep(1 + attempt)
    return b""


def write_status(deck_id: int, **fields):
    PACKS.mkdir(parents=True, exist_ok=True)
    path = PACKS / f"build-{deck_id}.json"
    try:
        cur = json.loads(path.read_text())
    except (OSError, ValueError):
        cur = {}
    cur.update(fields)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(cur))
    tmp.replace(path)


async def build(deck_id: int, count: int, bitrate: str = "16k", voice: str = VOICE, audio: bool = True):
    t0 = time.time()
    write_status(deck_id, state="running", done=0, total=0, started=int(t0), error="")
    with _db() as con:
        deck = con.execute("SELECT name FROM decks WHERE id=?", (deck_id,)).fetchone()
    if deck is None:
        write_status(deck_id, state="error", error="deck not found")
        raise SystemExit("deck not found")
    words = select_words(deck_id, count)
    if not words:
        write_status(deck_id, state="error", error="deck is empty")
        raise SystemExit("deck is empty")
    write_status(deck_id, total=len(words))

    clips: list[bytes] = [b""] * len(words)
    if audio:
        sem, done = asyncio.Semaphore(4), 0

        async def one(i: int):
            nonlocal done
            async with sem:
                clips[i] = await speak(words[i][1], voice, bitrate)
            done += 1
            if done % 25 == 0 or done == len(words):
                write_status(deck_id, done=done)

        await asyncio.gather(*(one(i) for i in range(len(words))))

    cards, cards_idx, audio_bin, audio_idx, ranks = bytearray(), bytearray(), bytearray(), bytearray(), bytearray()
    for (rank, word, ipa, meaning, example), clip in zip(words, clips):
        cards_idx += struct.pack("<I", len(cards))
        cards += b"\0".join(s.encode("utf-8").replace(b"\0", b"") for s in (word, ipa, meaning, example)) + b"\0"
        audio_idx += struct.pack("<II", len(audio_bin), len(clip))
        audio_bin += clip
        ranks += struct.pack("<I", rank)
    cards_idx += struct.pack("<I", len(cards))

    blobs = {"cards.bin": bytes(cards), "cards.idx": bytes(cards_idx), "audio.bin": bytes(audio_bin),
             "audio.idx": bytes(audio_idx), "ranks.bin": bytes(ranks)}
    digest = hashlib.sha256(b"".join(blobs[n] for n in DEVICE_FILES)).hexdigest()[:10]
    pack_id = f"d{deck_id}-{len(words)}-{digest}"
    manifest = {
        "v": 1, "pack_id": pack_id, "deck_id": deck_id, "deck": deck[0], "count": len(words),
        "built": int(time.time()), "audio": audio, "voice": voice if audio else "", "bitrate": bitrate if audio else "",
        "total_bytes": sum(len(blobs[n]) for n in DEVICE_FILES),
        "files": [{"name": n, "size": len(blobs[n]), "sha256": hashlib.sha256(blobs[n]).hexdigest()}
                  for n in DEVICE_FILES],
    }

    tmp = PACKS / f".tmp-{pack_id}"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    for name, data in blobs.items():
        (tmp / name).write_bytes(data)
    (tmp / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False))
    final = PACKS / pack_id
    shutil.rmtree(final, ignore_errors=True)
    tmp.rename(final)

    current_path = PACKS / "current.json"
    try:
        current = json.loads(current_path.read_text())
    except (OSError, ValueError):
        current = {}
    old = current.get(str(deck_id))
    current[str(deck_id)] = pack_id
    current_path.write_text(json.dumps(current))
    if old and old != pack_id:
        shutil.rmtree(PACKS / old, ignore_errors=True)
    write_status(deck_id, state="done", done=len(words), pack_id=pack_id, seconds=int(time.time() - t0),
                 bytes=manifest["total_bytes"], missing_audio=sum(1 for c in clips if not c) if audio else 0)
    log.info("built %s: %d words, %d bytes, %ds", pack_id, len(words), manifest["total_bytes"], time.time() - t0)
    return pack_id


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    ap = argparse.ArgumentParser(description="Build an offline study pack")
    ap.add_argument("--deck", type=int, required=True)
    ap.add_argument("--count", type=int, default=2000)
    ap.add_argument("--bitrate", default="16k", choices=["12k", "16k", "24k"])
    ap.add_argument("--voice", default=VOICE)
    ap.add_argument("--no-audio", action="store_true")
    a = ap.parse_args()
    try:
        asyncio.run(build(a.deck, max(1, min(a.count, 6000)), a.bitrate, a.voice, not a.no_audio))
    except SystemExit:
        raise
    except Exception as e:
        write_status(a.deck, state="error", error=str(e)[:300])
        raise


if __name__ == "__main__":
    main()
