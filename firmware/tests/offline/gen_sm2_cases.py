"""Writes sm2_cases.txt: random review sequences graded by the SERVER implementation (app.tools.apply_grade).

Run inside the backend container:  python gen_sm2_cases.py > sm2_cases.txt
Each line: "q1,q2,...|interval_minutes ease_milli reps" (the final state after the whole sequence).
"""
import random
import sqlite3

from app import tools

random.seed(20261108)
con = sqlite3.connect(":memory:")
con.execute("CREATE TABLE vocab(device TEXT, word TEXT, meaning TEXT, example TEXT, ease REAL DEFAULT 2.5, "
            "interval REAL DEFAULT 0, reps INTEGER DEFAULT 0, due INTEGER, created INTEGER, PRIMARY KEY(device, word))")
for _ in range(300):
    seq = [random.choice([0, 3, 4, 4, 4, 5]) for _ in range(random.randint(1, 14))]
    con.execute("DELETE FROM vocab")
    con.execute("INSERT INTO vocab(device,word,due,created) VALUES('d','w',0,0)")
    for q in seq:
        tools.apply_grade(con, "d", "w", q, 0)
    ease, interval, reps = con.execute("SELECT ease, interval, reps FROM vocab").fetchone()
    print(",".join(map(str, seq)) + f"|{round(interval * 1440)} {round(ease * 1000)} {reps}")
