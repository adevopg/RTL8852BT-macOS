/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_fwdl.cpp - FASE 2d, primera parte: poner el chip en modo descarga
 *
 * Port de reference/rtw89-linux/mac.c:
 *   rtw89_mac_disable_cpu_ax       -> disableCpu()
 *   rtw89_disable_fw_watchdog      -> disableFwWatchdog()
 *   rtw89_mac_enable_cpu_ax        -> enableCpuForDownload()
 *   rtw89_fw_get_rdy_ax            -> getFwdlStatus()
 *   rtw89_fwdl_check_path_ready_ax -> waitPathReady()
 * y de fw.c:
 *   rtw89_fw_check_rdy             -> waitFirmwareReady()
 *
 * QUE HACE Y QUE NO. El chip lleva dentro un procesador propio (el WCPU) que
 * ejecuta el firmware. Para cargarselo hay que pararlo, ponerlo en modo de
 * descarga y esperar a que abra el camino. Eso es lo que hay aqui, y es solo
 * escritura de registros.
 *
 * Lo que NO esta todavia: enviar los bytes del firmware, que viaja en paquetes
 * H2C por el canal CH12 usando los anillos DMA de la fase 2b. Esa es la
 * segunda parte, y es donde el proyecto se juega de verdad la partida.
 *
 * Por que partirlo asi: esta mitad se puede comprobar sola. Si el chip entra
 * en modo descarga y abre el camino, el WCPU esta vivo y responde. Si no,
 * no tiene sentido intentar mandarle nada.
 */
#include "RTL8852BT.hpp"

/* fw.h:10 - estados que devuelve B_AX_WCPU_FWDL_STS_MASK */
static const char *fwdlStatusName(u8 st)
{
	switch (st) {
	case RTW89_FWDL_INITIAL_STATE:     return "estado inicial";
	case RTW89_FWDL_FWDL_ONGOING:      return "descarga en curso";
	case RTW89_FWDL_CHECKSUM_FAIL:     return "FALLO de checksum";
	case RTW89_FWDL_SECURITY_FAIL:     return "FALLO de seguridad";
	case RTW89_FWDL_CV_NOT_MATCH:      return "FALLO: version de chip no coincide";
	case RTW89_FWDL_WCPU_FWDL_RDY:     return "listo para recibir firmware";
	case RTW89_FWDL_WCPU_FW_INIT_RDY:  return "firmware arrancado";
	default:                           return "desconocido";
	}
}

void RTL8852BT::write16Mask(u32 addr, u16 mask, u16 v)
{
	u16 cur = read16(addr);
	write16(addr, (u16)((cur & ~mask) | ((v << (__builtin_ffs(mask) - 1)) & mask)));
}

/* mac.c: rtw89_fw_get_rdy_ax */
u8 RTL8852BT::getFwdlStatus()
{
	return (u8)FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, read8(R_AX_WCPU_FW_CTRL));
}

/* mac.c: rtw89_disable_fw_watchdog, rama rtl885xb (la del 8852BT).
 * Los chips que no son 885xb necesitan acceso indirecto a memoria del CPU;
 * el nuestro se conforma con un pulso a APB_WRAP. */
void RTL8852BT::disableFwWatchdog()
{
	write32Clr(R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);
	write32Set(R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);
}

