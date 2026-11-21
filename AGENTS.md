# AGENTS.md

Qt-free CLI for the Hantek DSO-5200/5200A USB oscilloscope, extracted from
[OpenHantek](../). Controls the scope from a console and streams samples over
localhost TCP to a Python matplotlib viewer (`viewer/dso_viewer.py`).

## Language convention

The entire repo is in Spanish: README, docs, code comments, CLI output, and
commit messages. Keep all new prose, comments, and commit messages in Spanish.
Commit style is descriptive, past tense (see `git log`).

## Build & run (Arch Linux)

```sh
sudo pacman -S libusb cmake gcc python-matplotlib tk   # + graphviz for docs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)        # -> build/dso5200basic
```

- `build/` is gitignored; firmware `.hex` files are auto-copied next to the
  binary by a POST_BUILD step in `CMakeLists.txt`.
- Run: `./build/dso5200basic` (real hardware) or `./build/dso5200basic --simulate`
  (no hardware). Viewer in another terminal: `python3 viewer/dso_viewer.py`
  (connects to 127.0.0.1:5200).
- No `--simulate` requires the udev rule: `60-dso5200.rules` into
  `/etc/udev/rules.d/` (see MANUAL.md).

## No tests, no lint, no CI

There is no test suite, formatter, linter, typecheck step, or CI workflow.
The only automated verification is a successful `cmake --build`. Correctness of
the protocol/hardware tables is validated manually against real hardware or
`--simulate`; do not trust prose claims that lack a hardware verification note.

## Architecture

`main.cpp` wires a linear pipeline: find USB device -> upload Cypress FX2
firmware -> `Dso5200Control` -> `SampleServer` -> viewer.

- `src/device/dso5200.cpp/.h` — the core. A focused port of OpenHantek's
  `HantekDsoControl`, specialized for the single fixed DSO-5200 spec (no
  multi-model dispatch, no roll mode). Contains the capture state machine
  (`threadMain`) and all the hardware tables (gain, voltage limits, timebase).
- `src/protocol/*` — binary protocol definitions and command builders
  (`bulkcode.h`, `controlcode.h` document the wire commands).
- `src/usb/*` — libusb device handling, firmware upload (`ezusb.cpp`,
  `uploadfirmware.cpp`), device detection (`finddevice.cpp`).
- `src/streaming/sample_server.cpp` — TCP streaming; wire format in
  `docs/PROTOCOL.md` (little-endian, magic `"DSO5"`).
- `src/cli/command_parser.cpp` — interactive command dispatch.
- `viewer/dso_viewer.py` — matplotlib viewer (see backend gotcha below).

Includes use `src/` as the include root (`#include "device/dso5200.h"`), so
add new headers under `src/` and include them relative to it.

## Estado actual — leer primero

**Lee `docs/INFORME_ESTADO.md` y luego `docs/PLAN_ESCALAS.md` (diagnóstico de escalas V/div y lista de tareas T1–T14) antes de trabajar.** Es el informe de traspaso
(17-nov-2026) con todos los bugs encontrados, el método de validación y el
problema abierto real (calibración de amplitud por V/div, commit `c5d8ad9`,
no confirmado). Donde contradiga a este archivo, manda el informe: la viñeta
"Open bug (CH2 saturation)" y la descripción de la tabla V/div de abajo están
desactualizadas.

## Hardware/model gotchas (hard-earned, do not regress)

These are non-obvious and were fixed only after testing against real hardware.
Read the matching sections of README.md before touching the related code.

- **Timebase is a fixed 12-entry magic-value table, not a formula.**
  `kSlowTimebaseTable` in `dso5200.cpp`; `SamplerateFast` must be `2` (not `4`)
  for slow timebases — the hardware is not indifferent to the exact
  `slow`/`fast` pair even when the documented divider formula matches.
- **V/div steps are 10V..10mV (1-2-5 sequence), not 0.16V..80V.**
  `kGain[9]` labels follow `0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10`; the relay
  threshold is a contiguous range, not a single cutoff (10-bit model).
- **`triggerPoint` is used raw.** The bit-reversal decode that OpenHantek and
  sigrok apply to the value returned by `GETCAPTURESTATE` is wrong for the
  DSO-5200; `Dso5200Control` intentionally has no `calculateTriggerPoint`.
- **Pretrigger position is a fraction 0..1, not seconds.** `trigger.position`;
  don't reintroduce a `position * samplerate` conversion.
- **Trigger source is always CH1.** Hardcoded `triggerCommand.setTriggerSource(1)`
  in the constructor; the channel mapping is inverted (`1 - hardwareID`).
- **Open bug (CH2 saturation):** CH2 reads flat-zero at certain gain steps due
  to default offset mapping to the edge of the ADC range. Tracked in
  README.md ("CH2 lee cero plano…"); the real remaining issue is the default
  offset not centering the trace. Debug aids: `offsettest <ch> <frac>` and
  `rawgaintest <ch> <gainBits> <b1v> <b100>` CLI commands (temp, not in `help`).
- **Viewer must use TkAgg.** `dso_viewer.py` forces `matplotlib.use("TkAgg")`;
  the Qt/PyQt6 backend SIGABRTs in long sessions. Don't remove that.

## Docs map

- `README.md` — the authoritative history of every hardware bug found and fixed.
  Very long; read the relevant section before touching that subsystem.
- `docs/PROTOCOL.md` — TCP frame format (server <-> viewer).
- `MANUAL.md` — physical setup + usage.
- `docs/GUIA_ESTUDIO.md` — study guide, code reading order 1..7.
- `docs/*.pdf` — generated via pandoc; diagrams use Graphviz DOT + the
  `docs/pandoc-graphviz.lua` filter (`pandoc FILE.md --lua-filter=docs/pandoc-graphviz.lua -o out.pdf --pdf-engine=xelatex`).
