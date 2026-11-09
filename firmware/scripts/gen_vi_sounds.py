#!/usr/bin/env python3
"""Records the built-in Vietnamese voice prompts (main/assets/locales/vi-VN/*.ogg) with Microsoft Edge TTS.

These clips are played by the firmware itself (activation code, Wi-Fi setup, upgrade, start-up), so the server's
voice setting does not touch them. They use the same voice as the server's Vietnamese voice (config.yaml, tts.voice_vi).
Format as the other prompts: OGG/Opus, mono, silence trimmed at both ends.

Needs: ffmpeg with libopus on PATH, `pip install edge-tts==7.3.1`, internet access.
Run: python3 scripts/gen_vi_sounds.py [--voice vi-VN-HoaiMyNeural] [--only welcome,activation]
"""
import argparse
import asyncio
import os
import subprocess
import sys

import edge_tts

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "main", "assets", "locales", "vi-VN")

# file -> (text, rate). Digits are read one by one when the activation code is announced, so they stay short and plain.
PROMPTS = {
    "0": ("không", "+0%"), "1": ("một", "+0%"), "2": ("hai", "+0%"), "3": ("ba", "+0%"), "4": ("bốn", "+0%"),
    "5": ("năm", "+0%"), "6": ("sáu", "+0%"), "7": ("bảy", "+0%"), "8": ("tám", "+0%"), "9": ("chín", "+0%"),
    "welcome": ("Xin chào, mình là Sophia!", "-5%"),
    "wificonfig": ("Hãy làm theo ba bước trên màn hình để cài Wi-Fi cho mình nhé.", "-5%"),
    "activation": ("Hãy nhập mã kích hoạt này vào trang quản trị.", "-5%"),
    "upgrade": ("Đang nâng cấp, vui lòng chờ.", "-5%"),
    "err_pin": ("Vui lòng cắm thẻ SIM.", "-5%"),
    "err_reg": ("Không thể truy cập mạng. Vui lòng kiểm tra thẻ SIM.", "-5%"),
}
TRIM = ("silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse,"
        "silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse")


def encode(mp3: bytes) -> bytes:
    p = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", "pipe:0", "-af", TRIM, "-ac", "1", "-ar", "16000", "-c:a", "libopus",
         "-b:a", "24k", "-vbr", "on", "-application", "voip", "-frame_duration", "60", "-f", "ogg", "pipe:1"],
        input=mp3, capture_output=True)
    if p.returncode != 0 or not p.stdout:
        raise RuntimeError(p.stderr.decode(errors="replace"))
    return p.stdout


async def speak(text: str, voice: str, rate: str) -> bytes:
    for attempt in range(8):
        data = bytearray()
        # the service sometimes keeps refusing one particular rate/text pair: fall back to the plain rate
        use_rate = rate if attempt < 3 else "+0%"
        try:
            async for chunk in edge_tts.Communicate(text, voice, rate=use_rate).stream():
                if chunk["type"] == "audio":
                    data += chunk["data"]
            if data:
                return bytes(data)
        except Exception as e:
            print(f"  attempt {attempt + 1} failed: {type(e).__name__}")
        await asyncio.sleep(2 + attempt)        # the service sometimes refuses a burst of requests
    raise RuntimeError(f"no audio for {text!r}")


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default="vi-VN-HoaiMyNeural")
    ap.add_argument("--only", default="", help="comma separated file names to record, default all")
    a = ap.parse_args()
    only = {n for n in a.only.split(",") if n}
    for name, (text, rate) in PROMPTS.items():
        if only and name not in only:
            continue
        ogg = encode(await speak(text, a.voice, rate))
        with open(os.path.join(OUT, f"{name}.ogg"), "wb") as f:
            f.write(ogg)
        print(f"{name:11s} {len(ogg):6d} bytes  {text}")
        await asyncio.sleep(1.2)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
