// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/run-in-irq-context.h>
#include <kunit/test.h>
#include <linux/bitops.h>
#include <linux/ethtool.h>
#include <linux/jiffies.h>
#include <linux/kmsg_dump.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <net/pon.h>

#include "pon_test.h"

#define T_WORK_COUNT		4
#define T_LOG_MAX		16
#define T_FLUSH_MAX		64
#define T_LONG_DELAY		(100 * HZ)
#define T_IRQ_ITERATIONS	1000
#define T_FEC_BASE		1000
#define T_FEC_DELTA		7
#define T_SERIAL_BYTE		0xab
#define T_EDGE_MAX		8
#define T_LOG_OVERFLOW		4
#define T_LOG_DROP_LINE		"4 log lines dropped"

struct t_log;

struct t_work {
	struct pon_work work;
	struct t_log *log;
	unsigned int id;
};

struct t_log {
	struct t_work items[T_WORK_COUNT];
	unsigned int order[T_LOG_MAX];
	unsigned int runs;
};

struct t_dwork {
	struct pon_delayed_work dwork;
	unsigned int runs;
};

static void t_work_run(struct pon_dev *pdev, struct pon_work *work)
{
	struct t_work *item = container_of(work, struct t_work, work);
	struct t_log *log = item->log;

	if (log->runs < T_LOG_MAX)
		log->order[log->runs] = item->id;
	log->runs++;
}

static struct t_log *t_log_new(struct kunit *test)
{
	struct t_log *log;
	unsigned int i;

	log = kunit_kzalloc(test, sizeof(*log), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, log);

	for (i = 0; i < T_WORK_COUNT; i++) {
		log->items[i].log = log;
		log->items[i].id = i;
		pon_work_init(&log->items[i].work, t_work_run);
	}

	return log;
}

static void t_dwork_run(struct pon_dev *pdev, struct pon_work *work)
{
	struct t_dwork *item = container_of(work, struct t_dwork, dwork.work);

	item->runs++;
}

static struct t_dwork *t_dwork_new(struct kunit *test)
{
	struct t_dwork *item;

	item = kunit_kzalloc(test, sizeof(*item), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, item);
	pon_delayed_work_init(&item->dwork, t_dwork_run);

	return item;
}

static bool t_work_list_empty(struct pon_dev *pdev)
{
	bool empty;

	spin_lock_irq(&pdev->work_lock);
	empty = list_empty(&pdev->work_list);
	spin_unlock_irq(&pdev->work_lock);

	return empty;
}

static bool t_work_queued(struct pon_dev *pdev, struct pon_work *work)
{
	bool queued;

	spin_lock_irq(&pdev->work_lock);
	queued = !list_empty(&work->entry);
	spin_unlock_irq(&pdev->work_lock);

	return queued;
}

static void t_flush(struct kunit *test, struct pon_dev *pdev)
{
	unsigned int i;

	for (i = 0; i < T_FLUSH_MAX; i++) {
		flush_workqueue(pdev->wq);
		if (t_work_list_empty(pdev))
			break;
	}
	KUNIT_ASSERT_LT(test, i, T_FLUSH_MAX);
	flush_workqueue(pdev->wq);
}

static void pon_work_fifo_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_log *log = t_log_new(test);
	unsigned int i;

	mutex_lock(&ctx->pdev->lock);
	for (i = 0; i < T_WORK_COUNT; i++)
		pon_work_queue(ctx->pdev, &log->items[i].work);
	mutex_unlock(&ctx->pdev->lock);

	t_flush(test, ctx->pdev);

	KUNIT_ASSERT_EQ(test, log->runs, T_WORK_COUNT);
	for (i = 0; i < T_WORK_COUNT; i++)
		KUNIT_EXPECT_EQ_MSG(test, log->order[i], i, "run %u", i);
}

