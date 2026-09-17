/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_power.cpp - FASE 2a: encendido y apagado del MAC
 *
 * Port directo de reference/rtw89-linux/rtw8852bt.c:
 *   rtw8852bt_pwr_on_func()  -> RTL8852BT::powerOn()
 *   rtw8852bt_pwr_off_func() -> RTL8852BT::powerOff()
 * y de reference/rtw89-linux/mac.c:
 *   rtw89_mac_write_xtal_si_ax() -> RTL8852BT::writeXtalSi()
 *   rtw89_mac_read_xtal_si_ax()  -> RTL8852BT::readXtalSi()
 *   rtw89_mac_reset_pwr_state_ax() -> RTL8852BT::resetPowerState()
 *
 * El 8852BT no usa tablas pwr_on_seq (son NULL en rtw8852bt.c:832); usa esta
 * funcion imperativa. Por eso se porta tal cual, sin motor de secuencias.
 *
 * ORDEN DE LAS ESCRITURAS: es significativo. No reordenar ni agrupar.
 */
#include "RTL8852BT.hpp"

/* ------------------------------------------------------------------------ */
/* Utilidades: espera activa con timeout (equivale a read_poll_timeout)      */
/* ------------------------------------------------------------------------ */

/* read_poll_timeout(rtw89_read32, val, cond, sleep_us, timeout_us, ...) */
bool RTL8852BT::pollReg32(u32 addr, u32 mask, bool waitSet,
                          u32 sleepUs, u32 timeoutUs, u32 *lastVal)
{
	u32 elapsed = 0;
	for (;;) {
		u32 v = read32(addr);
		if (lastVal)
			*lastVal = v;
		bool isSet = (v & mask) != 0;
		if (isSet == waitSet)
			return true;
		if (elapsed >= timeoutUs)
			return false;
		IODelay(sleepUs);          /* espera activa, valido en contexto de arranque */
		elapsed += sleepUs;
	}
}

void RTL8852BT::write32Set(u32 addr, u32 bits)
{
	write32(addr, read32(addr) | bits);
}

void RTL8852BT::write32Clr(u32 addr, u32 bits)
{
	write32(addr, read32(addr) & ~bits);
}

void RTL8852BT::write8Set(u32 addr, u8 bits)
{
	write8(addr, (u8)(read8(addr) | bits));
}

void RTL8852BT::write8Clr(u32 addr, u8 bits)
{
	write8(addr, (u8)(read8(addr) & ~bits));
}

/* ------------------------------------------------------------------------ */
/* Bus serie del cristal (xtal_si): escritura/lectura indirecta              */
/* mac.c: rtw89_mac_write_xtal_si_ax / rtw89_mac_read_xtal_si_ax            */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::writeXtalSi(u8 offset, u8 val, u8 mask)
{
	u32 cmd = FIELD_PREP(B_AX_WL_XTAL_SI_ADDR_MASK, offset) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_DATA_MASK, val) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_BITMASK_MASK, mask) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_MODE_MASK, XTAL_SI_NORMAL_WRITE) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_CMD_POLL, 1);
	write32(R_AX_WLAN_XTAL_SI_CTRL, cmd);

	u32 done = 0;
	if (!pollReg32(R_AX_WLAN_XTAL_SI_CTRL, B_AX_WL_XTAL_SI_CMD_POLL,
	               false, 50, 50000, &done)) {
		RTLOG("xtal si not ready (W): offset=0x%02x val=0x%02x mask=0x%02x",
		      offset, val, mask);
		return false;
	}
	/* El driver Linux solo avisa, no falla, si el eco no coincide. */
	if (FIELD_GET(B_AX_WL_XTAL_SI_ADDR_MASK, done) != offset ||
	    FIELD_GET(B_AX_WL_XTAL_SI_DATA_MASK, done) != val)
		RTLOG("aviso: xtal si write eco distinto: offset=0x%02x val=0x%02x poll=0x%08x",
		      offset, val, done);
	return true;
}

