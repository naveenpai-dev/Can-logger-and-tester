/*!
 * @file        CanLogger_UdsClient.h
 * @brief       UDS client (ISO 14229-1 tester role) — request builders + response parser.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-U04
 *
 * @par Role
 *   Turns the passive CAN-FD logger into an active *tester*: it issues UDS diagnostic requests on
 *   the captured bus and parses the ECU's positive/negative responses. The logger keeps capturing
 *   in parallel — UDS RX is demultiplexed off the same accept-all capture path, so adding a tester
 *   does not blind the monitor (CanLogger_UdsClient_OnCapturedFrame is called from the drain task,
 *   never the ISR). Implemented services (ISO 14229-1 by clause):
 *     - $10 DiagnosticSessionControl   (§9.2)
 *     - $3E TesterPresent              (§9.5, keep-alive, suppressPosRsp 0x80)
 *     - $22 ReadDataByIdentifier       (§10.2)
 *     - $19 ReadDTCInformation         (§11.3)
 *     - $14 ClearDiagnosticInformation (§11.2)
 *   The $7F negative-response path (§A.1) and the 0x78 responsePending retry (§7.5) are handled in
 *   the response parser / task. P2 / P2* client timing is configurable (Platform_CanLogger_Cfg.h).
 *
 * @par Interfaces Provided
 *   - CanLogger_UdsClient_Init()             : create the request queue + reassembly state
 *   - CanLogger_UdsClient_Task()             : FreeRTOS task — drains requests, owns timeout/retry
 *   - CanLogger_UdsClient_Submit*()          : enqueue a typed request (returns immediately)
 *   - CanLogger_UdsClient_OnCapturedFrame()  : feed a captured response frame (drain-task context)
 *   - CanLogger_UdsClient_RegisterSink()     : receive the parsed response event
 *
 * @par FreeRTOS Interaction
 *   Task `uds` at CANLOGGEROS_PRIO_UDS. Request queue in (g_hCanLoggerUdsReqQueue), response
 *   delivered through a registered sink callback. RX is ISR-safe by construction: the MCAN ISR
 *   already posts every frame to the capture queue; the drain task calls OnCapturedFrame, which
 *   does a lightweight id match and a notify to the UDS task — no UDS work in ISR context.
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 *
 * @defgroup CanLogger_UdsClient UDS Client (Tester)
 */
#ifndef CANLOGGER_UDSCLIENT_H
#define CANLOGGER_UDSCLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "Bsp_CanFd_inf.h"

/* ── ISO 14229-1 service identifiers (request SIDs) used by this client ───────*/
#define CANLOGGER_UDS_SID_DSC           ((uint8_t) 0x10U)  /*!< DiagnosticSessionControl §9.2   */
#define CANLOGGER_UDS_SID_CDTCI         ((uint8_t) 0x14U)  /*!< ClearDiagnosticInformation §11.2*/
#define CANLOGGER_UDS_SID_RDTCI         ((uint8_t) 0x19U)  /*!< ReadDTCInformation §11.3        */
#define CANLOGGER_UDS_SID_RDBI          ((uint8_t) 0x22U)  /*!< ReadDataByIdentifier §10.2      */
#define CANLOGGER_UDS_SID_TP            ((uint8_t) 0x3EU)  /*!< TesterPresent §9.5              */

/*! @brief Positive-response SID = request SID + 0x40 (ISO 14229-1 §7.4). */
#define CANLOGGER_UDS_POSRSP_OFFSET     ((uint8_t) 0x40U)
/*! @brief Negative-response service identifier (ISO 14229-1 §7.5, Table). */
#define CANLOGGER_UDS_NR_SID            ((uint8_t) 0x7FU)
/*! @brief NRC = RequestCorrectlyReceived-ResponsePending (ISO 14229-1 §A.1). */
#define CANLOGGER_UDS_NRC_PENDING       ((uint8_t) 0x78U)

/*! @brief DiagnosticSessionControl sub-functions (ISO 14229-1 §9.2.2.2). */
#define CANLOGGER_UDS_SESS_DEFAULT      ((uint8_t) 0x01U)
#define CANLOGGER_UDS_SESS_PROGRAMMING  ((uint8_t) 0x02U)
#define CANLOGGER_UDS_SESS_EXTENDED     ((uint8_t) 0x03U)

/*! @brief TesterPresent sub-function: 0x00 normal, 0x80 = suppressPositiveResponse bit set. */
#define CANLOGGER_UDS_TP_SUPPRESS       ((uint8_t) 0x80U)

/*! @brief ReadDTCInformation sub-function: reportDTCByStatusMask (ISO 14229-1 §11.3.2.2). */
#define CANLOGGER_UDS_RDTCI_BY_STATUS   ((uint8_t) 0x02U)

