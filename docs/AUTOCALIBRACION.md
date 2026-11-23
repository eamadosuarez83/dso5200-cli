# Autocalibración del software original — hipótesis y qué falta por capturar

Fecha: 23-nov-2026. Complementa a `docs/firmware/ANALISIS_FIRMWARE.md` y al
`docs/PLAN_ESCALAS.md` (tarea T12). Estado: análisis; no hay medición nueva.

## Observación de partida

El software nativo de Windows del DSO-5200/5200A tiene una función de
autocalibración que tarda **unos segundos**, quita algo de ruido y deja la señal
en cero. Hipótesis inicial: conmutar la entrada a GND y caracterizar el ruido
(toda variación, y la variación respecto de 0 V).

## Qué dice la ingeniería inversa ya hecha

1. **No hay relé de GND.** Ver README §"Acoplamiento": la "firma GND" que manda
   el software de Windows (`GainBits=0`, `below1V=below100mV=false`) no es una
   masa real, es el grupo menos sensible (2–10 V) con los bits de ganancia que no
   tienen efecto. El `gnd` de este port es por software (muestras a 0 V). Por
   tanto **la parte "colocar la entrada a GND" de la hipótesis no puede ser**: no
   existe ese relé en el protocolo conocido.

2. **El firmware FX2 es un puente pasivo USB→FPGA** (`ANALISIS_FIRMWARE.md`): no
   hay rutina de autocalibración; la adquisición y el disparo viven en el FPGA.
   Los comandos `B6`–`BB` son de relleno (resetean FIFOs y esperan). El único sin
   explicar es **`BC`**: escritura SPI bit-bang a un chip de la placa sin
   identificar (16 registros, `reg = 0x50 | (data[6] & 0x0F)`, `val = data[7]`;
   candidatos: el ADC o su generador de reloj). Es el único candidato a disparar
   una calibración interna de hardware.

## Cómo se explican los síntomas sin GND

La "calibración" que sí carga el software original viene de la **EEPROM de
fábrica** (`src/hantekprotocol/controlvalue.h`), y encaja con lo observado, pero
es instantánea, no "unos segundos":

| Valor | Qué corrige | Síntoma |
|---|---|---|
| `0x08` `VALUE_OFFSETLIMITS` | offset top/bottom por cada gain step (tabla `offsetLimit`) | "deja en cero" |
| `0x60` `VALUE_FASTRATECALIBRATION` | *"corregir las diferencias de nivel entre los dos canales fusionados para evitar ruido determinista"* (comentario literal) | "quita ruido" |
| `0x70` `VALUE_ETSCORRECTION` | corrección ETS | — |

Nótese que `0x60` habla de **ruido determinista** (desnivel entre canales en modo
fast-rate), no de ruido aleatorio de entrada.

## La incógnita de los "segundos"

La lectura de EEPROM es instantánea, así que el retardo de unos segundos **no
está explicado** por nada encontrado hasta ahora. Apunta a algo que hace el FPGA
o el chip del frente analógico (posiblemente disparado por `BC`), o a una
secuencia en la app de Windows que aún no se capturó.

## Cómo confirmarlo

Capturar el tráfico USB **durante esos segundos de autocalibración** (método en
`docs/CAPTURA_USB_WINDOWS.txt`: Wireshark + USBPcap) y vigilar:

- escrituras **`BC`** (SPI al chip), y
- cambios de relés/offset (`CONTROL_SETRELAYS`, `CONTROL_SETOFFSET`).

Si en la captura no aparece `BC` ni comandos raros y solo se ve un barrido de
relés/offset por grupos, entonces la autocalibración es en realidad **software**:
medir con la entrada en corto (punta sobre pinza, como ya hace `zero`) y guardar
correcciones — exactamente T12.

## Conclusión para T12

La intuición es correcta (cero = offset, ruido = desnivel de canales), pero el
mecanismo no es un relé de GND. No hace falta relé de GND para T12:
`zero`/`calibrateZero` ya mide el cero con la punta en corto, y es lo que habría
que persistir a un archivo de config (patrón de OpenHantek6022:
`getCalibrationFromIniFile` + `.ini` persistente).

## Firma

Registrado por opencode + DeepSeek el 23-nov-2026, a partir del análisis de
firmware (`10fbe01`), del código de `openhantek` y de `OpenHantek6022`, sin
medición de hardware nueva.
