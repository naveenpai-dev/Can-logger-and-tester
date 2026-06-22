# Can-logger-and-tester

**A TI C2000 LaunchPad, retrofitted into an automotive-grade CAN-FD logger and UDS tester.**

Pure firmware + a laptop companion. No bench analyzer, no licensed stack — flash a
~$100 LaunchPad, plug in USB, and capture every frame on a 2 Mbps / 5 Mbps CAN-FD bus
to a replayable CSV with a live bus-load / error monitor.

This repository is a **self-contained snapshot**: clone it, install the two declared
dependencies (Code Composer Studio + C2000Ware for the firmware, `pyserial` for the
host), and build. Nothing reaches outside the tree except those two named toolchains.

---

## What's in the box

| Path | What it is | State |
|---|---|---|
| `firmware/atelier_can_logger.c` | C28x firmware core — MCAN FD, accept-all, lock-free ring, framed SCI stream. | Reference core — flash-and-iterate (see `firmware/README.md`). |
| `host/can_logger_host.py` | **Logger** — parses the stream, writes CSV, live stats. `--demo` needs no board. | Tested. |
| `host/uds_tester.py` | **UDS tester** — ISO 14229-1 client over ISO-TP (sessions, DID r/w, DTCs, security). `--demo` runs against a simulated ECU. | Tested. |
| `tools/canfd_bittiming.py` | CAN-FD bit-timing solver — BRP/TSEG1/TSEG2/SJW per phase for any MCAN clock. | Tested. |
| `docs/` | Wire protocol, bit-timing, BOM, UDS tester design, roadmap. | — |

Two host tools, one board: the logger is the **passive** capture side, the tester the
**active** diagnostic side. The firmware is a transparent CAN↔serial bridge serving both.

---

## 60-second proof (no hardware)

The host pipeline is provable end-to-end without a board — a built-in generator emits
the exact framed protocol the firmware would:

```bash
cd host
python can_logger_host.py --demo --seconds 2 --csv demo.csv
```

You'll see the live monitor tick (frame rate, bus load %, error state, busiest IDs) and
`demo.csv` will hold a replayable log. That same `--port COM5 --baud 3000000` invocation
drives the real board once flashed.

---

## The signal path

```
   CAN-FD bus                 C2000 LaunchPad (firmware)                    Laptop (host)
  ────────────    ┌──────────────────────────────────────────┐    ┌────────────────────────┐
   2/5 Mbps  ───► │ MCAN RX FIFO0 ─ISR─► lock-free ring (RAM) │    │ resync parser ► CSV     │
   accept-all     │            ─main─► framed SCI @ 3 Mbaud ───┼───►│            ► live stats │
                  │            ─main─► SD batch (FatFS, TODO)  │    │            ► busiest IDs │
                  └──────────────────────────────────────────┘    └────────────────────────┘
```

Sized for **≥92 % bus load**: hardware RX FIFO absorbs the burst, a power-of-two ring
buffer decouples the ISR from I/O, and a `drop_count` makes any overflow visible rather
than silent. The bus is never blocked — a full ring drops and counts, it does not stall.

The wire format is one flat framing — `AA 55 | type | len | payload | crc8` — shared
byte-for-byte between `firmware/` and `host/`. Full spec: [`docs/WIRE_PROTOCOL.md`](docs/WIRE_PROTOCOL.md).

---

## Two build targets, two cost tiers

| Tier | Board | Transceiver | Data rate | ~Cost |
|---|---|---|---|---|
| **A** (this snapshot) | LAUNCHXL-F280039C | on-board CAN-FD | ≤ 5 Mbps | $90–110 |
| **B** (roadmap) | F2838x + Ethernet | TCAN1462 SIC | 8 Mbps | $330–390 |

5 Mbps lives entirely within the LaunchPad's on-board transceiver — no external SIC.
8 Mbps needs the SIC transceiver and is a Tier-B build. See [`docs/BOM.md`](docs/BOM.md).

---

## Status & roadmap

Both halves named in the repo title are delivered host-side: the **CAN-FD logger** and the
**UDS tester** (ISO-TP + UDS, proven against a simulated ECU). Open threads — the on-board
**SD/FatFS** path, the firmware **SCI→CAN bridge** for live tester use, hardware bit-timing
verification, and a **Rust + egui** production host — are tracked in
[`docs/ROADMAP.md`](docs/ROADMAP.md).

---

## License

MIT — see [`LICENSE`](LICENSE).
