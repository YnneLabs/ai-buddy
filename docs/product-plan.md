# AI Buddy Personal Agent Roadmap

**Actualizado:** 2026-09-28

## Estado Del Producto

El primer vertical slice ya funciona en hardware real. El Buddy arranca, se
conecta al Wi-Fi configurado, obtiene su configuracion del gateway propio,
mantiene una sesion WebSocket y muestra en e-paper una respuesta producida por
Gemma local al presionar `BOOT`.

| Area | Estado | Evidencia |
| --- | --- | --- |
| Gateway propio | Completo | FastAPI en Docker con `GET /device/config` y `WS /device/session`. |
| Red y provisionamiento | Completo | Portal local, WPS y valores de provisionamiento locales ignorados por Git. |
| Firmware propio | Completo para texto | ESP32-S3 sin dependencia de XiaoZhi; estados en e-paper y reconexion. |
| Gemma | Completo para texto | `gemma4:e4b` en Ollama responde eventos de boton. |
| Audio, memoria y tools | Pendiente | Siguiente bloque de producto. |

La ultima sesion verificada asigno al dispositivo `buddy-1D0470` una IP de LAN
dinamica y establecio una sesion autenticada contra el gateway. No se debe
depender de esa IP: el dispositivo obtiene una direccion nueva por DHCP cuando
la red lo requiera.

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

### Fase 0: Base Del Producto - Completada

Entregable: este plan versionado y un repo preparado para crecer.

- Definir vision, arquitectura y fases.
- Mantener el bring-up Arduino como referencia de hardware.
- No mezclar todavia backend nuevo con firmware experimental.
- Documentar el estado real del dispositivo y retirar la dependencia de XiaoZhi.

### Fase 1: Backend Minimo Local - Completada

Entregable: un backend local que el buddy pueda contactar en la red.

- `backend/` usa FastAPI y `docker-compose.yml` levanta el gateway en el puerto 8000.
- `GET /device/config` y `WS /device/session` implementan token, handshake, ping/pong y logs.
- `AI_BUDDY_PUBLIC_BASE_URL` evita que un dispositivo LAN reciba una URL `127.0.0.1` invalida.
- Las pruebas cubren health check, autenticacion, contrato de configuracion y WebSocket.

### Fase 2: Firmware Cliente Propio - Completada Para Texto

Entregable: firmware que conecta a nuestro backend sin servicios externos.

- `firmware/ai_buddy_client` es un cliente ESP32-S3 propio con pantalla, botones, portal Wi-Fi, WPS y configuracion persistente.
- Implementa `GET /device/config`, `WS /device/session`, evento `boot` y evento `button`.
- El e-paper muestra arranque, provisionamiento, conexion Wi-Fi, configuracion, sesion, online y errores.
- `local_config.h` permite inyectar credenciales de un entorno de flasheo sin incluirlas en Git.
- Validado manualmente: cold boot, Wi-Fi, fetch de configuracion, WebSocket autenticado y respuesta de Gemma renderizada.

### Fase 3: Voz End-To-End - Siguiente

Entregable: presionar BOOT, hablar, recibir respuesta audible y visible.

- Interaccion principal: un toque de `BOOT` inicia la escucha; el siguiente toque finaliza la captura y envia el turno.
- `BOOT` mantenido durante tres segundos conserva el reset de configuracion; `PWR` conserva WPS.
- Confirmar y probar el camino de audio del hardware: I2S, ES8311, microfono y parlante.
- Definir el protocolo binario de `audio_chunk` y `audio_end`, con limites de duracion, tamanos y backpressure.
- Agregar STT local y conservar la transcripcion por sesion efimera.
- Reusar Gemma para la respuesta textual y agregar TTS local con audio de vuelta al dispositivo.
- Mostrar transcripcion y respuesta breve en e-paper; medir latencia de captura a voz.
- Criterio de salida: presionar `BOOT`, hablar una pregunta y oir una respuesta en menos de 8 segundos en LAN.

### Fase 4: Agente Personal Basico - Planificada

Entregable: asistente que recuerda contexto simple y responde como producto util.

- Definir identidad del asistente en un prompt/config versionado.
- Agregar memoria local con aprobacion explicita.
- Agregar historial corto por dispositivo/sesion.
- Implementar confirmacion para acciones sensibles.
- Agregar panel o CLI para ver memoria, logs y permisos.
- Tests: memoria opt-in, olvido de memoria, respuesta con contexto, accion bloqueada sin confirmacion.

