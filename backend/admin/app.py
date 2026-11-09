#!/usr/bin/env python3
"""Admin page for the English Learner backend.

Runs on the host next to the backend container and shares its data directory:
  data/app.db      devices, conversation turns, remote commands   (written by both)
  data/vocab.db    vocabulary notebooks
  data/settings.json, data/llm.json   behaviour and models        (written here, read by the backend)
  data/firmware/   uploaded firmware images served through OTA
Only the Python standard library is used.
"""
import base64
import calendar
import csv
import hashlib
import hmac
import io
import json
import os
import re
import secrets
import sqlite3
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BASE = os.environ.get("EL_DIR", "/opt/english-learner")
DATA = os.path.join(BASE, "data")
APP_DB = os.path.join(DATA, "app.db")
VOCAB_DB = os.path.join(DATA, "vocab.db")
LLM_FILE = os.path.join(DATA, "llm.json")
SETTINGS_FILE = os.path.join(DATA, "settings.json")
ADMIN_FILE = os.path.join(DATA, "admin.json")
FIRMWARE_DIR = os.path.join(DATA, "firmware")
PACKS_DIR = os.path.join(DATA, "packs")
CONTAINER = os.environ.get("EL_CONTAINER", "english-learner")
HEALTH_URL = os.environ.get("EL_HEALTH", "http://127.0.0.1:18000/health")
ENV_PASSWORD = os.environ.get("ADMIN_PASSWORD", "")
TZ_OFFSET = int(os.environ.get("EL_TZ_MIN", "420")) * 60
PORT = int(os.environ.get("ADMIN_PORT", "18080"))
PREFIX = "/admin"
OR_URL = "https://openrouter.ai/api/v1"
DAY = 86400
MAX_FIRMWARE = 16 * 1024 * 1024
MAX_DECK = 8 * 1024 * 1024

SETTING_KEYS = {
    "prompt": str, "wake_greeting": str, "tts_voice": str, "tts_voice_vi": str, "tts_rate": str,
    "silence_ms": int, "temperature": float, "max_tokens": int, "history_messages": int,
    "approval_required": bool, "daily_cost_limit": float, "log_retention_days": int,
    "firmware_release": str, "default_deck": int, "max_new_per_ip_day": int,
}

_models = {"at": 0.0, "data": []}


# ----------------------------------------------------------------- files / db
def read_json(path, default):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def write_json(path, obj, mode=0o600):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False)
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def db(path=APP_DB):
    con = sqlite3.connect(path, timeout=10)
    con.row_factory = sqlite3.Row
    return con


def rows(sql, args=(), path=APP_DB):
    try:
        with db(path) as con:
            return [dict(r) for r in con.execute(sql, args).fetchall()]
    except sqlite3.OperationalError:
        return []          # table does not exist yet


def execute(sql, args=(), path=APP_DB):
    with db(path) as con:
        return con.execute(sql, args).rowcount


def month_start(now):
    t = time.gmtime(now + TZ_OFFSET)
    return calendar.timegm((t.tm_year, t.tm_mon, 1, 0, 0, 0)) - TZ_OFFSET


def day_start(now=None):
    now = int(now if now is not None else time.time())
    return now - (now + TZ_OFFSET) % DAY


# ----------------------------------------------------------- plans / activation
def activate(con, device_id):
    """Approve a device. The first time, record the activation date and start the default plan.
    Mirrors backend/app/store.py activate()."""
    now = int(time.time())
    dev = con.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    if dev is None:
        return False
    if not dev["activated_at"]:
        con.execute("UPDATE devices SET activated_at=? WHERE id=?", (now, device_id))
    if not dev["plan_id"]:
        plan = con.execute("SELECT * FROM plans WHERE is_default=1 ORDER BY id LIMIT 1").fetchone()
        if plan:
            expires = now + plan["duration_days"] * DAY if plan["duration_days"] else 0
            con.execute("UPDATE devices SET plan_id=?, plan_started=?, plan_expires=? WHERE id=?",
                        (plan["id"], now, expires, device_id))
    con.execute("UPDATE devices SET status='approved', activation_code='' WHERE id=?", (device_id,))
    return True


def plans_list():
    return rows("SELECT p.*, (SELECT COUNT(*) FROM devices d WHERE d.plan_id=p.id) devices "
                "FROM plans p ORDER BY p.id")


