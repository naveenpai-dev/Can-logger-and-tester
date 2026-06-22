/*!
 * @file        FreeRTOSConfig.h
 * @brief       FreeRTOS kernel configuration for the CAN-FD Logger atelier (C28x / F280039C).
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Delegation
 *   Heap / clock / priorities come from platform-config/canlogger/Platform_OS_Cfg.h so this
 *   file is identical in shape across ateliers. Do not hardcode those here — change the Cfg.
 *
 * @par C28x specifics (platform lesson 7.2.5)
 *   - configAPPLICATION_ALLOCATED_HEAP = 1: heap placed in a named RAMGS section by the app,
 *     NOT in .bss (omitting this silently corrupts globals).
 *   - configUSE_PORT_OPTIMISED_TASK_SELECTION = 0: C28x has no CLZ.
 *   - Stack sizes are in WORDS.
 *
 * @par MISRA-C:2012 Deviations
 *   FreeRTOS config macros are preprocessor-evaluated; they are intentionally NOT type-cast
 *   (a cast inside #if is a hard compile-break). This is the documented exception to the
 *   "cast every #define" house rule.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "Platform_OS_Cfg.h"

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configCPU_CLOCK_HZ                      CANLOGGEROS_CPU_CLOCK_HZ
#define configTICK_RATE_HZ                      CANLOGGEROS_TICK_RATE_HZ
#define configMAX_PRIORITIES                    CANLOGGEROS_MAX_PRIORITIES
#define configMINIMAL_STACK_SIZE                128
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   CANLOGGEROS_HEAP_SIZE_BYTES
#define configAPPLICATION_ALLOCATED_HEAP        1

#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               4

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0

#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

/* Map configASSERT to the atelier safe-state (declared in CanLoggerOs_Init.h). */
extern void CanLoggerOs_SafeState(const char *file, int line);
#define configASSERT(x) if ((x) == 0) CanLoggerOs_SafeState(__FILE__, __LINE__)

#endif /* FREERTOS_CONFIG_H */
