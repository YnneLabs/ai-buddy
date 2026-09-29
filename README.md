# AI Buddy

Firmware de bring-up para una mascota digital basada en `Waveshare ESP32-S3 e-Paper 1.54`.

Este repo tambien sera la base para construir un asistente personal propio, privado y extensible sobre el hardware del buddy. La descripcion de producto, arquitectura objetivo y fases de construccion viven en [`docs/product-plan.md`](docs/product-plan.md).

El sketch principal vive en [`waveshare_pet_bringup/`](waveshare_pet_bringup/) y está pensado para compilarse desde Arduino IDE.

## Backend Local

La Fase 1 incorpora un gateway propio para dispositivos. Expone `GET /healthz`,
`GET /device/config` y `WS /device/session`; por ahora valida el protocolo y los
eventos de la placa, sin depender de servicios XiaoZhi. Al usar Ollama, el evento
de boton llama a Gemma 4 local y devuelve una respuesta corta al e-paper.

```sh
cp .env.example .env
docker compose up --build -d
curl http://localhost:8000/healthz
docker compose run --rm --no-deps -e PYTHONPATH=/app -v "$(pwd)/backend/tests:/app/tests:ro" backend pytest -q
```

Para una placa en la misma red, configurá `AI_BUDDY_PUBLIC_BASE_URL` con la IP o el
dominio accesible desde el Buddy. El contrato y las pruebas viven en
[`backend/`](backend/).

## Mac mini Behind Cloudflare Tunnel

Set `AI_BUDDY_PUBLIC_BASE_URL=https://ai-buddy.evil-gamer.net` and
`CLOUDFLARE_TUNNEL_TOKEN` in the Mac mini's `.env`. Start the application and
its outbound tunnel together:

```bash
docker compose --profile tunnel up -d
```

Cloudflare routes `ai-buddy.evil-gamer.net` to the tunnel's local service at
`http://backend:8000`; no router port-forwarding is required.

The firmware accepts both local `http`/`ws` URLs and deployed `https`/`wss`
URLs. A provisioned device can be migrated once using the two
`AI_BUDDY_BACKEND_MIGRATE_*` settings in `local_config.h`.

The Grokbot route is asynchronous: Cursor schedules the automation before it
can issue the callback. Buddy can play at most 8 seconds of PCM output per
message, so keep `AI_BUDDY_GROKBOT_RESPONSE_MAX_CHARS` at 100 or less. The
gateway rejects a synthesis that exceeds the device buffer with
`audio_too_long`; its logs report STT, dispatch, callback, and TTS timings
separately.

`FastRouter` keeps greetings, acknowledgements, and short social exchanges on
local Gemma. Reminders, information queries, and every non-trivial request use
Grokbot, which has the external context and action workflow.

## Firmware Del Buddy

El cliente de red propio vive en [`firmware/ai_buddy_client/`](firmware/ai_buddy_client/).
Reutiliza el pinout validado del bring-up, ofrece un portal Wi-Fi local y se comunica
con el gateway sin depender del firmware ni de los servicios XiaoZhi.

Cuando `AI_BUDDY_AGENT_PROVIDER=ollama`, el evento del boton llama a Gemma 4 local
(`gemma4:e4b`) y muestra su respuesta corta en el e-paper. El proveedor `mock` se
usa solo para pruebas reproducibles.
La seleccion y el camino de escalamiento estan en
[`docs/gemma-model-choice.md`](docs/gemma-model-choice.md).

La primera etapa de voz envia la captura del Buddy como PCM estereo de 16 kHz por
el WebSocket propio. El gateway la transcribe localmente con `faster-whisper` y
muestra el texto en el e-paper. Por defecto se usa el modelo `base` en espanol;
su descarga se realiza una unica vez y se conserva en el volumen Docker
`stt_models`. Configuralo mediante `AI_BUDDY_STT_PROVIDER`,
`AI_BUDDY_STT_MODEL` y `AI_BUDDY_STT_LANGUAGE`. Para pruebas sin modelo se puede
usar `AI_BUDDY_STT_PROVIDER=mock`.

