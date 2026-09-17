# GUION: de aqui a un kext de WiFi 100% funcional bajo OpenCore

Proyecto: RTL8852BT-macOS · Chip: Realtek RTL8852BE-VT (PCI 10EC:B520) · Equipo: HP OmniBook 7 Aero 13-bg1xxx
Guion escrito el 18/09/2026. Cada fase tiene tareas, criterio de aceptacion y como verificarlo bajo OpenCore.

---

## 0. Lo que tienes que saber antes de empezar

Esto no es un guion de horas. Es un guion de meses. Te digo el numero para que decidas
con datos, no para desanimarte:

| Referencia | Trabajo real |
|---|---|
| itlwm (WiFi Intel en macOS) | ~2 anos a tiempo parcial, partiendo de un driver **BSD** ya escrito |
| Este proyecto | Partes de un driver **Linux** que depende de mac80211. No existe port a BSD |
| Codigo a portar | ~45.000 lineas utiles de las 107 fuentes de rtw89 |
| Hecho hasta ahora | ~700 lineas (fases 1 y 2a) |

El grafo de conocimiento del proyecto (`graphify-out/graph.html`) confirma la dificultad:
`rtw89_debug()` toca 60 comunidades distintas y `rtw89_phy_write32_mask()` tiene 430 aristas.
No hay modulos aislados que portar por separado; todo esta acoplado por el acceso a registros.

**Bloqueo duro nº1: necesitas un macOS para compilar.** No existe forma de compilar un kext
en Windows. Kernel.framework solo viene con Xcode. Esto no se puede sortear.

**Bloqueo duro nº2: el enfoque solo-kext no llega al 100%.** Un kext propio puede encender el
chip y mover paquetes, pero para que aparezca en Ajustes > Wi-Fi con escaneo de redes y WPA3
hay que hablar con `IO80211Family`, que es cerrado. La via realista es implementar la interfaz
`ItlHalService` de itlwm y dejar que `AirportItlwm` haga esa parte (fases 4 y 5).

---

## 1. Requisito previo: entorno de compilacion (hazlo primero)

Sin esto las fases siguientes no se pueden ni probar.

### Opcion A (recomendada): macOS en maquina virtual sobre tu Windows

```
VMware Workstation Pro + unlocker, o QEMU/KVM
16 GB RAM asignados, 120 GB disco
Instalar Xcode desde la App Store, luego:  xcode-select --install
```

Tu Ryzen AI 7 350 con 32 GB va sobrado. Sin aceleracion grafica, pero para compilar sobra.

### Opcion B: un Mac de segunda mano

Un Mac mini Intel o M-series. Compila mas rapido y sirve para comparar comportamiento.

### Verificacion del entorno

```sh
cd kext && make
```

**Criterio de aceptacion:** `build/RTL8852BT.kext` existe y `kextlibs -xml build/RTL8852BT.kext`
no reporta dependencias sin resolver.

Espera errores de compilacion la primera vez. El codigo de fases 1 y 2a esta escrito pero
**nunca se ha compilado**. Los fallos probables:
- `IODelay`/`IOSleep` necesitan `<IOKit/IOLib.h>` (ya incluido).
- `FIELD_PREP` con `__builtin_ffsll` sobre `u32`: si clang se queja, castea a `u64`.
- `memcpy` en kernel: si falta, usa `bcopy` o `__builtin_memcpy`.
- Orden de includes en `rtw89_compat.h` frente a los headers de XNU.

---

## 2. Como cargar el kext bajo OpenCore (el bucle de pruebas)

Este es el ciclo que vas a repetir cientos de veces. Automatizalo ya.

### 2.1 Preparar OpenCore para aceptar kexts sin firmar

En `EFI/OC/config.plist` del USB que ya tienes en el Escritorio:

| Clave | Valor | Por que |
|---|---|---|
| `Misc > Security > SecureBootModel` | `Disabled` | ya esta asi |
| `NVRAM > ... > csr-active-config` | `03080000` o `FF0F0000` | SIP permisivo, ya esta en 03080000 |
| `NVRAM > ... > boot-args` | anade `keepsyms=1 -v` | ya estan puestos |
| `Kernel > Quirks > DisableLinkeditJettison` | `true` | ya esta, hace falta para `kextcache` |

