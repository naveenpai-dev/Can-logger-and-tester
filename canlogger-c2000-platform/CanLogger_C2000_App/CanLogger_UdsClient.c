/*!
 * @file        CanLogger_UdsClient.c
 * @brief       UDS client (ISO 14229-1 tester) — request build, ISO-TP TX, response parse, retry.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Transaction model (one in flight at a time)
 *   The tester is half-duplex per ISO 14229-2: it issues a request and waits for the (final)
 *   response before the next. The task serializes requests off g_hCanLoggerUdsReqQueue. Each
 *   transaction:
 *     1. Build the service PDU (request SID + sub-function/DID/args).
 *     2. Transmit via ISO-TP (SF for ≤7-byte requests; FF+CF otherwise).
 *     3. Block on a task notification (fed by OnCapturedFrame) for up to P2 client.
 *     4. Classify: positive ($SID+0x40) → DONE; $7F + 0x78 → re-arm for P2* and loop
 *        (≤ CANLOGGER_UDS_MAX_PENDING); $7F + other NRC → NEGATIVE; no frame in time → TIMEOUT.
 *     5. Multi-frame response → answer FF with a Flow Control CTS, reassemble CFs.
 *     6. Fire the registered sink with the parsed result.
 *
 * @par Why OnCapturedFrame, not a second ISR path
 *   The MCAN ISR already posts EVERY frame to the capture queue (the logger's reason to exist).
 *   Re-filtering for the UDS response id in the ISR would duplicate that and add ISR work. Instead
 *   the drain task — which already touches every frame — hands each one to OnCapturedFrame, which
 *   does a cheap id compare and a single xTaskNotify. No blocking, no allocation, no UDS parsing in
 *   interrupt context (CONTRIBUTING.md house rule).
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule | Category | Justification                                                     |
 *   |--------|------|----------|-------------------------------------------------------------------|
 *   | DEV-01 | 8.4  | Required | g_* globals are the documented task↔drain contract (header).       |
 *   | DEV-02 | 15.5 | Advisory | early returns in the parser keep the negative-response path clear. |
 *
 * @startuml CanLogger_UdsClient_Txn
 *   App -> Queue : SubmitReadDid(0xF190)
 *   Task -> IsoTp : TxStart(22 F1 90)
 *   IsoTp -> Bus : request
 *   Bus --> Drain : response frame(s)
 *   Drain -> Task : OnCapturedFrame -> notify
 *   Task -> Sink : CanLogger_UdsRsp_t
 * @enduml
 */
#include "CanLogger_UdsClient.h"
#include "CanLogger_IsoTp.h"
#include "Platform_CanLogger_Cfg.h"
#include "Platform_OS_Cfg.h"
#include <string.h>

/* ── Task↔drain contract globals ─────────────────────────────────────────────*/
QueueHandle_t g_hCanLoggerUdsReqQueue = NULL;

static CanLogger_UdsSinkCb s_sink           = NULL;     /* parsed-response consumer         */

/* RX frame queue: the drain task posts captured response frames here (in order), the UDS task
 * drains them (CLG-03). This replaces the former single-slot mailbox + coalescing task-notify:
 * a multi-frame response arrives as a back-to-back CF burst, and a single slot would be
 * overwritten by the higher-priority drain task before the UDS task read it, losing consecutive
 * frames and aborting on a sequence gap. A depth-N queue preserves every frame of one response. */
static QueueHandle_t        s_udsRxQueue     = NULL;

/* Reassembly buffer + context for an inbound multi-frame response. */
static uint8_t              s_rxBuf[CANLOGGER_UDS_RX_BUF_BYTES];
static CanLogger_IsoTp_Rx_t s_rx;

/* True while a non-default diagnostic session is held (a successful $10 with a non-default
 * sub-function). Gates the TesterPresent keep-alive (CLG-02) so an idle tester stays silent. */
static volatile bool        s_sessionActive = false;

/* ───────────────────────────── lifecycle ───────────────────────────────────*/

