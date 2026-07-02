/*!
 * @file        Mcal_C2000_Mcan.h
 * @brief       MCAN-FD MCAL — device-side driver behind the Bsp_CanFd_inf.h seam (F280039C).
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-M01
 *
 * @par Layer
 *   The chip-specific half of the CAN-FD seam. This is the ONLY translation unit that includes
 *   the TI driverlib CAN headers; everything above links against Bsp_CanFd_inf.h. It owns MCAN
 *   message-RAM layout, bit timing, the accept-all filter, the RX ISR (normalize → app callback),
 *   the TX buffer path (Bsp_CanFd_Transmit), and bus-off/error readout. No FreeRTOS headers here.
 *
 * @par Config source
 *   Bit timing, TDC, and the loopback knob come from platform-config/canlogger/Platform_CanLogger_Cfg.h.
 *
 * @par MISRA-C:2012 Deviations
 *   None (public interface).
 *
 * @defgroup Mcal_C2000_Mcan MCAN-FD MCAL
 */
#ifndef MCAL_C2000_MCAN_H
#define MCAL_C2000_MCAN_H

#include <stdint.h>
#include "Bsp_CanFd_inf.h"

/*!
 * @brief Configure the MCAN peripheral: clock divider, pins, bit timing, message RAM, accept-all
 *        RX FIFO0, and (per CANLOGGER_MCAN_LOOPBACK) internal loopback. Leaves MCAN in SW-init.
 * @param mcanClockHz  MCAN functional clock in Hz (informational — the divider is set to reach it).
 */
void Mcal_C2000_Mcan_Config(uint32_t mcanClockHz);

/*! @brief Take MCAN out of SW-init into NORMAL operation and enable RX interrupts. */
void Mcal_C2000_Mcan_Start(void);

/*! @brief Current bus error state (active/passive/bus-off). */
Bsp_CanFdErrState_e Mcal_C2000_Mcan_GetErrState(void);

/*! @brief Transmit Error Counter. */
uint8_t Mcal_C2000_Mcan_GetTec(void);

/*! @brief Receive Error Counter. */
uint8_t Mcal_C2000_Mcan_GetRec(void);

/*! @brief Command MCAN to leave bus-off and resume NORMAL (ISO 11898 fault confinement). */
void Mcal_C2000_Mcan_LeaveBusOff(void);

#endif /* MCAL_C2000_MCAN_H */
