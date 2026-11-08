"""Text-to-speech with Microsoft Edge voices (no API key needed)."""
import asyncio
import logging
import re

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


class Synthesizer:
    def __init__(self, voice: str, rate: str, sample_rate: int):
        self.voice = voice
        self.rate = rate
        self.sample_rate = sample_rate

    async def synthesize(self, text: str) -> bytes:
        """Return 16-bit mono PCM, or b"" when there is nothing to say."""
        text = clean_for_speech(text)
        if not re.search(r"\w", text):
            return b""
        mp3 = bytearray()
        for attempt in range(2):
            try:
                comm = edge_tts.Communicate(text, self.voice, rate=self.rate)
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
