/*!
 * @file        CanLogger_Poc0.c
 * @brief       PoC-0 on-chip self-test — UDS request driver + canned responder over MCAN loopback.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par The closed loop (all on one chip, internal loopback)
 * @verbatim
 *   Driver ── SubmitSession/ReadDid/ReadDtc ─▶ UDS tester ── ISO-TP TX(req_id) ─▶ MCAN
 *                                                                                   │ loopback
 *   host STATUS ◀── logger drain ◀── capture ◀────────────────────────────────────┘
 *                        │
 *                        ├─▶ UDS tester  OnCapturedFrame(rsp_id)  → reassemble → sink → stats
 *                        └─▶ responder    OnCapturedFrame(req_id)  → canned reply TX(rsp_id)
 * @endverbatim
 *   Every exchange puts ≥2 frames (request, response — more when multi-frame) through the capture
 *   path, so the logger's rx counter climbs and drop_count stays 0 while the tester round-trips.
 *
 * @par Responder scope (STUB)
 *   Answers single-frame requests only (all the driver's requests are ≤7 bytes → one SF). For the
 *   $22 reply it streams a First Frame + Consecutive Frames to exercise the tester's reassembly,
 *   WITHOUT waiting for the tester's Flow Control — safe on the loopback (tester offers BS=0),
 *   deliberately not ISO-compliant server behaviour. This is a self-test harness, not an ECU.
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule | Category | Justification                                            |
 *   |--------|------|----------|----------------------------------------------------------|
 *   | DEV-01 | 8.4  | Required | s_* statics are the responder queue/stats contract.      |
 *   | DEV-02 | 8.9  | Advisory | canned response tables are module-local (single TU).     |
 */
#include "CanLogger_Poc0.h"
#include "CanLogger_UdsClient.h"
#include "CanLogger_IsoTp.h"
#include "Platform_CanLogger_Cfg.h"
#include "Platform_OS_Cfg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

#if (CANLOGGER_POC0_ENABLE != 0U)

/* ── State ───────────────────────────────────────────────────────────────────*/
static QueueHandle_t          s_respQueue = NULL;    /* captured requests → responder task */
static CanLogger_Poc0_Stats_t s_stats;

/* Canned 17-byte "VIN" for the $22 F190 reply — makes the response multi-frame (20 B total)
 * so the tester's ISO-TP FF→CF reassembly runs. ASCII "MATTEROS-CANLOG01". */
static const uint8_t k_vin[17] = {
    'M','A','T','T','E','R','O','S','-','C','A','N','L','O','G','0','1'
};

/* ── Tester sink: tally parsed outcomes (observable proof the round-trip closed) ─────────────*/
static void CanLogger_Poc0_Sink(const CanLogger_UdsRsp_t *rsp)
{
    if (rsp->result == CANLOGGER_UDS_OK) {
        s_stats.responses_ok++;
    } else if (rsp->result == CANLOGGER_UDS_NEGATIVE) {
        s_stats.responses_neg++;
    } else {
        /* TIMEOUT / TP_ERROR — left uncounted here; visible as a stalled responses_ok. */
    }
}

void CanLogger_Poc0_Init(void)
{
    (void) memset(&s_stats, 0, sizeof(s_stats));
    s_respQueue = xQueueCreate(CANLOGGER_POC0_REQ_QUEUE_DEPTH, sizeof(Bsp_CanFdFrameType));
    configASSERT(s_respQueue != NULL);
    CanLogger_UdsClient_RegisterSink(&CanLogger_Poc0_Sink);
}

bool CanLogger_Poc0_OnCapturedFrame(const Bsp_CanFdFrameType *frame)
{
    /* Only physical/functional REQUEST ids are the responder's business. */
    if ((frame->id != CANLOGGER_UDS_REQ_ID) && (frame->id != CANLOGGER_UDS_FUNC_ID)) {
        return false;
    }
    if (s_respQueue != NULL) {
        (void) xQueueSend(s_respQueue, frame, 0U);
    }
    return true;
}

/*! @brief Send a complete response PDU on the response id (SF, or FF+CF for >7 bytes). */
static void CanLogger_Poc0_SendResponse(const uint8_t *pdu, uint16_t len)
{
    uint16_t pending = 0U;

    if (CanLogger_IsoTp_TxStart(BSP_CANFD_CH0, CANLOGGER_UDS_RSP_ID, pdu, len, &pending) != 0) {
        return;
    }
    s_stats.responder_replies++;
    if (pending > 0U) {
        /* Multi-frame: stream the Consecutive Frames without awaiting Flow Control (loopback stub).
         * The tester (BS=0/STmin=0) would grant exactly this; here we simply do not wait for it. */
        uint16_t sent = CANLOGGER_ISOTP_FF_DATA_LEN;   /* 6 bytes already in the First Frame */
        uint8_t  sn   = 1U;
        (void) CanLogger_IsoTp_TxContinue(BSP_CANFD_CH0, CANLOGGER_UDS_RSP_ID,
                                          pdu, len, &sent, &sn, 0U);
        s_stats.responder_replies++;
    }
}

