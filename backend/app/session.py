"""One device connection: audio in -> text -> LLM -> speech out."""
import asyncio
import json
import logging
import re
import time
import uuid

from aiohttp import WSMsgType, web

from . import store, tools
from .audio import OpusDecoder, OpusEncoder
from .config import effective, load_llm_settings
from .llm import LLM
from .tts import Synthesizer, clean_for_speech
from .vad import Endpointer

log = logging.getLogger("session")

PREBUFFER_FRAMES = 5      # frames sent ahead of real time so the device never starves
SENTENCE_END = re.compile(r"(?<=[.!?…])\s+|\n+")
MIN_CHUNK_CHARS = 14      # merge very short sentences into the next one
# "teach me new words" / "next word": handled by the server so a small model cannot invent words
NEW_WORD_INTENT = re.compile(
    r"\b(new|more|another|next)\s+words?\b|\bteach me\b.*\bwords?\b|\blearn\b.*\bwords?\b|"
    r"(học|dạy|cho).{0,12}từ\s*(mới|tiếp|khác)|từ\s*(mới|tiếp theo|tiếp|khác)", re.I)
OFFLINE_INTENT = re.compile(
    r"\boff-?line\b|\bwithout (the )?internet\b|\bno internet\b|không (cần )?(có )?(mạng|internet|wifi|wi-fi)|"
    r"học (bằng )?thẻ", re.I)
ASKING_MEANING = re.compile(r"\b(mean|means|meaning|define|definition)\b|nghĩa|là gì", re.I)
CONTINUE_INTENT = re.compile(
    r"^\W*(next|ready|continue|go on|go ahead|yes|yeah|ok(ay)?|sure|tiếp( theo| tục)?|sẵn sàng|được|rồi|ừ)\W*$", re.I)
# Interpreter mode: every utterance is translated, Vietnamese to English and English to Vietnamese. The device sends
# "Chế độ phiên dịch" when the learner opens the Interpreter tile; it can also be asked for by voice.
INTERPRETER_ON = re.compile(
    r"chế độ (phiên|thông) dịch|\b(làm|bật)\b.{0,12}\b(phiên|thông) dịch|phiên dịch (giúp|cho) (mình|tôi)|"
    r"\binterpreter mode\b|\bact as (an? )?interpreter\b", re.I)
INTERPRETER_OFF = re.compile(
    r"(thoát|dừng|tắt|hết|kết thúc|ngừng).{0,15}(phiên|thông) dịch|\b(stop|exit|end|quit)\b.{0,20}\b(interpret|translat)",
    re.I)
INTERPRETER_PROMPT = (
    "You are a precise two-way interpreter between Vietnamese and English. The user message is a speech transcript. "
    "If it is Vietnamese, output only its English translation. If it is English, output only its Vietnamese translation. "
    "If it mixes both, translate it into the language that is not the main one. "
    "Output only the translation: no quotes, no notes, no explanations, no markdown, no emoji, no romanization. "
    "Keep the meaning and the tone of the speaker, keep names unchanged, and translate the whole text however long. "
    "Write Vietnamese with full accents.")
WATCH_INTERVAL = 3        # seconds between status / command checks


