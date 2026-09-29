"""HTTP and WebSocket gateway for AI Buddy devices."""

from __future__ import annotations

import json
import logging
from typing import Any, Dict
from urllib.parse import urlparse
from uuid import uuid4

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, WebSocket, WebSocketDisconnect, status
from pydantic import BaseModel, Field

from .agent import AgentSettings, BuddyAgent
from .config import Settings, get_settings
from .memory import BuddyMemory
from .grokbot import GrokbotSettings, GrokbotWebhook
from .transcription import BuddyTranscriber, TranscriptionSettings
from .speech import BuddySpeech, SpeechSettings, TARGET_CHANNELS, TARGET_SAMPLE_RATE


logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s %(message)s")
logger = logging.getLogger("ai_buddy.gateway")

app = FastAPI(title="AI Buddy Gateway", version="0.1.0")
MAX_DEVICE_AUDIO_CHUNK_BYTES = 8 * 1024


class GrokbotReply(BaseModel):
    request_id: str = Field(min_length=1, max_length=128)
    device_id: str = Field(min_length=1, max_length=128)
    response_text: str = Field(min_length=1, max_length=1000)


class DeviceConnections:
    def __init__(self) -> None:
        self._connections: dict[str, WebSocket] = {}

    def connect(self, device_id: str, websocket: WebSocket) -> None:
        self._connections[device_id] = websocket

    def disconnect(self, device_id: str, websocket: WebSocket) -> None:
        if self._connections.get(device_id) is websocket:
            self._connections.pop(device_id, None)

    def get(self, device_id: str) -> WebSocket | None:
        return self._connections.get(device_id)


connections = DeviceConnections()


def require_device_token(
    x_device_token: str = Header(default="", alias="X-Device-Token"),
    settings: Settings = Depends(get_settings),
) -> Settings:
    if x_device_token != settings.device_token:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid device token")
    return settings


def websocket_url(request: Request, settings: Settings) -> str:
    base_url = settings.public_base_url or str(request.base_url)
    parsed = urlparse(base_url)
    scheme = "wss" if parsed.scheme == "https" else "ws"
    return f"{scheme}://{parsed.netloc}/device/session"


def get_agent(settings: Settings) -> BuddyAgent:
    return BuddyAgent(
        AgentSettings(
            provider=settings.agent_provider,
            model=settings.agent_model,
            ollama_base_url=settings.ollama_base_url,
            timeout_seconds=settings.agent_timeout_seconds,
        )
    )


def get_transcriber(settings: Settings) -> BuddyTranscriber:
    return BuddyTranscriber(
        TranscriptionSettings(
            provider=settings.stt_provider,
            model=settings.stt_model,
            language=settings.stt_language,
            timeout_seconds=settings.stt_timeout_seconds,
        )
    )


def get_speech(settings: Settings) -> BuddySpeech:
    return BuddySpeech(
        SpeechSettings(
            provider=settings.tts_provider,
            voice=settings.tts_voice,
            timeout_seconds=settings.tts_timeout_seconds,
        )
    )


def get_memory(settings: Settings) -> BuddyMemory:
    return BuddyMemory(settings.data_dir)


def get_grokbot(settings: Settings) -> GrokbotWebhook:
    callback_url = ""
    if settings.public_base_url:
        callback_url = f"{settings.public_base_url}/integrations/grokbot/reply"
    return GrokbotWebhook(
        GrokbotSettings(
            webhook_url=settings.grokbot_webhook_url,
            webhook_token=settings.grokbot_webhook_token,
            callback_url=callback_url,
            timeout_seconds=settings.grokbot_timeout_seconds,
        )
    )


async def send_spoken_reply(websocket: WebSocket, settings: Settings, device_id: str, response_text: str) -> None:
    await websocket.send_json({"type": "show_text", "text": response_text})
    speech = await get_speech(settings).synthesize(response_text)
    if len(speech) > 512000:
        raise ValueError("generated audio exceeds device limit")
    await websocket.send_json(
        {
            "type": "assistant_audio_start",
            "format": "pcm_s16le",
            "sample_rate": TARGET_SAMPLE_RATE,
            "channels": TARGET_CHANNELS,
            "bytes": len(speech),
        }
    )
    for offset in range(0, len(speech), MAX_DEVICE_AUDIO_CHUNK_BYTES):
        await websocket.send_bytes(speech[offset : offset + MAX_DEVICE_AUDIO_CHUNK_BYTES])
    logger.info(
        "reply audio sent device_id=%s bytes=%s chunks=%s",
        device_id,
        len(speech),
        (len(speech) + MAX_DEVICE_AUDIO_CHUNK_BYTES - 1) // MAX_DEVICE_AUDIO_CHUNK_BYTES,
    )


