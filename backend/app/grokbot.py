"""Asynchronous Cursor automation dispatch for Grokbot replies."""

from __future__ import annotations

from dataclasses import dataclass
import logging

import httpx


logger = logging.getLogger("ai_buddy.grokbot")


@dataclass(frozen=True)
class GrokbotSettings:
    webhook_url: str
    webhook_token: str
    callback_url: str
    timeout_seconds: float
    response_max_chars: int


class GrokbotWebhook:
    def __init__(self, settings: GrokbotSettings) -> None:
        self.settings = settings

    @property
    def enabled(self) -> bool:
        return bool(self.settings.webhook_url and self.settings.webhook_token and self.settings.callback_url)

    async def dispatch(self, request_id: str, device_id: str, transcript: str) -> None:
        if not self.enabled:
            raise RuntimeError("Grokbot webhook is not configured")
        payload = {
            "request_id": request_id,
            "device_id": device_id,
            "transcript": transcript,
            "callback_url": self.settings.callback_url,
            "response_max_chars": self.settings.response_max_chars,
        }
        try:
            async with httpx.AsyncClient(timeout=self.settings.timeout_seconds) as client:
                response = await client.post(
                    self.settings.webhook_url,
                    headers={"Authorization": f"Bearer {self.settings.webhook_token}"},
                    json=payload,
                )
                response.raise_for_status()
        except httpx.HTTPError as exc:
            logger.warning("Grokbot webhook dispatch failed request_id=%s error=%s", request_id, exc)
            raise RuntimeError("Grokbot webhook dispatch failed") from exc
