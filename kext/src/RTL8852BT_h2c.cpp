/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_h2c.cpp - FASE 2d, segunda parte: enviar el firmware al chip
 *
 * Port de reference/rtw89-linux:
 *   core.c  rtw89_h2c_tx, rtw89_core_fill_txdesc  -> sendH2C()
 *   pci.c   rtw89_pci_fwcmd_submit                -> submitH2C()
 *   pci.c   __rtw89_pci_tx_kick_off               -> kickRing()
 *   fw.c    __rtw89_fw_download_hdr               -> sendFirmwareHeader()
 *   fw.c    __rtw89_fw_download_main              -> sendFirmwareSections()
 *
 * ESTE ES EL HITO DEL PROYECTO. Con el firmware corriendo dentro del chip, el
 * resto es configuracion. Sin el, no funciona nada.
 *
 * Como viaja un paquete H2C (host to chip) por el canal CH12:
 *
 *   buffer DMA:  [ descriptor TX 24 B ][ cabecera H2C 8 B ][ datos ]
 *                 ^                                               ^
 *                 esto es lo que apunta el descriptor del anillo, y su longitud
 *
 *   anillo CH12: una entrada de 8 bytes con la direccion fisica y el tamano.
 *                Se avanza el puntero de escritura y se escribe en su registro
 *                de indice: eso es lo que despierta al chip.
 *
 * La cabecera H2C de 8 bytes solo la lleva el paquete de cabecera del firmware.
 * Las secciones van en crudo, con el bit fw_dl puesto en el descriptor.
 */
#include "RTL8852BT.hpp"

/* core.h:5019 */
#define RTW89_DMA_H2C 12

/* Tamano del descriptor TX del 8852BT: sizeof(struct rtw89_txwd_body) */
#define H2C_DESC_SIZE 24
/* fw.h:4673 */
#define H2C_HEADER_LEN 8

/* fw.h:4673-4691 */
#define H2C_HDR_CAT              GENMASK(1, 0)
#define H2C_HDR_CLASS            GENMASK(7, 2)
#define H2C_HDR_FUNC             GENMASK(15, 8)
#define H2C_HDR_DEL_TYPE         GENMASK(19, 16)
#define H2C_HDR_H2C_SEQ          GENMASK(31, 24)
#define H2C_HDR_TOTAL_LEN        GENMASK(13, 0)
#define FWCMD_TYPE_H2C           0
#define H2C_CAT_MAC              0x1
#define H2C_CL_MAC_FWDL          0x3
#define H2C_FUNC_MAC_FWHDR_DL    0x0

/* txrx.h:69-97 - campos del descriptor TX */
#define RTW89_TXWD_BODY0_WP_OFFSET     GENMASK(31, 24)
#define RTW89_TXWD_BODY0_WD_INFO_EN    BIT(22)
#define RTW89_TXWD_BODY0_FW_DL         BIT(20)
#define RTW89_TXWD_BODY0_CHANNEL_DMA   GENMASK(19, 16)
#define RTW89_TXWD_BODY0_HDR_LLC_LEN   GENMASK(15, 11)
#define RTW89_TXWD_BODY0_WD_PAGE       BIT(7)
#define RTW89_TXWD_BODY2_MACID         GENMASK(30, 24)
#define RTW89_TXWD_BODY2_QSEL          GENMASK(22, 17)
#define RTW89_TXWD_BODY2_TID_INDICATE  BIT(23)
#define RTW89_TXWD_BODY2_TXPKT_SIZE    GENMASK(13, 0)
#define RTW89_TXWD_BODY3_SW_SEQ        GENMASK(11, 0)

/* El anillo del canal de firmware es el ultimo de la tabla (CH12) */
#define H2C_RING_INDEX (RTW8852BT_TXCH_COUNT - 1)

/* ------------------------------------------------------------------------ */
/* Buffers DMA para los paquetes H2C                                         */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::allocH2CBuffers()
{
	/* Cabe el descriptor, la cabecera y el trozo mas grande de firmware */
	const u32 size = H2C_DESC_SIZE + H2C_HEADER_LEN + FWDL_SECTION_PER_PKT_LEN + 64;

	for (u32 i = 0; i < H2C_BUF_COUNT; i++) {
		if (!allocRing(&fH2CBuf[i], 1, size, "buffer H2C"))
			return false;
	}
	RTLOG("h2c: %u buffers de %u bytes listos", (unsigned)H2C_BUF_COUNT, size);
	return true;
}

