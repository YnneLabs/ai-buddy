import os

os.environ["AI_BUDDY_AGENT_PROVIDER"] = "mock"

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
    assert command == {"type": "show_text", "text": "Buddy online. I received your button."}