static void pon_work_dedupe_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_log *log = t_log_new(test);

	mutex_lock(&ctx->pdev->lock);
	pon_work_queue(ctx->pdev, &log->items[0].work);
	pon_work_queue(ctx->pdev, &log->items[0].work);
	mutex_unlock(&ctx->pdev->lock);

	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, log->runs, 1);

	pon_work_queue(ctx->pdev, &log->items[0].work);
	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, log->runs, 2);
	KUNIT_EXPECT_FALSE(test, t_work_queued(ctx->pdev, &log->items[0].work));
}

static void pon_work_cancel_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_log *log = t_log_new(test);

	mutex_lock(&ctx->pdev->lock);
	pon_work_queue(ctx->pdev, &log->items[0].work);
	pon_work_queue(ctx->pdev, &log->items[1].work);
	pon_work_cancel(ctx->pdev, &log->items[0].work);
	KUNIT_EXPECT_FALSE(test, t_work_queued(ctx->pdev, &log->items[0].work));
	mutex_unlock(&ctx->pdev->lock);

	t_flush(test, ctx->pdev);
	KUNIT_ASSERT_EQ(test, log->runs, 1);
	KUNIT_EXPECT_EQ(test, log->order[0], 1);

	mutex_lock(&ctx->pdev->lock);
	pon_work_cancel(ctx->pdev, &log->items[0].work);
	pon_work_queue(ctx->pdev, &log->items[0].work);
	mutex_unlock(&ctx->pdev->lock);

	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, log->runs, 2);
	KUNIT_EXPECT_EQ(test, log->order[1], 0);
}

static void pon_delayed_work_cancel_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_dwork *item = t_dwork_new(test);

	pon_delayed_work_queue(ctx->pdev, &item->dwork, T_LONG_DELAY);
	KUNIT_ASSERT_TRUE(test, timer_pending(&item->dwork.timer));

	mutex_lock(&ctx->pdev->lock);
	pon_delayed_work_cancel(ctx->pdev, &item->dwork);
	KUNIT_EXPECT_FALSE(test, timer_pending(&item->dwork.timer));

	pon_work_queue(ctx->pdev, &item->dwork.work);
	KUNIT_ASSERT_TRUE(test, t_work_queued(ctx->pdev, &item->dwork.work));
	pon_delayed_work_cancel(ctx->pdev, &item->dwork);
	KUNIT_EXPECT_TRUE(test, list_empty(&item->dwork.work.entry));
	mutex_unlock(&ctx->pdev->lock);

	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, item->runs, 0);
	timer_delete_sync(&item->dwork.timer);
}

static void pon_delayed_work_now_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_dwork *item = t_dwork_new(test);

	pon_delayed_work_queue(ctx->pdev, &item->dwork, T_LONG_DELAY);
	KUNIT_ASSERT_TRUE(test, timer_pending(&item->dwork.timer));

	pon_delayed_work_queue(ctx->pdev, &item->dwork, 0);
	KUNIT_EXPECT_FALSE(test, timer_pending(&item->dwork.timer));
	KUNIT_EXPECT_PTR_EQ(test, item->dwork.pdev, ctx->pdev);

	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, item->runs, 1);
	timer_delete_sync(&item->dwork.timer);
}

static void pon_delayed_work_shutdown_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_dwork *item = t_dwork_new(test);

	pon_delayed_work_queue(ctx->pdev, &item->dwork, T_LONG_DELAY);
	KUNIT_ASSERT_TRUE(test, timer_pending(&item->dwork.timer));

	pon_delayed_work_shutdown(&item->dwork);
	KUNIT_EXPECT_FALSE(test, timer_pending(&item->dwork.timer));

	pon_delayed_work_queue(ctx->pdev, &item->dwork, T_LONG_DELAY);
	KUNIT_EXPECT_FALSE(test, timer_pending(&item->dwork.timer));

	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, item->runs, 0);
}