void RTL8852BT::freeH2CBuffers()
{
	for (u32 i = 0; i < H2C_BUF_COUNT; i++)
		freeRing(&fH2CBuf[i]);
}

/* ------------------------------------------------------------------------ */
/* Descriptor TX. core.c: rtw89_core_fill_txdesc + rtw89_build_txwd_body*    */
/* ------------------------------------------------------------------------ */

void RTL8852BT::fillTxDesc(u8 *desc, u32 payloadLen, bool fwDl)
{
	memset(desc, 0, H2C_DESC_SIZE);
	u32 *d = (u32 *)desc;

	/* dword0: canal DMA 12 (H2C) y, para las secciones, el bit de descarga */
	u32 dw0 = FIELD_PREP(RTW89_TXWD_BODY0_CHANNEL_DMA, RTW89_DMA_H2C);
	if (fwDl)
		dw0 |= RTW89_TXWD_BODY0_FW_DL;
	d[0] = cpu_to_le32(dw0);

	/* dword2: tamano del paquete, sin contar este descriptor */
	d[2] = cpu_to_le32(FIELD_PREP(RTW89_TXWD_BODY2_TXPKT_SIZE, payloadLen));

	/* dword3 queda a cero: sin secuencia ni agregacion para comandos */
}

/* fw.c: rtw89_h2c_pkt_set_hdr_fwdl */
void RTL8852BT::fillH2CHeader(u8 *hdr, u8 cat, u8 cls, u8 func, u32 len)
{
	u32 *h = (u32 *)hdr;
	h[0] = cpu_to_le32(FIELD_PREP(H2C_HDR_DEL_TYPE, FWCMD_TYPE_H2C) |
	                   FIELD_PREP(H2C_HDR_CAT, cat) |
	                   FIELD_PREP(H2C_HDR_CLASS, cls) |
	                   FIELD_PREP(H2C_HDR_FUNC, func) |
	                   FIELD_PREP(H2C_HDR_H2C_SEQ, fH2CSeq));
	h[1] = cpu_to_le32(FIELD_PREP(H2C_HDR_TOTAL_LEN, len + H2C_HEADER_LEN));
}

/* ------------------------------------------------------------------------ */
/* Meter una entrada en el anillo CH12 y despertar al chip                   */
/* pci.c: rtw89_pci_fwcmd_submit + __rtw89_pci_tx_kick_off                   */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::submitH2C(u32 physAddr, u32 totalLen)
{
	rtw89_ring *ring = &fTxRing[H2C_RING_INDEX];
	const rtw89_pci_ch_addr *addr = &RTW8852BT_TXCH[H2C_RING_INDEX].addr;

	/* Escribir el descriptor del anillo en la posicion del puntero de escritura.
	 * OPT_LS marca que este es el ultimo (y unico) trozo del paquete. */
	rtw89_pci_tx_bd_32 *bd = (rtw89_pci_tx_bd_32 *)(ring->virt) + ring->wp;
	bd->length = cpu_to_le16((u16)totalLen);
	bd->opt    = cpu_to_le16(RTW89_PCI_TXBD_OPT_LS);
	bd->dma    = cpu_to_le32(physAddr);

	/* Avanzar el puntero de escritura, con vuelta al principio */
	ring->wp = (ring->wp + 1) % ring->numDesc;

	/* Escribir el puntero en su registro: esto es lo que despierta al chip */
	write16(addr->idx, (u16)ring->wp);

	/* Esperar a que el hardware lo consuma. El registro de indice trae el
	 * puntero del hardware en los bits altos. Sin esta espera reutilizariamos
	 * el buffer antes de que el chip lo haya leido. */
	for (u32 i = 0; i < 100000; i++) {
		u32 idx = read32(addr->idx);
		u32 hwIdx = FIELD_GET(TXBD_HW_IDX_MASK, idx);
		if (hwIdx == ring->wp)
			return true;
		IODelay(10);
	}

	u32 idx = read32(addr->idx);
	RTLOG("h2c: el chip no consumio el paquete (host=%u hw=%u idx=0x%08x)",
	      ring->wp, (unsigned)FIELD_GET(TXBD_HW_IDX_MASK, idx), idx);
	return false;
}

