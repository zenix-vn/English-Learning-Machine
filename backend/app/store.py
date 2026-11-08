"""SQLite storage shared with the admin page: devices, plans, conversation turns, commands.

The admin page (admin/app.py) writes the same database. Keep `activate()` and the schema in sync there.
"""
import calendar
import json
import random
import sqlite3
import time

from .config import DATA

DB_PATH = DATA / "app.db"
DAY = 86400


class RegistrationLimit(Exception):
    """Too many new devices registered from one IP address today."""


SCHEMA = """
CREATE TABLE IF NOT EXISTS devices(
    id TEXT PRIMARY KEY,
    name TEXT DEFAULT '',
    note TEXT DEFAULT '',
    status TEXT DEFAULT 'pending',      -- pending | approved | blocked
    first_seen INTEGER, last_seen INTEGER,
    last_ip TEXT DEFAULT '',
    first_ip TEXT DEFAULT '',
    fw_version TEXT DEFAULT '',
    board TEXT DEFAULT '',
    info TEXT DEFAULT '{}',             -- last OTA request details (JSON)
    online INTEGER DEFAULT 0,
    activation_code TEXT DEFAULT '',    -- shown on the device until it is activated
    activated_at INTEGER DEFAULT 0,     -- first time the device was allowed to run (0 = never)
    plan_id INTEGER DEFAULT 0,
    plan_started INTEGER DEFAULT 0,
    plan_expires INTEGER DEFAULT 0      -- 0 = never expires
);
CREATE TABLE IF NOT EXISTS plans(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE,
    description TEXT DEFAULT '',
    price INTEGER DEFAULT 0,            -- shown in the admin page only, no billing
    duration_days INTEGER DEFAULT 0,    -- 0 = no expiry
    daily_turns INTEGER DEFAULT 0,      -- 0 = unlimited
    monthly_turns INTEGER DEFAULT 0,    -- 0 = unlimited
    is_default INTEGER DEFAULT 0        -- assigned to newly activated devices
);
CREATE TABLE IF NOT EXISTS turns(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts INTEGER, device TEXT,
    user_text TEXT, reply_text TEXT,
    asr_ms INTEGER, latency_ms INTEGER,
    tools TEXT DEFAULT '',
    cost REAL DEFAULT 0,
    model TEXT DEFAULT '', asr_model TEXT DEFAULT '',
    ok INTEGER DEFAULT 1, error TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS turns_ts ON turns(ts);
CREATE INDEX IF NOT EXISTS turns_device ON turns(device, ts);
CREATE TABLE IF NOT EXISTS device_sync(      -- offline study uploads (see sync.py)
    device TEXT PRIMARY KEY, last_sync INTEGER, pack_id TEXT DEFAULT '',
    events INTEGER DEFAULT 0, last_events INTEGER DEFAULT 0
);
CREATE TABLE IF NOT EXISTS commands(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device TEXT, cmd TEXT, ts INTEGER, done INTEGER DEFAULT 0
);
"""

# Columns added after the first release, applied to databases created earlier.
MIGRATIONS = [
    ("devices", "first_ip", "TEXT DEFAULT ''"),
    ("devices", "activation_code", "TEXT DEFAULT ''"),
    ("devices", "activated_at", "INTEGER DEFAULT 0"),
    ("devices", "plan_id", "INTEGER DEFAULT 0"),
    ("devices", "plan_started", "INTEGER DEFAULT 0"),
    ("devices", "plan_expires", "INTEGER DEFAULT 0"),
]

DEFAULT_PLANS = [
    # name, description, price, duration_days, daily_turns, monthly_turns, is_default
    ("Dùng thử", "Trải nghiệm 7 ngày, tối đa 30 lượt mỗi ngày.", 0, 7, 30, 0, 1),
    ("Tiêu chuẩn", "30 ngày, tối đa 100 lượt mỗi ngày.", 0, 30, 100, 0, 0),
    ("Không giới hạn", "Không hết hạn, không giới hạn số lượt.", 0, 0, 0, 0, 0),
]


def connect() -> sqlite3.Connection:
    con = sqlite3.connect(DB_PATH, timeout=10)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA journal_mode=WAL")
    return con


def init():
    with connect() as con:
        con.executescript(SCHEMA)
        for table, column, ddl in MIGRATIONS:
            have = {r["name"] for r in con.execute(f"PRAGMA table_info({table})")}
            if column not in have:
                con.execute(f"ALTER TABLE {table} ADD COLUMN {column} {ddl}")
        if con.execute("SELECT COUNT(*) FROM plans").fetchone()[0] == 0:
            con.executemany("INSERT INTO plans(name,description,price,duration_days,daily_turns,monthly_turns,is_default) "
                            "VALUES(?,?,?,?,?,?,?)", DEFAULT_PLANS)
        con.execute("UPDATE devices SET online=0")   # nobody is connected after a restart
        # devices approved before plans existed: give them the default plan now
        for row in con.execute("SELECT id FROM devices WHERE status='approved' AND activated_at=0").fetchall():
            activate(con, row["id"])


def activate(con: sqlite3.Connection, device_id: str):
    """Mark a device approved. The first time, record the activation date and start the default plan."""
    now = int(time.time())
    dev = con.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    if dev is None:
        return
    if not dev["activated_at"]:
        con.execute("UPDATE devices SET activated_at=? WHERE id=?", (now, device_id))
    if not dev["plan_id"]:
        plan = con.execute("SELECT * FROM plans WHERE is_default=1 ORDER BY id LIMIT 1").fetchone()
        if plan:
            expires = now + plan["duration_days"] * DAY if plan["duration_days"] else 0
            con.execute("UPDATE devices SET plan_id=?, plan_started=?, plan_expires=? WHERE id=?",
                        (plan["id"], now, expires, device_id))
    con.execute("UPDATE devices SET status='approved', activation_code='' WHERE id=?", (device_id,))


