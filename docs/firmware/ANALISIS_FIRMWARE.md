# Análisis del firmware Cypress FX2 del DSO-5200/5200A

Análisis estático (solo lectura) de `firmware/dso5200ax86-firmware.hex`,
`firmware/dso5200x86-firmware.hex` y del loader, hecho el 22-nov-2026 con un desensamblador
8051 propio y Ghidra 12 en modo headless. No se escribió nada en el aparato.

**Conclusión:** el firmware no esconde nada que mejore el equipo. Es un puente USB → FPGA; la
adquisición y el disparo viven en el FPGA. Los comandos que ningún programa usa (`B6`–`BC`) son
copias de relleno o una escritura de configuración a un chip sin identificar. Tema cerrado por
ahora.

## Archivos

| Archivo | Tamaño | Notas |
|---|---|---|
| loader (`*-loader.hex`) | 792 bytes | Idéntico en 5200 y 5200A |
| firmware 5200A | 4690 bytes | Código en `0080-12BF` + vectores de interrupción |
| firmware 5200 | 4690 bytes | Difiere del 5200A en **solo 2 bytes** |

Las dos diferencias 5200 ↔ 5200A:

| Dirección | 5200A | 5200 | Qué es |
|---|---|---|---|
| `009A` | `0A` | `00` | Byte bajo del PID USB (`520A` / `5200`) |
| `0110` | `41` ("A") | `20` (" ") | Último carácter del nombre "DSO-5200A" |

El código es el mismo: la diferencia entre los modelos está en el hardware (FPGA, etapa analógica).

## Identidad USB

- VID `04B5`, PID `520A` (5200A), USB 2.0, clase vendor (`FF`), 500 mA.
- Fabricante "ODM", producto "DSO-5200A", sin número de serie.
- Endpoints bulk: `EP2 OUT` (comandos) y `EP6 IN` (muestras), 512 bytes en high speed y 64 en
  full speed.

## Configuración del chip (rutina en `0C04`)

- `CPUCS = 0x0A`: CPU a 24 MHz, salida CLKOUT activa.
- `IFCONFIG = 0x43`: **slave FIFO** con reloj externo (IFCLK lo da la lógica de la placa).
- `EP2CFG = 0xA0` (OUT bulk 512, 4 buffers), `EP6CFG = 0xE0` (IN bulk 512, 4 buffers),
  EP4/EP8 apagados.

En slave FIFO el FPGA lee los comandos de EP2 y escribe las muestras en EP6 directamente; el 8051
**no toca nunca** los buffers de datos (cero accesos a `EP2FIFOBUF`/`EP6FIFOBUF`). Por eso los
comandos bulk (base de tiempo, disparo, fuente del disparo, etc.) los interpreta el FPGA, no este
firmware. Consecuencia práctica: el disparo por CH2 que no funciona en hardware (ver README,
"Fuente del disparo: solo CH1 por hardware") **no se puede arreglar cambiando el firmware**.

## Comandos de control (EP0, `bmRequestType` vendor)

Despacho en `0114`; tabla de saltos en `0149` para `B2`..`BC`.

| bRequest | Manejador | Qué hace | ¿Lo usamos? |
|---|---|---|---|
| `A2`/`A3` | `04A4` | Lectura/escritura de memoria del chip vía EP0 (estilo ejemplos Cypress) | No |
| `B2` | `016A` | Devuelve 1 byte: velocidad USB (`GETSPEED`) | Sí |
| `B3` | `0186` | Resetea las FIFO, pone IOA bits 0,1,3 y **pulsa IOA.0** (`BEGINCOMMAND`) | Sí |
| `B4` | `0203` | Offsets de canal y disparo (`SETOFFSET`) | Sí |
| `B5` | `025D` | Relés (`SETRELAYS`) | Sí |
| `B6` | `035D` | Resetea las FIFO, **baja IOA.0** y espera `data[0]` ms | No |
| `B7`–`BB` | `03A0`…`062C` | **Idénticos**: resetean las FIFO y esperan `data[0]` ms | No |
| `BC` | `01C4` | Espera 15 ms y escribe un registro por SPI bit-bang (ver abajo) | No |

`L0ECB` es la rutina de espera en milisegundos (la `EZUSB_Delay` de Cypress: mira la velocidad
del CPU en `CPUCS`).

### `BC`: escritura serie a un chip de la placa

Rutina `0DB8`, bus de 3 hilos manejado por software en el puerto C:

- `IOC.2`: pulso de inicio/selección.
- `IOC.1`: reloj.
- `IOC.0`: dato, MSB primero.

Envía 16 bits: el **registro** `0x50 | (data[6] & 0x0F)` (16 registros posibles) y el **valor**
`data[7]`. El chip destino no está identificado (candidatos: generador de reloj del ADC o el
propio ADC). Para saberlo hay que abrir el aparato y seguir las pistas desde PC0–PC2 del FX2.
**No probarlo a ciegas**: escribir registros desconocidos puede dejar el aparato en un estado raro
hasta desconectarlo.

### Otros detalles

- La interrupción del timer 2 (vector `002B` → `0F56`) conmuta `IOC.5`, `IOA.1`, `IOC.6` e `IOC.7`
  (¿LEDs o una línea de control? sin confirmar).
- El desensamblador alcanza 3037 de los 4690 bytes; el resto son tablas, descriptores y código
  alcanzado solo por la tabla de saltos.

## Cómo repetir el análisis

Herramientas en `docs/firmware/herramientas/`:

- `ihex.py`: lector de Intel HEX.
- `dis51.py`: desensamblador 8051 con nombres de registros del FX2.
  `python3 dis51.py firmware/dso5200ax86-firmware.hex > fw.asm`
- `DecompFx2.java` + `regs.txt`: script de Ghidra que nombra los registros del FX2, crea las
  funciones de los manejadores vendor y decompila todo a pseudo-C:

```sh
mkdir -p /tmp/gproj
/opt/ghidra/support/analyzeHeadless /tmp/gproj fx2 -overwrite \
    -import firmware/dso5200ax86-firmware.hex -processor 8051:BE:16:default \
    -scriptPath docs/firmware/herramientas -postScript DecompFx2.java \
    docs/firmware/herramientas/regs.txt /tmp/fw.c
```

Ojo: el decompilador de Ghidra para 8051 a veces simplifica mal las escrituras por `DPTR`
(p. ej. muestra `FIFORESET = 0` donde hay tres escrituras `0x80`, `0x06`, `0x00`); ante la duda,
mirar el ensamblador.