static void pon_work_unregistered_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_log *log = t_log_new(test);

	pon_test_unregister(ctx);

	pon_work_queue(ctx->pdev, &log->items[0].work);
	KUNIT_EXPECT_FALSE(test, t_work_queued(ctx->pdev, &log->items[0].work));
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, log->runs, 0);
}

static void pon_work_going_away_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_log *log = t_log_new(test);

	mutex_lock(&ctx->pdev->lock);
	pon_work_queue(ctx->pdev, &log->items[0].work);
	WRITE_ONCE(ctx->pdev->going_away, true);
	mutex_unlock(&ctx->pdev->lock);

	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, log->runs, 0);
	KUNIT_EXPECT_TRUE(test, t_work_queued(ctx->pdev, &log->items[0].work));

	pon_work_drain(ctx->pdev);
	KUNIT_EXPECT_TRUE(test, t_work_list_empty(ctx->pdev));
	KUNIT_EXPECT_TRUE(test, list_empty(&log->items[0].work.entry));

	mutex_lock(&ctx->pdev->lock);
	WRITE_ONCE(ctx->pdev->going_away, false);
	mutex_unlock(&ctx->pdev->lock);

	pon_work_queue(ctx->pdev, &log->items[0].work);
	t_flush(test, ctx->pdev);
	KUNIT_EXPECT_EQ(test, log->runs, 1);
}

static void pon_alarm_set_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;

	mutex_lock(&pdev->lock);
	pon_dev_alarm_set(pdev, PON_ALARM_LODS, true);
	KUNIT_EXPECT_TRUE(test, test_bit(PON_ALARM_LODS, &pdev->alarms));
	KUNIT_EXPECT_TRUE(test, t_work_queued(pdev, &pdev->alarm_work));
	mutex_unlock(&pdev->lock);
	t_flush(test, pdev);
	KUNIT_ASSERT_FALSE(test, t_work_queued(pdev, &pdev->alarm_work));

	mutex_lock(&pdev->lock);
	pon_dev_alarm_set(pdev, PON_ALARM_LODS, true);
	KUNIT_EXPECT_TRUE(test, test_bit(PON_ALARM_LODS, &pdev->alarms));
	KUNIT_EXPECT_FALSE(test, t_work_queued(pdev, &pdev->alarm_work));
	mutex_unlock(&pdev->lock);
	t_flush(test, pdev);

	mutex_lock(&pdev->lock);
	pon_dev_alarm_set(pdev, PON_ALARM_LODS, false);
	KUNIT_EXPECT_FALSE(test, test_bit(PON_ALARM_LODS, &pdev->alarms));
	KUNIT_EXPECT_TRUE(test, t_work_queued(pdev, &pdev->alarm_work));
	mutex_unlock(&pdev->lock);
	t_flush(test, pdev);

	mutex_lock(&pdev->lock);
	pon_dev_alarm_set(pdev, PON_ALARM_LODS, false);
	KUNIT_EXPECT_FALSE(test, t_work_queued(pdev, &pdev->alarm_work));
	mutex_unlock(&pdev->lock);
}

static void pon_alarm_range_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;

	mutex_lock(&pdev->lock);
	pon_dev_alarm_set(pdev, PON_ALARM_COUNT, true);
	pon_dev_alarm_set(pdev, BITS_PER_LONG - 1, true);
	KUNIT_EXPECT_EQ(test, READ_ONCE(pdev->alarms), 0);
	KUNIT_EXPECT_FALSE(test, t_work_queued(pdev, &pdev->alarm_work));
	mutex_unlock(&pdev->lock);
}

static bool t_alarm_raise(void *data)
{
	struct pon_dev *pdev = data;

	pon_dev_alarm_set(pdev, PON_ALARM_LODS, true);

	return test_bit(PON_ALARM_LODS, &pdev->alarms);
}

