/*!
 * @file        Platform_CanLogger_Cfg.h
 * @brief       Per-atelier knobs for the CAN-FD Logger — the ONLY file an integrator edits.
 * @author      Naveen Pai <naveen.nagesh.pai@gmail.com>
 * @copyright   Copyright (c) 2026 Naveen Pai
 * @version     0.1.0
 * @date        2026-06-22
 *
 * SPDX-License-Identifier: MIT
 *
 * @par Layer position
 *   platform-config/canlogger/ — application-owned configuration. The BSP / MCAL `.c` files
 *   include this header by name; the makefile resolves it via `-Iplatform-config/canlogger`.
 *   Changing a value here re-shapes the atelier without touching the portable layers.
 *
 * @par Worked example — retune for a 40 MHz MCAN clock
 *   Run `python3 host/canfd_bittiming.py --clock 40e6 --nominal 2e6 --data 5e6`, then set the
 *   six CANLOGGER_BT_* values below to the solver's 0%-error result (40 MHz: NOM 15/4/4, DATA 5/2/2).
 *
 * @par Change History
 * | Version | Date       | Author     | Description       |
 * |---------|------------|------------|-------------------|
 * | 0.1.0   | 2026-06-22 | Naveen Pai | Initial release   |
 *
 * @par MISRA-C:2012 Deviations
 * None.
 *
 * @defgroup CanLogger_Cfg CAN-FD Logger Configuration
 */
#ifndef PLATFORM_CANLOGGER_CFG_H
#define PLATFORM_CANLOGGER_CFG_H

#include <stdint.h>

/* ── Target device ───────────────────────────────────────────────────────────
 * F280039C: C28x, single MCAN-FD instance, on-board CAN-FD transceiver (~5 Mbps). */
#define CANLOGGER_DEVICE_F280039C       (1U)

/* ── CAN-FD bit timing — solver output @ MCAN functional clock = 80 MHz, BRP=1 ──
 * NOMINAL 2 Mbps (SP 80.0%) · DATA 5 Mbps (SP 81.2%) · 0% error.
 * These are the (TSEG-1)/(TSEG-2)/(SJW) *human* values; the MCAL subtracts 1 for the register.
 * RE-VERIFY with host/canfd_bittiming.py for YOUR clock before trusting the bus. */
#define CANLOGGER_MCAN_CLOCK_HZ         (80000000UL)
#define CANLOGGER_BT_NOM_BRP            ((uint16_t) 1U)
#define CANLOGGER_BT_NOM_TSEG1          ((uint16_t) 31U)
#define CANLOGGER_BT_NOM_TSEG2          ((uint16_t) 8U)
#define CANLOGGER_BT_NOM_SJW            ((uint16_t) 8U)
#define CANLOGGER_BT_DAT_BRP            ((uint16_t) 1U)
#define CANLOGGER_BT_DAT_TSEG1          ((uint16_t) 12U)
#define CANLOGGER_BT_DAT_TSEG2          ((uint16_t) 3U)
#define CANLOGGER_BT_DAT_SJW            ((uint16_t) 3U)

/* Enable Transmitter Delay Compensation / Secondary Sample Point in the data phase.
 * REQUIRED above ~2 Mbps to tolerate transceiver loop delay. */
#define CANLOGGER_MCAN_TDC_ENABLE       (1U)

/* ── Capture sizing ──────────────────────────────────────────────────────────
 * Ring depth (power of two) sized for a burst at full bus load. The RX queue between
 * the ISR and the drain task is the platform-layer mirror of this ring. */
#define CANLOGGER_RING_DEPTH            ((uint16_t) 1024U)     /* MUST be a power of two */
#define CANLOGGER_MAX_DLEN              ((uint8_t)  64U)       /* CAN-FD max payload     */
#define CANLOGGER_RXQUEUE_DEPTH         ((uint16_t) 64U)       /* ISR → drain-task queue */

/* ── Host stream ─────────────────────────────────────────────────────────────
 * SCIA over the XDS110 virtual COM. 3 Mbaud keeps the link ahead of 5 Mbps capture. */
#define CANLOGGER_SCI_BAUD              (3000000UL)
#define CANLOGGER_STATUS_PERIOD_MS      ((uint16_t) 1000U)     /* STATUS frame cadence   */

/* ── microSD batch writer ────────────────────────────────────────────────────
 * Block-aligned batching: accumulate before f_write to keep FatFS off the hot path. */
#define CANLOGGER_SD_ENABLE             (1U)
#define CANLOGGER_SD_BATCH_BYTES        ((uint16_t) 512U)      /* one block               */

#endif /* PLATFORM_CANLOGGER_CFG_H */
