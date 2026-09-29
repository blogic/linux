// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <net/pon.h>
#include <net/rtnetlink.h>

#include "pon_test.h"

#define T_S(x)			BIT(PON_PLOAM_STATE_##x)
#define T_S_ANYWHERE		(T_S(O1) | T_S(O7))

#define T_DETACH_WAIT_MS	100
#define T_FEC_START		0xc0000000
#define T_FEC_STEP		0x60000000
#define T_FEC_FOLDS		3

static const u32 t_state_legal[] = {
	[PON_PLOAM_STATE_UNKNOWN] = T_S_ANYWHERE | T_S(O2) | T_S(O3) |
				    T_S(O4) | T_S(O5) | T_S(O6),
	[PON_PLOAM_STATE_O1]	  = T_S(O2) | T_S(O7),
	[PON_PLOAM_STATE_O2]	  = T_S_ANYWHERE | T_S(O3) | T_S(O4),
	[PON_PLOAM_STATE_O3]	  = T_S_ANYWHERE | T_S(O4),
	[PON_PLOAM_STATE_O4]	  = T_S_ANYWHERE | T_S(O2) | T_S(O5),
	[PON_PLOAM_STATE_O5]	  = T_S_ANYWHERE | T_S(O6),
	[PON_PLOAM_STATE_O6]	  = T_S(O1) | T_S(O5),
	[PON_PLOAM_STATE_O7]	  = T_S(O1),
};

enum t_create_flaw {
	T_CREATE_NO_NETDEV,
	T_CREATE_NO_PARENT,
	T_CREATE_NO_OPS,
	T_CREATE_NO_CAPS,
	T_CREATE_NO_TSTATS,
	T_CREATE_NETNS_MUTABLE,
	T_CREATE_NO_TCONT_SET,
	T_CREATE_NO_TCONT_CLEAR,
	T_CREATE_NO_GEM_ADD,
	T_CREATE_NO_GEM_DEL,
	T_CREATE_NO_GEM_STATS,
	T_CREATE_NO_OMCI_XMIT,
	T_CREATE_FLAWS,
};

static struct pon_dev *t_create_flawed(struct kunit *test,
				       struct net_device *netdev,
				       enum t_create_flaw flaw)
{
	struct pon_test_ctx *ctx = test->priv;
	const struct pon_dev_caps *caps = &pon_test_caps;
	struct device *parent = ctx->parent;
	struct pon_dev_ops *ops;
	struct pon_dev *pdev;

	ops = kunit_kmalloc(test, sizeof(*ops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ops);
	*ops = pon_test_ops;

	switch (flaw) {
	case T_CREATE_NO_NETDEV:
		netdev = NULL;
		break;
	case T_CREATE_NO_PARENT:
		parent = NULL;
		break;
	case T_CREATE_NO_OPS:
		ops = NULL;
		break;
	case T_CREATE_NO_CAPS:
		caps = NULL;
		break;
	case T_CREATE_NO_TSTATS:
		netdev->pcpu_stat_type = NETDEV_PCPU_STAT_NONE;
		break;
	case T_CREATE_NETNS_MUTABLE:
		netdev->netns_immutable = false;
		break;
	case T_CREATE_NO_TCONT_SET:
		ops->tcont_set = NULL;
		break;
	case T_CREATE_NO_TCONT_CLEAR:
		ops->tcont_clear = NULL;
		break;
	case T_CREATE_NO_GEM_ADD:
		ops->gem_add = NULL;
		break;
	case T_CREATE_NO_GEM_DEL:
		ops->gem_del = NULL;
		break;
	case T_CREATE_NO_GEM_STATS:
		ops->gem_stats = NULL;
		break;
	case T_CREATE_NO_OMCI_XMIT:
		ops->omci_xmit = NULL;
		break;
	case T_CREATE_FLAWS:
		break;
	}

	kunit_warning_suppress(test) {
		pdev = pon_dev_create(netdev, parent, ops, caps,
				      PON_MODE_XGS_PON, ctx);
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}

	return pdev;
}

static void pon_dev_create_refused_test(struct kunit *test)
{
	enum t_create_flaw flaw;
	struct net_device *netdev;
	struct pon_dev *pdev;

	netdev = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(test, netdev);

	for (flaw = T_CREATE_NO_NETDEV; flaw < T_CREATE_FLAWS; flaw++) {
		pdev = t_create_flawed(test, netdev, flaw);
		KUNIT_EXPECT_EQ_MSG(test, PTR_ERR_OR_ZERO(pdev), -EINVAL,
				    "flaw %d", flaw);
		if (!IS_ERR_OR_NULL(pdev)) {
			pon_dev_unregister(pdev);
			pon_dev_put(pdev);
		}
		netdev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;
		netdev->netns_immutable = true;
		KUNIT_EXPECT_NULL(test, rcu_access_pointer(netdev->pon_dev));
	}

	pon_test_netdev_destroy(netdev);
}

static void pon_state_edge_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 from, to;