### Fase 5: Herramientas Personales - Planificada

Entregable: el buddy puede hacer trabajo real con herramientas autorizadas.

- Tool registry backend con schemas claros.
- Primeras tools: hora/clima local, notas, recordatorios, calendario local o conectado.
- Tools del dispositivo: clima del sensor, bateria, sleep, reboot, display emotion.
- Politicas de permisos por tool: lectura libre, escritura con confirmacion, acciones peligrosas bloqueadas.
- Tests: tool call exitoso, permiso denegado, confirmacion requerida, error recuperable.

### Fase 6: Experiencia De Companion - Planificada

Entregable: se siente como un objeto vivo y confiable, no como una terminal.

- Estados emocionales consistentes en pantalla.
- Sonidos cortos para online/listening/error.
- Rutinas proactivas opcionales: resumen del dia, recordatorio, alerta de clima o bateria.
- Modo silencioso y modo privacidad.
- Deep sleep robusto y wake confiable.
- Tests: bateria, sleep/wake, perdida de red, backend caido, TTS fallando.

### Fase 7: Deploy Privado - Planificada

Entregable: backend accesible desde dominio propio.

- Docker Compose de produccion con TLS via Caddy o Traefik.
- Tokens por dispositivo.
- Logs y metricas basicas.
- Backups de memoria/config.
- Script de instalacion local/VPS.
- Tests: renovacion TLS, reconexion remota, endpoint publico, dispositivo autorizado/no autorizado.

### Fase 8: Demo Y Narrativa - Planificada

Entregable: demo publica y descripcion del proyecto.

- Preparar guion de demo: consulta personal, tool call, memoria y respuesta por voz.
- Documentar decisiones de modelo y privacidad.
- Capturar screenshots/fotos del e-paper y logs del backend.
- Crear README de uso para desarrolladores.
- Preparar video corto de funcionamiento.

## Primer Milestone Ejecutable - Completado

El primer milestone no debe intentar resolver todo. Debe demostrar la columna vertebral:

1. Levantar backend local con Docker.
2. El buddy obtiene config desde la IP correcta.
3. El buddy abre WebSocket y muestra `online`.
4. Al presionar BOOT, envia un evento al backend.
5. El backend responde con texto.
6. El buddy muestra ese texto en e-paper.

Este milestone esta verificado con Gemma real. Para respuestas breves, el
gateway envia `think: false` a Ollama: Gemma 4 puede consumir todo el limite de
tokens en razonamiento interno antes de producir texto visible. La salida se
restringe a ASCII porque la fuente actual del e-paper no cubre de forma
confiable emojis ni UTF-8.

## Siguiente Entrega: Fase 3

1. Auditar el codec y el pinout de audio con una grabacion/reproduccion local.
2. Implementar captura toggle desde `BOOT`: primer toque inicia, segundo toque envia y tres segundos reinicia configuracion.
3. Definir y probar audio WebSocket con muestras cortas antes de integrar STT/TTS.
4. Integrar STT y TTS locales, instrumentando tiempos por etapa.
5. Hacer una demo completa de pregunta y respuesta hablada.

## Riesgos Y Decisiones Actuales

- El backend LAN depende de que `AI_BUDDY_PUBLIC_BASE_URL` coincida con la IP o dominio visible por el Buddy. La Fase 7 elimina esta dependencia con un dominio y TLS.
- El token por defecto solo sirve para desarrollo local; se reemplazara por tokens unicos por dispositivo antes de exponer el servicio.
- El ESP32 no ejecuta inferencia: Gemma 4 se ejecuta en Ollama en el host, una seleccion adecuada para mantener el dispositivo ligero y dar respuestas con baja latencia en LAN.
- La Fase 3 debe validar primero el codec fisico antes de comprometer un proveedor de STT/TTS.

## Criterio De Exito

AI Buddy sera considerado viable cuando pueda:

- conectarse a un backend propio desde cold boot;
- sostener una sesion de voz bidireccional;
- ejecutar herramientas personales con permisos claros;
- recordar preferencias aprobadas por el usuario;
- explicar o registrar que hizo y por que;
- fallar de manera visible y recuperable cuando no haya red/backend/modelo.
