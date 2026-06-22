/*!
 * @file        CanLogger_Mcan.c
 * @brief       CAN-FD capture front-end — MCAN accept-all, ISR posts frames to the drain queue.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Design
 *   The raw MCAN RX element is normalized into a Bsp_CanFdFrameType by the MCAL and handed to
 *   this app-level callback, which copies it into the RX queue with an ISR-safe post. No filters
 *   are installed: non-matching frames are routed to RX FIFO0 (true line logger). The bus is
 *   never blocked — a full queue increments the drop counter and returns.
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule  | Category | Justification                                              |
 *   |--------|-------|----------|------------------------------------------------------------|
 *   | DEV-01 | 8.4   | Required | g_* globals are the documented ISR↔task contract (header). |
 *
 * @startuml CanLogger_Mcan_Rx
 *   MCAN -> ISR : frame in FIFO0
 *   ISR -> Queue : xQueueSendFromISR(frame)
 *   Queue --> Drain : (consumed by CanLogger_Drain_Task)
 * @enduml
 */
#include "CanLogger_Mcan.h"
#include "Mcal_C2000_Mcan.h"          /* MCAL: no FreeRTOS headers below this line */
#include "Platform_CanLogger_Cfg.h"

QueueHandle_t      g_hCanLoggerRxQueue    = NULL;
volatile uint32_t  g_u32CanLoggerDropCount = 0U;
volatile uint32_t  g_u32CanLoggerRxCount   = 0U;

/*!
 * @brief RX ISR callback (interrupt context) — ISR-safe queue post only.
 * @note  Registered via Bsp_CanFd_EnableRxISR. No blocking calls; @p frame is copied by value.
 */
static void CanLogger_RxIsrCb(uint8_t channel, const Bsp_CanFdFrameType *frame)
{
    (void) channel;
    BaseType_t xWoken = pdFALSE;

    if (xQueueSendFromISR(g_hCanLoggerRxQueue, frame, &xWoken) != pdPASS) {
        /* Queue full: never stall the bus — count and drop. (mask kept 32-bit; C28x lesson 7.2.12
         * does not bite a uint32_t, but we wrap defensively at the counter's natural width.) */
        g_u32CanLoggerDropCount++;
    } else {
        g_u32CanLoggerRxCount++;
    }
    portYIELD_FROM_ISR(xWoken);
}

/*!
 * @brief Bus-off / error-state callback (interrupt context).
 * @note  Recovery is scheduled on the status cadence by the SysMonitor, not forced in the ISR.
 */
static void CanLogger_BusOffCb(uint8_t channel, Bsp_CanFdErrState_e state)
{
    (void) channel;
    (void) state;   /* state is re-read in CanLogger_Mcan_GetErr() for the STATUS frame */
}

void CanLogger_Mcan_Init(void)
{
    /* Bind the solver-proven bit timing + accept-all RX FIFO0 in the MCAL, then register
     * the app hooks. The MCAL owns every register write; this layer owns only the policy. */
    Mcal_C2000_Mcan_Config(CANLOGGER_MCAN_CLOCK_HZ);
    Bsp_CanFd_EnableRxISR(BSP_CANFD_CH0, &CanLogger_RxIsrCb);
    Bsp_CanFd_EnableBusOffISR(BSP_CANFD_CH0, &CanLogger_BusOffCb);
    Mcal_C2000_Mcan_Start();
}

CanLogger_McanErr_t CanLogger_Mcan_GetErr(void)
{
    CanLogger_McanErr_t e;
    e.state = Mcal_C2000_Mcan_GetErrState();
    e.tec   = Mcal_C2000_Mcan_GetTec();
    e.rec   = Mcal_C2000_Mcan_GetRec();
    return e;
}

void CanLogger_Mcan_RecoverBusOff(void)
{
    Mcal_C2000_Mcan_LeaveBusOff();
}
