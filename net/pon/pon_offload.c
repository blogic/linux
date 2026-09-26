// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <net/pon.h>

#include "pon.h"

/**
 * pon_offload_flush_work() - drop the offloaded flows of every GEM port
 * @pdev:	PON device structure
 * @work:	the work item, @pdev->flow_flush_work
 *
 * Context: Runs in the instance's context, with @pdev->lock held.
 */
static void pon_offload_flush_work(struct pon_dev *pdev, struct pon_work *work)
{
	pon_conduit_flow_flush(pdev, PON_GEM_ANY);
}

/**
 * pon_offload_init() - set up the offload state of a new instance
 * @pdev:	PON device structure
 */
void pon_offload_init(struct pon_dev *pdev)
{
	pon_work_init(&pdev->flow_flush_work, pon_offload_flush_work);
}

/**
 * pon_offload_flush_sched() - drop the offloaded flows after a PLOAM report
 * @pdev:	PON device structure
 *
 * An activation edge or a released alloc-id is reported from the middle of
 * the PLOAM exchange that caused it. The acknowledgment the OLT waits for
 * goes out after the report returns. Nothing that walks the conduit's tables
 * belongs there, so the flush runs in the instance's context afterwards.
 */
void pon_offload_flush_sched(struct pon_dev *pdev)
{
	pon_work_queue(pdev, &pdev->flow_flush_work);
}

/**
 * pon_offload_dev_lock() - find and lock the instance of a PON network device
 * @dev:	a network device
 *
 * On success the caller holds a reference on the instance and its lock and
 * releases both with pon_offload_dev_unlock().
 *
 * Context: Takes the instance lock. Sleeps.
 * Return: the registered instance @dev belongs to, or NULL when @dev is not
 * a PON network device or its instance is not registered.
 */
static struct pon_dev *pon_offload_dev_lock(struct net_device *dev)
{
	struct pon_dev *pdev;

	rcu_read_lock();
	pdev = rcu_dereference(dev->pon_dev);
	if (pdev && !pon_dev_tryget(pdev))
		pdev = NULL;
	rcu_read_unlock();

	if (!pdev)
		return NULL;

	mutex_lock(&pdev->lock);
	if (pon_dev_is_registered(pdev))
		return pdev;

	mutex_unlock(&pdev->lock);
	pon_dev_put(pdev);

	return NULL;
}

/**
 * pon_offload_dev_unlock() - release what pon_offload_dev_lock() took
 * @pdev:	PON device structure
 *
 * Context: Called with @pdev->lock held.
 */
static void pon_offload_dev_unlock(struct pon_dev *pdev)
{
	mutex_unlock(&pdev->lock);
	pon_dev_put(pdev);
}

/**
 * pon_offload_gem_id() - the GEM port a flow leaving a PON device rides
 * @pdev:	PON device structure
 * @dev:	a PON network device of @pdev
 * @key:	the flow as the upstream classifier matches it, or NULL
 * @gem_id:	filled in with the GEM port id on success
 *
 * A GEM network device names its GEM port. The data interface does not: its
 * frames are classified one by one, so a flow leaving it is classified too,
 * from the rule set rather than from a frame.
 *
 * Context: Called with @pdev->lock held.
 * Return: 0, -ENODEV when @dev is neither a GEM network device nor the data
 * interface, -EOPNOTSUPP when there is no @key or the driver cannot classify
 * a flow, or what the driver's gem_resolve callback answered.
 */
static int pon_offload_gem_id(struct pon_dev *pdev, struct net_device *dev,
			      const struct pon_flow_key *key, u16 *gem_id)
{
	if (!pon_gem_netdev_id(dev, gem_id))
		return 0;

	if (dev != pdev->main_netdev)
		return -ENODEV;

	if (!key || !pdev->ops->gem_resolve)
		return -EOPNOTSUPP;

	return pdev->ops->gem_resolve(pdev, key, gem_id);
}

