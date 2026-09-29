/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#ifndef __NET_PON_PHY_H
#define __NET_PON_PHY_H

#include <linux/phy/phy.h>
#include <linux/types.h>
#include <uapi/linux/pon.h>

/**
 * pon_phy_mode_set() - select the line rate
 * @phy: the generic PHY
 * @mode: the PON mode, enum pon_mode
 *
 * Return: 0, or a negative errno when the PHY cannot run that mode.
 */
static inline int pon_phy_mode_set(struct phy *phy, enum pon_mode mode)
{
	return phy_set_mode_ext(phy, PHY_MODE_PON, mode);
}

/**
 * pon_phy_tx_enable() - turn the transmitter on or off
 * @phy: the generic PHY
 * @on: transmit when true
 *
 * Return: 0, or a negative errno.
 */
static inline int pon_phy_tx_enable(struct phy *phy, bool on)
{
	union phy_configure_opts opts = {
		.pon = {
			.tx_enable = on,
			.set_tx_enable = 1,
		},
	};

	return phy_configure(phy, &opts);
}

/**
 * pon_phy_rx_enable() - turn the receiver on or off
 * @phy: the generic PHY
 * @on: receive when true
 *
 * Return: 0, or a negative errno.
 */
static inline int pon_phy_rx_enable(struct phy *phy, bool on)
{
	union phy_configure_opts opts = {
		.pon = {
			.rx_enable = on,
			.set_rx_enable = 1,
		},
	};

	return phy_configure(phy, &opts);
}

/**
 * pon_phy_burst_profile_set() - program one upstream burst profile
 * @phy: the generic PHY
 * @profile: the profile the OLT assigned
 *
 * Return: 0, or a negative errno.
 */
static inline int
pon_phy_burst_profile_set(struct phy *phy,
			  const struct phy_pon_burst_profile *profile)
{
	union phy_configure_opts opts = {
		.pon = {
			.burst_profile = *profile,
			.set_burst_profile = 1,
		},
	};

	return phy_configure(phy, &opts);
}

/**
 * pon_phy_ds_fec_set() - select downstream FEC decoding
 * @phy: the generic PHY
 * @mode: enum phy_pon_fec
 *
 * Return: 0, or a negative errno.
 */
static inline int pon_phy_ds_fec_set(struct phy *phy, enum phy_pon_fec mode)
{
	union phy_configure_opts opts = {
		.pon = {
			.ds_fec = mode,
			.set_ds_fec = 1,
		},
	};

	return phy_configure(phy, &opts);
}

#endif /* __NET_PON_PHY_H */