@app.post("/integrations/grokbot/reply", status_code=status.HTTP_202_ACCEPTED)
async def grokbot_reply(
    reply: GrokbotReply,
    authorization: str = Header(default=""),
    settings: Settings = Depends(get_settings),
) -> Dict[str, str]:
    expected = f"Bearer {settings.grokbot_callback_token}"
    if not settings.grokbot_callback_token or authorization != expected:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid callback token")
    memory = get_memory(settings)
    pending = memory.external_request(reply.request_id)
    if pending is None or pending[0] != reply.device_id:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="unknown request")
    websocket = connections.get(reply.device_id)
    if websocket is None:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="device is offline")
    response_text = " ".join(reply.response_text.split())[:220]
    try:
        await send_spoken_reply(websocket, settings, reply.device_id, response_text)
    except Exception as exc:
        logger.warning("Grokbot reply delivery failed request_id=%s error=%s", reply.request_id, exc)
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail="reply delivery failed") from exc
    memory.append_turn(reply.device_id, pending[1], response_text)
    memory.complete_external_request(reply.request_id)
    proposal = memory.propose_from_transcript(reply.device_id, pending[1])
    if proposal is not None:
        request_id, content = proposal
        await websocket.send_json({"type": "memory_confirmation", "request_id": request_id, "content": content})
    return {"status": "delivered", "request_id": reply.request_id}


@app.get("/healthz")
def healthz(settings: Settings = Depends(get_settings)) -> Dict[str, str]:
    return {
        "status": "ok",
        "service": "ai-buddy-gateway",
        "protocol_version": settings.protocol_version,
    }


@app.get("/device/config")
def device_config(
    request: Request,
    device_id: str = Query(..., min_length=1, max_length=128),
    settings: Settings = Depends(require_device_token),
) -> Dict[str, Any]:
    logger.info("config requested device_id=%s", device_id)
    return {
        "protocol_version": settings.protocol_version,
        "device_id": device_id,
        "session_url": websocket_url(request, settings),
        "agent_model": settings.agent_model,
        "features": {
            "audio_streaming": True,
            "audio_upload": True,
            "display_text": True,
            "remote_commands": True,
            "approved_memory": True,
        },
    }