### 2.2 Inyectar el kext

```sh
python tools/install_to_opencore.py <ruta-al-USB>/EFI --kext kext/build/RTL8852BT.kext
```

El script copia el kext a `EFI/OC/Kexts/` y anade su entrada en `Kernel > Add` del
config.plist en el orden correcto (despues de Lilu y VirtualSMC, antes del resto).

### 2.3 Leer el log del kext

En macOS ya arrancado:

```sh
log show --last 10m --predicate 'eventMessage CONTAINS "RTL8852BT"' --style compact
```

Si macOS no llega a arrancar, el log se ve en la pantalla verbose. Fotografia las
ultimas 20 lineas: contienen el panic o el ultimo registro leido.

**Truco:** con `Misc > Debug > Target = 67` (ya configurado) OpenCore escribe
`opencore-<fecha>.txt` en la raiz de la particion EFI. Ahi queda el arranque completo.

---

## 3. FASE 1 — Handshake con el chip (ESCRITA, sin compilar)

**Archivos:** `kext/src/RTL8852BT.{hpp,cpp}`, `rtw89_compat.h`, `rtw89_fw_hdr.h`, `Info.plist`

Ya implementado:
- Emparejamiento PCI con 10EC:B520 via `IOPCIPrimaryMatch`.
- `setMemoryEnable` + `setBusMasterEnable` (equivale a `pci_enable_device` + `pci_set_master`).
- Mapeo de BAR2 (`pci.c` usa `bar_id = 2`).
- Lectura de `R_AX_SYS_CFG1` (0x00F0) para la version de silicio.
- Carga y validacion del firmware desde `Contents/Resources/rtw8852bt_fw.bin`,
  replicando `rtw89_mfw_recognize` y `rtw89_fw_hdr_parser_v0`.

**Criterio de aceptacion.** En el log aparece:

```
RTL8852BT: attached to PCI 10ec:b520 (subsys 103c:88e9)
RTL8852BT: BAR2 mapped, N KB at 0x...
RTL8852BT: R_AX_SYS_CFG1=0x........  chip cv=N
RTL8852BT: firmware rtw8852bt_fw.bin OK, v0.29.122.2, 3 sections
```

**Si `R_AX_SYS_CFG1` devuelve 0xFFFFFFFF:** el BAR no esta mapeado o el dispositivo
esta en D3. Comprueba que `setMemoryEnable(true)` se llamo y mira
`Booter > Quirks > SetupVirtualMap`.

**Ya validado en Windows:** `python tools/parse_fw.py` confirma que el formato del
firmware que espera el kext coincide con el fichero real. De hecho ese script detecto
un error mio en la numeracion de tipos de firmware (NORMAL es 1, no 0).

---

## 4. FASE 2 — Encender el MAC y cargar el firmware

### 4.1 FASE 2a — Secuencia de encendido (ESCRITA, sin compilar)

**Archivos:** `kext/src/RTL8852BT_power.cpp`, `rtw89_regs.h`

Port directo de `rtw8852bt_pwr_on_func` y `rtw8852bt_pwr_off_func`. Detalle importante
que descubri leyendo la fuente: el 8852BT tiene `pwr_on_seq = NULL`, o sea **no usa tablas
de secuencia** como los chips antiguos, usa una funcion imperativa. Por eso se porto tal cual.

Incluye tambien el bus serie del cristal (`xtal_si`), que es escritura indirecta a registros
analogicos a traves de `R_AX_WLAN_XTAL_SI_CTRL` (0x0270).

**Criterio de aceptacion:**

```
RTL8852BT: chip acv=N
RTL8852BT: MAC encendido: DMAC_FUNC_EN=0x... CMAC_FUNC_EN=0x... IC_PWR_STATE=1
```