/* core.c: rtw89_h2c_tx */
bool RTL8852BT::sendH2C(const u8 *payload, u32 payloadLen, bool withHeader,
                        u8 cat, u8 cls, u8 func, bool fwDl)
{
	if (!fPoweredOn) {
		RTLOG("h2c: el MAC no esta encendido");
		return false;
	}

	rtw89_ring *buf = &fH2CBuf[fH2CBufNext];
	fH2CBufNext = (fH2CBufNext + 1) % H2C_BUF_COUNT;

	u32 hdrLen = withHeader ? H2C_HEADER_LEN : 0;
	u32 bodyLen = hdrLen + payloadLen;
	if (H2C_DESC_SIZE + bodyLen > buf->bytes) {
		RTLOG("h2c: paquete de %u bytes, no cabe en el buffer de %u",
		      H2C_DESC_SIZE + bodyLen, buf->bytes);
		return false;
	}

	u8 *p = buf->virt;
	fillTxDesc(p, bodyLen, fwDl);
	if (withHeader)
		fillH2CHeader(p + H2C_DESC_SIZE, cat, cls, func, payloadLen);
	memcpy(p + H2C_DESC_SIZE + hdrLen, payload, payloadLen);

	bool ok = submitH2C(buf->phys, H2C_DESC_SIZE + bodyLen);
	if (ok && withHeader)
		fH2CSeq = (u8)((fH2CSeq + 1) & 0xff);
	return ok;
}

/* ------------------------------------------------------------------------ */
/* fw.c: __rtw89_fw_download_hdr                                             */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::sendFirmwareHeader()
{
	const rtw89_fw_bin_summary *fw = &fFwNormal;
	u32 len = fw->hdr_len - fw->dynamicHdrLen;

	RTLOG("fwdl: enviando la cabecera del firmware (%u bytes)", len);

	/* La cabecera va con cabecera H2C y SIN el bit de descarga */
	if (!sendH2C(fw->base, len, true,
	             H2C_CAT_MAC, H2C_CL_MAC_FWDL, H2C_FUNC_MAC_FWHDR_DL, false)) {
		RTLOG("fwdl: fallo al enviar la cabecera");
		return false;
	}

	/* El chip debe responder abriendo el camino de descarga */
	if (!waitPathReady(false)) {
		RTLOG("fwdl: la cabecera se envio pero el chip no abrio el camino");
		return false;
	}

	write32(R_AX_HALT_H2C_CTRL, 0);
	write32(R_AX_HALT_C2H_CTRL, 0);

	RTLOG("fwdl: cabecera aceptada, camino de descarga abierto");
	return true;
}

/* ------------------------------------------------------------------------ */
/* fw.c: __rtw89_fw_download_main                                            */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::sendFirmwareSections()
{
	const rtw89_fw_bin_summary *fw = &fFwNormal;
	u32 partSize = fw->partSize;

	for (u32 i = 0; i < fw->section_num; i++) {
		const rtw89_fw_section *sec = &fw->sections[i];
		const u8 *data = fw->base + sec->offset;
		u32 residue = sec->len;
		u32 packets = 0;

		RTLOG("fwdl: seccion %u -> 0x%08x, %u bytes en trozos de %u",
		      i, sec->dlAddr, sec->len, partSize);

		while (residue) {
			u32 chunk = residue < partSize ? residue : partSize;

			/* Las secciones van en crudo, sin cabecera H2C, y CON el
			 * bit de descarga puesto en el descriptor. */
			if (!sendH2C(data, chunk, false, 0, 0, 0, true)) {
				RTLOG("fwdl: fallo en la seccion %u, trozo %u (quedaban %u bytes)",
				      i, packets, residue);
				return false;
			}
			data    += chunk;
			residue -= chunk;
			packets++;
		}
		RTLOG("fwdl: seccion %u enviada en %u paquetes", i, packets);
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/* Orquestacion. fw.c: __rtw89_fw_download                                   */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::downloadFirmware()
{
	if (!fFwdlReady) {
		RTLOG("fwdl: el chip no esta en modo descarga; no se puede enviar");
		return false;
	}
	if (!fDmaReady) {
		RTLOG("fwdl: sin anillos DMA no hay por donde enviarlo");
		return false;
	}
	if (!allocH2CBuffers())
		return false;

	fH2CSeq = 0;
	bool ok = sendFirmwareHeader() && sendFirmwareSections();

	if (ok) {
		IOSleep(5);   /* mdelay(5) antes de comprobar */
		ok = waitFirmwareReady();
	}

	freeH2CBuffers();

	if (ok)
		RTLOG("FASE 2d COMPLETA: el firmware corre dentro del chip. "
		      "Este es el hito del proyecto.");
	else
		RTLOG("FASE 2d FALLO: el firmware no arranco.");
	return ok;
}
