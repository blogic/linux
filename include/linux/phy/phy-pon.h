/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#ifndef __PHY_PON_H_
#define __PHY_PON_H_

#include <linux/types.h>

/**
 * DOC: PON mode
 *
 * The submode of phy_set_mode_ext() for PHY_MODE_PON is the PON technology
 * the MAC runs, a value of enum pon_mode of <uapi/linux/pon.h>, for
 * example PON_MODE_XGS_PON. A provider refuses a technology it does not
 * run with -EOPNOTSUPP.
 */

#define PHY_PON_BURST_PATTERN_LEN	8

/**
 * enum phy_pon_fec - downstream FEC decoding
 * @PHY_PON_FEC_AUTO: follow what the downstream frame header says
 * @PHY_PON_FEC_OFF: never decode
 * @PHY_PON_FEC_ON: always decode
 */
enum phy_pon_fec {
	PHY_PON_FEC_AUTO,
	PHY_PON_FEC_OFF,
	PHY_PON_FEC_ON,
};

/**
 * struct phy_pon_burst_profile - one upstream burst profile the OLT assigned
 * @index: which of the profiles this one is, 0 to 3
 * @fec: upstream FEC is on for bursts sent with this profile, 0 or 1
 * @delimiter_len: significant bytes of @delimiter, 0 to
 *		   PHY_PON_BURST_PATTERN_LEN
 * @delimiter: the burst delimiter pattern
 * @preamble_len: significant bytes of @preamble, 1 to
 *		  PHY_PON_BURST_PATTERN_LEN
 * @preamble_repeat: how many times the preamble repeats
 * @preamble: the burst preamble pattern
 *
 * The four profiles are those of ITU-T G.9807.1 Table C.11.4. A provider
 * refuses an index or a length outside these ranges, or one its hardware
 * cannot hold, before it changes anything.
 */
struct phy_pon_burst_profile {
	u8 index;
	u8 fec;
	u8 delimiter_len;
	u8 delimiter[PHY_PON_BURST_PATTERN_LEN];
	u8 preamble_len;
	u8 preamble_repeat;
	u8 preamble[PHY_PON_BURST_PATTERN_LEN];
};

/**
 * struct phy_configure_opts_pon - PON configuration set
 * @burst_profile: the upstream burst profile to take
 * @ds_fec: how the downstream FEC is decoded
 * @tx_enable: the transmitter is on
 * @rx_enable: the receiver is on
 * @set_burst_profile: apply @burst_profile
 * @set_ds_fec: apply @ds_fec
 * @set_tx_enable: apply @tx_enable
 * @set_rx_enable: apply @rx_enable
 *
 * This structure is used to represent the configuration state of a PON
 * phy. Only the members whose set_ flag is 1 are applied. A member that
 * fails is not applied. With several flags set, the members applied
 * before it stay and the provider decides about the ones after it. A
 * caller that must know which member failed sets one flag per call, as
 * the helpers of <net/pon/phy.h> do.
 */
struct phy_configure_opts_pon {
	struct phy_pon_burst_profile burst_profile;
	enum phy_pon_fec ds_fec;
	u8 tx_enable : 1;
	u8 rx_enable : 1;
	u8 set_burst_profile : 1;
	u8 set_ds_fec : 1;
	u8 set_tx_enable : 1;
	u8 set_rx_enable : 1;
};

#endif /* __PHY_PON_H_ */