def plan_save(data):
    name = str(data.get("name", "")).strip()[:60]
    if not name:
        return {"error": "Cần đặt tên gói"}
    vals = []
    for key in ("price", "duration_days", "daily_turns", "monthly_turns"):
        try:
            v = int(data.get(key) or 0)
        except (TypeError, ValueError):
            return {"error": "Số không hợp lệ"}
        if v < 0:
            return {"error": "Số không được âm"}
        vals.append(v)
    desc = str(data.get("description", ""))[:200]
    try:
        with db() as con:
            if data.get("id"):
                con.execute("UPDATE plans SET name=?, description=?, price=?, duration_days=?, daily_turns=?, "
                            "monthly_turns=? WHERE id=?", (name, desc, *vals, int(data["id"])))
            else:
                con.execute("INSERT INTO plans(name,description,price,duration_days,daily_turns,monthly_turns) "
                            "VALUES(?,?,?,?,?,?)", (name, desc, *vals))
    except sqlite3.IntegrityError:
        return {"error": "Đã có gói trùng tên"}
    return {"ok": True}


def plan_delete(plan_id):
    used = rows("SELECT COUNT(*) n FROM devices WHERE plan_id=?", (plan_id,))
    if used and used[0]["n"]:
        return {"error": f"Còn {used[0]['n']} thiết bị đang dùng gói này. Hãy chuyển chúng sang gói khác trước."}
    plan = rows("SELECT is_default FROM plans WHERE id=?", (plan_id,))
    if plan and plan[0]["is_default"]:
        return {"error": "Đây là gói mặc định. Hãy đặt gói khác làm mặc định trước."}
    execute("DELETE FROM plans WHERE id=?", (plan_id,))
    return {"ok": True}


def plan_default(plan_id):
    with db() as con:
        con.execute("UPDATE plans SET is_default=0")
        con.execute("UPDATE plans SET is_default=1 WHERE id=?", (plan_id,))
    return {"ok": True}


def device_set_plan(device_id, plan_id, restart):
    with db() as con:
        plan = con.execute("SELECT * FROM plans WHERE id=?", (plan_id,)).fetchone() if plan_id else None
        if plan_id and plan is None:
            return {"error": "Không có gói này"}
        con.execute("UPDATE devices SET plan_id=? WHERE id=?", (plan_id, device_id))
        if restart and plan:
            now = int(time.time())
            expires = now + plan["duration_days"] * DAY if plan["duration_days"] else 0
            con.execute("UPDATE devices SET plan_started=?, plan_expires=? WHERE id=?", (now, expires, device_id))
    return {"ok": True}


def device_extend(device_id, days):
    now = int(time.time())
    with db() as con:
        dev = con.execute("SELECT plan_expires FROM devices WHERE id=?", (device_id,)).fetchone()
        if dev is None:
            return {"error": "Không có thiết bị này"}
        base = max(now, dev["plan_expires"] or 0)
        con.execute("UPDATE devices SET plan_expires=? WHERE id=?", (base + int(days) * DAY, device_id))
    return {"ok": True}


def activate_by_code(code):
    code = "".join(ch for ch in str(code) if ch.isdigit())
    if len(code) != 6:
        return {"error": "Mã kích hoạt gồm 6 chữ số"}
    with db() as con:
        dev = con.execute("SELECT id FROM devices WHERE activation_code=? AND status='pending'", (code,)).fetchone()
        if dev is None:
            return {"error": "Không tìm thấy thiết bị nào đang chờ với mã này"}
        activate(con, dev["id"])
    return {"ok": True, "id": dev["id"]}


# ----------------------------------------------------------------------- auth
def hash_password(password, salt):
    return hashlib.pbkdf2_hmac("sha256", password.encode(), bytes.fromhex(salt), 200_000).hex()


def check_password(password):
    saved = read_json(ADMIN_FILE, {})
    if saved.get("hash"):
        return hmac.compare_digest(hash_password(password, saved["salt"]), saved["hash"])
    return bool(ENV_PASSWORD) and hmac.compare_digest(password, ENV_PASSWORD)


def set_password(password):
    salt = secrets.token_hex(16)
    write_json(ADMIN_FILE, {"salt": salt, "hash": hash_password(password, salt)})


# ------------------------------------------------------------------- settings
def load_settings():
    return read_json(SETTINGS_FILE, {})


def save_settings(changes):
    cur = load_settings()
    for key, value in changes.items():
        if key not in SETTING_KEYS:
            continue
        kind = SETTING_KEYS[key]
        if kind is bool:
            cur[key] = bool(value)
        elif kind is str:
            cur[key] = str(value)
        elif value in ("", None):
            cur.pop(key, None)                      # back to the default
        else:
            cur[key] = kind(value)
    write_json(SETTINGS_FILE, cur)
    return cur


def config_defaults():
    """Defaults shipped with the backend, shown as placeholders in the settings form."""
    out = {"prompt": "", "wake_greeting": "", "tts_voice": "", "tts_voice_vi": "", "tts_rate": "", "silence_ms": 700,
           "temperature": 0.7, "max_tokens": 400, "history_messages": 16}
    try:
        out["prompt"] = open(os.path.join(BASE, "prompt.txt"), encoding="utf-8").read().strip()
        text = open(os.path.join(BASE, "config.yaml"), encoding="utf-8").read()
        for key, pat in (("tts_voice", r"voice:\s*(\S+)"), ("tts_voice_vi", r"voice_vi:\s*(\S+)"), ("tts_rate", r'rate:\s*"([^"]*)"'),
                         ("wake_greeting", r'wake_greeting:\s*"([^"]*)"')):
            m = re.search(pat, text)
            if m:
                out[key] = m.group(1)
    except OSError:
        pass
    return out