void CanLogger_UdsClient_Init(void)
{
    g_hCanLoggerUdsReqQueue =
        xQueueCreate(CANLOGGER_UDS_REQ_QUEUE_DEPTH, sizeof(CanLogger_UdsReq_t));
    configASSERT(g_hCanLoggerUdsReqQueue != NULL);
    s_udsRxQueue =
        xQueueCreate(CANLOGGER_UDS_RX_QUEUE_DEPTH, sizeof(Bsp_CanFdFrameType));
    configASSERT(s_udsRxQueue != NULL);
    s_sink    = NULL;
}

void CanLogger_UdsClient_RegisterSink(CanLogger_UdsSinkCb sink)
{
    s_sink = sink;
}

/* ───────────────────────── request enqueue helpers ─────────────────────────*/

static bool CanLogger_UdsClient_Enqueue(const CanLogger_UdsReq_t *req)
{
    if (g_hCanLoggerUdsReqQueue == NULL) {
        return false;
    }
    /* Non-blocking enqueue from app/CLI context — drop (return false) if the queue is full. */
    return (xQueueSend(g_hCanLoggerUdsReqQueue, req, 0U) == pdPASS);
}

bool CanLogger_UdsClient_SubmitSession(uint8_t session)
{
    CanLogger_UdsReq_t r;
    (void) memset(&r, 0, sizeof(r));
    r.sid = CANLOGGER_UDS_SID_DSC;
    r.sub = session;
    return CanLogger_UdsClient_Enqueue(&r);
}

bool CanLogger_UdsClient_SubmitReadDid(uint16_t did)
{
    CanLogger_UdsReq_t r;
    (void) memset(&r, 0, sizeof(r));
    r.sid = CANLOGGER_UDS_SID_RDBI;
    r.did = did;
    return CanLogger_UdsClient_Enqueue(&r);
}

bool CanLogger_UdsClient_SubmitReadDtc(uint8_t status_mask)
{
    CanLogger_UdsReq_t r;
    (void) memset(&r, 0, sizeof(r));
    r.sid        = CANLOGGER_UDS_SID_RDTCI;
    r.sub        = CANLOGGER_UDS_RDTCI_BY_STATUS;
    r.extra[0]   = status_mask;
    r.extra_len  = 1U;
    return CanLogger_UdsClient_Enqueue(&r);
}

bool CanLogger_UdsClient_SubmitClearDtc(uint32_t group_of_dtc)
{
    CanLogger_UdsReq_t r;
    (void) memset(&r, 0, sizeof(r));
    r.sid       = CANLOGGER_UDS_SID_CDTCI;
    /* 3-byte groupOfDTC, high byte first (ISO 14229-1 §11.2.2.2). Masked per lesson 7.2.12. */
    r.extra[0]  = (uint8_t) ((group_of_dtc >> 16) & 0xFFU);
    r.extra[1]  = (uint8_t) ((group_of_dtc >> 8)  & 0xFFU);
    r.extra[2]  = (uint8_t) (group_of_dtc & 0xFFU);
    r.extra_len = 3U;
    return CanLogger_UdsClient_Enqueue(&r);
}

bool CanLogger_UdsClient_SubmitTesterPresent(bool suppress_response)
{
    CanLogger_UdsReq_t r;
    (void) memset(&r, 0, sizeof(r));
    r.sid        = CANLOGGER_UDS_SID_TP;
    r.sub        = suppress_response ? CANLOGGER_UDS_TP_SUPPRESS : 0x00U;
    r.functional = true;     /* keep-alive is broadcast to all ECUs on the functional id */
    return CanLogger_UdsClient_Enqueue(&r);
}

/* ───────────────────────── request PDU builder ─────────────────────────────*/

/*!
 * @brief Serialize a queued request into a UDS service PDU (request SID first).
 * @return PDU length in bytes (>= 1).
 */
