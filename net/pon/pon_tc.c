// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <linux/bitmap.h>
#include <linux/netdevice.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/pon.h>

#include "pon.h"

/**
 * pon_tc_channel_named() - whether a T-CONT's scheduler sits on a channel
 * @pdev:	PON device structure
 * @channel:	the conduit transmit channel
 *
 * Context: Called with @pdev->lock held.
 * Return: true when the core offloaded the scheduler of a T-CONT onto
 * @channel and has not taken it off again, false otherwise.
 */
static bool pon_tc_channel_named(struct pon_dev *pdev, unsigned int channel)
{
	struct pon_tcont *tcont;

	list_for_each_entry(tcont, &pdev->tconts, list)
		if (tcont->ets_offloaded && tcont->ets_channel == channel)
			return true;

	return false;
}

/**
 * pon_tc_channel_destroy() - take an offloaded scheduler off a channel
 * @pdev:	PON device structure
 * @channel:	the conduit transmit channel the scheduler was offloaded to
 *
 * Sends TC_ETS_DESTROY for @channel, so the channel falls back to strict
 * priority, unless the scheduler of another T-CONT sits there now. Channels
 * are shared over time, because an alloc-id and with it a channel can move
 * from one T-CONT to another. Without a conduit there is nothing to take off.
 *
 * Context: Called with rtnl and @pdev->lock held.
 * Return: 0, or what pon_conduit_setup_tc() answered.
 */
static int pon_tc_channel_destroy(struct pon_dev *pdev, unsigned int channel)
{
	struct tc_ets_qopt_offload opt = {
		.command = TC_ETS_DESTROY,
	};
	bool paired;

	if (pon_tc_channel_named(pdev, channel))
		return 0;

	return pon_conduit_setup_tc(pdev, channel, TC_SETUP_QDISC_ETS, &opt,
				    &paired);
}

/**
 * pon_tc_tcont_unload() - take the scheduler of a T-CONT off its channel
 * @pdev:	PON device structure
 * @tcont:	the T-CONT
 *
 * Does nothing when the core has not offloaded the T-CONT's scheduler.
 *
 * Context: Called with rtnl and @pdev->lock held.
 * Return: 0, or what pon_tc_channel_destroy() answered.
 */
static int pon_tc_tcont_unload(struct pon_dev *pdev, struct pon_tcont *tcont)
{
	if (!tcont->ets_offloaded)
		return 0;

	tcont->ets_offloaded = false;

	return pon_tc_channel_destroy(pdev, tcont->ets_channel);
}

/**
 * pon_tc_ets_offload() - offload an ETS qdisc onto the channel of a T-CONT
 * @pdev:	PON device structure
 * @tcont:	the T-CONT whose queues the qdisc schedules
 * @opt:	the qdisc's replace parameters
 *
 * A T-CONT that has no conduit channel yet keeps the configuration and gets
 * it once the OLT assigns the alloc-id, as a qdisc on a cable that is not
 * plugged in yet. An instance without a conduit keeps it the same way and
 * gets it once a conduit pairs. A T-CONT whose scheduler the core offloaded
 * onto another channel before has it taken off there first.
 *
 * Context: Called with rtnl and @pdev->lock held.
 * Return: 0 when the qdisc is offloaded or waits for the channel or the
 * conduit, the driver's errno from its tcont_channel callback, or what
 * pon_conduit_setup_tc() answered.
 */
static int pon_tc_ets_offload(struct pon_dev *pdev, struct pon_tcont *tcont,
			      struct tc_ets_qopt_offload *opt)
{
	bool paired;
	int channel;
	int err;

	channel = pdev->ops->tcont_channel(pdev, &tcont->cfg);
	tcont->ets_pending = channel == -ENOLINK;
	if (channel == -ENOLINK)
		return 0;
	if (channel < 0)
		return channel;

	if (tcont->ets_offloaded && tcont->ets_channel != channel) {
		err = pon_tc_tcont_unload(pdev, tcont);
		if (err)
			netdev_warn(pdev->main_netdev,
				    "removing the scheduler of T-CONT %u from channel %u failed: %d\n",
				    tcont->cfg.index, tcont->ets_channel, err);
	}

	err = pon_conduit_setup_tc(pdev, channel, TC_SETUP_QDISC_ETS, opt,
				   &paired);
	if (err)
		return err;
	if (!paired) {
		tcont->ets_pending = true;
		return 0;
	}

