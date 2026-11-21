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
efecto real sobre la ventana de captura, los escalones de ganancia 1.6V/div
y 16V/div dejando el canal en cero plano, dos bugs de unidades en la
posición de pre-disparo, y — el más grave — el registro `fast` de la tabla
de timebase estando mal para 11 de sus 12 entradas, lo que hacía que
cualquier captura, a cualquier velocidad, sólo mostrara un flanco en vez de
la señal periódica completa (ver "Timebase lento sólo capturaba un flanco"
abajo para el detalle completo, incluyendo cómo se encontró replicando a
mano la captura USB del software oficial de Windows).

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

## Timebase lento sólo capturaba un flanco — resuelto, el registro `fast` de la tabla estaba mal

Con `timebase` y los escalones de ganancia ya arreglados, seguía apareciendo
un síntoma grave: capturando una señal cuadrada conocida (generador externo
*y* la salida CAL interna del propio equipo, para descartar el generador
como variable) el buffer mostraba **un único flanco aislado, seguido de una
meseta plana** hasta el final de la ventana — nunca los varios ciclos que
debía haber. Confirmado idéntico en las **12 entradas** de
`kSlowTimebaseTable` (246 us a 1.64 s): la forma no cambiaba con la ventana
pedida, sólo la etiqueta de tiempo del eje X.

### Primer hallazgo (real, pero insuficiente): dos bugs de unidades en la posición de pre-disparo

`Dso5200Control::doSetPretriggerPosition()` calculaba `positionSamples =
position * samplerateCurrent`, tratando `position` como si ya fuera un
desplazamiento en *segundos*. Pero `trigger.position` en este port está
declarado y usado en todas partes como una fracción `0..1` de pantalla,
nunca convertida a segundos antes de llegar ahí. El mismo error exacto
estaba también en `threadMain()`, en el cálculo de `startCycle`
(`trigger.position * 1000.0 / cycleTimeMs`) — para timebases rápidos esto
llegaba a esperar 51 ciclos de sondeo (más de medio segundo real) antes de
siquiera armar el disparo, para una ventana reclamada de sólo 8 ms. El
propio OpenHantek hace la conversión fracción→segundos un nivel más arriba
— en la GUI (`main.cpp`: `position * horizontal.timebase * DIVS_TIME`) —
antes de llamar a las funciones equivalentes; esa conversión nunca se
portó, en ninguno de los dos sitios.

Con el valor por omisión `position = 0.0` el primer bug queda enmascarado
(`0.0 * cualquier_cosa = 0`), pero ya producía `TriggerPositionPre =
0xD7FF`, `TriggerPositionPost = 0xFFFF` — este último **fuera del rango
documentado** en `bulkcode.h` (`0xd7ff` a `0xfffe`, un desborde de 1) — en
vez del valor real de Windows, `0xEBFF`/`0xEBFF` (disparo centrado al 50%,
confirmado byte a byte con una captura USB del software oficial).
Arreglando ambos (multiplicar por `recordLength` o por el largo de ventana
en ms, según corresponda, en vez de tratar la fracción como segundos) la
captura mejoró de ~99.9%/0.1% a ~50/50 — real, pero **no resolvió el
síntoma completo**: seguía siendo un único flanco, no varios ciclos.

### El bug real: el registro `fast` de la tabla de timebase estaba mal para 11 de 12 entradas

La pista definitiva salió de medir **tiempo real de reloj**, no el que el
software reporta de sí mismo: pidiendo la ventana más lenta (1.638 s) y
midiendo desde `start` hasta el primer dato (con el timestamp que ya trae
cada frame), el dato llegó en **0.435 s** — menos de un tercio de lo
físicamente posible. Repitiendo la misma medición sobre la captura USB real
del software de Windows, en un ajuste equivalente (~0.33 s reclamados): su
tiempo real promedio fue **0.309 s**, casi idéntico. El software real sí
respeta el tiempo físico; el nuestro no.

Replicando a mano, byte a byte y con los mismos tiempos de espera, la
secuencia USB completa de una captura lenta de Windows (con `pyusb`
directo, sin pasar por este proyecto en absoluto) **sí produjo una onda
periódica real** — 412 ciclos limpios donde nuestro software daba uno solo.
La diferencia encontrada: Windows usaba `SamplerateSlow=0xF825,
SamplerateFast=0x02` para ese ajuste; la entrada equivalente de nuestra
tabla era `SamplerateSlow=0xFFCE, SamplerateFast=0x04`. **Ambos pares dan
exactamente el mismo divisor** por la fórmula documentada (`divider =
2·(0x10000−slow) + 4−fast` → 100 en los dos casos), por eso se habían dado
por intercambiables (y por eso coincidían con sigrok y HantekDSO, que
también usan `fast=4` — el bug no es de esos proyectos, es que ninguno
parece haber probado a fondo el DSO-5200 real en timebases lentos).
**El hardware real no es indiferente a qué par le mandás aunque el divisor
matemático sea idéntico.**

