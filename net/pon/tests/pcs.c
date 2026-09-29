// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/device.h>
#include <kunit/resource.h>
#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ethtool.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <net/netlink.h>
#include <net/pon.h>

#include "pon_test.h"

#define T_FEC_BASE		10
#define T_FEC_CORRECTED		17
#define T_FEC_DRIVER		1000

struct t_block {
	struct work_struct work;
	struct completion release;
};

struct t_pcs {
	struct pon_pcs pcs;
	struct device *provider;
	struct device *mac;
	struct phy *phy;
	struct pon_pcs_state state;
	int state_err;
	unsigned int state_reads;
	struct pon_fec_stats fec;
	unsigned int fec_reads;
};

static struct t_pcs *t_pcs_from(struct pon_pcs *pcs)
{
	return container_of(pcs, struct t_pcs, pcs);
}

static int t_pcs_get_state(struct pon_pcs *pcs, struct pon_pcs_state *state)
{
	struct t_pcs *tpcs = t_pcs_from(pcs);

	tpcs->state_reads++;
	if (tpcs->state_err)
		return tpcs->state_err;
	*state = tpcs->state;

	return 0;
}

static int t_pcs_get_fec_stats(struct pon_pcs *pcs,
			       struct pon_fec_stats *stats)
{
	struct t_pcs *tpcs = t_pcs_from(pcs);

	tpcs->fec_reads++;
	*stats = tpcs->fec;

	return 0;
}

static const struct pon_pcs_ops t_pcs_ops = {
	.pcs_get_state		= t_pcs_get_state,
	.pcs_get_fec_stats	= t_pcs_get_fec_stats,
};

static const struct pon_pcs_ops t_pcs_ops_no_state = {
	.pcs_get_fec_stats	= t_pcs_get_fec_stats,
};

static const struct pon_pcs_ops t_pcs_ops_no_fec = {
	.pcs_get_state		= t_pcs_get_state,
};

static void t_phy_dev_release(struct device *dev)
{
	kfree(container_of(dev, struct phy, dev));
}

KUNIT_DEFINE_ACTION_WRAPPER(t_phy_put, put_device, struct device *);

static int t_pcs_init(struct kunit *test)
{
	struct pon_test_ctx *ctx;
	struct t_pcs *tpcs;
	struct phy *phy;
	int ret;

	tpcs = kunit_kzalloc(test, sizeof(*tpcs), GFP_KERNEL);
	if (!tpcs)
		return -ENOMEM;
	tpcs->pcs.ops = &t_pcs_ops;

	tpcs->provider = kunit_device_register(test, "pon-test-pcs");
	if (IS_ERR(tpcs->provider))
		return PTR_ERR(tpcs->provider);
	tpcs->mac = kunit_device_register(test, "pon-test-pcs-mac");
	if (IS_ERR(tpcs->mac))
		return PTR_ERR(tpcs->mac);

	phy = kzalloc_obj(*phy, GFP_KERNEL);
	if (!phy)
		return -ENOMEM;
	device_initialize(&phy->dev);
	phy->dev.parent = tpcs->provider;
	phy->dev.release = t_phy_dev_release;
	ret = dev_set_name(&phy->dev, "pon-test-pcs.0");
	if (ret) {
		put_device(&phy->dev);
		return ret;
	}
	ret = kunit_add_action_or_reset(test, t_phy_put, &phy->dev);
	if (ret)
		return ret;
	tpcs->phy = phy;

	ret = pon_test_init_full(test);
	if (ret)
		return ret;

	ctx = test->priv;
	ctx->pcs = &tpcs->pcs;

	return pon_test_nl_open(test);
}

static void t_pcs_exit(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	if (ctx)
		pon_dev_pcs_detach(ctx->pdev);
	pon_test_exit(test);
}

static struct t_pcs *t_pcs_of(struct pon_test_ctx *ctx)
{
	return t_pcs_from(ctx->pcs);
}

static void t_pcs_attach(struct pon_test_ctx *ctx)
{
	struct t_pcs *tpcs = t_pcs_of(ctx);
	struct kunit *test = ctx->test;

	KUNIT_ASSERT_EQ(test, pon_pcs_register(&tpcs->pcs, tpcs->phy), 0);
	KUNIT_ASSERT_PTR_EQ(test, pon_pcs_lookup(tpcs->mac, tpcs->phy),
			    &tpcs->pcs);
	KUNIT_ASSERT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs), 0);
}

static void t_pcs_report(struct pon_test_ctx *ctx, bool up)
{
	pon_pcs_change(&t_pcs_of(ctx)->pcs, up);
	flush_workqueue(ctx->pdev->wq);
}

