/*!
 * @file        CanLogger_Poc0.h
 * @brief       PoC-0 on-chip self-test — internal-loopback UDS responder + request driver.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-U05 (test scaffold — NOT part of the shipping logger)
 *
 * @par What this proves (WO-4 PoC-0, board-free)
 *   With MCAN in INTERNAL loopback, every frame a task transmits loops back onto the capture
 *   path. This module closes the loop entirely on-chip so BOTH roles run with no bus, no
 *   transceiver, no second node:
 *     - a **request driver** submits a UDS sequence ($10 → $22 → $19) through the tester;
 *     - an **on-chip responder** watches the captured request id and emits canned UDS positive
 *       responses on the response id — including a multi-frame $22 reply that exercises the
 *       tester's ISO-TP First-Frame → Consecutive-Frame reassembly (the CLG-03 path).
 *   The logger meanwhile captures all four frames of each exchange. Success is observable in the
 *   host STATUS stream (rx count climbs, drop_count stays 0) and in the counters below.
 *
 * @par Scope
 *   Compiled ONLY when CANLOGGER_POC0_ENABLE (derived from the CANLOGGER_POC0_BUILD flag). The
 *   responder is a deliberate STUB: it answers single-frame requests and, for the multi-frame
 *   reply, streams its Consecutive Frames without awaiting the tester's Flow Control (harmless on
 *   the loopback where the tester offers BS=0/STmin=0). It is a self-test harness, not an ECU.
 *
 * @defgroup CanLogger_Poc0 PoC-0 Loopback Self-Test
 */
#ifndef CANLOGGER_POC0_H
#define CANLOGGER_POC0_H

#include <stdint.h>
#include "Bsp_CanFd_inf.h"

/*! @brief Observable PoC-0 counters (watch in a debugger or surface over the host stream). */
typedef struct {
    uint32_t requests_sent;      /*!< UDS requests the driver submitted            */
    uint32_t responses_ok;       /*!< positive UDS responses the tester parsed     */
    uint32_t responses_neg;      /*!< negative ($7F) responses the tester parsed   */
    uint32_t responder_replies;  /*!< frames the on-chip responder transmitted     */
} CanLogger_Poc0_Stats_t;

/*! @brief Create the responder queue + register the tester sink. Call from CanLoggerOs_Init. */
void CanLogger_Poc0_Init(void);

/*!
 * @brief Feed one captured frame to the responder (drain-task context).
 * @details Mirror of CanLogger_UdsClient_OnCapturedFrame but for the REQUEST id: a request seen
 *          on the loopback is queued for the responder task to answer. Cheap id compare + queue.
 * @return true if the frame was a request destined for the responder.
 */
bool CanLogger_Poc0_OnCapturedFrame(const Bsp_CanFdFrameType *frame);

/*! @brief FreeRTOS task: answer queued requests with canned UDS responses on the response id. */
void CanLogger_Poc0_ResponderTask(void *pvParameters);

/*! @brief FreeRTOS task: drive the UDS request sequence on a cadence. */
void CanLogger_Poc0_DriverTask(void *pvParameters);

/*! @brief Snapshot the PoC-0 counters. */
CanLogger_Poc0_Stats_t CanLogger_Poc0_GetStats(void);

#endif /* CANLOGGER_POC0_H */
