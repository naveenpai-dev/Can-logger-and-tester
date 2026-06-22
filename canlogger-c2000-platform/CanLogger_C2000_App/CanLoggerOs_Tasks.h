/*!
 * @file        CanLoggerOs_Tasks.h
 * @brief       Task roster for the CAN-FD Logger atelier.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Roster
 *   | Task                    | Prio (Cfg)              | Trigger        | Job                          |
 *   |-------------------------|-------------------------|----------------|------------------------------|
 *   | CanLogger_Drain_Task    | CANLOGGEROS_PRIO_DRAIN  | RX queue       | queue → host stream (+ SD)   |
 *   | CanLogger_SysMon_Task   | CANLOGGEROS_PRIO_SYSMON | 1000 ms period | STATUS frame, bus-off recover|
 *
 * @defgroup CanLogger_Tasks Task Roster
 */
#ifndef CANLOGGEROS_TASKS_H
#define CANLOGGEROS_TASKS_H

#include "FreeRTOS.h"
#include "task.h"

/*! @brief Drains the RX queue to the host stream as fast as the link allows. */
void CanLogger_Drain_Task(void *pvParameters);

/*! @brief Periodic health: STATUS frame (bus-load/errors/counters) + bus-off recovery. */
void CanLogger_SysMon_Task(void *pvParameters);

#endif /* CANLOGGEROS_TASKS_H */
