"""HTTP API the device uses to download its offline pack and upload offline study results.

    GET  /sync/manifest             -> manifest of the pack for the device's deck
    GET  /sync/file/{pack}/{name}   -> a pack file (supports Range, so a download can resume)
    GET  /sync/state?pack=<id>      -> notebook progress already made online for words of the pack
    POST /sync/progress             -> offline reviews: {"pack": id, "events": [[card_index, quality, unix_ts], ...]}
All calls need the device token from /ota/ and an activated device whose plan has not expired.
"""
import asyncio
import json
import logging
import re
import struct
import time

from aiohttp import web

from . import store, tools
from .config import DATA, check_token

log = logging.getLogger("sync")
PACKS = DATA / "packs"
PACK_ID = re.compile(r"^d\d+-\d+-[0-9a-f]{10}$")
FILE_NAMES = {"cards.bin", "cards.idx", "audio.bin", "audio.idx"}
MAX_EVENTS = 20000


def _run(fn, *args):
    return asyncio.get_running_loop().run_in_executor(None, fn, *args)


async def authorize(request: web.Request) -> str:
    """Returns the device id or raises an HTTP error."""
    device_id = request.headers.get("Device-Id", "")
    token = request.headers.get("Authorization", "").removeprefix("Bearer ").strip()
    if not device_id or not check_token(device_id, token):
        raise web.HTTPUnauthorized(text="invalid token")
    if await _run(store.device_status, device_id) != "approved":
        raise web.HTTPForbidden(text="device not activated")
    quota = await _run(store.plan_quota, device_id, request.app["config"]["server"]["timezone_offset_minutes"] * 60)
    if quota["reason"] == "expired":
        raise web.HTTPForbidden(text="plan expired")
    return device_id


def _current_pack(device_id: str):
    """The (pack_id, manifest) built for the deck this device studies, or (None, None)."""
    with tools._db() as con:
        deck = tools._deck_for(con, device_id)
    if deck is None:
        return None, None
    try:
        pack_id = json.loads((PACKS / "current.json").read_text()).get(str(deck[0]))
        if pack_id and PACK_ID.match(pack_id):
            return pack_id, json.loads((PACKS / pack_id / "manifest.json").read_text())
    except (OSError, ValueError):
        pass
    return None, None


async def manifest(request: web.Request) -> web.Response:
    device_id = await authorize(request)
    pack_id, mf = await _run(_current_pack, device_id)
    if mf is None:
        return web.json_response({"error": "no offline pack has been built for this device's deck"}, status=404)
    return web.json_response({"pack": mf, "server_time": int(time.time())})


async def pack_file(request: web.Request) -> web.StreamResponse:
    await authorize(request)
    pack, name = request.match_info["pack"], request.match_info["name"]
    path = PACKS / pack / name
    if not PACK_ID.match(pack) or name not in FILE_NAMES or not path.is_file():
        raise web.HTTPNotFound()
    return web.FileResponse(path)


def _pack_words(pack_id: str) -> list[str]:
    """Lower-case word of every card of a pack, in card order."""
    ranks_raw = (PACKS / pack_id / "ranks.bin").read_bytes()
    ranks = struct.unpack(f"<{len(ranks_raw) // 4}I", ranks_raw)
    mf = json.loads((PACKS / pack_id / "manifest.json").read_text())
    with tools._db() as con:
        by_rank = dict(con.execute("SELECT rank, lower(word) FROM deck_words WHERE deck_id=?", (mf["deck_id"],)))
    return [by_rank.get(r, "") for r in ranks]


def _state(device_id: str, pack_id: str) -> list:
    words = _pack_words(pack_id)
    with tools._db() as con:
        rows = {w: (due, interval, ease, reps) for w, due, interval, ease, reps in con.execute(
            "SELECT word, due, interval, ease, reps FROM vocab WHERE device=? AND reps>0", (device_id,))}
    return [[i, due, round(interval, 4), round(ease, 3), reps]
            for i, w in enumerate(words) if w in rows for (due, interval, ease, reps) in [rows[w]]]


async def state(request: web.Request) -> web.Response:
    device_id = await authorize(request)
    pack_id = request.query.get("pack", "")
    if not PACK_ID.match(pack_id) or not (PACKS / pack_id).is_dir():
        raise web.HTTPNotFound()
    items = await _run(_state, device_id, pack_id)
    return web.json_response({"items": items})


def _apply(device_id: str, pack_id: str, events: list) -> int:
    words = _pack_words(pack_id)
    with tools._db() as con:
        meaning = {w: (m, e) for w, m, e in con.execute(
            "SELECT lower(w.word), w.meaning, w.example FROM deck_words w WHERE w.deck_id=?",
            (json.loads((PACKS / pack_id / "manifest.json").read_text())["deck_id"],))}
        applied = 0
        for idx, q, ts in sorted(events, key=lambda e: e[2]):       # replay in the order they were made
            if not (0 <= idx < len(words)) or not words[idx]:
                continue
            word, now = words[idx], int(time.time())
            ts = min(int(ts), now) if ts > 1_600_000_000 else now      # a device without a clock sends small numbers
            if con.execute("SELECT 1 FROM vocab WHERE device=? AND word=?", (device_id, word)).fetchone() is None:
                m, e = meaning.get(word, ("", ""))
                con.execute("INSERT INTO vocab(device,word,meaning,example,due,created) VALUES(?,?,?,?,?,?)",
                            (device_id, word, m[:200], e.split(" — ")[0][:160], ts, ts))
            if tools.apply_grade(con, device_id, word, int(q), ts) is not None:
                applied += 1
    with store.connect() as c2:
        c2.execute("INSERT INTO device_sync(device,last_sync,pack_id,events,last_events) VALUES(?,?,?,?,?) "
                   "ON CONFLICT(device) DO UPDATE SET last_sync=excluded.last_sync, pack_id=excluded.pack_id, "
                   "events=events+excluded.last_events, last_events=excluded.last_events",
                   (device_id, int(time.time()), pack_id, applied, applied))
    return applied


async def progress(request: web.Request) -> web.Response:
    device_id = await authorize(request)
    try:
        body = await request.json()
        pack_id, events = body["pack"], body["events"]
        assert PACK_ID.match(pack_id) and isinstance(events, list) and len(events) <= MAX_EVENTS
        assert all(isinstance(e, list) and len(e) == 3 for e in events)
    except Exception:
        raise web.HTTPBadRequest(text="bad progress payload")
    if not (PACKS / pack_id).is_dir():
        raise web.HTTPNotFound(text="unknown pack")
    applied = await _run(_apply, device_id, pack_id, events)
    log.info("%s uploaded %d offline reviews (%d applied)", device_id, len(events), applied)
    return web.json_response({"applied": applied})


def routes():
    return [
        web.get("/sync/manifest", manifest),
        web.get("/sync/file/{pack}/{name}", pack_file),
        web.get("/sync/state", state),
        web.post("/sync/progress", progress),
    ]
