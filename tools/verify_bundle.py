#!/usr/bin/env python3
"""verify_bundle.py - comprueba que el kext esta bien formado ANTES de probarlo.

Uso:  python tools/verify_bundle.py

Valida lo que macOS mira al cargar un kext, y que se puede comprobar sin macOS:

  1. Info.plist parsea y tiene las claves obligatorias.
  2. El identificador y la version de KMOD_EXPLICIT_DECL coinciden EXACTAMENTE
     con CFBundleIdentifier y CFBundleVersion de Info.plist. Si no coinciden,
     kextload rechaza el kext con un error poco claro.
  3. IOKitPersonalities apunta a una IOClass que existe en el codigo.
  4. El PCI ID de IOPCIPrimaryMatch cubre el 10EC:B520 de esta tarjeta, y su
     formato (0xDDDDVVVV, device arriba y vendor abajo) es el correcto.
  5. OSBundleLibraries declara las dependencias que el codigo necesita de verdad.

Devuelve 0 si todo cuadra, 1 si no.
"""
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INFO = ROOT / "kext" / "Info.plist"
KMOD = ROOT / "kext" / "src" / "kmod_info.cpp"
SRC = ROOT / "kext" / "src"

# Nuestra tarjeta: Realtek RTL8852BE-VT
TARGET_VENDOR = 0x10EC
TARGET_DEVICE = 0xB520

errors = []
warnings = []


def err(m):
    errors.append(m)
    print(f"  FALLO: {m}")


def warn(m):
    warnings.append(m)
    print(f"  aviso: {m}")


def ok(m):
    print(f"  ok: {m}")


def main():
    if not INFO.is_file():
        sys.exit(f"ERROR: falta {INFO}")

    print("1. Info.plist")
    try:
        with open(INFO, "rb") as f:
            info = plistlib.load(f)
    except Exception as e:
        sys.exit(f"  FALLO: Info.plist no parsea: {e}")

    required = ["CFBundleIdentifier", "CFBundleVersion", "CFBundleExecutable",
                "CFBundlePackageType", "IOKitPersonalities", "OSBundleLibraries"]
    for k in required:
        if k not in info:
            err(f"falta la clave obligatoria {k}")
    if info.get("CFBundlePackageType") != "KEXT":
        err(f"CFBundlePackageType deberia ser KEXT, es {info.get('CFBundlePackageType')!r}")
    bundle_id = info.get("CFBundleIdentifier", "")
    version = info.get("CFBundleVersion", "")
    if not errors:
        ok(f"{bundle_id} v{version}")

    print("2. kmod_info.cpp coincide con Info.plist")
    if not KMOD.is_file():
        err("falta kext/src/kmod_info.cpp: sin el, kextload rechaza el kext")
    else:
        text = KMOD.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"KMOD_EXPLICIT_DECL\s*\(\s*([^,]+?)\s*,\s*\"([^\"]+)\"", text)
        if not m:
            err("no encuentro KMOD_EXPLICIT_DECL en kmod_info.cpp")
        else:
            kid, kver = m.group(1).strip(), m.group(2).strip()
            if kid != bundle_id:
                err(f"identificador distinto: kmod_info={kid!r} Info.plist={bundle_id!r}")
            else:
                ok(f"identificador coincide: {kid}")
            if kver != version:
                err(f"version distinta: kmod_info={kver!r} Info.plist={version!r}")
            else:
                ok(f"version coincide: {kver}")
        for sym in ("_realmain", "_antimain", "_kext_apple_cc"):
            if sym not in text:
                err(f"kmod_info.cpp no define {sym}")

    print("3. IOKitPersonalities")
    pers = info.get("IOKitPersonalities", {})
    if not pers:
        err("IOKitPersonalities vacio: el kext cargaria pero no se emparejaria con nada")
    sources = "\n".join(p.read_text(encoding="utf-8", errors="replace")
                        for p in SRC.glob("*.?pp")) if SRC.is_dir() else ""
    for name, p in pers.items():
        cls = p.get("IOClass")
        if not cls:
            err(f"personalidad {name}: falta IOClass")
        elif f"OSDefineMetaClassAndStructors({cls}," not in sources:
            err(f"personalidad {name}: IOClass {cls} no existe en el codigo")
        else:
            ok(f"personalidad {name} -> IOClass {cls} existe")
        if p.get("CFBundleIdentifier") != bundle_id:
            err(f"personalidad {name}: CFBundleIdentifier no coincide con el del bundle")
        if p.get("IOProviderClass") != "IOPCIDevice":
            warn(f"personalidad {name}: IOProviderClass = {p.get('IOProviderClass')!r}")

        print("4. Emparejamiento PCI")
        match = p.get("IOPCIPrimaryMatch", "")
        if not match:
            err(f"personalidad {name}: falta IOPCIPrimaryMatch")
            continue
        # Formato de IOKit: 0xDDDDVVVV  (device en la parte alta, vendor en la baja)
        ids = []
        for tok in match.split():
            try:
                v = int(tok, 16)
            except ValueError:
                err(f"IOPCIPrimaryMatch: {tok!r} no es hexadecimal")
                continue
            ids.append((v & 0xFFFF, (v >> 16) & 0xFFFF))  # (vendor, device)
        for vend, dev in ids:
            if vend != TARGET_VENDOR and vend in (0xB520, 0xB852, 0xB85B):
                err(f"IOPCIPrimaryMatch invertido: 0x{dev:04x}{vend:04x} pone el vendor "
                    f"donde va el device. El formato correcto es 0xDEVICEVENDOR")
        if (TARGET_VENDOR, TARGET_DEVICE) in ids:
            ok(f"cubre nuestra tarjeta {TARGET_VENDOR:04x}:{TARGET_DEVICE:04x}")
        else:
            err(f"NO cubre {TARGET_VENDOR:04x}:{TARGET_DEVICE:04x}; encontrados: "
                + ", ".join(f"{v:04x}:{d:04x}" for v, d in ids))

    print("5. OSBundleLibraries")
    libs = info.get("OSBundleLibraries", {})
    needed = {"com.apple.kpi.iokit", "com.apple.kpi.libkern", "com.apple.kpi.mach"}
    if "IOPCIDevice" in sources or "IOPCIFamily" in str(libs):
        needed.add("com.apple.iokit.IOPCIFamily")
    faltan = needed - set(libs)
    if faltan:
        err("faltan dependencias: " + ", ".join(sorted(faltan)))
    else:
        ok(f"{len(libs)} dependencias declaradas, incluidas las necesarias")
    if "OSKextRequestResource" in sources and "com.apple.kpi.libkern" not in libs:
        err("se usa OSKextRequestResource pero no se declara com.apple.kpi.libkern")

    print()
    print(f"RESULTADO: {len(errors)} fallos, {len(warnings)} avisos")
    if errors:
        print("El kext seria rechazado o no se emparejaria. Corrige lo anterior.")
        return 1
    print("OK - el bundle esta bien formado. macOS deberia aceptarlo.")
    print("Esto NO garantiza que el chip responda: eso solo se sabe en el portatil.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
