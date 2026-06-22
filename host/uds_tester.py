#!/usr/bin/env python3
"""
uds_tester.py — laptop-side UDS (ISO 14229-1) tester over ISO-TP (ISO 15765-2),
driving the same C2000 LaunchPad used by the CAN logger.

Where the logger is the *passive* capture side, this is the *active* side: it sends
UDS requests onto the bus and decodes the responses — sessions, DID reads/writes,
DTC read/clear, routine control, ECU reset, security access, tester-present.

It runs against:
  * a real board — the firmware acts as a bidirectional CAN<->serial bridge
    (the logger's RX stream + a host->board TX_FRAME; see docs/WIRE_PROTOCOL.md);
  * a built-in simulated ECU (`--demo`) — proves the whole ISO-TP + UDS pipeline
    with no hardware, exactly like the logger's --demo.

  Live :  python uds_tester.py --port COM5 --ecu OBC --read 0xF190
  Demo :  python uds_tester.py --demo --ecu BMS --identify        (no hardware)

The MM vehicle bus is classic CAN @ 500 kbit/s, 8-byte frames — so the tester
defaults to classic ISO-TP. Pass --fd for CAN-FD single-frames up to 62 bytes.
"""
import argparse, struct, sys, time, threading, queue, collections

# ============================================================================
#  Serial framing — shared byte-for-byte with the logger (docs/WIRE_PROTOCOL.md)
#    AA 55 | type | len | payload | crc8     (crc8 over [type,len,payload])
#    type 0 FRAME    (board -> host)  one captured CAN frame
#    type 2 TX_FRAME (host -> board)  one CAN frame to transmit
#      TX payload:  id(u32) flags(u8) dlen(u8) data[dlen]   (flags as in FRAME)
# ============================================================================
SYNC0, SYNC1 = 0xAA, 0x55
T_FRAME, T_STATUS, T_TX = 0, 1, 2

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

class Parser:
    """Resyncing stream parser — mirrors the logger's, decoding board->host packets."""
    def __init__(self): self.buf = bytearray()
    def feed(self, data):
        out, b, i = [], self.buf, 0
        b.extend(data)
        while True:
            while i + 1 < len(b) and not (b[i] == SYNC0 and b[i+1] == SYNC1):
                i += 1
            if i + 4 > len(b): break
            rtype, ln = b[i+2], b[i+3]
            end = i + 4 + ln + 1
            if end > len(b): break
            bod = bytes(b[i+2:i+4+ln])
            if crc8(bod) == b[i+4+ln]:
                out.append((rtype, bytes(b[i+4:i+4+ln]))); i = end
            else:
                i += 2
        del b[:i]
        return out

# ============================================================================
#  CAN frame + link layer
# ============================================================================
class CanFrame:
    __slots__ = ("id", "ext", "fd", "brs", "data")
    def __init__(self, cid, ext=False, fd=False, brs=False, data=b""):
        self.id, self.ext, self.fd, self.brs, self.data = cid, ext, fd, brs, bytes(data)
    def flags(self):
        return (1 if self.fd else 0) | (2 if self.brs else 0) | (8 if self.ext else 0)

class SerialLink:
    """Drives a real board over the XDS110 virtual COM. send() -> TX_FRAME packet;
    recv() -> next captured FRAME as a CanFrame."""
    def __init__(self, port, baud):
        try:
            import serial
        except ImportError:
            sys.exit("pyserial needed for live mode:  pip install pyserial")
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.parser, self.q = Parser(), collections.deque()
    def send(self, f):
        p = struct.pack("<IBB", f.id, f.flags(), len(f.data)) + f.data
        self.ser.write(encode(T_TX, p))
    def recv(self, timeout=1.0):
        end = time.time() + timeout
        while True:
            if self.q: return self.q.popleft()
            chunk = self.ser.read(512)
            for rtype, p in self.parser.feed(chunk):
                if rtype != T_FRAME: continue
                ts, cid, fl, dlen, chan = struct.unpack_from("<QIBBB", p, 0)
                self.q.append(CanFrame(cid, bool(fl & 8), bool(fl & 1), bool(fl & 2), p[15:15+dlen]))
            if not self.q and time.time() > end: return None

