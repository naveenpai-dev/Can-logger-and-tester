/*!
 * @file        CanLogger_IsoTp.h
 * @brief       Minimal ISO 15765-2 (ISO-TP) transport for the UDS client tester role.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par SWE.3 Unit ID
 *   CANLOGGER-U03
 *
 * @par Scope (why a minimal in-tree implementation, not the vendored isotp-c)
 *   This is a *client/tester*-side transport: it SENDS a complete request (single- or multi-frame)
 *   and RECEIVES a complete response. The full bidirectional isotp-c link (two `IsoTpLink`s, its
 *   own `isotp_user_*` HAL, internal poll loop) is heavier than this role needs, and the platform's
 *   vendored `isotp-c/` tree is presently an empty submodule (see DEPENDENCY_LEDGER.md). This shim
 *   implements exactly the ISO 15765-2 §6 frame types a tester exercises:
 *     - Single Frame (SF, PCI type 0)            — request/response ≤ 7 bytes
 *     - First Frame (FF, PCI type 1)             — start of a multi-frame transfer
 *     - Consecutive Frame (CF, PCI type 2)       — body of a multi-frame transfer
 *     - Flow Control (FC, PCI type 3)            — CTS / WAIT / OVFLW, BS + STmin
 *   Classic-CAN escape (CAN-FD DL > 8, ISO 15765-2:2016 §6.5.x extended SF/FF length) is NOT
 *   implemented; requests use the classic 8-byte framing (CANLOGGER_UDS_TX_DL). It is a clean
 *   swap-out point for the vendored isotp-c later — see CanLogger_UdsClient.c TODO.
 *
 * @par FreeRTOS Interaction
 *   No OS calls inside the shim — it is pure framing logic driven by the UDS client task. TX uses
 *   Bsp_CanFd_Transmit (task context only). RX is fed frame-by-frame by the task from captured
 *   frames. The task owns all timing (P2/STmin); the shim never blocks.
 *
 * @par MISRA-C:2012 Deviations
 *   None.
 *
 * @defgroup CanLogger_IsoTp ISO-TP Tester Transport
 */
#ifndef CANLOGGER_ISOTP_H
#define CANLOGGER_ISOTP_H

#include <stdint.h>
#include <stdbool.h>
#include "Bsp_CanFd_inf.h"

/*! @brief Data bytes carried in a classic First Frame (ISO 15765-2 §6.5.2.2): B2..B7 = 6 bytes.
 *         Exposed so the client can seed its TxContinue cursor at the post-FF offset. */
#define CANLOGGER_ISOTP_FF_DATA_LEN     ((uint16_t) 6U)

/*! @brief ISO 15765-2 §6.5.2 protocol-control-information (PCI) frame types (high nibble of B0). */
typedef enum {
    CANLOGGER_ISOTP_PCI_SF = 0x0,   /*!< Single Frame      */
    CANLOGGER_ISOTP_PCI_FF = 0x1,   /*!< First Frame       */
    CANLOGGER_ISOTP_PCI_CF = 0x2,   /*!< Consecutive Frame */
    CANLOGGER_ISOTP_PCI_FC = 0x3    /*!< Flow Control      */
} CanLogger_IsoTp_Pci_e;

/*! @brief ISO 15765-2 §6.5.5.3 flow-status values carried in a Flow Control frame. */
typedef enum {
    CANLOGGER_ISOTP_FS_CTS   = 0x0, /*!< ClearToSend — continue transmitting              */
    CANLOGGER_ISOTP_FS_WAIT  = 0x1, /*!< Wait — sender must hold and await another FC      */
    CANLOGGER_ISOTP_FS_OVFLW = 0x2  /*!< Overflow — receiver cannot accept; abort transfer */
} CanLogger_IsoTp_Fs_e;

/*! @brief Result of feeding one received frame into the reassembly state machine. */
typedef enum {
    CANLOGGER_ISOTP_RX_IDLE       = 0, /*!< frame ignored (not for us / no transfer active) */
    CANLOGGER_ISOTP_RX_INPROGRESS = 1, /*!< multi-frame transfer advancing                  */
    CANLOGGER_ISOTP_RX_DONE       = 2, /*!< a complete PDU is available in the rx buffer    */
    CANLOGGER_ISOTP_RX_SENDFC     = 3, /*!< caller must transmit a Flow Control (CTS) frame */
    CANLOGGER_ISOTP_RX_ERROR      = 4  /*!< protocol/sequence/overflow error — abort        */
} CanLogger_IsoTp_RxResult_e;