	for (from = PON_PLOAM_STATE_UNKNOWN; from <= PON_PLOAM_STATE_O7;
	     from++) {
		for (to = PON_PLOAM_STATE_O1; to <= PON_PLOAM_STATE_O7; to++) {
			bool legal = t_state_legal[from] & BIT(to);

			if (from == to)
				continue;

			pon_test_state_report(ctx, from);
			KUNIT_EXPECT_EQ_MSG(test,
					    pon_test_state_report(ctx, to),
					    legal ? 0 : -EINVAL,
					    "O%u to O%u", from, to);
			KUNIT_EXPECT_EQ_MSG(test, pon_test_state(ctx), to,
					    "O%u to O%u", from, to);
		}
	}
}

static void pon_state_repeat_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test,
			pon_test_state_report(ctx, PON_PLOAM_STATE_O1), 0);
	KUNIT_EXPECT_EQ(test,
			pon_test_state_report(ctx, PON_PLOAM_STATE_O1), 0);
	KUNIT_EXPECT_EQ(test, pon_test_state(ctx), PON_PLOAM_STATE_O1);
}

static void pon_state_after_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_activate(ctx);
	pon_dev_unregister(ctx->pdev);
	ctx->unregistered = true;

	KUNIT_EXPECT_EQ(test,
			pon_test_state_report(ctx, PON_PLOAM_STATE_O1),
			-ENODEV);
	KUNIT_EXPECT_EQ(test, pon_test_state(ctx), PON_PLOAM_STATE_O5);
}

static void pon_state_range_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test,
			pon_test_state_report(ctx, PON_PLOAM_STATE_O1), 0);
	KUNIT_EXPECT_EQ(test,
			pon_test_state_report(ctx, PON_PLOAM_STATE_O7 + 1),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, pon_test_state(ctx), PON_PLOAM_STATE_O1);
}

static void t_fec_read(struct pon_test_ctx *ctx, u32 corrected,
		       u32 uncorrectable,
		       struct ethtool_fec_stats *stats)
{
	ctx->fec.corrected_codewords = corrected;
	ctx->fec.uncorrectable_codewords = uncorrectable;

	stats->corrected_blocks.total = ETHTOOL_STAT_NOT_SET;
	stats->uncorrectable_blocks.total = ETHTOOL_STAT_NOT_SET;
	pon_dev_fec_stats(ctx->pdev, stats);
}

static void pon_fec_baseline_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fec_stats stats;

	t_fec_read(ctx, 1000, 10, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total, 0);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total, 0);

	t_fec_read(ctx, 1500, 12, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total, 500);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total, 2);
}

static void pon_fec_wrap_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fec_stats stats;

	t_fec_read(ctx, 0xfffffff0, 0xffffffff, &stats);
	t_fec_read(ctx, 0x00000010, 0x00000000, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total, 0x20);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total, 1);
}

static void pon_fec_edge_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fec_stats stats;

	pon_test_activate(ctx);
	t_fec_read(ctx, 1000, 10, &stats);
	t_fec_read(ctx, 1500, 12, &stats);

	KUNIT_EXPECT_EQ(test,
			pon_test_state_report(ctx, PON_PLOAM_STATE_O1), 0);

	t_fec_read(ctx, 1600, 13, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total, 600);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total, 3);
}

static void t_fec_fold(struct pon_test_ctx *ctx, u32 codewords)
{
	ctx->fec.corrected_codewords = codewords;
	ctx->fec.total_codewords = codewords;

	pon_delayed_work_queue(ctx->pdev, &ctx->pdev->fec_work, 0);
	flush_workqueue(ctx->pdev->wq);
}