`IC_PWR_STATE=1` significa `MAC_AX_MAC_ON`. Ese es el hito: el MAC esta vivo.

**Si da timeout en `B_AX_RDY_SYSPWR`:** el chip no recibe alimentacion. Revisa si la
BIOS de HP deja la tarjeta encendida cuando no hay driver de Windows cargado.

### 4.2 FASE 2b — Anillos DMA y interrupciones (PENDIENTE, lo mas laborioso de la fase)

**Fuente:** `reference/rtw89-linux/pci.c` (3.900 lineas), `pci.h`

Tareas:
1. `rtw89_pci_alloc_trx_rings` → `IOBufferMemoryDescriptor` con `kIOMemoryPhysicallyContiguous`
   y `IODMACommand` para las direcciones fisicas de 32/64 bits.
2. Descriptores TX (`rtw89_pci_tx_bd_32`) y RX (`rtw89_pci_rx_bd_32`): son estructuras
   `__packed` en memoria compartida. Se copian tal cual desde `pci.h`.
3. Registrar la interrupcion: `IOInterruptEventSource` o `IOFilterInterruptEventSource`
   sobre el `IOPCIDevice`. MSI se pide con `fPci->setProperty("IOPCIMSIMode", ...)`.
4. Puntero de escritura/lectura de cada anillo: `rtw89_pci_ops_reset`, `__rtw89_pci_tx_kick_off`.

**Criterio de aceptacion:** una interrupcion recibida y contada en el log despues de
`rtw89_pci_enable_intr`. Aun sin transmitir nada.

**Trampa conocida:** macOS no garantiza memoria por debajo de 4 GB salvo que lo pidas.
Si el chip esta en modo DAC de 32 bits (`rtw89_pci_cfg_dac`), tienes que forzar
`kIOMemoryMapperNone` y mascara de 32 bits en el `IODMACommand`, o el DMA escribira
en una direccion que el chip no puede alcanzar.

### 4.3 FASE 2c — Leer la efuse (PENDIENTE)

**Fuente:** `efuse.c`, y `rtw89_mac_efuse_read_*` en `mac.c`

Aqui sale la **direccion MAC real** de tu tarjeta y las constantes de calibracion.
Sin esto no puedes asociarte a nada.

**Criterio de aceptacion:** el log imprime una MAC que empieza por un OUI de Realtek
o de HP, no `00:00:00:00:00:00` ni `FF:FF:FF:FF:FF:FF`. Comparala con la que ve Windows
en `getmac /v` para confirmar que la lectura es correcta. Este es el mejor test cruzado
que tienes: mismo hardware, dos sistemas, un valor que debe coincidir.

### 4.4 FASE 2d — Descargar el firmware al chip (PENDIENTE)

**Fuente:** `fw.c` → `rtw89_fw_download`, `rtw89_fw_download_hdr`, `rtw89_fw_download_main`

El firmware ya se valida en fase 1; aqui se **envia** al chip por H2C sobre DMA, seccion
a seccion, a las direcciones que trae la cabecera (0xb8970000, 0xb8e12c00, 0xb8e116a0),
en paquetes de `FWDL_SECTION_PER_PKT_LEN` = 2020 bytes.

**Criterio de aceptacion:** el chip contesta un C2H de `fwdl` con estado OK, y
`R_AX_WCPU_FW_CTRL` indica firmware listo. Equivale a `RTW89_FLAG_FW_RDY` en Linux.

**Este es el hito mas importante de todo el proyecto.** Con el firmware corriendo dentro
del chip, el resto es configuracion. Sin el, nada mas funciona.

---

## 5. FASE 3 — Inicializar la radio (PENDIENTE)

**Fuente:** `phy.c` (266 KB), `rtw8852bt.c`, `rtw8852bt_rfk.c` (140 KB),
`rtw8852b_table.c` (750 KB de tablas), `rtw8852bt_rfk_table.c`

Es la fase con mas volumen de codigo pero la mas mecanica: son tablas de valores que se
escriben en registros en un orden concreto. Casi no hay logica que reinterpretar.

