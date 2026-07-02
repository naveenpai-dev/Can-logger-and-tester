/*!
 * @file        Mcal_C2000_Sci.h
 * @brief       SCIA byte sink + microsecond timestamp for the framed host stream.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-M03
 *
 * @par Role
 *   The MCAL byte sink CanLogger_HostStream writes framed packets into (SCIA over the LaunchPad
 *   XDS110 virtual COM), plus the microsecond timestamp the host STATUS packet stamps. No
 *   FreeRTOS headers below this seam — the writer blocks on the SCI TX FIFO only, never on the OS.
 *   NowUs delegates to Mcal_C2000_Timebase so the SCI module and the MCAN capture path share ONE
 *   monotonic clock (kept as Mcal_C2000_Sci_NowUs to preserve the CanLogger_HostStream contract).
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 *
 * @defgroup Mcal_C2000_Sci SCIA Byte Sink
 */
#ifndef MCAL_C2000_SCI_H
#define MCAL_C2000_SCI_H

#include <stdint.h>

/*! @brief Bring up SCIA (8N1, TX FIFO) at @p baud over the XDS110 virtual COM. */
void Mcal_C2000_Sci_Init(uint32_t baud);

/*!
 * @brief Blocking write of @p len bytes to SCIA.
 * @details Blocks only while the TX FIFO is full; each source byte occupies a 16-bit C28x cell,
 *          and only its low 8 bits are shifted onto the wire.
 * @param data  source bytes (low 8 bits of each cell are transmitted).
 * @param len   number of bytes to send.
 */
void Mcal_C2000_Sci_Write(const uint8_t *data, uint16_t len);

/*! @brief Monotonic microseconds since bring-up (delegates to the shared timebase). */
uint64_t Mcal_C2000_Sci_NowUs(void);

#endif /* MCAL_C2000_SCI_H */
