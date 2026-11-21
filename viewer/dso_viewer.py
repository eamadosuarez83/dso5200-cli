#!/usr/bin/env python3
"""Live viewer for dso5200basic: connects to its TCP sample stream and plots
channels in real time with matplotlib. See ../docs/PROTOCOL.md for the wire
format this decodes.

Usage: python3 dso_viewer.py [--host 127.0.0.1] [--port 5200]
"""

import argparse
import socket
import struct
import threading
import math
import queue

import matplotlib

# Prefer TkAgg over the default Qt backend: a background thread here feeds
# frames into a queue.Queue (safe on its own), but matplotlib's Qt (PyQt6)
# backend has been observed to abort the whole process with a Qt-internal
# "QMessageLogger::fatal" inside QTimer's event handling during long-running
# animated sessions on this stack (confirmed via a core dump). TkAgg avoids
# PyQt6 entirely and is the more battle-tested backend for this pattern.
try:
    matplotlib.use("TkAgg")
except ImportError:
    pass  # fall back to whatever matplotlib picks by default

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

MAGIC = b"DSO6"
HEADER_FMT = "<4sIQd B"  # magic, frame_number, timestamp_ns, samplerate, num_channels
HEADER_SIZE = struct.calcsize(HEADER_FMT)
CHANNEL_HEADER_FMT = "<B f I I f f"  # channel_id, volts_per_div, num_samples, clipped, adc_min_v, adc_max_v
CHANNEL_HEADER_SIZE = struct.calcsize(CHANNEL_HEADER_FMT)

CHANNEL_COLORS = {0: "goldenrod", 1: "tab:cyan"}
HORIZONTAL_DIVS = 10
VERTICAL_HALF_DIVS = 5  # +-5 div: deja ver un poco mas alla de la pantalla de 8 div del aparato


def format_volts(v):
    return f"{v * 1000:g} mV" if abs(v) < 1 else f"{v:g} V"


def format_seconds(t):
    for unit, factor in (("s", 1), ("ms", 1e-3), ("us", 1e-6), ("ns", 1e-9)):
        if abs(t) >= factor:
            return f"{t / factor:.3g} {unit}"
    return f"{t:.3g} s"


def recv_exact(sock, length):
    chunks = []
    remaining = length
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("Servidor cerro la conexion")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_frame(sock):
    header = recv_exact(sock, HEADER_SIZE)
    magic, frame_number, timestamp_ns, samplerate, num_channels = struct.unpack(HEADER_FMT, header)
    if magic != MAGIC:
        raise ValueError(f"Magic invalido: {magic!r} (se esperaba {MAGIC!r}; servidor y visor de versiones distintas?)")

    channels = []
    for _ in range(num_channels):
        chan_header = recv_exact(sock, CHANNEL_HEADER_SIZE)
        channel_id, volts_per_div, num_samples, clipped, adc_min_v, adc_max_v = struct.unpack(
            CHANNEL_HEADER_FMT, chan_header
        )
        samples_bytes = recv_exact(sock, num_samples * 4)
        samples = struct.unpack(f"<{num_samples}f", samples_bytes)
        channels.append(
            {
                "channel_id": channel_id,
                "volts_per_div": volts_per_div,
                "samples": samples,
                "clipped": clipped,
                "adc_min_v": adc_min_v,
                "adc_max_v": adc_max_v,
            }
        )

    return {
        "frame_number": frame_number,
        "timestamp_ns": timestamp_ns,
        "samplerate": samplerate,
        "channels": channels,
    }


