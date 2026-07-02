<!--
  SPDX-License-Identifier: MIT
  Copyright (c) 2026 Naveen Pai
-->
# UDS Client — turning the CAN-FD Logger into a tester

The logger captures every frame on the bus. The **UDS client** adds an *active* role on top of that
same capture path: it issues ISO 14229-1 diagnostic requests and parses the ECU's responses, while
the logger keeps monitoring. One atelier, two roles — passive monitor and active tester — sharing
one CAN-FD seam.

This is an **optional** feature, gated by `CANLOGGER_UDS_ENABLE` in
`platform-config/canlogger/Platform_CanLogger_Cfg.h`. With it off, the build is the plain logger.

## What it does

| Service | SID | ISO 14229-1 clause | Helper |
|---|---|---|---|
| DiagnosticSessionControl | `$10` | §9.2 | `CanLogger_UdsClient_SubmitSession(session)` |
| TesterPresent | `$3E` | §9.5 | `CanLogger_UdsClient_SubmitTesterPresent(suppress)` (also periodic) |
| ReadDataByIdentifier | `$22` | §10.2 | `CanLogger_UdsClient_SubmitReadDid(did)` |
| ReadDTCInformation | `$19` | §11.3 | `CanLogger_UdsClient_SubmitReadDtc(status_mask)` |
| ClearDiagnosticInformation | `$14` | §11.2 | `CanLogger_UdsClient_SubmitClearDtc(group)` |

It handles the two response paths every real tester must:
- **Negative response** `$7F <SID> <NRC>` (ISO 14229-1 §7.5) — surfaced as
  `CANLOGGER_UDS_NEGATIVE` with the NRC.
- **Response pending** NRC `0x78` (§A.1 / §7.5) — the client extends its wait from **P2** to
  **P2\*** and retries up to `CANLOGGER_UDS_MAX_PENDING` times before declaring a timeout.

## Architecture — how it sits on the logger

```
   MCAN RX ISR ──post──▶ capture queue ──▶ CanLogger_Drain_Task
                                              │  (logs every frame to the host)
                                              ├──▶ CanLogger_UdsClient_OnCapturedFrame()
                                              │      if id == RSP_ID: copy + xTaskNotify
                                              ▼
   app/CLI ──Submit*()──▶ request queue ──▶ CanLogger_UdsClient_Task
                                              │  build PDU → ISO-TP TX → wait P2/P2* → parse
                                              ▼
                                          sink callback ──▶ CanLogger_UdsRsp_t
```