bool RTL8852BT::readXtalSi(u8 offset, u8 *out)
{
	u32 cmd = FIELD_PREP(B_AX_WL_XTAL_SI_ADDR_MASK, offset) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_DATA_MASK, 0x00) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_BITMASK_MASK, 0x00) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_MODE_MASK, XTAL_SI_NORMAL_READ) |
	          FIELD_PREP(B_AX_WL_XTAL_SI_CMD_POLL, 1);
	write32(R_AX_WLAN_XTAL_SI_CTRL, cmd);

	u32 done = 0;
	if (!pollReg32(R_AX_WLAN_XTAL_SI_CTRL, B_AX_WL_XTAL_SI_CMD_POLL,
	               false, 50, 50000, &done)) {
		RTLOG("xtal si not ready (R): offset=0x%02x", offset);
		return false;
	}
	if (out)
		*out = (u8)FIELD_GET(B_AX_WL_XTAL_SI_DATA_MASK, done);
	return true;
}

/* ------------------------------------------------------------------------ */
/* mac.c: rtw89_mac_reset_pwr_state_ax                                       */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::resetPowerState()
{
	u32 state = read32_mask(R_AX_IC_PWR_STATE, B_AX_WLMAC_PWR_STE_MASK);
	if (state == MAC_AX_MAC_ON) {
		/* En PCI esto es un error real: el MAC ya estaba encendido.
		 * (En USB el driver Linux devuelve EAGAIN; aqui no aplica.) */
		RTLOG("el MAC ya estaba encendido (IC_PWR_STATE=%u)", state);
		return false;
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/* rtw8852bt.c: rtw8852bt_pwr_on_func                                        */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::powerOn()
{
	if (!resetPowerState())
		return false;

	write32Set(R_AX_LDO_AON_CTRL0, B_AX_PD_REGU_L);
	write32Clr(R_AX_SYS_PW_CTRL, B_AX_AFSM_WLSUS_EN | B_AX_AFSM_PCIE_SUS_EN);
	write32Set(R_AX_SYS_PW_CTRL, B_AX_DIS_WLBT_PDNSUSEN_SOPC);
	write32Set(R_AX_WLLPS_CTRL, B_AX_DIS_WLBT_LPSEN_LOPC);
	write32Clr(R_AX_SYS_PW_CTRL, B_AX_APDM_HPDN);
	write32Clr(R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);
	write32_mask(R_AX_SPS_DIG_ON_CTRL0, B_AX_OCP_L1_MASK, 7);

	/* Esperar a que la alimentacion del sistema este lista */
	if (!pollReg32(R_AX_SYS_PW_CTRL, B_AX_RDY_SYSPWR, true, 1000, 20000, nullptr)) {
		RTLOG("timeout esperando B_AX_RDY_SYSPWR");
		return false;
	}

	write32Set(R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	write32Set(R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);

	/* El hardware limpia APFN_ONMAC cuando el MAC queda encendido */
	if (!pollReg32(R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC, false, 1000, 20000, nullptr)) {
		RTLOG("timeout esperando que se limpie B_AX_APFN_ONMAC");
		return false;
	}

	/* Pulso triple de PLATFORM_EN, tal cual en el driver Linux */
	write8Set(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	write8Clr(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	write8Set(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	write8Clr(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	write8Set(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);

	write32Clr(R_AX_SYS_SDIO_CTRL, B_AX_PCIE_CALIB_EN_V1);
	write32Set(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);

	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_GND_SHDN_WL, XTAL_SI_GND_SHDN_WL))
		return false;

	write32Set(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);

	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_SHDN_WL, XTAL_SI_SHDN_WL))  return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_WEI, XTAL_SI_OFF_WEI))  return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_EI, XTAL_SI_OFF_EI))    return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_RFC2RF))                 return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_PON_WEI, XTAL_SI_PON_WEI))  return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_PON_EI, XTAL_SI_PON_EI))    return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SRAM2RFC))               return false;
	if (!writeXtalSi(XTAL_SI_SRAM_CTRL, 0, XTAL_SI_SRAM_DIS))               return false;
	if (!writeXtalSi(XTAL_SI_XTAL_XMD_2, 0, XTAL_SI_LDO_LPS))               return false;
	if (!writeXtalSi(XTAL_SI_XTAL_XMD_4, 0, XTAL_SI_LPS_CAP))               return false;

	write32Set(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	write32Set(R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	write32Clr(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);

	IOSleep(1);   /* fsleep(1000) us */

	write32Clr(R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
	write32Clr(R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);

	/* El driver Linux ajusta el regulador solo si la efuse es valida y no
	 * trae calibracion de potencia. La lectura de efuse es FASE 2c, asi que
	 * de momento se omite este ajuste; no impide encender el MAC. */
	if (fEfuseValid && !fEfusePowerKValid) {
		write32_mask(R_AX_SPS_DIG_ON_CTRL0, B_AX_VOL_L1_MASK, 0x9);
		write32_mask(R_AX_SPS_DIG_ON_CTRL0, B_AX_VREFPFM_L_MASK, 0xA);
	}

	/* Habilitar DMAC y CMAC */
	write32Set(R_AX_DMAC_FUNC_EN,
	           B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN |
	           B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN | B_AX_TXPKT_CTRL_EN |
	           B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN |
	           B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN | B_AX_DLE_CPUIO_EN |
	           B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN |
	           B_AX_DMACREG_GCKEN);
	write32Set(R_AX_CMAC_FUNC_EN,
	           B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
	           B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN | B_AX_CMAC_DMA_EN |
	           B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN | B_AX_TMAC_EN |
	           B_AX_RMAC_EN);

	write32_mask(R_AX_EECS_EESK_FUNC_SEL, B_AX_PINMUX_EESK_FUNC_SEL_MASK, 0x1);

	fPoweredOn = true;
	RTLOG("MAC encendido: DMAC_FUNC_EN=0x%08x CMAC_FUNC_EN=0x%08x IC_PWR_STATE=%u",
	      read32(R_AX_DMAC_FUNC_EN), read32(R_AX_CMAC_FUNC_EN),
	      read32_mask(R_AX_IC_PWR_STATE, B_AX_WLMAC_PWR_STE_MASK));
	return true;
}

/* ------------------------------------------------------------------------ */
/* rtw8852bt.c: rtw8852bt_pwr_off_func                                       */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::powerOff()
{
	if (!fMmio)
		return false;

	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_RFC2RF, XTAL_SI_RFC2RF)) return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_EI))              return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_WEI))             return false;
	/* XTAL_SI_WL_RFC_S0 / S1 se omiten: solo aplican al apagado completo
	 * de la radio, que pertenece a FASE 3 (init de RF). */
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, XTAL_SI_SRAM2RFC, XTAL_SI_SRAM2RFC)) return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_EI))              return false;
	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_WEI))             return false;

	write32Set(R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	write32Clr(R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	write8Clr(R_AX_SYS_FUNC_EN, B_AX_FEN_BB_GLB_RSTN | B_AX_FEN_BBRSTB);
	write32Clr(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);

	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SHDN_WL)) return false;

	write32Clr(R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);

	if (!writeXtalSi(XTAL_SI_ANAPAR_WL, 0, XTAL_SI_GND_SHDN_WL)) return false;

	write32Set(R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC);

	if (!pollReg32(R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC, false, 1000, 20000, nullptr)) {
		RTLOG("timeout esperando que se limpie B_AX_APFM_OFFMAC");
		return false;
	}

	write32(R_AX_WLLPS_CTRL, SW_LPS_OPTION);
	write32Set(R_AX_SYS_SWR_CTRL1, B_AX_SYM_CTRL_SPS_PWMFREQ);
	write32_mask(R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x3);
	write32Set(R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	fPoweredOn = false;
	RTLOG("MAC apagado");
	return true;
}