static uint16_t CanLogger_UdsClient_BuildPdu(const CanLogger_UdsReq_t *req, uint8_t *pdu)
{
    uint16_t n = 0U;
    pdu[n++] = req->sid;

    switch (req->sid) {
        case CANLOGGER_UDS_SID_DSC:        /* $10: SID + session              */
        case CANLOGGER_UDS_SID_TP:         /* $3E: SID + sub-function (0/0x80) */
        case CANLOGGER_UDS_SID_RDTCI:      /* $19: SID + reportType + mask     */
            pdu[n++] = req->sub;
            break;

        case CANLOGGER_UDS_SID_RDBI:       /* $22: SID + DID (high, low)       */
            pdu[n++] = (uint8_t) ((req->did >> 8) & 0xFFU);
            pdu[n++] = (uint8_t) (req->did & 0xFFU);
            break;

        case CANLOGGER_UDS_SID_CDTCI:      /* $14: SID + 3-byte groupOfDTC     */
        default:
            break;
    }

    /* Append any service-specific trailing bytes (DTC status mask, groupOfDTC, …). */
    {
        uint8_t i;
        for (i = 0U; i < req->extra_len; i++) {
            pdu[n++] = req->extra[i];
        }
    }
    return n;
}

/* ───────────────────────── response classification ─────────────────────────*/

/*!
 * @brief Classify a reassembled response PDU against the request SID.
 * @param req_sid   the requested service id.
 * @param pdu       reassembled response payload.
 * @param len       payload length.
 * @param[out] nrc  negative-response code when the result is NEGATIVE/pending.
 * @return CANLOGGER_UDS_OK on a positive response, CANLOGGER_UDS_NEGATIVE on a final $7F.
 *         Returns CANLOGGER_UDS_BUSY as the sentinel for a 0x78 responsePending (caller re-waits).
 */
static CanLogger_UdsResult_e CanLogger_UdsClient_Classify(uint8_t req_sid,
                                                          const uint8_t *pdu, uint16_t len,
                                                          uint8_t *nrc)
{
    *nrc = 0U;
    if (len == 0U) {
        return CANLOGGER_UDS_TIMEOUT;
    }

    if (pdu[0] == (uint8_t) (req_sid + CANLOGGER_UDS_POSRSP_OFFSET)) {
        return CANLOGGER_UDS_OK;                       /* positive response (ISO 14229-1 §7.4) */
    }

    if ((pdu[0] == CANLOGGER_UDS_NR_SID) && (len >= 3U)) {
        /* $7F | requestSID | NRC  (ISO 14229-1 §7.5). Echoed SID must match the request. */
        if (pdu[1] == req_sid) {
            *nrc = pdu[2];
            if (pdu[2] == CANLOGGER_UDS_NRC_PENDING) {
                return CANLOGGER_UDS_BUSY;             /* 0x78 → caller extends to P2* and waits */
            }
            return CANLOGGER_UDS_NEGATIVE;
        }
    }
    return CANLOGGER_UDS_TIMEOUT;                       /* unrelated frame — treat as no answer */
}

/* ───────────────────────── RX feed (drain-task context) ────────────────────*/

bool CanLogger_UdsClient_OnCapturedFrame(const Bsp_CanFdFrameType *frame)
{
    if (frame->id != CANLOGGER_UDS_RSP_ID) {
        return false;                                  /* not a response to us */
    }
    /* Queue the response frame for the UDS task (drain-task context; non-blocking). If the queue
     * is full the frame is dropped here — but it was already logged by the caller, so capture is
     * never lost; only the tester's view of an over-deep response degrades (bounded by the depth). */
    if (s_udsRxQueue != NULL) {
        (void) xQueueSend(s_udsRxQueue, frame, 0U);
    }
    return true;
}

/* ───────────────────────── one transaction ─────────────────────────────────*/

/*!
 * @brief Wait for the next response frame (task-notify) within @p timeout_ms, feed it to ISO-TP.
 * @return ISO-TP feed result, or CANLOGGER_ISOTP_RX_ERROR coded as IDLE-timeout via @p timed_out.
 */
