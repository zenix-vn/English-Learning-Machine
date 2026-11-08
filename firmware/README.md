# English Learner firmware

Firmware for a small English-learning voice device built on the **ESP32-TPCB313**
(ESP32-S3, 3.13" 376x960 RGB display, ES8311 audio codec).
It streams microphone audio to the English Learner backend and plays back the tutor's answers.

## Hardware
- ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB PSRAM)
- GC9503V RGB panel, ES8311 codec + NS4150 amplifier, analog microphone
- Board definition: `main/boards/english-learner`

## Build
1. Install ESP-IDF **5.3.1** and run `idf.py set-target esp32s3`.
2. `idf.py menuconfig` -> *English Learner*: the OTA URL defaults to `https://e-learning.zenix.vn/ota/`.
3. `idf.py build flash monitor` (USB-C port, CH343P serial chip).

Default language is Vietnamese (`vi-VN`); English is also available.

## Wake word
The default wake word is **"Sophia"** (esp-sr model `wn9_sophia_tts`), selected by `scripts/build.py`.
Run `python scripts/build.py --list-wake-words` to see every model available in your esp-sr version and
pick another with `--wake-word <model>`. The Boot button also starts a conversation.

## Protocol
See [docs/websocket.md](docs/websocket.md). The backend lives in `../backend`.