static void pon_fec_fold_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fec_stats stats;
	u32 codewords = T_FEC_START;
	unsigned int i;

	KUNIT_EXPECT_TRUE(test, timer_pending(&ctx->pdev->fec_work.timer));

	t_fec_fold(ctx, codewords);
	for (i = 0; i < T_FEC_FOLDS; i++) {
		codewords += T_FEC_STEP;
		t_fec_fold(ctx, codewords);
	}

	KUNIT_EXPECT_TRUE(test, timer_pending(&ctx->pdev->fec_work.timer));

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, ctx->pdev->fec.total_codewords,
			(u64)T_FEC_STEP * T_FEC_FOLDS);
	mutex_unlock(&ctx->pdev->lock);

	t_fec_read(ctx, codewords, 0, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total,
			(u64)T_FEC_STEP * T_FEC_FOLDS);
}

static void pon_fec_error_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct ethtool_fec_stats stats;

	ctx->fec_err = -EIO;
	t_fec_read(ctx, 1000, 10, &stats);
	KUNIT_EXPECT_EQ(test, stats.corrected_blocks.total,
			ETHTOOL_STAT_NOT_SET);
	KUNIT_EXPECT_EQ(test, stats.uncorrectable_blocks.total,
			ETHTOOL_STAT_NOT_SET);
}

static void pon_link_o5_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_map_new(test, T_GEM_ID);

	pon_test_activate(ctx);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);
	KUNIT_EXPECT_EQ(test, ctx->map_sets, 0);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);
	KUNIT_EXPECT_EQ(test, ctx->map_sets, 0);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
}

static void pon_link_o1_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_map_new(test, T_GEM_ID);
	pon_test_activate(ctx);
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O6);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));

	pon_test_alloc_drop_all(ctx);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, list_empty(&ctx->pdev->tconts));
	KUNIT_EXPECT_FALSE(test, list_empty(&ctx->pdev->gems));
	KUNIT_EXPECT_FALSE(test, list_empty(&ctx->pdev->gem_maps));
	mutex_unlock(&ctx->pdev->lock);

	pon_test_activate(ctx);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
}

static void pon_link_default_alloc_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	pon_test_tcont_new(test, 0, T_ONU_ID);
	pon_test_gem_new(test, T_GEM_ID, 0, T_ONU_ID);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O2);
	pon_test_alloc_bind(ctx, T_ONU_ID);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O4);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	pon_test_state_report(ctx, PON_PLOAM_STATE_O5);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
}

static void pon_link_no_channel_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	mutex_lock(&ctx->pdev->lock);
	ctx->pdev->ops = &pon_test_ops_no_channel;
	mutex_unlock(&ctx->pdev->lock);

	pon_test_activate(ctx);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	mutex_lock(&ctx->pdev->lock);
	pon_dev_carrier_update(ctx->pdev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_EQ(test, ctx->channel_reads, 0);

	mutex_lock(&ctx->pdev->lock);
	ctx->pdev->ops = &pon_test_ops;
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_link_dealloc_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_up(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	mutex_lock(&ctx->pdev->lock);
	tcont->ets_set = true;
	mutex_unlock(&ctx->pdev->lock);
	pon_test_activate(ctx);
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));

	pon_test_alloc_release(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, tcont->ets_pending);
	mutex_unlock(&ctx->pdev->lock);

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, tcont->ets_pending);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_link_no_gem_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_activate(ctx);
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
}

static void pon_ets_pending_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	mutex_lock(&ctx->pdev->lock);
	tcont->ets_set = true;
	tcont->ets_pending = true;
	mutex_unlock(&ctx->pdev->lock);
	pon_test_activate(ctx);

	pon_test_alloc_assign(ctx, T_OTHER_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->channel_reads, 0);

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->channel_reads, 1);
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, tcont->ets_pending);
	mutex_unlock(&ctx->pdev->lock);

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->channel_reads, 1);
}

