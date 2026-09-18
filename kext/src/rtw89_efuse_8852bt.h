/* SPDX-License-Identifier: BSD-3-Clause
 *
 * rtw89_efuse_8852bt.h - mapa logico de la efuse del RTL8852BT
 *
 * La efuse es una memoria de un solo uso grabada en fabrica. De ella salen la
 * direccion MAC de la tarjeta y las constantes de calibracion de la radio.
 *
 * El struct reproduce literalmente rtw8852bx_efuse de
 * reference/rtw89-linux/rtw8852b_common.h. Los static_assert comprueban en
 * tiempo de compilacion que el compilador lo empaqueta en los mismos offsets
 * que calculamos a mano; si Apple cambiara el empaquetado de campos de bits,
 * la compilacion falla en vez de leer basura silenciosamente.
 */
#ifndef RTW89_EFUSE_8852BT_H
#define RTW89_EFUSE_8852BT_H

#include "rtw89_compat.h"

/* core.h: tamanos de grupo de canales para TSSI */
#define TSSI_CCK_CH_GROUP_NUM     6
#define TSSI_MCS_2G_CH_GROUP_NUM  5
#define TSSI_MCS_5G_CH_GROUP_NUM 14

/* Parametros de la efuse del 8852BT (rtw8852bt.c:881-889) */
#define RTW8852BT_SEC_CTRL_EFUSE_SIZE   4
#define RTW8852BT_PHYSICAL_EFUSE_SIZE   1216
#define RTW8852BT_LOGICAL_EFUSE_SIZE    2048
#define RTW8852BT_LIMIT_EFUSE_SIZE      1280
/* El banco DAV (96/16 bytes) necesita otra via de acceso y no contiene la MAC,
 * asi que de momento no se lee. Solo afecta a calibraciones finas. */

#define ETH_ALEN 6

struct rtw8852bx_tssi_offset {
	u8 cck_tssi[TSSI_CCK_CH_GROUP_NUM];
	u8 bw40_tssi[TSSI_MCS_2G_CH_GROUP_NUM];
	u8 rsvd[7];
	u8 bw40_1s_tssi_5g[TSSI_MCS_5G_CH_GROUP_NUM];
} __packed;

/* Variante PCIe de la union final. La USB tiene otro layout, no aplica aqui. */
struct rtw8852bx_e_efuse {
	u8 mac_addr[ETH_ALEN];
} __packed;

struct rtw8852bx_efuse {
	u8 rsvd[0x210];
	struct rtw8852bx_tssi_offset path_a_tssi;
	u8 rsvd1[10];
	struct rtw8852bx_tssi_offset path_b_tssi;
	u8 rsvd2[94];
	u8 channel_plan;
	u8 xtal_k;
	u8 rsvd3;
	u8 iqk_lck;
	u8 rsvd4[5];
	u8 reg_setting:2;
	u8 tx_diversity:1;
	u8 rx_diversity:2;
	u8 ac_mode:1;
	u8 module_type:2;
	u8 rsvd5;
	u8 shared_ant:1;
	u8 coex_type:3;
	u8 ant_iso:1;
	u8 radio_on_off:1;
	u8 rsvd6:2;
	u8 eeprom_version;
	u8 customer_id;
	u8 tx_bb_swing_2g;
	u8 tx_bb_swing_5g;
	u8 tx_cali_pwr_trk_mode;
	u8 trx_path_selection;
	u8 rfe_type;
	u8 country_code[2];
	u8 rsvd7[3];
	u8 path_a_therm;
	u8 path_b_therm;
	u8 rsvd8[2];
	u8 rx_gain_2g_ofdm;
	u8 rsvd9;
	u8 rx_gain_2g_cck;
	u8 rsvd10;
	u8 rx_gain_5g_low;
	u8 rsvd11;
	u8 rx_gain_5g_mid;
	u8 rsvd12;
	u8 rx_gain_5g_high;
	u8 rsvd13[35];
	u8 path_a_cck_pwr_idx[6];
	u8 path_a_bw40_1tx_pwr_idx[5];
	u8 path_a_ofdm_1tx_pwr_idx_diff:4;
	u8 path_a_bw20_1tx_pwr_idx_diff:4;
	u8 path_a_bw20_2tx_pwr_idx_diff:4;
	u8 path_a_bw40_2tx_pwr_idx_diff:4;
	u8 path_a_cck_2tx_pwr_idx_diff:4;
	u8 path_a_ofdm_2tx_pwr_idx_diff:4;
	u8 rsvd14[0xf2];
	struct rtw8852bx_e_efuse e;
} __packed;

/* Offsets calculados a mano y verificados aqui por el compilador. */
static_assert(sizeof(struct rtw8852bx_tssi_offset) == 32,
              "rtw8852bx_tssi_offset deberia ocupar 32 bytes");
static_assert(__builtin_offsetof(struct rtw8852bx_efuse, path_a_tssi) == 0x210,
              "path_a_tssi deberia estar en 0x210");
static_assert(__builtin_offsetof(struct rtw8852bx_efuse, xtal_k) == 0x2B9,
              "xtal_k deberia estar en 0x2B9");
static_assert(__builtin_offsetof(struct rtw8852bx_efuse, rfe_type) == 0x2CA,
              "rfe_type deberia estar en 0x2CA");
static_assert(__builtin_offsetof(struct rtw8852bx_efuse, country_code) == 0x2CB,
              "country_code deberia estar en 0x2CB");
static_assert(__builtin_offsetof(struct rtw8852bx_efuse, e.mac_addr) == 0x400,
              "la MAC deberia estar en 0x400 del mapa logico");
static_assert(sizeof(struct rtw8852bx_efuse) <= RTW8852BT_LOGICAL_EFUSE_SIZE,
              "el mapa no cabe en logical_efuse_size");

#endif /* RTW89_EFUSE_8852BT_H */
