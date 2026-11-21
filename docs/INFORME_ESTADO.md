# Informe de estado — dso5200basic (17-nov-2026)

Informe de traspaso preparado por Claude (Claude Code) para el agente que
continúa este proyecto (opencode + DeepSeek). Resume qué se encontró, qué se
corrigió, qué quedó abierto y cómo se trabajó. Fuente: los 17 commits del
repositorio (`git log`), `README.md` y el código actual. La conversación
original de septiembre ya no existe; este documento y el historial de git son
lo que queda.

**Léelo completo antes de tocar `src/device/dso5200.cpp`.**

---

## 1. Método de trabajo (cómo se validó todo)

Ninguna corrección se dio por buena sin probarla contra el **DSO-5200A
físico**. Las herramientas usadas, en orden de confiabilidad:

1. **Captura USB del software oficial de Windows** (`docs/CAPTURA_USB_WINDOWS.txt`)
   — es la referencia de verdad para valores de registros.
2. **Reproducir con pyusb los bytes exactos de Windows**, sin pasar por este
   programa, para aislar si el fallo está en el protocolo o en el código.
3. **Medición empírica**: generador externo (1 kHz, 2 Vpp) y la señal CAL del
   aparato; contar ciclos por captura; medir tiempo real de captura con los
   timestamps de cada frame; medir la muestra exacta de un flanco conocido en
   25+ capturas.
4. **Clics audibles de los relés** del aparato (solo suenan al entrar en
   10 V, 1 V y 100 mV/div).
5. **Drivers independientes**: sigrok `hantek-dso` y
   github.com/Blokkendoos/HantekDSO; historial git de OpenHantek6022
   (commit `b935f4e`, 2010).

> Ojo: sigrok, HantekDSO y OpenHantek **comparten varios de los errores** que
> se encontraron aquí (ver bugs 6 y 10). Que dos drivers coincidan no prueba
> que un valor sea correcto para este hardware.

Comandos de depuración ocultos (no salen en `help`), en
`src/cli/command_parser.cpp`:

| Comando | Para qué |
|---|---|
| `--debug` (flag) | Traza estados de captura, triggerPoint, histogramas crudos a stderr |
| `offsettest <1\|2> <fracción 0..1>` | Barrer el offset vertical sin recompilar |
| `rawgaintest <1\|2> <gainBits 0-3> <b1v 0\|1> <b100 0\|1>` | Escribir registros de ganancia/relés directamente, saltándose la tabla V/div |
| `cal <1\|2\|off>` | (nuevo, commit `0944abb`) estadísticas del ADC crudo por frame: min/max/promedio/pp, sin convertir a volts |

---

## 2. Errores encontrados y corregidos ✅