	tcont->ets_channel = channel;
	tcont->ets_offloaded = true;

	return 0;
}

/**
 * pon_tc_ets_stats() - tell tc whether the scheduler of a T-CONT runs
 * @pdev:	PON device structure
 * @opt:	the qdisc's offload parameters, a TC_ETS_STATS request
 *
 * The qdisc layer marks a qdisc offloaded when the answer is 0. That holds
 * while the core has the configuration tc holds on the channel the T-CONT
 * has now: not while it waits for a channel or a conduit and not after the
 * conduit refused tc's last replace. No counter is filled in.
 *
 * Context: Called with rtnl and @pdev->lock held.
 * Return: 0 when the scheduler is offloaded, -EOPNOTSUPP otherwise, also
 * when no T-CONT has the index.
 */
static int pon_tc_ets_stats(struct pon_dev *pdev,
			    struct tc_ets_qopt_offload *opt)
{
	struct pon_tcont *tcont;

	tcont = pon_tcont_find(pdev, TC_H_MIN(opt->parent) - 1);
	if (!tcont || !tcont->ets_set || !tcont->ets_offloaded ||
	    tcont->ets_pending || tcont->ets_refused)
		return -EOPNOTSUPP;

	return 0;
}

/**
 * pon_tc_ets() - offload an ETS qdisc of the PON data interface
 * @pdev:	PON device structure
 * @opt:	the qdisc's offload parameters
 *
 * The qdisc must sit under mq child N+1, which selects T-CONT index N. A
 * replace offloads the configuration and keeps it for a later rebind. A
 * replace the conduit refuses keeps the configuration before it and whether
 * that one waits for a channel. It marks the T-CONT refused. A destroy
 * forgets it and takes it off the channel the core offloaded it to, which
 * is not always the channel the T-CONT has now. A stats request is answered
 * by pon_tc_ets_stats().
 *
 * Context: Called with rtnl and @pdev->lock held.
 * Return: 0, -EOPNOTSUPP for a qdisc at the root, a command other than
 * replace, destroy and stats or a scheduler that is not offloaded, -ENOENT
 * when no T-CONT has the index of a replace or a destroy, or what
 * pon_tc_ets_offload() or pon_tc_tcont_unload() answered.
 */
static int pon_tc_ets(struct pon_dev *pdev, struct tc_ets_qopt_offload *opt)
{
	struct pon_tcont *tcont;
	bool pending;
	int err;

	lockdep_assert_held(&pdev->lock);

	if (opt->parent == TC_H_ROOT || !TC_H_MIN(opt->parent))
		return -EOPNOTSUPP;
	if (opt->command == TC_ETS_STATS)
		return pon_tc_ets_stats(pdev, opt);
	if (opt->command != TC_ETS_REPLACE && opt->command != TC_ETS_DESTROY)
		return -EOPNOTSUPP;

	tcont = pon_tcont_find(pdev, TC_H_MIN(opt->parent) - 1);
	if (!tcont)
		return -ENOENT;

	if (opt->command == TC_ETS_DESTROY) {
		tcont->ets_set = false;
		tcont->ets_pending = false;
		tcont->ets_refused = false;
		return pon_tc_tcont_unload(pdev, tcont);
	}

	pending = tcont->ets_pending;
	err = pon_tc_ets_offload(pdev, tcont, opt);
	if (err) {
		tcont->ets_pending = pending;
		tcont->ets_refused = true;
		return err;
	}
	tcont->ets = opt->replace_params;
	tcont->ets.qstats = NULL;
	tcont->ets_set = true;
	tcont->ets_refused = false;
	return 0;
}

/**
 * pon_dev_setup_tc() - offload a qdisc of the PON data interface
 * @pdev:	PON device structure
 * @type:	what tc is setting up
 * @type_data:	the qdisc's offload parameters
 *
 * For a driver's ndo_setup_tc. Transmit queue N of the data interface
 * carries T-CONT index N, so an ETS qdisc under mq child N+1 is the
 * scheduler of that T-CONT's queues and the conduit's driver programs it
 * onto the channel the T-CONT is bound to. The core keeps the last
 * configuration and offloads it again when the T-CONT moves to another
 * alloc-id, when a T-CONT that had no channel gets one and when a conduit
 * pairs with the instance. It answers TC_ETS_STATS by that state, so tc
 * shows the qdisc as offloaded only while the hardware runs it.
 *
 * The qdisc layer calls ndo_setup_tc for an ETS qdisc only when the device
 * advertises NETIF_F_HW_TC, so the driver sets it on the data interface.
 *
 * A flowtable block goes to the conduit instead, because the flows it
 * offloads leave through the conduit's rings. The instance lock is not held
 * for that: the conduit's driver takes its own locks and this path runs the
 * other way round from the qdisc one. A clsact block stays here, because the
 * filters on the data interface are the software path that a bound flow is
 * meant to skip. The conduit's driver would also read them as its own
 * port's.
 *
 * Return: 0, -EOPNOTSUPP for a qdisc or a position that is not offloaded,
 * -ENOENT when no T-CONT has that index, or the driver's errno.
 */
