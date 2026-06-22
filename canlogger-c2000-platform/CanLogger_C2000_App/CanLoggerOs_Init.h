/*!
 * @file        CanLoggerOs_Init.h
 * @brief       OS bring-up + safe-state for the CAN-FD Logger atelier.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @defgroup CanLogger_OsInit OS Bring-up
 */
#ifndef CANLOGGEROS_INIT_H
#define CANLOGGEROS_INIT_H

/*!
 * @brief Create the RX queue and the task roster; wire the capture + host stream.
 * @note  Call once from main() BEFORE vTaskStartScheduler(). On any creation failure it routes
 *        to CanLoggerOs_SafeState (no silent partial bring-up).
 */
void CanLoggerOs_Init(void);

/*!
 * @brief Terminal safe state — disables interrupts and halts deterministically.
 * @param file  __FILE__ of the failing site (from configASSERT / a hook).
 * @param line  __LINE__ of the failing site.
 */
void CanLoggerOs_SafeState(const char *file, int line);

#endif /* CANLOGGEROS_INIT_H */