# --------------------------------------------------------------------- models
def fetch_models():
    if time.time() - _models["at"] < 1800 and _models["data"]:
        return _models["data"]
    req = urllib.request.Request(OR_URL + "/models", headers={"User-Agent": "english-learner-admin"})
    with urllib.request.urlopen(req, timeout=20) as r:
        raw = json.load(r)["data"]
    out = []
    for m in raw:
        arch = m.get("architecture") or {}
        if "text" not in (arch.get("output_modalities") or ["text"]):
            continue
        p = m.get("pricing") or {}
        try:
            pin, pout = float(p.get("prompt") or 0) * 1e6, float(p.get("completion") or 0) * 1e6
        except ValueError:
            continue
        if pin < 0 or pout < 0:
            continue
        out.append({
            "id": m["id"], "name": m.get("name", m["id"]), "ctx": m.get("context_length") or 0,
            "in": round(pin, 4), "out": round(pout, 4),
            "tools": "tools" in (m.get("supported_parameters") or []),
            "audio": "audio" in (arch.get("input_modalities") or []),
            "free": pin == 0 and pout == 0})
    out.sort(key=lambda x: (x["in"] + x["out"], x["name"]))
    _models.update(at=time.time(), data=out)
    return out


def test_model(model, key):
    body = json.dumps({"model": model, "max_tokens": 30,
                       "messages": [{"role": "user", "content": "Say 'ok' in one word."}]}).encode()
    req = urllib.request.Request(OR_URL + "/chat/completions", data=body, headers={
        "Authorization": "Bearer " + key, "Content-Type": "application/json",
        "User-Agent": "english-learner-admin"})
    t = time.time()
    try:
        with urllib.request.urlopen(req, timeout=40) as r:
            d = json.load(r)
        txt = (d.get("choices") or [{}])[0].get("message", {}).get("content", "")
        return {"ok": True, "reply": txt, "ms": int((time.time() - t) * 1000)}
    except urllib.error.HTTPError as e:
        return {"ok": False, "error": f"HTTP {e.code}: {e.read().decode(errors='replace')[:300]}"}
    except Exception as e:
        return {"ok": False, "error": str(e)}


def openrouter_credit(key):
    """Remaining OpenRouter credit, or None when it cannot be read."""
    try:
        req = urllib.request.Request(OR_URL + "/credits", headers={"Authorization": "Bearer " + key})
        with urllib.request.urlopen(req, timeout=8) as r:
            d = json.load(r)["data"]
        return {"total": d.get("total_credits"), "used": d.get("total_usage")}
    except Exception:
        return None


# ------------------------------------------------------------------- features
def backend_status():
    try:
        with urllib.request.urlopen(HEALTH_URL, timeout=4) as r:
            return "running" if r.status == 200 else f"http {r.status}"
    except Exception as e:
        return f"unreachable ({e})"


def overview():
    now = int(time.time())
    today = day_start(now)
    settings = {**config_defaults(), **load_settings()}
    llm = read_json(LLM_FILE, {})

    def agg(since):
        r = rows("SELECT COUNT(*) n, COALESCE(SUM(cost),0) cost, "
                 "COALESCE(AVG(NULLIF(latency_ms,0)),0) lat, COALESCE(SUM(1-ok),0) err "
                 "FROM turns WHERE ts>=?", (since,))
        return r[0] if r else {"n": 0, "cost": 0, "lat": 0, "err": 0}

    series = rows("SELECT (ts+?)/? AS d, COUNT(*) n, COALESCE(SUM(cost),0) cost FROM turns "
                  "WHERE ts>=? GROUP BY d ORDER BY d", (TZ_OFFSET, DAY, today - 13 * DAY))
    by_day = {s["d"]: s for s in series}
    first = (today + TZ_OFFSET) // DAY - 13
    days = [{"day": time.strftime("%d/%m", time.gmtime((first + i) * DAY)),
             "n": by_day.get(first + i, {}).get("n", 0),
             "cost": by_day.get(first + i, {}).get("cost", 0)} for i in range(14)]
    tools = {}
    for r in rows("SELECT tools FROM turns WHERE ts>=? AND tools!=''", (today - 6 * DAY,)):
        for t in r["tools"].split(","):
            tools[t] = tools.get(t, 0) + 1
    dev = rows("SELECT COUNT(*) total, COALESCE(SUM(online),0) online, "
               "COALESCE(SUM(status='pending'),0) pending, COALESCE(SUM(status='blocked'),0) blocked, "
               "COALESCE(SUM(status='approved' AND plan_expires>0 AND plan_expires<?),0) expired, "
               "COALESCE(SUM(status='approved' AND plan_expires>=? AND plan_expires<?),0) expiring "
               "FROM devices", (now, now, now + 7 * DAY))
    return {
        "backend": backend_status(),
        "devices": dev[0] if dev else {"total": 0, "online": 0, "pending": 0, "blocked": 0, "expired": 0, "expiring": 0},
        "today": agg(today), "week": agg(today - 6 * DAY), "month": agg(today - 29 * DAY),
        "days": days, "tools": sorted(tools.items(), key=lambda kv: -kv[1]),
        "limit": float(settings.get("daily_cost_limit") or 0),
        "model": llm.get("model", ""), "chat_model": llm.get("chat_model", ""), "asr_model": llm.get("asr_model", ""),
        "credit": openrouter_credit(llm["api_key"]) if llm.get("api_key") else None,
    }


