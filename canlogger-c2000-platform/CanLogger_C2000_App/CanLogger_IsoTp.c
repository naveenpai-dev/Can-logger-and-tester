/*!
 * @file        CanLogger_IsoTp.c
 * @brief       Minimal ISO 15765-2 tester transport — SF/FF/CF/FC framing for the UDS client.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Design
 *   Pure framing logic, no OS calls. The owning UDS client task drives it: it sends request frames
 *   through Bsp_CanFd_Transmit and feeds captured response frames into CanLogger_IsoTp_RxFeed. All
 *   timing (P2 wait, STmin pacing) lives in the task; the shim only assembles/parses bytes.
 *
 * @par C28x byte note (platform lesson 7.2.12)
 *   On C28x a `uint8_t` occupies a 16-bit cell, so a bare increment is NOT mod-256. Sequence
 *   numbers and length nibbles are masked at every wrap/compose point (& 0x0FU / & 0xFFU) so the
 *   on-wire PCI bytes are exact regardless of the host cell width.
 *
 * @par MISRA-C:2012 Deviations
 *   | ID     | Rule  | Category | Justification                                                |
 *   |--------|-------|----------|--------------------------------------------------------------|
 *   | DEV-01 | 15.5  | Advisory | early return on transmit failure keeps the framing readable. |
 *
 * @startuml CanLogger_IsoTp_Tx
 *   Client -> IsoTp : TxStart(pdu)
 *   alt len <= 7
 *     IsoTp -> Bus : Single Frame
 *   else
 *     IsoTp -> Bus : First Frame
 *     Bus --> Client : Flow Control (CTS)
 *     Client -> IsoTp : TxContinue
 *     IsoTp -> Bus : Consecutive Frames
 *   end
 * @enduml
 */
#include "CanLogger_IsoTp.h"
#include "Platform_CanLogger_Cfg.h"
#include <string.h>

/* ── ISO 15765-2 framing constants ───────────────────────────────────────────*/
#define ISOTP_SF_MAX_DL         ((uint16_t) 7U)    /* classic SF data length (PCI in B0)        */
#define ISOTP_FF_FIRST_DL       CANLOGGER_ISOTP_FF_DATA_LEN /* classic FF carries 6 bytes (B2..B7)*/
#define ISOTP_CF_DL             ((uint16_t) 7U)    /* classic CF carries 7 data bytes (B1..B7)  */
#define ISOTP_SN_MASK           ((uint8_t)  0x0FU)
#define ISOTP_PAD_BYTE          ((uint8_t)  0xAAU) /* ISO-TP unused-byte padding (common choice) */

/*! @brief Compose and transmit one classic-CAN frame at CANLOGGER_UDS_TX_DL, padded. */
static int CanLogger_IsoTp_TxRaw(uint8_t channel, uint32_t id,
                                 const uint8_t *bytes, uint8_t n)
{
    Bsp_CanFdTxFrameType f;
    uint8_t i;

    f.id    = id;
    f.chan  = channel;
    f.dlen  = CANLOGGER_UDS_TX_DL;
#if (CANLOGGER_UDS_EXTENDED_ID != 0U)
    f.flags = BSP_CANFD_FLAG_IDE;
#else
    f.flags = 0U;
#endif
    for (i = 0U; i < CANLOGGER_UDS_TX_DL; i++) {
        f.data[i] = (i < n) ? bytes[i] : ISOTP_PAD_BYTE;
    }
    return Bsp_CanFd_Transmit(channel, &f);
}

int CanLogger_IsoTp_TxStart(uint8_t channel, uint32_t req_id,
                            const uint8_t *data, uint16_t len, uint16_t *pending)
{
    uint8_t  frame[8];
    uint16_t i;

    if (len <= ISOTP_SF_MAX_DL) {
        /* Single Frame: B0 = 0x0L (PCI type 0 | length), B1.. = data. ISO 15765-2 §6.5.2.1. */
        frame[0] = (uint8_t) (((uint8_t) CANLOGGER_ISOTP_PCI_SF << 4) | (uint8_t) (len & 0x0FU));
        for (i = 0U; i < len; i++) {
            frame[1U + i] = data[i];
        }
        *pending = 0U;
        return CanLogger_IsoTp_TxRaw(channel, req_id, frame, (uint8_t) (1U + len));
    }

    /* First Frame: B0 = 0x1H, B1 = L (12-bit length split high/low), B2..B7 = first 6 bytes.
     * ISO 15765-2 §6.5.2.2. Caller follows up with TxContinue after the peer's FC. */
    frame[0] = (uint8_t) (((uint8_t) CANLOGGER_ISOTP_PCI_FF << 4)
                          | (uint8_t) ((len >> 8) & 0x0FU));
    frame[1] = (uint8_t) (len & 0xFFU);
    for (i = 0U; i < ISOTP_FF_FIRST_DL; i++) {
        frame[2U + i] = data[i];
    }
    *pending = (uint16_t) (len - ISOTP_FF_FIRST_DL);
    return CanLogger_IsoTp_TxRaw(channel, req_id, frame, 8U);
}

