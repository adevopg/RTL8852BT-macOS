/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT.cpp - FASE 1: attach PCI + BAR2 + version de chip + firmware
 *
 * Referencias en el driver Linux:
 *   pci.c   rtw89_pci_probe -> rtw89_pci_setup_mapping (BAR 2)
 *   core.c  rtw89_read_chip_ver (R_AX_SYS_CFG1)
 *   fw.c    rtw89_mfw_recognize / rtw89_fw_hdr_parser_v0
 */
#include "RTL8852BT.hpp"

#define super IOService
OSDefineMetaClassAndStructors(RTL8852BT, IOService)

/* ------------------------------------------------------------------------ */
/* Ciclo de vida IOKit                                                       */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::init(OSDictionary *dict)
{
	if (!super::init(dict))
		return false;
	RTLOG("init (fase 1, sin WiFi funcional)");
	return true;
}

void RTL8852BT::free()
{
	if (fFwData) {
		IOFree(fFwData, fFwLen);
		fFwData = nullptr;
	}
	super::free();
}

IOService *RTL8852BT::probe(IOService *provider, SInt32 *score)
{
	IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
	if (!pci)
		return nullptr;

	u16 vid = pci->configRead16(kIOPCIConfigVendorID);
	u16 did = pci->configRead16(kIOPCIConfigDeviceID);
	if (vid != 0x10ec || (did != 0xb520 && did != 0xb852 && did != 0xb85b))
		return nullptr;

	RTLOG("probe: %04x:%04x", vid, did);
	return super::probe(provider, score);
}

bool RTL8852BT::start(IOService *provider)
{
	if (!super::start(provider))
		return false;

	fPci = OSDynamicCast(IOPCIDevice, provider);
	if (!fPci) {
		RTLOG("provider no es IOPCIDevice");
		return false;
	}
	fPci->retain();

	fVendorId  = fPci->configRead16(kIOPCIConfigVendorID);
	fDeviceId  = fPci->configRead16(kIOPCIConfigDeviceID);
	fSubVendor = fPci->configRead16(kIOPCIConfigSubSystemVendorID);
	fSubDevice = fPci->configRead16(kIOPCIConfigSubSystemID);
	RTLOG("attached to PCI %04x:%04x (subsys %04x:%04x)",
	      fVendorId, fDeviceId, fSubVendor, fSubDevice);

	/* Equivalente a pci_enable_device + pci_set_master */
	fPci->setMemoryEnable(true);
	fPci->setBusMasterEnable(true);

	if (!mapBar())
		goto fail;
	if (!readChipVersion())
		goto fail;

	/* La validacion del firmware llega por callback asincrono */
	if (!requestFirmware())
		RTLOG("aviso: no se pudo pedir el firmware (se sigue sin el)");

	/* FASE 2a: encender el MAC. Si falla, el kext sigue cargado para que
	 * los registros se puedan inspeccionar desde el log. */
	if (!powerOn())
		RTLOG("FASE 2a FALLO: el MAC no se encendio. Fase 1 sigue valida.");

	registerService();
	RTLOG("arranque terminado. poweredOn=%d fwValid=%d. NO hay WiFi aun.",
	      fPoweredOn ? 1 : 0, fFwValid ? 1 : 0);
	return true;

fail:
	unmapBar();
	fPci->release();
	fPci = nullptr;
	return false;
}

void RTL8852BT::stop(IOService *provider)
{
	RTLOG("stop");
	if (fPoweredOn)
		powerOff();
	unmapBar();
	if (fPci) {
		fPci->setBusMasterEnable(false);
		fPci->release();
		fPci = nullptr;
	}
	super::stop(provider);
}

/* ------------------------------------------------------------------------ */
/* BAR2 (MMIO). Ver pci.c: bar_id = 2, pci_iomap(pdev, 2, len)              */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::mapBar()
{
	fBarMap = fPci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
	if (!fBarMap) {
		RTLOG("no se pudo mapear BAR2");
		return false;
	}
	fMmio    = reinterpret_cast<volatile u8 *>(fBarMap->getVirtualAddress());
	fMmioLen = (u32)fBarMap->getLength();
	RTLOG("BAR2 mapped, %u KB at 0x%llx", fMmioLen / 1024,
	      (unsigned long long)fBarMap->getPhysicalAddress());
	return true;
}

