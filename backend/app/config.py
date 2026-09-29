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
    stt_provider: str
    stt_model: str
    stt_language: str
    stt_timeout_seconds: float
    tts_provider: str
    tts_voice: str
    tts_timeout_seconds: float
    data_dir: str
    grokbot_webhook_url: str
    grokbot_webhook_token: str
    grokbot_callback_token: str
    grokbot_timeout_seconds: float
    grokbot_response_max_chars: int


def get_settings() -> Settings:
    """Read settings on demand so tests and local reloads stay predictable."""
    return Settings(
        device_token=os.getenv("AI_BUDDY_DEVICE_TOKEN", "local-development-token"),
        protocol_version=os.getenv("AI_BUDDY_PROTOCOL_VERSION", "1"),
        public_base_url=os.getenv("AI_BUDDY_PUBLIC_BASE_URL", "").rstrip("/"),
        agent_model=os.getenv("AI_BUDDY_AGENT_MODEL", "gemma4:e4b"),
        agent_provider=os.getenv("AI_BUDDY_AGENT_PROVIDER", "mock"),
        ollama_base_url=os.getenv("AI_BUDDY_OLLAMA_BASE_URL", "http://host.docker.internal:11434"),
        agent_timeout_seconds=float(os.getenv("AI_BUDDY_AGENT_TIMEOUT_SECONDS", "90")),
        stt_provider=os.getenv("AI_BUDDY_STT_PROVIDER", "faster_whisper"),
        stt_model=os.getenv("AI_BUDDY_STT_MODEL", "base"),
        stt_language=os.getenv("AI_BUDDY_STT_LANGUAGE", "es"),
        stt_timeout_seconds=float(os.getenv("AI_BUDDY_STT_TIMEOUT_SECONDS", "90")),
        tts_provider=os.getenv("AI_BUDDY_TTS_PROVIDER", "piper"),
        tts_voice=os.getenv("AI_BUDDY_TTS_VOICE", "es_AR-daniela-high"),
        tts_timeout_seconds=float(os.getenv("AI_BUDDY_TTS_TIMEOUT_SECONDS", "90")),
        data_dir=os.getenv("AI_BUDDY_DATA_DIR", "/tmp/ai-buddy"),
        grokbot_webhook_url=os.getenv("AI_BUDDY_GROKBOT_WEBHOOK_URL", "").rstrip("/"),
        grokbot_webhook_token=os.getenv("AI_BUDDY_GROKBOT_WEBHOOK_TOKEN", ""),
        grokbot_callback_token=os.getenv("AI_BUDDY_GROKBOT_CALLBACK_TOKEN", ""),
        grokbot_timeout_seconds=float(os.getenv("AI_BUDDY_GROKBOT_TIMEOUT_SECONDS", "20")),
        grokbot_response_max_chars=int(os.getenv("AI_BUDDY_GROKBOT_RESPONSE_MAX_CHARS", "160")),
    )
