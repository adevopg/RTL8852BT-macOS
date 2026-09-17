/* SPDX-License-Identifier: BSD-3-Clause
 *
 * rtw89_regs.h - mapa de registros del RTL8852BT (subconjunto de fases 1-2)
 *
 * Valores copiados literalmente de reference/rtw89-linux/reg.h y mac.h
 * (Realtek, dual BSD/GPL). Solo se incluyen los registros que usa la
 * secuencia de encendido rtw8852bt_pwr_on_func / pwr_off_func y el acceso
 * al bus serie del cristal (xtal_si).
 *
 * Cada define lleva el nombre exacto del driver Linux para poder diferenciar
 * contra el original sin traducir nombres.
 */
#ifndef RTW89_REGS_H
#define RTW89_REGS_H

#include "rtw89_compat.h"

/* ---- Bloque de sistema (0x0000 - 0x03FF) --------------------------------- */
#define R_AX_SYS_ISO_CTRL                 0x0000
#define B_AX_PWC_EV2EF_B14                BIT(14)
#define B_AX_PWC_EV2EF_B15                BIT(15)
#define B_AX_ISO_EB2CORE                  BIT(8)

#define R_AX_SYS_FUNC_EN                  0x0002
#define B_AX_FEN_BB_GLB_RSTN              BIT(1)
#define B_AX_FEN_BBRSTB                   BIT(0)

#define R_AX_SYS_PW_CTRL                  0x0004
#define B_AX_DIS_WLBT_PDNSUSEN_SOPC       BIT(18)
#define B_AX_RDY_SYSPWR                   BIT(17)
#define B_AX_EN_WLON                      BIT(16)
#define B_AX_APDM_HPDN                    BIT(15)
#define B_AX_AFSM_PCIE_SUS_EN             BIT(12)
#define B_AX_AFSM_WLSUS_EN                BIT(11)
#define B_AX_APFM_SWLPS                   BIT(10)
#define B_AX_APFM_OFFMAC                  BIT(9)
#define B_AX_APFN_ONMAC                   BIT(8)

#define R_AX_SYS_SWR_CTRL1                0x0010
#define B_AX_SYM_CTRL_SPS_PWMFREQ         BIT(10)

#define R_AX_SYS_ADIE_PAD_PWR_CTRL        0x0018
#define B_AX_SYM_PADPDN_WL_PTA_1P3        BIT(6)
#define B_AX_SYM_PADPDN_WL_RFC_1P3        BIT(5)

#define R_AX_SYS_SDIO_CTRL                0x0070
#define B_AX_PCIE_CALIB_EN_V1             BIT(12)

#define R_AX_HCI_OPT_CTRL                 0x0074

#define R_AX_PLATFORM_ENABLE              0x0088
#define B_AX_PLATFORM_EN                  BIT(0)

#define R_AX_WLLPS_CTRL                   0x0090
#define B_AX_DIS_WLBT_LPSEN_LOPC          BIT(1)
#define SW_LPS_OPTION                     0x0001A0B2

#define R_AX_PMC_DBG_CTRL2                0x00CC
#define B_AX_SYSON_DIS_PMCR_AX_WRMSK      BIT(2)

#define R_AX_SYS_CFG1                     0x00F0
#define B_AX_CHIP_VER_MASK                GENMASK(15, 12)

#define R_AX_SYS_STATUS1                  0x00F4

#define R_AX_UDM0                         0x01F0
#define R_AX_UDM1                         0x01F4

#define R_AX_SPS_DIG_ON_CTRL0             0x0200
#define B_AX_VREFPFM_L_MASK               GENMASK(25, 22)
#define B_AX_REG_ZCDC_H_MASK              GENMASK(18, 17)
#define B_AX_OCP_L1_MASK                  GENMASK(15, 13)
#define B_AX_VOL_L1_MASK                  GENMASK(3, 0)

#define R_AX_LDO_AON_CTRL0                0x0218
#define B_AX_PD_REGU_L                    BIT(16)

