// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <net/pon.h>

#include "pon.h"

/**
 * DOC: Alarms
 *
 * An ONU alarm is a level, not an edge. The driver raises one while the
 * condition holds and clears it when the condition ends and the core keeps
 * the whole set in one bitmap per instance. A reader that misses a
 * notification is correct again after the next one and alarm-get always
 * carries the truth, so nothing has to be replayed or sequenced.
 *
 * pon_dev_alarm_set() is callable from any context. It changes the bitmap
 * with an atomic bit operation and leaves the notification to the instance's
 * own context.
 */

/**
 * pon_alarm_notify_work() - publish the alarm set
 * @pdev:	PON device structure
 * @work:	the instance's @alarm_work
 *
 * Context: The instance's context, with @pdev->lock held.
 */
static void pon_alarm_notify_work(struct pon_dev *pdev, struct pon_work *work)
{
	pon_nl_notify_alarm(pdev);
}

/**
 * pon_alarm_init() - prepare the alarm notification of a new PON device
 * @pdev:	PON device structure
 *
 * The alarm set travels as one 32 bit netlink attribute, so it holds at
 * most 32 alarms.
 *
 * Context: From pon_dev_create(), before the device is published.
 */
void pon_alarm_init(struct pon_dev *pdev)
{
	BUILD_BUG_ON(PON_ALARM_COUNT > 32);

	pon_work_init(&pdev->alarm_work, pon_alarm_notify_work);
}

/**
 * pon_dev_alarm_set() - raise or clear one ONU alarm
 * @pdev:	PON device structure
 * @alarm:	which alarm, enum pon_alarm
 * @raised:	true while the condition holds, false once it ends
 *
 * Safe from any context, hard interrupt included. Reporting an alarm that is
 * already in the state asked for does nothing, so a driver may report the
 * level as often as it likes.
 */
void pon_dev_alarm_set(struct pon_dev *pdev, u32 alarm, bool raised)
{
	bool changed;

	if (alarm >= PON_ALARM_COUNT)
		return;

	if (raised)
		changed = !test_and_set_bit(alarm, &pdev->alarms);
	else
		changed = test_and_clear_bit(alarm, &pdev->alarms);

	if (changed)
		pon_work_queue(pdev, &pdev->alarm_work);
}
EXPORT_SYMBOL_GPL(pon_dev_alarm_set);
