# dso5200basic

Herramienta CLI standalone y sin Qt para el osciloscopio USB **Hantek
DSO-5200 / DSO-5200A**, extraída de [OpenHantek](../). Reutiliza el protocolo,
la comunicación USB y la carga de firmware ya implementados en OpenHantek,
pero sin la GUI de Qt: controla el osciloscopio por consola y transmite las
muestras en vivo a un visor en Python (matplotlib).

## Qué incluye y qué no

- **Incluye:** comunicación USB (libusb), carga de firmware Cypress FX2,
  protocolo binario del DSO-5200/5200A, y control de: barrido en tiempo
  (timebase), volts/división por canal, selector de canal, pendiente de
  disparo.
- **No incluye** (fuera de alcance, no pedido): GUI, exportación a archivo,
  modo roll/scan (barrido continuo a timebases muy lentas), selección de
  fuente de disparo (siempre CH1) ni de modo de disparo (siempre "auto": el
  osciloscopio se auto-dispara periódicamente si no hay flanco, para que el
  visor siempre muestre algo).

## Build (Arch Linux)

```sh
sudo pacman -S libusb cmake gcc python-matplotlib
cd dso5200basic
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

El binario resultante es `build/dso5200basic`. El firmware (`firmware/*.hex`)
se copia automáticamente junto al binario tras el build.

Para que el dispositivo sea accesible sin root:

```sh
sudo cp 99-dso5200.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

## Uso

```sh
./build/dso5200basic                 # hardware real
./build/dso5200basic --simulate      # sin hardware, señal sintética (ver más abajo)
```

En otra terminal, el visor:

```sh
python3 viewer/dso_viewer.py         # se conecta a 127.0.0.1:5200 por defecto
```

Comandos interactivos (se escriben en la terminal donde corre `dso5200basic`):

| Comando | Efecto |
|---|---|
| `timebase <valor><s\|ms\|us\|ns>` | Duración total de pantalla (ej. `timebase 1ms`) |
| `volts <1\|2> <V/div>` | Volts/división del canal (ej. `volts 1 0.5`) |
| `channel <1\|2\|both\|none>` | Selector de canal(es) activo(s) |
| `slope <rising\|falling>` | Pendiente de disparo |
| `start` / `stop` | Inicia/detiene el muestreo |
| `status` | Estado actual |
| `help` | Ayuda |
| `quit` | Salir |

Flags de `dso5200basic`: `--simulate`, `--port <n>` (default 5200),
`--firmware-dir <dir>`, `--model5200a` (al detectar un dispositivo sin
firmware, sube el firmware del 5200A en vez del 5200 plano — ambas variantes
comparten el mismo VID/PID antes de tener firmware y no se pueden distinguir
de antemano).

## Limitación conocida: sin hardware para probar

Este port se escribió **sin el osciloscopio físicamente disponible**. La capa
de protocolo/USB/firmware (`src/protocol/`, `src/usb/`, `src/device/dso5200.*`)
es un port fiel de la lógica ya probada de OpenHantek (mismas fórmulas de
samplerate, mismo desempaquetado de muestras de 10 bits, mismos comandos
bulk/control), no una reescritura desde cero — pero **no se pudo validar
contra el dispositivo real**. Lo que sí se verificó en esta máquina:

- Compila sin Qt (`ldd build/dso5200basic` no muestra ninguna librería Qt).
- El modo `--simulate` ejercita toda la tubería real (CLI, hilo de captura,
  servidor TCP, visor Python) con una señal sintética.

Cuando conectes el osciloscopio, corré `./build/dso5200basic` (sin
`--simulate`) y si algo falla, lo más probable es un detalle del protocolo
binario que solo se puede afinar contra hardware real.

## Desviación deliberada respecto a OpenHantek

`HantekDsoControl::setRecordTime()` en OpenHantek nunca llega a usar la
duración solicitada para calcular el downsampler (siempre pasa
`downsampler=0`, fijando el samplerate al máximo del dispositivo) — parece un
bug latente en el código original, enmascarado porque la GUI usa mayormente
`setSamplerate()` (que sí funciona) en su lugar. Como el comando `timebase`
de esta herramienta depende exactamente de esa lógica, `doSetRecordTime()` en
`src/device/dso5200.cpp` sigue el patrón (correcto) de `setSamplerate()` en
vez de reproducir el bug de `setRecordTime()`.

## Protocolo de streaming hacia el visor

Ver [`docs/PROTOCOL.md`](docs/PROTOCOL.md).
