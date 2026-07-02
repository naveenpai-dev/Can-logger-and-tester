/*!
 * @file        Platform_CanLogger_Cfg.h
 * @brief       Per-atelier knobs for the CAN-FD Logger — the ONLY file an integrator edits.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Layer position
 *   platform-config/canlogger/ — application-owned configuration. The BSP / MCAL `.c` files
 *   include this header by name; the makefile resolves it via `-Iplatform-config/canlogger`.
 *   Changing a value here re-shapes the atelier without touching the portable layers.
 *
 * @par Worked example — retune for a 40 MHz MCAN clock
 *   Run `python3 host/canfd_bittiming.py --clock 40e6 --nominal 2e6 --data 5e6`, then set the
 *   six CANLOGGER_BT_* values below to the solver's 0%-error result (40 MHz: NOM 15/4/4, DATA 5/2/2).
 *
 * @par Change History
 * | Version | Date       | Author     | Description       |
 * |---------|------------|------------|-------------------|
 * | 0.1.0   | 2026-06-22 | Naveen Pai | Initial release   |
 *
 * @par MISRA-C:2012 Deviations
 * None.
 *
 * @defgroup CanLogger_Cfg CAN-FD Logger Configuration
 */
#ifndef PLATFORM_CANLOGGER_CFG_H
#define PLATFORM_CANLOGGER_CFG_H

#include <stdint.h>

/* ── Target device ───────────────────────────────────────────────────────────
 * F280039C: C28x, single MCAN-FD instance, on-board CAN-FD transceiver (~5 Mbps). */
#define CANLOGGER_DEVICE_F280039C       (1U)

/* ── CAN-FD bit timing — solver output @ MCAN functional clock = 80 MHz, BRP=1 ──
 * NOMINAL 2 Mbps (SP 80.0%) · DATA 5 Mbps (SP 81.2%) · 0% error.
 * These are the (TSEG-1)/(TSEG-2)/(SJW) *human* values; the MCAL subtracts 1 for the register.
 * RE-VERIFY with host/canfd_bittiming.py for YOUR clock before trusting the bus. */
#define CANLOGGER_MCAN_CLOCK_HZ         (80000000UL)
#define CANLOGGER_BT_NOM_BRP            ((uint16_t) 1U)
#define CANLOGGER_BT_NOM_TSEG1          ((uint16_t) 31U)
#define CANLOGGER_BT_NOM_TSEG2          ((uint16_t) 8U)
#define CANLOGGER_BT_NOM_SJW            ((uint16_t) 8U)
#define CANLOGGER_BT_DAT_BRP            ((uint16_t) 1U)
#define CANLOGGER_BT_DAT_TSEG1          ((uint16_t) 12U)
#define CANLOGGER_BT_DAT_TSEG2          ((uint16_t) 3U)
#define CANLOGGER_BT_DAT_SJW            ((uint16_t) 3U)

/* Enable Transmitter Delay Compensation / Secondary Sample Point in the data phase.
 * REQUIRED above ~2 Mbps to tolerate transceiver loop delay. */
#define CANLOGGER_MCAN_TDC_ENABLE       (1U)

/* ── Capture sizing ──────────────────────────────────────────────────────────
 * Ring depth (power of two) sized for a burst at full bus load. The RX queue between
 * the ISR and the drain task is the platform-layer mirror of this ring. */
#define CANLOGGER_RING_DEPTH            ((uint16_t) 1024U)     /* MUST be a power of two */
#define CANLOGGER_MAX_DLEN              ((uint8_t)  64U)       /* CAN-FD max payload     */
#define CANLOGGER_RXQUEUE_DEPTH         ((uint16_t) 64U)       /* ISR → drain-task queue */

/* ── Host stream ─────────────────────────────────────────────────────────────
 * SCIA over the XDS110 virtual COM. 3 Mbaud keeps the link ahead of 5 Mbps capture. */
#define CANLOGGER_SCI_BAUD              (3000000UL)
#define CANLOGGER_STATUS_PERIOD_MS      ((uint16_t) 1000U)     /* STATUS frame cadence   */

/* ── microSD batch writer ────────────────────────────────────────────────────
 * Block-aligned batching: accumulate before f_write to keep FatFS off the hot path. */
#define CANLOGGER_SD_ENABLE             (1U)
#define CANLOGGER_SD_BATCH_BYTES        ((uint16_t) 512U)      /* one block               */