static bool t_lods(struct pon_test_ctx *ctx)
{
	return test_bit(PON_ALARM_LODS, &ctx->pdev->alarms);
}

static int t_enable(struct pon_test_ctx *ctx, u8 on)
{
	struct nlmsghdr *reply;
	struct sk_buff *skb;

	skb = pon_test_nl_new(ctx, PON_CMD_DEV_SET, 0);
	KUNIT_ASSERT_EQ(ctx->test,
			nla_put_u32(skb, PON_A_DEV_ID, ctx->pdev->id), 0);
	KUNIT_ASSERT_EQ(ctx->test, nla_put_u8(skb, PON_A_DEV_ENABLE, on), 0);

	return pon_test_nl_request(ctx, skb, &reply);
}

static void t_pcs_attach_enabled(struct pon_test_ctx *ctx)
{
	t_pcs_attach(ctx);
	KUNIT_ASSERT_EQ(ctx->test, t_enable(ctx, 1), 0);
}

static void t_block_worker(struct work_struct *work)
{
	struct t_block *block = container_of(work, struct t_block, work);

	wait_for_completion(&block->release);
}

static struct t_block *t_block_start(struct pon_test_ctx *ctx)
{
	struct t_block *block;

	block = kunit_kzalloc(ctx->test, sizeof(*block), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, block);
	INIT_WORK(&block->work, t_block_worker);
	init_completion(&block->release);
	queue_work(ctx->pdev->wq, &block->work);

	return block;
}

static void t_block_end(struct pon_test_ctx *ctx, struct t_block *block)
{
	complete(&block->release);
	flush_workqueue(ctx->pdev->wq);
}

static void pon_pcs_register_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);
	struct pon_pcs other = { .ops = &t_pcs_ops };
	struct pon_pcs bad = {};

	KUNIT_EXPECT_EQ(test, pon_pcs_register(NULL, tpcs->phy), -EINVAL);
	KUNIT_EXPECT_EQ(test, pon_pcs_register(&tpcs->pcs, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, pon_pcs_register(&bad, tpcs->phy), -EINVAL);
	bad.ops = &t_pcs_ops_no_state;
	KUNIT_EXPECT_EQ(test, pon_pcs_register(&bad, tpcs->phy), -EINVAL);
	bad.ops = &t_pcs_ops_no_fec;
	KUNIT_EXPECT_EQ(test, pon_pcs_register(&bad, tpcs->phy), -EINVAL);

	KUNIT_ASSERT_EQ(test, pon_pcs_register(&tpcs->pcs, tpcs->phy), 0);
	KUNIT_EXPECT_PTR_EQ(test, tpcs->pcs.phy, tpcs->phy);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(tpcs->pcs.pdev));

	KUNIT_EXPECT_EQ(test, pon_pcs_register(&other, tpcs->phy), -EBUSY);
	KUNIT_EXPECT_PTR_EQ(test, pon_pcs_lookup(tpcs->mac, tpcs->phy),
			    &tpcs->pcs);
}

static void pon_pcs_lookup_none_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	KUNIT_EXPECT_EQ(test, PTR_ERR(pon_pcs_lookup(tpcs->mac, tpcs->phy)),
			-ENODEV);
	KUNIT_EXPECT_TRUE(test, list_empty(&tpcs->mac->links.suppliers));
}

static void pon_pcs_lookup_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	KUNIT_ASSERT_EQ(test, pon_pcs_register(&tpcs->pcs, tpcs->phy), 0);
	KUNIT_EXPECT_PTR_EQ(test, pon_pcs_lookup(tpcs->mac, tpcs->phy),
			    &tpcs->pcs);
	KUNIT_EXPECT_FALSE(test, list_empty(&tpcs->mac->links.suppliers));
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&tpcs->provider->links.consumers));
}

static void pon_pcs_attach_twice_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);
	struct pon_pcs other = { .ops = &t_pcs_ops };
	struct pon_dev_ops *no_enable;

	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, NULL), -EINVAL);

	t_pcs_attach(ctx);
	KUNIT_EXPECT_PTR_EQ(test, ctx->pdev->pcs, &tpcs->pcs);
	KUNIT_EXPECT_PTR_EQ(test, rcu_access_pointer(tpcs->pcs.pdev),
			    ctx->pdev);

	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs),
			-EBUSY);
	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &other), -EBUSY);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(other.pdev));
	KUNIT_EXPECT_PTR_EQ(test, ctx->pdev->pcs, &tpcs->pcs);

	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 0);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 0);

	pon_dev_pcs_detach(ctx->pdev);
	pon_test_ops_set(ctx, &pon_test_ops);
	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs),
			-EINVAL);
	KUNIT_EXPECT_NULL(test, ctx->pdev->pcs);

	no_enable = kunit_kzalloc(test, sizeof(*no_enable), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, no_enable);
	*no_enable = pon_test_ops_full;
	no_enable->enable = NULL;
	pon_test_ops_set(ctx, no_enable);
	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs),
			-EINVAL);
	KUNIT_EXPECT_NULL(test, ctx->pdev->pcs);
	pon_test_ops_set(ctx, &pon_test_ops_full);
	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs), 0);
}

