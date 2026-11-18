"""Streaming chat completion with tool calling (OpenAI-compatible API, e.g. OpenRouter)."""
import json
import logging
from typing import AsyncIterator, Awaitable, Callable

from openai import AsyncOpenAI, BadRequestError

from .asr import usage_cost

log = logging.getLogger("llm")
MAX_TOOL_ROUNDS = 3
# Models (on OpenRouter) that refuse "reasoning disabled": they get low-effort reasoning and room for it instead.
_REASONING_ONLY: set[str] = set()
REASONING_EXTRA_TOKENS = 3000


class LLM:
    def __init__(self, base_url: str, api_key: str, model: str,
                 temperature: float, max_tokens: int):
        self.model = model
        self.temperature = temperature
        self.max_tokens = max_tokens
        self._client = AsyncOpenAI(base_url=base_url, api_key=api_key, timeout=45)
        self.cost = 0.0                      # accumulated over all rounds of one stream() call
        self.tools_used: list[str] = []
        self._openrouter = "openrouter.ai" in base_url

    async def stream(self, messages: list[dict], tools: list[dict],
                     run_tool: Callable[[str, dict], Awaitable[str]]) -> AsyncIterator[str]:
        """Yield text deltas. Tool calls are executed and appended to `messages`."""
        for _ in range(MAX_TOOL_ROUNDS + 1):
            kwargs = dict(model=self.model, messages=messages, stream=True,
                          stream_options={"include_usage": True},
                          temperature=self.temperature, max_tokens=self.max_tokens)
            if tools:                       # an empty tools list is rejected by some providers
                kwargs["tools"] = tools
            if self._openrouter:
                kwargs["extra_body"] = {"reasoning": {"enabled": False}}
            if self.model in _REASONING_ONLY:
                kwargs["extra_body"] = {"reasoning": {"effort": "low", "exclude": True}}
                kwargs["max_tokens"] = self.max_tokens + REASONING_EXTRA_TOKENS
            text, calls = "", {}
            try:
                stream = await self._client.chat.completions.create(**kwargs)
            except BadRequestError as e:
                if self.model in _REASONING_ONLY or "reasoning" not in str(e).lower():
                    raise
                # e.g. "Reasoning is mandatory for this endpoint and cannot be disabled"
                log.warning("%s needs reasoning, retrying with low effort", self.model)
                _REASONING_ONLY.add(self.model)
                kwargs["extra_body"] = {"reasoning": {"effort": "low", "exclude": True}}
                kwargs["max_tokens"] = self.max_tokens + REASONING_EXTRA_TOKENS
                stream = await self._client.chat.completions.create(**kwargs)
            async for chunk in stream:
                if getattr(chunk, "usage", None):
                    self.cost += usage_cost(chunk.usage)
                if not chunk.choices:
                    continue
                delta = chunk.choices[0].delta
                if delta.content:
                    text += delta.content
                    yield delta.content
                for tc in delta.tool_calls or []:
                    c = calls.setdefault(tc.index, {"id": "", "name": "", "args": ""})
                    c["id"] = tc.id or c["id"]
                    if tc.function:
                        c["name"] += tc.function.name or ""
                        c["args"] += tc.function.arguments or ""
            if not calls:
                messages.append({"role": "assistant", "content": text})
                return
            messages.append({
                "role": "assistant", "content": text or None,
                "tool_calls": [{"id": c["id"], "type": "function",
                                "function": {"name": c["name"], "arguments": c["args"] or "{}"}}
                               for c in calls.values()]})
            for c in calls.values():
                try:
                    args = json.loads(c["args"] or "{}")
                except ValueError:
                    args = {}
                log.info("tool call %s(%s)", c["name"], args)
                self.tools_used.append(c["name"])
                result = await run_tool(c["name"], args)
                messages.append({"role": "tool", "tool_call_id": c["id"], "content": result})
        log.warning("too many tool rounds")
