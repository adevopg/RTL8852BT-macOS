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
 * IOPCIDevice sigue siendo la API correcta. El aviso se silencia en el Makefile
 * con -Wno-deprecated-declarations. */
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>

class IOWorkLoop;
class IOFilterInterruptEventSource;

#include "rtw89_compat.h"
#include "rtw89_fw_hdr.h"
#include "rtw89_regs.h"
#include "rtw89_efuse_8852bt.h"
#include "rtw89_pci_desc.h"

#define DRV_NAME "RTL8852BT"
#define RTLOG(fmt, ...) IOLog(DRV_NAME ": " fmt "\n", ##__VA_ARGS__)

/* Los registros viven ahora en rtw89_regs.h */

/* Versiones de silicio (enum rtw89_core_chip_cv en core.h) */
enum rtw89_chip_cv {
	CHIP_CAV = 0, CHIP_CBV, CHIP_CCV, CHIP_CDV, CHIP_CEV, CHIP_CFV,
};

/* Un anillo de descriptores compartido con el chip. */
struct rtw89_ring {
	IOBufferMemoryDescriptor *buf = nullptr;
	u8  *virt     = nullptr;   /* como lo ve el kernel */
	u32  phys     = 0;         /* como lo ve el chip (siempre < 4 GB) */
	u32  numDesc  = 0;
	u32  descSize = 0;
	u32  bytes    = 0;
	u32  wp       = 0;         /* indice del host */
	u32  rp       = 0;         /* indice del hardware */
};

/* Una seccion del firmware: un trozo de codigo y la direccion de la memoria
 * interna del chip donde hay que dejarlo. */
struct rtw89_fw_section {
	u32 dlAddr;    /* destino dentro del chip */
	u32 offset;    /* donde empieza dentro del fichero */
	u32 len;
	u8  type;
};

#define RTW89_FW_MAX_SECTIONS 16

struct rtw89_fw_bin_summary {
	u8  major, minor, sub, idx;
	u32 commit_id;
	u32 section_num;
	bool dynamic_hdr;
	u32 hdr_len;
	u32 dynamicHdrLen;
	u32 partSize;
	const u8 *base;      /* inicio de este firmware dentro del fichero */
	u32 totalLen;
	rtw89_fw_section sections[RTW89_FW_MAX_SECTIONS];
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
	void write16Set(u32 addr, u16 bits);
	void write16Clr(u32 addr, u16 bits);

	/* FASE 2c - efuse: direccion MAC y calibracion (RTL8852BT_efuse.cpp) */
	bool readEfuse();
	bool dumpPhysicalEfuse(u8 *map, u32 dumpAddr, u32 dumpSize);
	bool dumpLogicalEfuse(const u8 *phyMap, u8 *logMap);
	bool parseEfuseMap(const u8 *logMap);
	void enableEfusePwrCut();
	void disableEfusePwrCut();

	/* FASE 2b - anillos DMA e interrupciones (RTL8852BT_pci.cpp) */
	bool setupDma();
	void teardownDma();
	bool allocRings();
	void freeRings();
	bool allocRing(struct rtw89_ring *ring, u32 numDesc, u32 descSize, const char *name);
	void freeRing(struct rtw89_ring *ring);
	void programRings();
	bool setupInterrupt();
	void teardownInterrupt();
	void enableInterrupts();
	void disableInterrupts();
	static bool interruptFilter(OSObject *owner, IOFilterInterruptEventSource *src);
	static void interruptOccurred(OSObject *owner, IOInterruptEventSource *src, int count);

	/* FASE 2d-1 - modo descarga de firmware (RTL8852BT_fwdl.cpp) */
	bool prepareFirmwareDownload();
	void disableCpu();
	void disableFwWatchdog();
	bool enableCpuForDownload();
	bool waitPathReady(bool h2cOrFwdl);
	bool waitFirmwareReady();
	u8   getFwdlStatus();
	void write16Mask(u32 addr, u16 mask, u16 v);

	/* FASE 2d-2 - enviar el firmware por H2C (RTL8852BT_h2c.cpp) */
	bool downloadFirmware();
	bool sendFirmwareHeader();
	bool sendFirmwareSections();
	bool sendH2C(const u8 *payload, u32 payloadLen, bool withHeader,
	             u8 cat, u8 cls, u8 func, bool fwDl);
	bool submitH2C(u32 physAddr, u32 totalLen);
	void fillTxDesc(u8 *desc, u32 payloadLen, bool fwDl);
	void fillH2CHeader(u8 *hdr, u8 cat, u8 cls, u8 func, u32 len);
	bool allocH2CBuffers();
	void freeH2CBuffers();

private:
	bool mapBar();
	void unmapBar();
	bool readChipVersion();
	bool requestFirmware();
	static void firmwareCallback(OSKextRequestTag tag, OSReturn result,
	                             const void *data, uint32_t length, void *ctx);
	bool parseFirmware(const u8 *fw, u32 len);
	bool parseSingleFirmware(const u8 *fw, u32 len, rtw89_fw_bin_summary *out);

	IOPCIDevice   *fPci    = nullptr;
	IOMemoryMap   *fBarMap = nullptr;
	volatile u8   *fMmio   = nullptr;
	u32            fMmioLen = 0;

	u16 fVendorId = 0, fDeviceId = 0, fSubVendor = 0, fSubDevice = 0;
	u8  fChipCv = 0;
	u8  fChipAcv = 0;
	bool fPoweredOn = false;
	bool fEfuseRead = false;
	bool fDmaReady = false;
	bool fFwdlReady = false;
	bool fFwReady = false;

	/* FASE 2d-2 */
	static const u32 H2C_BUF_COUNT = 4;
	rtw89_ring fH2CBuf[H2C_BUF_COUNT];
	u32 fH2CBufNext = 0;
	u8  fH2CSeq = 0;
	/* FASE 2c leera la efuse de verdad; hasta entonces se asume invalida,
	 * lo que hace que powerOn() omita el ajuste del regulador. */
	bool fEfuseValid = false;
	bool fEfusePowerKValid = false;
	/* Rellenados por la fase 2c */
	u8  fMacAddr[ETH_ALEN] = {};
	u8  fRfeType = 0;
	u8  fXtalCap = 0;
	char fCountry[2] = {};

	/* FASE 2b */
	rtw89_ring fTxRing[RTW8852BT_TXCH_COUNT];
	rtw89_ring fRxRing[RTW8852BT_RXCH_COUNT];
	bool fRingsAllocated = false;
	IOWorkLoop *fWorkLoop = nullptr;
	IOFilterInterruptEventSource *fIntSource = nullptr;
	volatile u64 fIrqCount = 0;
	volatile u32 fLastHisr00 = 0, fLastHisr10 = 0, fLastHisr0 = 0;

	/* Copia del firmware en memoria del kernel (se libera en stop) */
	u8  *fFwData = nullptr;
	u32  fFwLen  = 0;
	bool fFwValid = false;
	rtw89_fw_bin_summary fFwNormal = {};
};

#endif /* RTL8852BT_HPP */
