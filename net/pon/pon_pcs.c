// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/rcupdate.h>
#include <net/pon.h>

#include "pon.h"

/**
 * DOC: The PON PCS
 *
 * The PHY of a PON MAC is a provider of the generic PHY framework, which the
 * MAC drives through PHY_MODE_PON and phy_configure(). What the framework has
 * no place for is the state of the downstream: whether the receiver sees
 * light and has downstream sync, the FEC counters and the reports of a
 * change. struct pon_pcs carries them, shaped after struct phylink_pcs: the
 * PHY driver provides it and the core consumes it.
 *
 * The provider embeds a struct pon_pcs, fills its ops and registers it with
 * pon_pcs_register() after it created the generic PHY. The MAC driver finds
 * it again with pon_pcs_lookup() through the generic PHY it holds and
 * attaches it to its PON device with pon_dev_pcs_attach().
 *
 * The provider reports a change with pon_pcs_change() from any context. A
 * loss sets a latch. Both queue the PCS work of the instance, which runs in
 * the lent context: it passes a latched loss on as down when the link was
 * up, then reads the level with the pcs_get_state op and passes on what it
 * reads. The driver's pcs_link callback sees a change only, never a repeat,
 * and always a down between two ups that a loss separated. So a loss that
 * ends before the work runs still reaches the activation state machine.
 * While the link is not enabled the work passes nothing on and reads
 * nothing. The first report once the link is enabled passes the link on.
 *
 * The core owns the LODS alarm: every read of the level sets it, in the
 * work and in pon_dev_pcs_sync(), which a MAC driver calls when it needs the
 * level at once. The FEC counters of an attached PCS are what the core folds
 * into its totals.
 */

/**
 * struct pon_pcs_res - the managed resource that finds a PCS from its PHY
 * @pcs: the PCS that the provider registered
 */
struct pon_pcs_res {
	struct pon_pcs *pcs;
};

static DEFINE_MUTEX(pon_pcs_lock);

/**
 * pon_pcs_release() - release the record of a PON PCS
 * @dev: the device of the generic PHY
 * @res: the struct pon_pcs_res
 *
 * The devres release of pon_pcs_register(). Warns when the PCS is still
 * attached, which the device link of pon_pcs_lookup() is there to prevent.
 */
static void pon_pcs_release(struct device *dev, void *res)
{
	struct pon_pcs_res *record = res;

	WARN_ON(rcu_access_pointer(record->pcs->pdev));
}

/**
 * pon_pcs_register() - make the PCS of a generic PHY known to the PON core
 * @pcs: the PCS, embedded in the provider's state, with its ops set
 * @phy: the generic PHY, which runs PHY_MODE_PON
 *
 * The record that finds @pcs from @phy belongs to the PHY and is released
 * when the PHY is destroyed. A consumer holds a device link to the provider,
 * so the driver core unbinds the consumer first and no PON device holds @pcs
 * by then.
 *
 * Context: Process context, the probe of the provider.
 * Return: 0, -EINVAL when an argument or an op is missing, -EBUSY when @phy
 * already has a PCS, or -ENOMEM.
 */
int pon_pcs_register(struct pon_pcs *pcs, struct phy *phy)
{
	struct pon_pcs_res *record;

	if (!pcs || !phy || !pcs->ops || !pcs->ops->pcs_get_state ||
	    !pcs->ops->pcs_get_fec_stats)
		return -EINVAL;

	if (devres_find(&phy->dev, pon_pcs_release, NULL, NULL))
		return -EBUSY;

	record = devres_alloc(pon_pcs_release, sizeof(*record), GFP_KERNEL);
	if (!record)
		return -ENOMEM;

	pcs->phy = phy;
	RCU_INIT_POINTER(pcs->pdev, NULL);
	record->pcs = pcs;
	devres_add(&phy->dev, record);

	return 0;
}
EXPORT_SYMBOL_GPL(pon_pcs_register);

/**
 * pon_pcs_lookup() - find the PCS of a generic PHY
 * @consumer: the device that drives the PHY
 * @phy: the generic PHY the consumer holds
 *
 * Links the consumer to the provider of the PHY, so that the consumer is
 * unbound before the provider can be. The link goes with the consumer.
 *
 * Context: Process context, the probe of the consumer.
 * Return: the PCS, -ENODEV when the PHY has none, or -EINVAL when the
 * consumer cannot be linked to the provider.
 */
