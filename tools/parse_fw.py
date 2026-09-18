#!/usr/bin/env python3
"""parse_fw.py - valida rtw8852bt_fw.bin con la misma logica que el kext (fase 1).

Uso:  python tools/parse_fw.py [firmware/rtw8852bt_fw.bin]

Replica rtw89_mfw_recognize + rtw89_fw_hdr_parser_v0 de reference/rtw89-linux/fw.c.
Sirve para comprobar en Windows, sin Mac, que el formato que espera RTL8852BT.cpp
coincide con el fichero real.
"""
import struct, sys, os

MFW_SIG = 0xFF
# enum rtw89_fw_type de reference/rtw89-linux/core.h
FW_TYPES = {1: "NORMAL", 3: "WOWLAN", 5: "NORMAL_CE", 14: "NORMAL_B", 15: "WOWLAN_B",
            64: "BBMCU0", 65: "BBMCU1", 255: "LOGFMT"}

def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)

def parse_single(buf, off, size, label):
    w = struct.unpack_from("<8I", buf, off)
    major, minor, sub, idx = bits(w[1], 7, 0), bits(w[1], 15, 8), bits(w[1], 23, 16), bits(w[1], 31, 24)
    commit = w[2]
    hdr_ver = bits(w[3], 31, 24)
    month, day, hour, minute = bits(w[4], 7, 0), bits(w[4], 15, 8), bits(w[4], 23, 16), bits(w[4], 31, 24)
    year = w[5]
    sec_num = bits(w[6], 15, 8)
    dyn_hdr = bits(w[7], 16, 16)
    hdr_len = bits(w[3], 23, 16) if dyn_hdr else 32 + 16 * sec_num
    print(f"  {label}: v{major}.{minor}.{sub}.{idx} commit {commit:08x} hdr_ver={hdr_ver} "
          f"fecha {year}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}  secciones={sec_num} dyn_hdr={dyn_hdr} hdr_len={hdr_len}")
    total = 0
    for i in range(sec_num):
        s0, s1, s2, s3 = struct.unpack_from("<4I", buf, off + 32 + 16 * i)
        ssize = bits(s1, 23, 0)
        stype = bits(s1, 27, 24)
        chk = bits(s1, 28, 28)
        redl = bits(s1, 29, 29)
        total += ssize
        print(f"     sec[{i}] dl_addr=0x{s0:08x} size={ssize:7d} type={stype} checksum={chk} redl={redl}")
    ok = hdr_len + total <= size
    print(f"     codigo total={total} bytes, cabecera={hdr_len}, entrada={size} -> {'OK' if ok else 'INCOHERENTE'}")
    return ok

ELEMENT_NAMES = {
    0: "BBMCU0", 1: "BBMCU1", 2: "BB_REG", 3: "BB_GAIN", 4: "RADIO_A", 5: "RADIO_B",
    6: "RADIO_C", 7: "RADIO_D", 8: "RF_NCTL", 9: "TXPWR_BYRATE", 10: "TXPWR_LMT_2GHZ",
    11: "TXPWR_LMT_5GHZ", 12: "TXPWR_LMT_6GHZ", 13: "TXPWR_LMT_RU_2GHZ",
    14: "TXPWR_LMT_RU_5GHZ", 15: "TXPWR_LMT_RU_6GHZ", 16: "TX_SHAPE_LMT",
    17: "TX_SHAPE_LMT_RU", 18: "TXPWR_TRK", 19: "RFKLOG_FMT", 20: "REGD",
    21: "TXPWR_DA_LMT_2GHZ", 22: "TXPWR_DA_LMT_5GHZ", 23: "TXPWR_DA_LMT_6GHZ",
}
# Elementos cuyo contenido son pares direccion/dato (idx + 7 rsvd + array)
REG2_ELEMENTS = {2, 4, 5, 8}
ELEMENT_ALIGN = 16
ELEMENT_HDR_SIZE = 32


def parse_elements(buf, mfw_end):
    """Las tablas de configuracion van PEGADAS al final del fichero, detras del
    contenedor multi-firmware. El 8852BT no las lleva compiladas en el driver
    (en rtw8852bt.c sus punteros son NULL), asi que salen de aqui."""
    off = (mfw_end + ELEMENT_ALIGN - 1) & ~(ELEMENT_ALIGN - 1)
    print()
    print(f"Elementos (tablas de configuracion) desde 0x{off:X}, "
          f"{len(buf) - off} bytes restantes:")
    found = {}
    n = 0
    while off + ELEMENT_HDR_SIZE <= len(buf):
        eid, esize = struct.unpack_from("<II", buf, off)
        ver = buf[off + 8:off + 12]
        if esize == 0 or off + ELEMENT_HDR_SIZE + esize > len(buf):
            break
        name = ELEMENT_NAMES.get(eid, f"id={eid}")
        extra = ""
        if eid in REG2_ELEMENTS:
            idx = buf[off + ELEMENT_HDR_SIZE - 8]
            npairs = (esize - 8) // 8
            extra = f"  idx={idx}  {npairs} pares direccion/dato"
            found[name] = npairs
        print(f"  0x{off:06X}  {name:<20}{esize:8d} B  "
              f"v{'.'.join(map(str, ver))}{extra}")
        off += ELEMENT_HDR_SIZE + esize
        off = (off + ELEMENT_ALIGN - 1) & ~(ELEMENT_ALIGN - 1)
        n += 1
    print(f"  {n} elementos")
    faltan = [k for k in ("BB_REG", "RADIO_A", "RADIO_B", "RF_NCTL") if k not in found]
    if faltan:
        print("  FALTAN tablas basicas: " + ", ".join(faltan))
        return False
    print("  Tablas basicas presentes: " +
          ", ".join(f"{k}={v}" for k, v in found.items()))
    return True


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "firmware", "rtw8852bt_fw.bin")
    buf = open(path, "rb").read()
    print(f"{path}: {len(buf)} bytes")
    if buf[0] != MFW_SIG:
        sys.exit(0 if parse_single(buf, 0, len(buf), "single") else 1)
    fw_nr = buf[1]
    major, minor, sub, idx = buf[4], buf[5], buf[6], buf[7]
    print(f"MFW container v{major}.{minor}.{sub}.{idx}, {fw_nr} entradas")
    ok_all = True
    for i in range(fw_nr):
        cv, typ, mp, _rsvd, shift, size = struct.unpack_from("<4BII", buf, 16 + 16 * i)
        label = f"[{i}] cv={'any' if cv == 0xFF else cv} type={FW_TYPES.get(typ, typ)} mp={mp} shift=0x{shift:x} size={size}"
        if shift + size > len(buf):
            print(f"  {label}: FUERA DEL FICHERO"); ok_all = False; continue
        ok_all &= parse_single(buf, shift, size, label)
    if fw_nr:
        cv, typ, mp, _r, shift, size = struct.unpack_from("<4BII", buf, 16 + 16 * (fw_nr - 1))
        ok_all &= parse_elements(buf, shift + size)

    print()
    print("RESULTADO:", "firmware valido" if ok_all else "firmware invalido")
    sys.exit(0 if ok_all else 1)

if __name__ == "__main__":
    main()
