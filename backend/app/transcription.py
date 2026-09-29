"""Local speech-to-text adapters for AI Buddy audio uploads."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import logging
from threading import Lock


logger = logging.getLogger("ai_buddy.transcription")


@dataclass(frozen=True)
class TranscriptionSettings:
    provider: str
    model: str
    language: str
    timeout_seconds: float


class BuddyTranscriber:
    """Transcribe 16-bit PCM recorded by the Buddy without a cloud API."""

    _model_cache: dict[tuple[str, str], object] = {}
    _model_lock = Lock()

    def __init__(self, settings: TranscriptionSettings) -> None:
        self.settings = settings

    async def transcribe_pcm(self, pcm: bytes, sample_rate: int, channels: int) -> str:
        if self.settings.provider == "mock":
            seconds = len(pcm) / (sample_rate * channels * 2)
            return f"Audio recibido localmente ({seconds:.1f} segundos)."
        if self.settings.provider != "faster_whisper":
            raise ValueError(f"unsupported STT provider: {self.settings.provider}")
        return await asyncio.wait_for(
            asyncio.to_thread(self._transcribe_with_faster_whisper, pcm, sample_rate, channels),
            timeout=self.settings.timeout_seconds,
        )

    def _transcribe_with_faster_whisper(self, pcm: bytes, sample_rate: int, channels: int) -> str:
        import numpy as np
        from faster_whisper import WhisperModel

        cache_key = (self.settings.model, "cpu")
        with self._model_lock:
            model = self._model_cache.get(cache_key)
            if model is None:
                logger.info("loading local STT model model=%s", self.settings.model)
                model = WhisperModel(self.settings.model, device="cpu", compute_type="int8")
                self._model_cache[cache_key] = model

        samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0
        if channels == 2:
            samples = samples.reshape(-1, channels).mean(axis=1)
        segments, _ = model.transcribe(
            samples,
            language=self.settings.language or None,
            beam_size=1,
            vad_filter=True,
        )
        text = " ".join(segment.text.strip() for segment in segments).strip()
        return text or "No pude detectar voz en la grabacion."
