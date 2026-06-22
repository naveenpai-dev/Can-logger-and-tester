#!/usr/bin/env python3
"""
can_logger_host.py — laptop-side companion for the C2000 "atelier" CAN-FD logger.

Reads the board's framed stream over USB-serial (XDS110 virtual COM or a USB-UART),
writes a replayable CSV log, and prints a live monitor: frame rate, bus load,
error state, and the busiest IDs.

  Live :  python can_logger_host.py --port COM5 --baud 3000000 --csv run1.csv
  Test :  python can_logger_host.py --demo --seconds 2 --csv demo.csv   (no hardware)

Wire protocol (little-endian), one record per packet:
  AA 55 | type(1) | len(1) | payload[len] | crc8
    type 0 FRAME  : ts_us(u64) id(u32) flags(u8) dlen(u8) chan(u8) data[dlen]
    type 1 STATUS : ts_us(u64) busload(u8) errstate(u8) tec(u8) rec(u8) rx(u32) err(u32)
  flags: b0 FD  b1 BRS  b2 ESI  b3 IDE(extended)   errstate: 0 active 1 passive 2 bus-off
"""
import argparse, struct, sys, time, threading, queue, collections

SYNC0, SYNC1 = 0xAA, 0x55
T_FRAME, T_STATUS = 0, 1
ERRT = {0: "ACTIVE", 1: "PASSIVE", 2: "BUS-OFF"}

def crc8(b, poly=0x07):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c

def encode(rtype, payload):
    body = bytes([rtype, len(payload)]) + payload
    return bytes([SYNC0, SYNC1]) + body + bytes([crc8(body)])

# ---------------- stream parser (state machine, resyncs on garbage) -------------
class Parser:
    def __init__(self): self.buf = bytearray()
    def feed(self, data):
        out = []
        self.buf.extend(data)
        b = self.buf
        i = 0
        while True:
            # find SYNC
            while i + 1 < len(b) and not (b[i] == SYNC0 and b[i+1] == SYNC1):
                i += 1
            if i + 4 > len(b): break                      # need at least sync+type+len+crc
            rtype, ln = b[i+2], b[i+3]
            end = i + 4 + ln + 1                          # +crc
            if end > len(b): break                        # wait for more
            body = bytes(b[i+2:i+4+ln])
            if crc8(body) == b[i+4+ln]:
                out.append((rtype, bytes(b[i+4:i+4+ln])))
                i = end
            else:
                i += 2                                     # bad crc, skip past this sync
        del b[:i]
        return out

def parse_frame(p):
    ts, cid, flags, dlen, chan = struct.unpack_from("<QIBBB", p, 0)
    data = p[15:15+dlen]
    return dict(ts=ts, id=cid, fd=bool(flags&1), brs=bool(flags&2),
                esi=bool(flags&4), ext=bool(flags&8), dlen=dlen, chan=chan, data=data)

def parse_status(p):
    ts, bl, es, tec, rec, rx, err = struct.unpack_from("<QBBBBII", p, 0)
    return dict(ts=ts, busload=bl, err=ERRT.get(es, "?"), tec=tec, rec=rec, rx=rx, errc=err)

# ---------------- live stats ----------------
class Stats:
    def __init__(self):
        self.total = 0; self.bytes = 0; self.window = collections.Counter()
        self.busload = 0; self.errstate = "—"; self.errc = 0
        self.t0 = time.time(); self.last = self.t0
    def on_frame(self, f):
        self.total += 1; self.bytes += f["dlen"]
        self.window[("%X" % f["id"]) + ("x" if f["ext"] else "")] += 1
    def on_status(self, s):
        self.busload = s["busload"]; self.errstate = s["err"]; self.errc = s["errc"]
    def tick(self):
        now = time.time(); dt = now - self.last; self.last = now
        fps = sum(self.window.values()) / dt if dt else 0
        top = ", ".join(f"{k}:{v}" for k, v in self.window.most_common(4))
        line = (f"[{now-self.t0:6.1f}s] {fps:7.0f} fr/s | bus {self.busload:3d}% | "
                f"{self.errstate:7s} | err {self.errc} | total {self.total} | top {top}")
        self.window.clear()
        return line

# ---------------- CSV sink (replayable) ----------------
class Csv:
    def __init__(self, path):
        self.f = open(path, "w", newline="") if path else None
        if self.f: self.f.write("ts_us,id_hex,ext,fd,brs,dlc,len,data_hex\n")
    def write(self, f):
        if not self.f: return
        self.f.write(f"{f['ts']},{f['id']:X},{int(f['ext'])},{int(f['fd'])},"
                     f"{int(f['brs'])},{f['dlen']},{f['dlen']},{f['data'].hex().upper()}\n")
    def close(self):
        if self.f: self.f.close()

# ---------------- demo generator (proves the pipeline without a board) ----------
def demo_source(q, seconds, rate):
    ids = [0x100, 0x18FF50E5, 0x244, 0x7DF, 0x3C0]
    t0 = time.time(); n = 0; period = 1.0 / rate
    nextstatus = t0 + 1.0
    import random
    while time.time() - t0 < seconds:
        ts = int((time.time() - t0) * 1_000_000)
        cid = random.choice(ids); ext = cid > 0x7FF
        dlen = random.choice([8, 12, 16, 24, 32, 48, 64])
        flags = 0b0001 | 0b0010 | (0b1000 if ext else 0)         # FD + BRS (+ IDE)
        data = bytes(random.getrandbits(8) for _ in range(dlen))
        q.put(encode(T_FRAME, struct.pack("<QIBBB", ts, cid, flags, dlen, 0) + data))
        n += 1
        if time.time() >= nextstatus:
            busload = min(99, 60 + (n % 35))
            q.put(encode(T_STATUS, struct.pack("<QBBBBII", ts, busload, 0, 0, 0, n, 0)))
            nextstatus += 1.0
        time.sleep(period)
    q.put(None)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port"); ap.add_argument("--baud", type=int, default=3_000_000)
    ap.add_argument("--csv", default="")
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("--rate", type=int, default=4000, help="demo frames/sec")
    a = ap.parse_args()

    parser, stats, csv = Parser(), Stats(), Csv(a.csv)
    q = queue.Queue()

    if a.demo:
        threading.Thread(target=demo_source, args=(q, a.seconds or 2, a.rate), daemon=True).start()
        reader = lambda: q.get()
    else:
        try:
            import serial
        except ImportError:
            sys.exit("pyserial needed for live mode:  pip install pyserial")
        ser = serial.Serial(a.port, a.baud, timeout=0.1)
        def reader():
            return ser.read(4096) or b""

    last_tick = time.time(); end = time.time() + a.seconds if a.seconds else None
    print("ts        rate     bus    state     errs   total  busiest-IDs")
    try:
        while True:
            chunk = reader()
            if chunk is None: break
            for rtype, p in parser.feed(chunk):
                if rtype == T_FRAME:
                    f = parse_frame(p); stats.on_frame(f); csv.write(f)
                elif rtype == T_STATUS:
                    stats.on_status(parse_status(p))
            if time.time() - last_tick >= 1.0:
                print(stats.tick()); last_tick = time.time()
            if end and time.time() > end and a.demo is False: break
    except KeyboardInterrupt:
        pass
    finally:
        csv.close()
        print(f"\nDONE — {stats.total} frames, {stats.bytes} payload bytes"
              + (f", log → {a.csv}" if a.csv else ""))

if __name__ == "__main__":
    main()
