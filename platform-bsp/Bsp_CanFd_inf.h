/*!
 * @file        Bsp_CanFd_inf.h
 * @brief       BSP CAN-FD interface — SW hooks the MCAL CAN-FD layer calls into the app.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Purpose
 *   Core-agnostic callback types the application registers to receive CAN-FD frames and
 *   bus-off notifications. The application includes THIS file; the BSP/MCAL implementation
 *   includes Bsp_CanFd.h. No chip guards, no FreeRTOS headers — this is the seam between the
 *   portable platform and the app (cf. the platform's Bsp_Can_inf.h pattern, STKHLD-ARCH-007).
 *
 * @par Integration pattern
 * @code
 *   // In CanLogger_Mcan.c (application layer):
 *   #include "Bsp_CanFd_inf.h"
 *
 *   static void CanLogger_RxIsrCb(uint8_t ch, const Bsp_CanFdFrameType *f)
 *   {
 *       BaseType_t woke = pdFALSE;
 *       xQueueSendFromISR(g_hRxQueue, f, &woke);   // copy-by-value; ISR-safe only
 *       portYIELD_FROM_ISR(woke);
 *   }
 *
 *   void CanLogger_Mcan_Init(void)
 *   {
 *       Bsp_CanFd_EnableRxISR(BSP_CANFD_CH0, CanLogger_RxIsrCb);
 *       Bsp_CanFd_EnableBusOffISR(BSP_CANFD_CH0, CanLogger_BusOffCb);
 *   }
 * @endcode
 *
 * @par Change History
 * | Version | Date       | Author     | Description                          |
 * |---------|------------|------------|--------------------------------------|
 * | 0.1.0   | 2026-06-22 | Naveen Pai | Initial release — CAN-FD capture seam |
 *
 * @par MISRA-C:2012 Deviations
 * None.
 *
 * @defgroup Bsp_CanFd_Inf BSP CAN-FD Interface Hooks
 */
#ifndef BSP_CANFD_INF_H
#define BSP_CANFD_INF_H

#include <stdint.h>

/*! @brief Logical CAN-FD channel indices (board wiring is bound in MCAL). */
#define BSP_CANFD_CH0                   ((uint8_t) 0U)

/*! @brief Frame flag bit positions in Bsp_CanFdFrameType.flags. */
#define BSP_CANFD_FLAG_FDF              ((uint8_t) 0x01U)   /* FD format            */
#define BSP_CANFD_FLAG_BRS              ((uint8_t) 0x02U)   /* bit-rate switch      */
#define BSP_CANFD_FLAG_ESI              ((uint8_t) 0x04U)   /* error-state indicator */
#define BSP_CANFD_FLAG_IDE              ((uint8_t) 0x08U)   /* extended 29-bit id   */

/*! @brief Bus error state reported to the bus-off / status path. */
typedef enum {
    BSP_CANFD_ERR_ACTIVE  = 0,   /*!< error-active (normal)            */
    BSP_CANFD_ERR_PASSIVE = 1,   /*!< error-passive (degraded)         */
    BSP_CANFD_ERR_BUSOFF  = 2    /*!< bus-off (recovery on next cycle) */
} Bsp_CanFdErrState_e;

/*!
 * @brief One received CAN-FD frame, normalized by the MCAL out of the raw RX element.
 * @details Passed BY POINTER into the RX callback; valid only for the call duration.
 *          The callback must copy it (e.g. xQueueSendFromISR by value) before returning.
 */
typedef struct {
    uint64_t ts_us;                         /*!< capture timestamp, microseconds     */
    uint32_t id;                            /*!< CAN id (right-aligned, std or ext)  */
    uint8_t  flags;                         /*!< BSP_CANFD_FLAG_* bitmask            */
    uint8_t  dlen;                          /*!< decoded payload length (0..64)      */
    uint8_t  chan;                          /*!< BSP_CANFD_CH* the frame arrived on  */
    uint8_t  data[64];                      /*!< payload, first @c dlen bytes valid  */
} Bsp_CanFdFrameType;

/*!
 * @brief CAN-FD receive ISR callback type.
 * @details Called from the MCAL RX ISR (interrupt context). Implementation MUST use only
 *          ISR-safe FreeRTOS APIs (xQueueSendFromISR / xTaskNotifyFromISR) — never a blocking
 *          call. Copy @p frame before returning.
 * @param channel  BSP CAN-FD channel index.
 * @param frame    Pointer to the received frame — valid only during the callback.
 */
typedef void (*Bsp_CanFdIsrCbType)(uint8_t channel, const Bsp_CanFdFrameType *frame);

/*!
 * @brief CAN-FD bus-off / error-state notification callback type.
 * @details Same ISR-safety constraints as Bsp_CanFdIsrCbType. The app uses this to schedule
 *          recovery (ISO 11898 fault confinement) and to surface the state in the STATUS frame.
 * @param channel  BSP CAN-FD channel index.
 * @param state    Current bus error state.
 */
typedef void (*Bsp_CanFdBusOffCbType)(uint8_t channel, Bsp_CanFdErrState_e state);

/*!
 * @brief One CAN-FD frame to transmit, handed to the MCAL by an active client (e.g. UDS tester).
 * @details A pure line logger never needs this; an *active* atelier built on the same capture
 *          seam (the UDS client) emits ISO-TP frames through it. The MCAL copies the frame into a
 *          TX buffer/mailbox SYNCHRONOUSLY before returning (cf. C2000 DCAN CAN_sendMessage), so a
 *          caller may reuse @p data immediately. Reuses the FDF/BRS/IDE flag bits above.
 */
typedef struct {
    uint32_t id;                            /*!< CAN id (right-aligned, std or ext)  */
    uint8_t  flags;                         /*!< BSP_CANFD_FLAG_* bitmask            */
    uint8_t  dlen;                          /*!< payload length to send (0..64)      */
    uint8_t  chan;                          /*!< BSP_CANFD_CH* to transmit on        */
    uint8_t  data[64];                      /*!< payload, first @c dlen bytes sent   */
} Bsp_CanFdTxFrameType;

/*!
 * @brief Transmit one CAN-FD frame (blocking only on mailbox availability, never on the bus).
 * @details Provided by the MCAL CAN-FD layer; declared on the seam so an active client links
 *          against the portable contract, not a chip header. MUST copy @p frame into controller
 *          storage before returning (synchronous TX), so the caller can immediately overwrite its
 *          staging buffer (the deterministic compute-ahead TX discipline). NOT callable from ISR
 *          context — the UDS client task owns all transmission.
 * @param channel  BSP CAN-FD channel index (e.g. BSP_CANFD_CH0).
 * @param frame    Frame to transmit; consumed by the call, may be reused afterwards.
 * @return 0 on success (queued to a TX mailbox); non-zero if no mailbox was free.
 */
extern int Bsp_CanFd_Transmit(uint8_t channel, const Bsp_CanFdTxFrameType *frame);

#endif /* BSP_CANFD_INF_H */
