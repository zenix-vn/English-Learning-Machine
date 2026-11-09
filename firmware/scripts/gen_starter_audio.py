#!/usr/bin/env python3
"""Speaks every starter word with Microsoft Edge TTS and writes main/offline/starter/starter_audio.{bin,idx}.

Same encoding as the server's study packs (backend/app/packbuild.py): OGG/Opus, mono, 16 kHz, 60 ms frames,
silence trimmed, so the device plays them with AudioService::PlaySound(). Clips are listed in card order.
  starter_audio.idx   per card: uint32 offset, uint32 length into starter_audio.bin (length 0 = no clip)

Needs: ffmpeg with libopus on PATH, and `pip install edge-tts==7.3.1`; internet access.
Run: python3 scripts/gen_starter_audio.py [--voice en-US-AriaNeural] [--bitrate 16k]
"""
import argparse
import asyncio
import os
import struct
import sys

import edge_tts

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_starter_pack import OUT, load_rows  # noqa: E402

TRIM = ("silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse,"
        "silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse")


async def speak(word: str, voice: str, bitrate: str) -> bytes:
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
            print(f"  {word!r} attempt {attempt + 1} failed: {e}")
        await asyncio.sleep(1 + attempt)
    return b""


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default="en-US-AriaNeural")
    ap.add_argument("--bitrate", default="16k")
    a = ap.parse_args()
    words = [r[0] for r in load_rows()]
    sem = asyncio.Semaphore(4)
    clips = [b""] * len(words)

    async def one(i):
        async with sem:
            clips[i] = await speak(words[i], a.voice, a.bitrate)

    await asyncio.gather(*(one(i) for i in range(len(words))))
    audio, idx = bytearray(), bytearray()
    for clip in clips:
        idx += struct.pack("<II", len(audio), len(clip))
        audio += clip
    missing = [w for w, c in zip(words, clips) if not c]
    with open(os.path.join(OUT, "starter_audio.bin"), "wb") as f:
        f.write(audio)
    with open(os.path.join(OUT, "starter_audio.idx"), "wb") as f:
        f.write(idx)
    print(f"{len(words) - len(missing)}/{len(words)} clips, {len(audio)} bytes" + (f", missing: {missing}" if missing else ""))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
