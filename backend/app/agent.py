"""Local Gemma-backed response generation for AI Buddy."""

from __future__ import annotations

import logging
from dataclasses import dataclass
import re

import httpx


logger = logging.getLogger("ai_buddy.agent")


@dataclass(frozen=True)
class AgentSettings:
    provider: str
    model: str
    ollama_base_url: str
    timeout_seconds: float


class BuddyAgent:
    def __init__(self, settings: AgentSettings) -> None:
        self.settings = settings

    async def respond_to_button(self, device_id: str) -> str:
        if self.settings.provider == "mock":
            return "Buddy online. I received your button."
        if self.settings.provider != "ollama":
            raise ValueError(f"unsupported agent provider: {self.settings.provider}")

        payload = {
            "model": self.settings.model,
            "stream": False,
            "messages": [
                {
                    "role": "system",
                    "content": (
                        "You are AI Buddy, a private desk companion. Respond in Spanish, "
                        "warmly and concisely. This is a device button check, so greet the "
                        "user and confirm that you are ready in one short sentence."
                    ),
                },
                {"role": "user", "content": f"The user pressed the button on {device_id}."},
            ],
            "options": {"temperature": 0.4, "num_predict": 60},
        }
        try:
            async with httpx.AsyncClient(timeout=self.settings.timeout_seconds) as client:
                response = await client.post(f"{self.settings.ollama_base_url.rstrip('/')}/api/chat", json=payload)
                response.raise_for_status()
        except httpx.HTTPError as exc:
            logger.warning("Gemma request failed: %s", exc)
            return "Estoy conectando con mi modelo local. Intenta de nuevo en un momento."

        content = response.json().get("message", {}).get("content", "")
        if not isinstance(content, str) or not content.strip():
            return "Estoy listo, pero no recibi una respuesta valida del modelo."
        return re.sub(r"\s+", " ", content).strip()[:180]
