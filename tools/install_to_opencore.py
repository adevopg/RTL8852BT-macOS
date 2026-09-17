#!/usr/bin/env python3
"""install_to_opencore.py - inyecta un kext compilado en una EFI de OpenCore.

Uso:
    python tools/install_to_opencore.py <ruta-a-EFI> --kext <ruta-al-.kext>
    python tools/install_to_opencore.py E:/EFI --kext kext/build/RTL8852BT.kext
    python tools/install_to_opencore.py E:/EFI --kext ... --dry-run
    python tools/install_to_opencore.py E:/EFI --remove RTL8852BT.kext

Que hace:
  1. Copia el .kext a <EFI>/OC/Kexts/ (reemplazando el anterior).
  2. Anade o actualiza su entrada en Kernel > Add del config.plist, leyendo
     CFBundleExecutable del Info.plist del propio kext.
  3. Lo coloca DESPUES de Lilu y VirtualSMC, que deben cargarse primero.
  4. Guarda una copia de seguridad config.plist.bak antes de tocar nada.

Funciona en Windows y en macOS. No necesita OpenCore instalado ni herramientas
de Apple: solo la biblioteca plistlib de Python.
"""
import argparse
import plistlib
import shutil
import sys
from pathlib import Path

# Kexts que deben cargarse antes que el nuestro, en este orden.
LOAD_FIRST = ["Lilu.kext", "VirtualSMC.kext"]


def read_bundle_info(kext: Path):
    """Devuelve (bundle_id, executable) leidos del Info.plist del kext."""
    info_path = kext / "Contents" / "Info.plist"
    if not info_path.is_file():
        sys.exit(f"ERROR: {kext} no tiene Contents/Info.plist (no es un kext valido)")
    with open(info_path, "rb") as f:
        info = plistlib.load(f)
    bundle_id = info.get("CFBundleIdentifier")
    if not bundle_id:
        sys.exit(f"ERROR: {info_path} no declara CFBundleIdentifier")
    return bundle_id, info.get("CFBundleExecutable")


def make_entry(name: str, executable: str | None, comment: str):
    return {
        "Arch": "x86_64",
        "BundlePath": name,
        "Comment": comment,
        "Enabled": True,
        "ExecutablePath": f"Contents/MacOS/{executable}" if executable else "",
        "MaxKernel": "",
        "MinKernel": "",
        "PlistPath": "Contents/Info.plist",
    }


def insert_position(add_list, name: str) -> int:
    """Indice donde insertar: justo detras del ultimo de LOAD_FIRST presente."""
    pos = 0
    for i, e in enumerate(add_list):
        if e.get("BundlePath") in LOAD_FIRST:
            pos = i + 1
    return pos if pos else len(add_list)


def check_resources(kext: Path):
    """Avisa si falta el firmware que el driver pide por OSKextRequestResource."""
    fw = kext / "Contents" / "Resources" / "rtw8852bt_fw.bin"
    if not fw.is_file():
        print("  AVISO: no hay Contents/Resources/rtw8852bt_fw.bin en el kext.")
        print("         El driver cargara pero no podra validar el firmware.")
        print("         El Makefile lo copia solo; si compilaste a mano, copialo.")
    else:
        print(f"  firmware embebido OK ({fw.stat().st_size} bytes)")


def main():
    ap = argparse.ArgumentParser(description="Inyecta un kext en una EFI de OpenCore")
    ap.add_argument("efi", help="ruta a la carpeta EFI (la que contiene OC/ y BOOT/)")
    ap.add_argument("--kext", help="ruta al .kext compilado")
    ap.add_argument("--remove", metavar="NOMBRE.kext",
                    help="quitar esta entrada del config.plist y borrar la carpeta")
    ap.add_argument("--dry-run", action="store_true",
                    help="mostrar lo que haria sin escribir nada")
    args = ap.parse_args()

    efi = Path(args.efi)
    oc = efi / "OC"
    cfg_path = oc / "config.plist"
    kexts_dir = oc / "Kexts"

    if not cfg_path.is_file():
        sys.exit(f"ERROR: no encuentro {cfg_path}. Apunta a la carpeta EFI, no al USB entero.")
    if not args.kext and not args.remove:
        sys.exit("ERROR: indica --kext o --remove")

    with open(cfg_path, "rb") as f:
        cfg = plistlib.load(f)
    add_list = cfg.setdefault("Kernel", {}).setdefault("Add", [])

    # ---- Desinstalar --------------------------------------------------------
    if args.remove:
        name = args.remove
        before = len(add_list)
        cfg["Kernel"]["Add"] = [e for e in add_list if e.get("BundlePath") != name]
        removed = before - len(cfg["Kernel"]["Add"])
        target = kexts_dir / name
        print(f"Quitando {name}: {removed} entrada(s) del config.plist"
              f"{', carpeta presente' if target.exists() else ''}")
        if not args.dry_run:
            shutil.copy2(cfg_path, cfg_path.with_suffix(".plist.bak"))
            with open(cfg_path, "wb") as f:
                plistlib.dump(cfg, f, sort_keys=True)
            if target.exists():
                shutil.rmtree(target)
            print("Hecho.")
        else:
            print("(dry-run: no se escribio nada)")
        return

    # ---- Instalar -----------------------------------------------------------
    kext = Path(args.kext)
    if not kext.is_dir():
        sys.exit(f"ERROR: {kext} no existe. Compila primero:  cd kext && make")

    name = kext.name
    bundle_id, executable = read_bundle_info(kext)
    print(f"Kext:   {name}")
    print(f"  id:   {bundle_id}")
    print(f"  exec: Contents/MacOS/{executable}")
    check_resources(kext)

    entry = make_entry(name, executable, bundle_id)
    existing = next((i for i, e in enumerate(add_list)
                     if e.get("BundlePath") == name), None)
    if existing is not None:
        add_list[existing] = entry
        print(f"  entrada actualizada en Kernel > Add (indice {existing})")
    else:
        pos = insert_position(add_list, name)
        add_list.insert(pos, entry)
        print(f"  entrada anadida en Kernel > Add (indice {pos}, tras "
              f"{', '.join(k for k in LOAD_FIRST if any(e.get('BundlePath') == k for e in add_list)) or 'nada'})")

    dest = kexts_dir / name
    print(f"Destino: {dest}")

    if args.dry_run:
        print("(dry-run: no se escribio nada)")
        return

    shutil.copy2(cfg_path, cfg_path.with_suffix(".plist.bak"))
    print(f"  copia de seguridad: {cfg_path.with_suffix('.plist.bak').name}")

    kexts_dir.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(kext, dest)

    with open(cfg_path, "wb") as f:
        plistlib.dump(cfg, f, sort_keys=True)

    print("\nHecho. Ahora:")
    print("  1. Arranca desde el USB (F9 en el HP).")
    print("  2. Una vez en macOS:")
    print("       log show --last 10m --predicate 'eventMessage CONTAINS \"RTL8852BT\"' --style compact")
    print("  3. Si no arranca, fotografia las ultimas lineas del texto verbose.")
    print("\nPara quitarlo:")
    print(f"  python tools/install_to_opencore.py {args.efi} --remove {name}")


if __name__ == "__main__":
    main()
