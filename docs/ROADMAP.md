# Roadmap

Delivered in this snapshot: the **CAN-FD logger** (firmware core + tested host + bit-timing
solver) and the **UDS tester** host tool (ISO-TP + UDS client, proven against a simulated
ECU via `--demo`). The open threads below are carried forward from the design capsule.

## CAN-FD logger — firmware completion

- [ ] **SD / FatFS batch writer** — block-aligned on-board logging. The hook exists in
      `send_frame()`; wire FatFS `f_write()` in blocks.
- [ ] **Hardware bit-timing verification** — set MCAN functional clock = 80 MHz; confirm
      NOM 31/8/8, DATA 12/3/3 (2 Mbps / 5 Mbps) against a known-good node.
- [ ] **Enable TDC/SSP** in `mcan_init()` (secondary sample point in the data phase).
- [ ] **Confirm the on-board transceiver part** (5 Mbps class) from the LaunchPad schematic.

## UDS tester — the second half of the repo title

- [x] **UDS request/response tester** (`host/uds_tester.py`) — ISO-TP transport + UDS client
      (sessions, DID r/w, DTC read/clear, routine control, ECU reset, security access,
      tester-present), MM Mill address book, NRC decoding, proven against a simulated ECU.
- [ ] **Firmware SCI→CAN bridge** — the small host→board TX path that lets the tester drive a
      real bus (documented in `firmware/README.md`; the demo needs no firmware).
- [ ] **Live-ECU validation** — run the tester against a real MM ECU once the bridge is flashed.
- [ ] **Extra services** — functional addressing, 0x85 ControlDTCSetting, 0x28 CommunicationControl.
- [ ] **Real key algorithm** — replace the demo seed→key toy transform with the OEM algorithm.

## Host — production hardening

- [ ] **Port the host to Rust + egui** for the fielded tool. The host is ARM/x86 and fully
      Rust-capable; the C28x firmware is not (no LLVM backend).

## Tier B — higher data rate

- [ ] If **8 Mbps** is ever required → **F2838x + TCAN1462 SIC + Ethernet** (see
      [`BOM.md`](BOM.md)). Backlog until a use case demands it.

---
*Source: the 2026-06-22 CAN-FD logger atelier design capsule. The verbatim firmware,
host, and solver artifacts were ferried into this repository from that capsule.*
