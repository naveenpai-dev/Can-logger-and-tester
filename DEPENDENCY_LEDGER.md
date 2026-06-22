# Dependency Ledger

> **Rule:** every compile input is either **vendored in-repo** or **declared here** with an exact
> version + fetch path — no third state. A fresh clone plus the fetches below is buildable with no
> reach-outs beyond those named. This is what keeps the atelier self-contained for a receiver.

## Vendored in-repo (committed under `vendor/`)

| Dependency | Path | Version / pin | Notes |
|---|---|---|---|
| C2000Ware driverlib (F28003x subset) | `vendor/c2000-driverlib/` | C2000Ware 5.x — `device/`, `driverlib/` MCAN+sysctl+gpio+sci+cputimer | Trimmed to the modules this atelier links. |

## Declared (fetched at setup — pinned, one command)

| Dependency | Fetch | Pin | Why not vendored yet |
|---|---|---|---|
| FreeRTOS-Kernel | `git clone https://github.com/FreeRTOS/FreeRTOS-Kernel vendor/FreeRTOS-Kernel` | tag `V11.1.0` | Large; pinned + vendor-on-first-build. The C28x port lives under `portable/`. |
| TI C2000 CGT (compiler) | TI: Code Composer Studio ≥ 12.1 → `ti-cgt-c2000` | ≥ 22.6.0 (CCS 12.1 floor) | Toolchain, not source — see `build/verify_env.sh`. |

### UDS client transport — decision

The UDS client (`CanLogger_UdsClient.*`) needs an ISO 15765-2 (ISO-TP) transport. Two options were
weighed:

| Option | Verdict |
|---|---|
| Vendor `isotp-c` (lishen2/isotp-c) and adapt its `isotp_user_*` HAL | **Not chosen now.** The library models a full bidirectional link (two `IsoTpLink`s + an internal poll loop) heavier than the tester-only role needs, and the platform's reference `isotp-c/` is presently an **empty submodule** — vendoring it is a separate, larger task. |
| **Minimal in-tree shim** (`CanLogger_IsoTp.*`) implementing exactly the tester SF/FF/CF/FC framing | **Chosen.** ~250 lines, no submodule, no third state in this ledger, and a clean swap-out point if a fuller stack is later wanted. Scope limits are stated in the file header (classic 8-byte framing; CAN-FD long-frame escape not implemented). |

If `isotp-c` is later vendored to replace the shim, declare it here exactly like FreeRTOS-Kernel:

| Dependency | Fetch | Pin | Why not vendored yet |
|---|---|---|---|
| isotp-c (optional — alternative to the in-tree shim) | `git clone https://github.com/lishen2/isotp-c vendor/isotp-c` | commit-pinned at vendoring time | The in-tree `CanLogger_IsoTp.*` shim covers the current tester role; isotp-c is only needed for a full bidirectional ISO-TP link. |

## Toolchain floor

- **Compiler:** TI CGT C2000 ≥ **22.6.0** (CCS 12.1) — newer 25.x also fine (EABI).
- **RTS library:** `rts2800_fpu32_eabi.lib` (FPU32; **not** the `_ml_` variant).
- **Make:** GNU Make (CCS ships `gmake`).
- Override the compiler path with `CL_C2000=<path-to-ti-cgt-c2000>` — no machine-specific path is
  baked into the makefile.

## How to vendor a declared dependency (closing the last gap)

```bash
git clone --depth 1 --branch V11.1.0 https://github.com/FreeRTOS/FreeRTOS-Kernel vendor/FreeRTOS-Kernel
rm -rf vendor/FreeRTOS-Kernel/.git           # commit as plain files, not a submodule
git add vendor/FreeRTOS-Kernel && git commit -m "vendor: FreeRTOS-Kernel V11.1.0 (de-submoduled)"
```

*A submodule is absent from a fresh clone until `git submodule update` — which a receiver may not
run. Vendoring as plain files is the precedent this platform follows for exactly that reason.*
