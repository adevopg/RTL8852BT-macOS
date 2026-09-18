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

**Bloqueo nº1 (RESUELTO): compilar sin Mac.** Los runners macOS de GitHub Actions traen
Xcode. `.github/workflows/build.yml` compila el kext en cada push y publica el binario en
la release `ultimo-build`. Ya no hace falta ni un Mac ni una VM para compilar.
Confirmado el 18/09/2026: compila limpio en macOS 26 con Xcode 26.6, salida Mach-O x86_64.
Sigues necesitando el portatil para PROBARLO, pero eso ya lo tienes.

**Bloqueo duro nº2: el enfoque solo-kext no llega al 100%.** Un kext propio puede encender el
chip y mover paquetes, pero para que aparezca en Ajustes > Wi-Fi con escaneo de redes y WPA3
hay que hablar con `IO80211Family`, que es cerrado. La via realista es implementar la interfaz
`ItlHalService` de itlwm y dejar que `AirportItlwm` haga esa parte (fases 4 y 5).

---

## 1. Entorno de compilacion

### Opcion A (la que usa este repositorio): GitHub Actions, sin Mac

`.github/workflows/build.yml` compila el kext en un runner macOS en cada push y publica
el binario en la release `ultimo-build`. **Ya funciona.** No necesitas nada instalado:
descargas el .tar.gz de Releases y lo inyectas en el USB.

Ademas el workflow valida el firmware y comprueba que el mapa de registros sigue siendo
fiel al driver Linux, asi que una transcripcion mal copiada se detecta sola.

### Opcion B: macOS en maquina virtual sobre tu Windows

Util si quieres iterar rapido sin esperar a CI, o depurar con lldb.

```
VMware Workstation Pro + unlocker, o QEMU/KVM
16 GB RAM asignados, 120 GB disco
Instalar Xcode desde la App Store, luego:  xcode-select --install
```

Tu Ryzen AI 7 350 con 32 GB va sobrado. Sin aceleracion grafica, pero para compilar sobra.

### Opcion C: un Mac de segunda mano

### Estado verificado el 18/09/2026

La primera compilacion en CI salio bien a la primera:

```
Runner:  macOS 26.6.2, imagen macos-26-arm64
Xcode:   26.6, SDK MacOSX26.5
Salida:  Mach-O 64-bit kext bundle x86_64
```

El runner es Apple Silicon y el kext sale x86_64: es compilacion cruzada, que es
exactamente lo que hace falta para tu portatil AMD.

Dos avisos aparecieron y ya estan corregidos: `rtw89_compat.h` redefinia `min`/`max`
que `IOLib.h` ya define, y `IOPCIDevice` esta marcado deprecated a favor de PCIDriverKit
(que es para DriverKit en espacio de usuario, no para kexts, asi que el aviso se silencia).

**Aviso benigno que veras siempre.** El enlazador dice:

```
ld: warning: object file (libkmod.a(c_start.o)) was built for newer 'macOS'
version (26.5) than being linked (10.15)
```

Es porque el Makefile usa `MINVER = 10.15` para que el kext valga tambien en macOS
antiguos, mientras que el SDK del runner es 26.5. No afecta a la carga del kext.
Si algun dia solo te interesa Tahoe, sube `MINVER` en `kext/Makefile` y desaparece.

**Leccion cara que ya esta resuelta:** el kext compilaba y enlazaba sin `kmod_info`
ni `_realmain`, que son el punto de entrada del modulo. Xcode los genera solo; compilando
a mano hay que escribirlos (`kext/src/kmod_info.cpp`). Sin ellos `kextload` habria
rechazado el kext y el fallo solo se habria visto al arrancar el portatil. Lo detecto el
paso de CI que separa los simbolos sin resolver; por eso ese paso ahora **falla el build**
si aparece cualquier simbolo que el kernel no exporte.

**Validacion en macOS Intel, automatica.** GitHub ofrece runners Intel como etiquetas
estandar: `macos-15-intel` y `macos-26-intel`. Ahi `kmutil libraries` resuelve de verdad
los simbolos del kext contra las colecciones del kernel. Eso responde a "¿macOS aceptaria
este kext?" sin montar ninguna maquina virtual, y corre en cada push.