int pon_dev_setup_tc(struct pon_dev *pdev, enum tc_setup_type type,
		     void *type_data)
{
	int err;

	if (type == TC_SETUP_FT)
		return pon_flow_block_setup(pdev, type_data);

	if (type != TC_SETUP_QDISC_ETS)
		return -EOPNOTSUPP;

	ASSERT_RTNL();

	/*
	 * @ops is the instance lock's. The data interface outlives the
	 * driver's unregister, so a tc command can arrive after it.
	 */
	mutex_lock(&pdev->lock);
	if (!pon_dev_is_registered(pdev) || !pdev->ops->tcont_channel)
		err = -EOPNOTSUPP;
	else
		err = pon_tc_ets(pdev, type_data);
	mutex_unlock(&pdev->lock);

	return err;
}
EXPORT_SYMBOL_GPL(pon_dev_setup_tc);

/**
 * pon_tc_tcont_rebind() - offload the pending scheduler of one T-CONT
 * @pdev:	PON device structure
 * @tcont:	the T-CONT
 *
 * Offloads the kept ETS configuration again when it waits for a channel.
 * A failure is logged, because no tc command waits for the result.
 *
 * Context: Called with rtnl and @pdev->lock held.
 */
static void pon_tc_tcont_rebind(struct pon_dev *pdev, struct pon_tcont *tcont)
{
	struct tc_ets_qopt_offload opt = {
		.command = TC_ETS_REPLACE,
	};
	int err;

	lockdep_assert_held(&pdev->lock);

	if (!pon_dev_is_registered(pdev) || !pdev->ops->tcont_channel ||
	    !tcont->ets_set || !tcont->ets_pending)
		return;

	opt.replace_params = tcont->ets;
	err = pon_tc_ets_offload(pdev, tcont, &opt);
	if (err)
		netdev_warn(pdev->main_netdev,
			    "offloading the scheduler of T-CONT %u failed: %d\n",
			    tcont->cfg.index, err);
}

/**
 * pon_tc_stale_destroy() - take the schedulers of deleted T-CONTs off
 * @pdev:	PON device structure
 *
 * Sends TC_ETS_DESTROY for every channel in @pdev->ets_stale that no
 * T-CONT's scheduler sits on now. A failure is logged, because no tc
 * command waits for the result.
 *
 * Context: Called with rtnl and @pdev->lock held.
 */
static void pon_tc_stale_destroy(struct pon_dev *pdev)
{
	unsigned int channel;
	int err;

	for_each_set_bit(channel, pdev->ets_stale, PON_TX_CHANNELS) {
		__clear_bit(channel, pdev->ets_stale);
		err = pon_tc_channel_destroy(pdev, channel);
		if (err)
			netdev_warn(pdev->main_netdev,
				    "removing the scheduler from channel %u failed: %d\n",
				    channel, err);
	}
}

/**
 * pon_tc_rebind() - bring every scheduler to the channel of its T-CONT
 * @pdev:	PON device structure
 *
 * Takes the schedulers of deleted T-CONTs off their channels, then
 * offloads the pending scheduler of every T-CONT. Does nothing once
 * pon_dev_unregister() has begun.
 *
 * Context: Runs from @pdev->tc_work, with rtnl and @pdev->lock held.
 */
void pon_tc_rebind(struct pon_dev *pdev)
{
	struct pon_tcont *tcont;

	if (!pon_dev_is_registered(pdev))
		return;

	pon_tc_stale_destroy(pdev);
	list_for_each_entry(tcont, &pdev->tconts, list)
		pon_tc_tcont_rebind(pdev, tcont);
}

