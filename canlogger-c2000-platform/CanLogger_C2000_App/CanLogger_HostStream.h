/*!
 * @file        CanLogger_HostStream.h
 * @brief       Framed host output — AA55 | type | len | payload | crc8 over SCIA.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-U02
 *
 * @par Wire contract (must match host/can_logger_host.py exactly)
 *   AA 55 | type | len | payload | crc8        crc8 = J1850/SAE poly 0x07 over [type,len,payload]
 *     FRAME (0): ts_us(u64) id(u32) flags(u8) dlen(u8) chan(u8) data[dlen]
 *     STATUS(1): ts_us(u64) busload(u8) errstate(u8) tec(u8) rec(u8) rx(u32) drops(u32)
 *
 * @defgroup CanLogger_HostStream Framed Host Output
 */
#ifndef CANLOGGER_HOSTSTREAM_H
#define CANLOGGER_HOSTSTREAM_H

#include <stdint.h>
#include "Bsp_CanFd_inf.h"

/*! @brief Bring up SCIA at CANLOGGER_SCI_BAUD over the XDS110 virtual COM. */
void CanLogger_HostStream_Init(void);

/*! @brief Emit one captured frame as a FRAME(0) packet. */
void CanLogger_HostStream_SendFrame(const Bsp_CanFdFrameType *frame);

/*! @brief Emit a STATUS(1) packet (health/telemetry). */
void CanLogger_HostStream_SendStatus(uint8_t busload, uint8_t errstate,
                                     uint8_t tec, uint8_t rec,
                                     uint32_t rx, uint32_t drops);

#endif /* CANLOGGER_HOSTSTREAM_H */
