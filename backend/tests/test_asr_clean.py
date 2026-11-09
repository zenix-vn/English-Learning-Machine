"""Time codes some audio models put in front of a transcript are removed.
Run from backend/:  python3 -m tests.test_asr_clean   (needs no network)"""
import sys
import types

sys.modules.setdefault("openai", types.SimpleNamespace(AsyncOpenAI=object))
from app.asr import clean_transcript  # noqa: E402

CASES = [
    ("00:01 Chào Sofia, hãy lên lịch họp vào lúc 10 giờ.", "Chào Sofia, hãy lên lịch họp vào lúc 10 giờ."),
    ("00:00 là gì vậy", "là gì vậy"),
    ("00:01 - 00:03", ""),
    ("[00:02] Nhớ đi ô tô", "Nhớ đi ô tô"),
    ("Hẹn gặp lúc 10:30 nhé", "Hẹn gặp lúc 10:30 nhé"),
    ("Hello there", "Hello there"),
]
bad = [(a, clean_transcript(a), b) for a, b in CASES if clean_transcript(a) != b]
print("FAIL" if bad else "ok", bad if bad else f"{len(CASES)} cases")
sys.exit(1 if bad else 0)
