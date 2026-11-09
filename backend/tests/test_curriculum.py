"""Checks the Học Online programme and the mode switches (patterns read out of app/session.py, no server packages needed).
Run from backend/:  python3 -m tests.test_curriculum"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from app import curriculum  # noqa: E402  (curriculum.py has no third-party imports)

bad = 0


def check(ok, what):
    global bad
    if not ok:
        bad += 1
        print("FAILED:", what)


# structure
ids = [x["id"] for x in curriculum.LESSONS]
check(len(ids) == len(set(ids)) == 24, "24 unique lessons")
for x in curriculum.LESSONS:
    check(5 <= len(x["words"]) <= 10, f"{x['id']}: 5 to 10 words")
    check(2 <= len(x["phrases"]) <= 4, f"{x['id']}: 2 to 4 sentences")
    check(all(x[k] for k in ("title", "goal", "role_play", "unit_title")), f"{x['id']}: texts filled")

# progress order
check(curriculum.current(set())["id"] == "u1l1", "first lesson is u1l1")
check(curriculum.current({"u1l1", "u1l2"})["id"] == "u1l3", "skips finished lessons")
check(curriculum.current({"u1l1", "u1l3"})["id"] == "u1l2", "a gap is filled first")
check(curriculum.current(set(ids)) is None, "all done -> None")
check(curriculum.label(curriculum.BY_ID["u1l3"]) == "Chủ đề 1, bài 3: Số đếm từ 1 đến 20", "label")
check("finish_lesson" in curriculum.teaching_brief(curriculum.LESSONS[0], 0), "brief mentions finish_lesson")

# the texts the tiles send
src = (Path(__file__).resolve().parent.parent / "app" / "session.py").read_text(encoding="utf-8")
ns = {"re": re}
exec(src[src.index("LEARN_ON = re.compile("):src.index("# \"Học Online\" only teaches")], ns)   # noqa: S102
LEARN_ON, CHAT_ON = ns["LEARN_ON"], ns["CHAT_ON"]
check(LEARN_ON.search("Chế độ học online.") and not CHAT_ON.search("Chế độ học online."), "learn tile text")
check(CHAT_ON.search("Chế độ trò chuyện.") and not LEARN_ON.search("Chế độ trò chuyện."), "chat tile text")
for t in ("Chế độ phiên dịch.", "Mình muốn luyện phát âm.", "Sophia", "Hello"):
    check(not LEARN_ON.search(t) and not CHAT_ON.search(t), f"{t!r} is neither")

print("all passed" if not bad else f"{bad} failed")
sys.exit(1 if bad else 0)
