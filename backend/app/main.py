"""HTTP and WebSocket gateway for AI Buddy devices."""

from __future__ import annotations

import logging
from typing import Any, Dict
from urllib.parse import urlparse
from uuid import uuid4

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, WebSocket, WebSocketDisconnect, status

from .agent import AgentSettings, BuddyAgent
from .config import Settings, get_settings


logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s %(message)s")
logger = logging.getLogger("ai_buddy.gateway")

app = FastAPI(title="AI Buddy Gateway", version="0.1.0")


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
            "audio_streaming": False,
            "display_text": True,
            "remote_commands": True,
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
    logger.info("session connected session_id=%s", session_id)

    try:
        while True:
            message = await websocket.receive_json()
            if not isinstance(message, dict):
                await websocket.send_json({"type": "error", "code": "invalid_message"})
                continue

            event_type = message.get("type")
            if event_type == "hello":
                device_id = str(message.get("device_id", "unknown"))
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
            else:
                logger.info("event received session_id=%s device_id=%s type=%s", session_id, device_id, event_type)
                await websocket.send_json({"type": "event_received", "event_type": event_type})
    except WebSocketDisconnect:
        logger.info("session disconnected session_id=%s device_id=%s", session_id, device_id)