Despues de transcribir, el gateway entrega el texto a Gemma 4 local y sintetiza
la respuesta con Piper antes de devolver PCM estereo de 16 kHz al Buddy. La voz
predeterminada es `es_AR-daniela-high`; se conserva en el mismo volumen local de
modelos y puede configurarse con `AI_BUDDY_TTS_PROVIDER` y `AI_BUDDY_TTS_VOICE`.

## Memoria Local

La Fase 4 agrega una identidad versionada, historial corto por dispositivo y
memoria SQLite local. Decir `recuerda que ...`, `recorda que ...` o `guarda que
...` nunca escribe datos de inmediato: el e-paper mostrara la propuesta, `BOOT`
la aprueba y un toque corto de `PWR` la descarta. Solo los recuerdos aprobados
forman parte del contexto de Gemma. Las acciones sensibles siguen bloqueadas
hasta recibir un mecanismo de confirmacion dedicado en la Fase 5.

Para inspeccionar los datos persistidos desde el contenedor:

```sh
docker compose exec backend python -m app.cli memories --device buddy-1D0470
docker compose exec backend python -m app.cli history --device buddy-1D0470
docker compose exec backend python -m app.cli permissions
```

## Grokbot Via Cursor Automation

El endpoint de Cursor inicia una automatizacion asincrona, por lo que el texto
de respuesta debe volver al gateway antes de poder hablarse en el Buddy. Al
configurar `AI_BUDDY_GROKBOT_WEBHOOK_URL`,
`AI_BUDDY_GROKBOT_WEBHOOK_TOKEN` y `AI_BUDDY_GROKBOT_CALLBACK_TOKEN`, cada
transcripcion se envia a Cursor con `request_id`, `device_id`, `transcript` y
`callback_url`. La automatizacion debe responder con un POST autenticado:

```sh
curl -X POST https://ai-buddy.evil-gamer.net/integrations/grokbot/reply \
  -H 'Authorization: Bearer <AI_BUDDY_GROKBOT_CALLBACK_TOKEN>' \
  -H 'Content-Type: application/json' \
  -d '{"request_id":"<request_id>","device_id":"buddy-1D0470","response_text":"Respuesta para hablar"}'
```

El backend valida el request, sintetiza `response_text` con Piper y lo envia
al Buddy por su WebSocket activo. Sin esas tres variables, el backend conserva
Gemma local como fallback.

Grokbot tambien puede iniciar un mensaje sin una transcripcion previa ni
`request_id`. Usa el mismo endpoint, autenticacion y `device_id`:

```sh
curl -X POST https://ai-buddy.evil-gamer.net/integrations/grokbot/reply \
  -H 'Authorization: Bearer <AI_BUDDY_GROKBOT_CALLBACK_TOKEN>' \
  -H 'Content-Type: application/json' \
  -d '{"device_id":"buddy-1D0470","response_text":"Tienes un recordatorio pendiente."}'
```

El gateway responde `202` con `mode: proactive`, muestra el texto y lo habla
de inmediato. El Buddy debe estar conectado; de lo contrario responde `409`.

## Estado

Incluye:

- reloj en pantalla e-paper;
- cara de mascota;
- lectura de RTC y sensor ambiental;
- grabación y reproducción de audio con el codec ES8311;
- entrada y salida de deep sleep usando el botón PWR.

## Uso

Abrí [`waveshare_pet_bringup/waveshare_pet_bringup.ino`](waveshare_pet_bringup/waveshare_pet_bringup.ino) en Arduino IDE y seguí las instrucciones de [`waveshare_pet_bringup/README.md`](waveshare_pet_bringup/README.md).

## Licencia

Este proyecto se publica bajo licencia MIT. Ver [`LICENSE`](LICENSE).
