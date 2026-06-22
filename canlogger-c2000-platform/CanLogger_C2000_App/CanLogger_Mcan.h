/*!
 * @file        CanLogger_Mcan.h
 * @brief       CAN-FD capture front-end — MCAN init + ISR→queue hand-off.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-U01
 *
 * @par Interfaces Provided
 *   - CanLogger_Mcan_Init()    : configure MCAN-FD accept-all, register the RX/bus-off hooks
 *   - g_hCanLoggerRxQueue      : QueueHandle_t — ISR posts frames here, the drain task reads
 *   - CanLogger_Mcan_GetErr()  : snapshot TEC/REC + error state for the STATUS frame
 *   - CanLogger_Mcan_RecoverBusOff() : leave bus-off (ISO 11898 fault confinement)
 *
 * @par FreeRTOS Interaction
 *   The RX ISR posts Bsp_CanFdFrameType BY VALUE to g_hCanLoggerRxQueue with xQueueSendFromISR.
 *   Queue full → drop_count++ (the bus is never blocked). MCAL holds no FreeRTOS headers; the
 *   queue post happens in the app-level callback registered through Bsp_CanFd_inf.h.
 *
 * @defgroup CanLogger_Mcan CAN-FD Capture Front-End
 */
#ifndef CANLOGGER_MCAN_H
#define CANLOGGER_MCAN_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "Bsp_CanFd_inf.h"

/*! @brief RX queue: MCAN ISR (producer) → CanLogger_Drain_Task (consumer). */
extern QueueHandle_t g_hCanLoggerRxQueue;

/*! @brief Frames dropped because the RX queue was full (surfaced in STATUS). */
extern volatile uint32_t g_u32CanLoggerDropCount;
/*! @brief Frames accepted into the RX queue (surfaced in STATUS). */
extern volatile uint32_t g_u32CanLoggerRxCount;

/*! @brief MCAN error snapshot for the STATUS frame. */
typedef struct {
    Bsp_CanFdErrState_e state;   /*!< active / passive / bus-off */
    uint8_t             tec;     /*!< transmit error counter     */
    uint8_t             rec;     /*!< receive error counter      */
} CanLogger_McanErr_t;

/*!
 * @brief Configure MCAN-FD (accept-all → RX FIFO0) and register the capture hooks.
 * @pre   Called from CanLoggerOs_Init AFTER g_hCanLoggerRxQueue is created.
 */
void CanLogger_Mcan_Init(void);

/*! @brief Read the current MCAN error counters + state (for STATUS). */
CanLogger_McanErr_t CanLogger_Mcan_GetErr(void);

/*! @brief Command MCAN to leave bus-off and resume NORMAL operation. */
void CanLogger_Mcan_RecoverBusOff(void);

#endif /* CANLOGGER_MCAN_H */
