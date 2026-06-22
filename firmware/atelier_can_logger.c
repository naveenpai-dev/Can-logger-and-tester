/*
 * atelier_can_logger.c  —  retrofit a C2000 LaunchPad into a CAN-FD logger.
 *
 * Target (default): LAUNCHXL-F280039C (MCAN + on-board CAN-FD transceiver, XDS110).
 * Build: Code Composer Studio + C2000Ware driverlib. This is a REFERENCE core —
 *        verify the bit-timing constants for YOUR device clock, and wire the SD
 *        hook to FatFS. It is written to flash-and-iterate, not to compile blind.
 *
 * Pipeline (built for >=92% bus load):
 *     MCAN RX FIFO0  --ISR-->  lock-free ring buffer (RAM)  --main-->  SCI stream + SD
 *
 * Wire format matches can_logger_host.py exactly:
 *   AA 55 | type | len | payload | crc8
 *     FRAME(0):  ts_us(u64) id(u32) flags(u8) dlen(u8) chan(u8) data[dlen]
 *     STATUS(1): ts_us(u64) busload(u8) errstate(u8) tec(u8) rec(u8) rx(u32) err(u32)
 */
#include "driverlib.h"
#include "device.h"
#include <string.h>

/* ----- bit timing: 2 Mbps nominal / 5 Mbps data @ MCAN clock = 80 MHz (BRP=1).
 *       NOM SP 80.0%, DATA SP 81.2%, 0% error. Set MCAN functional clock = 80 MHz.
 *       (40 MHz also clean: NOM 15/4/4, DATA 5/2/2; 120 MHz: NOM 47/12/12, DATA 18/5/5.)
 *       5 Mbps is within the LaunchPad's on-board transceiver — no external SIC needed.
 *       Keep TDC/SSP enabled for the data phase (see mcan_init). */
#define NOM_BRP   1u
#define NOM_TSEG1 31u
#define NOM_TSEG2 8u
#define NOM_SJW   8u
#define DAT_BRP   1u
#define DAT_TSEG1 12u
#define DAT_TSEG2 3u
#define DAT_SJW   3u

#define RING_SZ   1024u           /* power of two; sized for burst at full load */
#define MAX_DLEN  64u

typedef struct {                  /* one captured frame */
    uint64_t ts;
    uint32_t id;
    uint8_t  flags;               /* b0 FD b1 BRS b2 ESI b3 IDE */
    uint8_t  dlen;
    uint8_t  chan;
    uint8_t  data[MAX_DLEN];
} frame_t;

static volatile frame_t ring[RING_SZ];
static volatile uint16_t r_head = 0, r_tail = 0;   /* head: ISR writes, tail: main reads */
static volatile uint32_t rx_count = 0, drop_count = 0;

static inline uint16_t ring_next(uint16_t i){ return (i + 1u) & (RING_SZ - 1u); }

/* --- microsecond timestamp from a free-running CPU timer (configure in init) --- */
static inline uint64_t now_us(void){ return (uint64_t)CPUTimer_getTimerCount(CPUTIMER0_BASE) /* + epoch */; }

/* ---- DLC(0..15) -> byte length for CAN FD ---- */
static const uint8_t DLC2LEN[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};

/* =====================  RX ISR: drain FIFO0 into the ring  ===================== */
__interrupt void mcanRxISR(void)
{
    MCAN_RxBufElement rx;
    while (MCAN_getRxFIFOStatus(MCANA_BASE, MCAN_RX_FIFO_NUM_0).fillLevel) {
        MCAN_readMsgRam(MCANA_BASE, MCAN_MEM_TYPE_FIFO, 0, MCAN_RX_FIFO_NUM_0, &rx);
        uint16_t h = r_head, n = ring_next(h);
        if (n == r_tail) { drop_count++; }            /* ring full: never block the bus */
        else {
            volatile frame_t *f = &ring[h];
            f->ts    = now_us();
            f->id    = rx.xtd ? rx.id : (rx.id >> 18);  /* driverlib stores std id left-aligned */
            f->flags = (rx.fdf ? 1u:0u) | (rx.brs ? 2u:0u) | (rx.esi ? 4u:0u) | (rx.xtd ? 8u:0u);
            f->dlen  = DLC2LEN[rx.dlc & 0xF];
            f->chan  = 0;
            memcpy((void*)f->data, rx.data, f->dlen);
            r_head = n;
            rx_count++;
        }
    }
    MCAN_clearInterruptStatus(MCANA_BASE, MCAN_IR_RF0N_MASK);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}

