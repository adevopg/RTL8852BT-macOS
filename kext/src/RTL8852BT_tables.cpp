/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_tables.cpp - FASE 3b: aplicar las tablas de banda base y radio
 *
 * Port de reference/rtw89-linux:
 *   fw.c   rtw89_fw_recognize_elements   -> findElements()
 *   phy.c  rtw89_phy_config_bb_reg       -> applyBbPair()
 *   phy.c  rtw89_phy_config_rf_reg_v1    -> applyRfPair()
 *   phy.c  rtw89_phy_init_reg            -> applyRegTable()
 *
 * EL HALLAZGO QUE AHORRA MESES. Casi todos los chips de esta familia llevan sus
 * tablas de configuracion compiladas dentro del driver: rtw8852b_table.c ocupa
 * 750 KB. Portar eso a mano seria absurdo.
 *
 * El 8852BT NO las lleva. En rtw8852bt.c sus punteros de tabla son todos NULL,
 * porque las tablas vienen como "elementos" pegados al final del propio fichero
 * de firmware, detras del contenedor multi-firmware. Es decir: ya las tenemos,
 * estaban en los 143.130 bytes finales de rtw8852bt_fw.bin desde el principio.
 *
 * Confirmado con tools/parse_fw.py sobre el fichero real:
 *
 *   BB_REG     1.028 pares direccion/dato
 *   RADIO_A    3.647 pares
 *   RADIO_B    3.630 pares
 *   RF_NCTL    1.849 pares
 *   (mas las tablas de potencia, que son para mas adelante)
 *
 * Asi que esta fase es leer el fichero y escribir 10.154 pares de registros,
 * no transcribir 750 KB.
 */
#include "RTL8852BT.hpp"

/* fw.h:4380 */
#define RTW89_FW_ELEMENT_ALIGN 16
/* core.h:50 - un dato con este valor significa "saltar este registro" */
#define BYPASS_CR_DATA 0xbabecafe

/* fw.h:4382 */
enum rtw89_fw_element_id {
	RTW89_FW_ELEMENT_ID_BBMCU0 = 0,
	RTW89_FW_ELEMENT_ID_BB_REG = 2,
	RTW89_FW_ELEMENT_ID_BB_GAIN = 3,
	RTW89_FW_ELEMENT_ID_RADIO_A = 4,
	RTW89_FW_ELEMENT_ID_RADIO_B = 5,
	RTW89_FW_ELEMENT_ID_RF_NCTL = 8,
	RTW89_FW_ELEMENT_ID_TXPWR_BYRATE = 9,
	RTW89_FW_ELEMENT_ID_REGD = 20,
};

/* fw.h:4570. La cabecera mide 32 bytes y luego vienen los datos. */
struct rtw89_fw_element_hdr {
	__le32 id;
	__le32 size;    /* sin contar esta cabecera */
	u8     ver[4];
	__le16 aid;
	__le16 rsvd0;
	__le32 rsvd1;
	__le32 rsvd2;
	/* union: para los elementos de pares, el primer byte es idx y siguen
	 * 7 reservados, y despues el array de {addr, data}. */
	u8     idx;
	u8     rsvd3[7];
	/* aqui empiezan los datos */
} __packed;

static_assert(sizeof(struct rtw89_fw_element_hdr) == 32,
              "la cabecera de elemento debe medir 32 bytes");

struct rtw89_reg2_def {
	__le32 addr;
	__le32 data;
} __packed;

static const char *elementName(u32 id)
{
	switch (id) {
	case RTW89_FW_ELEMENT_ID_BB_REG:       return "BB_REG";
	case RTW89_FW_ELEMENT_ID_BB_GAIN:      return "BB_GAIN";
	case RTW89_FW_ELEMENT_ID_RADIO_A:      return "RADIO_A";
	case RTW89_FW_ELEMENT_ID_RADIO_B:      return "RADIO_B";
	case RTW89_FW_ELEMENT_ID_RF_NCTL:      return "RF_NCTL";
	case RTW89_FW_ELEMENT_ID_TXPWR_BYRATE: return "TXPWR_BYRATE";
	case RTW89_FW_ELEMENT_ID_REGD:         return "REGD";
	default:                               return "otro";
	}
}

