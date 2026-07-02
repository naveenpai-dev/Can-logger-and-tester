/*!
 * @file        Mcal_C2000_Mcan.c
 * @brief       MCAN-FD MCAL for F280039C — accept-all capture, TX buffer path, bus-off readout.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-07-02
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Design
 *   The chip-specific end of the CAN-FD seam. It configures MCAN for a true line logger:
 *   NO id filters installed, so every frame is a "non-matching frame" routed to RX FIFO0
 *   (accept-all). The RX ISR drains FIFO0, normalizes each element into a Bsp_CanFdFrameType,
 *   stamps it from the shared microsecond timebase, and hands it to the app callback registered
 *   through Bsp_CanFd_EnableRxISR — which does the ISR-safe queue post. TX (for the active UDS
 *   tester) copies one frame into a dedicated TX buffer and requests transmission synchronously.
 *
 * @par C28x width discipline (platform lesson 7.2.12)
 *   MCAN message-RAM data is exposed by driverlib as uint16_t[] (one byte per 16-bit cell). The
 *   seam's Bsp_CanFdFrameType.data is uint8_t[64] (also 16-bit cells on C28x). Copy is therefore
 *   cell-wise with an explicit &0xFF so only the on-wire byte survives, regardless of cell width.
 *
 * @par Message RAM budget (F280039C MCAN, 2 KB)
 *   FIFO0 = 16 × 64 B (capture), TX buffers = 4 × 64 B, no FIFO1 / RX buffers / event FIFO.
 *   16*18 + 4*18 = 360 words < 512-word RAM. Deeper FIFO0 is a bench-tuning knob (row J).
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule | Category | Justification                                                    |
 *   |--------|------|----------|------------------------------------------------------------------|
 *   | DEV-01 | 8.4  | Required | s_* statics are the ISR↔module callback/handle contract.          |
 *   | DEV-02 | 11.4 | Advisory | MCANA_DRIVER_BASE is a fixed HW address (driverlib convention).   |
 */
#include "Mcal_C2000_Mcan.h"
#include "Mcal_C2000_Timebase.h"
#include "Platform_CanLogger_Cfg.h"
#include "driverlib.h"
#include "device.h"
#include <string.h>

/* ── Message RAM layout (words), derived like the TI mcan examples ───────────*/
#define MCAL_MCAN_STD_FILTER_NUM        (0U)      /* accept-all → no std id filters   */
#define MCAL_MCAN_EXT_FILTER_NUM        (0U)      /* accept-all → no ext id filters   */
#define MCAL_MCAN_FIFO0_NUM             (16U)     /* capture FIFO depth (elements)    */
#define MCAL_MCAN_FIFO0_ELEM            (MCAN_ELEM_SIZE_64BYTES)
#define MCAL_MCAN_TXBUF_NUM             (4U)      /* dedicated TX buffers (tester)    */
#define MCAL_MCAN_TXBUF_ELEM            (MCAN_ELEM_SIZE_64BYTES)

#define MCAL_MCAN_STD_FILT_ADDR         (0x0U)
#define MCAL_MCAN_EXT_FILT_ADDR         (MCAL_MCAN_STD_FILT_ADDR + \
                                         (MCAL_MCAN_STD_FILTER_NUM * MCANSS_STD_ID_FILTER_SIZE_WORDS * 4U))
#define MCAL_MCAN_FIFO0_ADDR            (MCAL_MCAN_EXT_FILT_ADDR + \
                                         (MCAL_MCAN_EXT_FILTER_NUM * MCANSS_EXT_ID_FILTER_SIZE_WORDS * 4U))
#define MCAL_MCAN_TXBUF_ADDR            (MCAL_MCAN_FIFO0_ADDR + \
                                         (MCAN_getMsgObjSize(MCAL_MCAN_FIFO0_ELEM) * 4U * MCAL_MCAN_FIFO0_NUM))

