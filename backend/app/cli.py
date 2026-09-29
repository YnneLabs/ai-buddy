"""Operator CLI for locally stored Buddy memory, history and permission policy."""

from __future__ import annotations

import argparse
import json

from .config import get_settings
from .memory import BuddyMemory


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect AI Buddy local data")
    parser.add_argument("--data-dir", default=None, help="Override AI_BUDDY_DATA_DIR")
    subcommands = parser.add_subparsers(dest="command", required=True)

    memories = subcommands.add_parser("memories")
    memories.add_argument("--device", required=True)
    memories.add_argument("--forget", type=int)

    history = subcommands.add_parser("history")
    history.add_argument("--device", required=True)
    history.add_argument("--limit", type=int, default=8)

    subcommands.add_parser("permissions")
    args = parser.parse_args()
    settings = get_settings()
    store = BuddyMemory(args.data_dir or settings.data_dir)

    if args.command == "permissions":
        print(json.dumps({"memory": "physical confirmation required", "sensitive_actions": "blocked until confirmed"}))
        return 0
    if args.command == "memories":
        if args.forget is not None:
            print(json.dumps({"forgotten": store.forget(args.device, args.forget)}))
        else:
            print(json.dumps([memory.__dict__ for memory in store.list_memories(args.device)]))
        return 0
    print(json.dumps([turn.__dict__ for turn in store.recent_turns(args.device, args.limit)]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
