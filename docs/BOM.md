# Bill of materials & cost tiers

Two builds, chosen by the data rate you need.

## Tier A — on-board transceiver (this snapshot) · ~$90–110

| Item | Part | Note |
|---|---|---|
| LaunchPad | **LAUNCHXL-F280039C** | MCAN + on-board CAN-FD transceiver, XDS110 debug/COM |
| Storage | microSD breakout + card | on-board logging path (SD/FatFS writer is roadmap) |
| Cabling | DB9 / OBD-II to CAN header, USB | bus tap + host link |

Good to **5 Mbps** data — the on-board transceiver class. No external SIC needed. This is
the build the committed firmware and bit-timing constants target.

## Tier B — SIC transceiver + Ethernet (roadmap) · ~$330–390

| Item | Part | Note |
|---|---|---|
| MCU board | **F2838x** | higher headroom + Ethernet MAC |
| Transceiver | **TCAN1462** SIC | required for 8 Mbps data |
| Link | Ethernet PHY/magnetics | stream off-board faster than 3 Mbaud SCI |

Choose Tier B only when you need **8 Mbps** data or wire-speed off-board streaming. The
bit-timing reference tabulates the 8 Mbps constants; the firmware would need the SIC
transceiver init and the TDC/SSP enable (see [`BIT_TIMING.md`](BIT_TIMING.md)).

## Decision rule

```
data rate ≤ 5 Mbps   → Tier A  (LAUNCHXL-F280039C, on-board transceiver)
data rate = 8 Mbps   → Tier B  (F2838x + TCAN1462 SIC + Ethernet)
```
