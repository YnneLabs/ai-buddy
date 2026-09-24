# AI Buddy Personal Agent Plan

## Vision

AI Buddy evoluciona de una mascota digital de bring-up a un asistente personal fisico: un companion de escritorio o wearable ligero que escucha cuando se lo invoca, responde por voz, muestra estado emocional en e-paper y ejecuta acciones utiles en herramientas personales autorizadas.

El objetivo no es clonar un producto cerrado, sino construir una alternativa propia, auditable y controlada por el usuario. El buddy debe funcionar como una interfaz fisica para un agente personal: memoria, agenda, mensajes, notas, automatizaciones del hogar, busqueda privada y recordatorios contextuales.

## Principios Del Producto

- Privacidad primero: el usuario controla donde corre el backend, que datos se guardan y que herramientas puede usar el agente.
- Hardware simple: el ESP32 no razona; captura audio, reproduce audio, muestra estado, reporta sensores y ejecuta comandos locales seguros.
- Backend inteligente: STT, razonamiento, memoria, permisos, tools, TTS y observabilidad viven del lado servidor.
- Interaccion corta y natural: boton o wake flow, respuesta rapida, pantalla clara, fallbacks visibles.
- Construccion incremental: cada fase debe dejar una demo funcional y verificable.

## Arquitectura Objetivo

### Firmware

El firmware corre en `Waveshare ESP32-S3 e-Paper 1.54` y reutiliza lo ya validado en `waveshare_pet_bringup`:

- pantalla e-paper y cara de mascota;
- codec ES8311 para microfono y parlante;
- botones BOOT/PWR;
- RTC, sensor ambiental, bateria y deep sleep;
- Wi-Fi, reconexion y configuracion remota.

El protocolo nuevo debe ser propio y minimo:

- `GET /device/config`: obtiene endpoints, version de protocolo y feature flags;
- `WS /device/session`: canal bidireccional para audio, eventos, comandos y estado;
- eventos del dispositivo: boot, button, audio_chunk, audio_end, sensor_update, battery_update;
- comandos al dispositivo: speak_audio, display_state, set_emotion, show_text, sleep, reboot.

### Backend

El backend es el corazon del producto:

- gateway HTTP/WebSocket para dispositivos;
- STT o entrada de audio multimodal;
- modelo razonador principal;
- memoria personal con permisos explicitos;
- tool registry para calendario, mail, notas, web, hogar e integraciones locales;
- TTS local o self-hosted;
- panel de control para identidad, permisos, logs, dispositivos y herramientas.

### Agent Runtime

El agente debe separar claramente:

- identidad: tono, nombre, idioma, limites y estilo;
- memoria: hechos persistentes aprobados, preferencias y contexto reciente;
- herramientas: acciones externas declaradas con permisos;
- policy: reglas para no ejecutar acciones sensibles sin confirmacion;
- observabilidad: trazas de sesiones, latencia, errores y tool calls.

## Fases

### Fase 0: Base Del Producto

Entregable: este plan versionado y un repo preparado para crecer.

- Definir vision, arquitectura y fases.
- Mantener el bring-up Arduino como referencia de hardware.
- No mezclar todavia backend nuevo con firmware experimental.
- Documentar el estado real del dispositivo actual: firmware cargado, IP, endpoint OTA y causa de falla.

### Fase 1: Backend Minimo Local

Entregable: un backend local que el buddy pueda contactar en la red.

- Crear `backend/` con FastAPI o framework equivalente.
- Implementar `GET /device/config`.
- Implementar `WS /device/session` con handshake, ping/pong y logs.
- Agregar `docker-compose.yml` para levantar backend local.
- Agregar `.env.example` con host, puerto, token de dispositivo y modelo.
- Tests: health check, config endpoint, websocket handshake.

### Fase 2: Firmware Cliente Propio

Entregable: firmware que conecta a nuestro backend sin servicios externos.

- Crear `firmware/` como base limpia para el cliente del buddy.
- Reutilizar pinout, audio, display, botones, sensor y sleep del bring-up.
- Agregar Wi-Fi provisioning y persistencia de configuracion.
- Implementar `GET /device/config` y `WS /device/session`.
- Mostrar estados en e-paper: offline, connecting, online, listening, thinking, speaking, error.
- Tests manuales: boot, Wi-Fi, config fetch, websocket conectado, reconexion tras apagar backend.