Lo que NO se puede hacer en CI: `kmutil create` de una coleccion auxiliar, porque exige un
Kernel Development Kit ligado a una cuenta de Apple Developer. No hace falta: `kmutil
libraries` ya da la respuesta.

**No te fies de `kextlibs`.** Al compilar cruzado desde Apple Silicon reporta 298 simbolos
no encontrados, porque compara contra los kexts arm64 del sistema. El criterio correcto es
el que aplica ahora el workflow: que los simbolos sin resolver sean todos del kernel
(`_IOLog`, `_IOMalloc`, `OSMetaClass::...`), cosa normal en cualquier kext, y ninguno ajeno.

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

## 3. FASE 1 — Handshake con el chip (COMPILA, sin probar)

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

### 4.1 FASE 2a — Secuencia de encendido (COMPILA, sin probar)

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

### 4.2 FASE 2b — Anillos DMA y interrupciones (ESCRITA Y COMPILA, sin probar)

**Fuente:** `reference/rtw89-linux/pci.c` (3.900 lineas), `pci.h`

Tareas:
1. `rtw89_pci_alloc_trx_rings` → `IOBufferMemoryDescriptor` con `kIOMemoryPhysicallyContiguous`
   y `IODMACommand` para las direcciones fisicas de 32/64 bits.
2. Descriptores TX (`rtw89_pci_tx_bd_32`) y RX (`rtw89_pci_rx_bd_32`): son estructuras
   `__packed` en memoria compartida. Se copian tal cual desde `pci.h`.
3. Registrar la interrupcion: `IOInterruptEventSource` o `IOFilterInterruptEventSource`
   sobre el `IOPCIDevice`. MSI se pide con `fPci->setProperty("IOPCIMSIMode", ...)`.
4. Puntero de escritura/lectura de cada anillo: `rtw89_pci_ops_reset`, `__rtw89_pci_tx_kick_off`.

**Ya implementada** en `kext/src/RTL8852BT_pci.cpp`. Alcance: reservar los anillos,
decirle al chip donde estan y conseguir que llegue una interrupcion. NO transmite ni
recibe todavia.

Solo se montan los canales que el 8852BT usa de verdad. Su `pci_info` enmascara
ACH4..ACH7, CH10 y CH11, asi que quedan siete de transmision y dos de recepcion.

**Criterio de aceptacion.** En el log:

```
RTL8852BT: anillo ACH0   256 desc x 8 B = 2048 B  fisica 0x........
   (nueve lineas, una por anillo)
RTL8852BT: FASE 2b: los 9 anillos retienen su direccion. El chip sabe donde estan.
RTL8852BT: MSI disponible en el indice N
RTL8852BT: interrupcion registrada (indice N)
RTL8852BT: interrupcion #1: HISR00=0x........ ...
```

La linea de los nueve anillos es la importante: despues de escribir cada direccion,
el codigo la RELEE y compara. Si un anillo no retiene lo que le escribimos, el chip
no lo esta viendo, y el log lo dice con nombre y valores en vez de fallar mas tarde.

**La trampa de macOS, ya resuelta.** En Linux `dma_alloc_coherent` da memoria que el
dispositivo puede alcanzar. En macOS hay que pedirlo: con memoria normal el sistema
puede colocarla por encima de los 4 GB y el chip escribiria en una direccion que para
el no existe, corrompiendo memoria ajena. Se usa `inTaskWithPhysicalMask` con mascara
de 32 bits y `kIOMemoryPhysicallyContiguous`, y ademas se verifica a posteriori que la
direccion cae de verdad bajo los 4 GB.

### 4.3 FASE 2c — Leer la efuse (ESCRITA Y COMPILA, sin probar)

**Fuente:** `efuse.c`, y `rtw89_mac_efuse_read_*` en `mac.c`

Aqui sale la **direccion MAC real** de tu tarjeta y las constantes de calibracion.
Sin esto no puedes asociarte a nada.

**Ya implementada** en `kext/src/RTL8852BT_efuse.cpp`. Se adelanto por delante de la 2b
porque la efuse se lee entera por registros (`R_AX_EFUSE_CTRL`, un byte por vuelta con
espera activa), sin DMA ni interrupciones. Por eso puede ir justo tras encender el MAC.

