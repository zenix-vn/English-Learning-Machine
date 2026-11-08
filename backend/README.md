# Backend

See the [project README](../README.md). Quick reference:

```
app/main.py      HTTP + WebSocket entry point (/ota/, /ws/, /health)
app/session.py   one device connection: VAD -> ASR -> LLM(+tools) -> TTS -> Opus
app/asr.py       speech recognition via an audio-capable OpenRouter model
app/llm.py       streaming chat with tool calling
app/tts.py       Edge TTS
app/tools.py     dictionary lookup + spaced-repetition notebook (SQLite)
config.yaml      static settings; prompt.txt is the tutor's system prompt
admin/           admin page (runs on the host as a systemd service, proxied at /admin/)
tests/e2e.py     end-to-end test with real synthesized audio
```