/* mac.c: rtw89_mac_disable_cpu_ax */
void RTL8852BT::disableCpu()
{
	fFwReady = false;

	write32Clr(R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);
	write32Clr(R_AX_WCPU_FW_CTRL,
	           B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
	write32Clr(R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);

	disableFwWatchdog();

	write32Clr(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	write32Set(R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
}

/* mac.c: rtw89_mac_enable_cpu_ax con dlfw = true */
bool RTL8852BT::enableCpuForDownload()
{
	if (read32(R_AX_PLATFORM_ENABLE) & B_AX_WCPU_EN) {
		RTLOG("fwdl: el WCPU ya estaba encendido; hay que pararlo antes");
		return false;
	}

	/* Limpiar los buzones entre host y firmware antes de empezar */
	write32(R_AX_UDM1, 0);
	write32(R_AX_UDM2, 0);
	write32(R_AX_HALT_H2C_CTRL, 0);
	write32(R_AX_HALT_C2H_CTRL, 0);
	write32(R_AX_HALT_H2C, 0);
	write32(R_AX_HALT_C2H, 0);

	write32Set(R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);

	u32 val = read32(R_AX_WCPU_FW_CTRL);
	val &= ~(B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
	val = (val & ~B_AX_WCPU_FWDL_STS_MASK) |
	      FIELD_PREP(B_AX_WCPU_FWDL_STS_MASK, RTW89_FWDL_INITIAL_STATE);
	val |= B_AX_WCPU_FWDL_EN;      /* dlfw = true */
	write32(R_AX_WCPU_FW_CTRL, val);

	/* El 8852BT es de la familia rtl885xb y necesita este tamano de IDMEM */
	write32_mask(R_AX_SEC_CTRL, B_AX_SEC_IDMEM_SIZE_CONFIG_MASK, 0x2);

	write16Mask(R_AX_BOOT_REASON, B_AX_BOOT_REASON_MASK, 0);
	write32Set(R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);

	return true;
}

/* mac.c: rtw89_fwdl_check_path_ready_ax
 * h2cOrFwdl = true  -> esperar el camino de comandos H2C
 * h2cOrFwdl = false -> esperar el camino de descarga de firmware */
bool RTL8852BT::waitPathReady(bool h2cOrFwdl)
{
	u8 check = h2cOrFwdl ? B_AX_H2C_PATH_RDY : B_AX_FWDL_PATH_RDY;

	/* FWDL_WAIT_CNT = 400000 vueltas de 1 us */
	for (u32 i = 0; i < FWDL_WAIT_CNT; i++) {
		if (read8(R_AX_WCPU_FW_CTRL) & check)
			return true;
		IODelay(1);
	}
	RTLOG("fwdl: timeout esperando el camino %s (WCPU_FW_CTRL=0x%02x)",
	      h2cOrFwdl ? "H2C" : "de descarga", read8(R_AX_WCPU_FW_CTRL));
	return false;
}

/* fw.c: rtw89_fw_check_rdy. Solo tiene sentido DESPUES de enviar el firmware. */
bool RTL8852BT::waitFirmwareReady()
{
	for (u32 i = 0; i < FWDL_WAIT_CNT; i++) {
		u8 st = getFwdlStatus();
		if (st == RTW89_FWDL_WCPU_FW_INIT_RDY) {
			fFwReady = true;
			RTLOG("fwdl: el firmware ha arrancado dentro del chip.");
			return true;
		}
		if (st == RTW89_FWDL_CHECKSUM_FAIL || st == RTW89_FWDL_SECURITY_FAIL ||
		    st == RTW89_FWDL_CV_NOT_MATCH) {
			RTLOG("fwdl: %s (estado %u)", fwdlStatusName(st), st);
			return false;
		}
		IODelay(1);
	}
	u8 st = getFwdlStatus();
	RTLOG("fwdl: timeout; el chip se quedo en '%s' (estado %u)", fwdlStatusName(st), st);
	return false;
}

/* ------------------------------------------------------------------------ */
/* Punto de entrada de esta media fase                                       */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::prepareFirmwareDownload()
{
	if (!fFwValid) {
		RTLOG("fwdl: no hay firmware validado en memoria; nada que preparar");
		return false;
	}

	RTLOG("fwdl: estado inicial '%s' (%u)",
	      fwdlStatusName(getFwdlStatus()), getFwdlStatus());

	/* Parar el procesador interno antes de tocar nada */
	disableCpu();

	if (!enableCpuForDownload())
		return false;

	/* El chip debe abrir el camino de descarga */
	if (!waitPathReady(false))
		return false;

	u8 st = getFwdlStatus();
	RTLOG("fwdl: camino de descarga abierto. Estado '%s' (%u). "
	      "WCPU_FW_CTRL=0x%08x PLATFORM_ENABLE=0x%08x",
	      fwdlStatusName(st), st,
	      read32(R_AX_WCPU_FW_CTRL), read32(R_AX_PLATFORM_ENABLE));

	if (st != RTW89_FWDL_WCPU_FWDL_RDY) {
		RTLOG("fwdl: se esperaba 'listo para recibir firmware' (%u) y hay %u",
		      RTW89_FWDL_WCPU_FWDL_RDY, st);
		return false;
	}

	RTLOG("FASE 2d-1 lista: el chip espera el firmware. "
	      "Falta enviarlo por H2C, que es la segunda parte.");
	return true;
}
