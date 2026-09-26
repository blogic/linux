/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#ifndef __NET_PON_FUNCTIONS_H
#define __NET_PON_FUNCTIONS_H

#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <net/pon/types.h>

struct ethtool_fec_stats;
struct ethtool_fecparam;
struct pon_dev;
struct pon_dev_caps;
struct pon_dev_ops;
struct sk_buff;

#if IS_ENABLED(CONFIG_PON)

void pon_work_queue(struct pon_dev *pdev, struct pon_work *work);
void pon_work_cancel(struct pon_dev *pdev, struct pon_work *work);
void pon_delayed_work_timer(struct timer_list *timer);
void pon_delayed_work_queue(struct pon_dev *pdev,
			    struct pon_delayed_work *dwork,
			    unsigned long delay);
void pon_delayed_work_cancel(struct pon_dev *pdev,
			     struct pon_delayed_work *dwork);
void pon_delayed_work_shutdown(struct pon_delayed_work *dwork);

/**
 * pon_work_init() - prepare a work item
 * @work: the item
 * @func: the handler pon_work_queue() runs
 *
 * Call once before the item is first queued.
 */
static inline void pon_work_init(struct pon_work *work, pon_work_func_t func)
{
	INIT_LIST_HEAD(&work->entry);
	work->func = func;
}

/**
 * pon_delayed_work_init() - prepare a delayed work item
 * @dwork: the item
 * @func: the handler that runs once the delay passes
 *
 * Sets up the timer and the work item. pon_delayed_work_queue() sets the
 * instance the item belongs to.
 */
static inline void pon_delayed_work_init(struct pon_delayed_work *dwork,
					 pon_work_func_t func)
{
	timer_setup(&dwork->timer, pon_delayed_work_timer, 0);
	pon_work_init(&dwork->work, func);
}

struct pon_dev *pon_dev_create(struct net_device *netdev,
			       struct device *parent,
			       const struct pon_dev_ops *ops,
			       const struct pon_dev_caps *caps,
			       enum pon_mode mode, void *priv_ptr);
void pon_dev_unregister(struct pon_dev *pdev);
void pon_dev_put(struct pon_dev *pdev);

int pon_dev_state_report(struct pon_dev *pdev,
			 enum pon_ploam_state state);
__printf(2, 3) void pon_dev_log(struct pon_dev *pdev, const char *fmt, ...);
void pon_dev_event(struct pon_dev *pdev, const struct pon_event *ev);
void pon_dev_alarm_set(struct pon_dev *pdev, u32 alarm, bool raised);
void pon_dev_fec_stats(struct pon_dev *pdev, struct ethtool_fec_stats *stats);
int pon_dev_fec_param(struct pon_dev *pdev, struct ethtool_fecparam *fec);

int pon_conduit_register(struct net_device *conduit,
			 const struct pon_conduit_ops *ops);
void pon_conduit_unregister(struct net_device *conduit);
int pon_conduit_rx(struct net_device *conduit, struct sk_buff *skb,
		   const struct pon_rx_info *info);
int pon_conduit_xmit(struct pon_dev *pdev, struct sk_buff *skb,
		     const struct pon_tx_info *info);
int pon_conduit_addr_set(struct pon_dev *pdev, const u8 *addr);
int pon_conduit_mtu_set(struct pon_dev *pdev, const struct net_device *dev,
			unsigned int mtu);
int pon_dev_setup_tc(struct pon_dev *pdev, enum tc_setup_type type,
		     void *type_data);
int pon_netdev_info_get(struct net_device *dev, const struct pon_flow_key *key,
			struct pon_netdev_info *info);
bool pon_netdev_offload_blocked(struct net_device *dev);

/**
 * netdev_uses_pon() - whether a network device belongs to a PON MAC
 * @dev: the network device
 *
 * True for the PON data interface, for a GEM network device and for a
 * conduit that is paired with a PON MAC.
 *
 * Return: true when @dev points at a PON instance.
 */
static inline bool netdev_uses_pon(const struct net_device *dev)
{
	return rcu_access_pointer(dev->pon_dev);
}

#else /* CONFIG_PON */

/**
 * pon_conduit_register() - offer a network device's rings to a PON MAC
 * @conduit: the ethernet device
 * @ops: what the driver does for the MAC
 *
 * The stub for a kernel without CONFIG_PON.
 *
 * Return: -ENOENT, there is no PON MAC to serve.
 */
static inline int pon_conduit_register(struct net_device *conduit,
				       const struct pon_conduit_ops *ops)
{
	return -ENOENT;
}

/**
 * pon_conduit_unregister() - take a network device's rings back
 * @conduit: the ethernet device
 *
 * The stub for a kernel without CONFIG_PON. It does nothing.
 */
static inline void pon_conduit_unregister(struct net_device *conduit)
{
}

/**
 * pon_conduit_rx() - hand one received frame to the PON MAC that owns it
 * @conduit: the ethernet device the frame arrived on
 * @skb: the frame
 * @info: what the receive descriptor said about it
 *
 * The stub for a kernel without CONFIG_PON. The frame stays the conduit's.
 *
 * Return: -ENODEV.
 */
static inline int pon_conduit_rx(struct net_device *conduit,
				 struct sk_buff *skb,
				 const struct pon_rx_info *info)
{
	return -ENODEV;
}

/**
 * netdev_uses_pon() - whether a network device belongs to a PON MAC
 * @dev: the network device
 *
 * The stub for a kernel without CONFIG_PON.
 *
 * Return: false.
 */
static inline bool netdev_uses_pon(const struct net_device *dev)
{
	return false;
}

/**
 * pon_netdev_info_get() - where an offloaded flow leaving a PON device goes
 * @dev: the network device
 * @key: the flow as the upstream classifier matches it
 * @info: left untouched
 *
 * The stub for a kernel without CONFIG_PON.
 *
 * Return: -ENODEV, @dev is not a PON network device.
 */
static inline int pon_netdev_info_get(struct net_device *dev,
				      const struct pon_flow_key *key,
				      struct pon_netdev_info *info)
{
	return -ENODEV;
}

/**
 * pon_netdev_offload_blocked() - whether a device's flows must stay in software
 * @dev: the network device
 *
 * The stub for a kernel without CONFIG_PON.
 *
 * Return: false.
 */
static inline bool pon_netdev_offload_blocked(struct net_device *dev)
{
	return false;
}

#endif /* CONFIG_PON */

/**
 * pon_netdev_info_put() - release what pon_netdev_info_get() filled in
 * @info: the answer, which no longer holds a reference afterwards
 *
 * Drops the reference on @info->conduit and clears it.
 */
static inline void pon_netdev_info_put(struct pon_netdev_info *info)
{
	netdev_put(info->conduit, &info->tracker);
	info->conduit = NULL;
}

#endif /* __NET_PON_FUNCTIONS_H */
