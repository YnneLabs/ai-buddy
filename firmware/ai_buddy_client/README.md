# AI Buddy Client Firmware

Firmware Arduino para `Waveshare ESP32-S3 e-Paper 1.54` que se conecta al gateway propio de este repositorio. No usa servicios XiaoZhi.

## Alcance de Fase 2

- portal de aprovisionamiento Wi-Fi protegido por password;
- persistencia de Wi-Fi, URL del backend, token e ID en NVS;
- `GET /device/config` autenticado;
- sesión `WS /device/session` con `hello`, `boot` y eventos de `BOOT`;
- e-paper con estados de setup, conexión, online y error;
- reconexión al perder Wi-Fi o WebSocket;
- mantener `BOOT` durante tres segundos borra la configuración y reabre el portal.

Audio, TTS y deep sleep quedan para fases posteriores.

## Dependencias

Instaladas y verificadas con Arduino CLI:

- `GxEPD2` 1.6.9
- `ArduinoJson` 7.4.3
- `WebSockets` 2.7.2

## Compilar

```sh
/Users/enny/bin/arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=cdc \
  firmware/ai_buddy_client
```

Para una placa V1 reemplazá `FlashSize=8M,PartitionScheme=default_8MB` por
`FlashSize=4M`. El cliente actual entra en el límite de la partición de 4 MB, pero
la V2 de 8 MB es la configuración recomendada para dejar espacio a las fases de voz.

## Primer Arranque

1. Flasheá el sketch.
2. Conectate a la red Wi-Fi `AI-Buddy-Setup`.
3. La contraseña aparece en la pantalla del Buddy y deriva de su MAC.
4. Abrí `http://192.168.4.1`.
5. Ingresá la red Wi-Fi, el backend de la LAN, por ejemplo `http://192.168.1.4:8000`, y el token configurado en `.env`.
6. Guardá: el Buddy reinicia, obtiene la configuración y abre su sesión.

La URL debe ser accesible desde la placa. Para el backend local, definí `AI_BUDDY_PUBLIC_BASE_URL` con esa misma URL antes de levantar Docker.

## Contrato Esperado

El gateway debe responder `session_url` con `ws://` durante desarrollo local. TLS y `wss://` se agregan en la Fase 7.
