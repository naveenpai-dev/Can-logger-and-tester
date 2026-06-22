/*!
 * @file        main.c
 * @brief       CAN-FD Logger atelier entry — device init, OS bring-up, start scheduler.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Flow
 *   Device_init (clock/flash/GPIO via driverlib) → interrupt vector table → CanLoggerOs_Init
 *   (queue + capture + tasks) → vTaskStartScheduler. Control never returns from the scheduler;
 *   if it does, that is a fatal heap/config error → safe state.
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 */
#include "driverlib.h"
#include "device.h"
#include "CanLoggerOs_Init.h"
#include "FreeRTOS.h"
#include "task.h"

void main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    CanLoggerOs_Init();

    vTaskStartScheduler();

    /* Unreachable unless the idle task could not be created (heap exhaustion). */
    CanLoggerOs_SafeState(__FILE__, __LINE__);
}
