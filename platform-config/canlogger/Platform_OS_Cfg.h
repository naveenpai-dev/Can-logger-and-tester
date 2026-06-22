/*!
 * @file        Platform_OS_Cfg.h
 * @brief       OS knobs for the CAN-FD Logger atelier — FreeRTOSConfig.h delegates here.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Layer position
 *   platform-config/canlogger/ — the single place heap / clock / priorities are set.
 *   `FreeRTOSConfig.h` includes this and forwards the values, so the kernel config file stays
 *   identical across ateliers and only this file changes per product.
 *
 * @par C28x note (why these specific choices)
 *   - Stack sizes are in WORDS on C28x (not bytes).
 *   - heap_4 with configAPPLICATION_ALLOCATED_HEAP = 1: the heap lives in a named RAMGS section,
 *     never in `.bss` — forgetting this silently corrupts every global (platform lesson 7.2.5).
 *
 * @par MISRA-C:2012 Deviations
 * None.
 *
 * @defgroup CanLogger_OsCfg CAN-FD Logger OS Configuration
 */
#ifndef PLATFORM_OS_CFG_H
#define PLATFORM_OS_CFG_H

/* ── Scheduler ───────────────────────────────────────────────────────────────*/
#define CANLOGGEROS_TICK_RATE_HZ        (1000U)        /* 1 ms tick (CPU Timer 2) */
#define CANLOGGEROS_MAX_PRIORITIES      (8U)
#define CANLOGGEROS_CPU_CLOCK_HZ        (120000000UL)  /* F280039C SYSCLK 120 MHz */

/* ── Heap (heap_4, app-allocated into a RAMGS section) ───────────────────────*/
#define CANLOGGEROS_HEAP_SIZE_BYTES     (0x4000U)      /* 16 KB                   */

/* ── Task priorities (higher = more urgent) ──────────────────────────────────
 * Drain must outrank Status: losing a frame is worse than a late health frame. */
#define CANLOGGEROS_PRIO_DRAIN          (5U)           /* CanLogger_Drain_Task    */
#define CANLOGGEROS_PRIO_SD             (3U)           /* CanLogger_Sd_Task       */
#define CANLOGGEROS_PRIO_SYSMON         (1U)           /* CanLogger_SysMon_Task   */

/* ── Task stack budgets (WORDS, C28x) ────────────────────────────────────────*/
#define CANLOGGEROS_STACK_DRAIN         (512U)
#define CANLOGGEROS_STACK_SD            (512U)
#define CANLOGGEROS_STACK_SYSMON        (128U)

#endif /* PLATFORM_OS_CFG_H */