### Fase 3: Voz End-To-End

Entregable: presionar BOOT, hablar, recibir respuesta audible y visible.

- Enviar audio del firmware al backend.
- Agregar STT o modelo multimodal de audio en backend.
- Generar respuesta textual del agente.
- Agregar TTS y stream de audio de vuelta al dispositivo.
- Mostrar transcripcion/respuesta corta en e-paper.
- Medir latencia total y errores.

### Fase 4: Agente Personal Basico

Entregable: asistente que recuerda contexto simple y responde como producto util.

- Definir identidad del asistente en un prompt/config versionado.
- Agregar memoria local con aprobacion explicita.
- Agregar historial corto por dispositivo/sesion.
- Implementar confirmacion para acciones sensibles.
- Agregar panel o CLI para ver memoria, logs y permisos.
- Tests: memoria opt-in, olvido de memoria, respuesta con contexto, accion bloqueada sin confirmacion.

### Fase 5: Herramientas Personales

Entregable: el buddy puede hacer trabajo real con herramientas autorizadas.

- Tool registry backend con schemas claros.
- Primeras tools: hora/clima local, notas, recordatorios, calendario local o conectado.
- Tools del dispositivo: clima del sensor, bateria, sleep, reboot, display emotion.
- Politicas de permisos por tool: lectura libre, escritura con confirmacion, acciones peligrosas bloqueadas.
- Tests: tool call exitoso, permiso denegado, confirmacion requerida, error recuperable.

### Fase 6: Experiencia De Companion

Entregable: se siente como un objeto vivo y confiable, no como una terminal.

- Estados emocionales consistentes en pantalla.
- Sonidos cortos para online/listening/error.
- Rutinas proactivas opcionales: resumen del dia, recordatorio, alerta de clima o bateria.
- Modo silencioso y modo privacidad.
- Deep sleep robusto y wake confiable.
- Tests: bateria, sleep/wake, perdida de red, backend caido, TTS fallando.

### Fase 7: Deploy Privado

Entregable: backend accesible desde dominio propio.

- Docker Compose de produccion con TLS via Caddy o Traefik.
- Tokens por dispositivo.
- Logs y metricas basicas.
- Backups de memoria/config.
- Script de instalacion local/VPS.
- Tests: renovacion TLS, reconexion remota, endpoint publico, dispositivo autorizado/no autorizado.

### Fase 8: Demo Y Narrativa

Entregable: demo publica y descripcion del proyecto.

- Preparar guion de demo: consulta personal, tool call, memoria y respuesta por voz.
- Documentar decisiones de modelo y privacidad.
- Capturar screenshots/fotos del e-paper y logs del backend.
- Crear README de uso para desarrolladores.
- Preparar video corto de funcionamiento.

## Primer Milestone Ejecutable

El primer milestone no debe intentar resolver todo. Debe demostrar la columna vertebral:

1. Levantar backend local con Docker.
2. El buddy obtiene config desde la IP correcta.
3. El buddy abre WebSocket y muestra `online`.
4. Al presionar BOOT, envia un evento al backend.
5. El backend responde con texto.
6. El buddy muestra ese texto en e-paper.

Solo despues de esa base se agrega audio end-to-end.

## Estado Actual Conocido

- El bring-up Arduino funciona como referencia de hardware.
- La placa conectada por USB arranca un firmware llamado `gemmabuddy` version `0.1.0`.
- El buddy se conecta correctamente al Wi-Fi y obtiene IP `192.168.1.26`.
- La falla actual no es Wi-Fi: intenta consultar `http://192.168.1.11:8000/xiaozhi/ota/` y ese host/puerto no responde.
- La Mac actual esta en `192.168.1.4`; no hay backend escuchando en `:8000`.
- El repo contiene una copia de XiaoZhi en `tmp_xiaozhi_src`, pero el producto nuevo no debe depender de servicios XiaoZhi oficiales.

## Criterio De Exito

AI Buddy sera considerado viable cuando pueda:

- conectarse a un backend propio desde cold boot;
- sostener una sesion de voz bidireccional;
- ejecutar herramientas personales con permisos claros;
- recordar preferencias aprobadas por el usuario;
- explicar o registrar que hizo y por que;
- fallar de manera visible y recuperable cuando no haya red/backend/modelo.