Sustituyendo la entrada de la tabla por el par exacto de Windows —a través
de todo el pipeline C++ normal, sin tocar nada más— la captura pasó a
mostrar 413 ciclos reales. La regla general: `fast=2` en vez de `fast=4`,
con `slow` un valor más alto (`slow_correcto = slow_viejo + 1`, consecuencia
directa de la fórmula del divisor). Se corrigieron las 12 entradas de la
tabla con esa regla y se verificaron las 12 contra el generador real: el
número de ciclos capturados ahora escala proporcionalmente con la ventana
pedida (1, 2, 2, 2, 6, 12, 35, 74, 106, 304, 1002+ ciclos de 246 us a
819 ms) — exactamente lo esperado para una señal de 1 kHz.

## CH2 lee cero plano en 4V/div y 40V/div — resuelto (esos pasos no existían)

> **Cerrado:** "4V" y "40V" no eran pasos reales del aparato; se corrigió en
> `0df2b29` (ver la sección siguiente). El problema de fondo que quedaba, que
> la traza no escalaba ni quedaba centrada, era un error de unidades en la
> fórmula de conversión. Ver "Conversión a voltios" más abajo.


Con el bug de timebase ya resuelto, probando con el generador conectado a
ambos canales apareció un síntoma nuevo: CH1 funciona bien en las 12 bases
de tiempo, pero **CH2 lee cero plano exactamente en `volts 2 4.0` y
`volts 2 40.0`** — confirmado que no es un problema de "canal sin
inicializar" (pedir `volts 2 4.0` con el canal ya activo y muestreando
reproduce el cero plano igual).

Los datos de calibración que el dispositivo reporta para CH2 en esos dos
escalones (`gain=4` y `gain=7`) son **idénticos byte a byte**
(`start=510, end=3440`, un rango mucho más ancho que cualquier otro
escalón de CH2 o que el equivalente en CH1). Con el offset por omisión
(`0.0`, que mapea al extremo mínimo del rango — mismo patrón que el bug de
posición de disparo ya corregido) ese extremo cae fuera de la zona útil
del ADC para esos dos escalones específicos, causando saturación real
(confirmado con los bytes crudos: valor constante, no ruido).

Se agregó un comando de CLI temporal, sólo para depurar
(`offsettest <canal> <fracción>`), para barrer valores de offset sin
recompilar. Con offset 0.0-0.15 la lectura sigue saturada; desde ~0.2
aparece la periodicidad correcta (51 ciclos, coincidiendo con CH1), pero
**la escala de voltaje nunca se estabiliza** en un resultado limpio en
todo el rango 0.2-0.5 probado — descarta que el fix sea tan simple como
cambiar el offset por omisión a 0.5 (ese cambio genérico ya se probó y
rompió otros escalones, ver el bug de posición de disparo).

**Actualización**: "4V" y "40V" nunca fueron escalones reales de este
hardware — ver "Tabla de V/división corregida" abajo. El síntoma de
saturación en sí resultó ser más general (depende de `gainId`, no
exclusivo de esos dos valores viejos); sigue siendo el próximo bug a
resolver, ahora sobre la tabla ya corregida.

## Tabla de V/división corregida — 10V a 10mV reales, no 0.16V a 80V

Investigando el bug de arriba surgió una pregunta más básica: ¿de dónde
salió la tabla `kGain[]` (0.16V..80V) que este port traía desde el
principio? Pidiéndole al usuario que capturara el tráfico USB del software
oficial cambiando de V/división en Windows, y que anotara en qué valores
el relé de atenuación hace clic audible en el osciloscopio real, apareció
la respuesta: los V/división reales de este equipo son **10V, 5V, 2V, 1V,
500mV, 200mV, 100mV, 50mV, 20mV y 10mV** (secuencia 1-2-5 estándar), y **el
relé sólo hace clic al entrar a 10V, 1V y 100mV** — los demás pasos no
generan ningún tráfico USB adicional. Como bien intuyó el usuario:
*"supongo que hace un escalado matemático... es interno por software"* —
dentro de cada década, el software oficial sólo reinterpreta la misma
lectura de hardware, sin tocar relés ni registros de ganancia.