class Session:
    def __init__(self, ws: web.WebSocketResponse, app, device_id: str):
        self.ws = ws
        self.app = app
        self.cfg = app["config"]
        self.eff = effective(self.cfg)           # admin overrides, fixed for this connection
        self.device_id = device_id
        self.session_id = uuid.uuid4().hex
        rate, ms = self.cfg["audio"]["sample_rate"], self.cfg["audio"]["frame_ms"]
        self.rate, self.frame_ms = rate, ms
        self.decoder = OpusDecoder(rate, ms)
        self.encoder = OpusEncoder(rate, ms)
        v = self.cfg["vad"]
        self.endpointer = Endpointer(rate, v["aggressiveness"], self.eff["silence_ms"],
                                     v["min_speech_ms"], v["max_utterance_s"])
        self.tts = Synthesizer(self.eff["tts_voice"], self.eff["tts_rate"], rate, self.eff["tts_voice_vi"])
        self.listening = False
        self.mode = "auto"
        self.busy = False                       # transcribing or speaking
        self.reply_task: asyncio.Task | None = None
        self.history: list[dict] = []
        self.speaking = False                   # a tts start was sent and no tts stop yet
        self.learning = False                   # a new word was just taught, "next" means another one
        self.interpreter = False                # interpreter mode: translate every utterance
        self.utterance_end = 0.0
        self.turn: dict | None = None           # the turn about to be answered
        self.cur_turn: dict | None = None       # the turn being answered right now

    # ------------------------------------------------------------------ I/O
    async def send_json(self, **msg):
        msg["session_id"] = self.session_id
        if not self.ws.closed:
            await self.ws.send_str(json.dumps(msg, ensure_ascii=False))

    async def run(self):
        await self._db(store.set_online, self.device_id, True)
        watcher = asyncio.create_task(self._watch())
        try:
            async for msg in self.ws:
                if msg.type == WSMsgType.TEXT:
                    try:
                        await self.on_json(json.loads(msg.data))
                    except Exception:
                        log.exception("bad message")
                elif msg.type == WSMsgType.BINARY:
                    await self.on_audio(msg.data)
        finally:
            watcher.cancel()
            await self.abort()
            await self._db(store.set_online, self.device_id, False)

    @staticmethod
    async def _db(fn, *args):
        return await asyncio.get_running_loop().run_in_executor(None, fn, *args)

    async def _watch(self):
        """Apply admin actions to a live connection: block, reboot, disconnect."""
        while not self.ws.closed:
            await asyncio.sleep(WATCH_INTERVAL)
            try:
                status = await self._db(store.device_status, self.device_id)
                if status != "approved":
                    log.info("closing %s (status %s)", self.device_id, status)
                    await self.ws.close(code=4003, message=b"device not allowed")
                    return
                for cmd in await self._db(store.pending_commands, self.device_id):
                    log.info("command %s -> %s", cmd["cmd"], self.device_id)
                    if cmd["cmd"] == "reboot":
                        await self.send_json(type="system", command="reboot")
                    elif cmd["cmd"] == "offline":
                        await self.send_json(type="system", command="offline")
                    elif cmd["cmd"] == "disconnect":
                        await self.ws.close(code=1000, message=b"disconnected by admin")
                        return
            except Exception:
                log.exception("watcher failed")

    # ------------------------------------------------------------- messages
    async def on_json(self, m: dict):
        t = m.get("type")
        if t == "hello":
            await self.send_json(type="hello", transport="websocket", audio_params={
                "format": "opus", "sample_rate": self.rate, "channels": 1,
                "frame_duration": self.frame_ms})
        elif t == "listen":
            state = m.get("state")
            if state == "start":
                await self.abort()
                self.mode = m.get("mode", "auto")
                self.endpointer.reset()
                self.listening = True
            elif state == "stop":
                self.listening = False
                if self.endpointer.started or self.mode == "manual":
                    await self.finish_utterance()
            elif state == "detect":
                await self.on_wake(m.get("text", ""))
        elif t == "abort":
            await self.abort()
        # other types (mcp, ...) are not used by this server

    async def on_audio(self, packet: bytes):
        if not self.listening or self.busy:
            return
        try:
            pcm = self.decoder.decode(packet)
        except Exception:
            return
        if self.endpointer.feed(pcm) and self.mode != "manual":
            self.listening = False
            await self.finish_utterance()

    async def quota_message(self) -> str:
        """Spoken explanation when the device's plan does not allow another turn, else ""."""
        q = await self._db(store.plan_quota, self.device_id, self.cfg["server"]["timezone_offset_minutes"] * 60)
        if q["allowed"]:
            return ""
        log.info("plan limit for %s: %s", self.device_id, q["reason"])
        return {
            "expired": "Gói cước của thiết bị đã hết hạn. Vui lòng liên hệ quản trị viên để gia hạn.",
            "daily": "Hôm nay bạn đã dùng hết số lượt của gói. Hẹn gặp lại bạn vào ngày mai.",
            "monthly": "Tháng này bạn đã dùng hết số lượt của gói. Vui lòng gia hạn hoặc nâng cấp gói cước.",
        }[q["reason"]]

    async def on_wake(self, text: str):
        await self.abort()
        limit = await self.quota_message()
        if limit:
            self.start_reply(self.reply_fixed(limit))
            return
        norm = re.sub(r"[^a-z ]", "", text.lower()).strip()
        if self.cfg["wake_word"] in norm.replace(" ", "") and len(norm) <= 20:
            self.start_reply(self.reply_fixed(self.eff["wake_greeting"]))
        elif text.strip():
            self.turn = {"user": text, "asr_ms": 0, "asr_cost": 0.0, "asr_model": ""}
            await self.send_json(type="stt", text=text)
            self.start_reply(self.reply_llm(text))

    # ------------------------------------------------------------ utterance
    async def finish_utterance(self):
        pcm = bytes(self.endpointer.pcm)
        self.endpointer.reset()
        self.utterance_end = time.monotonic()
        if not pcm:
            return
        limit = await self.quota_message()          # checked before speech recognition so it costs nothing
        if limit:
            self.start_reply(self.reply_fixed(limit))
            return
        self.busy = True
        t0 = time.monotonic()
        settings = load_llm_settings()
        cost, error = 0.0, ""
        try:
            text, cost = await self.app["asr"].transcribe(
                pcm, settings["api_key"], settings["asr_model"])
        except Exception as e:
            log.exception("speech recognition failed")
            text, error = "", f"asr: {e}"
        asr_ms = int((time.monotonic() - t0) * 1000)
        log.info("asr took %.1fs", asr_ms / 1000)
        if not text:
            if error:
                await self._db(store.record_turn, self.device_id, "", "", asr_ms, 0, [], cost,
                               "", settings["asr_model"], False, error)
            self.busy = False
            self.listening = True            # nothing recognised: keep listening
            return
        self.turn = {"user": text, "asr_ms": asr_ms, "asr_cost": cost, "asr_model": settings["asr_model"]}
        await self.send_json(type="stt", text=text)
        self.start_reply(self.reply_llm(text))

    # --------------------------------------------------------------- replies
    def start_reply(self, coro):
        self.busy = True
        self.reply_task = asyncio.create_task(self._reply(coro))

    async def abort(self):
        task, self.reply_task = self.reply_task, None
        if task and not task.done():
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
            if self.speaking:
                self.speaking = False
                await self.send_json(type="tts", state="stop")
        self.busy = False

    async def _reply(self, sentences):
        """Run the sentence generator, synthesising ahead while the current one plays."""
        queue: asyncio.Queue = asyncio.Queue(maxsize=3)
        synth = self.tts
        turn = self.cur_turn = self.turn
        self.turn = None
        spoken: list[str] = []
        failure = ""

        async def producer():
            nonlocal failure
            try:
                async for sentence in sentences:
                    await queue.put((sentence, asyncio.create_task(synth.synthesize(sentence))))
            except Exception as e:
                log.exception("reply generation failed")
                failure = f"llm: {e}"
                msg = "Sorry, something went wrong. Please try again."
                await queue.put((msg, asyncio.create_task(synth.synthesize(msg))))
            finally:
                await queue.put(None)

        prod = asyncio.create_task(producer())
        first_audio_ms = 0
        try:
            self.speaking = True
            await self.send_json(type="tts", state="start")
            frames_sent, start = 0, None
            while (item := await queue.get()) is not None:
                sentence, pcm_task = item
                pcm = await pcm_task
                if not pcm:
                    continue
                spoken.append(sentence)
                await self.send_json(type="tts", state="sentence_start", text=sentence)
                for frame in self.encoder.encode_pcm(pcm):
                    if start is None:
                        start = time.monotonic()
                        if self.utterance_end:
                            first_audio_ms = int((start - self.utterance_end) * 1000)
                            log.info("latency: first audio %.1fs after end of speech",
                                     first_audio_ms / 1000)
                    due = start + max(0, frames_sent - PREBUFFER_FRAMES) * self.frame_ms / 1000
                    delay = due - time.monotonic()
                    if delay > 0:
                        await asyncio.sleep(delay)
                    if self.ws.closed:
                        return
                    await self.ws.send_bytes(frame)
                    frames_sent += 1
            # let the device drain its buffer before telling it we are done
            if start is not None:
                tail = start + frames_sent * self.frame_ms / 1000 - time.monotonic()
                if tail > 0:
                    await asyncio.sleep(min(tail, 30))
            self.speaking = False
            await self.send_json(type="tts", state="stop")
        except asyncio.CancelledError:
            prod.cancel()
            raise
        finally:
            self.busy = False
            if turn is not None:
                try:
                    llm = turn.get("llm")
                    await asyncio.shield(self._db(
                        store.record_turn, self.device_id, turn["user"], " ".join(spoken),
                        turn["asr_ms"], first_audio_ms, llm.tools_used if llm else [],
                        turn["asr_cost"] + (llm.cost if llm else 0.0),
                        llm.model if llm else "", turn["asr_model"], not failure, failure))
                except Exception:
                    log.exception("could not record turn")

    async def reply_fixed(self, text: str):
        yield text

    async def reply_llm(self, user_text: str):
        if self.interpreter and INTERPRETER_OFF.search(user_text):
            self.interpreter = False
            yield "Đã thoát chế độ phiên dịch."
            return
        if not self.interpreter and INTERPRETER_ON.search(user_text) and not INTERPRETER_OFF.search(user_text):
            self.interpreter = True
            yield ("Sẵn sàng phiên dịch. Bạn nói tiếng Việt thì mình dịch sang tiếng Anh, "
                   "bạn nói tiếng Anh thì mình dịch sang tiếng Việt.")
            return
        settings = load_llm_settings()
        if not settings["api_key"] or not settings["model"]:
            yield "The language model is not configured yet. Please set it up in the admin page."
            return
        limit = self.eff["daily_cost_limit"]
        if limit > 0:
            off = self.cfg["server"]["timezone_offset_minutes"] * 60
            now = int(time.time())
            spent = await self._db(store.cost_since, now - (now + off) % 86400)
            if spent >= limit:
                log.warning("daily cost limit reached (%.3f >= %.2f)", spent, limit)
                yield "Sorry, the daily usage limit has been reached. Please try again tomorrow."
                return
        c = self.cfg["llm"]
        llm = LLM(c["base_url"], settings["api_key"], settings["model"],
                  self.eff["temperature"], self.eff["max_tokens"])
        if self.cur_turn is not None:
            self.cur_turn["llm"] = llm
        if self.interpreter:
            # stateless: each utterance is translated on its own, without the tutor prompt, history or tools
            messages = [{"role": "system", "content": INTERPRETER_PROMPT}, {"role": "user", "content": user_text}]
            schemas = []
        else:
            self.history.append({"role": "user", "content": user_text})
            del self.history[:-self.eff["history_messages"]]
            messages = [{"role": "system", "content": self.eff["prompt"]}] + self.history
            schemas = tools.SCHEMAS

            if OFFLINE_INTENT.search(user_text) and not ASKING_MEANING.search(user_text):
                hint = await tools.run_tool(self.device_id, "start_offline_study", {})
                llm.tools_used.append("start_offline_study")
                messages.append({"role": "system", "content": hint})
                schemas = [s for s in schemas if s["function"]["name"] != "start_offline_study"]
            wants_word = bool(NEW_WORD_INTENT.search(user_text)) or (
                self.learning and bool(CONTINUE_INTENT.match(user_text)))
            self.learning = False
            if wants_word:
                word = await tools.run_tool(self.device_id, "learn_new_words", {})
                llm.tools_used.append("learn_new_words")
                self.learning = word.startswith("New word")
                messages.append({"role": "system", "content": word})
                schemas = [s for s in schemas if s["function"]["name"] != "learn_new_words"]


        async def run_tool(name, args):
            return await tools.run_tool(self.device_id, name, args)

        buf, produced = "", False
        async for delta in llm.stream(messages, schemas, run_tool):
            buf += delta
            parts = SENTENCE_END.split(buf)
            buf = parts.pop()
            pending = ""
            for p in parts:
                pending = (pending + " " + p).strip()
                if len(pending) >= MIN_CHUNK_CHARS:
                    produced = True
                    yield pending
                    pending = ""
            buf = (pending + " " + buf).strip() if pending else buf
        if clean_for_speech(buf):
            produced = True
            yield buf.strip()
        if not produced:
            yield "Sorry, I did not catch that. Could you say it again?"
        if not self.interpreter:
            # keep only the conversational turns (drop tool plumbing) for the next question
            self.history = [m for m in messages[1:]
                            if m["role"] in ("user", "assistant") and m.get("content")]