/* Bus serie del cristal: escritura/lectura indirecta de registros analogicos */
#define R_AX_WLAN_XTAL_SI_CTRL            0x0270
#define B_AX_WL_XTAL_SI_CMD_POLL          BIT(31)
#define B_AX_WL_XTAL_SI_MODE_MASK         GENMASK(25, 24)
#define XTAL_SI_NORMAL_WRITE              0x00
#define XTAL_SI_NORMAL_READ               0x01
#define B_AX_WL_XTAL_SI_BITMASK_MASK      GENMASK(23, 16)
#define B_AX_WL_XTAL_SI_DATA_MASK         GENMASK(15, 8)
#define B_AX_WL_XTAL_SI_ADDR_MASK         GENMASK(7, 0)

#define R_AX_EECS_EESK_FUNC_SEL           0x02D8
#define B_AX_PINMUX_EESK_FUNC_SEL_MASK    GENMASK(7, 4)

#define R_AX_WLRF_CTRL                    0x02F0
#define B_AX_AFC_AFEDIG                   BIT(17)

#define R_AX_IC_PWR_STATE                 0x03F0
#define B_AX_WLMAC_PWR_STE_MASK           GENMASK(9, 8)
#define MAC_AX_MAC_ON                     1

/* ---- Offsets dentro del bus xtal_si (enum rtw89_mac_xtal_si_offset) ------ */
#define XTAL_SI_XTAL_XMD_2                0x24
#define XTAL_SI_LDO_LPS                   GENMASK(6, 4)
#define XTAL_SI_XTAL_XMD_4                0x26
#define XTAL_SI_LPS_CAP                   GENMASK(3, 0)
#define XTAL_SI_CV                        0x41
#define XTAL_SI_ACV_MASK                  GENMASK(3, 0)
#define XTAL_SI_ANAPAR_WL                 0x90
#define XTAL_SI_SRAM2RFC                  BIT(7)
#define XTAL_SI_GND_SHDN_WL               BIT(6)
#define XTAL_SI_SHDN_WL                   BIT(5)
#define XTAL_SI_RFC2RF                    BIT(4)
#define XTAL_SI_OFF_EI                    BIT(3)
#define XTAL_SI_OFF_WEI                   BIT(2)
#define XTAL_SI_PON_EI                    BIT(1)
#define XTAL_SI_PON_WEI                   BIT(0)
#define XTAL_SI_SRAM_CTRL                 0xA1
#define XTAL_SI_SRAM_DIS                  BIT(1)

/* ---- Habilitacion de funciones DMAC / CMAC ------------------------------- */
#define R_AX_DMAC_FUNC_EN                 0x8400
#define B_AX_MAC_FUNC_EN                  BIT(30)
#define B_AX_DMAC_FUNC_EN                 BIT(29)
#define B_AX_MPDU_PROC_EN                 BIT(28)
#define B_AX_WD_RLS_EN                    BIT(27)
#define B_AX_DLE_WDE_EN                   BIT(26)
#define B_AX_TXPKT_CTRL_EN                BIT(25)
#define B_AX_STA_SCH_EN                   BIT(24)
#define B_AX_DLE_PLE_EN                   BIT(23)
#define B_AX_PKT_BUF_EN                   BIT(22)
#define B_AX_DMAC_TBL_EN                  BIT(21)
#define B_AX_PKT_IN_EN                    BIT(20)
#define B_AX_DLE_CPUIO_EN                 BIT(19)
#define B_AX_DISPATCHER_EN                BIT(18)
#define B_AX_BBRPT_EN                     BIT(17)
#define B_AX_MAC_SEC_EN                   BIT(16)
#define B_AX_DMACREG_GCKEN                BIT(15)

#define R_AX_HCI_FUNC_EN                  0x8380

#define R_AX_CMAC_FUNC_EN                 0xC000
#define B_AX_CMAC_EN                      BIT(30)
#define B_AX_CMAC_TXEN                    BIT(29)
#define B_AX_CMAC_RXEN                    BIT(28)
#define B_AX_FORCE_CMACREG_GCKEN          BIT(15)
#define B_AX_PHYINTF_EN                   BIT(5)
#define B_AX_CMAC_DMA_EN                  BIT(4)
#define B_AX_PTCLTOP_EN                   BIT(3)
#define B_AX_SCHEDULER_EN                 BIT(2)
#define B_AX_TMAC_EN                      BIT(1)
#define B_AX_RMAC_EN                      BIT(0)

#endif /* RTW89_REGS_H */