/* ------------------------------------------------------------------------ */
/* Localizar un elemento dentro del fichero de firmware                      */
/* fw.c: rtw89_fw_recognize_elements                                         */
/* ------------------------------------------------------------------------ */

const u8 *RTL8852BT::findElement(u32 wantedId, u32 *outCount, u8 *outIdx)
{
	if (!fFwData || !fFwLen)
		return nullptr;

	/* Los elementos empiezan justo detras del contenedor multi-firmware,
	 * alineados a 16 bytes. El final del contenedor es el shift + size de
	 * su ultima entrada. */
	const rtw89_mfw_hdr *mfw = (const rtw89_mfw_hdr *)fFwData;
	if (mfw->sig != RTW89_MFW_SIG || mfw->fw_nr == 0)
		return nullptr;
	const rtw89_mfw_info *last = &mfw->info[mfw->fw_nr - 1];
	u32 off = le32_to_cpu(last->shift) + le32_to_cpu(last->size);
	off = (off + RTW89_FW_ELEMENT_ALIGN - 1) & ~(u32)(RTW89_FW_ELEMENT_ALIGN - 1);

	while (off + sizeof(rtw89_fw_element_hdr) <= fFwLen) {
		const rtw89_fw_element_hdr *h =
		        (const rtw89_fw_element_hdr *)(fFwData + off);
		u32 id   = le32_to_cpu(h->id);
		u32 size = le32_to_cpu(h->size);

		if (size == 0 || off + sizeof(rtw89_fw_element_hdr) + size > fFwLen)
			break;

		if (id == wantedId) {
			if (outCount) {
				/* El array de pares empieza tras los 8 bytes de idx+rsvd
				 * que ya van dentro de la cabecera de 32. */
				*outCount = (size >= 8) ? (size - 8) / sizeof(rtw89_reg2_def) : 0;
			}
			if (outIdx)
				*outIdx = h->idx;
			return (const u8 *)h + sizeof(rtw89_fw_element_hdr);
		}

		off += sizeof(rtw89_fw_element_hdr) + size;
		off = (off + RTW89_FW_ELEMENT_ALIGN - 1) & ~(u32)(RTW89_FW_ELEMENT_ALIGN - 1);
	}
	return nullptr;
}

/* ------------------------------------------------------------------------ */
/* Aplicar un par direccion/dato                                             */
/* ------------------------------------------------------------------------ */

/* phy.c: rtw89_phy_config_bb_reg.
 * Las direcciones 0xf9 a 0xfe no son registros: son esperas codificadas
 * dentro de la propia tabla. Tratarlas como registros escribiria basura. */
bool RTL8852BT::applyBbPair(u32 addr, u32 data)
{
	switch (addr) {
	case 0xfe: IOSleep(50); return true;
	case 0xfd: IOSleep(5);  return true;
	case 0xfc: IOSleep(1);  return true;
	case 0xfb: IODelay(50); return true;
	case 0xfa: IODelay(5);  return true;
	case 0xf9: IODelay(1);  return true;
	default: break;
	}
	if (data == BYPASS_CR_DATA)
		return true;   /* la tabla pide saltarse este registro */

	phyWrite32(addr, data);
	return true;
}

/* phy.c: rtw89_phy_config_rf_reg_v1 */
bool RTL8852BT::applyRfPair(u8 path, u32 addr, u32 data)
{
	if (addr == 0xfe) {
		IOSleep(50);
		return true;
	}
	return rfWrite(path, addr, RFREG_MASK, data);
}

/* ------------------------------------------------------------------------ */
/* Recorrer una tabla entera                                                 */
/* phy.c: rtw89_phy_init_reg                                                 */
/* ------------------------------------------------------------------------ */

