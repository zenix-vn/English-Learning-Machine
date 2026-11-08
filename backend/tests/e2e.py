"""End-to-end test that behaves like the device: speaks real audio to the backend.

Run inside the backend container:
    docker exec english-learner python -m tests.e2e "What does serendipity mean?" ...
Each argument is synthesised to speech, sent as Opus frames, and the reply is checked.
"""
import asyncio
import json
import sys
import time

import aiohttp
import edge_tts

from app.audio import OpusDecoder, OpusEncoder, mp3_to_pcm

URL = "http://127.0.0.1:8000"
RATE, FRAME_MS = 16000, 60


async def speak(text: str, voice: str) -> bytes:
    mp3 = bytearray()
    async for c in edge_tts.Communicate(text, voice).stream():
        if c["type"] == "audio":
            mp3 += c["data"]
    return mp3_to_pcm(bytes(mp3), RATE)


async def main(utterances: list[str]):
    enc, dec = OpusEncoder(RATE, FRAME_MS), OpusDecoder(RATE, FRAME_MS)
    device = "aa:bb:cc:00:00:99"
    async with aiohttp.ClientSession() as http:
        async with http.post(f"{URL}/ota/", headers={"Device-Id": device, "Client-Id": "e2e"},
                             json={"application": {"version": "1.0.0"}}) as r:
            ota = await r.json()
        print("OTA ok:", list(ota))
        async with http.ws_connect(f"{URL}/ws/", headers={
                "Device-Id": device, "Client-Id": "e2e",
                "Authorization": "Bearer " + ota["websocket"]["token"]}) as ws:
            await ws.send_json({"type": "hello", "version": 1, "transport": "websocket",
                                "audio_params": {"format": "opus", "sample_rate": RATE,
                                                 "channels": 1, "frame_duration": FRAME_MS}})
            print("hello:", (await ws.receive_json())["type"])
            for text in utterances:
                voice = "vi-VN-HoaiMyNeural" if any(ord(ch) > 127 for ch in text) else "en-US-JennyNeural"
                pcm = await speak(text, voice) + b"\x00" * (RATE * 2 * 2)  # 2 s of silence ends it
                await ws.send_json({"type": "listen", "state": "start", "mode": "auto"})
                step = RATE * FRAME_MS // 1000 * 2
                t0 = time.monotonic()
                for i in range(0, len(pcm), step):
                    chunk = pcm[i:i + step].ljust(step, b"\x00")
                    await ws.send_bytes(enc.encode_pcm(chunk)[0])
                    await asyncio.sleep(0.005)
                heard, said, audio_ms, first = "", [], 0, None
                while True:
                    msg = await asyncio.wait_for(ws.receive(), 90)
                    if msg.type == aiohttp.WSMsgType.BINARY:
                        audio_ms += FRAME_MS
                        first = first or time.monotonic() - t0
                        dec.decode(msg.data)  # must be valid Opus
                    elif msg.type == aiohttp.WSMsgType.TEXT:
                        j = json.loads(msg.data)
                        if j["type"] == "stt":
                            heard = j["text"]
                        elif j["type"] == "tts" and j["state"] == "sentence_start":
                            said.append(j["text"])
                        elif j["type"] == "tts" and j["state"] == "stop":
                            break
                    else:
                        raise SystemExit("connection closed")
                print(f"\n> said:  {text}\n  heard: {heard}\n  reply: {' '.join(said)}\n"
                      f"  audio: {audio_ms / 1000:.1f}s, first audio after {first or 0:.1f}s")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1:]))
