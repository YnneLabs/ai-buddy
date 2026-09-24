"""Runtime configuration for the local AI Buddy backend."""

from dataclasses import dataclass
import os


@dataclass(frozen=True)
class Settings:
    device_token: str
    protocol_version: str
    public_base_url: str
    agent_model: str


def get_settings() -> Settings:
    """Read settings on demand so tests and local reloads stay predictable."""
    return Settings(
        device_token=os.getenv("AI_BUDDY_DEVICE_TOKEN", "local-development-token"),
        protocol_version=os.getenv("AI_BUDDY_PROTOCOL_VERSION", "1"),
        public_base_url=os.getenv("AI_BUDDY_PUBLIC_BASE_URL", "").rstrip("/"),
        agent_model=os.getenv("AI_BUDDY_AGENT_MODEL", "gemma-4-4b-it"),
    )