- **RX is ISR-safe by construction.** The MCAN ISR already posts every frame to the capture queue
  (the logger's job). The drain task — which already touches every frame — hands each to
  `OnCapturedFrame`, a cheap id compare plus one `xTaskNotify`. No UDS parsing in interrupt context
  (the house rule in `CONTRIBUTING.md`).
- **The task owns all timing and retry.** P2 client, P2\* after a `0x78`, the pending-retry bound,
  and the periodic TesterPresent keep-alive all live in `CanLogger_UdsClient_Task`. No blocking
  calls anywhere off the task.
- **Half-duplex.** One transaction is in flight at a time (ISO 14229-2 client model); requests are
  serialized off the request queue.

## Transport — ISO-TP (ISO 15765-2)

A **minimal in-tree shim** (`CanLogger_IsoTp.*`) implements exactly the tester-side framing:

| Frame | PCI (high nibble of B0) | Clause | Use |
|---|---|---|---|
| Single Frame (SF) | `0x0` | §6.5.2.1 | request/response ≤ 7 bytes |
| First Frame (FF) | `0x1` | §6.5.2.2 | start of a multi-frame transfer (12-bit length) |
| Consecutive Frame (CF) | `0x2` | §6.5.2.3 | body, 4-bit sequence number 1..15 wrapping to 0 |
| Flow Control (FC) | `0x3` | §6.5.2.4 | CTS / WAIT / OVFLW, block size + STmin |

Most diagnostic requests fit a Single Frame. A long request (multi-DID `$22`, routine arguments)
emits a First Frame, waits for the ECU's Flow Control, then sends Consecutive Frames. A long
*response* triggers the reverse: the client receives the First Frame, transmits a Flow Control
**CTS**, and reassembles the Consecutive Frames into the response buffer.

Why a shim and not the vendored `isotp-c`: see `DEPENDENCY_LEDGER.md` (the reference `isotp-c/` is an
empty submodule, and the bidirectional library is heavier than the tester role needs). The shim is a
clean swap-out point.

### Wire framing (classic 8-byte, request id `0x7A0`, response id `0x7A8` by default)

```
ReadDataByIdentifier $22 F1 90  (Single Frame request)
  0x7A0: 03 22 F1 90 AA AA AA AA          B0=0x03 → SF, length 3 ; AA = ISO-TP padding

Positive response (Single Frame, e.g. a 4-byte DID value)
  0x7A8: 07 62 F1 90 01 02 03 04          B0=0x07 → SF, length 7 ; 62 = 0x22+0x40

Negative response (response pending then final)
  0x7A8: 03 7F 22 78                      $7F, SID 0x22, NRC 0x78 → client waits P2*
  0x7A8: 07 62 F1 90 01 02 03 04          final positive response
```

## Configuration knobs (`Platform_CanLogger_Cfg.h`)

| Macro | Meaning | Default |
|---|---|---|
| `CANLOGGER_UDS_ENABLE` | compile the tester role | `1` |
| `CANLOGGER_UDS_REQ_ID` / `_RSP_ID` | physical request / response ids | `0x7A0` / `0x7A8` |
| `CANLOGGER_UDS_FUNC_ID` | functional broadcast id (TesterPresent) | `0x7DF` |
| `CANLOGGER_UDS_P2_CLIENT_MS` | P2 client timeout | `50` |
| `CANLOGGER_UDS_P2_STAR_MS` | P2\* (after `0x78`) | `5000` |
| `CANLOGGER_UDS_MAX_PENDING` | max consecutive `0x78` before timeout | `8` |
| `CANLOGGER_UDS_TP_PERIOD_MS` | TesterPresent keep-alive cadence | `2000` |
| `CANLOGGER_UDS_TX_DL` | request frame data length (8 classic / ≤64 FD) | `8` |
| `CANLOGGER_UDS_FC_BS` / `_FC_STMIN_MS` | flow control offered on RX | `0` / `0` |
| `CANLOGGER_UDS_RX_BUF_BYTES` / `_TX_BUF_BYTES` | reassembly / build buffers | `256` / `128` |

Retarget the ids per ECU: BMS `0x7B0/0x7B8`, DTU `0x7A0/0x7A8`, BCU `0x7C0/0x7C8`,
OBC `0x7E0/0x7E8`.

## Worked example — start a session, read a DID, keep-alive

```c
#include "CanLogger_UdsClient.h"

/* 1. Register a sink to receive parsed responses (called from the UDS task). */
static void OnUdsResponse(const CanLogger_UdsRsp_t *rsp)
{
    switch (rsp->result) {
        case CANLOGGER_UDS_OK:
            /* rsp->data[0] is the positive-response SID (req SID + 0x40);
             * for $22 the DID echo + value follow. Forward to the host stream. */
            break;
        case CANLOGGER_UDS_NEGATIVE:
            /* rsp->nrc holds the NRC (e.g. 0x31 requestOutOfRange). */
            break;
        case CANLOGGER_UDS_TIMEOUT:
        case CANLOGGER_UDS_TP_ERROR:
        default:
            break;
    }
}

void DemoTesterSequence(void)
{
    CanLogger_UdsClient_RegisterSink(&OnUdsResponse);

    /* 2. Enter the extended diagnostic session ($10 0x03). */
    (void) CanLogger_UdsClient_SubmitSession(CANLOGGER_UDS_SESS_EXTENDED);

    /* 3. Read VIN ($22 DID 0xF190). The task transmits, waits P2/P2*, and fires the sink. */
    (void) CanLogger_UdsClient_SubmitReadDid(0xF190U);

    /* 4. Keep the non-default session alive ($3E, suppressPosRsp, functional 0x7DF).
     *    The task ALSO sends this automatically every CANLOGGER_UDS_TP_PERIOD_MS; this is the
     *    explicit one-shot form if you want to drive it yourself. */
    (void) CanLogger_UdsClient_SubmitTesterPresent(true);

    /* 5. Read DTCs by status mask ($19 0x02, mask 0xFF) and clear them ($14, all groups). */
    (void) CanLogger_UdsClient_SubmitReadDtc(0xFFU);
    (void) CanLogger_UdsClient_SubmitClearDtc(0xFFFFFFUL);
}
```

The submit helpers are **non-blocking**: they enqueue and return. The `uds` task drains the queue
one transaction at a time and delivers each parsed response through the sink.

## Build status

**Review-only — not compiled here** (no TI CGT C2000 toolchain in this environment). The code
follows the atelier's MISRA-aligned C and the C28x lessons (no FreeRTOS calls in ISR context;
`uint8_t` wrap/index masking per the C28x 16-bit-cell trap). Makefile wiring is a follow-up — see
the source list in the PR description.