def devices():
    now = int(time.time())
    out = rows(
        "SELECT d.*, COALESCE(p.name,'') plan_name, COALESCE(p.daily_turns,0) plan_daily, "
        "COALESCE(p.monthly_turns,0) plan_monthly, "
        "(SELECT COUNT(*) FROM turns t WHERE t.device=d.id) turns, "
        "(SELECT COALESCE(SUM(cost),0) FROM turns t WHERE t.device=d.id) cost, "
        "(SELECT COUNT(*) FROM turns t WHERE t.device=d.id AND t.ts>=? AND t.user_text!='') turns_today, "
        "(SELECT COUNT(*) FROM turns t WHERE t.device=d.id AND t.ts>=? AND t.user_text!='') turns_month, "
        "COALESCE((SELECT last_sync FROM device_sync s WHERE s.device=d.id),0) last_sync, "
        "COALESCE((SELECT events FROM device_sync s WHERE s.device=d.id),0) sync_events "
        "FROM devices d LEFT JOIN plans p ON p.id=d.plan_id "
        "ORDER BY d.online DESC, d.last_seen DESC", (day_start(now), month_start(now)))
    for d in out:
        if d["status"] == "blocked":
            d["state"] = "blocked"
        elif d["status"] != "approved":
            d["state"] = "pending"
        elif d["plan_expires"] and now > d["plan_expires"]:
            d["state"] = "expired"
        elif (d["plan_daily"] and d["turns_today"] >= d["plan_daily"]) or \
                (d["plan_monthly"] and d["turns_month"] >= d["plan_monthly"]):
            d["state"] = "limit"
        else:
            d["state"] = "online" if d["online"] else "idle"
    assigned = {r["device"]: r["deck_id"] for r in rows("SELECT device, deck_id FROM device_deck", (), VOCAB_DB)}
    notebook = {r["device"]: r for r in rows(
        "SELECT device, COUNT(*) words, COALESCE(SUM(due<=?),0) due FROM vocab GROUP BY device",
        (int(time.time()),), VOCAB_DB)}
    for d in out:
        d["deck_id"] = assigned.get(d["id"], 0)
        d["notebook"] = notebook.get(d["id"], {}).get("words", 0)
        d["notebook_due"] = notebook.get(d["id"], {}).get("due", 0)
        try:
            info = json.loads(d.pop("info") or "{}")
        except ValueError:
            info = {}
        board = info.get("board") or {}
        d["details"] = {
            "chip": info.get("chip_model_name", ""), "flash": info.get("flash_size", 0),
            "language": info.get("language", ""), "ssid": board.get("ssid", ""),
            "rssi": board.get("rssi"), "local_ip": board.get("ip", ""),
            "uuid": info.get("uuid", ""),
        }
    return out


def turns(device, q, limit, before):
    sql, args = "SELECT * FROM turns WHERE 1=1", []
    if device:
        sql += " AND device=?"
        args.append(device)
    if q:
        sql += " AND (user_text LIKE ? OR reply_text LIKE ?)"
        args += [f"%{q}%"] * 2
    if before:
        sql += " AND id<?"
        args.append(before)
    sql += " ORDER BY id DESC LIMIT ?"
    args.append(min(limit, 200))
    return rows(sql, args)


def vocab(device):
    sql = "SELECT * FROM vocab" + (" WHERE device=?" if device else "") + " ORDER BY due"
    return rows(sql, (device,) if device else (), VOCAB_DB)


def vocab_csv(device):
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(["device", "word", "meaning", "example", "repetitions", "interval_days", "due"])
    for r in vocab(device):
        w.writerow([r["device"], r["word"], r["meaning"], r["example"], r["reps"],
                    round(r["interval"], 2), time.strftime("%Y-%m-%d", time.gmtime(r["due"] + TZ_OFFSET))])
    return buf.getvalue().encode("utf-8-sig")


