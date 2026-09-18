/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_rf.cpp - FASE 3, primera parte: encender la radio
 *
 * Port de reference/rtw89-linux:
 *   rtw8852b_common.c  __rtw8852bx_mac_enable_bb_rf  -> enableBbRf()
 *   rtw8852b_common.c  __rtw8852bx_mac_disable_bb_rf -> disableBbRf()
 *   rtw8852b_common.c  __rtw8852bx_get_thermal       -> readThermal()
 *   phy.c              rtw89_phy_read/write_rf       -> rfRead()/rfWrite()
 *   phy.h              rtw89_phy_read/write32        -> phyRead32()/phyWrite32()
 *
 * ALCANCE: encender los bloques de banda base y radiofrecuencia, y dejar
 * montado el acceso a sus registros. NO configura canal ni calibra la radio;
 * eso son las tablas y las calibraciones, que van despues.
 *
 * TRES ESPACIOS DE REGISTROS DISTINTOS, y confundirlos es el error clasico:
 *
 *   MAC:  se escriben tal cual en el BAR.                  write32(0x0004, ...)
 *   PHY:  van desplazados 0x10000.                         phyWrite32(0x2344, ...)
 *   RF:   direccionamiento directo, una base por camino,
 *         y la direccion se multiplica por 4.              rfWrite(0, 0x42, ...)
 *
 * El criterio de aceptacion de esta fase es el termometro interno. Si despues
 * de encender la radio devuelve un valor plausible, el bloque esta alimentado
 * y responde. Si devuelve 0 o 0x3F esta apagado o no contesta.
 */
#include "RTL8852BT.hpp"

/* ------------------------------------------------------------------------ */
/* Espacio de registros de la PHY: desplazado 0x10000 respecto al MAC        */
/* ------------------------------------------------------------------------ */

u32 RTL8852BT::phyRead32(u32 addr)
{
	return read32(addr + RTW89_PHY_CR_BASE);
}

void RTL8852BT::phyWrite32(u32 addr, u32 v)
{
	write32(addr + RTW89_PHY_CR_BASE, v);
}

u32 RTL8852BT::phyRead32Mask(u32 addr, u32 mask)
{
	return FIELD_GET(mask, phyRead32(addr));
}

void RTL8852BT::phyWrite32Mask(u32 addr, u32 mask, u32 v)
{
	u32 cur = phyRead32(addr);
	phyWrite32(addr, (cur & ~mask) | FIELD_PREP(mask, v));
}

/* ------------------------------------------------------------------------ */
/* Registros de radio. phy.c: rtw89_phy_read_rf / rtw89_phy_write_rf         */
/*                                                                           */
/* El 8852BT usa direccionamiento directo: cada camino de radio tiene su base */
/* dentro del espacio de la PHY, y la direccion del registro se multiplica    */
/* por cuatro porque son palabras de 32 bits.                                 */
/* ------------------------------------------------------------------------ */

static u32 rfBaseFor(u8 path)
{
	return path == 0 ? RTW89_RF_BASE_PATH_A : RTW89_RF_BASE_PATH_B;
}

u32 RTL8852BT::rfRead(u8 path, u32 addr, u32 mask)
{
	if (path >= RTW89_RF_PATH_NUM) {
		RTLOG("rf: camino %u no existe (hay %u)", path, RTW89_RF_PATH_NUM);
		return INV_RF_DATA;
	}
	u32 direct = rfBaseFor(path) + ((addr & 0xff) << 2);
	return phyRead32Mask(direct, mask & RFREG_MASK);
}

bool RTL8852BT::rfWrite(u8 path, u32 addr, u32 mask, u32 data)
{
	if (path >= RTW89_RF_PATH_NUM) {
		RTLOG("rf: camino %u no existe (hay %u)", path, RTW89_RF_PATH_NUM);
		return false;
	}
	u32 direct = rfBaseFor(path) + ((addr & 0xff) << 2);
	phyWrite32Mask(direct, mask & RFREG_MASK, data);
	IODelay(1);   /* el driver Linux espera 1 us para que la escritura cuaje */
	return true;
}

