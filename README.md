# AI Buddy

Firmware de bring-up para una mascota digital basada en `Waveshare ESP32-S3 e-Paper 1.54`.

Este repo tambien sera la base para construir un asistente personal propio, privado y extensible sobre el hardware del buddy. La descripcion de producto, arquitectura objetivo y fases de construccion viven en [`docs/product-plan.md`](docs/product-plan.md).

El sketch principal vive en [`waveshare_pet_bringup/`](waveshare_pet_bringup/) y está pensado para compilarse desde Arduino IDE.

## Backend Local

La Fase 1 incorpora un gateway propio para dispositivos. Expone `GET /healthz`,
`GET /device/config` y `WS /device/session`; por ahora valida el protocolo y los
eventos de la placa, sin depender de servicios XiaoZhi ni invocar un modelo.

```sh
cp .env.example .env
docker compose up --build -d
curl http://localhost:8000/healthz
docker compose run --rm --no-deps -e PYTHONPATH=/app -v "$(pwd)/backend/tests:/app/tests:ro" backend pytest -q
```

Para una placa en la misma red, configurá `AI_BUDDY_PUBLIC_BASE_URL` con la IP o el
dominio accesible desde el Buddy. El contrato y las pruebas viven en
[`backend/`](backend/).

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
