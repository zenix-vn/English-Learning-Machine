"""Speech recognition through an audio-capable model on OpenRouter.

The captured utterance is sent as a WAV clip to a multimodal model that is asked to
transcribe it. Works for Vietnamese and English without any local model.
"""
import base64
import asyncio
import io
import logging
import re
import wave

from openai import APIConnectionError, APIStatusError, AsyncOpenAI

log = logging.getLogger("asr")

INSTRUCTION = (
    "Transcribe the speech in this audio exactly as spoken. The speaker uses Vietnamese "
    "or English (or a mix). Keep the original language, do not translate. Output only the "
    "transcript with normal punctuation. Never add timestamps, time codes or speaker labels. "
    "If there is no clear human speech, output exactly: <silence>"
)


# Some audio models still put time codes in front of the transcript ("00:01 ...", "00:01 - 00:03").
# Only leading ones are removed, so a time the speaker really said ("at 10:30") stays.
_TIMECODE = re.compile(r"^\s*(?:\[?\d{1,2}:\d{2}(?::\d{2})?(?:[.,]\d+)?\]?\s*[-–—]*\s*)+")


def clean_transcript(text: str) -> str:
    return re.sub(r"\s+", " ", _TIMECODE.sub("", text)).strip(" -–—")


def usage_cost(usage) -> float:
    """Dollar cost reported by OpenRouter in the usage block (0 when absent)."""
    if usage is None:
        return 0.0
    cost = getattr(usage, "cost", None)
    if cost is None:
        cost = (getattr(usage, "model_extra", None) or {}).get("cost")
    try:
        return float(cost or 0)
    except (TypeError, ValueError):
        return 0.0


def pcm_to_wav(pcm16: bytes, rate: int) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm16)
    return buf.getvalue()


class Recognizer:
    def __init__(self, base_url: str, rate: int, min_ms: int):
        self.base_url = base_url
        self.rate = rate
        self.min_bytes = rate * 2 * min_ms // 1000

    async def transcribe(self, pcm16: bytes, api_key: str, model: str, fallback: str = "") -> tuple[str, float]:
        """Return (transcript, cost in USD); the transcript is "" when nothing intelligible was said.

        A busy upstream ("429 temporarily rate-limited", 5xx, a dropped connection) is retried once on the same model
        and then once on `fallback` (another model has its own rate limit), so a hiccup of one model does not cost the
        learner a lost sentence. Other errors, such as a wrong API key, are raised at once.
        """
        if len(pcm16) < self.min_bytes:
            return "", 0.0
        wav_b64 = base64.b64encode(pcm_to_wav(pcm16, self.rate)).decode()
        extra = {"reasoning": {"enabled": False}} if "openrouter.ai" in self.base_url else None
        messages = [{"role": "user", "content": [
            {"type": "text", "text": INSTRUCTION},
            {"type": "input_audio", "input_audio": {"data": wav_b64, "format": "wav"}},
        ]}]
        # max_retries=0: this method does the retrying, so the delay is short and the same everywhere
        client = AsyncOpenAI(base_url=self.base_url, api_key=api_key, timeout=25, max_retries=0)
        attempts = [(model, 0.0), (model, 0.8)] + ([(fallback, 0.0)] if fallback and fallback != model else [])
        resp, error = None, None
        for m, delay in attempts:
            if delay:
                await asyncio.sleep(delay)
            try:
                resp = await client.chat.completions.create(
                    model=m, temperature=0, max_tokens=300, extra_body=extra, messages=messages)
                break
            except (APIStatusError, APIConnectionError) as e:
                if isinstance(e, APIStatusError) and e.status_code not in (408, 409, 425, 429) and e.status_code < 500:
                    raise                                  # not a busy upstream: retrying will not help
                error = e
                log.warning("asr %s failed (%s), %s", m, type(e).__name__, "retrying" if (m, delay) != attempts[-1] else "giving up")
        if resp is None:
            raise error
        cost = usage_cost(resp.usage)
        text = clean_transcript((resp.choices[0].message.content or "").strip())
        if not text or "<silence>" in text.lower():
            return "", cost
        log.info("heard: %s", text)
        return text, cost