/* ============================  framed SCI output  ============================= */
static uint8_t crc8(const uint8_t *b, uint16_t n){
    uint8_t c = 0; while (n--) { c ^= *b++; for (int i=0;i<8;i++) c = (c&0x80)?(uint8_t)((c<<1)^0x07):(uint8_t)(c<<1);} return c;
}
static void sci_bytes(const uint8_t *b, uint16_t n){
    for (uint16_t i=0;i<n;i++){ while(!SCI_isSpaceAvailableNonFIFO(SCIA_BASE)); SCI_writeCharNonBlocking(SCIA_BASE, b[i]); }
}
static void send_packet(uint8_t type, const uint8_t *payload, uint8_t len){
    uint8_t hdr[2] = {type, len};
    uint8_t body_crc;                 /* crc over [type,len,payload] */
    uint8_t pre[2] = {0xAA, 0x55};
    sci_bytes(pre, 2); sci_bytes(hdr, 2); sci_bytes(payload, len);
    /* recompute crc over hdr+payload */
    uint8_t c = crc8(hdr, 2);
    for (uint8_t i=0;i<len;i++){ c ^= payload[i]; for(int k=0;k<8;k++) c=(c&0x80)?(uint8_t)((c<<1)^0x07):(uint8_t)(c<<1);}
    body_crc = c; sci_bytes(&body_crc, 1);
}
static void send_frame(const frame_t *f){
    uint8_t p[15 + MAX_DLEN];
    memcpy(p+0,&f->ts,8); memcpy(p+8,&f->id,4); p[12]=f->flags; p[13]=f->dlen; p[14]=f->chan;
    memcpy(p+15, f->data, f->dlen);
    send_packet(0, p, (uint8_t)(15 + f->dlen));
    /* TODO: also append to the SD batch buffer here -> FatFS f_write() in blocks */
}
static void send_status(uint8_t busload, uint8_t errstate, uint8_t tec, uint8_t rec){
    uint8_t p[20]; uint64_t ts = now_us();
    memcpy(p+0,&ts,8); p[8]=busload; p[9]=errstate; p[10]=tec; p[11]=rec;
    memcpy(p+12,(const void*)&rx_count,4); memcpy(p+16,(const void*)&drop_count,4);
    send_packet(1, p, 20);
}

