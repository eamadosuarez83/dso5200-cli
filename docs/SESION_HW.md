# Guion de sesión con hardware (T1, T2, T3, T4, T6)

Material: **solo** la salida CAL del propio aparato (cuadrada, 2 Vpp, 1 kHz),
una sonda con selector x1/x10 y la posibilidad de poner las entradas a GND.
No hace falta generador ni multímetro.

El programa lo puede manejar el agente, mandando comandos por stdin. La
persona solo cambia las conexiones en los puntos marcados con 🔌.

```sh
./build/dso5200basic --debug 2> sesion_hw.log     # sesion_hw.log está en .gitignore
```

Usar `timebase 10ms` (unos 10 ciclos de CAL por captura). `cal <ch>` imprime
dos líneas por frame:

```
[cal] ch1 raw min=… max=… mean=… pp=… n=…
[cal] ch1 meseta alto=… bajo=… amp=… ciclo=…% | modelo: grupo=… cpv=… rawZero=… Vamp=… Vbajo=…
```

Para la ganancia usar **`amp`** (diferencia entre los niveles de meseta), no
`pp`: con la sonda mal compensada la cuadrada tiene sobreimpulso en los
flancos y eso infla min/max. `Vamp` es la amplitud que da el modelo actual;
si el modelo está bien, debe salir 2.0 V (o 0.2 V en x10). Tomar 3–5 frames y
promediar.

Niveles esperados si el modelo actual (`kHwGroup`) es correcto:

| Grupo | V/div | cpv modelo | CAL x1 (2 V) | CAL x10 (0.2 V) |
|---|---|---|---|---|
| 0 | 20/50/100 mV | 690 | ~1380 → **satura** | ~138 |
| 1 | 200/500 mV/1 V | 112 | ~224 | ~22 |
| 2 | 2/5/10 V | 22 | ~44 | ~4 (inútil) |

## Paso A — cero (T1 + T2)

🔌 CH1 y CH2 a GND (gancho de la sonda sobre su propia pinza de tierra).

1. Para cada V/div `0.02 0.05 0.1 0.2 0.5 1 2 5 10`: `volts 1 v`, `volts 2 v`,
   `cal 1`, `cal 2`, `cal off`. Anotar `mean`. Esperado: el mismo valor
   dentro de cada grupo, idealmente ≈511, y pp pequeño (solo ruido).
2. En 0.1, 1 y 10 V/div: `offsettest 1 f` para f = 0, 0.25, 0.5, 0.75, 1, con
   `cal 1` después de cada uno. Esperado: `mean` lineal con f, de ~0 a ~1023.
   Si no es así, `rawZero()` necesita la recta medida por grupo.
3. `zero 1` y `zero 2` en cada grupo y `status` para ver `zeroCorr`.

## Paso B — ganancia con la CAL (T3 + T4)

🔌 Sonda de CH1 en **x1**, punta en CAL y pinza de tierra en la tierra de CAL.

1. `volts 1 1` (grupo 1): `cal 1` → `amp1_x1`. Esperado ≈224.
   **cpv(grupo 1) = amp1_x1 / 2.**
2. `volts 1 10` (grupo 2): `cal 1` → `amp2_x1`. Esperado ≈44.
   **cpv(grupo 2) = amp2_x1 / 2.** (Resolución baja; promediar varios frames.)
3. `Vbajo`: si la CAL va de 0 a 2 V, el nivel bajo debe dar ≈0 V. Es una
   comprobación extra del cero (no es concluyente: la CAL podría ser ±1 V).

🔌 Mismo cableado, selector de la sonda en **x10**.

4. `volts 1 1` (grupo 1): `cal 1` → `amp1_x10`.
   **Factor real de la sonda = amp1_x1 / amp1_x10** (debería ser ≈10).
5. `volts 1 0.1` (grupo 0): `cal 1` → `amp0_x10`. Esperado ≈138.
   **cpv(grupo 0) = cpv(grupo 1) × amp0_x10 / amp1_x10.** Este cociente no
   depende del factor de la sonda, porque es la misma señal en los dos grupos.
6. Repetir 1–5 en CH2.

Limitación: la exactitud absoluta depende de que la CAL dé exactamente 2 Vpp.
Los cocientes entre grupos no dependen de eso.

## Paso C — validación en pantalla (T6)

Cargar en `kHwGroup` los cpv medidos, recompilar y abrir el visor
(`python3 viewer/dso_viewer.py`).

🔌 Sonda en x1 en CAL: recorrer de 0.2 a 10 V/div. El pico a pico en
pantalla debe ser 2/Vdiv divisiones (±5 %): 10 div a 0.2 V, 4 div a 0.5 V,
2 div a 1 V, 1 div a 2 V, 0.4 div a 5 V, 0.2 div a 10 V.

🔌 Sonda en x10: recorrer de 20 a 100 mV/div para ver 0.2 V reales. Esperado:
10 div a 20 mV, 4 div a 50 mV, 2 div a 100 mV. El factor de la sonda se
corrige aparte: el programa no tiene ajuste de sonda.

Donde la señal se salga del ADC, el visor debe marcar RECORTE y dibujar los
topes en línea discontinua.

## Qué anotar en el commit

Tabla V/div → mean (GND), recta offset → mean por grupo, `amp` de cada paso
de B con su cpv calculado, factor real de la sonda, y el resultado de C.