/**
 * pon_tc_tcont_release() - a T-CONT is about to be deleted
 * @pdev:	PON device structure
 * @tcont:	the T-CONT, still on the list
 *
 * The netlink handler that deletes the T-CONT holds no rtnl, which the
 * conduit's setup_tc needs. So the channel its scheduler sits on is noted
 * in @pdev->ets_stale and pon_tc_rebind() takes the scheduler off from
 * @pdev->tc_work.
 *
 * Context: Called with @pdev->lock held.
 */
void pon_tc_tcont_release(struct pon_dev *pdev, struct pon_tcont *tcont)
{
	lockdep_assert_held(&pdev->lock);

	if (!tcont->ets_offloaded)
		return;

	tcont->ets_offloaded = false;
	__set_bit(tcont->ets_channel, pdev->ets_stale);
	pon_tc_rebind_sched(pdev);
}

/**
 * pon_tc_unload() - take every scheduler the core offloaded off its channel
 * @pdev:	PON device structure
 *
 * For pon_dev_unregister(), while the conduit is still paired: once the
 * instance goes, nothing else takes the schedulers of its T-CONTs off the
 * conduit's channels.
 *
 * Context: Called with rtnl and @pdev->lock held.
 */
void pon_tc_unload(struct pon_dev *pdev)
{
	struct pon_tcont *tcont;
	int err;

	lockdep_assert_held(&pdev->lock);

	pon_tc_stale_destroy(pdev);
	list_for_each_entry(tcont, &pdev->tconts, list) {
		err = pon_tc_tcont_unload(pdev, tcont);
		if (err)
			netdev_warn(pdev->main_netdev,
				    "removing the scheduler of T-CONT %u failed: %d\n",
				    tcont->cfg.index, err);
	}
}

/**
 * pon_tc_alloc_bound() - an alloc-id got a channel
 * @pdev:	PON device structure
 * @alloc_id:	the alloc-id the driver bound to a channel
 *
 * A T-CONT is linked to its alloc-id through the Alloc-ID attribute of the
 * T-CONT ME, ITU-T G.988 clause 9.2.2. When a T-CONT on @alloc_id has a
 * scheduler that waits for a channel, this schedules pon_tc_rebind().
 *
 * Context: Called with @pdev->lock held.
 */
void pon_tc_alloc_bound(struct pon_dev *pdev, u32 alloc_id)
{
	struct pon_tcont *tcont;

	lockdep_assert_held(&pdev->lock);

	list_for_each_entry(tcont, &pdev->tconts, list) {
		if (tcont->cfg.alloc_id != alloc_id || !tcont->ets_pending)
			continue;

		pon_tc_rebind_sched(pdev);
		return;
	}
}

/**
 * pon_tc_conduit_paired() - offload the kept schedulers onto a new conduit
 * @pdev:	PON device structure
 *
 * A conduit that pairs with the instance holds none of its schedulers,
 * whether it is the first conduit or a replacement. Forgets which channels
 * the core offloaded to before, marks every kept ETS configuration as
 * pending and schedules pon_tc_rebind().
 *
 * Context: Takes @pdev->lock. Called without it.
 */
void pon_tc_conduit_paired(struct pon_dev *pdev)
{
	struct pon_tcont *tcont;
	bool pending = false;

	mutex_lock(&pdev->lock);
	bitmap_zero(pdev->ets_stale, PON_TX_CHANNELS);
	list_for_each_entry(tcont, &pdev->tconts, list) {
		tcont->ets_offloaded = false;
		if (!tcont->ets_set)
			continue;
		tcont->ets_pending = true;
		pending = true;
	}
	mutex_unlock(&pdev->lock);

	if (pending)
		pon_tc_rebind_sched(pdev);
}

/**
 * pon_tc_alloc_unbound() - an alloc-id lost its channel
 * @pdev:	PON device structure
 * @alloc_id:	the alloc-id the driver released
 *
 * Marks the kept scheduler of every T-CONT on @alloc_id as pending, so it
 * is offloaded again when the alloc-id gets a channel.
 *
 * Context: Called with @pdev->lock held.
 */
void pon_tc_alloc_unbound(struct pon_dev *pdev, u32 alloc_id)
{
	struct pon_tcont *tcont;

	lockdep_assert_held(&pdev->lock);

	list_for_each_entry(tcont, &pdev->tconts, list)
		if (tcont->cfg.alloc_id == alloc_id && tcont->ets_set)
			tcont->ets_pending = true;
}
