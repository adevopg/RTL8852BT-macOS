/* SPDX-License-Identifier: BSD-3-Clause
 *
 * rtw89_pci_desc.h - descriptores DMA y tabla de anillos del RTL8852BT
 *
 * Los descriptores son estructuras que viven en memoria compartida entre el
 * host y el chip. El chip las lee y escribe por DMA, asi que su tamano y
 * disposicion tienen que ser EXACTOS. De ahi los static_assert.
 *
 * Copiados de reference/rtw89-linux/pci.h.
 */
#ifndef RTW89_PCI_DESC_H
#define RTW89_PCI_DESC_H

#include "rtw89_compat.h"
#include "rtw89_pci_regs.h"

/* pci.h:1122-1128 */
#define RTW89_PCI_TXBD_NUM_MAX    256
#define RTW89_PCI_RXBD_NUM_MAX    256
#define RTW89_PCI_TXWD_NUM_MAX    512
#define RTW89_PCI_TXWD_PAGE_SIZE  128
#define RTW89_PCI_RX_BUF_SIZE     (11454 + 40 + 4)

/* Descriptor de transmision: apunta a un buffer y dice cuanto mide. */
struct rtw89_pci_tx_bd_32 {
	__le16 length;
	__le16 opt;
	__le32 dma;
} __packed;
#define RTW89_PCI_TXBD_OPT_LS      BIT(14)
#define RTW89_PCI_TXBD_OPT_DMA_HI  GENMASK(13, 6)

/* Descriptor de recepcion: apunta a un buffer vacio que el chip rellenara. */
struct rtw89_pci_rx_bd_32 {
	__le16 buf_size;
	__le16 opt;
	__le32 dma;
} __packed;
#define RTW89_PCI_RXBD_OPT_DMA_HI  GENMASK(13, 6)
#define RTW89_PCI_RXBD_FS          BIT(15)
#define RTW89_PCI_RXBD_LS          BIT(14)
#define RTW89_PCI_RXBD_WRITE_SIZE  GENMASK(13, 0)
#define RTW89_PCI_RXBD_TAG         GENMASK(28, 16)

static_assert(sizeof(struct rtw89_pci_tx_bd_32) == 8, "el descriptor TX debe medir 8 bytes");
static_assert(sizeof(struct rtw89_pci_rx_bd_32) == 8, "el descriptor RX debe medir 8 bytes");

/* Canales, de txrx.h:769. El 8852BT solo usa los que aparecen abajo. */
enum rtw89_tx_channel {
	RTW89_TXCH_ACH0 = 0, RTW89_TXCH_ACH1 = 1, RTW89_TXCH_ACH2 = 2, RTW89_TXCH_ACH3 = 3,
	RTW89_TXCH_ACH4 = 4, RTW89_TXCH_ACH5 = 5, RTW89_TXCH_ACH6 = 6, RTW89_TXCH_ACH7 = 7,
	RTW89_TXCH_CH8  = 8,   /* gestion, banda 0 */
	RTW89_TXCH_CH9  = 9,   /* alta prioridad, banda 0 */
	RTW89_TXCH_CH10 = 10, RTW89_TXCH_CH11 = 11,
	RTW89_TXCH_CH12 = 12,  /* comandos al firmware (H2C) */
	RTW89_TXCH_NUM
};

enum rtw89_rx_channel {
	RTW89_RXCH_RXQ = 0,   /* paquetes recibidos */
	RTW89_RXCH_RPQ = 1,   /* informes de transmision */
	RTW89_RXCH_NUM
};

/* Direcciones de registro de un anillo (pci.h:1323) */
struct rtw89_pci_ch_addr {
	u32 num;      /* cuantos descriptores tiene (16 bits) */
	u32 idx;      /* indices de lectura y escritura */
	u32 bdram;    /* reparto de la RAM interna; 0 en los anillos RX */
	u32 desa_l;   /* direccion fisica base, 32 bits bajos */
	u32 desa_h;   /* 32 bits altos */
};

/* Reparto de la RAM interna de descriptores (pci.c:1711,
 * rtw89_bd_ram_table_single). Confirma los 7 canales activos. */
struct rtw89_pci_bd_ram {
	u8 start_idx;
	u8 max_num;
	u8 min_num;
};

/* Tabla de los canales que el 8852BT usa de verdad, con sus registros.
 * ACH4..7, CH10 y CH11 quedan fuera por tx_dma_ch_mask (rtw8852bte.c:60). */