static void pon_alarm_irq_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;
	unsigned long flags;

	local_irq_save(flags);
	pon_dev_alarm_set(pdev, PON_ALARM_LODS, true);
	local_irq_restore(flags);
	KUNIT_EXPECT_TRUE(test, test_bit(PON_ALARM_LODS, &pdev->alarms));
	t_flush(test, pdev);

	local_irq_save(flags);
	pon_dev_alarm_set(pdev, PON_ALARM_LODS, false);
	local_irq_restore(flags);
	KUNIT_EXPECT_FALSE(test, test_bit(PON_ALARM_LODS, &pdev->alarms));
	t_flush(test, pdev);

	kunit_run_irq_test(test, t_alarm_raise, T_IRQ_ITERATIONS, pdev);
	KUNIT_EXPECT_TRUE(test, test_bit(PON_ALARM_LODS, &pdev->alarms));
	t_flush(test, pdev);
	KUNIT_EXPECT_FALSE(test, t_work_queued(pdev, &pdev->alarm_work));
}

static void t_lods_expect(struct kunit *test, u64 events, u64 restored,
			  u64 reactivations)
{
	struct pon_test_ctx *ctx = test->priv;

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, ctx->pdev->lods_events, events);
	KUNIT_EXPECT_EQ(test, ctx->pdev->lods_restored, restored);
	KUNIT_EXPECT_EQ(test, ctx->pdev->lods_reactivations, reactivations);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_lods_count_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_activate(ctx);
	t_lods_expect(test, 0, 0, 0);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O6);
	t_lods_expect(test, 1, 0, 0);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O5);
	t_lods_expect(test, 1, 1, 0);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O6);
	t_lods_expect(test, 2, 1, 0);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	t_lods_expect(test, 2, 1, 1);

	pon_test_activate(ctx);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	t_lods_expect(test, 2, 1, 1);
}

static unsigned int t_state_lines(struct kunit *test,
				  struct kmsg_dump_iter *iter,
				  char edges[][T_LOG_TEXT_LEN])
{
	return pon_test_log_lines(test, iter, "PLOAM state ", edges,
				  T_EDGE_MAX);
}