/* ── ISR↔module contract ─────────────────────────────────────────────────────*/
static Bsp_CanFdIsrCbType    s_rxCb     = NULL;
static Bsp_CanFdBusOffCbType s_busOffCb = NULL;
static uint8_t               s_txBufIdx = 0U;    /* round-robin over the 4 TX buffers */

/*! @brief DLC (0..15) → payload byte count (ISO 11898-1 CAN-FD length coding). */
static uint16_t Mcal_Mcan_DlcToLen(uint32_t dlc)
{
    static const uint16_t k[16] = { 0U,1U,2U,3U,4U,5U,6U,7U,8U,12U,16U,20U,24U,32U,48U,64U };
    return k[dlc & 0x0FU];
}

/*! @brief Payload byte count → smallest DLC that carries it (CAN-FD length coding). */
static uint32_t Mcal_Mcan_LenToDlc(uint8_t len)
{
    uint32_t dlc;
    if (len <= 8U)       { dlc = (uint32_t) len; }
    else if (len <= 12U) { dlc = 9U;  }
    else if (len <= 16U) { dlc = 10U; }
    else if (len <= 20U) { dlc = 11U; }
    else if (len <= 24U) { dlc = 12U; }
    else if (len <= 32U) { dlc = 13U; }
    else if (len <= 48U) { dlc = 14U; }
    else                 { dlc = 15U; }
    return dlc;
}

/*! @brief Normalize a raw MCAN RX element into the portable seam frame. */
static void Mcal_Mcan_Normalize(const MCAN_RxBufElement *e, Bsp_CanFdFrameType *f)
{
    uint16_t n;
    uint16_t i;

    f->ts_us = Mcal_C2000_Timebase_NowUs();
    if (e->xtd != 0U) {
        f->id    = e->id & 0x1FFFFFFFU;              /* 29-bit extended, right-aligned */
        f->flags = BSP_CANFD_FLAG_IDE;
    } else {
        f->id    = (e->id >> 18U) & 0x7FFU;          /* 11-bit std left-aligned in [28:18] */
        f->flags = 0U;
    }
    if (e->fdf != 0U) { f->flags |= BSP_CANFD_FLAG_FDF; }
    if (e->brs != 0U) { f->flags |= BSP_CANFD_FLAG_BRS; }
    if (e->esi != 0U) { f->flags |= BSP_CANFD_FLAG_ESI; }

    n = Mcal_Mcan_DlcToLen(e->dlc);
    if (n > CANLOGGER_MAX_DLEN) { n = CANLOGGER_MAX_DLEN; }
    f->dlen = (uint8_t) n;
    f->chan = BSP_CANFD_CH0;
    for (i = 0U; i < n; i++) {
        f->data[i] = (uint8_t) (e->data[i] & 0xFFU); /* one byte per 16-bit cell */
    }
}

/*! @brief MCAN interrupt (line 1): drain FIFO0 → callback; surface bus-off/error state. PIE grp 9. */
__interrupt void Mcal_C2000_Mcan_ISR(void)
{
    uint32_t          status = MCAN_getIntrStatus(MCANA_DRIVER_BASE);
    MCAN_RxFIFOStatus fifo;
    MCAN_RxBufElement elem;
    Bsp_CanFdFrameType frame;

    /* Drain every element currently in RX FIFO0 (accept-all capture). */
    if ((status & (uint32_t) MCAN_INTR_SRC_RX_FIFO0_NEW_MSG) != 0U) {
        fifo.num = MCAN_RX_FIFO_NUM_0;
        MCAN_getRxFIFOStatus(MCANA_DRIVER_BASE, &fifo);
        while (fifo.fillLvl != 0U) {
            MCAN_readMsgRam(MCANA_DRIVER_BASE, MCAN_MEM_TYPE_FIFO,
                            fifo.getIdx, MCAN_RX_FIFO_NUM_0, &elem);
            (void) MCAN_writeRxFIFOAck(MCANA_DRIVER_BASE, MCAN_RX_FIFO_NUM_0, fifo.getIdx);
            if (s_rxCb != NULL) {
                Mcal_Mcan_Normalize(&elem, &frame);
                s_rxCb(BSP_CANFD_CH0, &frame);
            }
            fifo.num = MCAN_RX_FIFO_NUM_0;
            MCAN_getRxFIFOStatus(MCANA_DRIVER_BASE, &fifo);
        }
    }

    /* Bus-off / error-passive edge → notify the app (recovery is scheduled off the ISR). */
    if ((status & ((uint32_t) MCAN_INTR_SRC_BUS_OFF_STATUS |
                   (uint32_t) MCAN_INTR_SRC_ERR_PASSIVE)) != 0U) {
        if (s_busOffCb != NULL) {
            s_busOffCb(BSP_CANFD_CH0, Mcal_C2000_Mcan_GetErrState());
        }
    }

    MCAN_clearIntrStatus(MCANA_DRIVER_BASE, status);
    MCAN_clearInterrupt(MCANA_DRIVER_BASE, MCAN_INTR_LINE_NUM_1);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}

