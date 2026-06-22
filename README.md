# CAN-FD Logger Atelier — Material-OS

> A self-contained **Material-OS atelier**: a TI C2000 LaunchPad (LAUNCHXL-**F280039C**)
> turned into an automotive-grade **CAN-FD line logger** — accept-all capture, framed host
> streaming, microSD batch recording — built on the chip-agnostic Material-OS layered platform
> (BSP / MCAL / Config / OS / app-stubs) over FreeRTOS.

[![build](docs/badge_build.svg)](docs/) · **Target:** LAUNCHXL-F280039C · **Bus:** CAN-FD 2 Mbps nominal / 5 Mbps data · **License:** MIT

---

## Why this exists

A bench CAN-FD logger that you can **flash, trust, and extend** — and a clean, miniature
demonstration of the **Material-OS** architecture an application team builds on. The capture
core is fail-closed by construction: ISO 11898 fault confinement (bus-off auto-recovery), an
explicit `drop_count` so you *know* the instant a frame is lost, CRC-framed output, and
bit-timing proven by the solver at **0 % error** before a single frame is trusted.

It is also a teaching atelier. Every layer below is the same shape the production Material-OS
ateliers use, so reading this repo top to bottom teaches the platform: where the silicon ends
and the portable platform begins, how an ISR hands off to a task, and how one app builds across
chip families without `#ifdef`.

## The pipeline (built for ≥ 92 % bus load)

```
   CAN-FD bus ──▶ MCAN RX FIFO0 ──ISR──▶ lock-free ring (RAM) ──task──▶ framed SCI stream
                                                                  └────▶ microSD (FatFS, batched)
```

- **Accept-all** — no per-ID filters; non-matching frames routed to RX FIFO0.
- **ISR never blocks the bus** — ring-full increments `drop_count`, never stalls.
- **Periodic STATUS** — bus-load %, error state, TEC/REC, rx/drop counters.
- **Bus-off auto-recovery** — leaves bus-off on the status cadence (ISO 11898 fault confinement).

## Wire format (matches `host/can_logger_host.py` exactly)

```
AA 55 | type | len | payload | crc8           crc8 over [type,len,payload], poly 0x07
  FRAME (0):  ts_us(u64) id(u32) flags(u8) dlen(u8) chan(u8) data[dlen]
  STATUS(1):  ts_us(u64) busload(u8) errstate(u8) tec(u8) rec(u8) rx(u32) drops(u32)
```

## Repository layout (the Material-OS layering)

```
canlogger-c2000-platform/CanLogger_C2000_App/   the Weave — app layer (FreeRTOS tasks)
platform-app-stubs/        core-agnostic platform interface (Comms / Health / Types)
platform-os/freertos/      OS abstraction shim
platform-bsp/              Bsp_*_inf.h hooks + bsp-stubs/  (Weaver's hands)
platform-mcal/mcal-c2000/  Mcal_C2000_Mcan + clock/gpio/sci/timer  (Loom equipment)
platform-config/canlogger/ per-atelier knobs (channels, bit timing, task roster)
build/                     makefile (RAM/FLASH + bsp-only) · build.sh · verify_env · linker .cmd
vendor/                    FreeRTOS-Kernel + c2000ware-driverlib (see DEPENDENCY_LEDGER.md)
host/                      can_logger_host.py · canfd_bittiming.py
docs/                      Doxygen + programmer guide
```

## Quick start

```bash
# 1. host tool — works with NO hardware (replays a synthetic stream)
python3 host/can_logger_host.py --demo

# 2. recompute bit timing for your MCAN functional clock
python3 host/canfd_bittiming.py --clock 80e6 --nominal 2e6 --data 5e6

# 3. firmware — see build/README.md (needs TI CGT C2000 + C2000Ware; verify_env first)
cd build && ./verify_env.sh && make
```

## Hardware & cost

| Tier | Parts | Data rate | ~Cost |
|---|---|---|---|
| **A** (this repo) | F280039C LaunchPad + microSD + cabling | 5 Mbps (on-board transceiver) | $90–110 |
| B (if 8 Mbps needed) | F2838x + TCAN1462 SIC + Ethernet | 8 Mbps | $330–390 |

At 8 Mbps the on-board transceiver is the limiter — **this atelier targets 5 Mbps honestly.**
Tier B is a documented upgrade path, not a claim of this board.

## Contributing

This repo defaults to a **shared-repo (contribute-to-main) model** — you are added as a
collaborator, branch *in this repo*, open a PR into the protected `main`, and a maintainer
reviews and merges. **Forking is permitted but not required.** Full rules:
**[CONTRIBUTING.md](CONTRIBUTING.md)**.

## License

MIT — see [LICENSE](LICENSE). © 2026 Naveen Pai.
