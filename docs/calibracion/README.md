# Calibración del cero (en curso)

Estado al 24-nov-2026: **medido. No hace falta autocalibrar el cero**: el modelo actual
(ley del DAC de offset) ya deja el cero a menos de 1 cuenta en todos los casos (ver "Resultados").

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

## Medición (`herramientas/medir_cero.sh <log>`)

Con cada punta en corto con su propio clip de masa, 10–15 min de calentamiento, sin la punta
defectuosa. Barre V/div 10…0.01 en ambos canales (`zero` + `cal`), y a 5, 0.5 y 0.05 V/div prueba
offsets 0.25/0.5/0.75 y acoplamiento AC. Preguntas a responder:

1. ¿El cero cambia solo entre los 3 grupos de relés o también entre V/div del mismo grupo?
2. ¿La corrección se mantiene al mover el offset? (si no, corregir en espacio DAC/pendiente)
3. ¿El cero es distinto en AC?
4. Ruido p-p, para fijar los umbrales de rechazo (6022: 10 cuentas p-p / 20 de offset a 8 bits).

## Resultados (24-nov-2026, `medicion_cero_2026-11-24.log`)

Puntas en corto, aparato recién encendido (sin calentamiento). `z` = corrección que mediría
`zero` = media cruda − cero predicho por el modelo, en cuentas del ADC (10 bits).

| V/div | z CH1 | z CH2 | ruido p-p CH1/CH2 |
|---|---|---|---|
| 10 / 5 / 2 (grupo 2) | 0.76 / 0.88 / 0.75 | 0.28 / 0.36 / 0.23 | 7–8 |
| 1 / 0.5 / 0.2 (grupo 1) | 0.88 / 0.80 / 0.72 | 0.37 / 0.30 / 0.25 | 7–8 |
| 0.1 / 0.05 / 0.02 (grupo 0) | 0.63 / 0.74 / 0.58 | 0.05 / 0.16 / 0.08 | 7–8 |
| offset 0.25 / 0.5 / 0.75 (5, 0.5, 0.05 V/div) | 0.04 … 0.84 | 0.04 … 0.85 | — |
| AC (5, 0.5, 0.05 V/div) | 0.72 / 0.83 / 0.81 | 0.49 / 0.55 / 0.58 | — |

(`0.01` V/div no existe: el mínimo es 20 mV y el log repite 0.02.)

Respuestas:

1. El cero **no cambia** ni entre grupos ni dentro de un grupo más allá de ~0.3 cuentas.
2. Mover el offset cambia la corrección ±0.4 cuentas, sin tendencia: la ley del DAC vale en todo
   el rango, no hace falta corregir pendiente.
3. AC da lo mismo que DC (+0.1 a +0.3 cuentas).
4. Ruido: 7–8 cuentas p-p por captura; la media de una captura varía ~0.1 cuenta entre capturas.

En la pantalla, el peor error (0.9 cuentas) es menos del **4 % de una división** en cualquier V/div:
más fino que el grosor de la traza con ese ruido. Queda un sesgo fijo de ~+0.7 cuentas en CH1 y
~+0.3 en CH2 que se podría meter en `kDacIntercept` (−119.3 / −156.2), si se confirma con el
aparato caliente. El ajuste por software (T12 / botón de la GUI) no aportaría nada visible; lo que
sí tiene margen es la ganancia (`kCountsPerVolt`, ±2–3 %), que necesita una referencia de tensión
conocida.

Plan original (descartado por ahora): API get/set de `zeroCorrection` en el núcleo, guardar/cargar en archivo desde la CLI, y en
la GUI reactivar `actionCalibrateOffset` con barrido automático y `.ini` por modelo.