def receiver_thread(host, port, frame_queue, stop_event):
    while not stop_event.is_set():
        try:
            with socket.create_connection((host, port), timeout=5) as sock:
                print(f"Conectado a {host}:{port}")
                while not stop_event.is_set():
                    frame = read_frame(sock)
                    # Keep only the latest frame; drop older ones so the plot never lags.
                    try:
                        while True:
                            frame_queue.get_nowait()
                    except queue.Empty:
                        pass
                    frame_queue.put(frame)
        except (ConnectionError, OSError) as exc:
            if not stop_event.is_set():
                print(f"Conexion perdida ({exc}), reintentando en 2s...")
                stop_event.wait(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5200)
    args = parser.parse_args()

    frame_queue = queue.Queue()
    stop_event = threading.Event()
    thread = threading.Thread(target=receiver_thread, args=(args.host, args.port, frame_queue, stop_event), daemon=True)
    thread.start()

    fig, ax = plt.subplots()
    ax.set_xlabel("Tiempo (divisiones)")
    ax.set_ylabel("Divisiones")
    ax.set_xlim(0, HORIZONTAL_DIVS)
    ax.set_ylim(-VERTICAL_HALF_DIVS, VERTICAL_HALF_DIVS)
    ax.set_xticks(range(HORIZONTAL_DIVS + 1))
    ax.set_yticks(range(-VERTICAL_HALF_DIVS, VERTICAL_HALF_DIVS + 1))
    ax.grid(True, color="0.85", linewidth=0.6)
    ax.axhline(0.0, color="0.5", linewidth=0.8)  # 0 V de todos los canales
    lines = {}
    rails = {}  # cid -> (linea min ADC, linea max ADC)
    fig.suptitle("dso5200basic - esperando datos...")
    info = ax.text(0.01, 0.99, "", transform=ax.transAxes, va="top", ha="left", fontsize=9, family="monospace")

    def update(_frame_index):
        try:
            frame = frame_queue.get_nowait()
        except queue.Empty:
            return list(lines.values())

        samplerate = frame["samplerate"]
        active_ids = set()
        info_lines = []
        time_per_div = None
        for channel in frame["channels"]:
            cid = channel["channel_id"]
            active_ids.add(cid)
            samples = channel["samples"]
            n = len(samples)
            # Eje X en divisiones: la captura completa ocupa HORIZONTAL_DIVS.
            x = [i * HORIZONTAL_DIVS / n for i in range(n)] if n else []
            if samplerate > 0 and n:
                time_per_div = n / samplerate / HORIZONTAL_DIVS
            volts_per_div = channel["volts_per_div"] or 1.0
            divisions = [s / volts_per_div for s in samples]
            color = CHANNEL_COLORS.get(cid, None)

            if cid not in lines:
                (lines[cid],) = ax.plot([], [], color=color, linewidth=0.9, label=f"CH{cid + 1}")
                lo = ax.axhline(0, color=color, linestyle="--", linewidth=0.8, alpha=0.7)
                hi = ax.axhline(0, color=color, linestyle="--", linewidth=0.8, alpha=0.7)
                rails[cid] = (lo, hi)
                ax.legend(loc="upper right")
            lines[cid].set_data(x, divisions)

            # Topes del ADC en divisiones: fuera de ellos la traza no es medida real.
            lo, hi = rails[cid]
            for line, volts in ((lo, channel["adc_min_v"]), (hi, channel["adc_max_v"])):
                if math.isfinite(volts):
                    line.set_ydata([volts / volts_per_div] * 2)
                    line.set_visible(True)
                else:
                    line.set_visible(False)

            text = f"CH{cid + 1} {format_volts(volts_per_div)}/div"
            if channel["clipped"]:
                pct = 100.0 * channel["clipped"] / n if n else 0.0
                text += f"  RECORTE {channel['clipped']} muestras ({pct:.1f}%)"
            info_lines.append(text)

        for cid, line in lines.items():
            visible = cid in active_ids
            line.set_visible(visible)
            if not visible:
                for rail in rails[cid]:
                    rail.set_visible(False)

        if time_per_div is not None:
            info_lines.append(f"{format_seconds(time_per_div)}/div")
        info.set_text("\n".join(info_lines))
        clipped_any = any(ch["clipped"] for ch in frame["channels"])
        info.set_color("red" if clipped_any else "black")

        fig.suptitle(
            f"dso5200basic - frame {frame['frame_number']}  samplerate={samplerate/1e6:.3f} MS/s"
        )
        return list(lines.values())

    ani = FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)
    try:
        plt.show()
    finally:
        stop_event.set()


if __name__ == "__main__":
    main()