static void pon_pcs_change_unattached_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	KUNIT_ASSERT_EQ(test, pon_pcs_register(&tpcs->pcs, tpcs->phy), 0);
	tpcs->state.signal = true;
	tpcs->state.sync = true;

	t_pcs_report(ctx, false);
	t_pcs_report(ctx, true);

	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 0);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->pdev->pcs_failed), 0);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));
}

static void pon_pcs_loss_latch_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	t_pcs_attach_enabled(ctx);
	tpcs->state.signal = true;
	tpcs->state.sync = true;

	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 1);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[0]);

	mutex_lock(&ctx->pdev->lock);
	pon_pcs_change(&tpcs->pcs, false);
	pon_pcs_change(&tpcs->pcs, true);
	mutex_unlock(&ctx->pdev->lock);
	flush_workqueue(ctx->pdev->wq);

	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 3);
	KUNIT_EXPECT_FALSE(test, ctx->pcs_log[1]);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[2]);
	KUNIT_EXPECT_TRUE(test, ctx->pdev->pcs_up);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->pdev->pcs_failed), 0);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));
}

static void pon_pcs_repeat_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	t_pcs_attach_enabled(ctx);
	tpcs->state.signal = true;
	tpcs->state.sync = true;

	t_pcs_report(ctx, true);
	t_pcs_report(ctx, true);
	t_pcs_report(ctx, true);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 3);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 1);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[0]);

	tpcs->state.sync = false;
	t_pcs_report(ctx, true);
	t_pcs_report(ctx, true);
	t_pcs_report(ctx, false);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 2);
	KUNIT_EXPECT_FALSE(test, ctx->pcs_log[1]);

	tpcs->state.sync = true;
	t_pcs_report(ctx, true);
	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 3);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[2]);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 8);
}

static void pon_pcs_lods_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);
	bool sync;

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, pon_dev_pcs_sync(ctx->pdev));
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));

	t_pcs_attach_enabled(ctx);

	t_pcs_report(ctx, false);
	KUNIT_EXPECT_TRUE(test, t_lods(ctx));
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 0);

	tpcs->state.signal = true;
	tpcs->state.sync = true;
	t_pcs_report(ctx, true);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));

	tpcs->state.sync = false;
	mutex_lock(&ctx->pdev->lock);
	sync = pon_dev_pcs_sync(ctx->pdev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, sync);
	KUNIT_EXPECT_TRUE(test, t_lods(ctx));
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);

	tpcs->state.sync = true;
	mutex_lock(&ctx->pdev->lock);
	sync = pon_dev_pcs_sync(ctx->pdev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, sync);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));

	tpcs->state_err = -EIO;
	t_pcs_report(ctx, true);
	KUNIT_EXPECT_TRUE(test, t_lods(ctx));
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 2);
	KUNIT_EXPECT_FALSE(test, ctx->pcs_log[1]);
}

static void pon_pcs_fec_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);
	struct ethtool_fec_stats stats = {};

	ctx->fec.corrected_codewords = T_FEC_DRIVER;
	t_pcs_attach(ctx);

	tpcs->fec.corrected_codewords = T_FEC_BASE;
	pon_dev_fec_stats(ctx->pdev, &stats);
	KUNIT_EXPECT_EQ(test, tpcs->fec_reads, 1);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total, 0);

	tpcs->fec.corrected_codewords = T_FEC_CORRECTED;
	pon_dev_fec_stats(ctx->pdev, &stats);
	KUNIT_EXPECT_EQ(test, tpcs->fec_reads, 2);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total,
			T_FEC_CORRECTED - T_FEC_BASE);

	pon_dev_pcs_detach(ctx->pdev);
	pon_dev_fec_stats(ctx->pdev, &stats);
	KUNIT_EXPECT_EQ(test, tpcs->fec_reads, 2);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total,
			T_FEC_DRIVER - T_FEC_BASE);
}

static void pon_pcs_enable_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	t_pcs_attach(ctx);
	tpcs->state.signal = true;
	tpcs->state.sync = true;

	KUNIT_ASSERT_EQ(test, t_enable(ctx, 1), 0);
	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 1);

	KUNIT_ASSERT_EQ(test, t_enable(ctx, 0), 0);
	KUNIT_ASSERT_EQ(test, t_enable(ctx, 1), 0);
	KUNIT_EXPECT_EQ(test, ctx->enables, 3);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);

	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 2);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[1]);
}