El mapa fisico esta comprimido: cada bloque lleva dos bytes de cabecera que dicen a que
indice logico va y cuales de sus cuatro palabras estan escritas; `0xFF` marca el final.
`rtw89_efuse_8852bt.h` reproduce el struct y deja que el compilador confirme con
`static_assert` que la MAC cae en el offset 0x400, que fue lo que calcule a mano.

**Criterio de aceptacion.** En el log:

```
RTL8852BT: efuse: volcado fisico OK (1216 bytes). Primeros 16: ...
RTL8852BT: efuse: MAC xx:xx:xx:xx:xx:xx  rfe_type=N xtal_k=0xNN pais=ES
RTL8852BT: efuse: MAC valida. Compruebala en Windows con 'getmac /v'
```

**Este es el mejor test cruzado de todo el proyecto.** Arranca Windows, ejecuta
`getmac /v`, y compara. Mismo hardware, dos sistemas, un valor que debe ser identico.
Si coincide, queda validada de golpe la cadena entera: mapeo de BAR, encendido del MAC
y acceso a registros. El codigo ya rechaza una MAC de todo `FF` (efuse en blanco o sin
alimentar), de todo ceros (no se leyo nada) o con el bit multicast puesto.

### 4.4 FASE 2d — Descargar el firmware al chip (ESCRITA ENTERA, sin probar)

**Fuente:** `fw.c` → `rtw89_fw_download`, `rtw89_fw_download_hdr`, `rtw89_fw_download_main`

El chip lleva dentro su propio procesador, el WCPU, que es quien ejecuta el firmware.

**Parte 1, ya escrita** (`kext/src/RTL8852BT_fwdl.cpp`): parar el WCPU, ponerlo en modo
descarga y esperar a que abra el camino. Es solo escritura de registros, y se puede
comprobar sola: si el chip abre el camino, el procesador esta vivo y responde.

Criterio de aceptacion de la parte 1:

```
RTL8852BT: fwdl: camino de descarga abierto. Estado 'listo para recibir firmware' (6)
RTL8852BT: FASE 2d-1 lista: el chip espera el firmware.
```

El log traduce el estado a palabras, porque los fallos que se veran aqui (checksum,
seguridad, version de chip que no coincide) son incomprensibles como numero suelto.

**Parte 2, ya escrita** (`kext/src/RTL8852BT_h2c.cpp`): enviar los bytes por el canal
CH12 sobre los anillos de la fase 2b.

Como viaja un paquete:

```
buffer DMA:  [ descriptor TX 24 B ][ cabecera H2C 8 B ][ datos ]
anillo CH12: una entrada de 8 bytes con la direccion fisica y el tamano
```

Se avanza el puntero de escritura y se escribe en su registro de indice: eso es lo que
despierta al chip. Luego se espera a que el puntero del hardware, que viene en los bits
altos del mismo registro, alcance al nuestro. **Sin esa espera** reutilizariamos el
buffer antes de que el chip lo lea, y el firmware llegaria corrupto de forma
intermitente, que es la peor clase de fallo posible.

Dos formatos distintos, facil de pasar por alto:

| Que se envia | Cabecera H2C | Bit fw_dl |
|---|---|---|
| Cabecera del firmware | si, 8 bytes | no |
| Secciones | no, van en crudo | si |

Para este firmware son tres secciones, a 0xb8970000, 0xb8e12c00 y 0xb8e116a0, enviadas
en trozos de 2020 bytes.

**Criterio de aceptacion, el hito del proyecto:**

```
RTL8852BT: fwdl: cabecera aceptada, camino de descarga abierto
RTL8852BT: fwdl: seccion 0 -> 0xb8970000, 240088 bytes en trozos de 2020
RTL8852BT: fwdl: seccion 0 enviada en 119 paquetes
   (y las otras dos)
RTL8852BT: fwdl: el firmware ha arrancado dentro del chip.
RTL8852BT: FASE 2d COMPLETA
```

Con eso, el procesador del chip esta ejecutando el firmware de Realtek. A partir de ahi
el resto del driver es configurarlo.

**Criterio de aceptacion:** el chip contesta un C2H de `fwdl` con estado OK, y
`R_AX_WCPU_FW_CTRL` indica firmware listo. Equivale a `RTW89_FLAG_FW_RDY` en Linux.

