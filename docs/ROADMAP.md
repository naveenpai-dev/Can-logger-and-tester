# Roadmap

Status of the atelier, and where contributions land. `[x]` shipped · `[~]` scaffolded (review-grade,
not yet compiled) · `[ ]` open.

## v0.1 — scaffold (current)

- [x] Repository structure (Material-OS layering) + collaboration model
- [x] Premium docs: README, CONTRIBUTING, LICENSE, DEPENDENCY_LEDGER
- [x] Per-atelier config (`Platform_CanLogger_Cfg.h`, `Platform_OS_Cfg.h`)
- [x] CAN-FD BSP interface (`Bsp_CanFd_inf.h`) — ISR→app capture seam
- [x] App spine: `CanLogger_Mcan` (ISR→queue), `CanLogger_HostStream` (framed SCI),
      `CanLoggerOs_Init` / `CanLoggerOs_Tasks`, `main.c`, `FreeRTOSConfig.h`
- [x] Host tools: `can_logger_host.py`, `canfd_bittiming.py`
- [~] MCAL: `Mcal_C2000_Mcan` (+ clock/gpio/sci/timer) — interface defined, bodies next
- [~] `platform-app-stubs/` (Comms / Health / Types) — trimmed set
- [~] `bsp-stubs/CanLogger_C2000_BspStub.c` — bsp-only self-contained gate
- [ ] `build/` — makefile (RAM/FLASH + bsp-only), F280039C linker `.cmd`, `build.sh`, `verify_env.sh`
- [ ] Vendor: pin/vendor FreeRTOS-Kernel + C2000Ware driverlib (see DEPENDENCY_LEDGER.md)

## v0.2 — runnable on bench

- [ ] **SD/FatFS block-aligned batch writer** (`CanLogger_Sd`) — the onboard logging path *(good first issue)*
- [ ] **TDC/SSP enable** in `Mcal_C2000_Mcan` (required above ~2 Mbps) *(good first issue)*
- [ ] Verify bit timing on hardware (MCAN functional clock = 80 MHz; NOM 31/8/8, DATA 12/3/3)
- [ ] Confirm on-board transceiver part from the LaunchPad schematic (5 Mbps class)
- [ ] Bench capture proof (frames + STATUS on a host, drop_count = 0 under load)

## Backlog

- [ ] Tier-B path: F2838x + TCAN1462 SIC + Ethernet for 8 Mbps
- [ ] Port the host tool to **Rust + egui** (host is Rust-capable; the C28x firmware is not)
- [ ] Doxygen site under `docs/generated/`
