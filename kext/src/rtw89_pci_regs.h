/* SPDX-License-Identifier: BSD-3-Clause
 *
 * rtw89_pci_regs.h - registros del bloque PCI del RTL8852BT
 *
 * Copiados literalmente de reference/rtw89-linux/pci.h. Ojo: estos NO estan
 * en reg.h como el resto; viven en pci.h, por eso tools/verify_regs.py tuvo
 * que aprender a mirar tambien ahi.
 *
 * Solo se incluyen los canales que el 8852BT usa de verdad. Su pci_info
 * (rtw8852bte.c:60) enmascara ACH4..ACH7, CH10 y CH11, asi que quedan siete:
 * ACH0..ACH3 (colas de acceso por prioridad), CH8 (gestion), CH9 (alta
 * prioridad) y CH12 (comandos al firmware). Mas dos de recepcion: RXQ y RPQ.
 */
#ifndef RTW89_PCI_REGS_H
#define RTW89_PCI_REGS_H

#include "rtw89_compat.h"

/* ---- Numero de descriptores por anillo (16 bits) ------------------------- */
#define R_AX_RXQ_RXBD_NUM        0x1020
#define R_AX_RPQ_RXBD_NUM        0x1022
#define R_AX_ACH0_TXBD_NUM       0x1024
#define R_AX_ACH1_TXBD_NUM       0x1026
#define R_AX_ACH2_TXBD_NUM       0x1028
#define R_AX_ACH3_TXBD_NUM       0x102A
#define R_AX_CH8_TXBD_NUM        0x1034
#define R_AX_CH9_TXBD_NUM        0x1036
#define R_AX_CH12_TXBD_NUM       0x1038

/* ---- Indices de lectura/escritura --------------------------------------- */
#define R_AX_RXQ_RXBD_IDX        0x1050
#define R_AX_RPQ_RXBD_IDX        0x1054
#define R_AX_ACH0_TXBD_IDX       0x1058
#define R_AX_ACH1_TXBD_IDX       0x105C
#define R_AX_ACH2_TXBD_IDX       0x1060
#define R_AX_ACH3_TXBD_IDX       0x1064
#define R_AX_CH8_TXBD_IDX        0x1078   /* cola de gestion, banda 0 */
#define R_AX_CH9_TXBD_IDX        0x107C   /* cola de alta prioridad, banda 0 */
#define R_AX_CH12_TXBD_IDX       0x1080   /* cola de comandos al firmware */

#define TXBD_HW_IDX_MASK         GENMASK(27, 16)
#define TXBD_HOST_IDX_MASK       GENMASK(11, 0)

/* ---- Direccion fisica base de cada anillo (L = bajo, H = alto) ---------- */
#define R_AX_RXQ_RXBD_DESA_L     0x1100
#define R_AX_RXQ_RXBD_DESA_H     0x1104
#define R_AX_RPQ_RXBD_DESA_L     0x1108
#define R_AX_RPQ_RXBD_DESA_H     0x110C
#define R_AX_ACH0_TXBD_DESA_L    0x1110
#define R_AX_ACH0_TXBD_DESA_H    0x1114
#define R_AX_ACH1_TXBD_DESA_L    0x1118
#define R_AX_ACH1_TXBD_DESA_H    0x111C
#define R_AX_ACH2_TXBD_DESA_L    0x1120
#define R_AX_ACH2_TXBD_DESA_H    0x1124
#define R_AX_ACH3_TXBD_DESA_L    0x1128
#define R_AX_ACH3_TXBD_DESA_H    0x112C
#define R_AX_CH8_TXBD_DESA_L     0x1150
#define R_AX_CH8_TXBD_DESA_H     0x1154
#define R_AX_CH9_TXBD_DESA_L     0x1158
#define R_AX_CH9_TXBD_DESA_H     0x115C
#define R_AX_CH12_TXBD_DESA_L    0x1160
#define R_AX_CH12_TXBD_DESA_H    0x1164

/* ---- Reparto de la RAM interna de descriptores -------------------------- */
#define R_AX_ACH0_BDRAM_CTRL     0x1200
#define R_AX_ACH1_BDRAM_CTRL     0x1204
#define R_AX_ACH2_BDRAM_CTRL     0x1208
#define R_AX_ACH3_BDRAM_CTRL     0x120C
#define R_AX_CH8_BDRAM_CTRL      0x1220
#define R_AX_CH9_BDRAM_CTRL      0x1224
#define R_AX_CH12_BDRAM_CTRL     0x1228
#define BDRAM_SIDX_MASK          GENMASK(7, 0)
#define BDRAM_MAX_MASK           GENMASK(15, 8)
#define BDRAM_MIN_MASK           GENMASK(23, 16)

/* ---- Interrupciones ------------------------------------------------------ */
/* Dos niveles: el bloque PCIe tiene los suyos (HIMR00/HIMR10) y el sistema
 * uno de nivel superior (HIMR0) para los avisos del firmware. */
#define R_AX_HIMR0               0x01A0
#define R_AX_HISR0               0x01A4
#define R_AX_HIMR1               0x01A8
#define R_AX_HISR1               0x01AC
#define R_AX_PCIE_HIMR00         0x10B0
#define R_AX_PCIE_HISR00         0x10B4
#define R_AX_PCIE_HIMR10         0x13B0
#define R_AX_PCIE_HISR10         0x13B4
#define B_AX_HS0ISR_IND_INT_EN   BIT(24)
#define B_AX_HCI_AXIDMA_INT_EN   BIT(29)

/* ---- Parada y estado del DMA --------------------------------------------
 * Estos cinco los escribi de memoria en vez de copiarlos y los cinco estaban
 * mal. Los cazo tools/verify_regs.py. Valores correctos de pci.h:673-750.
 */
#define R_AX_PCIE_DMA_STOP1      0x1010
#define B_AX_STOP_PCIEIO         BIT(20)
#define R_AX_PCIE_DMA_BUSY1      0x101C
#define R_AX_RXBD_RWPTR_CLR      0x1018
#define R_AX_PCIE_INIT_CFG1      0x1000
#define B_AX_TXHCI_EN            BIT(11)
#define B_AX_RXHCI_EN            BIT(13)
#define B_AX_RXBD_MODE           BIT(18)

#endif /* RTW89_PCI_REGS_H */
