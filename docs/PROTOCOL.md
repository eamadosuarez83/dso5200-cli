# Protocolo de streaming dso5200basic -> visor

TCP, solo localhost (`127.0.0.1`), puerto por defecto `5200` (`--port` para cambiarlo).
Todos los enteros/flotantes van en little-endian nativo (host x86_64); no hay
negociación de endianness porque el servidor y el visor siempre corren en la
misma máquina.

El servidor envía un frame por cada captura procesada. No hay handshake: al
conectar, el cliente simplemente empieza a recibir frames.

## Formato de un frame

```
offset  tamaño  campo
0       4       magic = "DSO6" (ASCII, sin terminador; "DSO5" era la versión sin datos de recorte)
4       4       frame_number (uint32)
8       8       timestamp_ns (uint64, epoch UNIX en nanosegundos)
16      8       samplerate (float64, muestras/segundo)
24      1       num_channels (uint8)
25...           num_channels bloques de canal (ver abajo)
```

Cada bloque de canal:

```
offset  tamaño  campo
0       1       channel_id (uint8, 0 = CH1, 1 = CH2)
1       4       volts_per_div (float32)
5       4       num_samples (uint32)
9       4       clipped_samples (uint32, muestras en la cuenta 0 o 1023 del ADC)
13      4       adc_min_volts (float32, volts de la cuenta 0; NaN si se desconoce)
17      4       adc_max_volts (float32, volts de la cuenta 1023; NaN si se desconoce)
21      num_samples*4   samples (float32[], ya convertidas a volts)
```

Solo se incluyen los canales activos en ese momento (los inactivos no aparecen
en el frame, no se envían con longitud 0).

## Referencia de implementación

- Serialización: `src/streaming/sample_server.cpp` (`serializeFrame`)
- Deserialización: `viewer/dso_viewer.py` (`read_frame`)
- Estructura en memoria antes de serializar: `src/device/capture_frame.h`