int CanLogger_IsoTp_TxContinue(uint8_t channel, uint32_t req_id,
                               const uint8_t *data, uint16_t len,
                               uint16_t *sent, uint8_t *sn, uint8_t block_size)
{
    uint8_t  frame[8];
    uint8_t  blocks = 0U;

    while (*sent < len) {
        uint16_t remain = (uint16_t) (len - *sent);
        uint16_t chunk  = (remain < ISOTP_CF_DL) ? remain : ISOTP_CF_DL;
        uint16_t i;
        int      rc;

        /* Consecutive Frame: B0 = 0x2N (PCI type 2 | sequence number), B1.. = data.
         * ISO 15765-2 §6.5.2.3. SN starts at 1 and wraps 0xF→0x0. */
        frame[0] = (uint8_t) (((uint8_t) CANLOGGER_ISOTP_PCI_CF << 4) | (*sn & ISOTP_SN_MASK));
        for (i = 0U; i < chunk; i++) {
            frame[1U + i] = data[*sent + i];
        }
        rc = CanLogger_IsoTp_TxRaw(channel, req_id, frame, (uint8_t) (1U + chunk));
        if (rc != 0) {
            return rc;
        }
        *sent = (uint16_t) (*sent + chunk);
        *sn   = (uint8_t)  ((*sn + 1U) & ISOTP_SN_MASK);

        blocks++;
        if ((block_size != 0U) && (blocks >= block_size)) {
            break;   /* block exhausted — caller awaits the next FC */
        }
    }
    return 0;
}

void CanLogger_IsoTp_RxInit(CanLogger_IsoTp_Rx_t *rx, uint8_t *buf, uint16_t cap)
{
    rx->buf     = buf;
    rx->cap     = cap;
    rx->len     = 0U;
    rx->got     = 0U;
    rx->next_sn = 1U;     /* first CF after FF carries SN = 1 */
    rx->active  = false;
}

CanLogger_IsoTp_RxResult_e CanLogger_IsoTp_RxFeed(CanLogger_IsoTp_Rx_t *rx,
                                                  const Bsp_CanFdFrameType *frame)
{
    uint8_t pci;
    uint16_t i;

    if (frame->dlen == 0U) {
        return CANLOGGER_ISOTP_RX_IDLE;
    }
    pci = (uint8_t) ((frame->data[0] >> 4) & 0x0FU);

    switch (pci) {
        case (uint8_t) CANLOGGER_ISOTP_PCI_SF: {
            uint16_t sf_len = (uint16_t) (frame->data[0] & 0x0FU);
            if ((sf_len == 0U) || (sf_len > ISOTP_SF_MAX_DL) || (sf_len > rx->cap)) {
                return CANLOGGER_ISOTP_RX_ERROR;
            }
            for (i = 0U; i < sf_len; i++) {
                rx->buf[i] = frame->data[1U + i];
            }
            rx->len = sf_len;
            rx->got = sf_len;
            rx->active = false;
            return CANLOGGER_ISOTP_RX_DONE;
        }

        case (uint8_t) CANLOGGER_ISOTP_PCI_FF: {
            uint16_t ff_len = (uint16_t) ((((uint16_t) (frame->data[0] & 0x0FU)) << 8)
                                          | (uint16_t) frame->data[1]);
            if ((ff_len <= ISOTP_SF_MAX_DL) || (ff_len > rx->cap)) {
                /* FF must declare > 7 bytes; reject an oversize PDU rather than overrun the buffer
                 * (the task answers with FC.OVFLW via the SENDFC/ERROR split). */
                return CANLOGGER_ISOTP_RX_ERROR;
            }
            rx->len = ff_len;
            rx->got = 0U;
            for (i = 0U; i < ISOTP_FF_FIRST_DL; i++) {
                rx->buf[rx->got++] = frame->data[2U + i];
            }
            rx->next_sn = 1U;
            rx->active  = true;
            return CANLOGGER_ISOTP_RX_SENDFC;   /* task must transmit a Flow Control (CTS) */
        }

        case (uint8_t) CANLOGGER_ISOTP_PCI_CF: {
            uint8_t sn = (uint8_t) (frame->data[0] & ISOTP_SN_MASK);
            uint16_t avail;
            if (!rx->active) {
                return CANLOGGER_ISOTP_RX_IDLE;     /* stray CF, no transfer */
            }
            if (sn != rx->next_sn) {
                rx->active = false;                 /* wrong-sequence-number — abort */
                return CANLOGGER_ISOTP_RX_ERROR;    /* ISO 15765-2 §6.5.4.2 */
            }
            avail = (uint16_t) (rx->len - rx->got);
            for (i = 0U; (i < ISOTP_CF_DL) && (avail > 0U); i++) {
                rx->buf[rx->got++] = frame->data[1U + i];
                avail--;
            }
            rx->next_sn = (uint8_t) ((rx->next_sn + 1U) & ISOTP_SN_MASK);
            if (rx->got >= rx->len) {
                rx->active = false;
                return CANLOGGER_ISOTP_RX_DONE;
            }
            return CANLOGGER_ISOTP_RX_INPROGRESS;
        }

        default:
            /* Flow Control or unknown PCI: the tester drives TX, so an inbound FC is only relevant
             * to a TxContinue cycle and is parsed there by the task — ignore in the RX path. */
            return CANLOGGER_ISOTP_RX_IDLE;
    }
}

int CanLogger_IsoTp_SendFc(uint8_t channel, uint32_t req_id,
                           CanLogger_IsoTp_Fs_e fs, uint8_t bs, uint8_t stmin)
{
    uint8_t frame[3];
    /* Flow Control: B0 = 0x3F (PCI type 3 | flow status), B1 = BlockSize, B2 = STmin.
     * ISO 15765-2 §6.5.2.4. */
    frame[0] = (uint8_t) (((uint8_t) CANLOGGER_ISOTP_PCI_FC << 4) | ((uint8_t) fs & 0x0FU));
    frame[1] = bs;
    frame[2] = stmin;
    return CanLogger_IsoTp_TxRaw(channel, req_id, frame, 3U);
}