/* ── UDS client (ISO 14229-1 tester role) — turns the passive logger into a tester ──
 * When enabled, CanLogger_UdsClient_Task can issue diagnostic requests on the captured
 * bus and parse responses. The logger remains a true line monitor: UDS RX is demultiplexed
 * off the same accept-all capture path (CanLogger_UdsClient_OnCapturedFrame), so capture is
 * never disturbed. Set the IDs to the target ECU's diagnostic addresses before use. */
#define CANLOGGER_UDS_ENABLE            (1U)

/* Diagnostic addressing (ISO 14229-2 / ISO 15765-2). REQ_ID = tester→ECU (physical request),
 * RSP_ID = ECU→tester (response). Defaults below mirror the platform DTU pair (0x7A0/0x7A8);
 * retarget per ECU (BMS 0x7B0/0x7B8, BCU 0x7C0/0x7C8, OBC 0x7E0/0x7E8). FUNC_ID is the
 * functional broadcast request id (0x7DF) used for TesterPresent keep-alive to all ECUs. */
#define CANLOGGER_UDS_REQ_ID            ((uint32_t) 0x7A0UL)   /* tester → ECU (physical) */
#define CANLOGGER_UDS_RSP_ID            ((uint32_t) 0x7A8UL)   /* ECU → tester (response) */
#define CANLOGGER_UDS_FUNC_ID           ((uint32_t) 0x7DFUL)   /* functional broadcast    */

/* ISO-TP addressing format: 0 = 11-bit standard ids, 1 = 29-bit extended ids.
 * Drives the IDE flag on transmitted request frames. */
#define CANLOGGER_UDS_EXTENDED_ID       (0U)

/* ISO-TP frame data length used for the tester's request frames.
 * 8 = classic CAN DL (universally accepted by classic-CAN ECUs); a CAN-FD-only target may use
 * up to 64. Single-frame requests pad to this length; flow-control frames also use it. */
#define CANLOGGER_UDS_TX_DL             ((uint8_t) 8U)

/* ISO 14229-2 client session timing. P2_client = max wait for the first/final response after a
 * request. P2*_client = extended wait after a 0x78 (RequestCorrectlyReceived-ResponsePending)
 * negative response. Values per ISO 14229-1 §9 default session — retune to the target's
 * ACCESS_TIMING_PARAMETER ($83) report if it advertises tighter/looser limits. */
#define CANLOGGER_UDS_P2_CLIENT_MS      ((uint16_t) 50U)      /* default P2 client      */
#define CANLOGGER_UDS_P2_STAR_MS        ((uint16_t) 5000U)    /* default P2* client     */

/* Maximum number of consecutive 0x78 (responsePending) replies tolerated before the client
 * declares a timeout. Bounds an ECU that never finishes a long operation. */
#define CANLOGGER_UDS_MAX_PENDING       ((uint8_t)  8U)

/* ISO-TP flow-control the tester offers when RECEIVING a multi-frame response:
 *   BS    = block size (0 = send all consecutive frames without further FC; ISO 15765-2 §6.5.5)
 *   STmin = minimum separation time the tester requests between consecutive frames (ms here;
 *           0x00..0x7F = ms per ISO 15765-2 §6.5.5.5). */
#define CANLOGGER_UDS_FC_BS             ((uint8_t)  0U)
#define CANLOGGER_UDS_FC_STMIN_MS       ((uint8_t)  0U)

/* TesterPresent ($3E) keep-alive cadence. Per ISO 14229-1 §5.4.4 a non-default session times
 * out after S3 (~5 s) of silence; send a sub-function 0x80 (suppressPosRsp) ping below that. */
#define CANLOGGER_UDS_TP_PERIOD_MS      ((uint16_t) 2000U)

/* Reassembly buffer for a received multi-frame response. Bounds the largest response payload the
 * tester will accept (ISO-TP allows up to 4095 B; this caps RAM on the C28x). */
#define CANLOGGER_UDS_RX_BUF_BYTES      ((uint16_t) 256U)
/* Largest request payload the client will assemble (DID lists, routine arguments, etc.). */
#define CANLOGGER_UDS_TX_BUF_BYTES      ((uint16_t) 128U)

/* Depth of the request queue feeding CanLogger_UdsClient_Task (pending tester jobs). */
#define CANLOGGER_UDS_REQ_QUEUE_DEPTH   ((uint16_t) 8U)

#endif /* PLATFORM_CANLOGGER_CFG_H */