# ---------------------------------------------------------------------- decks
DECK_DDL = """
CREATE TABLE IF NOT EXISTS decks(
    id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE, description TEXT DEFAULT '',
    position INTEGER DEFAULT 0, words INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS deck_words(
    deck_id INTEGER, rank INTEGER, word TEXT, ipa TEXT, meaning TEXT, example TEXT,
    PRIMARY KEY(deck_id, rank));
CREATE TABLE IF NOT EXISTS device_deck(device TEXT PRIMARY KEY, deck_id INTEGER);
CREATE TABLE IF NOT EXISTS vocab(
    device TEXT, word TEXT, meaning TEXT, example TEXT,
    ease REAL DEFAULT 2.5, interval REAL DEFAULT 0, reps INTEGER DEFAULT 0,
    due INTEGER, created INTEGER, PRIMARY KEY(device, word));
"""


def deck_con():
    con = db(VOCAB_DB)
    con.executescript(DECK_DDL)       # keep in sync with backend/app/tools.py
    return con


def parse_deck(data):
    """TSV with columns word, IPA, meaning, example. Lines starting with # are comments."""
    words, bad = [], 0
    for line in data.decode("utf-8-sig").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3 or not parts[0].strip() or not parts[2].strip():
            bad += 1
            continue
        parts += [""] * (4 - len(parts))
        words.append([p.strip() for p in parts[:4]])
    return words, bad


def deck_import(name, description, position, data):
    name = (name or "").strip()
    if not name or len(name) > 80:
        return {"error": "Cần đặt tên bộ từ (tối đa 80 ký tự)"}
    words, bad = parse_deck(data)
    if not words:
        return {"error": "File không có dòng từ vựng hợp lệ (cần 4 cột cách nhau bằng Tab: từ, phiên âm, nghĩa, ví dụ)"}
    with deck_con() as con:
        old = con.execute("SELECT id FROM decks WHERE name=?", (name,)).fetchone()
        if old:                                    # re-importing replaces the deck, keeping its id
            deck_id = old["id"]
            con.execute("DELETE FROM deck_words WHERE deck_id=?", (deck_id,))
            con.execute("UPDATE decks SET description=?, position=? WHERE id=?",
                        (description, position, deck_id))
        else:
            deck_id = con.execute("INSERT INTO decks(name,description,position) VALUES(?,?,?)",
                                  (name, description, position)).lastrowid
        con.executemany("INSERT OR IGNORE INTO deck_words(deck_id,rank,word,ipa,meaning,example) VALUES(?,?,?,?,?,?)",
                        [(deck_id, i, *w) for i, w in enumerate(words, 1)])
        con.execute("UPDATE decks SET words=? WHERE id=?", (len(words), deck_id))
    return {"ok": True, "id": deck_id, "words": len(words), "skipped": bad}


def decks_overview():
    with deck_con() as con:
        decks = [dict(r) for r in con.execute("SELECT * FROM decks ORDER BY position, id")]
        assigned = {r["deck_id"]: r["n"] for r in con.execute(
            "SELECT deck_id, COUNT(*) n FROM device_deck GROUP BY deck_id")}
        total_learned = con.execute("SELECT COUNT(DISTINCT word) FROM vocab").fetchone()[0]
    for d in decks:
        d["devices"] = assigned.get(d["id"], 0)
    return {"decks": decks, "default": load_settings().get("default_deck", 0), "notebook_words": total_learned}


def deck_words(deck_id, q, offset):
    sql, args = "SELECT rank, word, ipa, meaning, example FROM deck_words WHERE deck_id=?", [deck_id]
    if q:
        sql += " AND word LIKE ?"
        args.append(q + "%")
    sql += " ORDER BY rank LIMIT 40 OFFSET ?"
    args.append(offset)
    with deck_con() as con:
        return [dict(r) for r in con.execute(sql, args)]


# ------------------------------------------------------------- offline packs
STORE_BYTES = 4 * 1024 * 1024          # size of the device's `store` flash partition (partitions/v2/16m.csv)


def packs_overview():
    """Per deck: the current offline pack, the state of the latest build, and the devices that sync it."""
    current = read_json(os.path.join(PACKS_DIR, "current.json"), {})
    decks = []
    with deck_con() as con:
        for d in con.execute("SELECT id, name, words FROM decks ORDER BY position, id"):
            pack_id = current.get(str(d["id"]))
            mf = read_json(os.path.join(PACKS_DIR, pack_id or "-", "manifest.json"), None) if pack_id else None
            status = read_json(os.path.join(PACKS_DIR, f"build-{d['id']}.json"), {})
            decks.append({"id": d["id"], "name": d["name"], "words": d["words"], "pack": mf, "build": status})
    sync = rows("SELECT d.id, d.name, s.last_sync, s.pack_id, s.events FROM devices d "
                "LEFT JOIN device_sync s ON s.device=d.id WHERE d.status='approved' ORDER BY s.last_sync DESC")
    return {"decks": decks, "sync": sync, "store_bytes": STORE_BYTES}


