"""Checks how replies are cut into Vietnamese and English runs for the two voices.
Run from backend/:  python3 -m tests.test_tts_split   (needs no network)"""
import sys
import types

sys.modules.setdefault("edge_tts", types.ModuleType("edge_tts"))     # not needed to test the splitting
audio = types.ModuleType("app.audio")
audio.mp3_to_pcm = lambda *a, **k: b""
sys.modules.setdefault("app.audio", audio)

from app.tts import split_languages  # noqa: E402

CASES = [
    ("Chào bạn, hôm nay mình học gì nào?", [("vi", "Chào bạn, hôm nay mình học gì nào?")]),
    ('Từ "serendipity" nghĩa là sự tình cờ may mắn.',
     [("vi", "Từ"), ("en", "serendipity"), ("vi", "nghĩa là sự tình cờ may mắn.")]),
    ("Từ serendipity nghĩa là sự tình cờ may mắn.",
     [("vi", "Từ"), ("en", "serendipity"), ("vi", "nghĩa là sự tình cờ may mắn.")]),
    ("I would like a cup of coffee, please.", [("en", "I would like a cup of coffee, please.")]),
    ('Bạn hãy nói "I love you" nhé.', [("vi", "Bạn hãy nói"), ("en", "I love you"), ("vi", "nhé.")]),
    ("Hello!", [("en", "Hello!")]),
    ("Bạn đúng rồi, giỏi lắm.", [("vi", "Bạn đúng rồi, giỏi lắm.")]),
    ("Đáp án là 5 nhé", [("vi", "Đáp án là 5 nhé")]),
    ('Hôm nay chúng ta học từ mới "shutdown".',
     [("vi", "Hôm nay chúng ta học từ mới"), ("en", "shutdown .")]),
    ("Ví dụ: shutdown.", [("vi", "Ví dụ:"), ("en", "shutdown.")]),
    ("Ví dụ: cat", [("vi", "Ví dụ:"), ("en", "cat")]),
    ("Ví dụ: I shut down the computer.", [("vi", "Ví dụ:"), ("en", "I shut down the computer.")]),
    ("Ví dụ: ba con mèo", [("vi", "Ví dụ: ba con mèo")]),
]

bad = 0
for text, want in CASES:
    got = split_languages(text)
    if got != want:
        bad += 1
        print("FAIL", text, "\n  got ", got, "\n  want", want)
print(f"{len(CASES) - bad}/{len(CASES)} passed")
sys.exit(1 if bad else 0)
