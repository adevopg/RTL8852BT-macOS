#!/usr/bin/env python3
"""verify_regs.py - comprueba que kext/src/rtw89_regs.h coincide con el driver Linux.

Uso:  python tools/verify_regs.py

Un solo bit mal transcrito en la secuencia de encendido hace que el chip no
arranque, y depurarlo desde el log es casi imposible. Este script compara cada
#define de rtw89_regs.h contra reference/rtw89-linux/reg.h y mac.h, evaluando
BIT() y GENMASK() de verdad, y avisa de:

  - valores que no coinciden con el original
  - defines que no existen en el driver Linux (posible invento)
  - registros que usa RTL8852BT_power.cpp pero no estan definidos

Salida: codigo 0 si todo cuadra, 1 si hay alguna discrepancia.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OURS = ROOT / "kext" / "src" / "rtw89_regs.h"
LINUX_DIR = ROOT / "reference" / "rtw89-linux"
LINUX_FILES = ["reg.h", "mac.h", "core.h"]
USERS = [ROOT / "kext" / "src" / "RTL8852BT_power.cpp",
         ROOT / "kext" / "src" / "RTL8852BT.cpp"]

DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")
ENUM_RE = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,?\s*$")


def bit(n):
    return 1 << n


def genmask(h, l):
    return ((~0 & ((1 << 64) - 1)) - (1 << l) + 1) & (((1 << 64) - 1) >> (64 - 1 - h))


def evaluate(expr):
    """Evalua una expresion de C con BIT/GENMASK. None si no es un valor simple."""
    e = expr.split("/*")[0].split("//")[0].strip()
    if not e:
        return None
    # Solo aceptamos aritmetica segura sobre BIT/GENMASK/enteros
    if not re.fullmatch(r"[0-9a-fA-FxXBITGENMASKULbitgenmask_\s\(\),\|\+\-<>]*", e):
        return None
    try:
        return eval(e, {"__builtins__": {}}, {  # noqa: S307 - entrada controlada
            "BIT": bit, "GENMASK": genmask,
            "BIT_ULL": bit, "GENMASK_ULL": genmask,
        })
    except Exception:
        return None


def parse_defines(path: Path):
    """Nombre -> (valor, expresion, linea). Se queda con la primera aparicion."""
    out = {}
    if not path.is_file():
        return out
    for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        m = DEFINE_RE.match(line)
        if m:
            name, expr = m.group(1), m.group(2)
            if "(" in name or name in out:
                continue  # macros con parametros, o ya vista
            val = evaluate(expr)
            if val is not None:
                out[name] = (val, expr.strip(), lineno)
            continue
        m = ENUM_RE.match(line)
        if m and m.group(1) not in out:
            out[m.group(1)] = (int(m.group(2), 0), m.group(2), lineno)
    return out


def main():
    if not OURS.is_file():
        sys.exit(f"ERROR: no encuentro {OURS}")

    ours = parse_defines(OURS)
    theirs = {}
    src_of = {}
    for fn in LINUX_FILES:
        p = LINUX_DIR / fn
        if not p.is_file():
            print(f"AVISO: falta {p}, se omite")
            continue
        for name, info in parse_defines(p).items():
            if name not in theirs:
                theirs[name] = info
                src_of[name] = fn

    print(f"rtw89_regs.h: {len(ours)} defines")
    print(f"driver Linux: {len(theirs)} defines en {', '.join(LINUX_FILES)}")
    print()

    mismatch, missing, ok = [], [], 0
    for name, (val, expr, lineno) in sorted(ours.items()):
        if name not in theirs:
            missing.append((name, val, expr, lineno))
            continue
        tval, texpr, tline = theirs[name]
        if val != tval:
            mismatch.append((name, val, expr, tval, texpr, src_of[name], tline))
        else:
            ok += 1

    if mismatch:
        print(f"DISCREPANCIAS ({len(mismatch)}) - ESTO ROMPE EL DRIVER:")
        for name, val, expr, tval, texpr, src, tline in mismatch:
            print(f"  {name}")
            print(f"     nuestro: {expr:28s} = 0x{val:08X}")
            print(f"     Linux:   {texpr:28s} = 0x{tval:08X}   ({src}:{tline})")
        print()

    if missing:
        print(f"NO ENCONTRADOS en el driver Linux ({len(missing)}):")
        for name, val, expr, lineno in missing:
            print(f"  {name:34s} = {expr:24s} (rtw89_regs.h:{lineno})")
        print("  Revisa si son constantes propias o nombres mal escritos.")
        print()

    # Registros que el codigo usa pero no estan definidos en ningun sitio
    known = set(ours) | set(theirs)
    undefined = set()
    for u in USERS:
        if not u.is_file():
            continue
        text = u.read_text(encoding="utf-8", errors="replace")
        for tok in re.findall(r"\b(?:R_AX|B_AX|XTAL_SI|MAC_AX|SW_LPS)[A-Z0-9_]*\b", text):
            if tok not in known:
                undefined.add(tok)
    if undefined:
        print(f"USADOS PERO NO DEFINIDOS ({len(undefined)}) - no compilara:")
        for tok in sorted(undefined):
            print(f"  {tok}")
        print()

    print(f"RESULTADO: {ok} coinciden, {len(mismatch)} discrepan, "
          f"{len(missing)} sin origen, {len(undefined)} sin definir")
    bad = bool(mismatch or undefined)
    print("OK - la transcripcion de registros es fiel al driver Linux." if not bad
          else "FALLO - corrige lo anterior antes de compilar.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