static void pon_tcont_in_use_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_tcont_new(test, T_OTHER_TCONT_INDEX, T_OTHER_ALLOC_ID);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, pon_tcont_in_use(ctx->pdev, T_TCONT_INDEX));
	mutex_unlock(&ctx->pdev->lock);

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, pon_tcont_in_use(ctx->pdev, T_TCONT_INDEX));
	KUNIT_EXPECT_FALSE(test,
			   pon_tcont_in_use(ctx->pdev, T_OTHER_TCONT_INDEX));
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_gems_full_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	unsigned int i, max;

	max = ctx->pdev->caps->max_gems;
	for (i = 0; i < max - 1; i++)
		pon_test_gem_new(test, T_GEM_ID + i, T_TCONT_INDEX,
				 T_TCONT_ALLOC_ID);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, pon_gems_full(ctx->pdev));
	mutex_unlock(&ctx->pdev->lock);

	pon_test_gem_new(test, T_GEM_ID + i, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, pon_gems_full(ctx->pdev));
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_tcont_alloc_taken_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, pon_tcont_alloc_taken(ctx->pdev, T_TCONT_INDEX,
						       T_TCONT_ALLOC_ID));
	KUNIT_EXPECT_TRUE(test, pon_tcont_alloc_taken(ctx->pdev,
						      T_OTHER_TCONT_INDEX,
						      T_TCONT_ALLOC_ID));
	KUNIT_EXPECT_FALSE(test, pon_tcont_alloc_taken(ctx->pdev,
						       T_OTHER_TCONT_INDEX,
						       T_OTHER_ALLOC_ID));
	mutex_unlock(&ctx->pdev->lock);
}

static void t_rebind_gems_new(struct kunit *test)
{
	unsigned int i;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	for (i = 0; i < T_GEM_COUNT - 1; i++)
		pon_test_gem_new(test, T_GEM_ID + i, T_TCONT_INDEX,
				 T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID + i, T_OTHER_TCONT_INDEX,
			 T_TCONT_ALLOC_ID);
}

static void t_gem_alloc_id_expect(struct kunit *test, u16 gem_id, u16 alloc_id)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem *gem;

	mutex_lock(&ctx->pdev->lock);
	gem = pon_gem_find(ctx->pdev, gem_id);
	KUNIT_EXPECT_NOT_NULL(test, gem);
	if (gem)
		KUNIT_EXPECT_EQ_MSG(test, gem->cfg.alloc_id, alloc_id,
				    "GEM %u", gem_id);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_tcont_rebind_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	int err;

	t_rebind_gems_new(test);

	mutex_lock(&ctx->pdev->lock);
	err = pon_tcont_gems_rebind(ctx->pdev, T_TCONT_INDEX, T_TCONT_ALLOC_ID,
				    T_OTHER_ALLOC_ID, NULL);
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 2);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[0], T_OTHER_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[1], T_OTHER_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[2], 0);
	t_gem_alloc_id_expect(test, T_GEM_ID, T_OTHER_ALLOC_ID);
	t_gem_alloc_id_expect(test, T_GEM_ID + 1, T_OTHER_ALLOC_ID);
	t_gem_alloc_id_expect(test, T_GEM_ID + 2, T_TCONT_ALLOC_ID);
}

static void pon_tcont_rebind_restore_test(struct kunit *test)
{
	struct netlink_ext_ack extack = {};
	struct pon_test_ctx *ctx = test->priv;
	int err;

	t_rebind_gems_new(test);
	ctx->gem_fail_id = T_GEM_ID + 1;

	mutex_lock(&ctx->pdev->lock);
	err = pon_tcont_gems_rebind(ctx->pdev, T_TCONT_INDEX, T_TCONT_ALLOC_ID,
				    T_OTHER_ALLOC_ID, &extack);
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_NOT_NULL(test, extack._msg);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 4);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[0], T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[1], T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[2], 0);
	t_gem_alloc_id_expect(test, T_GEM_ID, T_TCONT_ALLOC_ID);
	t_gem_alloc_id_expect(test, T_GEM_ID + 1, T_TCONT_ALLOC_ID);
	t_gem_alloc_id_expect(test, T_GEM_ID + 2, T_TCONT_ALLOC_ID);
}

static void t_unregister_worker(struct work_struct *work)
{
	struct pon_test_ctx *ctx =
		container_of(work, struct pon_test_ctx, unregister_work);

	pon_dev_unregister(ctx->pdev);
}

static bool t_going_away(struct pon_dev *pdev)
{
	return READ_ONCE(pdev->going_away);
}

static int t_gem_link_race_new(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(&ctx->pdev->gem_netdevs, T_GEM_ID),
			    (void *)dev);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 2);

	INIT_WORK(&ctx->unregister_work, t_unregister_worker);
	ctx->unregistered = true;

	return dev->ifindex;
}

static void t_gem_link_race_end(struct kunit *test, int ifindex)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net *net = dev_net(ctx->netdev);

	flush_work(&ctx->unregister_work);

	KUNIT_EXPECT_NULL(test, xa_load(&ctx->pdev->gem_netdevs, T_GEM_ID));
	rtnl_lock();
	KUNIT_EXPECT_NULL(test, __dev_get_by_index(net, ifindex));
	rtnl_unlock();
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 1);
}