Orden:
1. `rtw8852bt_bb_reset`, init del baseband con `rtw8852b_table.c`.
2. Init de RF con `rtw8852bt_rfk.c`: DACK, ADDCK, IQK, DPK, TSSI. Son calibraciones
   que el chip necesita para transmitir con potencia correcta.
3. `rtw89_phy_init_bb_reg` / `rtw89_phy_init_rf_reg`.
4. Ajuste de canal y ancho de banda: `rtw8852bt_set_channel`.

**Criterio de aceptacion:** despues del init, leer el termometro del chip
(`rtw8852bt_get_thermal`) devuelve un valor plausible (20-60 aproximadamente), no 0 ni 0xFF.
Es la senal de que el RF esta alimentado y calibrado.

**Aviso legal real:** la potencia de transmision depende de estas calibraciones y de las
tablas regulatorias (`regd.c`). Una radio mal calibrada puede emitir fuera de los limites
legales de tu pais. No te saltes `regd.c` ni las tablas SAR.

---

## 6. FASE 4 — Pila 802.11 (PENDIENTE, la fase mas larga)

**No escribas una pila nueva.** Reutiliza la de itlwm:

```sh
git clone https://github.com/OpenIntelWireless/itlwm
```

`itlwm/itl80211/` es net80211 de OpenBSD portado a IOKit, con escaneo, autenticacion,
asociacion y WPA ya resueltos. Cada HAL (`hal_iwm`, `hal_iwn`, `hal_iwx`) implementa la
interfaz `ItlHalService`. Tu trabajo es crear `hal_rtw89/` que la implemente.

El nucleo del trabajo es una tabla de equivalencias: rtw89 esta escrito contra `mac80211`
de Linux, e itl80211 ofrece `ieee80211com` de BSD. Hay que traducir cada llamada.
**Esa tabla no existe hecha. Es el trabajo mas largo del proyecto.**

Equivalencias principales a resolver:

| Linux mac80211 | BSD net80211 (itl80211) |
|---|---|
| `struct ieee80211_hw` | `struct ieee80211com` |
| `struct ieee80211_vif` | `struct ieee80211vap` / `ic` |
| `struct ieee80211_sta` | `struct ieee80211_node` |
| `struct sk_buff` | `struct mbuf` |
| `ieee80211_rx()` | `ieee80211_input()` |
| `ieee80211_tx_status()` | callback en `ic_start` |
| `ieee80211_ops` | `ItlHalService` virtuals |
| `ieee80211_scan_completed()` | `ieee80211_end_scan()` |

**Criterio de aceptacion parcial:** un escaneo pasivo que devuelva al menos un SSID real
de tu casa, con su BSSID y potencia. Compara con lo que ve Windows.

**Criterio de aceptacion de fase:** asociacion completa a una red WPA2 y `ping` que responde.

**Punto de decision.** El grafo marco como AMBIGUA la relacion entre el kext actual y
`hal_rtw89`. Aqui hay que decidir: `RTL8852BT.kext` se convierte en la capa de hardware que
`hal_rtw89` invoca, y deja de ser un kext independiente. Mi recomendacion: conservar
`RTL8852BT_power.cpp`, `rtw89_regs.h` y el acceso a registros como una biblioteca interna, y
que `hal_rtw89` sea quien exponga `ItlHalService`.

**Aviso de licencia:** itlwm es GPL-2.0. Si reutilizas su pila, tu driver tiene que
publicarse como GPL. rtw89 es dual BSD/GPL, asi que es compatible, pero el resultado
final queda bajo GPL. Tenlo en cuenta antes de escribir la primera linea de `hal_rtw89`.

---

## 7. FASE 5 — Que aparezca en Ajustes > Wi-Fi (PENDIENTE)

`AirportItlwm` es la variante de itlwm que se registra contra `IO80211Family` de Apple y
hace que la tarjeta salga en el menu de WiFi como una nativa. Si `hal_rtw89` implementa
bien `ItlHalService`, esta fase es mayormente configuracion de `Info.plist` y elegir la
version de `IO80211Family` correcta para tu macOS (cambia entre Sonoma, Sequoia y Tahoe).

