"""
|avg|      : Mittelwert der Beträge   mean(|x|)
|median|   : Median  der Beträge      median(|x|)
jitter σ   : Standardabw. der signed-Werte (±-Band um 0)
"""

import argparse
import csv
import re
import signal
import sys
from typing import List, Tuple, Optional

import matplotlib.pyplot as plt
import numpy as np
import serial

UART_PORT   = "/dev/tty.usbserial-021CRXVM"
BAUDRATE    = 115_200
DEFAULT_OUT = "valuesObserver.csv"

FLOAT = r"[-+]?\d+(?:\.\d+)?"

LINE_RE = re.compile(
    rf"""
    S\[(\d+)]\s*:\s*          # cycle-counter
    ({FLOAT})\s+              # offset
    ({FLOAT})                 # drift_filtered
    (?:\s+({FLOAT}))?         # drift_unfiltered
    """,
    re.VERBOSE,
)
DIFF_THRESHOLD  = 1_000_000


def parse(line: str) -> Optional[Tuple[int, int, float, float]]:
    m = LINE_RE.search(line)
    if not m:
        return None

    cycle          = int(m.group(1))
    offset         = int(m.group(2))          # ← jetzt signed int
    drift_filtered = float(m.group(3))
    drift_unfilt   = float(m.group(4)) if m.group(4) is not None else drift_filtered
    return cycle, offset, drift_filtered, drift_unfilt


def graceful_exit(_signum, _frame):
    """Translate an OS signal (e.g. Ctrl+C) into KeyboardInterrupt"""
    raise KeyboardInterrupt


def main():
    parser = argparse.ArgumentParser(
        description="Serielle Aufnahme von AP/M/S-Zeitstempeln, "
                    "Speichern als CSV und Plotten der Differenzen "
                    "mit |avg|, |median| und Jitter-Band. "
                    f"Zeilen mit |diff| > {DIFF_THRESHOLD} werden verworfen."
    )
    parser.add_argument("-p", "--port", default=UART_PORT,
                        help=f"Serieller Port (default {UART_PORT})")
    parser.add_argument("-b", "--baud", default=BAUDRATE, type=int,
                        help=f"Baudrate (default {BAUDRATE})")
    parser.add_argument("-n", "--max", type=int, default=None,
                        help="Stoppt nach X gültigen Zeilen (optional)")
    parser.add_argument("-o", "--out", default=DEFAULT_OUT,
                        help=f"CSV-Datei (default {DEFAULT_OUT})")
    args = parser.parse_args()

    # STRG+C
    signal.signal(signal.SIGINT, graceful_exit)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        sys.exit(f"Could not open {args.port}: {e}")

    cycles, of_vals, df_vals, du_vals = [], [], [], []

    print(
        f"Listening on {args.port} @ {args.baud} baud "
        f"(Ctrl+C to stop; logging & plotting will follow…)\n"
    )

    try:
        while True:
            raw = ser.readline().decode(errors="ignore").strip()
            if not raw:
                continue

            parsed = parse(raw)
            if not parsed:
                continue

            c, of, df, du = parsed

            cycles.append(c)
            of_vals.append(of)
            df_vals.append(df)
            du_vals.append(du)

            print(f"\rCaptured {len(cycles)} valid lines; last cycle {c}", end="")

            if args.max and len(cycles) >= args.max:
                break
    except KeyboardInterrupt:
        print("\nCapture stopped by user.")
    finally:
        ser.close()

    if not cycles:
        sys.exit("No matching lines captured; nothing to save or plot.")

    try:
        with open(args.out, "w", newline="") as f:
            wr = csv.writer(f)
            wr.writerow(["cycle", "offset", "drift_filtered", "drift_unfiltered"])

            for row in zip(cycles, of_vals, df_vals, du_vals):
                wr.writerow(row)
        print(f"\nCSV written to {args.out}")
    except IOError as e:
        print(f"\nCould not write CSV: {e}")

if __name__ == "__main__":
    main()