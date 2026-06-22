/*!
 * @file        CanLogger_HostStream.c
 * @brief       Framed host output — CRC8-protected packets over SCIA (XDS110 virtual COM).
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule | Category | Justification                                          |
 *   |--------|------|----------|--------------------------------------------------------|
 *   | DEV-01 | 8.9  | Advisory | DLC2LEN table is module-local, single translation unit. |
 */
#include "CanLogger_HostStream.h"
#include "Mcal_C2000_Sci.h"          /* MCAL byte sink — no FreeRTOS headers */
#include "Platform_CanLogger_Cfg.h"
#include <string.h>

/*! @brief SAE-J1850 poly 0x07 CRC8 over a byte span (matches the host decoder). */
static uint8_t CanLogger_Crc8(const uint8_t *b, uint16_t n)
{
    uint8_t c = 0U;
    while (n-- != 0U) {
        c ^= *b++;
        for (uint8_t k = 0U; k < 8U; k++) {
            c = (uint8_t) (((c & 0x80U) != 0U) ? ((uint16_t) (c << 1) ^ 0x07U)
                                               : (uint16_t) (c << 1));
        }
    }
    return c;
}

static void CanLogger_SendPacket(uint8_t type, const uint8_t *payload, uint8_t len)
{
    const uint8_t preamble[2] = { 0xAAU, 0x55U };
    const uint8_t header[2]   = { type, len };

    Mcal_C2000_Sci_Write(preamble, 2U);
    Mcal_C2000_Sci_Write(header, 2U);
    Mcal_C2000_Sci_Write(payload, len);

    /* crc over [type,len,payload] — exactly the host's check window. */
    uint8_t c = CanLogger_Crc8(header, 2U);
    for (uint8_t i = 0U; i < len; i++) {
        c ^= payload[i];
        for (uint8_t k = 0U; k < 8U; k++) {
            c = (uint8_t) (((c & 0x80U) != 0U) ? ((uint16_t) (c << 1) ^ 0x07U)
                                               : (uint16_t) (c << 1));
        }
    }
    Mcal_C2000_Sci_Write(&c, 1U);
}

void CanLogger_HostStream_Init(void)
{
    Mcal_C2000_Sci_Init(CANLOGGER_SCI_BAUD);
}

void CanLogger_HostStream_SendFrame(const Bsp_CanFdFrameType *f)
{
    uint8_t p[15 + 64];
    (void) memcpy(&p[0], &f->ts_us, 8U);
    (void) memcpy(&p[8], &f->id, 4U);
    p[12] = f->flags;
    p[13] = f->dlen;
    p[14] = f->chan;
    (void) memcpy(&p[15], f->data, f->dlen);
    CanLogger_SendPacket(0U, p, (uint8_t) (15U + f->dlen));
}

void CanLogger_HostStream_SendStatus(uint8_t busload, uint8_t errstate,
                                     uint8_t tec, uint8_t rec,
                                     uint32_t rx, uint32_t drops)
{
    uint8_t p[20];
    uint64_t ts = Mcal_C2000_Sci_NowUs();
    (void) memcpy(&p[0], &ts, 8U);
    p[8]  = busload;
    p[9]  = errstate;
    p[10] = tec;
    p[11] = rec;
    (void) memcpy(&p[12], &rx, 4U);
    (void) memcpy(&p[16], &drops, 4U);
    CanLogger_SendPacket(1U, p, 20U);
}
