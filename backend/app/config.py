"""Runtime configuration for the local AI Buddy backend."""

from dataclasses import dataclass
import os


@dataclass(frozen=True)
class Settings:
    device_token: str
    protocol_version: str
    public_base_url: str
    agent_model: str
    agent_provider: str
    ollama_base_url: str
    agent_timeout_seconds: float


def get_settings() -> Settings:
    """Read settings on demand so tests and local reloads stay predictable."""
    return Settings(
        device_token=os.getenv("AI_BUDDY_DEVICE_TOKEN", "local-development-token"),
        protocol_version=os.getenv("AI_BUDDY_PROTOCOL_VERSION", "1"),
        public_base_url=os.getenv("AI_BUDDY_PUBLIC_BASE_URL", "").rstrip("/"),
        agent_model=os.getenv("AI_BUDDY_AGENT_MODEL", "gemma-4-4b-it"),
        agent_provider=os.getenv("AI_BUDDY_AGENT_PROVIDER", "mock"),
        ollama_base_url=os.getenv("AI_BUDDY_OLLAMA_BASE_URL", "http://host.docker.internal:11434"),
        agent_timeout_seconds=float(os.getenv("AI_BUDDY_AGENT_TIMEOUT_SECONDS", "90")),
    )