static void pon_state_log_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	char edges[T_EDGE_MAX][T_LOG_TEXT_LEN];
	struct kmsg_dump_iter iter, level_iter;
	enum pon_mode mode;

	kmsg_dump_rewind(&iter);
	iter.cur_seq = iter.next_seq;

	pon_test_activate(ctx);
	pon_test_log_flush(ctx);
	level_iter = iter;
	KUNIT_EXPECT_EQ(test,
			pon_test_log_level(test, &level_iter,
					   "PLOAM state unknown -> O1"),
			LOGLEVEL_INFO);
	KUNIT_ASSERT_EQ(test, t_state_lines(test, &iter, edges), 4);
	KUNIT_EXPECT_STREQ(test, edges[0], "unknown -> O1");
	KUNIT_EXPECT_STREQ(test, edges[1], "O1 -> O2-3");
	KUNIT_EXPECT_STREQ(test, edges[2], "O2-3 -> O4");
	KUNIT_EXPECT_STREQ(test, edges[3], "O4 -> O5");

	pon_test_state_report(ctx, PON_PLOAM_STATE_O5);
	pon_test_log_flush(ctx);
	KUNIT_EXPECT_EQ(test, t_state_lines(test, &iter, edges), 0);

	mutex_lock(&ctx->pdev->lock);
	pon_dev_state_report(ctx->pdev, PON_PLOAM_STATE_O6);
	pon_dev_state_report(ctx->pdev, PON_PLOAM_STATE_O6);
	KUNIT_EXPECT_EQ(test, t_state_lines(test, &iter, edges), 0);
	mutex_unlock(&ctx->pdev->lock);
	pon_test_log_flush(ctx);
	KUNIT_ASSERT_EQ(test, t_state_lines(test, &iter, edges), 1);
	KUNIT_EXPECT_STREQ(test, edges[0], "O5 -> O6");

	mutex_lock(&ctx->pdev->lock);
	mode = ctx->pdev->mode;
	ctx->pdev->mode = PON_MODE_GPON;
	mutex_unlock(&ctx->pdev->lock);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O2);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O3);
	pon_test_log_flush(ctx);
	KUNIT_ASSERT_EQ(test, t_state_lines(test, &iter, edges), 3);
	KUNIT_EXPECT_STREQ(test, edges[0], "O6 -> O1");
	KUNIT_EXPECT_STREQ(test, edges[1], "O1 -> O2");
	KUNIT_EXPECT_STREQ(test, edges[2], "O2 -> O3");

	mutex_lock(&ctx->pdev->lock);
	ctx->pdev->mode = mode;
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_state_log_overflow_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	char (*lines)[T_LOG_TEXT_LEN];
	enum pon_ploam_state state;
	struct kmsg_dump_iter iter;
	unsigned int i;

	lines = kunit_kcalloc(test, PON_LOG_LINES + 1, T_LOG_TEXT_LEN,
			      GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lines);

	pon_test_activate(ctx);
	pon_test_log_flush(ctx);
	kmsg_dump_rewind(&iter);
	iter.cur_seq = iter.next_seq;

	mutex_lock(&ctx->pdev->lock);
	for (i = 0; i < PON_LOG_LINES + T_LOG_OVERFLOW; i++) {
		state = i % 2 ? PON_PLOAM_STATE_O5 : PON_PLOAM_STATE_O6;
		pon_dev_state_report(ctx->pdev, state);
	}
	mutex_unlock(&ctx->pdev->lock);
	pon_test_log_flush(ctx);

	KUNIT_ASSERT_EQ(test,
			pon_test_log_lines(test, &iter, "", lines,
					   PON_LOG_LINES + 1),
			PON_LOG_LINES + 1);
	KUNIT_EXPECT_STREQ(test, lines[0], T_LOG_DROP_LINE);
	for (i = 1; i <= PON_LOG_LINES; i++)
		KUNIT_EXPECT_STREQ_MSG(test, lines[i],
				       i % 2 ? "PLOAM state O5 -> O6" :
					       "PLOAM state O6 -> O5",
				       "line %u", i);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O6);
	pon_test_log_flush(ctx);
	KUNIT_ASSERT_EQ(test, pon_test_log_lines(test, &iter, "", lines, 1), 1);
	KUNIT_EXPECT_STREQ(test, lines[0], "PLOAM state O5 -> O6");
}

static void pon_carrier_gem_netdev_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	pon_test_activate(ctx);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(dev));

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(dev));

	pon_test_alloc_drop_all(ctx);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(dev));
}

static void pon_carrier_no_tcont_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_OTHER_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	pon_test_activate(ctx);
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, pon_test_state(ctx), PON_PLOAM_STATE_O5);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(dev));
}