void Mcal_C2000_Mcan_Config(uint32_t mcanClockHz)
{
    MCAN_InitParams         initParams;
    MCAN_ConfigParams       configParams;
    MCAN_MsgRAMConfigParams ramParams;
    MCAN_BitTimingParams    bitTimes;

    (void) mcanClockHz;   /* informational; the divider knob sets the actual clock (CLG-04) */

    (void) memset(&initParams, 0, sizeof(initParams));
    (void) memset(&configParams, 0, sizeof(configParams));
    (void) memset(&ramParams, 0, sizeof(ramParams));
    (void) memset(&bitTimes, 0, sizeof(bitTimes));

    /* MCAN pins + functional clock (see CANLOGGER_MCAN_CLK_DIV note in the cfg). */
    GPIO_setPinConfig(DEVICE_GPIO_CFG_MCANRXA);
    GPIO_setPinConfig(DEVICE_GPIO_CFG_MCANTXA);
    SysCtl_setMCANClk((SysCtl_MCANClkDivider) CANLOGGER_MCAN_CLK_DIV);

    /* FD + BRS enabled; auto-retransmit ON (a tester expects it); TDC per config. */
    initParams.fdMode          = 1U;
    initParams.brsEnable       = 1U;
    initParams.darEnable       = 0U;   /* 0 = automatic retransmission ENABLED */
    initParams.tdcEnable       = (CANLOGGER_MCAN_TDC_ENABLE != 0U) ? 1U : 0U;
    initParams.tdcConfig.tdcf  = 0xAU;
    initParams.tdcConfig.tdco  = 0x6U;
    initParams.emulationEnable = 1U;
    initParams.wdcPreload      = 0xFFU;

    /* Accept-all: no filter list; non-matching std/ext frames land in FIFO0 (anfs/anfe = 0).
     * Remote frames are NOT rejected — a line logger records them too (rrfs/rrfe = 0). */
    configParams.filterConfig.anfs = 0U;   /* accept non-matching std → FIFO0 */
    configParams.filterConfig.anfe = 0U;   /* accept non-matching ext → FIFO0 */
    configParams.filterConfig.rrfs = 0U;   /* keep std remote frames          */
    configParams.filterConfig.rrfe = 0U;   /* keep ext remote frames          */
    configParams.tsPrescalar       = 0xFU;
    configParams.tsSelect          = 0U;
    configParams.timeoutSelect     = MCAN_TIMEOUT_SELECT_CONT;
    configParams.timeoutPreload    = 0xFFFFU;
    configParams.timeoutCntEnable  = 0U;

    ramParams.flssa            = MCAL_MCAN_STD_FILT_ADDR;
    ramParams.lss              = MCAL_MCAN_STD_FILTER_NUM;
    ramParams.flesa            = MCAL_MCAN_EXT_FILT_ADDR;
    ramParams.lse              = MCAL_MCAN_EXT_FILTER_NUM;
    ramParams.rxFIFO0startAddr = MCAL_MCAN_FIFO0_ADDR;
    ramParams.rxFIFO0size      = MCAL_MCAN_FIFO0_NUM;
    ramParams.rxFIFO0waterMark = 1U;
    ramParams.rxFIFO0OpMode    = 0U;                    /* blocking mode */
    ramParams.rxFIFO0ElemSize  = MCAL_MCAN_FIFO0_ELEM;
    ramParams.txStartAddr      = MCAL_MCAN_TXBUF_ADDR;
    ramParams.txBufNum         = MCAL_MCAN_TXBUF_NUM;
    ramParams.txFIFOSize       = 0U;
    ramParams.txBufMode        = 0U;
    ramParams.txBufElemSize    = MCAL_MCAN_TXBUF_ELEM;

    /* Bit timing: config holds HUMAN tq values; MCAN_setBitTime writes raw register fields
     * (hardware adds 1), so subtract 1 here. See Platform_CanLogger_Cfg.h CANLOGGER_BT_*. */
    bitTimes.nomRatePrescalar   = CANLOGGER_BT_NOM_BRP   - 1U;
    bitTimes.nomTimeSeg1        = CANLOGGER_BT_NOM_TSEG1 - 1U;
    bitTimes.nomTimeSeg2        = CANLOGGER_BT_NOM_TSEG2 - 1U;
    bitTimes.nomSynchJumpWidth  = CANLOGGER_BT_NOM_SJW   - 1U;
    bitTimes.dataRatePrescalar  = CANLOGGER_BT_DAT_BRP   - 1U;
    bitTimes.dataTimeSeg1       = CANLOGGER_BT_DAT_TSEG1 - 1U;
    bitTimes.dataTimeSeg2       = CANLOGGER_BT_DAT_TSEG2 - 1U;
    bitTimes.dataSynchJumpWidth = CANLOGGER_BT_DAT_SJW   - 1U;

    while (MCAN_isMemInitDone(MCANA_DRIVER_BASE) == false) { /* wait for msg-RAM init */ }

    MCAN_setOpMode(MCANA_DRIVER_BASE, MCAN_OPERATION_MODE_SW_INIT);
    while (MCAN_getOpMode(MCANA_DRIVER_BASE) != MCAN_OPERATION_MODE_SW_INIT) { }

    MCAN_init(MCANA_DRIVER_BASE, &initParams);
    MCAN_config(MCANA_DRIVER_BASE, &configParams);
    MCAN_setBitTime(MCANA_DRIVER_BASE, &bitTimes);
    MCAN_msgRAMConfig(MCANA_DRIVER_BASE, &ramParams);

#if (CANLOGGER_MCAN_LOOPBACK != 0U)
    /* PoC-0 self-test: route TX→RX on-chip; no bus, no transceiver, no wiring. */
    MCAN_lpbkModeEnable(MCANA_DRIVER_BASE, MCAN_LPBK_MODE_INTERNAL, true);
#endif
}

