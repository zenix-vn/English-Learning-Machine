# English Learning Machine

A voice-driven English tutor on the **ESP32-TPCB313** board (ESP32-S3, 3.13" 376x960 display).
Say "Sophia" and talk to your tutor to look up words, translate Vietnamese <-> English, build a vocabulary notebook
with spaced-repetition review, and practise conversation.

| Folder | What it is |
|---|---|
| `firmware/` | ESP-IDF firmware for the device (board: `main/boards/english-learner`) |
| `backend/`  | Python server: Opus audio in, speech recognition, LLM tutor with tools, speech out |
| `backend/admin/` | Admin page: dashboard and costs, device approval and remote control, conversation log, vocabulary, firmware (OTA) upload, models, prompt and safety settings, live logs |

## How it works
Device <-WebSocket (Opus audio + JSON)-> backend -> OpenRouter (speech recognition + chat) and Edge TTS.
Tools the tutor can call: `lookup_word` (dictionary), `learn_new_words` (next word from the device's vocabulary deck),
`save_word`, `next_review_word`, `grade_word` (spaced-repetition notebook).

Vocabulary decks (TSV: word, IPA, meaning, example) are imported from the admin page; the nine English decks
(~41,000 cards: Oxford 3000, 5000 common words, school levels, CET-4/6, IELTS, TOEFL, GRE) are preloaded.

## Backend
```bash
cd backend
docker compose up -d --build        # listens on 127.0.0.1:18000, put a TLS reverse proxy in front
```
- `GET/POST /ota/` is called by the device at boot and returns the WebSocket address and a token.
- `/ws/` is the device WebSocket. `/health` is a liveness probe.
- Everything editable from the admin page (`data/llm.json`, `data/settings.json`) is re-read for every new
  conversation, so no restart is needed. Devices, conversation turns and costs live in `data/app.db`.
- **First boot:** after Wi-Fi is set up the device calls `/ota/`, which registers it and returns an *activation* block;
  the firmware shows and reads out a 6-digit code and polls `/ota/activate` (202 = wait, 200 = activated).
  The admin enters the code (or presses Activate) and the device starts on the default **plan**.
- **Plans** (admin tab *Gói cước*): duration, turns per day, turns per month. Expired or exhausted plans are announced
  by voice. The server stores registration date, first activation date, plan, expiry and usage for every device.
- Safety: a daily cost limit protects the API key, and registrations per IP per day are capped.
- End-to-end test with real synthesized speech: `docker exec english-learner python -m tests.e2e "What does serendipity mean?"`

Production deployment: `https://e-learning.zenix.vn` (OpenLiteSpeed reverse proxy; the admin page is at `/admin/`).

## Offline study
The device can study without network: flashcards with spoken words and spaced repetition, progress synced when Wi-Fi returns.
The board has no SD slot and no free GPIO, so the data lives in a 4 MB FAT partition in flash.
Design, formats, controls and test status: [docs/offline-learning.md](docs/offline-learning.md).

## Firmware
See [firmware/README.md](firmware/README.md). Set the OTA URL to `https://e-learning.zenix.vn/ota/`.

## Known gaps
- Firmware has not been compiled or flashed yet (no ESP-IDF on the development machine).
- The "Sophia" wake word (`wn9_sophia_tts`) is untested on the device; the Boot button always works.
- On-screen Vietnamese text needs `firmware/scripts/gen_fonts.sh` to be run once (offline study screen); the built-in font is ASCII only.
- The offline study glue (`main/offline/{storage,sync,study}.cc`) is type-checked against stubs only; the portable core is unit-tested.
