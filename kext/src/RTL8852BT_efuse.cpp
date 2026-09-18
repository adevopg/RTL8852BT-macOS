/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_efuse.cpp - FASE 2c: leer la efuse y sacar la direccion MAC
 *
 * Port de reference/rtw89-linux/efuse.c:
 *   rtw89_enable_efuse_pwr_cut_ddv     -> enableEfusePwrCut()
 *   rtw89_disable_efuse_pwr_cut_ddv    -> disableEfusePwrCut()
 *   rtw89_dump_physical_efuse_map_ddv  -> dumpPhysicalEfuse()
 *   rtw89_dump_logical_efuse_map       -> dumpLogicalEfuse()
 *   rtw89_parse_efuse_map_ax           -> readEfuse()
 * y de rtw8852b_common.c:
 *   __rtw8852bx_read_efuse             -> parseEfuseMap()
 *
 * Por que esta fase va ANTES que los anillos DMA: la efuse se lee entera por
 * registros, con una espera activa por cada byte. No necesita DMA ni
 * interrupciones, asi que se puede hacer justo despues de encender el MAC.
 *
 * Y es la unica fase con un resultado CONTRASTABLE: la MAC que salga de aqui
 * tiene que ser la misma que Windows muestra con 'getmac /v'. Si coincide, la
 * ruta completa (mapeo de BAR, encendido, acceso a registros) esta bien.
 */
#include "RTL8852BT.hpp"

/* ------------------------------------------------------------------------ */
/* Ayudantes de 16 bits que faltaban                                         */
/* ------------------------------------------------------------------------ */

void RTL8852BT::write16Set(u32 addr, u16 bits)
{
	write16(addr, (u16)(read16(addr) | bits));
}

void RTL8852BT::write16Clr(u32 addr, u16 bits)
{
	write16(addr, (u16)(read16(addr) & ~bits));
}

/* ------------------------------------------------------------------------ */
/* Alimentacion del bloque de efuse                                          */
/* efuse.c: rtw89_enable/disable_efuse_pwr_cut_ddv                           */
/* ------------------------------------------------------------------------ */

