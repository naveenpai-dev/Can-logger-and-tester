# Contributing to the CAN-FD Logger Atelier

This project **defaults to a shared-repository (inner-source) model**: you are added as a
collaborator and contribute **directly into this repository's `main`** through a reviewed pull
request. There is one spine — `main` — and every change lands on it through the same gate.

**Forking is permitted** if you prefer to work in your own namespace — but a fork is not required,
and either way the pull request targets **this repo's `main`** and a maintainer reviews and merges.

## Why the shared repo is the default

A fork lives in *your* namespace and the maintainer can't see it until you open a PR. Working as a
collaborator in this repo keeps every branch in one place, makes work visible early, and keeps a
clean merge history. One spine; tributaries optional.

## The model in one picture

```
  you (collaborator)                     maintainer
  ─────────────────                     ───────────
  git switch -c feat/<topic>   ──push──▶  branch visible in THIS repo
  …commits…                                       │
  open PR  feat/<topic> ──▶ main  ───────▶  review (required)
                                                  │
                                          approve + merge  ──▶ main
  (no self-merge — main is protected)
```

## Ground rules

1. **You have write access as a collaborator.** Cloning this repo directly is the default path;
   forking is allowed if you prefer it.
2. **Never push to `main`.** It is protected: direct pushes are rejected. Always branch.
3. **One branch per topic**, named `feat/<topic>`, `fix/<topic>`, or `docs/<topic>`.
4. **Open a PR into `main`.** A maintainer review is **required** before merge; **no self-merge.**
5. **Keep the build honest.** If you touch firmware, state in the PR whether it was built
   (TI CGT C2000) or is review-only — never imply a green build you didn't run.
6. **Match the house style.** Premium Doxygen file header on every `.c/.h` (see any existing
   module), MISRA-C:2012-aligned C, ISR callbacks use only `xTaskNotifyFromISR` /
   `xQueueSendFromISR`. The MCAL layer includes **no** FreeRTOS headers.

## Step by step

```bash
# one-time (collaborator clone — the default)
git clone https://github.com/naveenpai-dev/can-logger.git
cd can-logger

# per change
git switch -c feat/sd-fatfs-writer
# …edit, commit…
git push -u origin feat/sd-fatfs-writer
# then open a Pull Request: base = main, compare = feat/sd-fatfs-writer
```

## What a good PR looks like

- **Scope:** one topic. A logger feature, a bug fix, a doc — not three at once.
- **Build statement:** "built RAM+FLASH with CGT C2000 25.x, no unresolved" **or**
  "review-only, not compiled here" — be explicit (a wrapper exit code is not a build proof;
  gate on the `.out` + a clean `.map`).
- **Hardware note:** if it changes on-target behaviour, say what you flashed and observed.
- **No secrets, no vendor dumps.** Vendored dependencies are governed by `DEPENDENCY_LEDGER.md`.

## Branch protection (maintainer-configured)

`main` enforces: PR required · ≥ 1 maintainer approval · no direct pushes · no self-merge ·
linear history. These are the GitHub-side teeth behind the rules above.

## Open tasks (good first contributions)

See `docs/ROADMAP.md`. Lead candidates: the **SD/FatFS block-aligned batch writer**, **TDC/SSP
enable** in `mcan_init`, and porting the host tool to **Rust + egui**.