/*! @brief Build + send the canned positive response for one single-frame request. */
static void CanLogger_Poc0_Answer(const Bsp_CanFdFrameType *req)
{
    uint8_t  pci;
    uint8_t  sid;
    uint8_t  pdu[24];
    uint16_t n = 0U;

    if (req->dlen == 0U) { return; }
    pci = (uint8_t) ((req->data[0] >> 4) & 0x0FU);
    if (pci != (uint8_t) CANLOGGER_ISOTP_PCI_SF) { return; }   /* stub: single-frame requests only */
    if (req->dlen < 2U) { return; }
    sid = req->data[1];

    switch (sid) {
        case CANLOGGER_UDS_SID_DSC:                 /* $10 → 0x50 <sub> <P2(2)> <P2*(2)> */
            pdu[n++] = (uint8_t) (sid + CANLOGGER_UDS_POSRSP_OFFSET);
            pdu[n++] = (req->dlen >= 3U) ? req->data[2] : CANLOGGER_UDS_SESS_EXTENDED;
            pdu[n++] = 0x00U; pdu[n++] = 0x32U;     /* P2 = 50 ms  */
            pdu[n++] = 0x01U; pdu[n++] = 0xF4U;     /* P2* = 5000 ms (×10 ms units nominal) */
            break;

        case CANLOGGER_UDS_SID_RDBI:                /* $22 → 0x62 <did_hi> <did_lo> <VIN 17> */
            pdu[n++] = (uint8_t) (sid + CANLOGGER_UDS_POSRSP_OFFSET);
            pdu[n++] = (req->dlen >= 3U) ? req->data[2] : 0xF1U;
            pdu[n++] = (req->dlen >= 4U) ? req->data[3] : 0x90U;
            {
                uint16_t i;
                for (i = 0U; i < (uint16_t) sizeof(k_vin); i++) { pdu[n++] = k_vin[i]; }
            }
            break;

        case CANLOGGER_UDS_SID_RDTCI:               /* $19 → 0x59 <sub> <statusAvailMask> <one DTC> */
            pdu[n++] = (uint8_t) (sid + CANLOGGER_UDS_POSRSP_OFFSET);
            pdu[n++] = (req->dlen >= 3U) ? req->data[2] : CANLOGGER_UDS_RDTCI_BY_STATUS;
            pdu[n++] = 0xFFU;                        /* DTCStatusAvailabilityMask */
            pdu[n++] = 0x12U; pdu[n++] = 0x34U; pdu[n++] = 0x56U; /* DTC high/mid/low */
            pdu[n++] = 0x09U;                        /* status: confirmed|testFailed */
            break;

        case CANLOGGER_UDS_SID_TP:                  /* $3E: suppressPosRsp → answer nothing */
            return;

        default:                                    /* unknown → $7F <sid> serviceNotSupported */
            pdu[n++] = 0x7FU;
            pdu[n++] = sid;
            pdu[n++] = 0x11U;                        /* NRC 0x11 = serviceNotSupported */
            break;
    }
    CanLogger_Poc0_SendResponse(pdu, n);
}

void CanLogger_Poc0_ResponderTask(void *pvParameters)
{
    (void) pvParameters;
    Bsp_CanFdFrameType req;
    for (;;) {
        if (xQueueReceive(s_respQueue, &req, portMAX_DELAY) == pdPASS) {
            CanLogger_Poc0_Answer(&req);
        }
    }
}

void CanLogger_Poc0_DriverTask(void *pvParameters)
{
    (void) pvParameters;

    /* Let the scheduler settle and MCAN reach NORMAL/loopback before the first request. */
    vTaskDelay(pdMS_TO_TICKS(50U));

    for (;;) {
        /* Open an extended session (arms the keep-alive per CLG-02), then read a DID (multi-frame
         * response) and the DTCs. Spaced so each round-trip completes before the next request. */
        if (CanLogger_UdsClient_SubmitSession(CANLOGGER_UDS_SESS_EXTENDED)) { s_stats.requests_sent++; }
        vTaskDelay(pdMS_TO_TICKS(100U));

        if (CanLogger_UdsClient_SubmitReadDid(0xF190U)) { s_stats.requests_sent++; }
        vTaskDelay(pdMS_TO_TICKS(100U));

        if (CanLogger_UdsClient_SubmitReadDtc(0xFFU)) { s_stats.requests_sent++; }
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

CanLogger_Poc0_Stats_t CanLogger_Poc0_GetStats(void)
{
    return s_stats;
}

#endif /* CANLOGGER_POC0_ENABLE */
