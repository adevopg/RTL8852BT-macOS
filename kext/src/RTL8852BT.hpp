/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT.hpp - driver IOKit para Realtek RTL8852BE-VT (PCI 10EC:B520)
 *
 * FASE 1: attach PCI, mapeo de BAR2, lectura de version de chip, carga y
 * validacion del firmware. No inicializa el MAC ni la radio.
 */
#ifndef RTL8852BT_HPP
#define RTL8852BT_HPP

#include <IOKit/IOService.h>
/* IOPCIDevice esta marcado deprecated a favor de PCIDriverKit, pero PCIDriverKit
 * es para DriverKit (espacio de usuario), no para kexts. Para un driver de kernel
 * IOPCIDevice sigue siendo la API correcta, asi que silenciamos el aviso. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <IOKit/pci/IOPCIDevice.h>
#pragma clang diagnostic pop
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>

#include "rtw89_compat.h"
#include "rtw89_fw_hdr.h"
#include "rtw89_regs.h"

#define DRV_NAME "RTL8852BT"
#define RTLOG(fmt, ...) IOLog(DRV_NAME ": " fmt "\n", ##__VA_ARGS__)

/* Los registros viven ahora en rtw89_regs.h */

/* Versiones de silicio (enum rtw89_core_chip_cv en core.h) */
enum rtw89_chip_cv {
	CHIP_CAV = 0, CHIP_CBV, CHIP_CCV, CHIP_CDV, CHIP_CEV, CHIP_CFV,
};

struct rtw89_fw_bin_summary {
	u8  major, minor, sub, idx;
	u32 commit_id;
	u32 section_num;
	bool dynamic_hdr;
	u32 hdr_len;
};

class RTL8852BT : public IOService {
	OSDeclareDefaultStructors(RTL8852BT)

public:
	virtual bool init(OSDictionary *dict = nullptr) override;
	virtual void free() override;
	virtual IOService *probe(IOService *provider, SInt32 *score) override;
	virtual bool start(IOService *provider) override;
	virtual void stop(IOService *provider) override;

	/* Acceso MMIO, equivalente a rtw89_pci_ops_read32 / write32 */
	u8   read8(u32 addr);
	u16  read16(u32 addr);
	u32  read32(u32 addr);
	void write8(u32 addr, u8 v);
	void write16(u32 addr, u16 v);
	void write32(u32 addr, u32 v);
	u32  read32_mask(u32 addr, u32 mask);
	void write32_mask(u32 addr, u32 mask, u32 v);
	void write32Set(u32 addr, u32 bits);
	void write32Clr(u32 addr, u32 bits);
	void write8Set(u32 addr, u8 bits);
	void write8Clr(u32 addr, u8 bits);

	/* FASE 2a - encendido del MAC (RTL8852BT_power.cpp) */
	bool powerOn();
	bool powerOff();
	bool resetPowerState();
	bool writeXtalSi(u8 offset, u8 val, u8 mask);
	bool readXtalSi(u8 offset, u8 *out);
	bool pollReg32(u32 addr, u32 mask, bool waitSet,
	               u32 sleepUs, u32 timeoutUs, u32 *lastVal);

private:
	bool mapBar();
	void unmapBar();
	bool readChipVersion();
	bool requestFirmware();
	static void firmwareCallback(OSKextRequestTag tag, OSReturn result,
	                             const void *data, uint32_t length, void *ctx);
	bool parseFirmware(const u8 *fw, u32 len);
	bool parseSingleFirmware(const u8 *fw, u32 len, rtw89_fw_bin_summary *out);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	IOPCIDevice   *fPci    = nullptr;
#pragma clang diagnostic pop
	IOMemoryMap   *fBarMap = nullptr;
	volatile u8   *fMmio   = nullptr;
	u32            fMmioLen = 0;

	u16 fVendorId = 0, fDeviceId = 0, fSubVendor = 0, fSubDevice = 0;
	u8  fChipCv = 0;
	u8  fChipAcv = 0;
	bool fPoweredOn = false;
	/* FASE 2c leera la efuse de verdad; hasta entonces se asume invalida,
	 * lo que hace que powerOn() omita el ajuste del regulador. */
	bool fEfuseValid = false;
	bool fEfusePowerKValid = false;

	/* Copia del firmware en memoria del kernel (se libera en stop) */
	u8  *fFwData = nullptr;
	u32  fFwLen  = 0;
	bool fFwValid = false;
	rtw89_fw_bin_summary fFwNormal = {};
};

#endif /* RTL8852BT_HPP */
