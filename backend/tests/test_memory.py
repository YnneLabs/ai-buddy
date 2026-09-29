from app.agent import AgentSettings, BuddyAgent
from app.memory import BuddyMemory, ConversationTurn, Memory


def test_memory_requires_physical_confirmation(tmp_path):
    store = BuddyMemory(str(tmp_path))

    proposal = store.propose_from_transcript("buddy-01", "Recuerda que prefiero cafe sin azucar.")

    assert proposal is not None
    request_id, content = proposal
    assert content == "prefiero cafe sin azucar"
    assert store.list_memories("buddy-01") == []

    saved = store.confirm("buddy-01", request_id)

    assert saved is not None
    assert saved.content == content
    assert [memory.content for memory in store.list_memories("buddy-01")] == [content]


def test_memory_can_be_rejected_or_forgotten(tmp_path):
    store = BuddyMemory(str(tmp_path))
    rejected_request, _ = store.propose_from_transcript("buddy-01", "Guarda que vivo en Buenos Aires.")

    assert store.reject("buddy-01", rejected_request)
    assert store.list_memories("buddy-01") == []

    saved_request, _ = store.propose_from_transcript("buddy-01", "Recorda que trabajo temprano.")
    saved = store.confirm("buddy-01", saved_request)

    assert saved is not None
    assert store.forget("buddy-01", saved.id)
    assert store.list_memories("buddy-01") == []


def test_agent_messages_include_only_approved_memory_and_short_history():
    memory = Memory(1, "buddy-01", "prefiere cafe sin azucar", "2026-09-29T00:00:00+00:00")
    history = [ConversationTurn("Hola", "Hola, en que te ayudo?")]
    agent = BuddyAgent(AgentSettings("mock", "gemma4:e4b", "http://localhost", 1))

    messages = agent._messages("buddy-01", "Que tomo?", "answer helpfully", [memory], history)

    assert "Identity version:" in messages[0]["content"]
    assert "prefiere cafe sin azucar" in messages[1]["content"]
    assert messages[2:4] == [
        {"role": "user", "content": "Hola"},
        {"role": "assistant", "content": "Hola, en que te ayudo?"},
    ]
