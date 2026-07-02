/*!
 * @file        CanLoggerOs_Tasks.c
 * @brief       Task bodies — drain (queue→host) and system monitor (STATUS + recovery).
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Bus-load estimate
 *   Coarse: (frames * 40 overhead bits + payload_bytes * 8) vs the 5 Mbps data-phase capacity,
 *   over the 1 s STATUS window. Estimate only — honest about being an estimate (the host can
 *   compute exact load from timestamps if needed).
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 */
#include "CanLoggerOs_Tasks.h"
#include "CanLogger_Mcan.h"
#include "CanLogger_HostStream.h"
#include "Platform_CanLogger_Cfg.h"
#include "queue.h"
#if (CANLOGGER_UDS_ENABLE != 0U)
#include "CanLogger_UdsClient.h"
#endif
#if (CANLOGGER_POC0_ENABLE != 0U)
#include "CanLogger_Poc0.h"
#endif

static uint8_t CanLogger_BusLoadPct(uint32_t frames, uint32_t payload_bytes)
{
    uint32_t bits = (frames * 40UL) + (payload_bytes * 8UL);
    uint32_t cap  = 5000000UL;                       /* effective bits/s @ 5 Mbps data */
    uint32_t pct  = (bits * 100UL) / cap;
    return (uint8_t) ((pct > 100UL) ? 100U : pct);
}

void CanLogger_Drain_Task(void *pvParameters)
{
    (void) pvParameters;
    Bsp_CanFdFrameType frame;

    for (;;) {
        /* Block until the ISR posts a frame; drain in a tight inner loop on bursts. */
        if (xQueueReceive(g_hCanLoggerRxQueue, &frame, portMAX_DELAY) == pdPASS) {
            CanLogger_HostStream_SendFrame(&frame);
#if (CANLOGGER_UDS_ENABLE != 0U)
            /* Demux UDS responses off the same capture path: a frame on the response id is also
             * handed to the tester (it remains logged above). Cheap id compare + a notify; all UDS
             * parsing runs in the UDS task, never here on the hot path. */
            (void) CanLogger_UdsClient_OnCapturedFrame(&frame);
#endif
#if (CANLOGGER_POC0_ENABLE != 0U)
            /* PoC-0 loopback: hand request-id frames to the on-chip responder (also still logged). */
            (void) CanLogger_Poc0_OnCapturedFrame(&frame);
#endif
            /* TODO(why): append to the block-aligned SD batch buffer here when CANLOGGER_SD_ENABLE
             *            and the FatFS writer (CanLogger_Sd) land — keep f_write off this hot path. */
        }
    }
}

void CanLogger_SysMon_Task(void *pvParameters)
{
    (void) pvParameters;
    TickType_t xLast = xTaskGetTickCount();
    uint32_t   last_rx = 0U;
    uint32_t   last_bytes = 0U;   /* reserved for a payload-byte accumulator (host can refine) */

    for (;;) {
        vTaskDelayUntil(&xLast, pdMS_TO_TICKS(CANLOGGER_STATUS_PERIOD_MS));

        uint32_t rx   = g_u32CanLoggerRxCount;
        uint32_t win  = rx - last_rx;
        last_rx = rx;

        CanLogger_McanErr_t e = CanLogger_Mcan_GetErr();
        CanLogger_HostStream_SendStatus(
            CanLogger_BusLoadPct(win, last_bytes),
            (uint8_t) e.state, e.tec, e.rec, rx, g_u32CanLoggerDropCount);

        /* ISO 11898 fault confinement: recover bus-off on the status cadence, not in the ISR. */
        if (e.state == BSP_CANFD_ERR_BUSOFF) {
            CanLogger_Mcan_RecoverBusOff();
        }
    }
}
