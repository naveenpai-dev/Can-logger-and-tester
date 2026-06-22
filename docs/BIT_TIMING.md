# CAN-FD bit-timing reference

The MCAN bit-timing registers depend on the **functional clock** feeding the MCAN module.
The shipped firmware assumes an **80 MHz** MCAN clock; set yours to match, or re-solve with
`tools/canfd_bittiming.py`.

All values below are `BRP=1`, 0 % bit-rate error.

## 80 MHz MCAN clock (shipped)

| Phase | Bit rate | TSEG1 | TSEG2 | SJW | Sample point |
|---|---|---|---|---|---|
| Nominal | 2 Mbps | 31 | 8 | 8 | 80.0 % |
| Data | 5 Mbps | 12 | 3 | 3 | 81.2 % |
| Data (Tier B) | 8 Mbps | 7 | 2 | 2 | — |

5 Mbps is within the LaunchPad's on-board CAN-FD transceiver. **8 Mbps requires the
TCAN1462 SIC transceiver** (Tier-B build) — see [`BOM.md`](BOM.md).

## Clean alternates at other clocks

| MCAN clock | Nominal 2 Mbps | Data 5 Mbps |
|---|---|---|
| 40 MHz | 15 / 4 / 4 | 5 / 2 / 2 |
| 80 MHz | 31 / 8 / 8 | 12 / 3 / 3 |
| 120 MHz | 47 / 12 / 12 | 18 / 5 / 5 |

*(TSEG1 / TSEG2 / SJW. Register fields are programmed as value − 1; the firmware applies
the −1 in `mcan_init()`.)*

## TDC / SSP

Enable **transmitter delay compensation** and a **secondary sample point** in the data
phase above ~2 Mbps. It is **required at 8 Mbps** to tolerate transceiver loop delay. The
hook is stubbed in `mcan_init()` — set the SSP offset near the data sample point and
confirm the exact driverlib API for your C2000Ware version.

## Re-solving

```bash
python ../tools/canfd_bittiming.py
```

Prints solutions for 40 / 80 / 120 MHz. Edit the `solve(...)` bit-rate / sample-point
arguments to target a different operating point.