@app.websocket("/device/session")
async def device_session(
    websocket: WebSocket,
    token: str = Query(default=""),
) -> None:
    settings = get_settings()
    if token != settings.device_token:
        await websocket.close(code=status.WS_1008_POLICY_VIOLATION, reason="invalid device token")
        return

    await websocket.accept()
    session_id = str(uuid4())
    device_id = "unknown"
    pending_audio: dict[str, Any] | None = None
    logger.info("session connected session_id=%s", session_id)

    try:
        while True:
            frame = await websocket.receive()
            if frame["type"] == "websocket.disconnect":
                raise WebSocketDisconnect
            audio_bytes = frame.get("bytes")
            if audio_bytes is not None:
                if pending_audio is None:
                    await websocket.send_json({"type": "error", "code": "unexpected_audio"})
                    continue
                expected_bytes = pending_audio["bytes"]
                if len(audio_bytes) != expected_bytes:
                    await websocket.send_json(
                        {"type": "error", "code": "invalid_audio_length", "expected": expected_bytes}
                    )
                    pending_audio = None
                    continue
                try:
                    transcript = await get_transcriber(settings).transcribe_pcm(
                        audio_bytes,
                        pending_audio["sample_rate"],
                        pending_audio["channels"],
                    )
                except Exception as exc:  # Keep a transcription failure from dropping the device session.
                    logger.warning("audio transcription failed device_id=%s error=%s", device_id, exc)
                    await websocket.send_json({"type": "error", "code": "transcription_failed"})
                else:
                    logger.info("audio transcribed device_id=%s bytes=%s", device_id, len(audio_bytes))
                    await websocket.send_json({"type": "transcription", "text": transcript})
                    try:
                        memory = get_memory(settings)
                        grokbot = get_grokbot(settings)
                        if grokbot.enabled:
                            request_id = memory.create_external_request(device_id, transcript)
                            await grokbot.dispatch(request_id, device_id, transcript)
                            await websocket.send_json({"type": "show_text", "text": "Consultando Grokbot..."})
                        else:
                            response_text = await get_agent(settings).respond_to_transcript(
                                device_id, transcript, memory.list_memories(device_id), memory.recent_turns(device_id)
                            )
                            memory.append_turn(device_id, transcript, response_text)
                            await send_spoken_reply(websocket, settings, device_id, response_text)
                            proposal = memory.propose_from_transcript(device_id, transcript)
                            if proposal is not None:
                                request_id, content = proposal
                                await websocket.send_json(
                                    {"type": "memory_confirmation", "request_id": request_id, "content": content}
                                )
                    except Exception as exc:  # Preserve the session if the model or TTS fails.
                        logger.warning("voice response failed device_id=%s error=%s", device_id, exc)
                        await websocket.send_json({"type": "error", "code": "voice_response_failed"})
                pending_audio = None
                continue

            text = frame.get("text")
            if text is None:
                await websocket.send_json({"type": "error", "code": "invalid_message"})
                continue
            try:
                message = json.loads(text)
            except json.JSONDecodeError:
                await websocket.send_json({"type": "error", "code": "invalid_message"})
                continue
            if not isinstance(message, dict):
                await websocket.send_json({"type": "error", "code": "invalid_message"})
                continue

            event_type = message.get("type")
            if event_type == "hello":
                device_id = str(message.get("device_id", "unknown"))
                connections.connect(device_id, websocket)
                logger.info("session ready session_id=%s device_id=%s", session_id, device_id)
                await websocket.send_json(
                    {
                        "type": "session_ready",
                        "session_id": session_id,
                        "protocol_version": settings.protocol_version,
                    }
                )
            elif event_type == "ping":
                await websocket.send_json({"type": "pong"})
            elif event_type == "audio_start":
                sample_rate = message.get("sample_rate")
                channels = message.get("channels")
                byte_count = message.get("bytes")
                if (
                    message.get("format") != "pcm_s16le"
                    or not isinstance(sample_rate, int)
                    or not isinstance(channels, int)
                    or not isinstance(byte_count, int)
                    or sample_rate != 16000
                    or channels != 2
                    or byte_count <= 0
                    or byte_count > 512000
                    or byte_count % (channels * 2) != 0
                ):
                    await websocket.send_json({"type": "error", "code": "invalid_audio_metadata"})
                    continue
                pending_audio = {"sample_rate": sample_rate, "channels": channels, "bytes": byte_count}
                await websocket.send_json({"type": "audio_receiving"})
            elif event_type == "button":
                button = str(message.get("button", "unknown"))
                logger.info("button event session_id=%s device_id=%s button=%s", session_id, device_id, button)
                response_text = await get_agent(settings).respond_to_button(device_id)
                await websocket.send_json(
                    {
                        "type": "show_text",
                        "text": response_text,
                    }
                )
            elif event_type in {"memory_confirm", "memory_reject"}:
                request_id = str(message.get("request_id", ""))
                memory = get_memory(settings)
                if event_type == "memory_confirm":
                    saved = memory.confirm(device_id, request_id)
                    if saved:
                        await websocket.send_json({"type": "memory_saved", "memory_id": saved.id})
                    else:
                        await websocket.send_json({"type": "error", "code": "memory_not_found"})
                else:
                    rejected = memory.reject(device_id, request_id)
                    if rejected:
                        await websocket.send_json({"type": "memory_rejected"})
                    else:
                        await websocket.send_json({"type": "error", "code": "memory_not_found"})
            elif event_type == "action_request":
                await websocket.send_json({"type": "action_blocked", "reason": "confirmation_required"})
            else:
                logger.info("event received session_id=%s device_id=%s type=%s", session_id, device_id, event_type)
                await websocket.send_json({"type": "event_received", "event_type": event_type})
    except WebSocketDisconnect:
        connections.disconnect(device_id, websocket)
        logger.info("session disconnected session_id=%s device_id=%s", session_id, device_id)
