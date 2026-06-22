# UDS tester

`host/uds_tester.py` is the active diagnostic side of this board — a UDS (ISO 14229-1)
client over ISO-TP (ISO 15765-2). Where the logger passively captures the bus, the tester
*sends* requests and decodes responses: sessions, DID read/write, DTC read/clear, routine
control, ECU reset, security access, and tester-present.

It targets the MM Mill's classic-CAN (500 kbit/s, 8-byte) ECUs by default; `--fd` switches
to CAN-FD single-frames (up to 62 bytes).

---

## Architecture — host does the protocol, firmware is a wire

```
  uds_tester.py                                  board (bridge)            bus
  ┌───────────────────────────────┐    serial    ┌───────────────┐
  │ UDS client                    │   TX_FRAME    │ SCI -> MCAN TX │──► request
  │   └ ISO-TP (SF/FF/CF/FC)      │ ───────────►  │               │
  │       └ Link (serial | sim)   │   FRAME       │ MCAN RX -> SCI │◄── response
  └───────────────────────────────┘   ◄────────   └───────────────┘
```

Every byte of ISO-TP and UDS lives on the host. The firmware only bridges CAN↔serial
(type-2 `TX_FRAME` in / type-0 `FRAME` out — see [`WIRE_PROTOCOL.md`](WIRE_PROTOCOL.md)),
so the same board image serves the logger and the tester, and the diagnostic logic is easy
to iterate (and Rust-portable later). The `--demo` mode swaps the serial link for an
in-process **simulated ECU** that speaks real ISO-TP back — the full pipeline proves out
with no board.

---

## MM Mill address book (`--ecu`)

| ECU | Request ID | Response ID | Role |
|---|---|---|---|
| BMS | 0x7B0 | 0x7B8 | energy authority |
| DTU | 0x7A0 | 0x7A8 | motor control |
| BCU | 0x7C0 | 0x7C8 | vehicle SM / NM coordinator |
| OBC | 0x7E0 | 0x7E8 | charger |
| *(functional broadcast)* | 0x7DF | — | — |

*(Source: platform CLAUDE.md §2.2.)*

---

## Services

| SID | Service | Helper |
|---|---|---|
| 0x10 | DiagnosticSessionControl | `--session N` |
| 0x11 | ECUReset | `--reset N` |
| 0x14 | ClearDiagnosticInformation | `--clear-dtc` |
| 0x19 | ReadDTCInformation (subfn 0x02) | `--read-dtc` |
| 0x22 | ReadDataByIdentifier | `--read DID` |
| 0x27 | SecurityAccess (seed/key) | `--security LEVEL` |
| 0x2E | WriteDataByIdentifier | `--write DID --data HEX` |
| 0x31 | RoutineControl | `--routine start\|stop\|result --rid RID` |
| 0x3E | TesterPresent | `--tester-present` |

`--identify` is shorthand for reading VIN (0xF190), ECU serial (0xF18C), and SW version
(0xF195). With no command, the tester runs `--identify`.

### Known DIDs

| DID | Name | DID | Name |
|---|---|---|---|
| 0xF190 | VIN | 0xF332 | Battery Cell Variant (BMS) |
| 0xF18C | ECU Serial Number | 0xF333 | Battery Pack Variant (BMS) |
| 0xF195 | SW Version | 0xF390 | Security Event Log *(gated)* |
| 0xF187 | Spare Part Number | 0xF3E0 | Session Nonce *(gated)* |

---

## Negative-response codes decoded

The client raises a readable error on any `0x7F` negative response and transparently waits
out `0x78` (responsePending) up to the P2\* timeout. Decoded NRCs include `0x11`
serviceNotSupported, `0x13` incorrectMessageLength, `0x22` conditionsNotCorrect, `0x31`
requestOutOfRange, `0x33` securityAccessDenied, `0x35` invalidKey, `0x36`
exceedNumberOfAttempts, `0x7E/0x7F` not-supported-in-active-session — full table in the
source `NRC` map.

---

## Usage

```bash
# No hardware — against the built-in simulated ECU:
python uds_tester.py --demo --ecu BMS                       # identify (default)
python uds_tester.py --demo --ecu OBC --read 0xF190         # read VIN (multi-frame ISO-TP)
python uds_tester.py --demo --ecu DTU --read-dtc --clear-dtc
python uds_tester.py --demo --ecu BCU --session 0x03 --write 0xF195 --data 020100 --read 0xF195
python uds_tester.py --demo --ecu BMS --security 0x01 --read 0xF3E0   # unlock + gated read

# Real board (needs the firmware SCI->CAN bridge):
python uds_tester.py --port COM5 --ecu OBC --session 0x03 --read 0xF190
```

### Security access (demo algorithm only)

The `--demo` ECU uses a **toy** seed→key transform (`key = seed XOR 0xA5`) purely to
exercise the 0x27 seed/request/send-key handshake end-to-end. A real ECU's key algorithm
is supplied by the OEM; replace `demo_key()` (or pass your own key function) for live use.
**Do not** mistake the demo transform for a security mechanism.

---

## Conformance notes & limits

- **ISO-TP:** classic single/first/consecutive/flow-control with block-size and STmin
  honored. Multi-frame is exercised by the VIN read in `--demo`. FD mode currently uses FD
  framing for single-frames; multi-frame segmentation stays 8-byte (valid, just not maximally
  packed).
- **Timing:** the host-over-serial path adds link latency, fine for a diagnostic tester
  (the ECU's N_Bs/N_Cr budgets are generous) but **not** a substitute for an ECU-resident
  stack in timing-critical conformance testing.
- **Functional addressing / 0x85 ControlDTCSetting / 0x28 CommunicationControl** are not yet
  wired — see [`ROADMAP.md`](ROADMAP.md).
