# Gemma 4 Model Choice

AI Buddy uses `gemma4:e4b` through the local Ollama runtime as its interactive model.

## Why E4B

- The Buddy needs short responses after a button press, where latency matters more than deep analysis.
- The local installation is approximately 9.6 GB and is already available on the development host.
- Running locally keeps the companion's interactions on the user's own machine and avoids a cloud model dependency.
- The device is deliberately thin: it captures interaction and renders results, while Gemma performs the conversational work in the backend.

## Escalation Path

`gemma4:31b` remains installed for later tasks that justify its higher latency, such as a daily summary, long memory consolidation, or multi-tool planning. The backend keeps the provider and model configurable through environment variables so this policy can evolve without reflashing the Buddy.

## Verified Behavior

The gateway sends a short Spanish prompt to `POST /api/chat` in Ollama when it receives a Buddy button event. The response is normalized to a short e-paper-safe message and returned as `show_text` through the active WebSocket session.
