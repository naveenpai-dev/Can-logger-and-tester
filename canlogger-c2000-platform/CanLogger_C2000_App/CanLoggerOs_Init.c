/*!
 * @file        CanLoggerOs_Init.c
 * @brief       OS bring-up, FreeRTOS application hooks, and the atelier safe state.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Bring-up order (why)
 *   1. Host stream first  — so a STATUS/diagnostic can be emitted even if later steps fail.
 *   2. RX queue          — the ISR↔task contract must exist before MCAN can post.
 *   3. MCAN capture       — registers the ISR hooks against the live queue.
 *   4. Task roster        — drain (high) then sysmon (low).
 *   Every xQueueCreate / xTaskCreate return is checked → SafeState on failure (house rule).
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 */
#include "CanLoggerOs_Init.h"
#include "CanLoggerOs_Tasks.h"
#include "CanLogger_Mcan.h"
#include "CanLogger_HostStream.h"
#include "Platform_OS_Cfg.h"
#include "Platform_CanLogger_Cfg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#if (CANLOGGER_UDS_ENABLE != 0U)
#include "CanLogger_UdsClient.h"
#endif
#if (CANLOGGER_POC0_ENABLE != 0U)
#include "CanLogger_Poc0.h"
#endif

/* heap_4 placed by the app into a named RAMGS section (configAPPLICATION_ALLOCATED_HEAP=1). */
#pragma DATA_SECTION(ucHeap, ".freertos_heap")
uint8_t ucHeap[CANLOGGEROS_HEAP_SIZE_BYTES];

void CanLoggerOs_Init(void)
{
    CanLogger_HostStream_Init();

    g_hCanLoggerRxQueue = xQueueCreate(CANLOGGER_RXQUEUE_DEPTH, sizeof(Bsp_CanFdFrameType));
    configASSERT(g_hCanLoggerRxQueue != NULL);

    CanLogger_Mcan_Init();

    if (xTaskCreate(CanLogger_Drain_Task, "drain", CANLOGGEROS_STACK_DRAIN, NULL,
                    CANLOGGEROS_PRIO_DRAIN, NULL) != pdPASS) {
        CanLoggerOs_SafeState(__FILE__, __LINE__);
    }
    if (xTaskCreate(CanLogger_SysMon_Task, "sysmon", CANLOGGEROS_STACK_SYSMON, NULL,
                    CANLOGGEROS_PRIO_SYSMON, NULL) != pdPASS) {
        CanLoggerOs_SafeState(__FILE__, __LINE__);
    }

#if (CANLOGGER_UDS_ENABLE != 0U)
    /* Optional tester role: the UDS client task drives diagnostic requests on the captured bus.
     * Created AFTER its request queue exists; the drain task feeds it captured response frames. */
    CanLogger_UdsClient_Init();
    if (xTaskCreate(CanLogger_UdsClient_Task, "uds", CANLOGGEROS_STACK_UDS, NULL,
                    CANLOGGEROS_PRIO_UDS, NULL) != pdPASS) {
        CanLoggerOs_SafeState(__FILE__, __LINE__);
    }
#endif

#if (CANLOGGER_POC0_ENABLE != 0U)
    /* PoC-0 self-test (internal loopback): an on-chip responder answers the tester's requests, and
     * a driver task issues the request sequence — both roles exercised with no bus or second node. */
    CanLogger_Poc0_Init();
    if (xTaskCreate(CanLogger_Poc0_ResponderTask, "poc-resp", CANLOGGEROS_STACK_POC_RESP, NULL,
                    CANLOGGEROS_PRIO_POC_RESP, NULL) != pdPASS) {
        CanLoggerOs_SafeState(__FILE__, __LINE__);
    }
    if (xTaskCreate(CanLogger_Poc0_DriverTask, "poc-drv", CANLOGGEROS_STACK_POC_DRV, NULL,
                    CANLOGGEROS_PRIO_POC_DRV, NULL) != pdPASS) {
        CanLoggerOs_SafeState(__FILE__, __LINE__);
    }
#endif
}

void CanLoggerOs_SafeState(const char *file, int line)
{
    (void) file;
    (void) line;
    portDISABLE_INTERRUPTS();
    /* TODO(why): emit a final STATUS/diagnostic over the host stream before halting, so a bench
     *            operator sees the assert site. Kept minimal here to stay ISR/abort-safe. */
    for (;;) {
        /* deterministic halt */
    }
}

/* ── FreeRTOS application hooks (kernel-required) ─────────────────────────────*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    (void) pcTaskName;
    CanLoggerOs_SafeState(__FILE__, __LINE__);
}

void vApplicationMallocFailedHook(void)
{
    CanLoggerOs_SafeState(__FILE__, __LINE__);
}

void vApplicationIdleHook(void)
{
    /* TODO(why): service the watchdog here once Bsp_Wdt is wired — idle starvation then trips
     *            the WDT, which is the intended liveness guarantee. */
}