**Este es el hito mas importante de todo el proyecto.** Con el firmware corriendo dentro
del chip, el resto es configuracion. Sin el, nada mas funciona.

---

## 5. FASE 3 — Inicializar la radio (EMPEZADA: 3a escrita)

**Fuente:** `phy.c` (266 KB), `rtw8852bt.c`, `rtw8852bt_rfk.c` (140 KB),
`rtw8852b_table.c` (750 KB de tablas), `rtw8852bt_rfk_table.c`

Es la fase con mas volumen de codigo pero la mas mecanica: son tablas de valores que se
escriben en registros en un orden concreto. Casi no hay logica que reinterpretar.

### 5.1 FASE 3a — Encender la radio (ESCRITA Y COMPILA)

`kext/src/RTL8852BT_rf.cpp`. Enciende los bloques de banda base y radio, y deja montado
el acceso a sus registros.

**Tres espacios de registros distintos.** Confundirlos es el error clasico del port:

| Espacio | Como se accede |
|---|---|
| MAC | tal cual en el BAR: `write32(0x0004, ...)` |
| PHY | desplazado 0x10000: `phyWrite32(0x2344, ...)` |
| RF | base por camino (0xe000 / 0xf000) y direccion por 4: `rfWrite(0, 0x42, ...)` |

Estan en tres parejas de funciones separadas para no mezclarlos nunca.

**Criterio de aceptacion:**

```
RTL8852BT: rf: banda base y radio encendidas. SYS_FUNC_EN=0x.. WLRF_CTRL=0x........
RTL8852BT: rf: termometro camino A=NN camino B=NN (rango 0..63)
RTL8852BT: FASE 3a lista: la radio esta encendida y responde.
```

El termometro es la prueba de vida. Son 6 bits, rango 0 a 63. A temperatura ambiente
tiene que dar un valor intermedio. Si los dos caminos dan 0 o 63 clavados, el bloque no
esta alimentado, y el codigo falla ahi en vez de seguir creyendo que hay radio.

### 5.2 FASE 3b — Las tablas (ESCRITA Y COMPILA)

**Aqui me equivocaba, y a favor.** Tenia previsto escribir un generador para convertir
los 750 KB de `rtw8852b_table.c`, trabajo de dias. No hace falta.

El 8852BT **no lleva tablas compiladas**: en `rtw8852bt.c` sus punteros `bb_table`,
`bb_gain_table`, `rf_table` y `nctl_table` son todos NULL. Sus tablas vienen como
*elementos* pegados al final del propio fichero de firmware, detras del contenedor
multi-firmware. Ya las teniamos desde que descargamos `rtw8852bt_fw.bin`.

Verificado con `python tools/parse_fw.py` sobre el fichero real:

| Elemento | Contenido |
|---|---|
| BB_REG | 1.028 pares direccion/dato |
| RADIO_A | 3.647 pares |
| RADIO_B | 3.630 pares |
| RF_NCTL | 1.849 pares |

Mas once elementos de potencia y regulatorios para mas adelante.

**Trampa que habria costado cara.** Dentro de esas tablas, las direcciones 0xf9 a 0xfe
NO son registros: son esperas codificadas, de 1 microsegundo a 50 milisegundos.
Tratarlas como registros escribiria basura en direcciones bajas de la PHY. Igual que el
dato 0xbabecafe, que significa "saltate este registro". Ambas contempladas.

**Criterio de aceptacion:**

```
RTL8852BT: tablas: BB_REG=1028 RADIO_A=3647 RADIO_B=3630 RF_NCTL=1849 pares
RTL8852BT: tablas: BB_REG       idx=0  1028 pares -> N escritos, N esperas, N saltados
   (una linea por tabla)
RTL8852BT: FASE 3b lista: tablas de banda base y radio aplicadas.
```

### 5.3 Lo que falta de la fase 3

1. Calibraciones de `rtw8852bt_rfk.c`: DACK, ADDCK, IQK, DPK, TSSI. Son lo que permite
   transmitir con la potencia correcta.