void Mcal_C2000_Mcan_Start(void)
{
    /* Interrupt plumbing: RX FIFO0 new-message + bus-off/error on line 1 (INT_MCANA_1). */
    MCAN_enableIntr(MCANA_DRIVER_BASE,
                    (uint32_t) MCAN_INTR_SRC_RX_FIFO0_NEW_MSG |
                    (uint32_t) MCAN_INTR_SRC_BUS_OFF_STATUS   |
                    (uint32_t) MCAN_INTR_SRC_ERR_PASSIVE, 1U);
    MCAN_selectIntrLine(MCANA_DRIVER_BASE,
                        (uint32_t) MCAN_INTR_SRC_RX_FIFO0_NEW_MSG |
                        (uint32_t) MCAN_INTR_SRC_BUS_OFF_STATUS   |
                        (uint32_t) MCAN_INTR_SRC_ERR_PASSIVE, MCAN_INTR_LINE_NUM_1);
    MCAN_enableIntrLine(MCANA_DRIVER_BASE, MCAN_INTR_LINE_NUM_1, 1U);

    Interrupt_register(INT_MCANA_1, &Mcal_C2000_Mcan_ISR);
    Interrupt_enable(INT_MCANA_1);

    MCAN_setOpMode(MCANA_DRIVER_BASE, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_getOpMode(MCANA_DRIVER_BASE) != MCAN_OPERATION_MODE_NORMAL) { }
}

