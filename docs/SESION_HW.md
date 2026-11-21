# Guion de sesión con hardware (T1, T2, T3, T6)

Objetivo: confirmar o descartar el modelo de `PLAN_ESCALAS.md` §3 con el menor
número de cambios de cableado. El programa lo puede manejar el agente,
mandando comandos por stdin. La persona solo cambia las conexiones físicas en
los puntos marcados con 🔌.

Arranque (deja todo el log en un archivo para analizarlo después):

```sh
./build/dso5200basic --debug 2> sesion_hw.log
```

`sesion_hw.log` está en `.gitignore`. Cada línea `[cal] chN raw min=… max=…
mean=… pp=…` corresponde a un frame.

## Paso A — cero con la entrada a GND (T1 + T2)

🔌 CH1 y CH2 a GND: punta con el gancho sobre su propia pinza de tierra, o
entrada BNC en corto.

Para cada V/div `v` de `0.02 0.05 0.1 0.2 0.5 1 2 5 10`:

```
volts 1 v
volts 2 v
cal 1          # ~2 s de frames, anotar mean
cal 2
cal off
```

Resultado esperado si el modelo es correcto: `mean` ≈ 511 en los 9 pasos, o
al menos el mismo valor dentro de cada grupo (0–2, 3–5, 6–8), y pp pequeño
(solo ruido).

Después, en un V/div de cada grupo (0.1, 1 y 10):

```
offsettest 1 0      → cal 1
offsettest 1 0.25   → cal 1
offsettest 1 0.5    → cal 1
offsettest 1 0.75   → cal 1
offsettest 1 1      → cal 1
```

Resultado esperado: `mean` lineal con el offset, de ~0 a ~1023. Si la
pendiente o la ordenada no cuadran, `rawZero()` necesita la recta medida
(`a + b·offset`) por grupo en lugar de `offset × 1023`.

Por último, `zero 1` y `zero 2` en cada grupo, y `status` para ver `zeroCorr`.

## Paso B — ganancia por grupo (T3/T4)

🔌 Generador de 1 kHz senoidal a CH1, con la amplitud medida con multímetro
(VAC × 2.83 = Vpp) u otro osciloscopio:
- grupo 2–10 V: unos 10 Vpp (sin saturar: pp en `cal` < 900);
- grupo 0.2–1 V: unos 2 Vpp;
- grupo 20–100 mV: 100–300 mVpp mediante un divisor resistivo de razón medida.

En un V/div de cada grupo: `cal 1`, y anotar pp. Entonces
`cpv = pp / Vpp_real`. Compararlo con 22 / 112 / 690.

## Paso C — validación en pantalla (T6)

🔌 Generador a 2 Vpp en CH1, visor abierto (`python3 viewer/dso_viewer.py`).
Recorrer los 9 V/div. En cada uno, el pico a pico en pantalla debería ser
2/Vdiv divisiones (±5 %), centrado en 0. Donde se salga de la pantalla, el
visor debe marcar RECORTE y dibujar los topes del ADC en línea discontinua.

## Qué anotar en el commit

La tabla V/div → mean (GND), la recta offset → mean por grupo, el cpv medido
por grupo con la amplitud real usada, y el resultado del paso C.
