import os
from unittest.mock import AsyncMock

os.environ["AI_BUDDY_AGENT_PROVIDER"] = "mock"
os.environ["AI_BUDDY_STT_PROVIDER"] = "mock"
os.environ["AI_BUDDY_TTS_PROVIDER"] = "mock"
os.environ["AI_BUDDY_DATA_DIR"] = "/tmp/ai-buddy-tests"

from fastapi.testclient import TestClient

from app.main import app


client = TestClient(app)
TOKEN = "local-development-token"


def test_healthz_reports_gateway_status():
    response = client.get("/healthz")

    assert response.status_code == 200
    assert response.json()["status"] == "ok"


def test_device_config_requires_token():
    response = client.get("/device/config?device_id=buddy-01")

    assert response.status_code == 401


def test_device_config_returns_protocol_contract():
    response = client.get(
        "/device/config?device_id=buddy-01",
        headers={"X-Device-Token": TOKEN},
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["device_id"] == "buddy-01"
    assert payload["protocol_version"] == "1"
    assert payload["session_url"].endswith("/device/session")


def test_websocket_handshake_ping_and_button_event():
    with client.websocket_connect(f"/device/session?token={TOKEN}") as websocket:
        websocket.send_json({"type": "hello", "device_id": "buddy-01"})
        ready = websocket.receive_json()
        websocket.send_json({"type": "ping"})
        pong = websocket.receive_json()
        websocket.send_json({"type": "button", "button": "boot"})
        command = websocket.receive_json()

    assert ready["type"] == "session_ready"
    assert ready["protocol_version"] == "1"
    assert pong == {"type": "pong"}
    assert command == {"type": "show_text", "text": "Recibi tu mensaje."}


def test_websocket_receives_pcm_and_returns_transcription():
    pcm = b"\x00\x00\x00\x00" * 16000
    with client.websocket_connect(f"/device/session?token={TOKEN}") as websocket:
        websocket.send_json({"type": "hello", "device_id": "buddy-01"})
        websocket.receive_json()
        websocket.send_json(
            {
                "type": "audio_start",
                "format": "pcm_s16le",
                "sample_rate": 16000,
                "channels": 2,
                "bytes": len(pcm),
            }
        )
        receiving = websocket.receive_json()
        websocket.send_bytes(pcm)
        transcription = websocket.receive_json()
        thinking = websocket.receive_json()
        response = websocket.receive_json()
        speech_start = websocket.receive_json()
        speech = websocket.receive_bytes()

    assert receiving == {"type": "audio_receiving"}
    assert transcription == {"type": "transcription", "text": "Audio recibido localmente (1.0 segundos)."}
    assert thinking == {"type": "thinking", "provider": "gemma", "text": "Pensando..."}
    assert response == {"type": "show_text", "text": "Recibi tu mensaje."}
    assert speech_start == {
        "type": "assistant_audio_start",
        "format": "pcm_s16le",
        "sample_rate": 16000,
        "channels": 2,
        "bytes": 6400,
    }
    assert speech == b"\x00\x00\x00\x00" * 1600


def test_websocket_emits_thinking_before_local_gemma_reply(monkeypatch):
    transcriber = type("Transcriber", (), {"transcribe_pcm": AsyncMock(return_value="Hola Buddy")})()
    monkeypatch.setattr("app.main.get_transcriber", lambda settings: transcriber)
    pcm = b"\x00\x00\x00\x00" * 100
    with client.websocket_connect(f"/device/session?token={TOKEN}") as websocket:
        websocket.send_json({"type": "hello", "device_id": "buddy-thinking"})
        websocket.receive_json()
        websocket.send_json(
            {"type": "audio_start", "format": "pcm_s16le", "sample_rate": 16000, "channels": 2, "bytes": len(pcm)}
        )
        websocket.receive_json()
        websocket.send_bytes(pcm)
        websocket.receive_json()
        thinking = websocket.receive_json()

    assert thinking == {"type": "thinking", "provider": "gemma", "text": "Pensando..."}


def test_websocket_blocks_sensitive_action_without_confirmation():
    with client.websocket_connect(f"/device/session?token={TOKEN}") as websocket:
        websocket.send_json({"type": "hello", "device_id": "buddy-01"})
        websocket.receive_json()
        websocket.send_json({"type": "action_request", "action": "send_message"})
        blocked = websocket.receive_json()

    assert blocked == {"type": "action_blocked", "reason": "confirmation_required"}


def test_grokbot_callback_rejects_invalid_token():
    response = client.post(
        "/integrations/grokbot/reply",
        json={"request_id": "request-01", "device_id": "buddy-01", "response_text": "Hola"},
    )

    assert response.status_code == 401


def test_grokbot_callback_rejects_unknown_request(monkeypatch):
    monkeypatch.setenv("AI_BUDDY_GROKBOT_CALLBACK_TOKEN", "callback-test-token")

    response = client.post(
        "/integrations/grokbot/reply",
        headers={"Authorization": "Bearer callback-test-token"},
        json={"request_id": "request-01", "device_id": "buddy-01", "response_text": "Hola"},
    )

    assert response.status_code == 404


def test_grokbot_can_send_a_proactive_message_without_request_id(monkeypatch):
    monkeypatch.setenv("AI_BUDDY_GROKBOT_CALLBACK_TOKEN", "callback-test-token")

    with client.websocket_connect(f"/device/session?token={TOKEN}") as websocket:
        websocket.send_json({"type": "hello", "device_id": "buddy-proactive"})
        websocket.receive_json()
        response = client.post(
            "/integrations/grokbot/reply",
            headers={"Authorization": "Bearer callback-test-token"},
            json={"device_id": "buddy-proactive", "response_text": "Tienes una nueva alerta."},
        )
        caption = websocket.receive_json()
        audio_start = websocket.receive_json()
        audio = websocket.receive_bytes()

    assert response.status_code == 202
    assert response.json() == {"status": "delivered", "request_id": "none", "mode": "proactive"}
    assert caption == {"type": "show_text", "text": "Tienes una nueva alerta."}
    assert audio_start["type"] == "assistant_audio_start"
    assert audio == b"\x00\x00\x00\x00" * 1600