2. Ajuste de canal y ancho de banda: `rtw8852bt_set_channel`.
3. Tablas de potencia y limites regulatorios, que tambien estan ya en el fichero.

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
[HECHO]     CI en GitHub Actions: compila el kext sin necesidad de Mac
[AHORA]     ---- PROBAR el kext en el portatil con el USB de OpenCore ----
[HECHO]     Fase 2c  efuse: MAC address                 (efuse.c)
[HECHO]     Fase 2b  anillos DMA + interrupciones      (pci.c)
[HECHO]     Fase 2d-1 modo descarga de firmware         (mac.c)
[HECHO]     Fase 2d-2 enviar el firmware por H2C        (fw.c)   <- hito clave
[HECHO]     Fase 3a  encender la radio                 (rtw8852b_common.c)
[HECHO]     Fase 3b  tablas de BB/RF (venian en el firmware)
[AHORA]     ---- PROBAR en el portatil ----
[SIGUIENTE] Fase 3c  calibraciones + canal              (rtw8852bt_rfk.c)
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
| `RTL8852BT.cpp` fase 1 | **Compila y enlaza** en CI. Nunca ejecutado |
| `RTL8852BT_power.cpp` fase 2a | **Compila y enlaza** en CI. Nunca ejecutado en el chip |
| CI en GitHub Actions | **Funciona**: macOS 26, Xcode 26.6, Mach-O x86_64 |
| Validacion en macOS Intel | **Funciona**: kmutil resuelve los simbolos; el binario lleva codigo |
| Carga real del kext en el chip | **Sin probar**. Necesita el portatil |
| Fases 2b a 5 | **No escritas** |

Nada de lo que hay en `kext/` ha tocado hardware real todavia. El primer paso del guion
es cambiar eso.

---

## 11. Diario de pruebas en el hardware

### 18/09/2026 — primeras pruebas con OpenCore instalado en la particion EFI del disco

| Prueba | Cambio | Hasta donde llega |
|---|---|---|
| 1 | configuracion inicial | el cargador de macOS falla al reservar memoria (`EB.MM.AKM`, `EB|STOP 0x16`) |
| 2 | `DevirtualiseMmio` + OpenCore DEBUG + SSDT corregido | **entrega el control al kernel** (`EXITBS:START`) y ahi se queda |
| 3 | `SetupVirtualMap` desactivado | **el kernel arranca** (Darwin 25.6.0) y da panic a los 0,34 s |
| 4 | `MmioWhitelist 0x80000000` | vuelve el fallo de memoria: `No slide values are usable!` |
| 5 | `AllowRelocationBlock` | activo pero OpenCore no lo usa; mismo fallo de memoria |
| 6 | config de la prueba 3 + NVRAM emulada (`OpenVariableRuntimeDxe`) | **pasa el panic**; cargan VirtualSMC, RestrictEvents y 36 tablas ACPI (incluido nuestro SSDT); se para al arrancar IOPCIFamily |
| 7 | quitar `npci=0x3000` (error mio: el firmware ya usa Above 4G) | mismo sitio: `npci` no era la causa |
| 8 | solo diagnostico: `pci_log_mode=0x2 pci_log=0x202` (registro de PCI en pantalla) | ninguna linea `[PCIe:`: se cuelga antes de leer el puente raiz |
| 9 | `SSDT-CPUR-OMNI.aml`: 16 objetos `Processor` (el firmware solo tiene `Device ACPI0007`) + `SysReport` | **macOS reconoce los 16 hilos** (`AppleACPICPU ... Enabled`) y pasa el cuelgue; pantalla negra (normal durante la configuracion PCI) y reinicio brusco a los pocos segundos |
| 10 | `cpus=1` (diagnostico: un solo hilo) | **panic legible**: `VoodooI2CDeviceNub::getGPIOController`, kext del panel tactil, a los 0,0039 s |
| 11 | desactivar VoodooI2C, VoodooI2CServices, VoodooGPIO y VoodooI2CHID | sin panic; entra en `[ PCI configuration begin ]`, todos los rangos ACPI `added(ok)`; la pantalla se apaga (normal) |
| 12 | parche IOPCIFamily: `kPEDisableScreen` 7 -> `kPEEnableScreen` 6 en `IOPCIConfigurator::configure` (diagnostico) | pantalla visible; se para en `bridgeAllocateResources` del puente raiz, tras listar los hijos (antes de `applyConfiguration`) |
| 13 | `ResizeAppleGpuBars = 0` (la Radeon 860M tiene un BAR de 2 GB; macOS admite 1 GB como maximo) | **PCI completo. Nuestro driver se ejecuta: `RTL8852BT: init`**. NVMe y APFS cargan; se para en `ktriage_register_subsystem_strings` |
| 14 | kext nuevo que registra cada salida de `probe`/`start` | pendiente |

