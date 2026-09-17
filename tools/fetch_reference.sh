#!/bin/sh
# fetch_reference.sh - descarga el driver rtw89 de Linux a reference/rtw89-linux/
#
# El driver de referencia son 11 MB de codigo del kernel que NO se guardan en
# este repositorio: se descargan cuando hacen falta. Lo necesitan:
#   - tools/verify_regs.py  (compara nuestro mapa de registros con el original)
#   - tu, para leer la fuente al portar cada fase
#
# El kext NO lo necesita para compilar: kext/src/ tiene copias propias de todo
# lo que usa.
#
# Uso:  sh tools/fetch_reference.sh
set -e

DEST="$(cd "$(dirname "$0")/.." && pwd)/reference"
SUB="drivers/net/wireless/realtek/rtw89"

if [ -d "$DEST/rtw89-linux" ] && [ -f "$DEST/rtw89-linux/core.c" ]; then
    echo "Ya existe $DEST/rtw89-linux, no se vuelve a descargar."
    echo "Para forzar:  rm -rf '$DEST/rtw89-linux' && sh tools/fetch_reference.sh"
    exit 0
fi

mkdir -p "$DEST"
TMP="$DEST/.linux-sparse"
rm -rf "$TMP"

echo "Clonando solo $SUB de torvalds/linux (sparse checkout)..."
git clone -q --depth 1 --filter=blob:none --sparse \
    https://github.com/torvalds/linux.git "$TMP"

cd "$TMP"
git sparse-checkout set "$SUB" include/linux --skip-checks

cp -r "$SUB" "$DEST/rtw89-linux"
cp include/linux/ieee80211.h "$DEST/rtw89-linux/"
git log -1 --format='%H %cd' > "$DEST/rtw89-linux/LINUX_COMMIT.txt"

cd "$DEST"
rm -rf "$TMP"

echo "Listo: $DEST/rtw89-linux"
echo "Commit: $(cat "$DEST/rtw89-linux/LINUX_COMMIT.txt")"
echo "Ficheros: $(ls "$DEST/rtw89-linux" | wc -l)"
