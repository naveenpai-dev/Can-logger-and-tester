/*!
 * @file        Mcal_C2000_Timebase.h
 * @brief       Free-running microsecond timebase (CPUTimer2) for capture timestamps.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-M04
 *
 * @par Role
 *   A single monotonic microsecond clock shared by the two things that need one: the MCAN
 *   RX path (per-frame @c ts_us) and the host STATUS packet. CPUTimer2 is dedicated to this
 *   (FreeRTOS on C28x ticks off CPUTimer0/1 — see the c2000ware-FreeRTOS port), so the two
 *   timebases never collide. The counter is a 32-bit down-counter reloaded from a full-scale
 *   period; wrap is accumulated into a 64-bit microsecond count on each read, giving a >500,000-
 *   year range before the u64 itself wraps. No interrupt is used — the read is lock-free and
 *   ISR-safe, which is exactly what the capture ISR needs.
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 *
 * @defgroup Mcal_C2000_Timebase Microsecond Timebase
 */
#ifndef MCAL_C2000_TIMEBASE_H
#define MCAL_C2000_TIMEBASE_H

#include <stdint.h>

/*!
 * @brief Start the free-running microsecond timebase.
 * @param sysclk_hz  CPU/SYSCLK frequency in Hz (CPUTimer2 counts SYSCLK ticks).
 * @pre   Called once from device bring-up, before the first NowUs() read.
 */
void Mcal_C2000_Timebase_Init(uint32_t sysclk_hz);

/*!
 * @brief Current monotonic time in microseconds since Init.
 * @details Lock-free and ISR-safe. Accumulates the down-counter's elapsed ticks into a 64-bit
 *          microsecond total; safe to call from the MCAN RX ISR and from task context.
 * @return microseconds elapsed since Mcal_C2000_Timebase_Init().
 */
uint64_t Mcal_C2000_Timebase_NowUs(void);

#endif /* MCAL_C2000_TIMEBASE_H */
