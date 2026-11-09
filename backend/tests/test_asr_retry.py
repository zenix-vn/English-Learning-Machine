"""Speech recognition survives a busy upstream: retry on the same model, then the fallback model.
Needs the server packages (openai, httpx), so run it in the backend container:
    docker exec english-learner python -m tests.test_asr_retry"""
import asyncio
import sys
from types import SimpleNamespace

import httpx
import openai

from app import asr

bad = 0


def check(ok, what):
    global bad
    if not ok:
        bad += 1
        print("FAILED:", what)


def status_error(code):
    resp = httpx.Response(code, request=httpx.Request("POST", "http://x"))
    cls = openai.RateLimitError if code == 429 else openai.APIStatusError
    return cls(f"error {code}", response=resp, body=None)


class FakeClient:
    """Fails with `errors[model]` (a list, consumed one per call), then answers."""
    calls: list = []
    errors: dict = {}

    def __init__(self, **kw):
        self.chat = SimpleNamespace(completions=SimpleNamespace(create=self.create))

    async def create(self, model, **kw):
        FakeClient.calls.append(model)
        queue = FakeClient.errors.get(model, [])
        if queue:
            raise queue.pop(0)
        msg = SimpleNamespace(content="hello there")
        return SimpleNamespace(choices=[SimpleNamespace(message=msg)], usage=None)


asr.AsyncOpenAI = FakeClient
_sleep = asyncio.sleep
asr.asyncio.sleep = lambda s: _sleep(0)                  # no real waiting in the test
rec = asr.Recognizer("http://x", 16000, 100)
pcm = b"\x01\x00" * 16000


def run(errors, fallback="fb"):
    FakeClient.calls, FakeClient.errors = [], errors
    return asyncio.run(rec.transcribe(pcm, "key", "main", fallback))


text, _ = run({})
check(text == "hello there" and FakeClient.calls == ["main"], "no error: one call")

text, _ = run({"main": [status_error(429)]})
check(text == "hello there" and FakeClient.calls == ["main", "main"], "one 429: retried on the same model")

text, _ = run({"main": [status_error(429), status_error(429)]})
check(text == "hello there" and FakeClient.calls == ["main", "main", "fb"], "two 429: fallback model answers")

text, _ = run({"main": [status_error(503), status_error(503)]})
check(FakeClient.calls == ["main", "main", "fb"], "5xx counts as busy too")

try:
    run({"main": [status_error(429)] * 2, "fb": [status_error(429)]})
    check(False, "all models busy must raise")
except openai.RateLimitError:
    check(FakeClient.calls == ["main", "main", "fb"], "all busy: raises after trying everything")

try:
    run({"main": [status_error(401)]})
    check(False, "a bad key must raise at once")
except openai.APIStatusError:
    check(FakeClient.calls == ["main"], "401 is not retried")

try:
    run({"main": [status_error(429)] * 2}, fallback="")
    check(False, "no fallback: must raise")
except openai.RateLimitError:
    check(FakeClient.calls == ["main", "main"], "no fallback configured: stops after the retry")

print("all passed" if not bad else f"{bad} failed")
sys.exit(1 if bad else 0)