static void pon_pcs_disabled_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);
	struct t_block *block;

	t_pcs_attach_enabled(ctx);
	tpcs->state.signal = true;
	tpcs->state.sync = true;
	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 1);
	KUNIT_ASSERT_EQ(test, tpcs->state_reads, 1);

	block = t_block_start(ctx);
	pon_pcs_change(&tpcs->pcs, false);
	pon_pcs_change(&tpcs->pcs, true);
	KUNIT_EXPECT_EQ(test, t_enable(ctx, 0), 0);
	t_block_end(ctx, block);

	KUNIT_EXPECT_FALSE(test, ctx->pdev->enabled);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 1);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);
	KUNIT_EXPECT_FALSE(test, ctx->pdev->pcs_up);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->pdev->pcs_failed), 0);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));

	KUNIT_ASSERT_EQ(test, t_enable(ctx, 1), 0);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);
	t_pcs_report(ctx, true);
	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 2);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[1]);
	KUNIT_EXPECT_TRUE(test, ctx->pdev->pcs_up);
}

static void pon_pcs_disabled_loss_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);
	struct t_block *block;

	t_pcs_attach_enabled(ctx);
	tpcs->state.signal = true;
	tpcs->state.sync = true;
	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 1);

	KUNIT_ASSERT_EQ(test, t_enable(ctx, 0), 0);
	tpcs->state.signal = false;
	tpcs->state.sync = false;
	t_pcs_report(ctx, false);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 1);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->pdev->pcs_failed), 0);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));

	tpcs->state.signal = true;
	tpcs->state.sync = true;
	block = t_block_start(ctx);
	pon_pcs_change(&tpcs->pcs, false);
	KUNIT_EXPECT_EQ(test, t_enable(ctx, 1), 0);
	t_block_end(ctx, block);

	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 2);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 2);
	KUNIT_EXPECT_TRUE(test, ctx->pcs_log[1]);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->pdev->pcs_failed), 0);
	KUNIT_EXPECT_FALSE(test, t_lods(ctx));
}

static void pon_pcs_detach_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_pcs *tpcs = t_pcs_of(ctx);

	pon_dev_pcs_detach(ctx->pdev);

	t_pcs_attach_enabled(ctx);
	tpcs->state.signal = true;
	tpcs->state.sync = true;
	t_pcs_report(ctx, true);
	KUNIT_ASSERT_EQ(test, ctx->pcs_calls, 1);

	pon_dev_pcs_detach(ctx->pdev);
	KUNIT_EXPECT_NULL(test, ctx->pdev->pcs);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(tpcs->pcs.pdev));
	KUNIT_EXPECT_FALSE(test, ctx->pdev->pcs_up);

	t_pcs_report(ctx, false);
	t_pcs_report(ctx, true);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 1);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);

	pon_dev_pcs_detach(ctx->pdev);

	KUNIT_ASSERT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs), 0);
	pon_test_unregister(ctx);
	KUNIT_EXPECT_EQ(test, pon_dev_pcs_attach(ctx->pdev, &tpcs->pcs),
			-EBUSY);
	pon_pcs_change(&tpcs->pcs, false);
	pon_pcs_change(&tpcs->pcs, true);
	KUNIT_EXPECT_EQ(test, tpcs->state_reads, 1);
	KUNIT_EXPECT_EQ(test, ctx->pcs_calls, 1);
}

static struct kunit_case pon_pcs_test_cases[] = {
	KUNIT_CASE(pon_pcs_register_test),
	KUNIT_CASE(pon_pcs_lookup_none_test),
	KUNIT_CASE(pon_pcs_lookup_test),
	KUNIT_CASE(pon_pcs_attach_twice_test),
	KUNIT_CASE(pon_pcs_change_unattached_test),
	KUNIT_CASE(pon_pcs_loss_latch_test),
	KUNIT_CASE(pon_pcs_repeat_test),
	KUNIT_CASE(pon_pcs_lods_test),
	KUNIT_CASE(pon_pcs_fec_test),
	KUNIT_CASE(pon_pcs_enable_test),
	KUNIT_CASE(pon_pcs_disabled_test),
	KUNIT_CASE(pon_pcs_disabled_loss_test),
	KUNIT_CASE(pon_pcs_detach_test),
	{}
};

static struct kunit_suite pon_pcs_test_suite = {
	.name = "pon_pcs",
	.init = t_pcs_init,
	.exit = t_pcs_exit,
	.test_cases = pon_pcs_test_cases,
};

kunit_test_suite(pon_pcs_test_suite);
