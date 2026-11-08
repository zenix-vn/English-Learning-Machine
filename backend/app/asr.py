"""Speech recognition through an audio-capable model on OpenRouter.

The captured utterance is sent as a WAV clip to a multimodal model that is asked to
transcribe it. Works for Vietnamese and English without any local model.
"""
import base64
import io
import logging
import wave

from openai import AsyncOpenAI

log = logging.getLogger("asr")

INSTRUCTION = (
    "Transcribe the speech in this audio exactly as spoken. The speaker uses Vietnamese "
    "or English (or a mix). Keep the original language, do not translate. Output only the "
    "transcript with normal punctuation. If there is no clear human speech, output exactly: <silence>"
)


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

    async def transcribe(self, pcm16: bytes, api_key: str, model: str) -> tuple[str, float]:
        """Return (transcript, cost in USD); the transcript is "" when nothing intelligible was said."""
        if len(pcm16) < self.min_bytes:
            return "", 0.0
        client = AsyncOpenAI(base_url=self.base_url, api_key=api_key, timeout=30)
        wav_b64 = base64.b64encode(pcm_to_wav(pcm16, self.rate)).decode()
        extra = {"reasoning": {"enabled": False}} if "openrouter.ai" in self.base_url else None
        resp = await client.chat.completions.create(
            model=model, temperature=0, max_tokens=300, extra_body=extra,
            messages=[{"role": "user", "content": [
                {"type": "text", "text": INSTRUCTION},
                {"type": "input_audio", "input_audio": {"data": wav_b64, "format": "wav"}},
            ]}])
        cost = usage_cost(resp.usage)
        text = (resp.choices[0].message.content or "").strip()
        if not text or "<silence>" in text.lower():
            return "", cost
        log.info("heard: %s", text)
        return text, cost