/* ------------------------------------------------------------------------ */
/* rtw8852b_common.c: __rtw8852bx_mac_enable_bb_rf                           */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::enableBbRf()
{
	if (!fPoweredOn) {
		RTLOG("rf: el MAC no esta encendido");
		return false;
	}

	write8Set(R_AX_SYS_FUNC_EN, B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	write32_mask(R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x1);

	/* Pulso en el reloj analogico: encender, apagar, encender */
	write32Set(R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	write32Clr(R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	write32Set(R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

	/* Esta parte es EXCLUSIVA del 8852BT: ajusta el regulador de los dos
	 * caminos de radio. Los demas chips de la familia se la saltan. */
	u32 val32 = read32(R_AX_AFE_OFF_CTRL1);
	val32 = (val32 & ~B_AX_S0_LDO_VSEL_F_MASK) | FIELD_PREP(B_AX_S0_LDO_VSEL_F_MASK, 0x1);
	val32 = (val32 & ~B_AX_S1_LDO_VSEL_F_MASK) | FIELD_PREP(B_AX_S1_LDO_VSEL_F_MASK, 0x1);
	write32(R_AX_AFE_OFF_CTRL1, val32);

	/* Encender el controlador de cada camino de radio por el bus del cristal */
	if (!writeXtalSi(XTAL_SI_WL_RFC_S0, 0xC7, FULL_BIT_MASK)) {
		RTLOG("rf: no se pudo encender el camino A");
		return false;
	}
	if (!writeXtalSi(XTAL_SI_WL_RFC_S1, 0xC7, FULL_BIT_MASK)) {
		RTLOG("rf: no se pudo encender el camino B");
		return false;
	}

	write8(R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);

	fBbRfOn = true;
	RTLOG("rf: banda base y radio encendidas. SYS_FUNC_EN=0x%02x WLRF_CTRL=0x%08x "
	      "AFE_OFF_CTRL1=0x%08x", read8(R_AX_SYS_FUNC_EN),
	      read32(R_AX_WLRF_CTRL), read32(R_AX_AFE_OFF_CTRL1));
	return true;
}

/* rtw8852b_common.c: __rtw8852bx_mac_disable_bb_rf */
bool RTL8852BT::disableBbRf()
{
	if (!fMmio || !fBbRfOn)
		return true;

	write32Clr(R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	write8Clr(R_AX_SYS_FUNC_EN, B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	u8 s0 = 0, s1 = 0;
	if (readXtalSi(XTAL_SI_WL_RFC_S0, &s0)) {
		s0 &= (u8)~XTAL_SI_RF00S_EN;
		writeXtalSi(XTAL_SI_WL_RFC_S0, s0, FULL_BIT_MASK);
	}
	if (readXtalSi(XTAL_SI_WL_RFC_S1, &s1)) {
		s1 &= (u8)~XTAL_SI_RF10S_EN;
		writeXtalSi(XTAL_SI_WL_RFC_S1, s1, FULL_BIT_MASK);
	}

	fBbRfOn = false;
	RTLOG("rf: banda base y radio apagadas");
	return true;
}

/* ------------------------------------------------------------------------ */
/* Termometro interno. rtw8852b_common.c: __rtw8852bx_get_thermal            */
/*                                                                           */
/* Es la prueba de vida de la radio: se dispara una medida con un pulso en el */
/* bit de trigger y se lee el resultado. Solo la version sin modo TSSI, que   */
/* es la que aplica antes de calibrar.                                        */
/* ------------------------------------------------------------------------ */

u8 RTL8852BT::readThermal(u8 path)
{
	rfWrite(path, RR_TM, RR_TM_TRI, 0x1);
	rfWrite(path, RR_TM, RR_TM_TRI, 0x0);
	rfWrite(path, RR_TM, RR_TM_TRI, 0x1);

	IODelay(200);   /* fsleep(200) us */

	return (u8)rfRead(path, RR_TM, RR_TM_VAL);
}

/* ------------------------------------------------------------------------ */
/* Punto de entrada de la fase                                               */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::setupRadio()
{
	if (!enableBbRf())
		return false;

	/* Prueba de vida: leer el termometro de los dos caminos.
	 * RR_TM_VAL son 6 bits, asi que el rango es 0..63. Un chip encendido a
	 * temperatura ambiente da un valor intermedio; 0 o 63 clavados significan
	 * que el bloque no esta alimentado o no contesta. */
	u8 t0 = readThermal(0);
	u8 t1 = readThermal(1);
	RTLOG("rf: termometro camino A=%u camino B=%u (rango 0..63)", t0, t1);

	bool plausible = (t0 > 0 && t0 < 63) || (t1 > 0 && t1 < 63);
	if (!plausible) {
		RTLOG("rf: los dos termometros dan un valor extremo. La radio no responde.");
		return false;
	}

	/* Comprobar que el espacio de registros de radio contesta algo coherente:
	 * si todas las lecturas devuelven lo mismo, no hay bus de verdad detras. */
	u32 a0 = rfRead(0, 0x00, RFREG_MASK);
	u32 a1 = rfRead(0, 0x01, RFREG_MASK);
	RTLOG("rf: registros de radio A[0x00]=0x%05x A[0x01]=0x%05x", a0, a1);
	if (a0 == INV_RF_DATA || (a0 == a1 && a0 == 0xfffff))
		RTLOG("rf: aviso, el bus de radio devuelve solo unos; sospechoso");

	fRadioReady = true;
	RTLOG("FASE 3a lista: la radio esta encendida y responde. "
	      "Faltan las tablas de configuracion y las calibraciones.");
	return true;
}