static CanLogger_IsoTp_RxResult_e CanLogger_UdsClient_AwaitFrame(uint16_t timeout_ms,
                                                                 bool *timed_out)
{
    Bsp_CanFdFrameType local;
    *timed_out = false;

    if (xQueueReceive(s_udsRxQueue, &local, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        *timed_out = true;
        return CANLOGGER_ISOTP_RX_IDLE;
    }
    return CanLogger_IsoTp_RxFeed(&s_rx, &local);
}

/*!
 * @brief Run a single UDS transaction end-to-end and fill @p rsp.
 */
static void CanLogger_UdsClient_Transact(const CanLogger_UdsReq_t *req, CanLogger_UdsRsp_t *rsp)
{
    uint8_t  pdu[CANLOGGER_UDS_TX_BUF_BYTES];
    uint16_t pdu_len;
    uint16_t pending = 0U;
    uint32_t req_id  = req->functional ? CANLOGGER_UDS_FUNC_ID : CANLOGGER_UDS_REQ_ID;
    uint8_t  pending_count = 0U;
    uint16_t wait_ms = CANLOGGER_UDS_P2_CLIENT_MS;

    (void) memset(rsp, 0, sizeof(*rsp));
    rsp->req_sid = req->sid;

    /* 1. Build + transmit the request via ISO-TP. Flush any frames left from a prior
     *    transaction so this response reassembles from a clean queue (half-duplex model). */
    pdu_len = CanLogger_UdsClient_BuildPdu(req, pdu);
    CanLogger_IsoTp_RxInit(&s_rx, s_rxBuf, (uint16_t) sizeof(s_rxBuf));
    (void) xQueueReset(s_udsRxQueue);

    if (CanLogger_IsoTp_TxStart(BSP_CANFD_CH0, req_id, pdu, pdu_len, &pending) != 0) {
        rsp->result = CANLOGGER_UDS_TP_ERROR;
        return;
    }
    if (pending > 0U) {
        /* Multi-frame request: await the ECU's Flow Control before sending consecutive frames.
         * (Rare for a tester; a long $22 DID-list or routine argument list triggers it.) */
        bool to;
        CanLogger_IsoTp_RxResult_e fr = CanLogger_UdsClient_AwaitFrame(wait_ms, &to);
        (void) fr;   /* the awaited frame is the ECU's Flow Control; see block-size note below */
        if (to) { rsp->result = CANLOGGER_UDS_TIMEOUT; return; }
        {
            uint16_t sent = CANLOGGER_ISOTP_FF_DATA_LEN; /* 6 bytes already in the First Frame */
            uint8_t  sn   = 1U;
            /* TODO(why): honour the ECU's FC block-size/STmin from the received FC frame instead of
             *            the tester's configured CANLOGGER_UDS_FC_BS. Multi-frame *requests* are rare
             *            for a tester (most requests are ≤ 7 bytes); the configured BS is a safe
             *            default until an FC parser is added to RxFeed's FC case. */
            if (CanLogger_IsoTp_TxContinue(BSP_CANFD_CH0, req_id, pdu, pdu_len,
                                           &sent, &sn, CANLOGGER_UDS_FC_BS) != 0) {
                rsp->result = CANLOGGER_UDS_TP_ERROR;
                return;
            }
        }
    }

    /* Suppress-positive-response TesterPresent gets no answer by design — done. */
    if ((req->sid == CANLOGGER_UDS_SID_TP) && ((req->sub & CANLOGGER_UDS_TP_SUPPRESS) != 0U)) {
        rsp->result = CANLOGGER_UDS_OK;
        return;
    }

    /* 2. Await + reassemble the response, handling 0x78 responsePending and multi-frame. */
    for (;;) {
        bool to;
        CanLogger_IsoTp_RxResult_e fr = CanLogger_UdsClient_AwaitFrame(wait_ms, &to);

        if (to) {
            rsp->result = CANLOGGER_UDS_TIMEOUT;
            return;
        }
        if (fr == CANLOGGER_ISOTP_RX_SENDFC) {
            /* First Frame of a multi-frame response — grant flow control, keep receiving. */
            (void) CanLogger_IsoTp_SendFc(BSP_CANFD_CH0, CANLOGGER_UDS_REQ_ID,
                                          CANLOGGER_ISOTP_FS_CTS,
                                          CANLOGGER_UDS_FC_BS, CANLOGGER_UDS_FC_STMIN_MS);
            continue;
        }
        if (fr == CANLOGGER_ISOTP_RX_INPROGRESS) {
            continue;                                  /* more consecutive frames coming */
        }
        if ((fr == CANLOGGER_ISOTP_RX_ERROR) || (fr == CANLOGGER_ISOTP_RX_IDLE)) {
            rsp->result = CANLOGGER_UDS_TP_ERROR;
            return;
        }

        /* fr == RX_DONE: a complete PDU is in s_rxBuf — classify it. */
        {
            uint8_t nrc = 0U;
            CanLogger_UdsResult_e cls =
                CanLogger_UdsClient_Classify(req->sid, s_rxBuf, s_rx.len, &nrc);

            if (cls == CANLOGGER_UDS_BUSY) {
                /* 0x78 responsePending: re-arm with P2* and keep waiting (bounded). */
                pending_count++;
                if (pending_count > CANLOGGER_UDS_MAX_PENDING) {
                    rsp->result = CANLOGGER_UDS_TIMEOUT;
                    return;
                }
                wait_ms = CANLOGGER_UDS_P2_STAR_MS;
                CanLogger_IsoTp_RxInit(&s_rx, s_rxBuf, (uint16_t) sizeof(s_rxBuf));
                continue;
            }

            rsp->result = cls;
            rsp->nrc    = nrc;
            if (cls == CANLOGGER_UDS_OK) {
                uint16_t n = (s_rx.len <= CANLOGGER_UDS_RX_BUF_BYTES)
                             ? s_rx.len : CANLOGGER_UDS_RX_BUF_BYTES;
                /* Track session state for the keep-alive gate (CLG-02): a positive $10 to a
                 * non-default session arms the keep-alive; a return to default disarms it. */
                if (req->sid == CANLOGGER_UDS_SID_DSC) {
                    s_sessionActive = (req->sub != CANLOGGER_UDS_SESS_DEFAULT);
                }
                (void) memcpy(rsp->data, s_rxBuf, n);
                rsp->len = n;
            }
            return;
        }
    }
}

/* ───────────────────────────── task body ───────────────────────────────────*/

void CanLogger_UdsClient_Task(void *pvParameters)
{
    (void) pvParameters;
    CanLogger_UdsReq_t req;
    CanLogger_UdsRsp_t rsp;

    for (;;) {
        /* Block up to the TesterPresent cadence for the next queued request. A timeout means it is
         * time to emit the keep-alive (non-default sessions die after S3 ≈ 5 s of silence). */
        if (xQueueReceive(g_hCanLoggerUdsReqQueue, &req,
                          pdMS_TO_TICKS(CANLOGGER_UDS_TP_PERIOD_MS)) == pdPASS) {
            CanLogger_UdsClient_Transact(&req, &rsp);
            if (s_sink != NULL) {
                s_sink(&rsp);
            }
        } else {
#if (CANLOGGER_UDS_TP_REQUIRE_SESSION != 0U)
            /* CLG-02: only keep a session alive if we actually hold one. A bare-armed tester in
             * the default session has nothing to keep alive and must stay silent on the bus. */
            if (s_sessionActive)
#endif
            {
                /* Periodic keep-alive: suppressPosRsp TesterPresent on the functional id. */
                CanLogger_UdsReq_t tp;
                (void) memset(&tp, 0, sizeof(tp));
                tp.sid        = CANLOGGER_UDS_SID_TP;
                tp.sub        = CANLOGGER_UDS_TP_SUPPRESS;
                tp.functional = true;
                CanLogger_UdsClient_Transact(&tp, &rsp);
                /* suppressPosRsp ⇒ no response expected; result is not delivered to the sink. */
            }
        }
    }
}