void RTL8852BT::enableEfusePwrCut()
{
	write8Set(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	write16Set(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);

	IOSleep(1);   /* fsleep(1000) us */

	write16Set(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);
	write16Clr(R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	/* El modo rafaga solo aplica al 8852B cut A; el 8852BT no lo usa. */
}

void RTL8852BT::disableEfusePwrCut()
{
	write16Set(R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	write16Clr(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);

	IOSleep(1);

	write16Clr(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
	write8Clr(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
}

/* ------------------------------------------------------------------------ */
/* Volcado fisico: un byte por vuelta, esperando B_AX_EF_RDY                 */
/* efuse.c: rtw89_dump_physical_efuse_map_ddv                                */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::dumpPhysicalEfuse(u8 *map, u32 dumpAddr, u32 dumpSize)
{
	enableEfusePwrCut();

	for (u32 addr = dumpAddr; addr < dumpAddr + dumpSize; addr++) {
		u32 ctl = FIELD_PREP(B_AX_EF_ADDR_MASK, addr);
		write32(R_AX_EFUSE_CTRL, ctl & ~B_AX_EF_RDY);

		/* read_poll_timeout_atomic(..., 1 us, 1000000 us) */
		u32 val = 0;
		if (!pollReg32(R_AX_EFUSE_CTRL, B_AX_EF_RDY, true, 1, 1000000, &val)) {
			RTLOG("efuse: timeout leyendo el byte 0x%03x", addr);
			disableEfusePwrCut();
			return false;
		}
		*map++ = (u8)(val & 0xff);
	}

	disableEfusePwrCut();
	return true;
}

/* ------------------------------------------------------------------------ */
/* Conversion a mapa logico                                                  */
/* efuse.c: rtw89_dump_logical_efuse_map                                     */
/*                                                                           */
/* La efuse fisica esta comprimida: cada bloque lleva dos bytes de cabecera   */
/* que dicen a que indice logico va y cuales de sus 4 palabras estan escritas.*/
/* ------------------------------------------------------------------------ */

#define invalid_efuse_header(h1, h2)   ((h1) == 0xff || (h2) == 0xff)
#define invalid_efuse_content(we, i)   ((((we) & BIT(i)) != 0x0))
#define get_efuse_blk_idx(h1, h2)      ((((h2) & 0xf0) >> 4) | (((h1) & 0x0f) << 4))
#define block_idx_to_logical_idx(b, i) ((((u32)(b)) << 3) + (((u32)(i)) << 1))

bool RTL8852BT::dumpLogicalEfuse(const u8 *phyMap, u8 *logMap)
{
	const u32 physicalSize = RTW8852BT_PHYSICAL_EFUSE_SIZE;
	const u32 logicalSize  = RTW8852BT_LOGICAL_EFUSE_SIZE;
	const u8  secCtrlSize  = RTW8852BT_SEC_CTRL_EFUSE_SIZE;
	u32 phyIdx = secCtrlSize;

	while (phyIdx < physicalSize - secCtrlSize) {
		u8 hdr1 = phyMap[phyIdx];
		u8 hdr2 = phyMap[phyIdx + 1];
		if (invalid_efuse_header(hdr1, hdr2))
			break;   /* 0xff = fin de los datos grabados */

		u8 blkIdx = (u8)get_efuse_blk_idx(hdr1, hdr2);
		u8 wordEn = (u8)(hdr2 & 0xf);
		phyIdx += 2;

		for (int i = 0; i < 4; i++) {
			if (invalid_efuse_content(wordEn, i))
				continue;   /* ese bit a 1 significa palabra NO escrita */

			u32 logIdx = block_idx_to_logical_idx(blkIdx, i);
			if (phyIdx + 1 > physicalSize - secCtrlSize - 1 ||
			    logIdx + 1 > logicalSize) {
				RTLOG("efuse: mapa incoherente (phy=%u log=%u)", phyIdx, logIdx);
				return false;
			}
			logMap[logIdx]     = phyMap[phyIdx];
			logMap[logIdx + 1] = phyMap[phyIdx + 1];
			phyIdx += 2;
		}
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/* Extraer los campos que nos interesan                                      */
/* rtw8852b_common.c: __rtw8852bx_read_efuse (rama RTW89_HCI_TYPE_PCIE)      */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::parseEfuseMap(const u8 *logMap)
{
	const rtw8852bx_efuse *map = (const rtw8852bx_efuse *)logMap;

	memcpy(fMacAddr, map->e.mac_addr, ETH_ALEN);
	fRfeType      = map->rfe_type;
	fXtalCap      = map->xtal_k;
	fCountry[0]   = (char)map->country_code[0];
	fCountry[1]   = (char)map->country_code[1];

	/* Una efuse en blanco da todo 0xFF; sin grabar, todo 0x00. Ninguna de las
	 * dos es una MAC valida, y ademas el bit multicast (el 0 del primer byte)
	 * nunca esta puesto en una direccion de tarjeta. */
	bool allFF = true, allZero = true;
	for (int i = 0; i < ETH_ALEN; i++) {
		if (fMacAddr[i] != 0xFF) allFF = false;
		if (fMacAddr[i] != 0x00) allZero = false;
	}
	bool multicast = (fMacAddr[0] & 0x01) != 0;

	RTLOG("efuse: MAC %02x:%02x:%02x:%02x:%02x:%02x  rfe_type=%u xtal_k=0x%02x pais=%c%c",
	      fMacAddr[0], fMacAddr[1], fMacAddr[2], fMacAddr[3], fMacAddr[4], fMacAddr[5],
	      fRfeType, fXtalCap,
	      fCountry[0] >= 32 && fCountry[0] < 127 ? fCountry[0] : '?',
	      fCountry[1] >= 32 && fCountry[1] < 127 ? fCountry[1] : '?');

	if (allFF || allZero || multicast) {
		RTLOG("efuse: esa MAC NO es valida (%s). La lectura ha fallado.",
		      allFF ? "todo FF, efuse en blanco o no alimentada"
		            : allZero ? "todo ceros, no se leyo nada"
		                      : "bit multicast puesto");
		return false;
	}

	RTLOG("efuse: MAC valida. Compruebala en Windows con 'getmac /v'; deben coincidir.");
	setProperty("IOMACAddress", fMacAddr, ETH_ALEN);
	setProperty("RfeType", fRfeType, 8);
	return true;
}

/* ------------------------------------------------------------------------ */
/* Punto de entrada de la fase                                               */
/* efuse.c: rtw89_parse_efuse_map_ax                                         */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::readEfuse()
{
	/* Autoload: el chip copia la efuse a sus registros al arrancar. Si este
	 * bit no esta, los datos pueden no ser fiables. */
	u16 wlCtrl = read16(R_AX_SYS_WL_EFUSE_CTRL);
	fEfuseValid = (wlCtrl & B_AX_AUTOLOAD_SUS) != 0;
	if (!fEfuseValid)
		RTLOG("efuse: aviso, autoload no confirmado (WL_EFUSE_CTRL=0x%04x)", wlCtrl);

	u8 *phyMap = (u8 *)IOMalloc(RTW8852BT_PHYSICAL_EFUSE_SIZE);
	u8 *logMap = (u8 *)IOMalloc(RTW8852BT_LOGICAL_EFUSE_SIZE);
	if (!phyMap || !logMap) {
		if (phyMap) IOFree(phyMap, RTW8852BT_PHYSICAL_EFUSE_SIZE);
		if (logMap) IOFree(logMap, RTW8852BT_LOGICAL_EFUSE_SIZE);
		RTLOG("efuse: sin memoria");
		return false;
	}

	bool ok = false;
	if (!dumpPhysicalEfuse(phyMap, 0, RTW8852BT_PHYSICAL_EFUSE_SIZE)) {
		RTLOG("efuse: fallo el volcado fisico");
		goto out;
	}
	RTLOG("efuse: volcado fisico OK (%u bytes). Primeros 16: "
	      "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
	      RTW8852BT_PHYSICAL_EFUSE_SIZE,
	      phyMap[0], phyMap[1], phyMap[2], phyMap[3], phyMap[4], phyMap[5], phyMap[6], phyMap[7],
	      phyMap[8], phyMap[9], phyMap[10], phyMap[11], phyMap[12], phyMap[13], phyMap[14], phyMap[15]);

	/* El mapa logico empieza a 0xFF: lo que no este grabado se queda asi. */
	memset(logMap, 0xff, RTW8852BT_LOGICAL_EFUSE_SIZE);
	if (!dumpLogicalEfuse(phyMap, logMap)) {
		RTLOG("efuse: fallo la conversion a mapa logico");
		goto out;
	}

	ok = parseEfuseMap(logMap);

out:
	IOFree(phyMap, RTW8852BT_PHYSICAL_EFUSE_SIZE);
	IOFree(logMap, RTW8852BT_LOGICAL_EFUSE_SIZE);
	return ok;
}
