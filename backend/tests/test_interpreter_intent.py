"""Checks which utterances switch the interpreter mode on and off (the patterns are read out of app/session.py, so no
server packages are needed). Run from backend/:  python3 -m tests.test_interpreter_intent"""
import re
import sys
from pathlib import Path

src = (Path(__file__).resolve().parent.parent / "app" / "session.py").read_text(encoding="utf-8")
ns = {"re": re}
exec(src[src.index("INTERPRETER_ON = re.compile("):src.index("INTERPRETER_PROMPT")], ns)   # noqa: S102
ON, OFF = ns["INTERPRETER_ON"], ns["INTERPRETER_OFF"]

CASES_ON = ["Chế độ phiên dịch.", "bật chế độ phiên dịch", "Làm phiên dịch giúp mình", "phiên dịch giúp mình nhé",
            "interpreter mode please", "Act as an interpreter"]
CASES_OFF = ["thoát chế độ phiên dịch", "dừng phiên dịch", "Tắt phiên dịch đi", "stop translating", "exit interpreter"]
NEITHER = ["Dịch câu này sang tiếng Anh", "Hello how are you", "Tôi thích mèo", "what does serendipity mean"]
bad = 0
for t in CASES_ON:
    if not ON.search(t) or OFF.search(t):
        bad += 1
        print("ON expected:", t)
for t in CASES_OFF:
    if not OFF.search(t):
        bad += 1
        print("OFF expected:", t)
for t in NEITHER:
    if ON.search(t) or OFF.search(t):
        bad += 1
        print("neither expected:", t)
total = len(CASES_ON) + len(CASES_OFF) + len(NEITHER)
print(f"{total - bad}/{total} passed")
sys.exit(1 if bad else 0)
