# tools/ — CAN-FD bit-timing solver

`canfd_bittiming.py` computes the MCAN register fields — BRP, TSEG1, TSEG2, SJW — for a
target bit rate and sample point at a given MCAN functional clock. Use it to re-derive
the firmware's timing constants for a different clock or data rate.

## Run

```bash
python canfd_bittiming.py
```

It prints clean solutions for MCAN clocks of **40 / 80 / 120 MHz**, for the nominal
(arbitration) and data phases. To solve a different point, edit the `solve(...)` calls at
the bottom — the signature is:

```python
solve(fcan, bitrate, sample_point, brp_max, tseg1_max, tseg2_max, sjw_max)
```

## What ships in the firmware

The committed firmware targets an **80 MHz** MCAN clock, 2 Mbps nominal / 5 Mbps data:

| Phase | Bit rate | TSEG1 | TSEG2 | SJW | Sample point |
|---|---|---|---|---|---|
| Nominal | 2 Mbps | 31 | 8 | 8 | 80.0 % |
| Data | 5 Mbps | 12 | 3 | 3 | 81.2 % |

> **Note:** as shipped, the script's *data*-phase call solves for **8 Mbps** (the Tier-B
> SIC-transceiver target), while the firmware runs **5 Mbps** on the LaunchPad's on-board
> transceiver. Both, plus the 40/120 MHz alternates, are tabulated in
> [`../docs/BIT_TIMING.md`](../docs/BIT_TIMING.md). Edit the data-phase `bitrate` argument
> to `5e6` to reproduce the shipped data-phase numbers.

Always enable **TDC/SSP** in the data phase above ~2 Mbps.
