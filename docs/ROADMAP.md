# Roadmap

The delivered half of this snapshot is the **CAN-FD logger** (firmware core + tested host +
bit-timing solver). The open threads below are carried forward from the design capsule.

## CAN-FD logger — firmware completion

- [ ] **SD / FatFS batch writer** — block-aligned on-board logging. The hook exists in
      `send_frame()`; wire FatFS `f_write()` in blocks.
- [ ] **Hardware bit-timing verification** — set MCAN functional clock = 80 MHz; confirm
      NOM 31/8/8, DATA 12/3/3 (2 Mbps / 5 Mbps) against a known-good node.
- [ ] **Enable TDC/SSP** in `mcan_init()` (secondary sample point in the data phase).
- [ ] **Confirm the on-board transceiver part** (5 Mbps class) from the LaunchPad schematic.

## UDS tester — the second half of the repo title

- [ ] **UDS request/response tester** — the diagnostic-tester counterpart to the logger
      (the on-bus active side). Not yet delivered in this snapshot.

## Host — production hardening

- [ ] **Port the host to Rust + egui** for the fielded tool. The host is ARM/x86 and fully
      Rust-capable; the C28x firmware is not (no LLVM backend).

## Tier B — higher data rate

- [ ] If **8 Mbps** is ever required → **F2838x + TCAN1462 SIC + Ethernet** (see
      [`BOM.md`](BOM.md)). Backlog until a use case demands it.

---
*Source: the 2026-06-22 CAN-FD logger atelier design capsule. The verbatim firmware,
host, and solver artifacts were ferried into this repository from that capsule.*
