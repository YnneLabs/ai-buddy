"""Local, explicit-consent memory and short conversation history for a Buddy."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import re
import sqlite3
from uuid import uuid4


MEMORY_REQUEST_PATTERN = re.compile(
    r"^\s*(?:recuerda|recorda|guarda)\s+(?:que\s+)?(.+?)\s*[.!?]*\s*$", re.IGNORECASE
)


@dataclass(frozen=True)
class Memory:
    id: int
    device_id: str
    content: str
    created_at: str


@dataclass(frozen=True)
class ConversationTurn:
    user_text: str
    assistant_text: str


class BuddyMemory:
    """SQLite storage. A proposal is never persisted as a usable memory until confirmed."""

    def __init__(self, data_dir: str) -> None:
        directory = Path(data_dir)
        directory.mkdir(parents=True, exist_ok=True)
        self.database_path = directory / "buddy.sqlite3"
        self._initialize()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.database_path)
        connection.row_factory = sqlite3.Row
        return connection

    def _initialize(self) -> None:
        with self._connect() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS memories (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT NOT NULL,
                    content TEXT NOT NULL,
                    created_at TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS pending_memories (
                    request_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    content TEXT NOT NULL,
                    created_at TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS conversation_turns (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT NOT NULL,
                    user_text TEXT NOT NULL,
                    assistant_text TEXT NOT NULL,
                    created_at TEXT NOT NULL
                );
                """
            )

    def propose_from_transcript(self, device_id: str, transcript: str) -> tuple[str, str] | None:
        match = MEMORY_REQUEST_PATTERN.match(transcript)
        if not match:
            return None
        content = " ".join(match.group(1).split())[:220]
        if not content:
            return None
        request_id = str(uuid4())
        with self._connect() as connection:
            connection.execute(
                "INSERT INTO pending_memories (request_id, device_id, content, created_at) VALUES (?, ?, ?, ?)",
                (request_id, device_id, content, self._now()),
            )
        return request_id, content

    def confirm(self, device_id: str, request_id: str) -> Memory | None:
        with self._connect() as connection:
            pending = connection.execute(
                "SELECT content FROM pending_memories WHERE request_id = ? AND device_id = ?",
                (request_id, device_id),
            ).fetchone()
            if pending is None:
                return None
            created_at = self._now()
            cursor = connection.execute(
                "INSERT INTO memories (device_id, content, created_at) VALUES (?, ?, ?)",
                (device_id, pending["content"], created_at),
            )
            connection.execute("DELETE FROM pending_memories WHERE request_id = ?", (request_id,))
            return Memory(cursor.lastrowid, device_id, pending["content"], created_at)

    def reject(self, device_id: str, request_id: str) -> bool:
        with self._connect() as connection:
            cursor = connection.execute(
                "DELETE FROM pending_memories WHERE request_id = ? AND device_id = ?", (request_id, device_id)
            )
        return cursor.rowcount == 1

    def list_memories(self, device_id: str) -> list[Memory]:
        with self._connect() as connection:
            rows = connection.execute(
                "SELECT id, device_id, content, created_at FROM memories WHERE device_id = ? ORDER BY id DESC",
                (device_id,),
            ).fetchall()
        return [Memory(**dict(row)) for row in rows]

    def forget(self, device_id: str, memory_id: int) -> bool:
        with self._connect() as connection:
            cursor = connection.execute("DELETE FROM memories WHERE id = ? AND device_id = ?", (memory_id, device_id))
        return cursor.rowcount == 1

    def append_turn(self, device_id: str, user_text: str, assistant_text: str) -> None:
        with self._connect() as connection:
            connection.execute(
                "INSERT INTO conversation_turns (device_id, user_text, assistant_text, created_at) VALUES (?, ?, ?, ?)",
                (device_id, user_text[:500], assistant_text[:500], self._now()),
            )

    def recent_turns(self, device_id: str, limit: int = 4) -> list[ConversationTurn]:
        with self._connect() as connection:
            rows = connection.execute(
                """SELECT user_text, assistant_text FROM conversation_turns
                WHERE device_id = ? ORDER BY id DESC LIMIT ?""",
                (device_id, limit),
            ).fetchall()
        return [ConversationTurn(**dict(row)) for row in reversed(rows)]

    @staticmethod
    def _now() -> str:
        return datetime.now(timezone.utc).isoformat(timespec="seconds")
