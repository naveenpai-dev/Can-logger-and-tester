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