u32 RTL8852BT::applyRegTable(u32 elementId, bool isRf, u8 path)
{
	u32 count = 0;
	u8 idx = 0;
	const u8 *data = findElement(elementId, &count, &idx);
	if (!data) {
		RTLOG("tablas: no encuentro el elemento %s (%u)", elementName(elementId), elementId);
		return 0;
	}
	if (count == 0) {
		RTLOG("tablas: el elemento %s esta vacio", elementName(elementId));
		return 0;
	}

	const rtw89_reg2_def *regs = (const rtw89_reg2_def *)data;
	u32 applied = 0, delays = 0, skipped = 0;

	for (u32 i = 0; i < count; i++) {
		u32 addr = le32_to_cpu(regs[i].addr);
		u32 dat  = le32_to_cpu(regs[i].data);

		if (addr >= 0xf9 && addr <= 0xfe) {
			delays++;
		} else if (!isRf && dat == BYPASS_CR_DATA) {
			skipped++;
			continue;
		} else {
			applied++;
		}

		if (isRf)
			applyRfPair(path, addr, dat);
		else
			applyBbPair(addr, dat);
	}

	RTLOG("tablas: %-12s idx=%u  %u pares -> %u escritos, %u esperas, %u saltados",
	      elementName(elementId), idx, count, applied, delays, skipped);
	return applied;
}

/* ------------------------------------------------------------------------ */
/* Punto de entrada de la fase                                               */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::applyPhyTables()
{
	if (!fRadioReady) {
		RTLOG("tablas: la radio no esta encendida; no tiene sentido configurarla");
		return false;
	}

	/* Inventario de lo que trae este firmware, para dejarlo en el log */
	RTLOG("tablas: buscando elementos dentro de rtw8852bt_fw.bin (%u bytes)", fFwLen);
	u32 nBb = 0, nRa = 0, nRb = 0, nNctl = 0;
	findElement(RTW89_FW_ELEMENT_ID_BB_REG,  &nBb,   nullptr);
	findElement(RTW89_FW_ELEMENT_ID_RADIO_A, &nRa,   nullptr);
	findElement(RTW89_FW_ELEMENT_ID_RADIO_B, &nRb,   nullptr);
	findElement(RTW89_FW_ELEMENT_ID_RF_NCTL, &nNctl, nullptr);
	RTLOG("tablas: BB_REG=%u RADIO_A=%u RADIO_B=%u RF_NCTL=%u pares",
	      nBb, nRa, nRb, nNctl);

	if (nBb == 0 || nRa == 0) {
		RTLOG("tablas: faltan las tablas basicas; el firmware no es el esperado");
		return false;
	}

	/* Orden: primero la banda base, luego cada camino de radio, y al final
	 * el control numerico. Invertirlo deja la radio mal configurada. */
	u32 total = 0;
	total += applyRegTable(RTW89_FW_ELEMENT_ID_BB_REG,  false, 0);
	total += applyRegTable(RTW89_FW_ELEMENT_ID_RADIO_A, true,  0);
	total += applyRegTable(RTW89_FW_ELEMENT_ID_RADIO_B, true,  1);
	total += applyRegTable(RTW89_FW_ELEMENT_ID_RF_NCTL, true,  0);

	/* Prueba de vida: si la configuracion ha entrado, el termometro deberia
	 * seguir contestando, y ahora con la radio ya configurada. */
	u8 t0 = readThermal(0);
	u8 t1 = readThermal(1);
	RTLOG("tablas: %u registros escritos. Termometro tras configurar: A=%u B=%u",
	      total, t0, t1);

	if ((t0 == 0 || t0 == 63) && (t1 == 0 || t1 == 63)) {
		RTLOG("tablas: la radio dejo de contestar tras aplicar las tablas.");
		return false;
	}

	fTablesApplied = true;
	RTLOG("FASE 3b lista: tablas de banda base y radio aplicadas. "
	      "Faltan las calibraciones (DACK, IQK, DPK, TSSI) y fijar el canal.");
	return true;
}