**Donde se cuelga (prueba 7, leido del codigo de IOPCIFamily-726.100.6):** la ultima linea,
`pci (build ...)`, la escribe `IOPCIConfigurator::createRoot()`. Lo siguiente es
`addHostBridge()`, y al acabar llama a `configure()`, que escribe `[ PCI configuration begin ]`
con `IOLog` (visible en pantalla). Como no sale, el cuelgue esta dentro de `addHostBridge()`:
al leer el espacio de configuracion del puente raiz (ECAM) o al recoger sus rangos de ACPI.
Es antes de configurar ningun dispositivo, asi que nuestro driver no interviene.

**Callejon de la prueba 4:** sin la region `0x80000000` el firmware no puede leer su NVRAM
(panic de la prueba 3); con ella, OpenCore solo libera 526 MB en vez de 2,6 GB y no queda
ningun slide valido para el kernel. `AllowRelocationBlock` esta pensado justo para cuando
"no existe un slide mejor". Plan B: quitar la lista blanca y usar NVRAM emulada.

**Panic de la prueba 3, leido del video:** page fault en codigo del firmware al leer
`0xFF0CC04C`, la ventana de la flash donde vive la NVRAM, que `DevirtualiseMmio` habia
quitado del mapa. **No es nuestro driver:** no esta en el backtrace, la instruccion que
falla esta fuera de la coleccion de kexts (68 MB; el fallo esta a 78,8 MB de su inicio) y
ocurre antes de que arranque ningun driver.

**Hito de la prueba 2:** `OC: Prelinked injection RTL8852BT.kext (com.poveda.driver.RTL8852BT) - Success`.
Primera confirmacion en hardware real de que macOS acepta el kext dentro de su coleccion
del kernel. Los 14 parches de AMD tambien se aplican con `Success`.

El driver aun no se ha ejecutado: se carga cuando el kernel ya esta en marcha, y el kernel
todavia no arranca en este equipo. El bloqueo actual es de arranque de macOS en un Zen 5
(Ryzen AI 7 350, familia 0x1A) con firmware Insyde de HP, no del driver.

Error mio encontrado en la prueba 1: el `SSDT-EC-USBX-LAPTOP.aml` descargado era una pagina
HTML de error de GitHub. Sustituido por la muestra oficial de OpenCore con `LPCB` cambiado a
`LPC0`, que es el nombre real del bus en este portatil (`\_SB.PCI0.LPC0.EC0`, sacado de
Windows), y la suma de comprobacion ACPI recalculada.

**Prueba 8 y el SSDT de procesadores.** Con el registro de PCI activo no sale ni `root id`,
lo primero que se escribe al leer el puente raiz. El cuelgue esta entre
`IOPCIHostBridge::probe` y el inicio de `addHostBridge()`, en el arranque de la plataforma
ACPI. Desde Windows se ve que el firmware declara los 16 hilos como `Device (C000..C00F)`
con `_HID "ACPI0007"` en `\_SB.PLTF` y ningun `Processor`, que es lo unico que macOS
reconoce. Arreglo, como `SSDT-CPUR` en AMD B550/A520: `opencore/ACPI/SSDT-CPUR-OMNI.dsl`,
16 `Processor` con ProcId y `_UID` 0..15, que coinciden con la MADT del firmware.

