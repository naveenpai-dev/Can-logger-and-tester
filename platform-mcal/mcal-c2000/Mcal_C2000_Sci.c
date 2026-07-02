/*!
 * @file        Mcal_C2000_Sci.c
 * @brief       SCIA byte sink over the LaunchPad XDS110 virtual COM.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Pins
 *   SCIA on the F280039C LaunchPad routes to GPIO28 (RX) / GPIO29 (TX), bridged to the host by
 *   the XDS110. GPIO mux + LSPCLK come from Device_init(); this module owns only the SCI block.
 *
 * @par Throughput note (honest)
 *   The configured baud is a host STATUS/observation channel, not the full-load capture sink.
 *   Per HANDOVER §3 row I, a UART cannot absorb worst-case CAN-FD load — the lossless sink is SD
 *   / USB-CDC (roadmap v0.2). PoC-0 uses this stream to *watch* loopback frames, not to prove
 *   zero-loss at rate.
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 */
#include "Mcal_C2000_Sci.h"
#include "Mcal_C2000_Timebase.h"
#include "driverlib.h"
#include "device.h"

void Mcal_C2000_Sci_Init(uint32_t baud)
{
    /* SCIA pin mux (XDS110 virtual COM on the F280039C LaunchPad): GPIO28=RX, GPIO29=TX. */
    GPIO_setPinConfig(GPIO_28_SCIA_RX);
    GPIO_setPinConfig(GPIO_29_SCIA_TX);

    SCI_performSoftwareReset(SCIA_BASE);
    SCI_setConfig(SCIA_BASE, DEVICE_LSPCLK_FREQ, baud,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    SCI_resetChannels(SCIA_BASE);
    SCI_enableFIFO(SCIA_BASE);
    SCI_enableModule(SCIA_BASE);
    SCI_performSoftwareReset(SCIA_BASE);
}

void Mcal_C2000_Sci_Write(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0U; i < len; i++) {
        /* Blocks only while the 16-deep TX FIFO is full; low 8 bits go on the wire. */
        SCI_writeCharBlockingFIFO(SCIA_BASE, (uint16_t) (data[i] & 0xFFU));
    }
}

uint64_t Mcal_C2000_Sci_NowUs(void)
{
    return Mcal_C2000_Timebase_NowUs();
}