static void t_event(struct pon_test_ctx *ctx, enum pon_event_type type,
		    u32 alloc_id)
{
	struct pon_event ev = {
		.type = type,
		.alloc_id = alloc_id,
	};

	mutex_lock(&ctx->pdev->lock);
	pon_dev_event(ctx->pdev, &ev);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_event_unregistered_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_activate(ctx);
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_ASSERT_TRUE(test, netif_carrier_ok(ctx->netdev));

	pon_test_unregister(ctx);

	t_event(ctx, PON_EVENT_TYPE_TCONT_DEALLOC, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
	t_event(ctx, PON_EVENT_TYPE_TCONT_ALLOC, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
}

static void pon_event_no_carrier_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_event ev = {
		.type = PON_EVENT_TYPE_REBOOT_REQ,
		.reboot.depth = 1,
	};

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_activate(ctx);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	KUNIT_ASSERT_FALSE(test, netif_carrier_ok(ctx->netdev));

	t_event(ctx, PON_EVENT_TYPE_MIB_RESET_REQ, 0);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	mutex_lock(&ctx->pdev->lock);
	pon_dev_event(ctx->pdev, &ev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	mutex_lock(&ctx->pdev->lock);
	pon_dev_carrier_update(ctx->pdev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
}

static void t_fec_set(struct pon_test_ctx *ctx, u32 base)
{
	ctx->fec.corrected_bytes = base;
	ctx->fec.corrected_codewords = base * 2;
	ctx->fec.uncorrectable_codewords = base * 3;
	ctx->fec.total_codewords = base * 4;
	ctx->fec.seconds = base * 5;
}

static void t_fec_read(struct pon_test_ctx *ctx,
		       struct ethtool_fec_stats *stats)
{
	stats->corrected_blocks.total = ETHTOOL_STAT_NOT_SET;
	stats->uncorrectable_blocks.total = ETHTOOL_STAT_NOT_SET;
	pon_dev_fec_stats(ctx->pdev, stats);
}

static void pon_fec_fold_fields_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_fec_totals *fec = &ctx->pdev->fec;
	struct ethtool_fec_stats stats;

	t_fec_set(ctx, T_FEC_BASE);
	t_fec_read(ctx, &stats);
	t_fec_set(ctx, T_FEC_BASE + T_FEC_DELTA);
	t_fec_read(ctx, &stats);

	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total, T_FEC_DELTA * 2);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total,
			T_FEC_DELTA * 3);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, fec->corrected_bytes, T_FEC_DELTA);
	KUNIT_EXPECT_EQ(test, fec->corrected_codewords, T_FEC_DELTA * 2);
	KUNIT_EXPECT_EQ(test, fec->uncorrectable_codewords, T_FEC_DELTA * 3);
	KUNIT_EXPECT_EQ(test, fec->total_codewords, T_FEC_DELTA * 4);
	KUNIT_EXPECT_EQ(test, fec->seconds, T_FEC_DELTA * 5);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_fec_unregistered_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fec_stats stats;

	t_fec_set(ctx, T_FEC_BASE);
	t_fec_read(ctx, &stats);
	KUNIT_ASSERT_EQ(test, stats.corrected_blocks.total, 0);

	pon_test_unregister(ctx);

	t_fec_set(ctx, T_FEC_BASE + T_FEC_DELTA);
	t_fec_read(ctx, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total,
			ETHTOOL_STAT_NOT_SET);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total,
			ETHTOOL_STAT_NOT_SET);
}

static void pon_fec_param_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fecparam param = {};

	KUNIT_EXPECT_EQ(test, pon_dev_fec_param(ctx->pdev, &param), 0);
	KUNIT_EXPECT_EQ(test, param.fec, ETHTOOL_FEC_RS);
	KUNIT_EXPECT_EQ(test, param.active_fec, ETHTOOL_FEC_NONE);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	memset(&param, 0, sizeof(param));
	KUNIT_EXPECT_EQ(test, pon_dev_fec_param(ctx->pdev, &param), 0);
	KUNIT_EXPECT_EQ(test, param.fec, ETHTOOL_FEC_RS);
	KUNIT_EXPECT_EQ(test, param.active_fec, ETHTOOL_FEC_NONE);

	pon_test_activate(ctx);
	memset(&param, 0, sizeof(param));
	KUNIT_EXPECT_EQ(test, pon_dev_fec_param(ctx->pdev, &param), 0);
	KUNIT_EXPECT_EQ(test, param.fec, ETHTOOL_FEC_RS);
	KUNIT_EXPECT_EQ(test, param.active_fec, ETHTOOL_FEC_RS);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O6);
	memset(&param, 0, sizeof(param));
	KUNIT_EXPECT_EQ(test, pon_dev_fec_param(ctx->pdev, &param), 0);
	KUNIT_EXPECT_EQ(test, param.active_fec, ETHTOOL_FEC_RS);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O7);
	memset(&param, 0, sizeof(param));
	KUNIT_EXPECT_EQ(test, pon_dev_fec_param(ctx->pdev, &param), 0);
	KUNIT_EXPECT_EQ(test, param.active_fec, ETHTOOL_FEC_RS);
}

