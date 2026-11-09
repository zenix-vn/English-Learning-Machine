"""Text-to-speech with Microsoft Edge voices (no API key needed)."""
import asyncio
import logging
import re
import unicodedata

import edge_tts

from .audio import mp3_to_pcm

log = logging.getLogger("tts")

_MARKDOWN = re.compile(r"[*_`#>~|]+")
_BRACKETS = re.compile(r"[\(\[\{][^\)\]\}]*[\)\]\}]")
_SLASHED = re.compile(r"/[^/\s]*[^\x00-\x7f][^/]*/")      # /IPA symbols/
_EMOJI = re.compile("[\U0001F000-\U0001FFFF☀-➿️]")
_CJK = re.compile("[　-〿㐀-鿿＀-￯]")


def clean_for_speech(text: str) -> str:
    text = _SLASHED.sub(" ", text)
    text = _BRACKETS.sub(" ", text)
    text = _EMOJI.sub("", text)
    text = _CJK.sub("", text)
    text = _MARKDOWN.sub("", text)
    return re.sub(r"\s+", " ", text).strip()


# ---------------------------------------------------------------- language split
# The tutor explains in Vietnamese and says English words and sentences in English. One voice cannot do both
# well, so every sentence is cut into Vietnamese and English runs and each run gets its own voice.
_VI_MARKS = {"\u0300", "\u0301", "\u0309", "\u0303", "\u0323", "\u0302", "\u0306", "\u031b"}   # tones, circumflex, breve, horn
_QUOTED = re.compile(r'("[^"]*"|\u201c[^\u201d]*\u201d)')
# Shape of a Vietnamese syllable without its tone mark (onset + vowels + coda). Words that do not fit
# (love, dog, serendipity) can only be English; words that do fit (cat, the, trong) are ambiguous.
_VI_SYLLABLE = re.compile(r"^(ngh|ng|nh|ch|gh|gi|kh|ph|qu|th|tr|[bcdghklmnpqrstvx])?[aeiouy]{1,3}(ch|ng|nh|c|m|n|p|t)?$", re.I)


def _has_vietnamese(word: str) -> bool:
    if "\u0111" in word or "\u0110" in word:
        return True
    return any(c in _VI_MARKS for c in unicodedata.normalize("NFD", word))


def _word_language(token: str):
    """'vi', 'en' or None when the token does not say (digits, short ambiguous words)."""
    word = re.sub(r"^\W+|\W+$", "", token)
    if not word or not re.search(r"[^\W\d_]", word):
        return None
    if _has_vietnamese(word):
        return "vi"
    if re.search(r"[fjwzFJWZ]", word) or not _VI_SYLLABLE.match(word):
        return "en"
    return None


def split_languages(text: str):
    """[(language, text)] runs, language 'vi' or 'en'. Text in double quotes is taken as English unless it
    contains Vietnamese letters; outside quotes each word is judged by its letters and shape."""
    pieces = []                                   # (language or None, text)
    for part in _QUOTED.split(text):
        if not part.strip():
            continue
        if _QUOTED.fullmatch(part):
            inner = part[1:-1].strip()
            if inner:
                pieces.append(("vi" if _has_vietnamese(inner) else "en", inner))
            continue
        tokens = part.split()
        for k, tok in enumerate(tokens):
            lang = _word_language(tok)
            # right after a colon ("Ví dụ: cat", "Ví dụ: I shut down...") an unaccented word starts an English example,
            # unless Vietnamese words follow straight away ("Ví dụ: ba con mèo")
            if (lang is None and k > 0 and tokens[k - 1].endswith(":") and re.fullmatch(r"[A-Za-z'’-]+\W*", tok)
                    and not any(_has_vietnamese(t) for t in tokens[k + 1:k + 3])):
                lang = "en"
            pieces.append((lang, tok))
    known = [lang for lang, _ in pieces if lang]
    default = "vi" if "vi" in known or not known else "en"
    out, prev = [], None
    # an ambiguous word follows the language of the word before it; at the start, the one after it
    resolved = []
    for i, (lang, tok) in enumerate(pieces):
        if lang is None:
            lang = prev
            if lang is None:
                lang = next((l for l, _ in pieces[i + 1:] if l), default)
        resolved.append((lang, tok))
        prev = lang
    for lang, tok in resolved:
        if out and out[-1][0] == lang:
            out[-1] = (lang, out[-1][1] + " " + tok)
        else:
            out.append((lang, tok))
    return out


class Synthesizer:
    def __init__(self, voice: str, rate: str, sample_rate: int, voice_vi: str = ""):
        self.voice = voice                  # English runs (and everything when there is no Vietnamese voice)
        self.voice_vi = voice_vi or voice   # Vietnamese runs
        self.rate = rate
        self.sample_rate = sample_rate

    async def synthesize(self, text: str) -> bytes:
        """Return 16-bit mono PCM, or b"" when there is nothing to say."""
        text = clean_for_speech(text)
        if not re.search(r"\w", text):
            return b""
        if self.voice_vi == self.voice:
            return await self._speak(text, self.voice)
        runs = [(lang, t) for lang, t in split_languages(text) if re.search(r"\w", t)]
        pcm = await asyncio.gather(*(self._speak(t, self.voice_vi if lang == "vi" else self.voice)
                                     for lang, t in runs))
        return b"".join(pcm)

    async def _speak(self, text: str, voice: str) -> bytes:
        mp3 = bytearray()
        for attempt in range(2):
            try:
                comm = edge_tts.Communicate(text, voice, rate=self.rate)
                async for chunk in comm.stream():
                    if chunk["type"] == "audio":
                        mp3 += chunk["data"]
                break
            except Exception as e:  # network hiccup: retry once
                log.warning("tts attempt %d failed: %s", attempt + 1, e)
                mp3.clear()
        if not mp3:
            return b""
        return await asyncio.get_running_loop().run_in_executor(
            None, mp3_to_pcm, bytes(mp3), self.sample_rate)
