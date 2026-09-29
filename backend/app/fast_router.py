"""Low-latency, deterministic routing for spoken Buddy requests."""

from __future__ import annotations

from dataclasses import dataclass
import re
import unicodedata


@dataclass(frozen=True)
class RouteDecision:
    target: str
    reason: str


class FastRouter:
    """Keep only small-talk local; route every useful request to Grokbot."""

    _LOCAL_PATTERNS = (
        r"hola(?: buddy)?",
        r"buen(?:os dias| dia|as tardes|as noches)",
        r"buenas",
        r"que tal",
        r"como estas",
        r"estas ahi",
        r"gracias(?: buddy)?",
        r"(?:ok|okay|dale|perfecto|esta bien|si|no)",
        r"(?:chau|adios|hasta luego)",
        r"quien eres",
    )

    def route(self, transcript: str) -> RouteDecision:
        normalized = self._normalize(transcript)
        if not normalized:
            return RouteDecision("gemma", "empty_acknowledgement")
        if any(re.fullmatch(pattern, normalized) for pattern in self._LOCAL_PATTERNS):
            return RouteDecision("gemma", "small_talk")
        return RouteDecision("grokbot", "request_requires_context_or_action")

    @staticmethod
    def _normalize(text: str) -> str:
        plain = unicodedata.normalize("NFD", text.lower())
        plain = "".join(character for character in plain if unicodedata.category(character) != "Mn")
        return re.sub(r"[^a-z0-9 ]+", "", plain).strip()
