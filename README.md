# RTL8852BT-macOS — driver WiFi para Realtek RTL8852BE-VT en macOS

Proyecto iniciado el 18/09/2026 para el HP OmniBook 7 Aero 13-bg1xxx.

## Estado real: FASES 1 y 2a ESCRITAS, sin compilar. NO DA WIFI TODAVIA.

**Lee [ROADMAP.md](ROADMAP.md): es el guion completo hasta el 100%, fase a fase,
con criterio de aceptacion y como verificar cada una bajo OpenCore.**

| Fase | Que hace | Estado |
|------|----------|--------|
| 0 | Identificar chip, PCI ID, firmware y fuente de referencia | HECHO |
| 1 | Kext que se engancha al PCI 10EC:B520, mapea BAR2, lee la version del chip y carga/valida el firmware | ESCRITO, sin compilar (hace falta un Mac con Xcode) |
| 2a | Encendido y apagado del MAC + bus xtal_si (port de rtw8852bt_pwr_on_func) | ESCRITO, sin compilar. Registros verificados uno a uno |
| 2b | Anillos DMA TX/RX e interrupciones (pci.c) | pendiente, ~3.900 lineas |
| 2c | Lectura de efuse: direccion MAC y calibracion (efuse.c) | pendiente |
| 2d | Descarga del firmware al chip por H2C (fw.c) | pendiente. Hito clave del proyecto |
| 3 | Init de PHY/BB/RF con las tablas de rtw8852bt (rtw8852b_table.c, 750 KB) | pendiente |
| 4 | Pila 802.11: scan, auth, assoc, cifrado WPA2/3. Se reutiliza la de itlwm (net80211 de OpenBSD portada) | pendiente, la mayor parte del trabajo |
| 5 | Integracion con IO80211Family de Apple para que aparezca en Ajustes > Wi-Fi | pendiente |

Escrito hasta ahora: ~700 lineas de las ~45.000 utiles.

Estimacion honesta: itlwm (el equivalente para Intel) le llevo a su autor unos dos anos
a tiempo parcial partiendo del driver de OpenBSD. Aqui el punto de partida es peor:
rtw89 es un driver de Linux que depende de mac80211, y no existe port a BSD.

## Hardware objetivo

```
Vendor/Device PCI : 10EC:B520  (Realtek RTL8852BE-VT = RTL8852BT)
Subsystem         : 103C:88E9  (HP)
Driver Linux      : drivers/net/wireless/realtek/rtw89  (rtw8852bte.c -> rtw8852bt.c)
Firmware          : rtw89/rtw8852bt_fw.bin  (v0.29.122, 928.714 bytes, en firmware/)
BAR de registros  : BAR2 (MMIO), igual que en pci.c:rtw89_pci_setup_mapping
Registro de ver.  : R_AX_SYS_CFG1 (0x00F0), bits 15:12 = version del silicio
```

## Estructura del proyecto

```
RTL8852BT-macOS/
  README.md                  <- este archivo
  firmware/rtw8852bt_fw.bin  <- firmware oficial de linux-firmware (licencia Realtek, redistribuible)
  reference/rtw89-linux/     <- copia del driver Linux (commit en LINUX_COMMIT.txt). SOLO LECTURA.
  ROADMAP.md                 <- EL GUION hasta el 100%, fase a fase
  tools/
    parse_fw.py              <- valida el firmware (ejecutable en Windows, ya probado)
    verify_regs.py           <- comprueba rtw89_regs.h contra el driver Linux (ya probado)
    install_to_opencore.py   <- inyecta el kext compilado en la EFI de OpenCore
  kext/
    Info.plist               <- matching PCI y dependencias del kernel
    Makefile                 <- compila con clang + Kernel.framework en macOS
    src/RTL8852BT.hpp/.cpp   <- driver IOKit fase 1
    src/RTL8852BT_power.cpp  <- fase 2a: encendido del MAC y xtal_si
    src/rtw89_regs.h         <- mapa de registros (105 defines, verificados)
    src/rtw89_compat.h       <- shims Linux -> IOKit (BIT, GENMASK, u32, le32...)
    src/rtw89_fw_hdr.h       <- cabecera del firmware (copiada de fw.h)
```

## Como compilar

### Opcion A (recomendada): GitHub Actions, sin necesidad de Mac