/**
 * pon_netdev_info_get() - where an offloaded flow leaving a PON device goes
 * @dev:	a PON network device, the data interface or a GEM interface
 * @key:	the flow as the upstream classifier matches it, needed only for
 *		the data interface, where nothing else names the GEM port
 * @info:	filled in on success and released with pon_netdev_info_put()
 *
 * A forwarding engine that binds a flow to hardware needs what the transmit
 * path would have put in the descriptor: the conduit that carries the frames,
 * the GEM port they ride and the transmit channel of its T-CONT. Answering
 * from the same rule set the transmit path classifies with is what keeps an
 * accelerated flow on the GEM port its own frames take.
 *
 * Return: 0, -ENODEV when @dev is not a PON network device, -EOPNOTSUPP when
 * the driver cannot classify a flow or the GEM port refuses to be offloaded,
 * -ENOENT when no rule takes it or the GEM port is gone, -ENOLINK while that
 * GEM port has no transmit channel, or -ENETDOWN without a conduit.
 */
int pon_netdev_info_get(struct net_device *dev, const struct pon_flow_key *key,
			struct pon_netdev_info *info)
{
	struct net_device *conduit;
	struct pon_dev *pdev;
	struct pon_gem *gem;
	u16 gem_id;
	int ret;

	pdev = pon_offload_dev_lock(dev);
	if (!pdev)
		return -ENODEV;

	ret = pon_offload_gem_id(pdev, dev, key, &gem_id);
	if (ret)
		goto out;

	gem = pon_gem_find(pdev, gem_id);
	if (!gem) {
		ret = -ENOENT;
		goto out;
	}

	if (gem->cfg.no_offload) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = pon_gem_channel(pdev, gem);
	if (ret < 0)
		goto out;

	info->channel = ret;
	info->queue = gem->cfg.queue;
	info->gem = gem_id;
	ret = 0;

	conduit = pon_conduit_hold(pdev, NULL, &info->tracker);
	info->conduit = conduit;
	if (!conduit)
		ret = -ENETDOWN;

out:
	pon_offload_dev_unlock(pdev);

	return ret;
}
EXPORT_SYMBOL_GPL(pon_netdev_info_get);

/**
 * pon_offload_map_blocked() - whether a classifier rule reaches a blocked port
 * @pdev:	PON device structure
 *
 * Context: Called with @pdev->lock held.
 * Return: true when a classifier rule names a GEM port that refuses to be
 * offloaded.
 */
static bool pon_offload_map_blocked(struct pon_dev *pdev)
{
	struct pon_gem_map *map;
	struct pon_gem *gem;

	list_for_each_entry(map, &pdev->gem_maps, list) {
		gem = pon_gem_find(pdev, map->cfg.gem_id);
		if (gem && gem->cfg.no_offload)
			return true;
	}

	return false;
}

/**
 * pon_netdev_offload_blocked() - whether a device's flows must stay in software
 * @dev: a PON network device, the data interface or a GEM interface
 *
 * A GEM port whose frames the software path rewrites or drops may not be
 * accelerated, because a forwarding engine sees neither the rewrite nor the
 * drop. A GEM network device names one port, so the answer is that port's.
 *
 * The data interface carries every port and which one a frame arrived on is
 * something only the classifier knows. A flow rule does not record it, so the
 * answer covers every port the classifier can reach and only those. A port
 * that no rule names is reached through its own network device. It is that
 * device's answer rather than this one's.
 *
 * Ask this of the device a flow arrives on. For the device it leaves by,
 * pon_netdev_info_get() answers for one port.
 *
 * Return: true when a forwarding engine must not take flows of @dev.
 */
bool pon_netdev_offload_blocked(struct net_device *dev)
{
	bool blocked = false;
	struct pon_dev *pdev;
	struct pon_gem *gem;
	u16 gem_id;

	pdev = pon_offload_dev_lock(dev);
	if (!pdev)
		return false;

	if (!pon_gem_netdev_id(dev, &gem_id)) {
		gem = pon_gem_find(pdev, gem_id);
		blocked = gem && gem->cfg.no_offload;
	} else if (dev == pdev->main_netdev) {
		blocked = pon_offload_map_blocked(pdev);
	}

	pon_offload_dev_unlock(pdev);

	return blocked;
}
EXPORT_SYMBOL_GPL(pon_netdev_offload_blocked);
