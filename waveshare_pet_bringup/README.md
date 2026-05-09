# Waveshare Pet Bring-up

Bring-up inicial para `Waveshare ESP32-S3 e-Paper 1.54` en `Arduino IDE`.

## Qué hace

- muestra `reloj` en la esquina superior izquierda;
- dibuja una `cara de mascota`;
- el `botón PWR / inferior` entra en `sleep` con toque corto y despierta con otra pulsación;
- el `botón BOOT / superior` graba al mantener, reproduce al soltar y repite la última grabación con toque corto;
- usa el `LED indicador` en rojo cuando entra en `sleep`;
- inicializa el `RTC PCF85063` con hora de compilación si no hay hora válida.

## Librerías

Instalar desde Library Manager:

- `GxEPD2`

No requiere librerías extra para RTC, botones ni audio.

## Board en Arduino IDE

- Board: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled`
- PSRAM: `OPI PSRAM` o `Enabled` si tu menú lo muestra así
- Flash Size:
  - `8MB` para V2
  - `4MB` para V1

## Pinout usado

- e-paper: `GPIO8/9/10/11/12/13` + `EPD_PWR GPIO6`
- I2C: `GPIO47 SDA`, `GPIO48 SCL`
- I2S ES8311: `GPIO14 MCLK`, `GPIO15 BCLK`, `GPIO16 DIN`, `GPIO43 LRCK`, `GPIO44 DOUT`
- audio power: `GPIO42`
- battery hold: `GPIO17`
- LED indicador: `GPIO3`
- botones: `GPIO0` arriba, `GPIO18` abajo

## Notas

- La grabación usa `16 kHz`, `16-bit`, con slots estéreo por compatibilidad con el ejemplo oficial de Waveshare.
- En modo batería, el firmware debe poner `GPIO17` en `HIGH` para mantener latcheada la alimentación después de soltar `PWR`.
- `PWR` corto entra en `deep sleep` y despierta otra vez con `PWR`.
- el firmware arma el sleep y espera a que `PWR` quede liberado antes de entrar, para evitar wake inmediato por el mismo botón.
- `PWR` largo no hace ninguna acción en esta versión.
- Si la placa no enciende al desconectarla de USB, necesitás conectar una `batería LiPo` al header de batería; sin USB ni batería no hay alimentación.
- Si tus botones están invertidos en tu revisión de placa, intercambiá `PIN_BTN_TOP` y `PIN_BTN_BOTTOM` en [`board_pins.h`](board_pins.h).
