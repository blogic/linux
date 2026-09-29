// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/visibility.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <net/pon.h>

#include "pon.h"

/**
 * DOC: The lent context
 *
 * A PON MAC runs the ITU-T activation state machine itself, but it must not
 * run it in hard interrupt context and it must not run it against a half
 * finished netlink transaction. The core therefore lends the driver its own
 * serialized context: one ordered workqueue per instance and one lock that
 * the netlink handlers take in their pre_doit.
 *
 * A driver queues a pon_work from any context, hard interrupt included. The
 * handler runs with pdev->lock held, so it may sleep.
 *
 * One worker drains the list one item at a time and re-queues itself while
 * the list is not empty, so the ordering a driver sees is the order it
 * queued in.
 *
 * pon_work_cancel() and pon_delayed_work_cancel() must be called with
 * pdev->lock held, which is what a state machine needs: a state transition
 * stops its own timers and drops its own queued items from inside the
 * handler that the same lock protects.
 *
 * pon_delayed_work_shutdown() ends a delayed item for good when the driver
 * tears down, without the lock and without the instance.
 */

void pon_work_worker(struct work_struct *work)
{
	struct pon_dev *pdev = container_of(work, struct pon_dev, work);
	struct pon_work *item;

	mutex_lock(&pdev->lock);

	if (pdev->going_away) {
		mutex_unlock(&pdev->lock);
		return;
	}

	spin_lock_irq(&pdev->work_lock);
	item = list_first_entry_or_null(&pdev->work_list, struct pon_work,
					entry);
	if (item) {
		list_del_init(&item->entry);
		if (!list_empty(&pdev->work_list))
			queue_work(pdev->wq, work);
	}
	spin_unlock_irq(&pdev->work_lock);

	if (item)
		item->func(pdev, item);

	mutex_unlock(&pdev->lock);
}

/**
 * pon_work_queue() - run a work item in the instance's context
 * @pdev: PON device structure
 * @work: the item, initialized with pon_work_init()
 *
 * Safe from any context, hard interrupt included. Queueing an item that is
 * already queued and has not yet run does nothing, so a burst of reports
 * costs one run.
 */
void pon_work_queue(struct pon_dev *pdev, struct pon_work *work)
{
	unsigned long flags;

	/*
	 * A driver that reports after pon_dev_unregister() canceled the work
	 * would queue onto a workqueue that is being drained, which warns. The
	 * instance is going: drop the item instead.
	 */
	if (READ_ONCE(pdev->going_away))
		return;

	spin_lock_irqsave(&pdev->work_lock, flags);
	if (list_empty(&work->entry))
		list_add_tail(&work->entry, &pdev->work_list);
	spin_unlock_irqrestore(&pdev->work_lock, flags);

	queue_work(pdev->wq, &pdev->work);
}
EXPORT_SYMBOL_GPL(pon_work_queue);

/**
 * pon_work_cancel() - take a work item off the list
 * @pdev: PON device structure
 * @work: the item
 *
 * Takes @work off the list if it is queued. The worker takes an item off
 * the list and runs it with @pdev->lock held, so a caller that holds the
 * lock never sees an item that has been taken but has not run yet. Called
 * from the handler of @work itself, it does nothing: the worker already
 * took the item off the list.
 *
 * Context: Any context that holds @pdev->lock. Does not sleep.
 */
void pon_work_cancel(struct pon_dev *pdev, struct pon_work *work)
{
	unsigned long flags;

	lockdep_assert_held(&pdev->lock);

	spin_lock_irqsave(&pdev->work_lock, flags);
	if (!list_empty(&work->entry))
		list_del_init(&work->entry);
	spin_unlock_irqrestore(&pdev->work_lock, flags);
}
EXPORT_SYMBOL_GPL(pon_work_cancel);

/**
 * pon_delayed_work_timer() - queue a delayed item when its timer fires
 * @timer: the timer
 *
 * The timer does nothing but queue, so the handler never runs in softirq
 * context.
 */
void pon_delayed_work_timer(struct timer_list *timer)
{
	struct pon_delayed_work *dwork =
		timer_container_of(dwork, timer, timer);

	pon_work_queue(dwork->pdev, &dwork->work);
}
EXPORT_SYMBOL_GPL(pon_delayed_work_timer);

/**
 * pon_delayed_work_queue() - run a work item after a delay
 * @pdev: PON device structure
 * @dwork: the item, initialized with pon_delayed_work_init()
 * @delay: jiffies to wait, 0 to queue now
 *
 * Safe from any context.
 */
void pon_delayed_work_queue(struct pon_dev *pdev,
			    struct pon_delayed_work *dwork,
			    unsigned long delay)
{
	if (!delay) {
		timer_delete(&dwork->timer);
		dwork->pdev = pdev;
		pon_work_queue(pdev, &dwork->work);
		return;
	}

	dwork->pdev = pdev;
	mod_timer(&dwork->timer, jiffies + delay);
}
EXPORT_SYMBOL_GPL(pon_delayed_work_queue);

/**
 * pon_delayed_work_cancel() - stop a delayed work item
 * @pdev: PON device structure
 * @dwork: the item
 *
 * Stops the timer and takes the item off the list, so neither the timer
 * nor the queue can bring it back.
 *
 * Context: Process context, with @pdev->lock held. Waits for a timer
 * callback that is running, which takes only the work lock.
 */
void pon_delayed_work_cancel(struct pon_dev *pdev,
			     struct pon_delayed_work *dwork)
{
	lockdep_assert_held(&pdev->lock);

	timer_delete_sync(&dwork->timer);
	pon_work_cancel(pdev, &dwork->work);
}
EXPORT_SYMBOL_GPL(pon_delayed_work_cancel);

/**
 * pon_delayed_work_shutdown() - stop a delayed work item for good
 * @dwork: the item
 *
 * For a driver's teardown, after pon_dev_unregister() or before the
 * instance exists: it takes no lock and needs no instance. It waits for a
 * running timer callback and the timer cannot be armed again afterwards.
 * Release every delayed item this way before the last pon_dev_put().
 *
 * Context: Process context. Called without @dwork's instance lock held.
 */
void pon_delayed_work_shutdown(struct pon_delayed_work *dwork)
{
	timer_shutdown_sync(&dwork->timer);
}
EXPORT_SYMBOL_GPL(pon_delayed_work_shutdown);

/**
 * pon_work_drain() - drop every queued item
 * @pdev: PON device structure
 *
 * For the unregister path, after @going_away is set. Items still on the list
 * are dropped rather than run, because the driver that owns them is leaving.
 */
void pon_work_drain(struct pon_dev *pdev)
{
	unsigned long flags;

	spin_lock_irqsave(&pdev->work_lock, flags);
	while (!list_empty(&pdev->work_list)) {
		struct pon_work *item;

		item = list_first_entry(&pdev->work_list, struct pon_work,
					entry);
		list_del_init(&item->entry);
	}
	spin_unlock_irqrestore(&pdev->work_lock, flags);
}
EXPORT_SYMBOL_IF_KUNIT(pon_work_drain);
