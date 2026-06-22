# host/ — laptop logger & live monitor

`can_logger_host.py` is the laptop-side companion for the C2000 CAN-FD logger. It reads
the board's framed stream over USB-serial (the XDS110 virtual COM, or any USB-UART),
writes a **replayable CSV**, and prints a **live monitor**: frame rate, bus load, error
state, and the busiest IDs.

Pure Python stdlib except `pyserial`, which is needed **only** for live capture — the
`--demo` mode (and the parser, CSV sink, and stats) run on the standard library alone.

## Install

```bash
pip install -r requirements.txt
```

## Run

```bash
# Live — capture from a flashed board:
python can_logger_host.py --port COM5 --baud 3000000 --csv run1.csv

# Demo — prove the whole pipeline with no hardware:
python can_logger_host.py --demo --seconds 2 --csv demo.csv
```

| Flag | Default | Meaning |
|---|---|---|
| `--port` | — | Serial port (e.g. `COM5`, `/dev/ttyACM0`). Required for live mode. |
| `--baud` | `3000000` | Must match the firmware's SCIA baud (3 Mbaud). |
| `--csv` | *(none)* | Output CSV path. Omit to monitor without logging. |
| `--demo` | off | Run the built-in frame generator instead of a serial port. |
| `--seconds` | `0` | Run duration (0 = until Ctrl-C; demo defaults to 2 s). |
| `--rate` | `4000` | Demo frames/sec. |

## CSV columns (replayable)

```
ts_us, id_hex, ext, fd, brs, dlc, len, data_hex
```

One row per captured frame — timestamp (µs), CAN ID (hex), extended/FD/BRS flags, DLC,
byte length, and the payload as upper-hex. Suitable for replay and offline analysis.

## How it parses

The `Parser` is a resyncing state machine: it hunts for the `AA 55` sync, validates the
CRC-8 over `[type, len, payload]`, and on any garbage skips past the bad sync and keeps
going — a noisy or mid-stream connection self-recovers rather than wedging. Wire spec:
[`../docs/WIRE_PROTOCOL.md`](../docs/WIRE_PROTOCOL.md).

## Production note

For a fielded tool, the host is the part to harden — and unlike the C28x firmware, it is
fully Rust-capable (host is ARM/x86). A **Rust + egui** rewrite is the planned production
host; see [`../docs/ROADMAP.md`](../docs/ROADMAP.md).
