"""Local Gemma-backed response generation for AI Buddy."""

from __future__ import annotations

import logging
from dataclasses import dataclass
import re

import httpx

from .identity import IDENTITY_VERSION, SYSTEM_PROMPT
from .memory import ConversationTurn, Memory


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
        return await self._respond(device_id, "The user pressed the button.", "greet the user and confirm that you are ready")

    async def respond_to_transcript(
        self, device_id: str, transcript: str, memories: list[Memory], history: list[ConversationTurn]
    ) -> str:
        return await self._respond(
            device_id, transcript, "answer the user's spoken request helpfully", memories=memories, history=history
        )

    async def respond_to_small_talk(self, device_id: str, transcript: str) -> str:
        return await self._respond(
            device_id,
            transcript,
            "reply in Spanish to this brief greeting or acknowledgement in one short sentence",
            max_predict=24,
            max_chars=120,
        )

    async def _respond(
        self,
        device_id: str,
        user_message: str,
        task: str,
        memories: list[Memory] | None = None,
        history: list[ConversationTurn] | None = None,
        max_predict: int = 60,
        max_chars: int = 220,
    ) -> str:
        if self.settings.provider == "mock":
            return "Recibi tu mensaje."
        if self.settings.provider != "ollama":
            raise ValueError(f"unsupported agent provider: {self.settings.provider}")

        payload = {
            "model": self.settings.model,
            "stream": False,
            # Gemma 4 otherwise spends a short device response in hidden reasoning.
            "think": False,
            "messages": self._messages(device_id, user_message, task, memories or [], history or []),
            "options": {"temperature": 0.4, "num_predict": max_predict},
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
        return re.sub(r"\s+", " ", content).strip()[:max_chars]

    @staticmethod
    def _messages(
        device_id: str, user_message: str, task: str, memories: list[Memory], history: list[ConversationTurn]
    ) -> list[dict[str, str]]:
        messages = [{"role": "system", "content": f"Identity version: {IDENTITY_VERSION}.\n{SYSTEM_PROMPT}\nTask: {task}"}]
        if memories:
            context = "\n".join(f"- {memory.content}" for memory in memories[:12])
            messages.append({"role": "system", "content": f"Approved memories for this device:\n{context}"})
        for turn in history[-4:]:
            messages.extend(({"role": "user", "content": turn.user_text}, {"role": "assistant", "content": turn.assistant_text}))
        messages.append({"role": "user", "content": f"Buddy device: {device_id}. User message: {user_message}"})
        return messages