/*! @brief Outcome of a UDS transaction, surfaced to the response sink. */
typedef enum {
    CANLOGGER_UDS_OK            = 0, /*!< positive response received                  */
    CANLOGGER_UDS_NEGATIVE      = 1, /*!< $7F negative response (see nrc)             */
    CANLOGGER_UDS_TIMEOUT       = 2, /*!< no (final) response within P2/P2*           */
    CANLOGGER_UDS_TP_ERROR      = 3, /*!< ISO-TP framing/sequence error               */
    CANLOGGER_UDS_BUSY          = 4  /*!< client busy with another transaction        */
} CanLogger_UdsResult_e;

/*! @brief One queued tester request. The task serializes these (one transaction at a time). */
typedef struct {
    uint8_t  sid;                                  /*!< service id (CANLOGGER_UDS_SID_*)  */
    uint8_t  sub;                                  /*!< sub-function / 0 if none          */
    uint16_t did;                                  /*!< data identifier ($22) / 0         */
    uint8_t  extra[6];                             /*!< extra request bytes (DTC mask…)   */
    uint8_t  extra_len;                            /*!< count of valid bytes in @c extra  */
    bool     functional;                           /*!< true → functional (0x7DF) request */
} CanLogger_UdsReq_t;

/*! @brief Parsed response delivered to the sink. @c data holds the full positive-response PDU. */
typedef struct {
    CanLogger_UdsResult_e result;                  /*!< transaction outcome               */
    uint8_t  req_sid;                              /*!< the SID that was requested         */
    uint8_t  nrc;                                  /*!< negative-response code (if NEGATIVE)*/
    uint16_t len;                                  /*!< response payload length            */
    uint8_t  data[CANLOGGER_UDS_RX_BUF_BYTES];     /*!< response payload (incl. response SID)*/
} CanLogger_UdsRsp_t;

/*! @brief Sink callback type — invoked from the UDS task when a transaction completes. */
typedef void (*CanLogger_UdsSinkCb)(const CanLogger_UdsRsp_t *rsp);

/*! @brief Request queue handle (tester jobs → UDS task). */
extern QueueHandle_t g_hCanLoggerUdsReqQueue;

/*!
 * @brief Create the request queue and reset client state. Call from CanLoggerOs_Init.
 * @note  Routes to CanLoggerOs_SafeState on a queue-creation failure (house rule).
 */
void CanLogger_UdsClient_Init(void);

/*! @brief Register the callback that receives parsed responses (one sink). */
void CanLogger_UdsClient_RegisterSink(CanLogger_UdsSinkCb sink);

/*!
 * @brief UDS client task body — drains the request queue, drives ISO-TP, owns P2/P2* + 0x78 retry.
 * @details For each request: build the PDU, transmit via ISO-TP, then wait (blocked on the RX frame
 *          queue fed by OnCapturedFrame) up to P2; a 0x78 extends the wait to P2* and loops up to
 *          CANLOGGER_UDS_MAX_PENDING times; a $7F (other NRC) or positive response completes the
 *          transaction and fires the sink. The periodic TesterPresent keep-alive is gated on an
 *          active non-default session (CANLOGGER_UDS_TP_REQUIRE_SESSION).
 */
void CanLogger_UdsClient_Task(void *pvParameters);

/*!
 * @brief Feed a captured frame to the client (drain-task context — NOT ISR).
 * @details Lightweight: if @p frame->id equals the configured response id, it is posted to the
 *          client's RX frame queue for the UDS task to reassemble. All parsing happens in the task.
 *          Returns true if the frame was consumed as a UDS response (still also logged).
 * @param frame  a captured CAN-FD frame from the drain path.
 * @return true if this frame matched the UDS response id.
 */
bool CanLogger_UdsClient_OnCapturedFrame(const Bsp_CanFdFrameType *frame);

/* ── Typed request helpers (enqueue + return; non-blocking) ───────────────────*/

/*! @brief $10 DiagnosticSessionControl — request @p session (CANLOGGER_UDS_SESS_*). */
bool CanLogger_UdsClient_SubmitSession(uint8_t session);

/*! @brief $22 ReadDataByIdentifier — read one @p did. */
bool CanLogger_UdsClient_SubmitReadDid(uint16_t did);

/*! @brief $19 ReadDTCInformation, reportDTCByStatusMask — @p status_mask per ISO 14229-1 §D. */
bool CanLogger_UdsClient_SubmitReadDtc(uint8_t status_mask);

/*! @brief $14 ClearDiagnosticInformation — @p group_of_dtc is a 24-bit group (0xFFFFFF = all). */
bool CanLogger_UdsClient_SubmitClearDtc(uint32_t group_of_dtc);

/*! @brief $3E TesterPresent — explicit one-shot (the task also sends it periodically). */
bool CanLogger_UdsClient_SubmitTesterPresent(bool suppress_response);

#endif /* CANLOGGER_UDSCLIENT_H */
