# Plan: escalas verticales (V/div) — síntesis y tareas

Fecha: 21-nov-2026. Base: commit `0944abb` + `docs/INFORME_ESTADO.md`.
Complementa al informe de estado; no lo reemplaza.

> **Actualización 21-nov-2026:** la fórmula de §3 ya está en el código
> (T5 en su parte de software: `kHwGroup`, `rawZero()`, nivel de disparo,
> comando `zero <ch>`, offset por omisión 0.5). **No está verificada en el
> aparato.** El cero usa el supuesto `rawZero = offsetReal × 1023` más la
> corrección que mide `zero`. Siguiente paso: T1, T2 y T6 con el aparato.
> Para comprobar la coherencia en T3: con el offset en 0.5, el ADC completo
> (±512 cuentas) equivale a ±512/(cpv·Vdiv) divisiones. En 1 V/div (cpv 112)
> da ±4.6 div, y eso cuadra con una pantalla de 8–10 div. En 10 V/div
> (cpv 22) da solo ±2.3 div, y en 100 mV/div (cpv 690) ±7.4 div. Si en el
> software oficial esos pasos llenan la pantalla, el valor de cpv de esos
> grupos es sospechoso.
>
> Hecho también sin hardware: T9 (recorte: protocolo `DSO6` con muestras
> recortadas y topes del ADC en volts; `status` las muestra), T10 (visor con
> cuadrícula 10 div, eje en divisiones, topes del ADC y aviso RECORTE) y T13
> (documentación). Guion de la sesión con el aparato: `docs/SESION_HW.md`.
> Cambio de comportamiento a tener en cuenta: el offset ya **no** desplaza la
> traza en pantalla (los volts son absolutos). Solo mueve en qué parte del ADC
> cae la señal, y eso se ve en los topes discontinuos. La posición en pantalla
> sería T8.

---

## 1. Qué hay hoy

**Funciona y está verificado contra el aparato** (no tocar, ver INFORME §2):
carga de firmware, detección 5200/5200A, máquina de estados de captura,
disparo en CH1, `triggerPoint` crudo, base de tiempo con tabla de 12 valores
(`fast=2`), pre-disparo como fracción, y el visor con TkAgg.

**Lo que se sabe del frente analógico** (medido con `rawgaintest`):

| Grupo físico (relés) | V/div que cubre | Cuentas ADC por voltio | Fiabilidad |
|---|---|---|---|
| ninguno | 2 / 5 / 10 V | ≈22 | medido directo |
| `below1V` | 200 / 500 mV / 1 V | ≈112 | medido directo |
| `below1V` + `below100mV` | (10) / 20 / 50 / 100 mV | ≈690 | derivado vía sonda, **débil** |

`gainBits` (`BulkSetGain`) no cambia la amplitud. Dentro de un grupo el ADC
entrega **exactamente los mismos datos**, así que la diferencia entre 2, 5 y
10 V/div tiene que hacerse por software (son escalas "matemáticas").

## 2. Por qué la señal no cambia o desaparece en pantalla (diagnóstico)

Esto es una hipótesis que sale del código. Falta confirmarla con `cal` (tarea T1).

La conversión (`dso5200.cpp:598` y `:626`) viene de OpenHantek:

```
volts = (raw / limit − offsetReal) × gainStep
```

En OpenHantek cada término tenía unidades de **pantalla completa**:
- `raw/limit` iba de 0 a 1 (`limit` era la cuenta a fondo de escala),
- `offsetReal` también iba de 0 a 1,
- `gainStep` era **V por pantalla** (8 div). Por eso la tabla original era
  0.16…80 V: es 8 × (20 mV…10 V/div). No era una tabla inventada, solo estaba
  en otra unidad.

En `0df2b29` y `c5d8ad9` se cambió `gainStep` a V/div y `limit` a
`cuentasPorVolt × V/div`, pero la fórmula no se tocó. Ahora las unidades no
encajan:

- El visor dibuja `muestra / Vdiv = raw/limit − offsetReal` (en divisiones).
- `raw` está centrado en algún valor del ADC de 10 bits (por ejemplo ~512).
  Ese **nivel de reposo no se resta nunca**. Lo que se resta es `offsetReal`
  (0..1), como si fueran divisiones.
- Con raw≈512, el centro de la traza cae en:

| V/div | limit | centro ≈ 512/limit − 0.5 | ¿Se ve en ±5 div? |
|---|---|---|---|
| 20 mV | 14 | 36 div | no |
| 50 mV | 35 | 14 div | no |
| 100 mV | 69 | 6.9 div | no |
| 200 mV | 22 | 23 div | no |
| 500 mV | 56 | 8.6 div | no |
| 1 V | 112 | 4.1 div | apenas, arriba |
| 2 V | 44 | 11 div | no |
| 5 V | 110 | 4.2 div | apenas |
| 10 V | 220 | 1.8 div | sí |

  En la mayoría de los pasos la traza se sale del `ylim(-5, 5)` fijo del
  visor. Por eso parece que "no cambia" o que queda plana o recortada.
- El mismo error de unidades está en el **nivel de disparo**
  (`doSetTriggerLevel`, `:407`): `level/gainStep` da divisiones, pero se usa
  como fracción de pantalla.
- La ganancia en sí (cuentas/V) probablemente está bien. El fallo está en el
  **cero**, es decir en qué cuenta corresponde a 0 V.

## 3. Modelo correcto propuesto

Hay que separar las dos capas de forma explícita:

```
Capa física (3 grupos)                         Capa matemática (10 V/div)
grupo = f(V/div) → relés                       V/div elegido por el usuario
cpv[grupo]   (cuentas/V, calibrado)
raw0[grupo](offset)  (cuenta que equivale a 0 V)

volts      = (raw − raw0[grupo](offset)) / cpv[grupo]
divisiones = volts / Vdiv            ← lo hace el visor (ya lo hace)
```

- `raw0` se mide con la entrada a GND (o con la punta en corto) para cada grupo
  y varios valores de offset. Lo más probable es que sea lineal en el valor del
  DAC: `raw0 = a + b·offsetDAC`.
- La posición vertical (offset) pasa a ser algo que el usuario controla en
  divisiones. Ya no es un término escondido dentro de la fórmula.
- Disparo: `nivelDAC` se calcula con la misma relación cuenta↔voltio. Hay que
  confirmar si el comparador trabaja en cuentas del ADC o del DAC de offset.
- Saturación: si `raw` vale 0 o 1023, hay que marcar la muestra como recortada
  y no interpretarla como voltaje.

## 4. Plan por fases

1. **Medir** (con hardware): el reposo `raw0` por grupo y offset, y las
   ganancias `cpv` de los 3 grupos con una señal que no sature.
2. **Refactorizar la conversión** (sin hardware): una tabla de 3 grupos más
   una tabla de V/div que apunte al grupo, y la fórmula nueva. Hay que quitar
   `kVoltageLimit` y la mezcla de unidades.
3. **Arreglar el disparo y el offset** usando el mismo modelo.
4. **Mejorar el visor**: nivel de 0 V, marcas de saturación, eje en
   divisiones, y opcionalmente un modo crudo.
5. **Documentar** y cerrar las secciones obsoletas de README y AGENTS.

## 5. Lista de tareas para delegar

Cada tarea dice si necesita el **aparato físico** (HW) o se puede hacer sin él
(SW). Reglas de siempre: todo en español, commits locales, no declarar nada
corregido sin una verificación HW descrita en el commit, y no revertir nada de
INFORME §2.