/*!
 * @brief Reassembly context for a single inbound ISO-TP PDU (one response at a time).
 * @details Driven by CanLogger_IsoTp_RxFeed. The owning UDS client task holds one of these.
 */
typedef struct {
    uint8_t  *buf;          /*!< caller-owned reassembly buffer            */
    uint16_t  cap;          /*!< capacity of @c buf in bytes               */
    uint16_t  len;          /*!< total declared PDU length (from SF/FF)    */
    uint16_t  got;          /*!< bytes accumulated so far                  */
    uint8_t   next_sn;      /*!< expected next CF sequence number (0..15)  */
    bool      active;       /*!< a transfer is in progress                 */
} CanLogger_IsoTp_Rx_t;

/*!
 * @brief Build the request frame(s) for a complete UDS PDU and transmit via the BSP seam.
 * @details Single Frame if @p len <= 7; otherwise a First Frame followed by — after the peer's
 *          Flow Control CTS is delivered by the task — the Consecutive Frames. For the multi-frame
 *          case this call emits ONLY the First Frame and returns the number of bytes still pending;
 *          the task then calls CanLogger_IsoTp_TxContinue once it has the FC. (A tester request
 *          longer than 7 bytes is rare — multi-DID reads, long routine arguments — but supported.)
 * @param channel  BSP CAN-FD channel.
 * @param req_id   arbitration id for the request (physical or functional).
 * @param data     PDU payload (the UDS service bytes).
 * @param len      PDU length in bytes.
 * @param[out] pending  bytes not yet sent (0 if the whole PDU fit in a single frame).
 * @return 0 on success (frame queued); non-zero on a BSP transmit failure.
 */
int CanLogger_IsoTp_TxStart(uint8_t channel, uint32_t req_id,
                            const uint8_t *data, uint16_t len, uint16_t *pending);

/*!
 * @brief Emit the next block of Consecutive Frames after a Flow Control CTS.
 * @details Sends up to @p block_size CFs (0 = unbounded), honouring the peer's STmin between them
 *          via a caller-supplied delay hook is the task's job — this call sends back-to-back and
 *          relies on the task to pace per FC.STmin. Updates the internal TX cursor.
 * @param channel    BSP CAN-FD channel.
 * @param req_id     arbitration id for the request.
 * @param data       the full PDU payload (same pointer as TxStart).
 * @param len        full PDU length.
 * @param[in,out] sent  bytes already transmitted (advanced by this call).
 * @param[in,out] sn    next sequence number to use (advanced, wraps 0x0..0xF).
 * @param block_size FC block size (0 = send the remainder).
 * @return 0 on success; non-zero on a BSP transmit failure.
 */
int CanLogger_IsoTp_TxContinue(uint8_t channel, uint32_t req_id,
                               const uint8_t *data, uint16_t len,
                               uint16_t *sent, uint8_t *sn, uint8_t block_size);

/*! @brief Reset a reassembly context onto a caller-owned buffer before receiving a response. */
void CanLogger_IsoTp_RxInit(CanLogger_IsoTp_Rx_t *rx, uint8_t *buf, uint16_t cap);

/*!
 * @brief Feed one received CAN-FD frame into the response reassembly state machine.
 * @details Classifies the frame by PCI (ISO 15765-2 §6.5.2) and advances reassembly:
 *          SF → DONE; FF → SENDFC (task must transmit CTS) then CFs → INPROGRESS/DONE. Enforces the
 *          consecutive-frame sequence number; a gap yields RX_ERROR (ISO 15765-2 §6.5.4.2).
 * @param rx     reassembly context.
 * @param frame  the captured frame (already filtered to the response id by the caller).
 * @return one of CanLogger_IsoTp_RxResult_e.
 */
CanLogger_IsoTp_RxResult_e CanLogger_IsoTp_RxFeed(CanLogger_IsoTp_Rx_t *rx,
                                                  const Bsp_CanFdFrameType *frame);

/*!
 * @brief Transmit a Flow Control frame (used by the tester when receiving a multi-frame response).
 * @param channel  BSP CAN-FD channel.
 * @param req_id   arbitration id to send the FC on (the tester's request id).
 * @param fs       flow status (CTS / WAIT / OVFLW).
 * @param bs       block size to grant.
 * @param stmin    separation-time minimum to request (raw ISO-TP STmin byte).
 * @return 0 on success; non-zero on a BSP transmit failure.
 */
int CanLogger_IsoTp_SendFc(uint8_t channel, uint32_t req_id,
                           CanLogger_IsoTp_Fs_e fs, uint8_t bs, uint8_t stmin);

#endif /* CANLOGGER_ISOTP_H */
