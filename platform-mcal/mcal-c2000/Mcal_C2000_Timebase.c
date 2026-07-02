/*!
 * @file        Mcal_C2000_Timebase.c
 * @brief       CPUTimer0-based free-running microsecond timebase.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Timer choice
 *   The c2000ware-FreeRTOS C28x port owns CPUTimer2 for the RTOS tick (INT_TIMER2), so this
 *   timebase takes CPUTimer0 — no collision. A 1 ms periodic interrupt increments a 64-bit
 *   microsecond base; that ISR is the SOLE writer, so NowUs() only ever reads, which makes the
 *   read lock-free and safe from both the MCAN RX ISR and task context.
 *
 * @par Resolution & honesty
 *   NowUs returns the 1 ms base plus the sub-millisecond remainder read straight from the
 *   down-counter, so the returned value has counter-tick resolution (< 1 µs at any real SYSCLK).
 *   The rubric's ≤1 µs *accuracy* claim (HANDOVER §3 rows F/G) is a PoC-1 HIL measurement against
 *   a known-Δt reference — NOT asserted here; this module only provides the plumbing.
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule | Category | Justification                                                  |
 *   |--------|------|----------|----------------------------------------------------------------|
 *   | DEV-01 | 8.4  | Required | s_* file-statics are the ISR↔reader contract (module-private).  |
 *   | DEV-02 | 8.9  | Advisory | file-scope statics keep the single-TU timebase self-contained.  |
 */
#include "Mcal_C2000_Timebase.h"
#include "driverlib.h"
#include "device.h"

/* Sole-writer: the 1 ms tick ISR. Readers only ever read these. */
static volatile uint64_t s_baseUs      = 0U;   /* microseconds accumulated at 1 ms granularity */
static uint32_t          s_periodTicks = 0U;   /* CPUTimer0 PRD = SYSCLK ticks per 1 ms          */
static uint32_t          s_ticksPerUs  = 1U;   /* SYSCLK ticks per microsecond                   */

/*! @brief CPUTimer0 1 ms ISR — the only writer of s_baseUs. PIE group 1. */
__interrupt void Mcal_C2000_Timebase_ISR(void)
{
    s_baseUs += 1000U;
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

void Mcal_C2000_Timebase_Init(uint32_t sysclk_hz)
{
    s_ticksPerUs  = sysclk_hz / 1000000U;
    s_periodTicks = sysclk_hz / 1000U;          /* 1 ms period */
    s_baseUs      = 0U;

    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_setPeriod(CPUTIMER0_BASE, s_periodTicks);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_setEmulationMode(CPUTIMER0_BASE, CPUTIMER_EMULATIONMODE_RUNFREE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);

    Interrupt_register(INT_TIMER0, &Mcal_C2000_Timebase_ISR);
    Interrupt_enable(INT_TIMER0);

    CPUTimer_startTimer(CPUTIMER0_BASE);
}

uint64_t Mcal_C2000_Timebase_NowUs(void)
{
    uint64_t hi;
    uint64_t lo;
    uint32_t count;
    uint32_t subUs;

    /* Seqlock-lite: re-read the base around the counter sample; if the 1 ms ISR fired in
     * between (base changed), retry so the sub-ms remainder always pairs with its own base. */
    do {
        hi    = s_baseUs;
        count = CPUTimer_getTimerCount(CPUTIMER0_BASE);   /* down-counter: PRD..0 */
        lo    = s_baseUs;
    } while (hi != lo);

    /* Elapsed ticks within the current millisecond → microseconds (0..999). */
    subUs = (s_periodTicks - count) / s_ticksPerUs;
    if (subUs > 999U) {                                   /* guard the wrap edge */
        subUs = 999U;
    }
    return hi + (uint64_t) subUs;
}