| ID | Tarea | Tipo | Depende de | Criterio de terminado |
|---|---|---|---|---|
| T1 | **Confirmar el diagnóstico §2.** Con la entrada en GND/corto, ejecutar `cal 1` en cada uno de los 9 V/div. Anotar el `mean` crudo y compararlo con la tabla de §2 | HW | — | Tabla V/div → raw mean registrada en `docs/`. Confirma o descarta la hipótesis |
| T2 | **Medir `raw0` contra el offset**: en cada uno de los 3 grupos, barrer `offsettest 1 {0,0.25,0.5,0.75,1}` con la entrada a GND y anotar el `mean` de `cal` | HW | T1 | Ajuste lineal `raw0 = a + b·offset` por grupo y canal (CH1 y CH2) |
| T3 | **Re-medir `cpv` de los grupos 2–10 V y 0.2–1 V** con el generador a una amplitud que dé pp entre 200 y 800 cuentas (sin saturar), medida con multímetro u otro osciloscopio | HW | — | 2 valores con su incertidumbre, CH1 y CH2 |
| T4 | **Medir `cpv` del grupo 10–100 mV directamente**: señal de 50–300 mVpp con un divisor resistivo medido. Sin sonda x10 | HW | — | Se reemplaza el valor ≈690 derivado |
| T5 | **Refactor de la conversión**: `struct HwGroup{below1V, below100mV, cpv, raw0A, raw0B}` ×3, tabla de V/div → grupo (añadir 10 mV, que el código ya admite), fórmula de §3. Eliminar `kVoltageLimit`. Dejar los valores medidos como constantes con un comentario que diga de dónde salieron | SW | (valores: T2–T4) | Compila. Con `--simulate` la traza escala al cambiar el V/div dentro de un grupo |
| T6 | **Validar T5 con hardware**: señal de 1 kHz a 2 Vpp en todos los V/div. En cada paso, el pp en pantalla debe ser 2/Vdiv divisiones (±5 %) y la traza debe quedar centrada | HW | T5 | Tabla de resultados en el mensaje del commit |
| T7 | **Arreglar el nivel de disparo**: recalcular `levelValue` con el modelo nuevo. Verificar que `trigger 0.5` engancha en el flanco a +0.5 V en 3 grupos distintos | SW+HW | T5 | El disparo es estable en el nivel pedido |
| T8 | **Offset vertical en divisiones**: un comando `offset <ch> <div>` que mueva la traza sin que cambie la amplitud medida | SW+HW | T5 | El pp no cambia al mover el offset |
| T9 | **Detectar saturación**: si raw es 0 o 1023, avisar en `status`/stderr y marcar la muestra en el frame. Puede ser un flag por canal en el protocolo TCP, lo que obliga a subir la versión en `PROTOCOL.md` | SW | T5 | El visor muestra el aviso de recorte |
| T10 | **Visor**: línea de 0 V por canal, cuadrícula de 10×8 divisiones, texto con el V/div de cada canal, ylim acorde con la cuadrícula | SW | — | Prueba visual con `--simulate` |
| T11 | **Comando `coupling <ch> gnd`** (la firma USB está en INFORME §4). Sirve también para automatizar T1/T2 | SW+HW | — | Traza plana en 0 V con GND |
| T12 | **Calibración automática al arrancar** (opcional): con GND, medir `raw0` por grupo y guardarlo en un archivo de config | SW+HW | T2, T11 | Arranque sin ajuste manual |
| T13 | **Limpieza**: quitar los comandos temporales o documentarlos en `help`. Actualizar AGENTS.md (viñetas obsoletas de V/div y CH2), README §"CH2 lee cero plano" (marcarla resuelta), el comentario `gain = 4 // 4.00 V/div` de `dso5200.h:66`, y añadir AGENTS.md y INFORME_ESTADO.md al repositorio (hoy están sin seguimiento) | SW | T6 | Docs coherentes con el código |
| T14 | **Pruebas sin hardware** (opcional): test unitario de la conversión raw→volts y del mapeo V/div→grupo→relés | SW | T5 | `ctest` pasa |

**Orden recomendado:** primero T1, porque es barata y confirma o descarta todo
el diagnóstico. Luego T2–T4 en una misma sesión de hardware, T5 sin hardware,
T6 para validar, y después T7–T13.

---

## 6. Notas para el agente que continúa

- **§2 es una hipótesis deducida del código, no medida.** Antes de reescribir
  nada, ejecuta T1. Si el `mean` crudo con GND **no** está cerca del centro del
  ADC (o no explica la posición de la traza que ve el usuario), el diagnóstico
  está mal: para y repórtalo en vez de forzar el refactor.
- Puntos que pueden invalidar el modelo y que hay que vigilar:
  - que `raw0` no sea lineal con el offset, o que dependa del canal (CH2 tiene
    otra posición de bits en `convertRawDataToSamples`);
  - que la tabla `offsetLimit` leída de la EEPROM (`retrieveChannelLevelData`)
    esté indexada con los 9 pasos del firmware original. Verifica que el
    índice `voltage[ch].gain` siga correspondiendo a esos 9 pasos con la tabla
    nueva, porque el offset DAC depende de eso;
  - que el comparador de disparo trabaje en el dominio del DAC de offset y no
    en cuentas del ADC (afecta a T7).
- El ADC es de 10 bits (0..1023). Si la señal CAL satura, no sirve para
  calibrar (lección de `d477bae`). Usa el generador con amplitud medida.
- Ejecuta `--debug` y `cal <ch>` en todas las pruebas, y pega los números
  crudos en los mensajes de commit. Sin números del aparato, la tarea no está
  terminada.
- Haz commits pequeños, uno por tarea, con el ID (T1, T2…) en el mensaje.
  Así quien revise después puede seguir el progreso con `git log`.