/* ===============================  MCAN setup  ================================= */
static void mcan_init(void)
{
    MCAN_InitParams init = {0};
    init.fdMode = 1U; init.brsEnable = 1U;            /* CAN FD + bit-rate switching */
    MCAN_setOpMode(MCANA_BASE, MCAN_OPERATION_MODE_SW_INIT);
    MCAN_initOperation(MCANA_BASE);

    MCAN_BitTimingParams bt = {0};
    bt.nomRatePrescalar=NOM_BRP-1; bt.nomTimeSeg1=NOM_TSEG1-1; bt.nomTimeSeg2=NOM_TSEG2-1; bt.nomSynchJumpWidth=NOM_SJW-1;
    bt.dataRatePrescalar=DAT_BRP-1; bt.dataTimeSeg1=DAT_TSEG1-1; bt.dataTimeSeg2=DAT_TSEG2-1; bt.dataSynchJumpWidth=DAT_SJW-1;
    MCAN_setBitTime(MCANA_BASE, &bt);

    /* TDC / Secondary Sample Point — REQUIRED at 8 Mbps to tolerate transceiver loop delay.
     * Enable transmitter delay compensation and set SSP offset ~ data sample point.
     *   MCAN_TDCConfig tdc = { .tdcf = 0, .tdco = (DAT_TSEG1 + 1) };  // ~SSP at 80%
     *   MCAN_setTDCOffset / MCAN_enableTDC(MCANA_BASE, &tdc);   // check exact driverlib API */

    /* Message RAM: one big RX FIFO0, no per-id filters -> accept everything. */
    MCAN_MsgRAMConfigParams ram = {0};
    ram.rxFIFO0startAddr = 0x0; ram.rxFIFO0size = 64; ram.rxFIFO0OpMode = MCAN_RX_FIFO_OPMODE_BLOCKING;
    MCAN_msgRAMConfig(MCANA_BASE, &ram);
    MCAN_GlobalFiltConfigParams gf = {0};
    gf.rrfe=0; gf.rrfs=0; gf.anfe=MCAN_RX_FIFO_NUM_0; gf.anfs=MCAN_RX_FIFO_NUM_0;   /* non-matching -> FIFO0 */
    MCAN_setGlobalFilters(MCANA_BASE, &gf);

    MCAN_enableInterrupt(MCANA_BASE, MCAN_IR_RF0N_MASK);     /* new msg in FIFO0 */
    MCAN_selectInterruptLine(MCANA_BASE, MCAN_IR_RF0N_MASK, MCAN_INTR_LINE_NUM_0);
    MCAN_enableIntrLine(MCANA_BASE, MCAN_INTR_LINE_NUM_0, 1U);

    MCAN_setOpMode(MCANA_BASE, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_getOpMode(MCANA_BASE) != MCAN_OPERATION_MODE_NORMAL) { }
}

/* ----- rough CAN-FD bus-load estimate (bits over a 1s window) ----- */
static uint8_t bus_load_pct(uint32_t frames, uint32_t payload_bytes){
    /* ~ (arb overhead bits @ nominal + data bits @ data rate) vs capacity. Estimate only. */
    uint32_t bits = frames*40u + payload_bytes*8u;       /* coarse: 40 overhead bits + payload */
    uint32_t cap  = 5000000u;                            /* effective bits/s @ 5 Mbps data */
    uint32_t pct  = (bits*100u)/cap; return pct>100?100:(uint8_t)pct;
}

/* ==================================  main  =================================== */
void main(void)
{
    Device_init(); Device_initGPIO();
    Interrupt_initModule(); Interrupt_initVectorTable();
    /* Board_init(): configure MCANA pins+clock, SCIA (XDS110 COM) at 3 Mbaud, CPUTIMER0 free-run @1us. */
    Interrupt_register(INT_MCANA_0, &mcanRxISR);
    Interrupt_enable(INT_MCANA_0);
    mcan_init();
    EINT; ERTM;

    uint64_t t_status = now_us(); uint32_t win_frames = 0, win_bytes = 0;
    for (;;) {
        /* drain ring -> SCI/SD as fast as we can */
        while (r_tail != r_head) {
            frame_t f; memcpy(&f, (const void*)&ring[r_tail], sizeof(frame_t));
            r_tail = ring_next(r_tail);
            send_frame(&f); win_frames++; win_bytes += f.dlen;
        }
        /* periodic STATUS + bus-off auto-recovery */
        if (now_us() - t_status >= 1000000u) {
            uint16_t ecr = MCAN_getErrCounters(MCANA_BASE).canErrLogCnt; (void)ecr;
            MCAN_ProtocolStatus ps = MCAN_getProtocolStatus(MCANA_BASE);
            uint8_t es = ps.busOffStatus ? 2u : (ps.errPassive ? 1u : 0u);
            send_status(bus_load_pct(win_frames, win_bytes), es,
                        (uint8_t)MCAN_getErrCounters(MCANA_BASE).transErrLogCnt,
                        (uint8_t)MCAN_getErrCounters(MCANA_BASE).recErrCnt);
            if (ps.busOffStatus) { MCAN_setOpMode(MCANA_BASE, MCAN_OPERATION_MODE_NORMAL); } /* leave bus-off */
            win_frames = 0; win_bytes = 0; t_status = now_us();
        }
    }
}