struct pon_pcs *pon_pcs_lookup(struct device *consumer, struct phy *phy)
{
	struct pon_pcs_res *record;

	record = devres_find(&phy->dev, pon_pcs_release, NULL, NULL);
	if (!record)
		return ERR_PTR(-ENODEV);

	if (!device_link_add(consumer, phy->dev.parent,
			     DL_FLAG_AUTOREMOVE_CONSUMER))
		return ERR_PTR(-EINVAL);

	return record->pcs;
}
EXPORT_SYMBOL_GPL(pon_pcs_lookup);

/**
 * pon_pcs_change() - report a change of the downstream state
 * @pcs: the PCS
 * @up: false for a loss of the link, true for anything else
 *
 * Latches a loss, then queues the PCS work of the attached PON device, which
 * reads the level. Does nothing while no PON device is attached. The rules
 * of the report are those of struct pon_pcs_ops.
 *
 * Context: Any context, hard interrupt included. Takes no sleeping lock.
 */
void pon_pcs_change(struct pon_pcs *pcs, bool up)
{
	struct pon_dev *pdev;

	rcu_read_lock();
	pdev = rcu_dereference(pcs->pdev);
	if (pdev) {
		if (!up)
			atomic_set(&pdev->pcs_failed, 1);
		pon_work_queue(pdev, &pdev->pcs_work);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(pon_pcs_change);

/**
 * pon_dev_pcs_read() - read the level of the attached PCS
 * @pdev: PON device structure, with a PCS attached
 * @state: filled with the state, all false when the read fails
 *
 * Raises the LODS alarm without sync and clears it with sync, as ITU-T
 * G.9807.1 clause C.14.2.2 and Table C.14.3 define it.
 *
 * Context: Called with @pdev->lock held. May sleep.
 * Return: true while the PCS has downstream sync.
 */
static bool pon_dev_pcs_read(struct pon_dev *pdev,
			     struct pon_pcs_state *state)
{
	struct pon_pcs *pcs = pdev->pcs;

	*state = (struct pon_pcs_state){};
	if (pcs->ops->pcs_get_state(pcs, state))
		*state = (struct pon_pcs_state){};

	pon_dev_alarm_set(pdev, PON_ALARM_LODS, !state->sync);

	return state->sync;
}

/**
 * pon_dev_pcs_link_set() - pass a change of the link to the driver
 * @pdev: PON device structure
 * @up: the link
 *
 * Context: Called with @pdev->lock held. May sleep.
 */
static void pon_dev_pcs_link_set(struct pon_dev *pdev, bool up)
{
	if (pdev->pcs_up == up)
		return;

	pdev->pcs_up = up;
	pdev->ops->pcs_link(pdev, up);
}

/**
 * pon_pcs_work() - act on the reports of the attached PCS
 * @pdev: PON device structure
 * @work: the instance's @pcs_work
 *
 * Passes a latched loss on as down when the link was up, then reads the
 * level and passes on a change, as phylink does for a latched failure of a
 * struct phylink_pcs. While the link is not enabled it drops the latched
 * loss and reads nothing, as phylink passes no link up to a stopped MAC: a
 * report queued before the link was stopped must not start an activation.
 *
 * Context: The instance's context, with @pdev->lock held. May sleep.
 */
static void pon_pcs_work(struct pon_dev *pdev, struct pon_work *work)
{
	bool failed = atomic_xchg(&pdev->pcs_failed, 0);
	struct pon_pcs_state state;

	if (!pdev->pcs || !pdev->enabled)
		return;

	if (failed)
		pon_dev_pcs_link_set(pdev, false);

	pon_dev_pcs_link_set(pdev, pon_dev_pcs_read(pdev, &state));
}

/**
 * pon_pcs_init() - prepare the PCS work of a new PON device
 * @pdev: PON device structure
 *
 * Context: From pon_dev_create(), before the device is published.
 */
void pon_pcs_init(struct pon_dev *pdev)
{
	pon_work_init(&pdev->pcs_work, pon_pcs_work);
}

/**
 * pon_pcs_link_forget() - forget the link last passed to the driver
 * @pdev: PON device structure
 *
 * For the core, before it calls the driver's enable callback, which starts
 * or parks the link. The next read with sync then passes the link on as up
 * again, without a down first.
 *
 * Context: Called with @pdev->lock held.
 */
void pon_pcs_link_forget(struct pon_dev *pdev)
{
	lockdep_assert_held(&pdev->lock);

	pdev->pcs_up = false;
}

/**
 * pon_dev_pcs_attach() - take the reports and the counters of a PCS
 * @pdev: PON device structure
 * @pcs: the PCS, from pon_pcs_lookup()
 *
 * From here on the core reads the state and the FEC counters of @pcs and
 * passes a change of the link to the driver's pcs_link callback while the
 * link is enabled. No report comes at attach: the first comes once the
 * provider runs.
 *
 * Context: Process context, without @pdev->lock held. Takes it.
 * Return: 0, -EINVAL without @pcs or without a pcs_link or an enable
 * callback, -ENODEV once pon_dev_unregister() has begun, or -EBUSY when
 * @pcs or @pdev is attached already.
 */
int pon_dev_pcs_attach(struct pon_dev *pdev, struct pon_pcs *pcs)
{
	int err = 0;

	if (!pcs)
		return -EINVAL;

	mutex_lock(&pon_pcs_lock);
	if (rcu_access_pointer(pcs->pdev)) {
		err = -EBUSY;
		goto out;
	}

	mutex_lock(&pdev->lock);
	if (!pon_dev_is_registered(pdev))
		err = -ENODEV;
	else if (!pdev->ops->pcs_link || !pdev->ops->enable)
		err = -EINVAL;
	else if (pdev->pcs)
		err = -EBUSY;

	if (!err) {
		pdev->pcs = pcs;
		pdev->pcs_up = false;
		atomic_set(&pdev->pcs_failed, 0);
		pon_fec_start(pdev);
	}
	mutex_unlock(&pdev->lock);

	if (!err)
		rcu_assign_pointer(pcs->pdev, pdev);
out:
	mutex_unlock(&pon_pcs_lock);
	return err;
}
EXPORT_SYMBOL_GPL(pon_dev_pcs_attach);

/**
 * pon_dev_pcs_detach() - stop taking the reports of the attached PCS
 * @pdev: PON device structure
 *
 * Does nothing when no PCS is attached. Returns once no report of the PCS
 * can reach @pdev any more and its PCS work is off the list.
 *
 * Context: Process context, without @pdev->lock held. Takes it and waits for
 * an RCU grace period.
 */
void pon_dev_pcs_detach(struct pon_dev *pdev)
{
	struct pon_pcs *pcs;

	mutex_lock(&pon_pcs_lock);
	mutex_lock(&pdev->lock);
	pcs = pdev->pcs;
	pdev->pcs = NULL;
	pdev->pcs_up = false;
	mutex_unlock(&pdev->lock);
	if (pcs)
		RCU_INIT_POINTER(pcs->pdev, NULL);
	mutex_unlock(&pon_pcs_lock);

	if (!pcs)
		return;

	synchronize_rcu();

	mutex_lock(&pdev->lock);
	pon_work_cancel(pdev, &pdev->pcs_work);
	atomic_set(&pdev->pcs_failed, 0);
	mutex_unlock(&pdev->lock);
}
EXPORT_SYMBOL_GPL(pon_dev_pcs_detach);

/**
 * pon_dev_pcs_sync() - read the downstream sync of the attached PCS
 * @pdev: PON device structure
 *
 * For a MAC driver that needs the level at once, for example when it
 * enters O1 on an enabled link. Sets the LODS alarm from what it reads,
 * as the PCS work does. Passes nothing to the pcs_link callback.
 *
 * Context: Called with @pdev->lock held. May sleep.
 * Return: true while the PCS has downstream sync, false without it or
 * without a PCS.
 */
bool pon_dev_pcs_sync(struct pon_dev *pdev)
{
	struct pon_pcs_state state;

	lockdep_assert_held(&pdev->lock);

	if (!pdev->pcs)
		return false;

	return pon_dev_pcs_read(pdev, &state);
}
EXPORT_SYMBOL_GPL(pon_dev_pcs_sync);