static void pon_gem_link_del_race_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net *net = dev_net(ctx->netdev);
	struct net_device *dev;
	void *entry;
	int ifindex;
	int err;

	ifindex = t_gem_link_race_new(test);
	schedule_work(&ctx->unregister_work);

	err = read_poll_timeout(xa_load, entry, !entry, USEC_PER_MSEC,
				T_DETACH_WAIT_MS * USEC_PER_MSEC, false,
				&ctx->pdev->gem_netdevs, T_GEM_ID);
	KUNIT_EXPECT_EQ(test, err, 0);

	rtnl_lock();
	dev = __dev_get_by_index(net, ifindex);
	KUNIT_EXPECT_NULL(test, dev);
	if (dev)
		KUNIT_EXPECT_EQ(test, rtnl_delete_link(dev, 0, NULL), 0);
	rtnl_unlock();

	t_gem_link_race_end(test, ifindex);
}

static void pon_gem_link_del_in_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net *net = dev_net(ctx->netdev);
	struct net_device *dev;
	int ifindex;
	bool away;
	int err;

	ifindex = t_gem_link_race_new(test);
	rtnl_lock();
	schedule_work(&ctx->unregister_work);

	err = read_poll_timeout(t_going_away, away, away, USEC_PER_MSEC,
				T_DETACH_WAIT_MS * USEC_PER_MSEC, false,
				ctx->pdev);
	KUNIT_EXPECT_EQ(test, err, 0);

	dev = __dev_get_by_index(net, ifindex);
	KUNIT_EXPECT_NOT_NULL(test, dev);
	if (dev)
		KUNIT_EXPECT_EQ(test, rtnl_delete_link(dev, 0, NULL), 0);
	rtnl_unlock();

	t_gem_link_race_end(test, ifindex);
}

static void pon_gem_link_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net *net = dev_net(ctx->netdev);
	int ifindex;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	ifindex = pon_test_gem_link_new(test, T_GEM_ID)->ifindex;

	ctx->unregistered = true;
	pon_dev_unregister(ctx->pdev);

	KUNIT_EXPECT_NULL(test, xa_load(&ctx->pdev->gem_netdevs, T_GEM_ID));
	rtnl_lock();
	KUNIT_EXPECT_NULL(test, __dev_get_by_index(net, ifindex));
	rtnl_unlock();
}

static struct kunit_case pon_dev_test_cases[] = {
	KUNIT_CASE(pon_dev_create_refused_test),
	KUNIT_CASE(pon_state_edge_test),
	KUNIT_CASE(pon_state_repeat_test),
	KUNIT_CASE(pon_state_after_unregister_test),
	KUNIT_CASE(pon_state_range_test),
	KUNIT_CASE(pon_fec_baseline_test),
	KUNIT_CASE(pon_fec_wrap_test),
	KUNIT_CASE(pon_fec_edge_test),
	KUNIT_CASE(pon_fec_fold_test),
	KUNIT_CASE(pon_fec_error_test),
	KUNIT_CASE(pon_link_o5_test),
	KUNIT_CASE(pon_link_o1_test),
	KUNIT_CASE(pon_link_default_alloc_test),
	KUNIT_CASE(pon_link_no_channel_op_test),
	KUNIT_CASE(pon_link_dealloc_test),
	KUNIT_CASE(pon_link_no_gem_test),
	KUNIT_CASE(pon_ets_pending_test),
	KUNIT_CASE(pon_tcont_in_use_test),
	KUNIT_CASE(pon_tcont_alloc_taken_test),
	KUNIT_CASE(pon_gems_full_test),
	KUNIT_CASE(pon_tcont_rebind_test),
	KUNIT_CASE(pon_tcont_rebind_restore_test),
	KUNIT_CASE(pon_gem_link_del_race_test),
	KUNIT_CASE(pon_gem_link_del_in_unregister_test),
	KUNIT_CASE(pon_gem_link_unregister_test),
	{}
};

static struct kunit_suite pon_dev_test_suite = {
	.name = "pon_dev",
	.init = pon_test_init,
	.exit = pon_test_exit,
	.test_cases = pon_dev_test_cases,
};

kunit_test_suite(pon_dev_test_suite);
