"""HTTP + WebSocket entry point."""
import asyncio
import logging
import re
import time

from aiohttp import web

from . import store, sync
from .asr import Recognizer
from .config import DATA, check_token, effective, load_config, make_token
from .session import Session

log = logging.getLogger("main")
FIRMWARE_DIR = DATA / "firmware"
FIRMWARE_NAME = re.compile(r"^english-learner_([0-9][A-Za-z0-9.\-]*)\.bin$")


def version_key(v: str) -> tuple:
    return tuple(int(p) for p in re.findall(r"\d+", v)) or (0,)


def client_ip(request: web.Request) -> str:
    fwd = request.headers.get("X-Forwarded-For", "").split(",")[0].strip()
    return fwd or request.headers.get("X-Real-IP", "") or request.remote or ""


async def register(request: web.Request, info: dict | None):
    """Create or refresh the device record. Returns the row, or None when registration is refused."""
    eff = effective(request.app["config"])
    loop = asyncio.get_running_loop()
    try:
        return await loop.run_in_executor(
            None, lambda: store.touch_device(
                request.headers["Device-Id"], client_ip(request), info,
                auto_approve=not eff["approval_required"], max_new_per_ip=eff["max_new_per_ip_day"]))
    except store.RegistrationLimit:
        log.warning("registration limit reached for %s", client_ip(request))
        return None


def activation_block(device) -> dict | None:
    """The `activation` object the firmware shows on screen (and reads aloud) until the device is activated."""
    if device["status"] == "approved":
        return None
    if device["status"] == "blocked":
        return {"message": "Thiết bị đã bị khóa. Vui lòng liên hệ quản trị viên.", "challenge": "blocked",
                "timeout_ms": 30000}
    code = device["activation_code"]
    return {"message": f"Thiết bị đã đăng ký. Hãy nhập mã kích hoạt {code} vào trang quản trị.",
            "code": code, "challenge": code, "timeout_ms": 30000}


async def ota(request: web.Request) -> web.Response:
    """Called by the device at boot: registers it and returns time, firmware update, activation and WebSocket."""
    if request.method == "GET":
        return web.Response(text="OTA endpoint is running.\n")
    cfg = request.app["config"]["server"]
    if not request.headers.get("Device-Id"):
        return web.json_response({"error": "missing Device-Id"}, status=400)
    try:
        body = await request.json()
    except Exception:
        body = {}
    device = await register(request, body)
    if device is None:
        return web.json_response({"error": "too many registrations from this address"}, status=429)
    eff = effective(request.app["config"])
    current = (body.get("application") or {}).get("version", "0.0.0")
    firmware = {"version": current, "url": ""}
    release = eff["firmware_release"]
    if release and device["status"] == "approved":
        name = f"english-learner_{release}.bin"
        if (FIRMWARE_DIR / name).is_file() and version_key(release) > version_key(current):
            firmware = {"version": release, "url": f"{cfg['public_http_url']}/firmware/{name}"}
    reply = {
        "server_time": {"timestamp": int(time.time() * 1000),
                        "timezone_offset": cfg["timezone_offset_minutes"]},
        "firmware": firmware,
        "websocket": {"url": cfg["public_ws_url"],
                      "token": make_token(request.headers["Device-Id"]) if cfg["require_token"] else ""},
    }
    activation = activation_block(device)
    if activation:
        reply["activation"] = activation
    return web.json_response(reply)


async def ota_activate(request: web.Request) -> web.Response:
    """Polled by a device that is waiting for activation: 200 = activated, 202 = keep waiting."""
    device_id = request.headers.get("Device-Id", "")
    if not device_id:
        return web.json_response({"error": "missing Device-Id"}, status=400)
    status = await asyncio.get_running_loop().run_in_executor(None, store.device_status, device_id)
    if status == "approved":
        return web.json_response({"ok": True})
    if status == "blocked":
        return web.json_response({"error": "device blocked"}, status=403)
    return web.json_response({"status": "pending"}, status=202)


async def firmware_file(request: web.Request) -> web.StreamResponse:
    name = request.match_info["name"]
    path = FIRMWARE_DIR / name
    if not FIRMWARE_NAME.match(name) or not path.is_file():
        raise web.HTTPNotFound()
    return web.FileResponse(path)


async def websocket(request: web.Request) -> web.StreamResponse:
    cfg = request.app["config"]
    device_id = request.headers.get("Device-Id", "")
    if not device_id:
        return web.Response(status=400, text="missing Device-Id")
    if cfg["server"]["require_token"]:
        token = request.headers.get("Authorization", "").removeprefix("Bearer ").strip()
        if not check_token(device_id, token):
            return web.Response(status=401, text="invalid token")
    device = await register(request, None)
    if device is None or device["status"] == "blocked":
        return web.Response(status=403, text="device blocked")
    if device["status"] == "pending":
        return web.Response(status=403, text="device waiting for activation")
    ws = web.WebSocketResponse(heartbeat=30, max_msg_size=1 << 20)
    await ws.prepare(request)
    log.info("device %s connected", device_id)
    try:
        await Session(ws, request.app, device_id).run()
    finally:
        log.info("device %s disconnected", device_id)
    return ws


async def health(request: web.Request) -> web.Response:
    return web.json_response({"ok": True})


async def housekeeping(app: web.Application):
    """Delete old conversation logs once at startup and then every six hours."""
    loop = asyncio.get_running_loop()
    while True:
        try:
            await loop.run_in_executor(None, store.prune, effective(app["config"])["log_retention_days"])
        except Exception:
            log.exception("housekeeping failed")
        await asyncio.sleep(6 * 3600)


async def on_startup(app: web.Application):
    cfg = app["config"]
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    store.init()
    app["asr"] = Recognizer(cfg["llm"]["base_url"], cfg["audio"]["sample_rate"],
                            cfg["asr"]["min_speech_ms"])
    app["housekeeping"] = asyncio.create_task(housekeeping(app))
    log.info("ready")


def main():
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    cfg = load_config()
    app = web.Application()
    app["config"] = cfg
    app.add_routes([
        web.route("*", "/ota/", ota),
        web.post("/ota/activate", ota_activate),
        web.get("/ws/", websocket),
        web.get("/firmware/{name}", firmware_file),
        web.get("/health", health),
        *sync.routes(),
    ])
    app.on_startup.append(on_startup)
    web.run_app(app, host=cfg["server"]["host"], port=cfg["server"]["port"], print=None)


if __name__ == "__main__":
    main()
