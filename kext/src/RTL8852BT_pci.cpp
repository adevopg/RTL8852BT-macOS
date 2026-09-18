/* SPDX-License-Identifier: BSD-3-Clause
 *
 * RTL8852BT_pci.cpp - FASE 2b: anillos DMA e interrupciones
 *
 * Port de reference/rtw89-linux/pci.c:
 *   rtw89_pci_alloc_trx_rings     -> allocRings()
 *   rtw89_pci_reset_trx_rings     -> programRings()
 *   rtw89_pci_enable_intr         -> enableInterrupts()
 *   rtw89_pci_disable_intr        -> disableInterrupts()
 *
 * ALCANCE DE ESTA FASE: reservar los anillos de descriptores, decirle al chip
 * donde estan, y conseguir que una interrupcion llegue. NO se transmite ni se
 * recibe ningun paquete todavia; el camino de datos es trabajo posterior.
 *
 * LA TRAMPA IMPORTANTE. En Linux, dma_alloc_coherent da memoria que el
 * dispositivo puede alcanzar. En macOS hay que pedirlo explicitamente: si
 * reservas memoria normal, el sistema puede colocarla por encima de los 4 GB
 * y el chip escribira en una direccion que no existe para el, corrompiendo
 * memoria ajena. Por eso aqui se usa inTaskWithPhysicalMask con mascara de
 * 32 bits: fuerza direcciones que caben en DESA_L, y DESA_H queda a cero.
 */
#include "RTL8852BT.hpp"
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOWorkLoop.h>

/* ------------------------------------------------------------------------ */
/* Reserva de un anillo                                                      */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::allocRing(rtw89_ring *ring, u32 numDesc, u32 descSize, const char *name)
{
	u32 bytes = numDesc * descSize;

	/* kIOMemoryPhysicallyContiguous: el chip recorre el anillo por direcciones
	 * consecutivas, asi que tiene que serlo de verdad, no solo en virtual.
	 * La mascara 0xFFFFFFFF obliga a que quepa en 32 bits. */
	IOBufferMemoryDescriptor *buf = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
	        kernel_task,
	        kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache,
	        bytes,
	        0x00000000FFFFFFFFULL);
	if (!buf) {
		RTLOG("anillo %s: no se pudo reservar %u bytes bajo los 4 GB", name, bytes);
		return false;
	}
	if (buf->prepare() != kIOReturnSuccess) {
		RTLOG("anillo %s: prepare() fallo", name);
		buf->release();
		return false;
	}

	IOPhysicalAddress phys = buf->getPhysicalAddress();
	if (phys == 0 || (phys + bytes) > 0x100000000ULL) {
		RTLOG("anillo %s: direccion fisica 0x%llx fuera de los 4 GB", name,
		      (unsigned long long)phys);
		buf->complete();
		buf->release();
		return false;
	}

	ring->buf      = buf;
	ring->virt     = (u8 *)buf->getBytesNoCopy();
	ring->phys     = (u32)phys;
	ring->numDesc  = numDesc;
	ring->descSize = descSize;
	ring->bytes    = bytes;
	ring->wp = 0;
	ring->rp = 0;
	memset(ring->virt, 0, bytes);

	RTLOG("anillo %-16s %3u desc x %u B = %5u B  fisica 0x%08x",
	      name, numDesc, descSize, bytes, ring->phys);
	return true;
}

void RTL8852BT::freeRing(rtw89_ring *ring)
{
	if (ring->buf) {
		ring->buf->complete();
		ring->buf->release();
		ring->buf = nullptr;
	}
	ring->virt = nullptr;
	ring->phys = 0;
}

/* ------------------------------------------------------------------------ */
/* pci.c: rtw89_pci_alloc_trx_rings                                          */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::allocRings()
{
	RTLOG("reservando anillos DMA (%u TX + %u RX)",
	      (unsigned)RTW8852BT_TXCH_COUNT, (unsigned)RTW8852BT_RXCH_COUNT);

	for (u32 i = 0; i < RTW8852BT_TXCH_COUNT; i++) {
		if (!allocRing(&fTxRing[i], RTW89_PCI_TXBD_NUM_MAX,
		               sizeof(rtw89_pci_tx_bd_32), RTW8852BT_TXCH[i].name))
			goto fail;
	}
	for (u32 i = 0; i < RTW8852BT_RXCH_COUNT; i++) {
		if (!allocRing(&fRxRing[i], RTW89_PCI_RXBD_NUM_MAX,
		               sizeof(rtw89_pci_rx_bd_32), RTW8852BT_RXCH[i].name))
			goto fail;
	}

	fRingsAllocated = true;
	return true;

fail:
	freeRings();
	return false;
}

