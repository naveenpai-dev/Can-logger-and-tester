# Wire protocol — firmware ↔ host

One flat framing carries every record from the board to the laptop over USB-serial. It is
shared **byte-for-byte** between `firmware/atelier_can_logger.c` and `host/can_logger_host.py`
— if you change one side, change both.

All multi-byte fields are **little-endian**.

## Packet envelope

```
┌──────┬──────┬──────┬──────┬───────────────┬──────┐
│ AA   │ 55   │ type │ len  │ payload[len]  │ crc8 │
└──────┴──────┴──────┴──────┴───────────────┴──────┘
  sync0  sync1  1 B    1 B     len bytes       1 B
```

- **Sync:** `0xAA 0x55` marks a packet start. The host hunts for it and resyncs on garbage.
- **type:** `0` = FRAME (board→host), `1` = STATUS (board→host), `2` = TX_FRAME (host→board).
- **len:** payload length in bytes.
- **crc8:** CRC-8 (polynomial `0x07`, init `0x00`) computed over `[type, len, payload]` —
  i.e. everything between the sync and the CRC. A bad CRC makes the host skip past the sync
  and keep parsing.

## type 0 — FRAME (one captured CAN frame)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `ts_us` | u64 | microsecond timestamp (free-running CPUTIMER0) |
| 8 | `id` | u32 | CAN ID (11-bit standard or 29-bit extended) |
| 12 | `flags` | u8 | bitfield, see below |
| 13 | `dlen` | u8 | payload byte length (0..64, from the CAN-FD DLC table) |
| 14 | `chan` | u8 | channel index (0 on a single-MCAN build) |
| 15 | `data` | u8 × `dlen` | frame payload |

`flags` bits: `b0` FD · `b1` BRS (bit-rate switch) · `b2` ESI · `b3` IDE (extended ID).

## type 1 — STATUS (emitted ~once per second)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `ts_us` | u64 | microsecond timestamp |
| 8 | `busload` | u8 | estimated bus load %, 0..100 |
| 9 | `errstate` | u8 | `0` active · `1` passive · `2` bus-off |
| 10 | `tec` | u8 | transmit error counter |
| 11 | `rec` | u8 | receive error counter |
| 12 | `rx` | u32 | total frames received since boot |
| 16 | `err` | u32 | total drops (ring-full) since boot |

## type 2 — TX_FRAME (host → board, one CAN frame to transmit)

The active direction, used by the UDS tester (`host/uds_tester.py`). The host hands the
board a CAN frame; the firmware bridge transmits it on the bus. Responses return through
the normal type-0 FRAME stream. The board firmware needs the small SCI→CAN bridge for
this (the logger core is RX-only) — see `firmware/README.md`.

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `id` | u32 | CAN ID to transmit |
| 4 | `flags` | u8 | `b0` FD · `b1` BRS · `b3` IDE (extended) — same bitfield as FRAME |
| 5 | `dlen` | u8 | payload byte length (0..64) |
| 6 | `data` | u8 × `dlen` | frame payload |

The board is thus a transparent bidirectional CAN↔serial bridge: one firmware serves both
the passive logger and the active tester. All ISO-TP segmentation/reassembly and UDS logic
live on the host, where they are easy to iterate (and Rust-portable) — the firmware never
parses UDS.

## CAN-FD DLC → byte length

The firmware maps the 4-bit DLC to the FD length with this table (`DLC2LEN`):

```
DLC : 0 1 2 3 4 5 6 7  8  9 10 11 12 13 14 15
len : 0 1 2 3 4 5 6 7  8 12 16 20 24 32 48 64
```

## Integrity & flow notes

- The ring buffer drops (and counts in `err`/`drop_count`) rather than ever blocking the
  bus — a rising `err` in STATUS means the host/SD drain isn't keeping up, not bus loss.
- CRC-8 is a transport-integrity check over the serial link, independent of the CAN bus's
  own CRC.