int Bsp_CanFd_Transmit(uint8_t channel, const Bsp_CanFdTxFrameType *frame)
{
    MCAN_TxBufElement tx;
    uint16_t i;
    uint8_t  idx;

    (void) channel;
    if (frame == NULL) { return -1; }

    (void) memset(&tx, 0, sizeof(tx));
    if ((frame->flags & BSP_CANFD_FLAG_IDE) != 0U) {
        tx.id  = frame->id & 0x1FFFFFFFU;            /* extended, right-aligned */
        tx.xtd = 1U;
    } else {
        tx.id  = (frame->id & 0x7FFU) << 18U;        /* std, left-aligned into [28:18] */
        tx.xtd = 0U;
    }
    tx.rtr = 0U;
    tx.esi = 0U;
    tx.brs = ((frame->flags & BSP_CANFD_FLAG_BRS) != 0U) ? 1U : 0U;
    tx.fdf = ((frame->flags & BSP_CANFD_FLAG_FDF) != 0U) ? 1U : 0U;
    tx.efc = 0U;                                     /* no TX event FIFO */
    tx.dlc = Mcal_Mcan_LenToDlc(frame->dlen);
    tx.mm  = 0U;
    for (i = 0U; i < frame->dlen; i++) {
        tx.data[i] = (uint16_t) (frame->data[i] & 0xFFU);
    }

    /* Round-robin the dedicated TX buffers; write to message RAM and request send. */
    idx = s_txBufIdx;
    s_txBufIdx = (uint8_t) ((s_txBufIdx + 1U) % MCAL_MCAN_TXBUF_NUM);
    MCAN_writeMsgRam(MCANA_DRIVER_BASE, MCAN_MEM_TYPE_BUF, (uint32_t) idx, &tx);
    (void) MCAN_txBufAddReq(MCANA_DRIVER_BASE, (uint32_t) idx);
    return 0;
}

void Bsp_CanFd_EnableRxISR(uint8_t channel, Bsp_CanFdIsrCbType cb)
{
    (void) channel;
    s_rxCb = cb;
}

void Bsp_CanFd_EnableBusOffISR(uint8_t channel, Bsp_CanFdBusOffCbType cb)
{
    (void) channel;
    s_busOffCb = cb;
}

Bsp_CanFdErrState_e Mcal_C2000_Mcan_GetErrState(void)
{
    MCAN_ProtocolStatus ps;
    Bsp_CanFdErrState_e st;
    MCAN_getProtocolStatus(MCANA_DRIVER_BASE, &ps);
    if (ps.busOffStatus != 0U) {
        st = BSP_CANFD_ERR_BUSOFF;
    } else if (ps.errPassive != 0U) {
        st = BSP_CANFD_ERR_PASSIVE;
    } else {
        st = BSP_CANFD_ERR_ACTIVE;
    }
    return st;
}

uint8_t Mcal_C2000_Mcan_GetTec(void)
{
    MCAN_ErrCntStatus ec;
    MCAN_getErrCounters(MCANA_DRIVER_BASE, &ec);
    return (uint8_t) (ec.transErrLogCnt & 0xFFU);
}

uint8_t Mcal_C2000_Mcan_GetRec(void)
{
    MCAN_ErrCntStatus ec;
    MCAN_getErrCounters(MCANA_DRIVER_BASE, &ec);
    return (uint8_t) (ec.recErrCnt & 0xFFU);
}

void Mcal_C2000_Mcan_LeaveBusOff(void)
{
    /* Re-entering NORMAL from bus-off restarts the 128×11-bit recovery sequence (ISO 11898-1). */
    MCAN_setOpMode(MCANA_DRIVER_BASE, MCAN_OPERATION_MODE_NORMAL);
}