Cada push dispara el workflow `.github/workflows/build.yml`, que compila el kext en un
runner **macOS** de GitHub con Xcode ya instalado. No hace falta ni un Mac ni una VM.

1. Ve a la pestana **Actions** del repositorio.
2. Abre la ejecucion mas reciente de `build-kext`.
3. Si el job `Compilar el kext en macOS` esta en verde, descarga el artefacto
   **RTL8852BT.kext** desde la parte inferior de la pagina.
4. Si esta en rojo, el paso *Diagnostico si algo fallo* dice si el problema es
   nuestro codigo o el entorno, y el artefacto **build-log** trae la salida completa.

Tambien se puede lanzar a mano: Actions > build-kext > *Run workflow*.

El workflow ademas valida el firmware y comprueba que el mapa de registros sigue siendo
fiel al driver Linux, asi que una transcripcion mal copiada se detecta sola.

### Opcion B: en local, en un Mac o en una VM de macOS

```sh
xcode-select --install          # una vez
cd kext
make                            # genera build/RTL8852BT.kext
```

### Instalar el kext en el USB de OpenCore

```sh
python tools/install_to_opencore.py <ruta-al-USB>/EFI --kext kext/build/RTL8852BT.kext
```

## Como probar la fase 1

1. macOS con SIP desactivado o `csr-active-config` que permita kexts sin firmar
   (el OpenCore del USB ya lleva 03080000).
2. Anadir `RTL8852BT.kext` a `EFI/OC/Kexts` y a `Kernel > Add` del config.plist.
3. Arrancar y ejecutar:
   ```sh
   log show --last 5m --predicate 'sender == "RTL8852BT"'
   ```
   Exito de fase 1 = ves algo como:
   ```
   RTL8852BT: attached to PCI 10ec:b520 (subsys 103c:88e9)
   RTL8852BT: BAR2 mapped, 64 KB at ...
   RTL8852BT: R_AX_SYS_CFG1=0x........  chip cv=N
   RTL8852BT: firmware rtw8852bt_fw.bin OK, v0.29.122.2, N sections
   ```
   Eso demuestra que el kext habla con el chip. Nada mas.

## Plan tecnico para las fases siguientes

La estrategia menos arriesgada es NO escribir una pila 802.11 nueva sino
reutilizar la de `itlwm` (https://github.com/OpenIntelWireless/itlwm, GPL):

- `itlwm/itl80211/` ya contiene net80211 de OpenBSD portado a IOKit, con
  scan/auth/assoc/WPA y la integracion con IO80211Family (`AirportItlwm`).
- itlwm define una interfaz `ItlHalService` que cada HAL (iwm, iwn, iwx)
  implementa. El trabajo es crear `hal_rtw89/` que implemente esa interfaz
  con el codigo de rtw89.
- rtw89 esta escrito contra mac80211 de Linux. Hay que sustituir cada llamada
  `ieee80211_*` por su equivalente en net80211 (`ieee80211com`). Esa tabla de
  equivalencias es la parte mas larga y no existe hecha.

Orden recomendado dentro de rtw89 (por dependencias):
1. `pci.c` (DMA rings, interrupciones, MSI) -> IOBufferMemoryDescriptor + IOInterruptEventSource
2. `mac.c` + `mac_be.c`: power on, descarga de firmware (`fw.c` rtw89_fw_download)
3. `efuse.c`: leer MAC address y calibracion
4. `phy.c` + `rtw8852bt.c` + `rtw8852bt_rfk.c` + tablas: init de radio
5. `core.c`: tx/rx path, rate control
6. `cam.c`, `ser.c`, `coex.c`: claves, recuperacion de errores, coexistencia BT

## Que NO se puede hacer

- Portar el driver de Windows del portatil: es binario cerrado, no hay fuente.
- Compilar el kext en Windows: hace falta Kernel.framework y un macOS.
  (Pero SI se puede compilar sin Mac usando el runner macOS de GitHub Actions; ver arriba.)
- Probar el kext sin el hardware: hay que arrancar el portatil con el USB de OpenCore.
- Que esto funcione "en unas horas". Es un proyecto de meses.

## Licencias

- rtw89: Dual BSD-3-Clause / GPL-2.0 (Realtek). Se usa bajo BSD-3.
- Firmware: licencia de redistribucion de Realtek (ver linux-firmware/LICENCE.rtlwifi_firmware.txt).
- itlwm (si se reutiliza): GPL-2.0, lo que obliga a publicar este driver como GPL.
