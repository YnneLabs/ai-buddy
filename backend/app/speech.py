"""Local text-to-speech adapters for AI Buddy responses."""

from __future__ import annotations

import asyncio
import audioop
from dataclasses import dataclass
from io import BytesIO
import logging
from threading import Lock
import wave


logger = logging.getLogger("ai_buddy.speech")

TARGET_SAMPLE_RATE = 16000
TARGET_CHANNELS = 2


@dataclass(frozen=True)
class SpeechSettings:
    provider: str
    voice: str
    timeout_seconds: float


class BuddySpeech:
    """Synthesize short local Buddy replies as 16 kHz stereo PCM."""

    _voice_cache: dict[str, object] = {}
    _voice_lock = Lock()

    def __init__(self, settings: SpeechSettings) -> None:
        self.settings = settings

    async def synthesize(self, text: str) -> bytes:
        if self.settings.provider == "mock":
            return b"\x00\x00\x00\x00" * 1600
        if self.settings.provider != "piper":
            raise ValueError(f"unsupported TTS provider: {self.settings.provider}")
        return await asyncio.wait_for(
            asyncio.to_thread(self._synthesize_with_piper, text),
            timeout=self.settings.timeout_seconds,
        )

    def _synthesize_with_piper(self, text: str) -> bytes:
        from huggingface_hub import hf_hub_download
        from piper import PiperVoice

        voice_name = self.settings.voice
        locale, speaker, quality = voice_name.split("-", maxsplit=2)
        language_family = locale.split("_", maxsplit=1)[0]
        voice_path = f"{language_family}/{locale}/{speaker}/{quality}/{voice_name}.onnx"
        config_path = f"{voice_path}.json"
        with self._voice_lock:
            voice = self._voice_cache.get(voice_name)
            if voice is None:
                logger.info("loading local TTS voice voice=%s", voice_name)
                model_file = hf_hub_download("rhasspy/piper-voices", voice_path, revision="v1.0.0")
                config_file = hf_hub_download("rhasspy/piper-voices", config_path, revision="v1.0.0")
                voice = PiperVoice.load(model_file, config_path=config_file)
                self._voice_cache[voice_name] = voice

        wav_buffer = BytesIO()
        with wave.open(wav_buffer, "wb") as wav_file:
            voice.synthesize_wav(text, wav_file)
        with wave.open(BytesIO(wav_buffer.getvalue()), "rb") as wav_file:
            if wav_file.getsampwidth() != 2 or wav_file.getnchannels() != 1:
                raise ValueError("unsupported Piper audio format")
            audio = wav_file.readframes(wav_file.getnframes())
            sample_rate = wav_file.getframerate()
        if sample_rate != TARGET_SAMPLE_RATE:
            audio, _ = audioop.ratecv(audio, 2, 1, sample_rate, TARGET_SAMPLE_RATE, None)
        return audioop.tostereo(audio, 2, 1, 1)
