"""Configuration loading.

Static defaults live in config.yaml and prompt.txt. Everything the admin page can change is
stored in data/settings.json (behaviour, prompt, voice) and data/llm.json (models and API key).
Both are re-read for every new conversation, so changes never need a restart.
"""
import hashlib
import hmac
import json
import os
import secrets
from pathlib import Path

import yaml

ROOT = Path(os.environ.get("APP_ROOT", Path(__file__).resolve().parent.parent))
DATA = ROOT / "data"
DATA.mkdir(parents=True, exist_ok=True)

DEFAULT_ASR_MODEL = "google/gemini-2.5-flash-lite"
# Tried when the main speech model is busy (rate limited): another model has its own limit.
DEFAULT_ASR_FALLBACK_MODEL = "google/gemini-2.5-flash"

# Settings editable from the admin page, with their defaults.
SETTINGS_DEFAULTS = {
    "prompt": "",                 # empty = use prompt.txt
    "wake_greeting": "",          # empty = use config.yaml
    "tts_voice": "",
    "tts_voice_vi": "",
    "tts_rate": "",
    "silence_ms": 0,              # 0 = use config.yaml
    "temperature": None,
    "max_tokens": 0,
    "history_messages": 0,
    "approval_required": True,    # new devices stay pending until approved
    "daily_cost_limit": 2.0,      # USD per day across all devices, 0 = unlimited
    "log_retention_days": 30,
    "firmware_release": "",       # version offered to devices through OTA
    "default_deck": 0,            # vocabulary deck used by devices without their own (0 = first deck)
    "max_new_per_ip_day": 10,     # new devices one IP address may register per day (0 = unlimited)
}


def load_config() -> dict:
    with open(ROOT / "config.yaml", encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_prompt() -> str:
    return (ROOT / "prompt.txt").read_text(encoding="utf-8").strip()


def _read_json(name: str) -> dict:
    try:
        with open(DATA / name, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def load_settings() -> dict:
    s = dict(SETTINGS_DEFAULTS)
    s.update({k: v for k, v in _read_json("settings.json").items() if k in SETTINGS_DEFAULTS})
    return s


def effective(cfg: dict) -> dict:
    """config.yaml merged with the admin overrides, as a flat dict used by a session."""
    s = load_settings()
    return {
        "prompt": s["prompt"].strip() or load_prompt(),
        "wake_greeting": s["wake_greeting"].strip() or cfg["wake_greeting"],
        "tts_voice": s["tts_voice"].strip() or cfg["tts"]["voice"],
        "tts_voice_vi": s["tts_voice_vi"].strip() or cfg["tts"].get("voice_vi", "vi-VN-HoaiMyNeural"),
        "tts_rate": s["tts_rate"].strip() or cfg["tts"]["rate"],
        "silence_ms": s["silence_ms"] or cfg["vad"]["silence_ms"],
        "temperature": cfg["llm"]["temperature"] if s["temperature"] is None else s["temperature"],
        "max_tokens": s["max_tokens"] or cfg["llm"]["max_tokens"],
        "history_messages": s["history_messages"] or cfg["llm"]["history_messages"],
        "approval_required": bool(s["approval_required"]),
        "daily_cost_limit": float(s["daily_cost_limit"] or 0),
        "log_retention_days": int(s["log_retention_days"] or 0),
        "firmware_release": s["firmware_release"],
        "max_new_per_ip_day": int(s["max_new_per_ip_day"] or 0),
    }


def load_llm_settings() -> dict:
    """Return model, chat_model, asr_model and api_key; empty strings when not configured."""
    d = _read_json("llm.json")
    return {"model": d.get("model", ""), "api_key": d.get("api_key", ""),
            "asr_model": d.get("asr_model") or DEFAULT_ASR_MODEL,
            "asr_fallback_model": d.get("asr_fallback_model", DEFAULT_ASR_FALLBACK_MODEL),   # "" turns it off
            "chat_model": d.get("chat_model", "")}       # free conversation; empty = same model as the lessons


def _secret() -> bytes:
    path = DATA / "secret.key"
    if not path.exists():
        path.write_text(secrets.token_hex(32))
        os.chmod(path, 0o600)
    return path.read_text().strip().encode()


def make_token(device_id: str) -> str:
    return hmac.new(_secret(), device_id.encode(), hashlib.sha256).hexdigest()


def check_token(device_id: str, token: str) -> bool:
    return hmac.compare_digest(make_token(device_id), token or "")