void RTL8852BT::freeRings()
{
	for (u32 i = 0; i < RTW8852BT_TXCH_COUNT; i++)
		freeRing(&fTxRing[i]);
	for (u32 i = 0; i < RTW8852BT_RXCH_COUNT; i++)
		freeRing(&fRxRing[i]);
	fRingsAllocated = false;
}

/* ------------------------------------------------------------------------ */
/* pci.c: rtw89_pci_reset_trx_rings                                          */
/* Decirle al chip cuantos descriptores tiene cada anillo y donde estan.     */
/* ------------------------------------------------------------------------ */

void RTL8852BT::programRings()
{
	for (u32 i = 0; i < RTW8852BT_TXCH_COUNT; i++) {
		const rtw89_pci_txch_def *def = &RTW8852BT_TXCH[i];
		rtw89_ring *ring = &fTxRing[i];
		ring->wp = 0;
		ring->rp = 0;

		write16(def->addr.num, (u16)ring->numDesc);

		/* Reparto de la RAM interna de descriptores del chip */
		u32 bdram = FIELD_PREP(BDRAM_SIDX_MASK, def->bd_ram.start_idx) |
		            FIELD_PREP(BDRAM_MAX_MASK,  def->bd_ram.max_num)   |
		            FIELD_PREP(BDRAM_MIN_MASK,  def->bd_ram.min_num);
		write32(def->addr.bdram, bdram);

		write32(def->addr.desa_l, ring->phys);
		write32(def->addr.desa_h, 0);   /* siempre por debajo de los 4 GB */
	}

	for (u32 i = 0; i < RTW8852BT_RXCH_COUNT; i++) {
		const rtw89_pci_rxch_def *def = &RTW8852BT_RXCH[i];
		rtw89_ring *ring = &fRxRing[i];
		ring->wp = 0;
		ring->rp = 0;

		write16(def->addr.num, (u16)ring->numDesc);
		write32(def->addr.desa_l, ring->phys);
		write32(def->addr.desa_h, 0);
	}

	/* Releer para confirmar que el chip acepto las direcciones. Si un anillo
	 * devuelve 0 o algo distinto, el bus no esta escribiendo donde creemos. */
	u32 bad = 0;
	for (u32 i = 0; i < RTW8852BT_TXCH_COUNT; i++) {
		u32 back = read32(RTW8852BT_TXCH[i].addr.desa_l);
		if (back != fTxRing[i].phys) {
			RTLOG("anillo %s: escribi 0x%08x pero lei 0x%08x",
			      RTW8852BT_TXCH[i].name, fTxRing[i].phys, back);
			bad++;
		}
	}
	for (u32 i = 0; i < RTW8852BT_RXCH_COUNT; i++) {
		u32 back = read32(RTW8852BT_RXCH[i].addr.desa_l);
		if (back != fRxRing[i].phys) {
			RTLOG("anillo %s: escribi 0x%08x pero lei 0x%08x",
			      RTW8852BT_RXCH[i].name, fRxRing[i].phys, back);
			bad++;
		}
	}

	if (bad)
		RTLOG("FASE 2b: %u anillos NO retienen su direccion. El chip no los ve.", bad);
	else
		RTLOG("FASE 2b: los %u anillos retienen su direccion. El chip sabe donde estan.",
		      (unsigned)(RTW8852BT_TXCH_COUNT + RTW8852BT_RXCH_COUNT));
}

/* ------------------------------------------------------------------------ */
/* Interrupciones                                                            */
/* pci.c: rtw89_pci_enable_intr / rtw89_pci_disable_intr                     */
/* ------------------------------------------------------------------------ */

void RTL8852BT::disableInterrupts()
{
	if (!fMmio)
		return;
	write32(R_AX_HIMR0, 0);
	write32(R_AX_PCIE_HIMR00, 0);
	write32(R_AX_PCIE_HIMR10, 0);
}

void RTL8852BT::enableInterrupts()
{
	/* Limpiar cualquier interrupcion pendiente antes de desenmascarar, o la
	 * primera que llegue sera basura del arranque. */
	write32(R_AX_HISR0, read32(R_AX_HISR0));
	write32(R_AX_PCIE_HISR00, read32(R_AX_PCIE_HISR00));
	write32(R_AX_PCIE_HISR10, read32(R_AX_PCIE_HISR10));

	/* En esta fase solo interesa saber que LLEGAN. El reparto fino de que
	 * causas habilitar es trabajo de la fase de datos; aqui se abre el aviso
	 * de nivel superior del bloque PCIe. */
	write32(R_AX_HIMR0, B_AX_HS0ISR_IND_INT_EN);
	write32(R_AX_PCIE_HIMR00, B_AX_HCI_AXIDMA_INT_EN);
	write32(R_AX_PCIE_HIMR10, 0);
}

/* Filtro: corre en contexto de interrupcion primario. Debe ser corto y solo
 * decir si esta interrupcion es nuestra. */
