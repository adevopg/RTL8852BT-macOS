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
    print("RESULTADO:", "firmware valido" if ok_all else "firmware invalido")
    sys.exit(0 if ok_all else 1)

if __name__ == "__main__":
    main()
