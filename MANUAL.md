# Manual de uso — dso5200basic

Guía práctica para verificar la conexión física del DSO-5200/5200A y usar la
herramienta. Para detalles de arquitectura/protocolo ver [README.md](README.md)
y [docs/PROTOCOL.md](docs/PROTOCOL.md).

## 1. Antes de conectar el osciloscopio

```sh
sudo pacman -S libusb cmake gcc python-matplotlib tk
cd dso5200basic
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

sudo cp 60-dso5200.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Las reglas udev son necesarias para poder usar el dispositivo sin `sudo`. Si
las instalás con el osciloscopio ya conectado, desconectalo y volvelo a
conectar (o corré `udevadm trigger`) para que se apliquen.

## 2. Conectar el osciloscopio y verificar que Linux lo detecta

Conectá el DSO-5200/5200A por USB y, en una terminal, revisá el log del
kernel en vivo:

```sh
sudo dmesg -w
```

Deberías ver un bloque como:

```
usb 1-2: new high-speed USB device number N using xhci_hcd
usb 1-2: New USB device found, idVendor=04b4, idProduct=5200
```

`Ctrl+C` para salir de `dmesg -w`. Si no aparece nada al conectar el cable,
es un problema de cable/puerto/hub antes que de software — probá otro puerto
USB (preferentemente directo a la placa, no por un hub) y otro cable.

En paralelo, listá los dispositivos USB:

```sh
lsusb | grep -i 04b[45]
```

### Qué VID:PID esperar

El DSO-5200/5200A usa dos identidades distintas según si ya tiene firmware
cargado (el firmware se pierde al desenchufar, hay que subirlo cada vez que
se conecta):

| Estado | Vendor:Product | Notas |
|---|---|---|
| Sin firmware (DSO-5200) | `04b4:5200` | |
| Sin firmware (DSO-5200A) | `04b4:520a` | |
| Con firmware cargado (DSO-5200) | `04b5:5200` | |
| Con firmware cargado (DSO-5200A) | `04b5:520a` | |

El product ID (`5200` vs `520a`) ya identifica la variante en ambos estados —
solo cambia el vendor ID al cargar firmware. `dso5200basic` detecta esto
automáticamente, no hace falta indicar la variante a mano.

Si `lsusb` no muestra ninguno de estos IDs, el sistema operativo no está
viendo el dispositivo — revisar cable/puerto, o probar en otra máquina/con
otro cable para descartar hardware.

## 3. Primera ejecución

```sh
cd dso5200basic
./build/dso5200basic
```

Flujo esperado:

1. Si el dispositivo aparece como `04b4:5200` (sin firmware), la herramienta
   sube el firmware Cypress FX2 automáticamente:
   ```
   Dispositivo sin firmware detectado, subiendo firmware DSO-5200...
   Firmware cargado, esperando a que el dispositivo se reconecte...
   ```
   Durante este paso el dispositivo desaparece y reaparece de `lsusb`/`dmesg`
   con el nuevo VID:PID (`04b5:5200` o `04b5:520a`). Es normal que tarde 1-2
   segundos.
2. Una vez con firmware:
   ```
   Conectado a DSO-5200.
   Streaming de muestras en 127.0.0.1:5200 (viewer/dso_viewer.py)
   ```
3. Quedás en el prompt interactivo (`>`) listo para recibir comandos.

## 4. Problemas comunes

**`No se encontro ningun DSO-5200/5200A conectado.`**
`lsusb` tampoco lo muestra → problema de cable/puerto (ver paso 2). Si
`lsusb` sí lo muestra pero la herramienta no lo encuentra, revisá que el
VID:PID coincida exactamente con la tabla de arriba (podría ser un clon con
otro ID).

**`No se pudo conectar al dispositivo: Access denied ...` (`LIBUSB_ERROR_ACCESS`)**
Las reglas udev no están instaladas, no se recargaron, o el dispositivo no
recibió el evento `add` después de instalarlas (reconectá el cable). Para
confirmar si el permiso está realmente aplicado (no alcanza con que la regla
"exista", tiene que haber otorgado el ACL):
```sh
# reemplazá bus/dispositivo por los que muestre lsusb
getfacl /dev/bus/usb/<bus>/<dispositivo>
```
Si no aparece una línea `user:tu_usuario:rw-`, el ACL no se aplicó. Una causa
frecuente en sistemas systemd/Manjaro: el archivo de reglas debe tener un
número de archivo **menor a 73** (por ejemplo `60-dso5200.rules`, no
`99-dso5200.rules`) — `/usr/lib/udev/rules.d/73-seat-late.rules` es el que
convierte la etiqueta `uaccess` en un ACL real, y sólo ve la etiqueta si tu
regla ya corrió antes que él (las reglas se procesan en orden numérico de
archivo). Confirmá el número con `ls /etc/udev/rules.d/`, corregilo si hace
falta, y reconectá el cable.

**El firmware se sube pero el dispositivo no reaparece**
Puede ser un problema de alimentación USB (algunos hubs no dan suficiente
corriente) o de cable de mala calidad para transferencias de alta velocidad.
Probá conectando directo a un puerto USB del equipo, sin hub.

**Quiero ver qué está pasando a nivel de protocolo (captureState, muestras)**
Corré con `./build/dso5200basic --debug`: traza a stderr cada transición de
`captureState` y cuántos bytes/muestras trae cada captura. Útil para
diagnosticar un osciloscopio que parece "colgado" sin capturar nada.

**El visor (`dso_viewer.py`) se cierra solo / "se cae" la conexión y no vuelve a conectar**
Si ves varios mensajes seguidos de `Conexión perdida (...), reintentando en
2s...` y el proceso de `dso5200basic` ya no está corriendo, lo que pasó es
que el visor perdió la conexión porque `dso5200basic` se cerró o se
reinició — el visor por sí solo reintenta indefinidamente y no es un bug (es
el comportamiento esperado, solo hace falta volver a arrancar
`dso5200basic`). Si en cambio la **ventana del visor** desaparece sola con
`dso5200basic` todavía corriendo, era un crash real: se detectó (con
`coredumpctl list`) que el backend Qt/PyQt6 de matplotlib podía abortar el
proceso completo (`QMessageLogger::fatal` dentro del manejo de un `QTimer`)
en sesiones largas. Ya está corregido forzando el backend `TkAgg` al
principio de `viewer/dso_viewer.py`, verificado con una sesión real de 95+
segundos sin caídas. Si ya tenías el repo desde antes, actualizá
`dso_viewer.py`.

**El osciloscopio conecta pero `start` nunca produce datos / se queda "pegado"**
Sin nada conectado a la punta del canal de disparo (CH1 por defecto), puede
tardar en disparar por primera vez, pero no debería quedarse colgado
indefinidamente — `dso5200basic` fuerza el disparo automáticamente al cabo de
unos ciclos. Si aun así no ves frames, corré con `--debug` y fijate si
`captureState` queda fijo en `1` (CAPTURE_SAMPLING) sin volver a cambiar.

**Cambio `volts 1|2 1.6` (o `16`) y el canal queda en cero plano**
Este era un bug real (relé de atenuación con el umbral equivocado para el
DSO-5200, un modelo de 10 bits) — ya está corregido. Si lo ves en una copia
vieja del repo, actualizá `src/device/dso5200.cpp` (ver "Escalones de
ganancia (V/div) — resuelto" en el README para el detalle técnico).

**`timebase` no cambia nada, siempre se ve la misma ventana de señal**
También era un bug real (la fórmula de samplerate no aplicaba a este modelo)
— ya está corregido; `timebase 100us`, `10ms` y `40ms` ahora producen
ventanas de captura genuinamente distintas. Ver "Timebase — resuelto" en el
README para el detalle técnico.

**Cada captura muestra un solo flanco y después queda plana el resto de la ventana**
Bug real, parcialmente corregido: la posición de pre-disparo se mandaba en
unidades equivocadas (ver "Posición de pre-disparo" en el README). El fix
mejoró la proporción de la captura que muestra el nivel bajo de la señal
(de ~0.1 % a ~50 %), pero el síntoma completo — un solo flanco en vez de
varios ciclos periódicos — sigue sin resolverse. Si lo estás viendo, no es
tu configuración; es una investigación en curso.

## 5. Uso normal

En una terminal:

```sh
./build/dso5200basic
```

En otra terminal, el visor en vivo:

```sh
python3 viewer/dso_viewer.py
```

Comandos en la terminal de `dso5200basic`:

| Comando | Efecto |
|---|---|
| `start` | Inicia el muestreo (necesario para ver algo en el visor) |
| `stop` | Detiene el muestreo |
| `timebase <valor><s\|ms\|us\|ns>` | Duración total de pantalla, ej. `timebase 1ms` |
| `volts <1\|2> <V/div>` | Volts/división del canal, ej. `volts 1 0.5` |
| `channel <1\|2\|both\|none>` | Selector de canal(es) activo(s) |
| `slope <rising\|falling>` | Pendiente de disparo |
| `trigger <volts>` | Nivel de disparo en volts, sobre CH1 (ej. `trigger 1.0`) |
| `status` | Estado actual (samplerate, canales, ganancia, pendiente) |
| `help` | Ayuda |
| `quit` | Salir (cierra la conexión USB y el servidor) |

Ejemplo de sesión:

```
> start
Muestreo iniciado.
> volts 1 0.2
Canal 1 ajustado a 0.2 V/div.
> timebase 500us
Timebase ajustado a 500us de pantalla.
> status
sampling=on samplerate=20.480MS/s fastRate=no ch1=on@0.20V/div ch2=on@4.00V/div slope=rising triggerLevel=0.000V(ch1)
```

### Ajustar el nivel de disparo para estabilizar la señal

La fuente de disparo es siempre CH1. Para que el disparo enganche sobre un
flanco real (en vez de caer en disparo forzado, que produce una imagen
inestable):

1. Conectá la señal a CH1 y arrancá el muestreo (`start`).
2. Corré con `--debug` y mirá unos frames — o simplemente observá el visor —
   para estimar entre qué valores oscila la señal (el calibrado de voltaje
   de este puerto es aproximado, heredado de OpenHantek; no asumas que el
   valor nominal de tu generador coincide exactamente con lo que se lee acá).
3. Ajustá `trigger <volts>` a un valor **entre el mínimo y el máximo reales**
   de la señal (idealmente cerca del punto medio). Un nivel fuera de ese
   rango nunca va a calificar un flanco real.
4. Usá `slope rising` o `slope falling` según qué flanco quieras que dispare.

Ejemplo con una señal cuadrada que en la práctica osciló entre 0 V y 0.9 V:

```
> trigger 0.45
Nivel de disparo ajustado a 0.45 V.
```

Con el disparo bien configurado, el osciloscopio debería enganchar un flanco
real casi de inmediato (visible con `--debug`: la transición a
`CAPTURE_READY5200` ocurre en el ciclo 1-2 de sondeo, no en el ciclo 9 del
disparo forzado), **y la posición horizontal del flanco queda fija en
pantalla** (verificado: la misma muestra ±1 en decenas de capturas
consecutivas, tanto con uno como con dos canales activos) — ver "Función de
disparo... — resuelto" en el README para el detalle técnico de cómo se
encontró y corrigió.

## 6. Probar sin hardware (modo simulado)

Útil para verificar que la instalación de Python/matplotlib y la conexión
CLI-visor funcionan, sin depender del osciloscopio:

```sh
./build/dso5200basic --simulate
python3 viewer/dso_viewer.py   # en otra terminal
```

Los mismos comandos de la sección 5 aplican; la señal es sintética (senoidal
con ruido) pero reacciona a `timebase`/`volts`/`channel`/`slope` igual que el
hardware real.