static void pon_fec_param_mode_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fecparam param = {};
	enum pon_mode mode;

	mutex_lock(&ctx->pdev->lock);
	mode = ctx->pdev->mode;
	ctx->pdev->mode = PON_MODE_GPON;
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_EXPECT_EQ(test, pon_dev_fec_param(ctx->pdev, &param),
			-EOPNOTSUPP);

	mutex_lock(&ctx->pdev->lock);
	ctx->pdev->mode = mode;
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_unregister_teardown_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_map_new(test, T_GEM_ID);
	KUNIT_ASSERT_TRUE(test, timer_pending(&pdev->fec_work.timer));

	mutex_lock(&pdev->lock);
	pdev->enabled = true;
	pdev->identity.serial_set = true;
	memset(pdev->identity.serial, T_SERIAL_BYTE,
	       sizeof(pdev->identity.serial));
	mutex_unlock(&pdev->lock);
	ctx->enable_on = true;

	pon_test_unregister(ctx);

	KUNIT_EXPECT_EQ(test, ctx->enables, 1);
	KUNIT_EXPECT_FALSE(test, ctx->enable_on);

	mutex_lock(&pdev->lock);
	KUNIT_EXPECT_FALSE(test, pdev->enabled);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->netdev->pon_dev));
	KUNIT_EXPECT_NULL(test, pdev->ops);
	KUNIT_EXPECT_NULL(test, pdev->drv_priv);
	KUNIT_EXPECT_NULL(test, memchr_inv(&pdev->identity, 0,
					   sizeof(pdev->identity)));
	KUNIT_EXPECT_TRUE(test, list_empty(&pdev->tconts));
	KUNIT_EXPECT_TRUE(test, list_empty(&pdev->gems));
	KUNIT_EXPECT_TRUE(test, list_empty(&pdev->gem_maps));
	KUNIT_EXPECT_FALSE(test, timer_pending(&pdev->fec_work.timer));
	mutex_unlock(&pdev->lock);
}

static struct kunit_case pon_state_test_cases[] = {
	KUNIT_CASE(pon_work_fifo_test),
	KUNIT_CASE(pon_work_dedupe_test),
	KUNIT_CASE(pon_work_cancel_test),
	KUNIT_CASE(pon_delayed_work_cancel_test),
	KUNIT_CASE(pon_delayed_work_now_test),
	KUNIT_CASE(pon_delayed_work_shutdown_test),
	KUNIT_CASE(pon_work_unregistered_test),
	KUNIT_CASE(pon_work_going_away_test),
	KUNIT_CASE(pon_alarm_set_test),
	KUNIT_CASE(pon_alarm_range_test),
	KUNIT_CASE(pon_alarm_irq_test),
	KUNIT_CASE(pon_lods_count_test),
	KUNIT_CASE(pon_state_log_test),
	KUNIT_CASE(pon_state_log_overflow_test),
	KUNIT_CASE(pon_carrier_gem_netdev_test),
	KUNIT_CASE(pon_carrier_no_tcont_test),
	KUNIT_CASE(pon_event_unregistered_test),
	KUNIT_CASE(pon_event_no_carrier_test),
	KUNIT_CASE(pon_fec_fold_fields_test),
	KUNIT_CASE(pon_fec_unregistered_test),
	KUNIT_CASE(pon_fec_param_test),
	KUNIT_CASE(pon_fec_param_mode_test),
	KUNIT_CASE(pon_unregister_teardown_test),
	{}
};

static struct kunit_suite pon_state_test_suite = {
	.name = "pon_state",
	.init = pon_test_init_full,
	.exit = pon_test_exit,
	.test_cases = pon_state_test_cases,
};

kunit_test_suite(pon_state_test_suite);
