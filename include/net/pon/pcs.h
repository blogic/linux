/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#ifndef __NET_PON_PCS_H
#define __NET_PON_PCS_H

#include <linux/rcupdate.h>
#include <linux/types.h>

struct device;
struct phy;
struct pon_dev;
struct pon_fec_stats;
struct pon_pcs;

/**
 * struct pon_pcs_state - the downstream state of a PON PCS
 * @signal: the receiver sees light
 * @sync: the downstream synchronization state machine is in the Sync or
 *	  Re-Sync state of ITU-T G.9807.1 clause C.10.1.1.3, where LODS is not
 *	  declared (Table C.14.3), false without @signal
 */
struct pon_pcs_state {
	bool signal;
	bool sync;
};

/**
 * struct pon_pcs_ops - what the PON core reads from a PON PCS
 * @pcs_get_state: read the downstream state. The level it reads is the
 *		   truth. A report of pon_pcs_change() is a hint to read it.
 *		   Return 0, or a negative errno, which the core takes as no
 *		   sync
 * @pcs_get_fec_stats: read the downstream FEC counters, which follow the
 *		       rules of struct pon_fec_stats: they go back only when
 *		       they wrap. A PCS whose hardware clears its counters, on
 *		       a reset, a retrain or a loss of signal, adds what they
 *		       held to a running total first. Return 0, or a negative
 *		       errno
 *
 * Every member is mandatory. The core calls both in process context with
 * the instance lock held: from the instance's context, from a netlink
 * handler, from ethtool with rtnl held too, or from a driver callback that
 * reads the level through pon_dev_pcs_sync(). Both may sleep and neither
 * may take rtnl.
 *
 * The provider reports a change of the state with pon_pcs_change(). The
 * rules of the report:
 *
 * - It is legal from any context, hard interrupt included. It only sets the
 *   loss latch and queues the work of the core, so it never waits for the
 *   instance lock or for rtnl. That makes it legal with the locks of the
 *   provider held, also from inside a phy_reset() or another call of the
 *   generic PHY that the consumer made with the instance lock held.
 * - The consumer calls the generic PHY with the instance lock held, also
 *   phy_power_off(), which may wait for the work of the provider. The core
 *   calls these ops with the same lock held. So the provider must not wait
 *   for anything that needs the instance lock or rtnl, neither in its work
 *   nor while it holds a lock that its ops take.
 * - Reports are hints. A repeated report, a report without a change and a
 *   report from inside a call of the consumer are harmless. No report comes
 *   at attach.
 * - A loss is latched. The core reports the link down once for it, even
 *   when the level is back by the time it reads it, then up again.
 * - After phy_power_on() the provider reports the state once: up when the
 *   receiver has sync, down when it sees no light. Light without sync gives
 *   no report until sync is found. After a reset of a running PHY the
 *   provider reports a loss.
 */
struct pon_pcs_ops {
	int (*pcs_get_state)(struct pon_pcs *pcs, struct pon_pcs_state *state);
	int (*pcs_get_fec_stats)(struct pon_pcs *pcs,
				 struct pon_fec_stats *stats);
};

/**
 * struct pon_pcs - the PCS of a PON PHY, which the PON core consumes
 * @ops: what the core reads, set by the provider before pon_pcs_register()
 * @phy: the generic PHY the PCS belongs to, set by pon_pcs_register()
 * @pdev: the PON device the PCS is attached to, NULL while it is attached
 *	  to none, read under RCU by pon_pcs_change()
 *
 * The provider embeds it in its own state, which lives at least as long as
 * the generic PHY.
 */
struct pon_pcs {
	const struct pon_pcs_ops *ops;
	struct phy *phy;
	struct pon_dev __rcu *pdev;
};

int pon_pcs_register(struct pon_pcs *pcs, struct phy *phy);
struct pon_pcs *pon_pcs_lookup(struct device *consumer, struct phy *phy);
void pon_pcs_change(struct pon_pcs *pcs, bool up);

int pon_dev_pcs_attach(struct pon_dev *pdev, struct pon_pcs *pcs);
void pon_dev_pcs_detach(struct pon_dev *pdev);
bool pon_dev_pcs_sync(struct pon_dev *pdev);

#endif /* __NET_PON_PCS_H */