def pack_build(deck_id, count, bitrate):
    status_file = os.path.join(PACKS_DIR, f"build-{deck_id}.json")
    if read_json(status_file, {}).get("state") == "running" and time.time() - read_json(status_file, {}).get("started", 0) < 3 * 3600:
        return {"error": "Gói của bộ từ này đang được dựng"}
    if bitrate not in ("12k", "16k", "24k"):
        return {"error": "Bitrate không hợp lệ"}
    count = max(1, min(int(count), 6000))
    os.makedirs(PACKS_DIR, exist_ok=True)
    write_json(status_file, {"state": "running", "done": 0, "total": count, "started": int(time.time()), "error": ""}, 0o644)
    subprocess.Popen(["docker", "exec", "-d", CONTAINER, "python", "-m", "app.packbuild", "--deck", str(deck_id),
                      "--count", str(count), "--bitrate", bitrate], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {"ok": True}


def pack_delete(deck_id):
    current_file = os.path.join(PACKS_DIR, "current.json")
    current = read_json(current_file, {})
    pack_id = current.pop(str(deck_id), None)
    if pack_id and re.fullmatch(r"d\d+-\d+-[0-9a-f]{10}", pack_id):
        import shutil
        shutil.rmtree(os.path.join(PACKS_DIR, pack_id), ignore_errors=True)
    write_json(current_file, current, 0o644)
    return {"ok": True}


def firmware_list():
    out = []
    if os.path.isdir(FIRMWARE_DIR):
        for name in sorted(os.listdir(FIRMWARE_DIR)):
            m = re.match(r"^english-learner_([0-9][A-Za-z0-9.\-]*)\.bin$", name)
            if m:
                st = os.stat(os.path.join(FIRMWARE_DIR, name))
                out.append({"version": m.group(1), "size": st.st_size, "mtime": int(st.st_mtime)})
    out.sort(key=lambda f: [int(x) for x in re.findall(r"\d+", f["version"])] or [0], reverse=True)
    return {"files": out, "release": load_settings().get("firmware_release", "")}


def firmware_save(version, data):
    if not re.fullmatch(r"[0-9][A-Za-z0-9.\-]{0,20}", version or ""):
        return {"error": "Phiên bản không hợp lệ (ví dụ 1.0.1)"}
    if not data or len(data) > MAX_FIRMWARE:
        return {"error": "File rỗng hoặc lớn hơn 16 MB"}
    if data[0] != 0xE9:
        return {"error": "Không phải file firmware ESP32 (byte đầu phải là 0xE9). Hãy tải file app .bin, không phải bootloader hay merged."}
    os.makedirs(FIRMWARE_DIR, exist_ok=True)
    path = os.path.join(FIRMWARE_DIR, f"english-learner_{version}.bin")
    with open(path + ".tmp", "wb") as f:
        f.write(data)
    os.replace(path + ".tmp", path)
    os.chmod(path, 0o644)
    return {"ok": True}


def firmware_delete(version):
    if not re.fullmatch(r"[0-9][A-Za-z0-9.\-]*", version or ""):
        return {"error": "bad version"}
    try:
        os.remove(os.path.join(FIRMWARE_DIR, f"english-learner_{version}.bin"))
    except OSError:
        return {"error": "Không tìm thấy file"}
    if load_settings().get("firmware_release") == version:
        save_settings({"firmware_release": ""})
    return {"ok": True}


def container_logs(lines):
    try:
        r = subprocess.run(["docker", "logs", "--tail", str(min(lines, 1000)), CONTAINER],
                           capture_output=True, text=True, timeout=15)
        text = (r.stdout + r.stderr).splitlines()
    except Exception as e:
        return [f"Không đọc được log: {e}"]
    noise = ("httpx", "aiohttp.access", "HTTP Request:")
    return [l for l in text if not any(n in l for n in noise)]


# ----------------------------------------------------------------- http layer
class H(BaseHTTPRequestHandler):
    server_version = "ELAdmin"

    def log_message(self, *a):
        pass

    def _auth(self):
        h = self.headers.get("Authorization", "")
        if h.startswith("Basic "):
            try:
                user, _, pw = base64.b64decode(h[6:]).decode().partition(":")
                if user == "admin" and check_password(pw):
                    return True
            except Exception:
                pass
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="English Learner Admin"')
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def _send(self, body, ctype, code=200, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(json.dumps(obj, ensure_ascii=False).encode(), "application/json", code)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    # -- routing
    def do_GET(self):
        if not self._auth():
            return
        url = urllib.parse.urlparse(self.path)
        path, q = url.path, {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
        try:
            if path in (PREFIX, PREFIX + "/"):
                with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html"), "rb") as f:
                    return self._send(f.read(), "text/html; charset=utf-8")
            if path == PREFIX + "/api/models":
                return self._json({"models": fetch_models()})
            if path == PREFIX + "/api/state":
                s = read_json(LLM_FILE, {})
                k = s.get("api_key", "")
                return self._json({"model": s.get("model", ""), "chat_model": s.get("chat_model", ""),
                                   "asr_model": s.get("asr_model", ""),
                                   "has_key": bool(k), "key_hint": ("…" + k[-4:]) if k else "",
                                   "backend": backend_status()})
            if path == PREFIX + "/api/overview":
                return self._json(overview())
            if path == PREFIX + "/api/devices":
                return self._json({"devices": devices()})
            if path == PREFIX + "/api/turns":
                return self._json({"turns": turns(q.get("device"), q.get("q"), int(q.get("limit", 50)),
                                                  int(q.get("before", 0) or 0))})
            if path == PREFIX + "/api/vocab":
                return self._json({"words": vocab(q.get("device"))})
            if path == PREFIX + "/api/packs":
                return self._json(packs_overview())
            if path == PREFIX + "/api/plans":
                return self._json({"plans": plans_list()})
            if path == PREFIX + "/api/decks":
                return self._json(decks_overview())
            if path == PREFIX + "/api/decks/words":
                return self._json({"words": deck_words(int(q.get("deck", 0)), q.get("q", ""), int(q.get("offset", 0)))})
            if path == PREFIX + "/api/vocab.csv":
                return self._send(vocab_csv(q.get("device")), "text/csv; charset=utf-8", extra={
                    "Content-Disposition": 'attachment; filename="vocabulary.csv"'})
            if path == PREFIX + "/api/settings":
                return self._json({"settings": load_settings(), "defaults": config_defaults()})
            if path == PREFIX + "/api/firmware":
                return self._json(firmware_list())
            if path == PREFIX + "/api/logs":
                return self._json({"lines": container_logs(int(q.get("lines", 200)))})
        except Exception as e:
            return self._json({"error": str(e)}, 500)
        self.send_error(404)

    def do_PUT(self):
        if not self._auth():
            return
        url = urllib.parse.urlparse(self.path)
        if url.path == PREFIX + "/api/firmware":
            version = urllib.parse.parse_qs(url.query).get("version", [""])[0]
            n = int(self.headers.get("Content-Length") or 0)
            if n > MAX_FIRMWARE:
                return self._json({"error": "File lớn hơn 16 MB"}, 413)
            return self._json(firmware_save(version, self.rfile.read(n)))
        if url.path == PREFIX + "/api/decks/import":
            qs = urllib.parse.parse_qs(url.query)
            n = int(self.headers.get("Content-Length") or 0)
            if n > MAX_DECK:
                return self._json({"error": "File lớn hơn 8 MB"}, 413)
            return self._json(deck_import(qs.get("name", [""])[0], qs.get("description", [""])[0],
                                          int(qs.get("position", ["0"])[0] or 0), self.rfile.read(n)))
        self.send_error(404)

    def do_POST(self):
        if not self._auth():
            return
        try:
            data = json.loads(self._body() or b"{}")
        except ValueError:
            return self._json({"error": "bad json"}, 400)
        path = self.path.split("?")[0]
        try:
            return self._post(path, data)
        except Exception as e:
            return self._json({"error": str(e)}, 500)

    def _post(self, path, data):
        a = path[len(PREFIX):]
        llm = read_json(LLM_FILE, {})
        if a == "/api/test":
            model = (data.get("model") or "").strip()
            key = (data.get("api_key") or "").strip() or llm.get("api_key", "")
            if not model or not key:
                return self._json({"ok": False, "error": "Thiếu model hoặc API key"})
            return self._json(test_model(model, key))
        if a == "/api/save":
            model = (data.get("model") or "").strip()
            key = (data.get("api_key") or "").strip() or llm.get("api_key", "")
            if not model or not key:
                return self._json({"error": "Thiếu model hoặc API key"}, 400)
            llm.update(model=model, api_key=key)
            if (data.get("asr_model") or "").strip():
                llm["asr_model"] = data["asr_model"].strip()
            if "chat_model" in data:                # empty = the free conversation uses the lessons' model
                llm["chat_model"] = (data.get("chat_model") or "").strip()
            write_json(LLM_FILE, llm)
            return self._json({"ok": True})
        if a == "/api/settings":
            return self._json({"ok": True, "settings": save_settings(data)})
        if a == "/api/device/update":
            did, fields = data.get("id", ""), []
            args = []
            for col in ("name", "note"):
                if col in data:
                    fields.append(f"{col}=?")
                    args.append(str(data[col])[:200])
            status = data.get("status")
            if status in ("blocked", "pending"):
                fields.append("status=?")
                args.append(status)
            if not did or not (fields or status == "approved"):
                return self._json({"error": "Không có gì để cập nhật"}, 400)
            with db() as con:
                if fields:
                    con.execute(f"UPDATE devices SET {','.join(fields)} WHERE id=?", args + [did])
                if status == "approved":
                    activate(con, did)
            return self._json({"ok": True})
        if a == "/api/packs/build":
            return self._json(pack_build(int(data.get("deck_id", 0)), data.get("count", 2000), str(data.get("bitrate", "16k"))))
        if a == "/api/packs/delete":
            return self._json(pack_delete(int(data.get("deck_id", 0))))
        if a == "/api/plans/save":
            return self._json(plan_save(data))
        if a == "/api/plans/delete":
            return self._json(plan_delete(int(data.get("id", 0))))
        if a == "/api/plans/default":
            return self._json(plan_default(int(data.get("id", 0))))
        if a == "/api/device/plan":
            return self._json(device_set_plan(data.get("id", ""), int(data.get("plan_id", 0)), bool(data.get("restart"))))
        if a == "/api/device/extend":
            return self._json(device_extend(data.get("id", ""), int(data.get("days", 0))))
        if a == "/api/device/activate":
            return self._json(activate_by_code(data.get("code", "")))
        if a == "/api/device/command":
            if data.get("cmd") not in ("reboot", "disconnect") or not data.get("id"):
                return self._json({"error": "Lệnh không hợp lệ"}, 400)
            execute("INSERT INTO commands(device,cmd,ts) VALUES(?,?,?)",
                    (data["id"], data["cmd"], int(time.time())))
            return self._json({"ok": True})
        if a == "/api/device/delete":
            did = data.get("id", "")
            if not did:
                return self._json({"error": "missing id"}, 400)
            execute("DELETE FROM devices WHERE id=?", (did,))
            if data.get("purge"):
                execute("DELETE FROM turns WHERE device=?", (did,))
                execute("DELETE FROM vocab WHERE device=?", (did,), VOCAB_DB)
            return self._json({"ok": True})
        if a == "/api/turns/clear":
            n = execute("DELETE FROM turns WHERE ts<?", (int(time.time()) - int(data.get("older_days", 0)) * DAY,))
            return self._json({"ok": True, "deleted": n})
        if a == "/api/vocab/delete":
            execute("DELETE FROM vocab WHERE device=? AND word=?", (data.get("device", ""), data.get("word", "")),
                    VOCAB_DB)
            return self._json({"ok": True})
        if a == "/api/vocab/update":
            execute("UPDATE vocab SET meaning=?, example=? WHERE device=? AND word=?",
                    (data.get("meaning", ""), data.get("example", ""), data.get("device", ""), data.get("word", "")),
                    VOCAB_DB)
            return self._json({"ok": True})
        if a == "/api/vocab/reset":
            execute("UPDATE vocab SET due=?, reps=0, interval=0 WHERE device=? AND word=?",
                    (int(time.time()), data.get("device", ""), data.get("word", "")), VOCAB_DB)
            return self._json({"ok": True})
        if a == "/api/decks/delete":
            deck_id = int(data.get("id", 0))
            with deck_con() as con:
                con.execute("DELETE FROM deck_words WHERE deck_id=?", (deck_id,))
                con.execute("DELETE FROM device_deck WHERE deck_id=?", (deck_id,))
                con.execute("DELETE FROM decks WHERE id=?", (deck_id,))
            if load_settings().get("default_deck") == deck_id:
                save_settings({"default_deck": ""})
            return self._json({"ok": True})
        if a == "/api/decks/default":
            save_settings({"default_deck": int(data.get("id", 0)) or ""})
            return self._json({"ok": True})
        if a == "/api/device/deck":
            deck_id = int(data.get("deck_id", 0))
            with deck_con() as con:
                if deck_id:
                    con.execute("INSERT INTO device_deck(device,deck_id) VALUES(?,?) "
                                "ON CONFLICT(device) DO UPDATE SET deck_id=excluded.deck_id", (data.get("id", ""), deck_id))
                else:
                    con.execute("DELETE FROM device_deck WHERE device=?", (data.get("id", ""),))
            return self._json({"ok": True})
        if a == "/api/firmware/release":
            version = data.get("version", "")
            if version and not any(f["version"] == version for f in firmware_list()["files"]):
                return self._json({"error": "Không có file firmware này"}, 400)
            save_settings({"firmware_release": version})
            return self._json({"ok": True})
        if a == "/api/firmware/delete":
            return self._json(firmware_delete(data.get("version", "")))
        if a == "/api/password":
            if not check_password(data.get("current", "")):
                return self._json({"error": "Mật khẩu hiện tại không đúng"}, 400)
            new = data.get("new", "")
            if len(new) < 10:
                return self._json({"error": "Mật khẩu mới cần ít nhất 10 ký tự"}, 400)
            set_password(new)
            return self._json({"ok": True})
        self.send_error(404)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