void RTL8852BT::unmapBar()
{
	if (fBarMap) {
		fBarMap->release();
		fBarMap = nullptr;
	}
	fMmio = nullptr;
	fMmioLen = 0;
}

u8  RTL8852BT::read8(u32 a)  { return *(volatile u8  *)(fMmio + a); }
u16 RTL8852BT::read16(u32 a) { return *(volatile u16 *)(fMmio + a); }
u32 RTL8852BT::read32(u32 a) { return *(volatile u32 *)(fMmio + a); }
void RTL8852BT::write8(u32 a, u8 v)   { *(volatile u8  *)(fMmio + a) = v; }
void RTL8852BT::write16(u32 a, u16 v) { *(volatile u16 *)(fMmio + a) = v; }
void RTL8852BT::write32(u32 a, u32 v) { *(volatile u32 *)(fMmio + a) = v; }

u32 RTL8852BT::read32_mask(u32 addr, u32 mask)
{
	return FIELD_GET(mask, read32(addr));
}

void RTL8852BT::write32_mask(u32 addr, u32 mask, u32 v)
{
	u32 cur = read32(addr);
	write32(addr, (cur & ~mask) | FIELD_PREP(mask, v));
}

/* ------------------------------------------------------------------------ */
/* Version de chip. core.c: rtw89_read_chip_ver                              */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::readChipVersion()
{
	u32 cfg1 = read32(R_AX_SYS_CFG1);
	if (cfg1 == RTW89_R32_DEAD || cfg1 == 0xffffffff) {
		RTLOG("R_AX_SYS_CFG1 devuelve 0x%08x: el chip no responde (bus apagado?)", cfg1);
		return false;
	}
	fChipCv = (u8)FIELD_GET(B_AX_CHIP_VER_MASK, cfg1);
	RTLOG("R_AX_SYS_CFG1=0x%08x  chip cv=%u  SYS_STATUS1=0x%08x  PW_CTRL=0x%08x",
	      cfg1, fChipCv, read32(R_AX_SYS_STATUS1), read32(R_AX_SYS_PW_CTRL));
	setProperty("ChipCV", fChipCv, 8);

	/* core.c: para rtl885xb se lee ademas la version analogica via xtal_si.
	 * Requiere que el bus xtal_si responda, asi que un fallo aqui no es fatal. */
	u8 acv = 0;
	if (readXtalSi(XTAL_SI_CV, &acv)) {
		fChipAcv = (u8)FIELD_GET(XTAL_SI_ACV_MASK, acv);
		RTLOG("chip acv=%u", fChipAcv);
		setProperty("ChipACV", fChipAcv, 8);
	} else {
		RTLOG("aviso: no se pudo leer XTAL_SI_CV (acv desconocido)");
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/* Firmware. Se carga desde Contents/Resources/rtw8852bt_fw.bin del kext.   */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::requestFirmware()
{
	OSReturn r = OSKextRequestResource(OSKextGetCurrentIdentifier(),
	                                   "rtw8852bt_fw.bin",
	                                   firmwareCallback, this, nullptr);
	if (r != kOSReturnSuccess) {
		RTLOG("OSKextRequestResource fallo: 0x%x", r);
		return false;
	}
	return true;
}

void RTL8852BT::firmwareCallback(OSKextRequestTag, OSReturn result,
                                 const void *data, uint32_t length, void *ctx)
{
	RTL8852BT *self = static_cast<RTL8852BT *>(ctx);
	if (result != kOSReturnSuccess || !data || !length) {
		RTLOG("firmware no disponible (0x%x). Copia firmware/rtw8852bt_fw.bin a Contents/Resources", result);
		return;
	}
	self->fFwData = (u8 *)IOMalloc(length);
	if (!self->fFwData)
		return;
	memcpy(self->fFwData, data, length);
	self->fFwLen = length;
	self->fFwValid = self->parseFirmware(self->fFwData, length);
}

/* fw.c: rtw89_mfw_get_hdr_ptr + rtw89_mfw_validate_hdr + rtw89_mfw_recognize */
bool RTL8852BT::parseFirmware(const u8 *fw, u32 len)
{
	if (len < sizeof(rtw89_mfw_hdr)) {
		RTLOG("firmware demasiado corto (%u)", len);
		return false;
	}
	const rtw89_mfw_hdr *mfw = (const rtw89_mfw_hdr *)fw;
	if (mfw->sig != RTW89_MFW_SIG) {
		/* Fichero con un unico firmware, sin contenedor */
		return parseSingleFirmware(fw, len, &fFwNormal);
	}
	if (mfw->fw_nr == 0 ||
	    sizeof(rtw89_mfw_hdr) + (u32)mfw->fw_nr * sizeof(rtw89_mfw_info) > len) {
		RTLOG("mfw header invalido (fw_nr=%u)", mfw->fw_nr);
		return false;
	}
	RTLOG("firmware MFW v%u.%u.%u.%u, %u entradas",
	      mfw->ver.major, mfw->ver.minor, mfw->ver.sub, mfw->ver.idx, mfw->fw_nr);

	/* Misma regla que rtw89_mfw_recognize: entrada de tipo NORMAL, no MP,
	 * con cv <= cv del chip, quedandonos con la mas alta. */
	const rtw89_mfw_info *pick = nullptr;
	for (u32 i = 0; i < mfw->fw_nr; i++) {
		const rtw89_mfw_info *info = &mfw->info[i];
		u32 shift = le32_to_cpu(info->shift), size = le32_to_cpu(info->size);
		RTLOG("  [%u] cv=0x%02x type=%u mp=%u shift=0x%x size=%u",
		      i, info->cv, info->type, info->mp, shift, size);
		if (info->type != RTW89_FW_NORMAL || info->mp)
			continue;
		if (info->cv <= fChipCv && (!pick || pick->cv < info->cv))
			pick = info;
	}
	if (!pick) {
		RTLOG("no hay firmware NORMAL para cv=%u", fChipCv);
		return false;
	}
	u32 shift = le32_to_cpu(pick->shift), size = le32_to_cpu(pick->size);
	if (shift + size > len) {
		RTLOG("entrada fuera del fichero");
		return false;
	}
	return parseSingleFirmware(fw + shift, size, &fFwNormal);
}

/* fw.c: rtw89_fw_hdr_parser_v0 (solo la cabecera, sin construir la lista de secciones) */
bool RTL8852BT::parseSingleFirmware(const u8 *fw, u32 len, rtw89_fw_bin_summary *out)
{
	if (len < sizeof(rtw89_fw_hdr))
		return false;
	const rtw89_fw_hdr *h = (const rtw89_fw_hdr *)fw;
	out->major      = (u8)le32_get_bits(h->w1, FW_HDR_W1_MAJOR_VERSION);
	out->minor      = (u8)le32_get_bits(h->w1, FW_HDR_W1_MINOR_VERSION);
	out->sub        = (u8)le32_get_bits(h->w1, FW_HDR_W1_SUBVERSION);
	out->idx        = (u8)le32_get_bits(h->w1, FW_HDR_W1_SUBINDEX);
	out->commit_id  = le32_get_bits(h->w2, FW_HDR_W2_COMMITID);
	out->section_num = le32_get_bits(h->w6, FW_HDR_W6_SEC_NUM);
	out->dynamic_hdr = le32_get_bits(h->w7, FW_HDR_W7_DYN_HDR) != 0;
	out->hdr_len = out->dynamic_hdr
		? le32_get_bits(h->w3, FW_HDR_W3_LEN)
		: (u32)(sizeof(rtw89_fw_hdr) + out->section_num * sizeof(rtw89_fw_hdr_section));

	if (out->hdr_len > len || out->section_num == 0 || out->section_num > 64) {
		RTLOG("cabecera de firmware incoherente (hdr_len=%u sec=%u)", out->hdr_len, out->section_num);
		return false;
	}

	u32 total = 0;
	for (u32 i = 0; i < out->section_num; i++)
		total += le32_get_bits(h->sections[i].w1, FWSECTION_HDR_W1_SEC_SIZE);
	if (out->hdr_len + total > len) {
		RTLOG("secciones (%u bytes) exceden el fichero (%u)", total, len);
		return false;
	}

	RTLOG("firmware rtw8852bt_fw.bin OK, v%u.%u.%u.%u commit %08x, %u sections, %u bytes de codigo",
	      out->major, out->minor, out->sub, out->idx, out->commit_id, out->section_num, total);
	setProperty("FirmwareVersion", out->minor, 32);
	return true;
}
