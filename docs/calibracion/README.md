# Calibración del cero (en curso)

Estado al 24-nov-2026: **pendiente de medir**. El aparato perdió la conexión USB (`error -71`,
desconexiones repetidas en `journalctl`); hay que estabilizar cable/puerto antes de seguir.

## EEPROM (solo lectura, `herramientas/leer_eeprom.py`)

`eeprom_dso5200a.bin`: 256 bytes (24C02; la lectura da la vuelta cada 256).

| Dirección | Contenido |
|---|---|
| `0x00` | `C0 B4 04 0A 52 00 00 00`: cabecera de arranque C0 del FX2 (VID/PID) |
| `0x08`–`0x57` | Límites de offset: 2 canales × 10 pasos × (inicio, fin), 16 bits big-endian |
| `0x60` | FASTRATECALIBRATION `01 0E 03 14 93 04` (no se usa) |
| `0x70` | ETSCORRECTION `5A 00 14 00` (no se usa) |
| resto | `FF` |

CH1: `0468/059B 046A/06AE 015E/06D1 015E/0C62 0470/06A3 0168/06CC 0168/0C58 0473/06A1 0168/06CC 0168/0C58`
CH2: `0527/0654 0523/0778 01F4/079E 01FE/0D70 0526/0773 01F4/079E 01FE/0D70 0526/0773 01F4/079E 01FE/0D70`

El núcleo ya no usa esta tabla (estaba desalineada: se leían 9 pasos y hay 10).
Nunca escribir la EEPROM (`bmRequestType 0x40` con `A2`/`A3`).

## Medición pendiente (`herramientas/medir_cero.sh <log>`)

Con cada punta en corto con su propio clip de masa, 10–15 min de calentamiento, sin la punta
defectuosa. Barre V/div 10…0.01 en ambos canales (`zero` + `cal`), y a 5, 0.5 y 0.05 V/div prueba
offsets 0.25/0.5/0.75 y acoplamiento AC. Preguntas a responder:

1. ¿El cero cambia solo entre los 3 grupos de relés o también entre V/div del mismo grupo?
2. ¿La corrección se mantiene al mover el offset? (si no, corregir en espacio DAC/pendiente)
3. ¿El cero es distinto en AC?
4. Ruido p-p, para fijar los umbrales de rechazo (6022: 10 cuentas p-p / 20 de offset a 8 bits).

Después: API get/set de `zeroCorrection` en el núcleo, guardar/cargar en archivo desde la CLI, y en
la GUI reactivar `actionCalibrateOffset` con barrido automático y `.ini` por modelo.