class EndpointLink:
    """One end of an in-memory channel — the demo's stand-in for a serial link.
    Both the host and the simulated ECU hold one, with tx/rx queues crossed."""
    def __init__(self, tx_q, rx_q): self.tx, self.rx = tx_q, rx_q
    def send(self, f): self.tx.put(f)
    def recv(self, timeout=1.0):
        try: return self.rx.get(timeout=timeout)
        except queue.Empty: return None

# ============================================================================
#  ISO-TP transport (ISO 15765-2) — SF / FF / CF / FC
# ============================================================================
class IsoTp:
    def __init__(self, link, tx_id, rx_id, ext=False, fd=False, brs=False,
                 pad=0xCC, bs=0, stmin=0):
        self.link, self.tx_id, self.rx_id = link, tx_id, rx_id
        self.ext, self.fd, self.brs, self.pad = ext, fd, brs, pad
        self.bs, self.stmin = bs, stmin            # what WE grant as receiver

    _FD = (8, 12, 16, 20, 24, 32, 48, 64)
    def _frame(self, data):
        n = len(data)
        tgt = 8 if n <= 8 else next(s for s in self._FD if s >= n)
        data = bytes(data) + bytes([self.pad]) * (tgt - n)
        return CanFrame(self.tx_id, self.ext, self.fd, self.brs, data)

    def _read(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            f = self.link.recv(max(0.01, end - time.time()))
            if f is not None and f.id == self.rx_id:
                return f
        return None

    @staticmethod
    def _stmin_s(v):
        if v <= 0x7F: return v / 1000.0
        if 0xF1 <= v <= 0xF9: return (v - 0xF0) / 10000.0
        return 0.0

    def _wait_fc(self, timeout):
        while True:
            f = self._read(timeout)
            if f is None: raise TimeoutError("no FlowControl from ECU")
            fs = f.data[0] & 0x0F
            if fs == 0: return (f.data[1], f.data[2])     # CTS -> (BS, STmin)
            if fs == 1: continue                          # Wait -> keep waiting
            raise IOError("ISO-TP overflow (FC.OVFLW from ECU)")

    def send(self, payload, timeout=1.0):
        cap = 62 if self.fd else 7
        if len(payload) <= cap:
            pci = bytes([0x00, len(payload)]) if (self.fd and len(payload) > 7) \
                  else bytes([len(payload)])
            self.link.send(self._frame(pci + payload)); return
        ln = len(payload)
        self.link.send(self._frame(bytes([0x10 | ((ln >> 8) & 0x0F), ln & 0xFF]) + payload[:6]))
        bs, stmin = self._wait_fc(timeout)
        idx, seq, since = 6, 1, 0
        while idx < ln:
            self.link.send(self._frame(bytes([0x20 | (seq & 0x0F)]) + payload[idx:idx+7]))
            idx += 7; seq += 1; since += 1
            if stmin: time.sleep(self._stmin_s(stmin))
            if bs and since == bs and idx < ln:
                bs, stmin = self._wait_fc(timeout); since = 0

    def recv(self, timeout=1.0):
        f = self._read(timeout)
        if f is None: return None
        b = f.data
        pci = b[0] >> 4
        if pci == 0:                                       # Single Frame
            ln, off = b[0] & 0x0F, 1
            if ln == 0: ln, off = b[1], 2                  # FD SF escape
            return bytes(b[off:off+ln])
        if pci == 1:                                       # First Frame
            ln = ((b[0] & 0x0F) << 8) | b[1]
            out = bytearray(b[2:8])
            self.link.send(self._frame(bytes([0x30, self.bs, self.stmin])))  # FC.CTS
            seq, since = 1, 0
            while len(out) < ln:
                cf = self._read(timeout)
                if cf is None: return None
                out += cf.data[1:8]
                seq = (seq + 1) & 0x0F; since += 1
                if self.bs and since == self.bs and len(out) < ln:
                    self.link.send(self._frame(bytes([0x30, self.bs, self.stmin]))); since = 0
            return bytes(out[:ln])
        return None                                        # unexpected (FC/CF w/o FF)

# ============================================================================
#  UDS client (ISO 14229-1)
# ============================================================================
NRC = {
    0x10: "generalReject", 0x11: "serviceNotSupported", 0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat", 0x14: "responseTooLong",
    0x21: "busyRepeatRequest", 0x22: "conditionsNotCorrect", 0x24: "requestSequenceError",
    0x31: "requestOutOfRange", 0x33: "securityAccessDenied", 0x35: "invalidKey",
    0x36: "exceedNumberOfAttempts", 0x37: "requiredTimeDelayNotExpired",
    0x70: "uploadDownloadNotAccepted", 0x72: "generalProgrammingFailure",
    0x73: "wrongBlockSequenceCounter", 0x78: "requestCorrectlyReceived-ResponsePending",
    0x7E: "subFunctionNotSupportedInActiveSession", 0x7F: "serviceNotSupportedInActiveSession",
}
SID = {0x10: "DiagnosticSessionControl", 0x11: "ECUReset", 0x14: "ClearDTC",
       0x19: "ReadDTCInformation", 0x22: "ReadDataByIdentifier", 0x27: "SecurityAccess",
       0x2E: "WriteDataByIdentifier", 0x31: "RoutineControl", 0x3E: "TesterPresent"}

class NegativeResponse(Exception):
    def __init__(self, sid, nrc):
        self.sid, self.nrc = sid, nrc
        super().__init__(f"NRC 0x{nrc:02X} {NRC.get(nrc, 'unknown')} "
                         f"(to {SID.get(sid, 'SID 0x%02X' % sid)})")

class Uds:
    def __init__(self, isotp, p2=1.0, p2star=5.0):
        self.tp, self.p2, self.p2star = isotp, p2, p2star
    def request(self, sid, data=b""):
        self.tp.send(bytes([sid]) + bytes(data))
        deadline = time.time() + self.p2
        while True:
            resp = self.tp.recv(max(0.05, deadline - time.time()))
            if resp is None: raise TimeoutError(f"no response to {SID.get(sid,'0x%02X'%sid)}")
            if resp[0] == 0x7F:
                if resp[2] == 0x78:                        # response pending -> extend
                    deadline = time.time() + self.p2star; continue
                raise NegativeResponse(resp[1], resp[2])
            if resp[0] != sid + 0x40:
                raise IOError(f"unexpected SID 0x{resp[0]:02X}")
            return resp[1:]
    # --- service helpers ---
    def session(self, s):          return self.request(0x10, [s])
    def ecu_reset(self, t):        return self.request(0x11, [t])
    def tester_present(self):      return self.request(0x3E, [0x00])
    def read_did(self, did):       return self.request(0x22, struct.pack(">H", did))[2:]
    def write_did(self, did, d):   return self.request(0x2E, struct.pack(">H", did) + bytes(d))
    def clear_dtc(self, g=0xFFFFFF): return self.request(0x14, struct.pack(">I", g)[1:])
    def read_dtc(self, mask=0xFF):  return self.request(0x19, [0x02, mask])
    def routine(self, sub, rid, d=b""): return self.request(0x31, bytes([sub]) + struct.pack(">H", rid) + bytes(d))
    def security_access(self, level, key_fn):
        seed = self.request(0x27, [level])[1:]
        if not any(seed): return seed                      # already unlocked
        return self.request(0x27, [level + 1] + list(key_fn(seed)))

# ============================================================================
#  Reference data — MM Mill address book + DID catalog
# ============================================================================
ECU = {  # name : (request id, response id)        (CLAUDE.md §2.2)
    "BMS": (0x7B0, 0x7B8), "DTU": (0x7A0, 0x7A8),
    "BCU": (0x7C0, 0x7C8), "OBC": (0x7E0, 0x7E8),
}
FUNCTIONAL = 0x7DF
DID = {
    0xF190: "VIN", 0xF18C: "ECU Serial Number", 0xF195: "SW Version",
    0xF187: "Spare Part Number", 0xF18A: "System Supplier Identifier",
    0xF332: "Battery Cell Variant", 0xF333: "Battery Pack Variant",
    0xF390: "Security Event Log", 0xF3E0: "Session Nonce",
}
def demo_key(seed):                                        # toy key algo (demo ECU only)
    return bytes((b ^ 0xA5) for b in seed)

# ============================================================================
#  Simulated ECU — runs in a thread, speaks real ISO-TP back to the host
# ============================================================================
class SimEcu(threading.Thread):
    def __init__(self, link, req_id, resp_id, name="SIM"):
        super().__init__(daemon=True)
        self.tp = IsoTp(link, resp_id, req_id, bs=0, stmin=0)
        self.name, self.session_id, self.unlocked, self.stop = name, 0x01, False, False
        self.seed = bytes([0x11, 0x22, 0x33, 0x44])
        self.store = {
            0xF190: b"MM1CANLOGGER0TEST7",             # 17-char VIN
            0xF18C: b"SN-" + name.encode() + b"-0001",
            0xF195: b"1.0.0",
            0xF332: bytes([0x02]),                      # cell variant
            0xF333: bytes([0x05]),                      # pack variant
            0xF3E0: bytes([0xDE, 0xAD, 0xBE, 0xEF]),    # session nonce (security-gated)
            0xF390: b"SEC-LOG-EMPTY",                    # security event log (security-gated)
        }
        self.dtcs = [(0x11, 0x01, 0x00, 0x08),      # P1101, confirmed+testFailed
                     (0x18, 0x04, 0x00, 0x2F)]      # U1804, pending
    def run(self):
        while not self.stop:
            req = self.tp.recv(timeout=0.2)
            if req: self.tp.send(self._dispatch(req))
    def _neg(self, sid, nrc): return bytes([0x7F, sid, nrc])
    def _dispatch(self, req):
        sid, d = req[0], req[1:]
        if sid == 0x10:                                    # session
            self.session_id = d[0]; return bytes([0x50, d[0], 0x00, 0x32, 0x01, 0xF4])
        if sid == 0x3E: return bytes([0x7E, 0x00])
        if sid == 0x11: return bytes([0x51, d[0]])
        if sid == 0x22:                                    # read DID
            did = struct.unpack(">H", d[:2])[0]
            if did in (0xF390, 0xF3E0) and not self.unlocked:
                return self._neg(sid, 0x33)                # securityAccessDenied
            if did not in self.store: return self._neg(sid, 0x31)  # requestOutOfRange
            return bytes([0x62]) + d[:2] + self.store[did]
        if sid == 0x2E:                                    # write DID
            if self.session_id == 0x01: return self._neg(sid, 0x22)  # need non-default
            did = struct.unpack(">H", d[:2])[0]
            self.store[did] = bytes(d[2:]); return bytes([0x6E]) + d[:2]
        if sid == 0x14: return bytes([0x54])               # clear DTC
        if sid == 0x19 and d[0] == 0x02:                   # read DTC by status mask
            out = bytearray([0x59, 0x02, 0xFF])
            for dtc in self.dtcs: out += bytes(dtc)
            return bytes(out)
        if sid == 0x27:                                    # security access
            if d[0] & 1:                                   # request seed
                return bytes([0x67, d[0]]) + (bytes(len(self.seed)) if self.unlocked else self.seed)
            if bytes(d[1:]) == demo_key(self.seed):        # send key
                self.unlocked = True; return bytes([0x67, d[0]])
            return self._neg(sid, 0x35)                    # invalidKey
        if sid == 0x31:                                    # routine control
            return bytes([0x71, d[0]]) + d[1:3] + bytes([0x00])
        return self._neg(sid, 0x11)                        # serviceNotSupported

# ============================================================================
#  CLI
# ============================================================================
def hexbytes(s): return bytes.fromhex(s.replace(" ", ""))
def show(label, raw):
    txt = ""
    try:
        if all(32 <= c < 127 for c in raw) and raw: txt = "  \"%s\"" % raw.decode("ascii")
    except Exception: pass
    print(f"  {label}: {raw.hex().upper() or '(empty)'}{txt}")

def run_commands(uds, a):
    did_int = lambda x: int(x, 0)
    if a.session is not None:
        uds.session(did_int(a.session)); print(f"  session -> 0x{did_int(a.session):02X} OK")
    if a.security is not None:
        uds.security_access(did_int(a.security), demo_key); print("  security access -> UNLOCKED")
    if a.tester_present:
        uds.tester_present(); print("  tester present -> OK")
    if a.identify:
        for did in (0xF190, 0xF18C, 0xF195):
            try: show(f"DID 0x{did:04X} {DID.get(did,''):<22}", uds.read_did(did))
            except (NegativeResponse, TimeoutError) as e: print(f"  DID 0x{did:04X}: {e}")
    if a.write is not None:
        d = did_int(a.write); uds.write_did(d, hexbytes(a.data or "")); print(f"  wrote DID 0x{d:04X}")
    if a.read is not None:
        d = did_int(a.read); show(f"DID 0x{d:04X} {DID.get(d,'')}", uds.read_did(d))
    if a.read_dtc:
        r = uds.read_dtc()                              # [subfunc, statusAvailMask, (DTC*3, status)...]
        n = (len(r) - 2) // 4
        print(f"  DTCs ({n}):")
        for i in range(n):
            o = 2 + i*4; print(f"    {r[o]:02X}{r[o+1]:02X}{r[o+2]:02X}  status 0x{r[o+3]:02X}")
    if a.clear_dtc:
        uds.clear_dtc(); print("  clear DTC -> OK")
    if a.routine is not None:
        sub = {"start": 0x01, "stop": 0x02, "result": 0x03}[a.routine]
        show(f"routine 0x{did_int(a.rid):04X} {a.routine}", uds.routine(sub, did_int(a.rid)))
    if a.reset is not None:
        uds.ecu_reset(did_int(a.reset)); print(f"  ECU reset 0x{did_int(a.reset):02X} -> OK")

def main():
    ap = argparse.ArgumentParser(description="UDS tester over ISO-TP for the C2000 logger board")
    ap.add_argument("--port"); ap.add_argument("--baud", type=int, default=3_000_000)
    ap.add_argument("--demo", action="store_true", help="run against a built-in simulated ECU")
    ap.add_argument("--ecu", default="BMS", choices=list(ECU), help="target ECU (sets CAN IDs)")
    ap.add_argument("--fd", action="store_true", help="CAN-FD frames (single-frame up to 62 B)")
    ap.add_argument("--session"); ap.add_argument("--security")
    ap.add_argument("--read"); ap.add_argument("--write"); ap.add_argument("--data")
    ap.add_argument("--reset"); ap.add_argument("--routine", choices=["start", "stop", "result"])
    ap.add_argument("--rid", default="0x0203")
    ap.add_argument("--read-dtc", action="store_true"); ap.add_argument("--clear-dtc", action="store_true")
    ap.add_argument("--tester-present", dest="tester_present", action="store_true")
    ap.add_argument("--identify", action="store_true", help="read VIN + serial + SW version")
    a = ap.parse_args()

    req, resp = ECU[a.ecu]
    if a.demo:
        a2b, b2a = queue.Queue(), queue.Queue()
        SimEcu(EndpointLink(b2a, a2b), req, resp, a.ecu).start()
        link = EndpointLink(a2b, b2a)
    elif a.port:
        link = SerialLink(a.port, a.baud)
    else:
        sys.exit("need --port <serial> or --demo")

    tp = IsoTp(link, req, resp, fd=a.fd, brs=a.fd)
    uds = Uds(tp)
    print(f"UDS tester -> {a.ecu}  (req 0x{req:03X} / resp 0x{resp:03X})"
          f"{'  [DEMO]' if a.demo else ''}")
    # default action if no command was given: identify
    if not any([a.session, a.security, a.read, a.write, a.reset, a.routine,
                a.read_dtc, a.clear_dtc, a.tester_present, a.identify]):
        a.identify = True
    try:
        run_commands(uds, a)
    except (NegativeResponse, TimeoutError, IOError) as e:
        print(f"  ERROR: {e}"); sys.exit(1)
    print("DONE")

if __name__ == "__main__":
    main()