Esto encaja con algo que `kGain[]` ya tenía sin aprovechar del todo: separa
un campo `index` (lo que se manda al hardware) de un campo `voltsPerDiv`
(usado sólo en la fórmula de conversión de software) — exactamente el
mecanismo que hacía falta. Y la tabla `kVoltageLimit[]`/`kGain[].index` ya
corregida en un bug anterior seguía un patrón `gainId % 3` (con una
excepción en `gainId == 0`) que sólo tiene sentido si el hardware agrupa el
multiplicador fino (1×/2×/5×) en 3 décadas contiguas de 3 pasos — es decir,
esas dos tablas **ya estaban bien**; sólo las *etiquetas* `voltsPerDiv`
estaban mal.

Se reescribió `kGain[9]` con las etiquetas correctas en el mismo orden de
`gainId` (`0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0` — el décimo paso,
10mV, no tiene calibración propia y reutiliza la de 20mV, consistente con
que tampoco hace clic de relé) y se corrigió el umbral de relé de un único
punto de corte a un rango contiguo (`below1V = gainID >= 3`,
`below100mV = gainID >= 3 && gainID <= 5`). Probado en vivo contra el
osciloscopio real con la señal CAL: los 5 pasos donde CAL cabe en rango
(0.5V a 10V) muestran periodicidad correcta y consistente (51-52 ciclos,
igual que la referencia ya validada). Los pasos más sensibles (0.02V-0.2V)
muestran forma errática, pero es esperable — CAL es demasiado grande para
esos rangos y satura el ADC, no es un síntoma de bug nuevo.

Se agregó otro comando de depuración temporal
(`rawgaintest <canal> <gainBits> <b1v> <b100>`) para seguir probando esto
sin recompilar.

**Seguimiento (un día después)**: la aparente "escala de amplitud
inconsistente entre pasos" resultó **no ser un bug nuevo**. Barriendo el
offset en los 9 pasos reales se confirmó que el offset en sí funciona
perfectamente (el delta medido se mantiene exactamente constante al
variarlo, sólo cambia dónde se centra la traza). Lo que parecía
sospechoso — 0.2V y 2V/división dando un delta mucho más chico que sus
vecinos con el offset de fábrica — se explicó deshaciendo la conversión de
voltios a código crudo del ADC (`raw = volts_delta * limit / gainStep`):
en 0.2V, 0.5V y 1V/división el resultado da exactamente **1023** (el ADC
de 10 bits saturado por completo en ambas puntas) para los tres — la señal
CAL es simplemente demasiado grande para esa década entera, igual que ya
sabíamos que pasaba en 0.02V-0.1V. En 2V, 5V y 10V/división, en cambio, los
tres dan el mismo código crudo entre sí (~237-238) una vez que el offset
está razonablemente puesto — prueba de que esa década sí está bien
calibrada de punta a punta. El único bug real que sigue pendiente es el de
offset por omisión sin centrar (arriba); para probarlo bien hace falta una
señal cuya amplitud quepa dentro del rango a probar, no CAL saturando por
los dos extremos de la tabla. Ver `docs/GUIA_ESTUDIO.md` (Bloque 6, octavo
caso real) para el detalle completo.

## Conversión a voltios — corregida en software, pendiente de verificar en hardware

La fórmula heredada de OpenHantek, `(raw/limit − offsetReal) × gainStep`,
trabaja en unidades de pantalla completa: `gainStep` son voltios por pantalla
de 8 divisiones. Por eso la tabla original era 0.16…80 V, es decir
8 × 20 mV…10 V/div. Al cambiar `gainStep` a V/div sin tocar la fórmula, la
cuenta de reposo del ADC dejó de restarse y la traza quedaba fuera de pantalla
en casi todos los V/div.

Desde `af64c0a`: `volts = (raw − rawZero) / countsPerVolt[grupo]`. Hay 3
grupos de relés (`kHwGroup`) y el V/div solo lo usa el visor para dividir.
`rawZero` se toma como `offset × 1023` más una corrección que se mide con
`zero <canal>` con la entrada a GND. El frame incluye además las muestras
recortadas y los voltios de los topes del ADC (protocolo `DSO6`), que el visor
dibuja en línea discontinua. Diagnóstico, supuestos y tareas pendientes:
[`docs/PLAN_ESCALAS.md`](docs/PLAN_ESCALAS.md).

## Protocolo de streaming hacia el visor

Ver [`docs/PROTOCOL.md`](docs/PROTOCOL.md).