| # | Error | Cómo se encontró | Corrección | Commit |
|---|---|---|---|---|
| 1 | Regla udev nunca aplicaba el ACL (hacía falta sudo) | Orden de reglas: `99-` corría después de `73-seat-late.rules` | Renombrada a `60-dso5200.rules` | `0613663` |
| 2 | Había que pasar `--model5200a` a mano | El product ID USB (`5200`/`520a`) ya identifica la variante antes del firmware | Detección automática, flag eliminado | `0613663` |
| 3 | Máquina de estados colgada en `CAPTURE_SAMPLING` sin señal en el canal de disparo | Trazas `--debug` | Reintentar armado/forzado también desde ese estado | `0613663`, `01b0e45` |
| 4 | Visor abortaba (SIGABRT) tras un rato con señal real | `coredumpctl`: abort dentro de Qt6 (backend qtagg de matplotlib) | Forzar `TkAgg` en `dso_viewer.py` | `3fea504` |
| 5 | Disparo nunca enganchaba (siempre disparo forzado) | `ETsrBits.triggerSource` quedaba en 0 = CH2 por el mapeo invertido `1 - hardwareID` | `setTriggerSource(1)` (CH1) en el constructor | `ae450c2` |
| 6 | Flanco horizontal "bailaba" entre capturas | 25+ capturas: con la decodificación por inversión de bits (OpenHantek/sigrok) σ≈3100 muestras; con el valor crudo σ=0 | Eliminado `calculateTriggerPoint()`; se usa `triggerPoint` crudo | `46feae8` |
| 7 | Base de tiempo calculada con fórmula continua | Historial OpenHantek + sigrok + HantekDSO: el registro solo acepta 12 valores "mágicos" | `kSlowTimebaseTable` de 12 entradas | `aba04da` |
| 8 | 1.6 V y 16 V/div leían 0 V plano (umbral de relés `<` en vez de `<=`) | Commit `b935f4e` de OpenHantek6022 (modelos de 10 bits) | Umbrales y tablas corregidos (luego reemplazados por #11 y §3) | `aba04da` |
| 9 | Posición de pre-disparo: fracción 0..1 multiplicada como si fueran segundos | Captura Windows: `0xEBFF/0xEBFF`; el código enviaba `0xD7FF/0xFFFF` (fuera de rango) | `position * recordLength`, default 0.5. Mismo error en `startCycle` de `threadMain()` | `1b405c9`, `1fb234f` |
| 10 | **Cada captura mostraba un solo flanco** en vez de ciclos periódicos | Tiempo real: ventana de 1.6 s lista en 0.43 s (imposible). Replay pyusb de los bytes de Windows: usaba `fast=2` | `SamplerateFast = 2` (no 4) con `slow+1` en 11/12 entradas. Matemáticamente equivalente, pero **el hardware no es indiferente**. sigrok y HantekDSO tienen el mismo error. Verificado 1→1000+ ciclos de 246 µs a 819 ms | `1fb234f` |
| 11 | Tabla V/div ficticia (0.16 V..80 V) | Captura Windows recorriendo el selector + clics de relés | Tabla real 10 V..10 mV (1-2-5). Explica el "CH2 plano en 4 V/40 V": esos pasos no existían | `0df2b29` |

Descartado (no era bug): en `d477bae` se concluyó que la escala "inconsistente"
entre pasos era la señal CAL saturando el ADC (1023 en ambos extremos en
0.2–1 V/div). **Esa conclusión fue incompleta** — ver §3.

---

## 3. Abierto: calibración de amplitud por V/div ❌ (prioridad 1)

Estado del código (commit `c5d8ad9`, marcado WIP, **no confirmado**):

```cpp
// dso5200.cpp:115-119
kGain[9] = { {0,0.02},{0,0.05},{0,0.1},{0,0.2},{0,0.5},{0,1.0},{0,2.0},{0,5.0},{0,10.0} };
kVoltageLimit[9] = {14, 35, 69, 22, 56, 112, 44, 110, 220};
// dso5200.cpp:353-354 (doSetGain)
setBelow1V(channel, gainID <= 5);
setBelow100mV(channel, gainID <= 2);
```

Lo que se midió con `rawgaintest` y generador a 2 Vpp/1 kHz (sin saturar):

- Solo existen **3 estados analógicos reales**. `below1V`/`below100mV`
  funcionan como un selector de 2 bits ("cuántos están activos"):
  - ninguno → grupo 2/5/10 V: **≈22 cuentas/V** (medido directo, fiable)
  - `below1V` → grupo 200 mV/500 mV/1 V: **≈112 cuentas/V** (medido directo, fiable)
  - ambos → grupo 20/50/100 mV: **≈690 cuentas/V** (*derivado* vía una sonda
    x10 sin calibrar con razón ≈6.2×; **el dato más débil**)
- `gainBits` (`kGain[].index`, enviado por `BulkSetGain`) **no tiene efecto
  medible** en la amplitud (probado 0–3). Se dejó en 0.
- Las razones entre grupos no son limpias (≈5.1× y ≈6.2×).
- Dentro de un grupo, `kVoltageLimit[g] = cuentasPorVolt(grupo) × voltsPerDiv(g)`
  (porque el visor divide otra vez por volts/div al graficar).

**Resultado al probar con el aparato: "salió mal".** El usuario no dio más
detalle y no quedó registrado qué pasos fallaron ni el síntoma.

### Próximos pasos sugeridos
1. **Preguntar al usuario** qué vio exactamente: qué V/div, y si la señal sale
   más grande, más pequeña, plana o ruidosa.
2. Usar el nuevo comando **`cal`** (cuentas crudas, sin conversión) para
   re-medir cada grupo con una señal que **no sature** — no la señal CAL.
3. Medir el grupo 20–100 mV **directamente**, con una señal de amplitud
   conocida bajo ~0.3–0.5 Vpp (generador atenuado o divisor resistivo de
   razón medida con multímetro), sin pasar por la sonda sin calibrar.
4. Confirmar los otros dos grupos también (podrían estar mal de una forma que
   no se detectó).
5. El paso de 10 mV/div no tiene calibración propia (reutiliza la de 20 mV).

---

## 4. Otros pendientes menores

- Acoplamiento **GND**: firma USB aislada (`GainBits=0, below1V=false,
  below100mV=false`, independiente por canal), pero no expuesta como comando
  CLI. El bit de relé AC/DC en modo GND no está verificado.
- El offset vertical por defecto no siempre centra la señal: comportamiento
  normal de un osciloscopio, no un defecto.

---

## 5. ⚠️ Documentación desactualizada — corregir

- **`AGENTS.md`**, viñeta "Open bug (CH2 saturation)": está **obsoleta**. El
  "CH2 plano en 4 V/40 V" se resolvió en `0df2b29`: esos pasos no existían.
  El problema abierto real es la calibración de amplitud (§3).
- **`AGENTS.md`**, viñeta de V/div: describe la tabla de `0df2b29`. Desde
  `c5d8ad9` los umbrales son `below1V = gainID<=5`, `below100mV = gainID<=2`,
  `gainBits` = 0 en todos, y `kVoltageLimit` es proporcional dentro de cada
  grupo (§3). No "corregir" el código de vuelta a lo que dice `AGENTS.md`.
- **`README.md`** línea ~326: sección "CH2 lee cero plano en 4V/div y 40V/div
  — abierto, sin resolver". Igual que arriba: está resuelta; marcarla como
  tal y añadir una sección para la calibración de amplitud abierta.

---

## 6. Reglas para seguir trabajando

- Todo en español (código, comentarios, commits), como dice `AGENTS.md`.
- Commits solo locales; **nunca hacer push**.
- No declarar un bug corregido sin una verificación contra el hardware real
  descrita en el commit (qué señal, qué V/div, qué se midió).
- No regresar ninguno de los arreglos de §2: cada uno costó una sesión de
  hardware encontrarlo.