struct rtw89_pci_txch_def {
	u8 ch;
	const char *name;
	struct rtw89_pci_ch_addr addr;
	struct rtw89_pci_bd_ram bd_ram;
};

static const struct rtw89_pci_txch_def RTW8852BT_TXCH[] = {
	{ RTW89_TXCH_ACH0, "ACH0",
	  { R_AX_ACH0_TXBD_NUM, R_AX_ACH0_TXBD_IDX, R_AX_ACH0_BDRAM_CTRL,
	    R_AX_ACH0_TXBD_DESA_L, R_AX_ACH0_TXBD_DESA_H }, { 0,  5, 2 } },
	{ RTW89_TXCH_ACH1, "ACH1",
	  { R_AX_ACH1_TXBD_NUM, R_AX_ACH1_TXBD_IDX, R_AX_ACH1_BDRAM_CTRL,
	    R_AX_ACH1_TXBD_DESA_L, R_AX_ACH1_TXBD_DESA_H }, { 5,  5, 2 } },
	{ RTW89_TXCH_ACH2, "ACH2",
	  { R_AX_ACH2_TXBD_NUM, R_AX_ACH2_TXBD_IDX, R_AX_ACH2_BDRAM_CTRL,
	    R_AX_ACH2_TXBD_DESA_L, R_AX_ACH2_TXBD_DESA_H }, { 10, 5, 2 } },
	{ RTW89_TXCH_ACH3, "ACH3",
	  { R_AX_ACH3_TXBD_NUM, R_AX_ACH3_TXBD_IDX, R_AX_ACH3_BDRAM_CTRL,
	    R_AX_ACH3_TXBD_DESA_L, R_AX_ACH3_TXBD_DESA_H }, { 15, 5, 2 } },
	{ RTW89_TXCH_CH8,  "CH8 (gestion)",
	  { R_AX_CH8_TXBD_NUM,  R_AX_CH8_TXBD_IDX,  R_AX_CH8_BDRAM_CTRL,
	    R_AX_CH8_TXBD_DESA_L,  R_AX_CH8_TXBD_DESA_H }, { 20, 4, 1 } },
	{ RTW89_TXCH_CH9,  "CH9 (alta prio)",
	  { R_AX_CH9_TXBD_NUM,  R_AX_CH9_TXBD_IDX,  R_AX_CH9_BDRAM_CTRL,
	    R_AX_CH9_TXBD_DESA_L,  R_AX_CH9_TXBD_DESA_H }, { 24, 4, 1 } },
	{ RTW89_TXCH_CH12, "CH12 (firmware)",
	  { R_AX_CH12_TXBD_NUM, R_AX_CH12_TXBD_IDX, R_AX_CH12_BDRAM_CTRL,
	    R_AX_CH12_TXBD_DESA_L, R_AX_CH12_TXBD_DESA_H }, { 28, 4, 1 } },
};
#define RTW8852BT_TXCH_COUNT (sizeof(RTW8852BT_TXCH) / sizeof(RTW8852BT_TXCH[0]))

struct rtw89_pci_rxch_def {
	u8 ch;
	const char *name;
	struct rtw89_pci_ch_addr addr;
};

static const struct rtw89_pci_rxch_def RTW8852BT_RXCH[] = {
	{ RTW89_RXCH_RXQ, "RXQ (paquetes)",
	  { R_AX_RXQ_RXBD_NUM, R_AX_RXQ_RXBD_IDX, 0,
	    R_AX_RXQ_RXBD_DESA_L, R_AX_RXQ_RXBD_DESA_H } },
	{ RTW89_RXCH_RPQ, "RPQ (informes TX)",
	  { R_AX_RPQ_RXBD_NUM, R_AX_RPQ_RXBD_IDX, 0,
	    R_AX_RPQ_RXBD_DESA_L, R_AX_RPQ_RXBD_DESA_H } },
};
#define RTW8852BT_RXCH_COUNT (sizeof(RTW8852BT_RXCH) / sizeof(RTW8852BT_RXCH[0]))

/* El ultimo canal TX empieza en el indice 28 y usa 4 entradas: la RAM interna
 * de descriptores debe tener al menos 32. Si alguien anade canales sin ampliar
 * la tabla, esto lo detecta. */
static_assert(RTW8852BT_TXCH_COUNT == 7, "el 8852BT usa exactamente 7 canales TX");
static_assert(RTW8852BT_RXCH_COUNT == 2, "el 8852BT usa exactamente 2 canales RX");

#endif /* RTW89_PCI_DESC_H */
