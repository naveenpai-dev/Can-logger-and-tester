# firmware/ — C2000 CAN-FD logger core

`atelier_can_logger.c` retrofits a TI C2000 LaunchPad into a CAN-FD logger. It is a
**reference core**: written to flash-and-iterate on real silicon, not to compile blind.
Verify the bit-timing constants and the board-init hooks for *your* device clock before
trusting a capture.

## Target

- **Board:** LAUNCHXL-F280039C (MCAN + on-board CAN-FD transceiver, XDS110 debug/COM).
- **Core:** TI C28x (no LLVM backend — this firmware is **not** Rust-migratable; the host is).
- **Bus:** CAN-FD, 2 Mbps nominal / 5 Mbps data, accept-all (no per-ID filters).

## Dependencies (declared, not vendored)

C2000Ware is large and licensed by TI; it is **not** vendored here. Install it and point
your CCS project at its `driverlib`:

| Dependency | Version floor | Where to get it |
|---|---|---|
| Code Composer Studio | 12.x+ | https://www.ti.com/tool/CCSTUDIO |
| C2000Ware (driverlib + device support) | 5.x+ | https://www.ti.com/tool/C2000WARE |

The firmware includes only `driverlib.h`, `device.h`, and `<string.h>` — everything else
is C2000Ware's MCAN/SCI/CPUTimer driverlib.

## Build

1. CCS → *New CCS Project* for `TMS320F280039C`, or import C2000Ware's empty
   `driverlib` MCAN example and replace its `main` with `atelier_can_logger.c`.
2. Add the C2000Ware `driverlib` source/include paths and the device support files.
3. Build, then flash/debug over the on-board XDS110.

## Board-init checklist (the `Board_init()` you must supply)

The `main()` calls `Device_init()`, then expects board bring-up before `mcan_init()`.
Wire these (C2000Ware SysConfig generates most of it):

- **MCANA** clock = **80 MHz** functional clock (the bit-timing constants assume this —
  see [`../docs/BIT_TIMING.md`](../docs/BIT_TIMING.md)), pins muxed to the LaunchPad CAN header.
- **SCIA** on the XDS110 virtual COM at **3 Mbaud**, non-FIFO (the host default `--baud 3000000`).
- **CPUTIMER0** free-running at 1 µs resolution for `now_us()` timestamps.

## Known TODOs (carried from the design capsule)

These are deliberate, documented gaps — not oversights. Tracked in [`../docs/ROADMAP.md`](../docs/ROADMAP.md):

- **SD/FatFS batch writer** — `send_frame()` has the hook; wire a block-aligned FatFS
  `f_write()` for on-board logging.
- **TDC/SSP** — enable transmitter-delay compensation in `mcan_init()` (required at 8 Mbps,
  good practice above ~2 Mbps). The driverlib call is stubbed in a comment; confirm the exact API.
- **Hardware bit-timing verification** — confirm 80 MHz MCAN clock and the NOM 31/8/8,
  DATA 12/3/3 constants against a known-good node before a real capture.
- **On-board transceiver part** — confirm the 5 Mbps-class part from the LaunchPad schematic.

## Enabling the UDS tester — the SCI→CAN bridge (small addition)

The committed logger core is **RX-only**. To drive the active UDS tester
(`host/uds_tester.py`) on a real bus, add a host→board transmit path so the board becomes a
bidirectional CAN↔serial bridge. This is a deliberately small, documented addition (the
firmware never parses UDS — all ISO-TP/UDS logic stays on the host):

1. **Parse `TX_FRAME` (type 2) from SCI.** Run the same `AA 55 | type | len | payload | crc8`
   framing in reverse on the SCIA RX path. Payload: `id(u32) flags(u8) dlen(u8) data[dlen]`
   (see [`../docs/WIRE_PROTOCOL.md`](../docs/WIRE_PROTOCOL.md)).
2. **Transmit it.** Fill an `MCAN_TxBufElement` (`id`, `xtd`/`fdf`/`brs` from `flags`, `dlc`
   from the length→DLC table), `MCAN_writeMsgRam(MCANA_BASE, MCAN_MEM_TYPE_BUF, idx, &tx)`,
   then `MCAN_txBufAddReq(MCANA_BASE, idx)`.
3. **Poll SCI RX in the main loop**, alongside the existing ring drain — keep it non-blocking
   so the RX/logging path is never stalled.

Responses arrive on the bus and flow back out unchanged through the existing type-0 FRAME
stream, which the tester reads. No other change to the capture pipeline is needed.

## How it stays up under load

- Hardware **RX FIFO0** (64 deep) absorbs the inbound burst before software runs.
- A power-of-two **lock-free ring** (single ISR producer, single main consumer) decouples
  capture from the slower SCI/SD drain.
- Ring-full **drops and counts** (`drop_count`) — it never blocks the bus or stalls the ISR.
- Periodic **STATUS** reports bus-load %, error state, TEC/REC, rx and drop counts; the
  main loop auto-recovers from bus-off.