**Criterio de aceptacion final, el 100%:**

1. La tarjeta aparece en Ajustes > Wi-Fi sin herramientas externas.
2. Lista las redes de tu zona y se conecta a WPA2 y WPA3.
3. Sobrevive a dormir y despertar la tapa.
4. Velocidad razonable en WiFi 6 (esperables 300-600 Mbps en 5 GHz con este chip).
5. La coexistencia con Bluetooth no corta el audio (`coex.c`, fase opcional 6).

---

## 8. Orden recomendado y como no perderte

```
[HECHO]     Fase 0   identificacion del chip y fuentes
[HECHO]     Fase 1   attach PCI, BAR2, version, validacion de firmware
[HECHO]     Fase 2a  encendido del MAC + xtal_si
[AHORA]     ---- montar macOS en VM y COMPILAR lo escrito ----
[SIGUIENTE] Fase 2b  anillos DMA + interrupciones      (pci.c)
            Fase 2c  efuse: MAC address                 (efuse.c)
            Fase 2d  descarga de firmware al chip       (fw.c)   <- hito clave
            Fase 3   init de BB/RF + tablas             (phy.c, rtw8852bt_rfk.c)
            Fase 4   pila 802.11 via itlwm              (hal_rtw89)
            Fase 5   IO80211Family / AirportItlwm
            Fase 6   coexistencia Bluetooth (opcional)  (coex.c)
```

Regla de oro: **no avances de fase sin que el criterio de aceptacion de la anterior
aparezca en el log.** Si te salt s la efuse y vas al firmware, vas a depurar dos cosas
a la vez sin saber cual falla.

### Usa el grafo para orientarte

```sh
graphify query "que funciones llaman a rtw89_fw_download"
graphify path "rtw89_pci_setup_mapping" "rtw89_fw_download"
graphify explain "rtw8852bt_pwr_on_func"
```

El grafo tiene las 23.373 aristas del driver Linux indexadas. Cuando no sepas que necesita
una funcion, preguntaselo al grafo antes de leer 200 KB de C.

---

## 9. Plan B honesto

Si en la fase 2b o 2d te atascas semanas, estas son las salidas, de mejor a peor:

1. **Cambiar la tarjeta M.2.** Una Intel AX210 cuesta poco y funciona con itlwm ya hecho,
   hoy, sin escribir codigo. Antes comprueba dos cosas: que el modulo no este soldado
   (abrir la tapa inferior) y que la BIOS de HP no tenga lista blanca de tarjetas.
   **Esta es la via mas corta a WiFi funcionando en macOS en este portatil.**
2. **Adaptador USB WiFi** con chipset ya soportado. Feo pero inmediato.
3. **Ethernet USB** y olvidarte del WiFi en macOS.
4. **macOS en maquina virtual** con red puenteada desde Windows. La VM no toca el WiFi.

No te digo esto para que abandones. Te lo digo porque la opcion 1 es tan barata que seria
absurdo descubrirla despues de tres meses de trabajo.

---

## 10. Estado de verificacion de este repositorio

Se honesto contigo mismo sobre que esta probado:

| Elemento | Estado |
|---|---|
| `tools/parse_fw.py` | **Ejecutado y correcto** en Windows contra el firmware real |
| Firmware `rtw8852bt_fw.bin` | **Descargado y verificado**, 928.714 bytes, v0.29.122.2 |
| `reference/rtw89-linux/` | **Completo**, commit b5a051f del 17/09/2026 |
| Registros en `rtw89_regs.h` | **Copiados literalmente** de reg.h y mac.h, uno a uno |
| `RTL8852BT.cpp` fase 1 | Escrito, **nunca compilado** |
| `RTL8852BT_power.cpp` fase 2a | Escrito, **nunca compilado, nunca ejecutado en el chip** |
| Fases 2b a 5 | **No escritas** |

Nada de lo que hay en `kext/` ha tocado hardware real todavia. El primer paso del guion
es cambiar eso.
