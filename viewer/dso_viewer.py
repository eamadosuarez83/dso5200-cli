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
import queue

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

MAGIC = b"DSO5"
HEADER_FMT = "<4sIQd B"  # magic, frame_number, timestamp_ns, samplerate, num_channels
HEADER_SIZE = struct.calcsize(HEADER_FMT)
CHANNEL_HEADER_FMT = "<B f I"  # channel_id, volts_per_div, num_samples
CHANNEL_HEADER_SIZE = struct.calcsize(CHANNEL_HEADER_FMT)

CHANNEL_COLORS = {0: "gold", 1: "tab:cyan"}


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
        raise ValueError(f"Magic invalido: {magic!r}")

    channels = []
    for _ in range(num_channels):
        chan_header = recv_exact(sock, CHANNEL_HEADER_SIZE)
        channel_id, volts_per_div, num_samples = struct.unpack(CHANNEL_HEADER_FMT, chan_header)
        samples_bytes = recv_exact(sock, num_samples * 4)
        samples = struct.unpack(f"<{num_samples}f", samples_bytes)
        channels.append({"channel_id": channel_id, "volts_per_div": volts_per_div, "samples": samples})

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
    ax.set_xlabel("Tiempo (s)")
    ax.set_ylabel("Divisiones (V/div por canal)")
    ax.set_ylim(-5, 5)
    lines = {}
    fig.suptitle("dso5200basic - esperando datos...")

    def update(_frame_index):
        try:
            frame = frame_queue.get_nowait()
        except queue.Empty:
            return list(lines.values())

        samplerate = frame["samplerate"]
        active_ids = set()
        for channel in frame["channels"]:
            cid = channel["channel_id"]
            active_ids.add(cid)
            samples = channel["samples"]
            n = len(samples)
            t = [i / samplerate for i in range(n)] if samplerate > 0 else list(range(n))
            volts_per_div = channel["volts_per_div"] or 1.0
            divisions = [s / volts_per_div for s in samples]

            if cid not in lines:
                (lines[cid],) = ax.plot([], [], color=CHANNEL_COLORS.get(cid, None), label=f"CH{cid + 1}")
                ax.legend(loc="upper right")
            lines[cid].set_data(t, divisions)
            if t:
                ax.set_xlim(t[0], t[-1])

        for cid, line in lines.items():
            line.set_visible(cid in active_ids)

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