def _new_code(con: sqlite3.Connection) -> str:
    while True:
        code = f"{random.SystemRandom().randrange(100000, 1000000)}"
        if not con.execute("SELECT 1 FROM devices WHERE activation_code=?", (code,)).fetchone():
            return code


def touch_device(device_id: str, ip: str = "", info: dict | None = None,
                 auto_approve: bool = False, max_new_per_ip: int = 0) -> sqlite3.Row:
    """Register the device on first contact (raises RegistrationLimit when abused), update it afterwards."""
    now = int(time.time())
    info = info or {}
    app = info.get("application") or {}
    board = info.get("board") or {}
    with connect() as con:
        row = con.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
        if row is None:
            if ip and max_new_per_ip:
                recent = con.execute("SELECT COUNT(*) FROM devices WHERE first_ip=? AND first_seen>=?",
                                     (ip, now - DAY)).fetchone()[0]
                if recent >= max_new_per_ip:
                    raise RegistrationLimit(ip)
            con.execute("INSERT INTO devices(id,status,first_seen,last_seen,first_ip,activation_code) "
                        "VALUES(?,?,?,?,?,?)", (device_id, "pending", now, now, ip, _new_code(con)))
            if auto_approve:
                activate(con, device_id)
        con.execute("UPDATE devices SET last_seen=?, last_ip=COALESCE(NULLIF(?,''),last_ip) WHERE id=?",
                    (now, ip, device_id))
        if info:
            con.execute("UPDATE devices SET fw_version=?, board=?, info=? WHERE id=?",
                        (app.get("version", ""), board.get("type") or board.get("name", ""),
                         json.dumps(info)[:4000], device_id))
        # a pending device that lost its code (e.g. admin reset) gets a new one
        dev = con.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
        if dev["status"] == "pending" and not dev["activation_code"]:
            con.execute("UPDATE devices SET activation_code=? WHERE id=?", (_new_code(con), device_id))
            dev = con.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
        return dev


def device_status(device_id: str) -> str:
    with connect() as con:
        row = con.execute("SELECT status FROM devices WHERE id=?", (device_id,)).fetchone()
    return row["status"] if row else "pending"


def set_online(device_id: str, online: bool):
    with connect() as con:
        con.execute("UPDATE devices SET online=?, last_seen=? WHERE id=?",
                    (1 if online else 0, int(time.time()), device_id))


def month_start(now: int, tz_offset: int) -> int:
    """Unix time of 00:00 on the first day of the current month in the server's timezone."""
    t = time.gmtime(now + tz_offset)
    return calendar.timegm((t.tm_year, t.tm_mon, 1, 0, 0, 0)) - tz_offset


def plan_quota(device_id: str, tz_offset: int) -> dict:
    """Whether the device may use the tutor right now, and why not."""
    now = int(time.time())
    today = now - (now + tz_offset) % DAY
    with connect() as con:
        dev = con.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
        plan = con.execute("SELECT * FROM plans WHERE id=?", (dev["plan_id"] if dev else 0,)).fetchone()
        if dev is None or plan is None:
            return {"allowed": True, "reason": "", "plan": ""}      # no plan assigned: no limits
        used_today = con.execute("SELECT COUNT(*) FROM turns WHERE device=? AND ts>=? AND user_text!=''",
                                 (device_id, today)).fetchone()[0]
        used_month = con.execute("SELECT COUNT(*) FROM turns WHERE device=? AND ts>=? AND user_text!=''",
                                 (device_id, month_start(now, tz_offset))).fetchone()[0]
    reason = ""
    if dev["plan_expires"] and now > dev["plan_expires"]:
        reason = "expired"
    elif plan["daily_turns"] and used_today >= plan["daily_turns"]:
        reason = "daily"
    elif plan["monthly_turns"] and used_month >= plan["monthly_turns"]:
        reason = "monthly"
    return {"allowed": not reason, "reason": reason, "plan": plan["name"],
            "used_today": used_today, "used_month": used_month,
            "daily_turns": plan["daily_turns"], "monthly_turns": plan["monthly_turns"],
            "expires": dev["plan_expires"]}


def record_turn(device: str, user_text: str, reply_text: str, asr_ms: int, latency_ms: int,
                tools: list[str], cost: float, model: str, asr_model: str,
                ok: bool = True, error: str = ""):
    with connect() as con:
        con.execute(
            "INSERT INTO turns(ts,device,user_text,reply_text,asr_ms,latency_ms,tools,cost,model,asr_model,ok,error) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
            (int(time.time()), device, user_text, reply_text, asr_ms, latency_ms,
             ",".join(tools), cost, model, asr_model, 1 if ok else 0, error[:300]))


def cost_since(ts: int) -> float:
    with connect() as con:
        return con.execute("SELECT COALESCE(SUM(cost),0) FROM turns WHERE ts>=?", (ts,)).fetchone()[0]


def pending_commands(device: str) -> list[sqlite3.Row]:
    with connect() as con:
        rows = con.execute("SELECT * FROM commands WHERE device=? AND done=0 ORDER BY id", (device,)).fetchall()
        if rows:
            con.execute("UPDATE commands SET done=1 WHERE device=? AND done=0", (device,))
    return rows


def prune(days: int):
    if days > 0:
        with connect() as con:
            con.execute("DELETE FROM turns WHERE ts<?", (int(time.time()) - days * DAY,))
            con.execute("DELETE FROM commands WHERE ts<?", (int(time.time()) - 7 * DAY,))
