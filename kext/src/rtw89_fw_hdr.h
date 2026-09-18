/* SPDX-License-Identifier: BSD-3-Clause
 *
 * rtw89_fw_hdr.h - formato del fichero rtw8852bt_fw.bin
 *
 * Copiado de reference/rtw89-linux/fw.h (Realtek, dual BSD/GPL).
 * El fichero es un contenedor "MFW": una cabecera rtw89_mfw_hdr seguida de
 * fw_nr entradas rtw89_mfw_info, cada una apuntando (shift, size) a un
 * firmware individual que empieza por rtw89_fw_hdr.
 */
#ifndef RTW89_FW_HDR_H
#define RTW89_FW_HDR_H

#include "rtw89_compat.h"

/* Contenedor multi-firmware ------------------------------------------------ */
struct rtw89_mfw_info {
	u8 cv;       /* chip version (cut) a la que aplica; 0xFF = cualquiera */
	u8 type;     /* enum rtw89_fw_type: 0 = normal, 1 = WOWLAN */
	u8 mp;
	u8 rsvd;
	__le32 shift; /* offset desde el inicio del fichero */
	__le32 size;
	u8 rsvd2[4];
} __packed;

struct rtw89_mfw_hdr {
	u8 sig;	/* RTW89_MFW_SIG = 0xFF */
	u8 fw_nr;
	u8 rsvd0[2];
	struct {
		u8 major;
		u8 minor;
		u8 sub;
		u8 idx;
	} ver;
	u8 rsvd1[8];
	struct rtw89_mfw_info info[];
} __packed;

/* Valores reales de core.h (enum rtw89_fw_type). En rtw8852bt_fw.bin v0.29.122
 * aparecen NORMAL (1), WOWLAN (3), NORMAL_CE (5) y LOGFMT (255). */
enum rtw89_fw_type {
	RTW89_FW_NORMAL = 1,
	RTW89_FW_WOWLAN = 3,
	RTW89_FW_NORMAL_CE = 5,
	RTW89_FW_NORMAL_B = 14,
	RTW89_FW_WOWLAN_B = 15,
	RTW89_FW_BBMCU0 = 64,
	RTW89_FW_BBMCU1 = 65,
	RTW89_FW_LOGFMT = 255,
};

/* Estado de la descarga, en B_AX_WCPU_FWDL_STS_MASK (fw.h:10) */
enum rtw89_fwdl_check_type {
	RTW89_FWDL_INITIAL_STATE = 0,
	RTW89_FWDL_FWDL_ONGOING = 1,
	RTW89_FWDL_CHECKSUM_FAIL = 2,
	RTW89_FWDL_SECURITY_FAIL = 3,
	RTW89_FWDL_CV_NOT_MATCH = 4,
	RTW89_FWDL_RSVD0 = 5,
	RTW89_FWDL_WCPU_FWDL_RDY = 6,
	RTW89_FWDL_WCPU_FW_INIT_RDY = 7
};

/* fw.h:5358 - vueltas de 1 us esperando al chip */
#define FWDL_WAIT_CNT 400000

/* Cabecera de cada firmware individual (v0, la que usa 8852BT) ------------- */
struct rtw89_fw_hdr_section {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
} __packed;

#define FWSECTION_HDR_W0_DL_ADDR GENMASK(31, 0)
#define FWSECTION_HDR_W1_METADATA GENMASK(31, 24)
#define FWSECTION_HDR_W1_SECTIONTYPE GENMASK(27, 24)
#define FWSECTION_HDR_W1_SEC_SIZE GENMASK(23, 0)
#define FWSECTION_HDR_W1_CHECKSUM BIT(28)
#define FWSECTION_HDR_W1_REDL BIT(29)

struct rtw89_fw_hdr {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	struct rtw89_fw_hdr_section sections[];
} __packed;

#define FW_HDR_W1_MAJOR_VERSION GENMASK(7, 0)
#define FW_HDR_W1_MINOR_VERSION GENMASK(15, 8)
#define FW_HDR_W1_SUBVERSION GENMASK(23, 16)
#define FW_HDR_W1_SUBINDEX GENMASK(31, 24)
#define FW_HDR_W2_COMMITID GENMASK(31, 0)
#define FW_HDR_W3_LEN GENMASK(23, 16)
#define FW_HDR_W3_HDR_VER GENMASK(31, 24)
#define FW_HDR_W4_MONTH GENMASK(7, 0)
#define FW_HDR_W4_DATE GENMASK(15, 8)
#define FW_HDR_W4_HOUR GENMASK(23, 16)
#define FW_HDR_W4_MIN GENMASK(31, 24)
#define FW_HDR_W5_YEAR GENMASK(31, 0)
#define FW_HDR_W6_SEC_NUM GENMASK(15, 8)
#define FW_HDR_W7_PART_SIZE GENMASK(15, 0)
#define FW_HDR_W7_DYN_HDR BIT(16)
#define FW_HDR_W7_IDMEM_SHARE_MODE GENMASK(21, 18)
#define FW_HDR_W7_CMD_VERSERION GENMASK(31, 24)

/* Tamano de cada paquete de descarga de firmware por H2C en chips AX */
#define FWDL_SECTION_PER_PKT_LEN 2020

#endif /* RTW89_FW_HDR_H */