**Prueba 9.** El SSDT funciona: `AppleACPICPU: ProcessorId=0..15 LocalApicId=0..15 Enabled`.
La pantalla negra es normal: `IOPCIConfigurator::configure()` llama a
`setConsoleInfo(0, kPEDisableScreen)` durante la configuracion PCI. El error
`[_PRR] AE_ALREADY_EXISTS` es de la BIOS (SSDT-15 redeclara `_PRR` en
`\_SB.PCI0.GPPC.XHC0.RHUB.PRT5`) y es inofensivo. Lo nuevo es el reinicio: unos 6 segundos
entre `EXITBS:START` y el siguiente arranque de OpenCore, sin panic. Es lo esperable si
falla el arranque de los nucleos secundarios en un Zen 5 hibrido (4 Zen 5 + 4 Zen 5c),
que hasta ahora macOS no intentaba porque no veia ningun procesador.
La prueba 10 (`cpus=1`) separa las dos hipotesis: nucleos o configuracion PCI.

**Prueba 10: el reinicio era VoodooI2C.** Con un solo hilo macOS pinta el panic entero:
page fault en `VoodooI2CDeviceNub::getGPIOController` (`com.alexandred.VoodooI2C` 2.9.1,
dependencia `org.coolstar.VoodooGPIO`), llamado desde `VoodooI2CControllerDriver::start`.
VoodooI2C busca un controlador GPIO que solo existe en portatiles Intel. **No es nuestro
driver:** ocurre a los 0,0039 s de arrancar el emparejamiento de dispositivos y no aparece
`RTL8852BT` en el backtrace. Se desactivan los cuatro kexts de VoodooI2C (son del panel
tactil, no hacen falta para arrancar).

**Prueba 11.** Sin VoodooI2C no hay panic. El registro de PCI sale entero hasta
`[ PCI configuration begin ]` y `console 1920 x 1200 @ 0x900000000`: puente raiz
`1022:1122`, todos los rangos del `_CRS` aceptados, ventana de 64 bits
`0x8a0200000 len 0x7a9fe00000` que contiene la pantalla. Despues `configure()` apaga la
pantalla hasta terminar, y lo que pase ahi no se ve.

**Parche de diagnostico (prueba 12).** Del `BootKernelExtensions.kc` de macOS 26.6
(extraido del BaseSystem.dmg con 7-Zip) se desensamblo `IOPCIConfigurator::configure`:
`31 f6 ba 07 00 00 00 ff 91 f0 08 00 00` = `xor esi,esi; mov edx,7 (kPEDisableScreen);
call [rcx+0x8f0] (setConsoleInfo)`. Patron unico en todo el KC. Se cambia el 7 por un 6
(`kPEEnableScreen`) con un `Kernel > Patch` de OpenCore sobre `com.apple.iokit.IOPCIFamily`,
base `__ZN17IOPCIConfigurator9configureEj`. Se quita cuando macOS arranque.

**Prueba 12.** Con la pantalla encendida se ve toda la configuracion PCI: escaneo,
reparto de buses y `iterate allocate: start`. Se para en
`bridgeAllocateResources(bridge [i2]0:0:0)` tras listar los rangos de sus hijos
(`0:2:3` WiFi, `0:2:4` NVMe, `0:8:1` grafica, `0:8:2`, `0:8:3`). Lo siguiente en el codigo
es `applyConfiguration()` sobre cada hijo. El puente de la grafica (`0:8:1`, `1022:1110`)
pide `PFM` de 2 GB con alineacion de 2 GB y aparece sin asignar (`0x0:0x0`). En Windows
la Radeon 860M (`1002:1114`) tiene un BAR de 2 GB en `0x900000000` (la pantalla) y otro de
256 MB en `0x80000000`. Segun la documentacion de OpenCore, macOS admite como maximo 1 GB
por BAR de GPU; `ResizeAppleGpuBars = 0` es el unico valor soportado y solo afecta a macOS.

**Prueba 13: hito.** Con `ResizeAppleGpuBars = 0` OpenCore reduce los BAR de la Radeon
(`RBAR 1/2 from 11 to 8`, 2 GB -> 256 MB; `RBAR 2/2 from 8 to 1`) y macOS termina la
configuracion PCI. Se publica `WLAN (1:0:0)` y **nuestro kext se ejecuta en el hardware
real por primera vez**: `RTL8852BT: init (fase 1, sin WiFi funcional)`. No sale `probe`:
en el codigo, `probe` sale en silencio si `configRead16` no devuelve `10ec:b520`, asi que
probablemente leyo `0xffff`. El kext de la prueba 14 registra cada salida. El arranque
sigue hasta cargar APFS y se para en `ktriage_register_subsystem_strings`.