bool RTL8852BT::interruptFilter(OSObject *owner, IOFilterInterruptEventSource *src)
{
	RTL8852BT *self = OSDynamicCast(RTL8852BT, owner);
	if (!self || !self->fMmio)
		return false;

	u32 hisr00 = self->read32(R_AX_PCIE_HISR00);
	u32 hisr10 = self->read32(R_AX_PCIE_HISR10);
	u32 hisr0  = self->read32(R_AX_HISR0);
	if (!hisr00 && !hisr10 && !hisr0)
		return false;   /* no es nuestra: compartida con otro dispositivo */

	/* Reconocer escribiendo de vuelta los bits activos y guardarlos para el
	 * manejador secundario. */
	self->fLastHisr00 = hisr00;
	self->fLastHisr10 = hisr10;
	self->fLastHisr0  = hisr0;
	self->write32(R_AX_PCIE_HISR00, hisr00);
	self->write32(R_AX_PCIE_HISR10, hisr10);
	self->write32(R_AX_HISR0, hisr0);
	self->fIrqCount++;
	return true;
}

/* Manejador secundario: corre en el workloop, puede tomarse su tiempo. */
void RTL8852BT::interruptOccurred(OSObject *owner, IOInterruptEventSource *src, int count)
{
	RTL8852BT *self = OSDynamicCast(RTL8852BT, owner);
	if (!self)
		return;
	/* Las primeras interrupciones se registran para poder confirmar que el
	 * camino funciona. Despues se callan para no inundar el log. */
	if (self->fIrqCount <= 5)
		RTLOG("interrupcion #%llu: HISR00=0x%08x HISR10=0x%08x HISR0=0x%08x",
		      (unsigned long long)self->fIrqCount,
		      self->fLastHisr00, self->fLastHisr10, self->fLastHisr0);
	else if (self->fIrqCount == 6)
		RTLOG("interrupciones llegando con normalidad; dejo de registrarlas una a una");
}

bool RTL8852BT::setupInterrupt()
{
	fWorkLoop = IOWorkLoop::workLoop();
	if (!fWorkLoop) {
		RTLOG("no se pudo crear el workloop");
		return false;
	}

	/* Buscar una fuente de interrupcion por mensaje (MSI). Las tarjetas
	 * PCIe modernas la traen, y es preferible a la de linea porque no se
	 * comparte con otros dispositivos. */
	int msiIndex = -1;
	for (int i = 0; i < 8; i++) {
		int type = 0;
		if (fPci->getInterruptType(i, &type) != kIOReturnSuccess)
			break;
		if (type & kIOInterruptTypePCIMessaged) {
			msiIndex = i;
			break;
		}
	}
	if (msiIndex < 0) {
		RTLOG("esta tarjeta no ofrece MSI; se usara la interrupcion de linea (indice 0)");
		msiIndex = 0;
	} else {
		RTLOG("MSI disponible en el indice %d", msiIndex);
	}

	/* interruptOccurred e interruptFilter son estaticas y ya tienen la firma
	 * exacta que pide IOKit, asi que van directas. OSMemberFunctionCast es
	 * para metodos de instancia; usarlo aqui no compila. */
	fIntSource = IOFilterInterruptEventSource::filterInterruptEventSource(
	        this,
	        &RTL8852BT::interruptOccurred,
	        &RTL8852BT::interruptFilter,
	        fPci, msiIndex);
	if (!fIntSource) {
		RTLOG("no se pudo crear la fuente de interrupcion");
		return false;
	}
	if (fWorkLoop->addEventSource(fIntSource) != kIOReturnSuccess) {
		RTLOG("no se pudo anadir la fuente al workloop");
		fIntSource->release();
		fIntSource = nullptr;
		return false;
	}
	fIntSource->enable();
	RTLOG("interrupcion registrada (indice %d)", msiIndex);
	return true;
}

void RTL8852BT::teardownInterrupt()
{
	if (fIntSource) {
		fIntSource->disable();
		if (fWorkLoop)
			fWorkLoop->removeEventSource(fIntSource);
		fIntSource->release();
		fIntSource = nullptr;
	}
	if (fWorkLoop) {
		fWorkLoop->release();
		fWorkLoop = nullptr;
	}
}

/* ------------------------------------------------------------------------ */
/* Punto de entrada de la fase                                               */
/* ------------------------------------------------------------------------ */

bool RTL8852BT::setupDma()
{
	if (!allocRings())
		return false;

	programRings();

	if (!setupInterrupt()) {
		freeRings();
		return false;
	}

	enableInterrupts();
	RTLOG("FASE 2b lista: anillos reservados, chip informado, interrupcion activa.");
	return true;
}

void RTL8852BT::teardownDma()
{
	disableInterrupts();
	teardownInterrupt();
	freeRings();
	RTLOG("interrupciones recibidas en total: %llu", (unsigned long long)fIrqCount);
}
