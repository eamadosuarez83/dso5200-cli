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
  disparo y nivel de disparo (`trigger <volts>`) — con disparo real
  (no sólo forzado) y la señal alineada de forma estable en pantalla.
- **No incluye** (fuera de alcance, no pedido): GUI, exportación a archivo,
  modo roll/scan (barrido continuo a timebases muy lentas), selección de
  fuente de disparo (siempre CH1) ni de modo de disparo (siempre "auto": el
  osciloscopio se auto-dispara periódicamente si no hay flanco, para que el
  visor siempre muestre algo).

## Build (Arch Linux)

```sh
sudo pacman -S libusb cmake gcc python-matplotlib tk
cd dso5200basic
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

El binario resultante es `build/dso5200basic`. El firmware (`firmware/*.hex`)
se copia automáticamente junto al binario tras el build.

Para que el dispositivo sea accesible sin root:

```sh
sudo cp 60-dso5200.rules /etc/udev/rules.d/
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
| `trigger <volts>` | Nivel de disparo en volts, sobre CH1 (ej. `trigger 1.0`) |
| `start` / `stop` | Inicia/detiene el muestreo |
| `status` | Estado actual |
| `help` | Ayuda |
| `quit` | Salir |

Flags de `dso5200basic`: `--simulate`, `--debug` (traza captureState/muestras
a stderr), `--port <n>` (default 5200), `--firmware-dir <dir>`. La variante
(DSO-5200 vs DSO-5200A) se detecta
automáticamente por el product ID (`5200` vs `520a`), que ya identifica el
modelo aunque el dispositivo todavía no tenga firmware cargado (solo el
vendor ID cambia de `04b4` a `04b5` al cargarlo) — confirmado contra
hardware real.

## Validado contra hardware real

Este port se escribió inicialmente sin el osciloscopio disponible, pero ya se
probó contra un DSO-5200A físico (detección USB, subida de firmware Cypress
FX2, conexión, captura continua y streaming al visor con valores de voltaje
estables y físicamente coherentes). Ver `docs/GUIA_ESTUDIO.md` para el detalle
de qué se verificó en cada bloque.

Varios problemas reales aparecieron sólo al probar contra hardware, y ya
están corregidos (ver "Desviaciones deliberadas respecto a OpenHantek"
abajo): detección de variante 5200 vs 5200A antes de tener firmware, un
bloqueo del osciloscopio en modo simulación de disparo automático sin señal
conectada, un crash del visor Python (backend Qt de matplotlib abortando
el proceso en sesiones largas, confirmado con `coredumpctl`), `timebase` sin
efecto real sobre la ventana de captura, y los escalones de ganancia 1.6V/div
y 16V/div dejando el canal en cero plano.

Para depurar a bajo nivel (ver transiciones de `captureState` y conteo de
muestras en tiempo real), usá `./build/dso5200basic --debug`.

## Desviaciones deliberadas respecto a OpenHantek

**`setRecordTime()`**: en OpenHantek, `HantekDsoControl::setRecordTime()`
nunca llega a usar la duración solicitada para calcular el downsampler
(siempre pasa `downsampler=0`, fijando el samplerate al máximo del
dispositivo) — parece un bug latente en el código original, enmascarado
porque la GUI usa mayormente `setSamplerate()` (que sí funciona) en su lugar.
Como el comando `timebase` de esta herramienta depende exactamente de esa
lógica, `doSetRecordTime()` en `src/device/dso5200.cpp` sigue el patrón
(correcto) de `setSamplerate()` en vez de reproducir el bug de
`setRecordTime()`.

**Detección de variante sin firmware**: se asumió inicialmente (siguiendo una
lectura apresurada del modelo original) que el DSO-5200 y el DSO-5200A
comparten el mismo product ID antes de cargar firmware. **Falso**: contra
hardware real, el product ID (`5200` vs `520a`) ya identifica la variante en
ambos estados, sólo el vendor ID cambia (`04b4`→`04b5`) al cargar firmware.
Corregido en `src/usb/finddevice.cpp` — ya no hace falta indicar la variante
a mano.

**Recuperación del disparo (trigger) sin señal conectada**: el bucle de
captura (`Dso5200Control::threadMain`, `src/device/dso5200.cpp`) porta la
máquina de estados de `HantekDsoControl::run()` de OpenHantek, que sólo
reintenta armar/forzar el disparo mientras `captureState == CAPTURE_WAITING`.
Contra hardware real, sin señal en el canal de disparo (CH1 por defecto), el
DSO-5200A se queda indefinidamente en `CAPTURE_SAMPLING` en vez de volver a
`CAPTURE_WAITING` — y como upstream nunca reintenta desde `CAPTURE_SAMPLING`,
la captura queda colgada para siempre. Se corrigió compartiendo la misma
lógica de armado/forzado/reinicio entre ambos estados.

**Backend Qt del visor abortaba el proceso**: en una sesión real con señal
conectada, `viewer/dso_viewer.py` (con el backend por defecto de matplotlib,
`qtagg`/PyQt6) crasheó con `SIGABRT` — confirmado con `coredumpctl info`, la
pila muestra `QMessageLogger::fatal` disparado desde el manejo interno de un
`QTimer` de Qt. No hay forma de recuperarse de eso desde Python (es un abort
a nivel de la librería Qt, no una excepción). Se corrigió forzando el backend
`TkAgg` al principio del script (evita PyQt6 por completo), verificado con
una sesión real de 95+ segundos sin caídas.

**Fuente del disparo apuntaba al canal equivocado**: `ETsrBits.triggerSource`
usa un mapeo de canal invertido (`1 - hardwareID`, tal como hace
`HantekDsoControl::setTriggerSource()` en OpenHantek). Al dejarlo en su valor
por defecto (0) nunca se llamaba a esa función, así que el comparador de
disparo quedaba mirando CH2 en vez de CH1 — con una señal real conectada
sólo a CH1, nunca había flanco que calificara y el osciloscopio caía siempre
en disparo forzado (pantalla inestable). Corregido fijando
`triggerCommand.setTriggerSource(1)` una vez en el constructor de
`Dso5200Control` (la fuente de disparo es siempre CH1 en esta herramienta).
Verificado: tras el fix, el disparo real engancha en el primer o segundo
ciclo de sondeo en el 100% de las capturas con una señal de prueba conectada,
en vez de caer siempre en el disparo forzado del ciclo 9.

## Función de disparo (nivel/pendiente) — resuelto, incluye alineación exacta

Se agregó el comando `trigger <volts>` (ver tabla de comandos abajo), que
llama a `Dso5200Control::setTriggerLevel()` → `doSetTriggerLevel()` (ya
portado de `HantekDsoControl::setTriggerLevel()`, pero hasta ahora sin nada
en la CLI que lo invocara). Junto con el fix de fuente de disparo de arriba,
esto permite que el disparo real (no forzado) enganche de forma consistente
sobre una señal periódica.

**La alineación horizontal del flanco también se resolvió.** Encontrado
comparando contra una implementación independiente (el driver
[`hantek-dso` de sigrok](https://github.com/sigrokproject/libsigrok/blob/master/src/hardware/hantek-dso/protocol.c),
que da soporte a la misma familia de osciloscopios Hantek): tanto sigrok
como OpenHantek decodifican el `triggerPoint` crudo que devuelve
`GETCAPTURESTATE` con el mismo truco de inversión de bits — pero el propio
comentario del código de sigrok admite *"No idea why the device reports the
trigger point this way"* (ese decode viene de ingeniería inversa contra el
DSO-2090/2150, sin una explicación teórica sólida ni siquiera para sus
autores). Probando empíricamente contra un DSO-5200A real (25+ capturas,
midiendo en qué muestra exacta aparecía el flanco de la señal de prueba en
cada una): con el decode aplicado, el flanco aparecía disperso en casi toda
la ventana de captura (desviación estándar ≈3100 muestras sobre una ventana
de 10240); **usando el valor crudo directamente, sin ese decode**, el flanco
quedó fijo en la muestra 14-15 en el 100% de las capturas (desviación
estándar = 0), tanto en modo normal (2 canales) como en modo fast-rate (1
canal). El decode de bits, sea lo que sea que compense en otros modelos
Hantek, **no aplica al DSO-5200(A)**. Corregido eliminando esa decodificación
(`Dso5200Control` ya no tiene una función `calculateTriggerPoint`; ver el
comentario en `dso5200.cpp` junto a `retrieveChannelLevelData()` para el
detalle completo, con referencia cruzada al código de sigrok).

## Timebase — resuelto, tabla de valores fijos en vez de fórmula continua

`timebase` no tenía ningún efecto real sobre la ventana de captura: pedir
100 us, 10 ms o 40 ms daba siempre la misma señal (confirmado byte a byte,
capturando datos crudos). La causa: `updateSamplerate()` calculaba los
registros `SamplerateFast`/`SamplerateSlow` del comando
`CSETTRIGGERORSAMPLERATE` (`0x0c`) con una fórmula *continua*
(`0xffff - valueSlow`) heredada de OpenHantek — pero el DSO-5200 sólo
entiende un conjunto **fijo** de 12 valores "mágicos" de 16 bits para
`SamplerateSlow`; cualquier otro valor no cambia la tasa real de muestreo.
Se cruzaron esos 12 valores contra dos implementaciones independientes con
soporte probado del DSO-5200A — el driver `hantek-dso` de sigrok y
[`Blokkendoos/HantekDSO`](https://github.com/Blokkendoos/HantekDSO) — y
ambas reportan **exactamente la misma tabla**, byte a byte. Corregido
reemplazando la fórmula por una búsqueda en esa tabla fija
(`kSlowTimebaseTable`, `doSetRecordTime()`/`applyTimebaseTableEntry()` en
`src/device/dso5200.cpp`), calculando la duración real de cada entrada con
la fórmula documentada del propio comando `0x0c` y eligiendo la más cercana
a lo pedido. Verificado: pedir `timebase 1ms`, `10ms` y `40ms` ahora produce
ventanas de captura genuinamente distintas (1.4746 ms, 8.192 ms y 40.796 ms
reales, medidas por el samplerate que reporta el propio dispositivo).

En el camino se probó — y se descartó — una hipótesis relacionada: que el
comando *combinado* `SETTRIGGERANDSAMPLERATE` (`0x01`, el que usa sigrok "de
forma uniforme" para toda la familia 2090/2150/5200, y el que usa
`HantekDSO` en su `dsoSetTriggerAndSampleRate()`) fuera el correcto acá
también. Pero la propia documentación de protocolo portada de OpenHantek
(`src/protocol/bulkcode.h`) aclara que ese comando combinado sólo es válido
para `MODEL_DSO2090`/`MODEL_DSO2150` — el DSO-5200/5200A usan el par de
comandos *separados* `CSETTRIGGERORSAMPLERATE` (`0x0c`, sólo samplerate) +
`ESETTRIGGERORSAMPLERATE` (`0x0e`, sólo bits de disparo), que es justamente
lo que este port ya enviaba. El problema nunca fue el comando en sí, sino
los valores que se le mandaban.

## Escalones de ganancia (V/div) — resuelto, umbral de relé equivocado para modelos de 10 bits

Pedir ciertos V/división concretos — 1.6 V o 16 V, los valores a los que
redondean pedidos "razonables" como "1 V" o "10 V" — dejaba el canal leyendo
**cero plano**, de forma permanente (no era un problema de asentamiento:
seguía en cero tras 6+ segundos de capturas continuas), en CH1 y CH2 por
igual, y se recuperaba en cuanto se pedía *cualquier otro* V/división.

La pista salió del historial de git de un proyecto hermano
([`OpenHantek6022`](https://github.com/OpenHantek/OpenHantek6022), que en
su día también dio soporte al DSO-5200): el commit `b935f4e` ("Mostly
DSO-5200 bugfixes, thanks to Ash", 2010) agrega la explicación que ya
estaba, sin contexto, en el propio `src/protocol/controlcode.h` de este
port: *"The limits are `<=` instead of `<` for the 10 bit models, since
those support voltages up to 10 V"*. El DSO-5200/5200A son justamente eso,
un modelo de 10 bits — y en algún punto entre ese fix de 2010 y la versión
de `modelDSO5200.cpp` de la que sale este port, esa rama específica del
modelo se perdió, dejando sólo la fórmula genérica (de 8 bits)
`gainID < 3` / `gainID < 6`, que conmuta el relé de atenuación un escalón
antes de lo debido — justo en los índices 3 y 6, justo donde se veía el
cero plano. El mismo commit reveló, además, que la tabla de índice de
ganancia (`kGain[].index`, el valor que se manda por `BulkSetGain`) y la
tabla de divisores de decodificación (`kVoltageLimit[]`) también habían
derivado hacia la fórmula genérica en vez de la específica del DSO-5200
(`gainId % 3`, con una excepción en `gainId == 0`).

Corregidas las tres tablas a la vez (`src/device/dso5200.cpp`, comentarios
junto a `kGain`/`kVoltageLimit` y dentro de `doSetGain()`), los 9 escalones
de ganancia dan lecturas reales — verificado capturando en los tres
escenarios que reprodujeron el síntoma original: cambio de ganancia en
pleno muestreo sobre CH1, sobre CH2, y ambos canales configurados a un
escalón antes problemático desde antes de arrancar el muestreo.

## Protocolo de streaming hacia el visor

Ver [`docs/PROTOCOL.md`](docs/PROTOCOL.md).
