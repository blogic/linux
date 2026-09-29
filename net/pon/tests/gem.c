// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/test.h>
#include <linux/bits.h>
#include <linux/bottom_half.h>
#include <linux/etherdevice.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/refcount.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/pon.h>
#include <net/pon/ploam.h>
#include <net/rtnetlink.h>

#include "pon_test.h"

#define T_ATTR_LINK		BIT(0)
#define T_ATTR_DATA		BIT(1)
#define T_ATTR_GEM_ID		BIT(2)
#define T_ATTR_MTU		BIT(3)
#define T_ATTR_ALL		(T_ATTR_LINK | T_ATTR_DATA | T_ATTR_GEM_ID)

#define T_BAD_IFINDEX		INT_MAX
#define T_LOWER_MTU		1400
#define T_LOWER_MIN_MTU		1280
#define T_LOWER_MAX_MTU		9000
#define T_FRAME_LEN		128
#define T_RULE_VID		100
#define T_RULE_PBIT		5
#define T_RULE_DSCP		46

enum t_map_field {
	T_MAP_GEM_ID,
	T_MAP_TAG_VALID,
	T_MAP_TAGGED,
	T_MAP_VID_VALID,
	T_MAP_VID,
	T_MAP_PBIT_VALID,
	T_MAP_PBIT,
	T_MAP_DSCP_VALID,
	T_MAP_DSCP,
	T_MAP_FIELDS,
};

static int t_link_try(struct kunit *test, int ifindex, u32 gem_id, u32 mtu,
		      unsigned long attrs, struct net_device **devp)
{
	struct nlattr **data = NULL;
	struct nlattr **tb;

	tb = kunit_kcalloc(test, IFLA_MAX + 1, sizeof(*tb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tb);

	if (attrs & T_ATTR_LINK)
		tb[IFLA_LINK] = pon_test_nla_u32(test, IFLA_LINK, ifindex);
	if (attrs & T_ATTR_MTU)
		tb[IFLA_MTU] = pon_test_nla_u32(test, IFLA_MTU, mtu);

	if (attrs & T_ATTR_DATA) {
		data = kunit_kcalloc(test, IFLA_GEM_MAX + 1, sizeof(*data),
				     GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, data);
		if (attrs & T_ATTR_GEM_ID)
			data[IFLA_GEM_ID] = pon_test_nla_u32(test, IFLA_GEM_ID,
							     gem_id);
	}

	return pon_test_gem_link_try(test, tb, data, devp);
}

static void t_link_refused(struct kunit *test, int ifindex, u32 gem_id,
			   u32 mtu, unsigned long attrs, int err)
{
	struct pon_test_ctx *ctx = test->priv;
	unsigned int refs = refcount_read(&ctx->pdev->refcnt);
	struct net_device *dev;

	KUNIT_EXPECT_EQ(test,
			t_link_try(test, ifindex, gem_id, mtu, attrs, &dev),
			err);
	KUNIT_EXPECT_NULL(test, dev);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), refs);
}

static void t_lower_mtu_set(struct pon_test_ctx *ctx, unsigned int mtu,
			    unsigned int min_mtu, unsigned int max_mtu)
{
	rtnl_lock();
	WRITE_ONCE(ctx->netdev->mtu, mtu);
	ctx->netdev->min_mtu = min_mtu;
	ctx->netdev->max_mtu = max_mtu;
	rtnl_unlock();
}

static void pon_gem_newlink_no_link_test(struct kunit *test)
{
	t_link_refused(test, 0, T_GEM_ID, 0, T_ATTR_DATA | T_ATTR_GEM_ID,
		       -EINVAL);
}

static void pon_gem_newlink_no_gem_id_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	int ifindex = ctx->netdev->ifindex;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	t_link_refused(test, ifindex, T_GEM_ID, 0, T_ATTR_LINK, -EINVAL);
	t_link_refused(test, ifindex, T_GEM_ID, 0, T_ATTR_LINK | T_ATTR_DATA,
		       -EINVAL);
}

static void pon_gem_newlink_no_lower_test(struct kunit *test)
{
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	t_link_refused(test, T_BAD_IFINDEX, T_GEM_ID, 0, T_ATTR_ALL, -ENODEV);
}

static void pon_gem_newlink_not_pon_test(struct kunit *test)
{
	struct net_device *other;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	other = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(test, other);

	t_link_refused(test, other->ifindex, T_GEM_ID, 0, T_ATTR_ALL,
		       -EOPNOTSUPP);

	pon_test_netdev_destroy(other);
}

static void pon_gem_newlink_gem_lower_test(struct kunit *test)
{
	struct net_device *gem_dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID + 1, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	gem_dev = pon_test_gem_link_new(test, T_GEM_ID);

	t_link_refused(test, gem_dev->ifindex, T_GEM_ID + 1, 0, T_ATTR_ALL,
		       -EINVAL);

	pon_test_gem_link_del(gem_dev);
}

static void pon_gem_newlink_unregistered_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_unregister(ctx);

	t_link_refused(test, ctx->netdev->ifindex, T_GEM_ID, 0, T_ATTR_ALL,
		       -EOPNOTSUPP);
}

static void pon_gem_newlink_no_object_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	t_link_refused(test, ctx->netdev->ifindex, T_GEM_ID + 1, 0, T_ATTR_ALL,
		       -ENOENT);
}

static void pon_gem_newlink_busy_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	t_link_refused(test, ctx->netdev->ifindex, T_GEM_ID, 0, T_ATTR_ALL,
		       -EBUSY);

	pon_test_gem_link_del(dev);
}

static void pon_gem_newlink_mtu_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	int ifindex = ctx->netdev->ifindex;
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	t_lower_mtu_set(ctx, T_LOWER_MTU, T_LOWER_MIN_MTU, T_LOWER_MAX_MTU);

	t_link_refused(test, ifindex, T_GEM_ID, T_LOWER_MAX_MTU + 1,
		       T_ATTR_ALL | T_ATTR_MTU, -EINVAL);
	t_link_refused(test, ifindex, T_GEM_ID, T_LOWER_MIN_MTU - 1,
		       T_ATTR_ALL | T_ATTR_MTU, -EINVAL);

	KUNIT_ASSERT_EQ(test,
			t_link_try(test, ifindex, T_GEM_ID, T_LOWER_MAX_MTU,
				   T_ATTR_ALL | T_ATTR_MTU, &dev),
			0);
	KUNIT_EXPECT_EQ(test, dev->mtu, T_LOWER_MAX_MTU);
	pon_test_gem_link_del(dev);

	KUNIT_ASSERT_EQ(test,
			t_link_try(test, ifindex, T_GEM_ID, T_LOWER_MIN_MTU,
				   T_ATTR_ALL | T_ATTR_MTU, &dev),
			0);
	KUNIT_EXPECT_EQ(test, dev->mtu, T_LOWER_MIN_MTU);
	pon_test_gem_link_del(dev);
}

static void pon_gem_newlink_conduit_mtu_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	t_lower_mtu_set(ctx, ETH_DATA_LEN, T_LOWER_MIN_MTU, T_LOWER_MAX_MTU);
	ctx->conduit_mtu_err = -EIO;

	t_link_refused(test, ctx->netdev->ifindex, T_GEM_ID, T_LOWER_MAX_MTU,
		       T_ATTR_ALL | T_ATTR_MTU, -EIO);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu),
			PON_OMCI_MAX_LEN + PON_OMCI_MIC_LEN);
	KUNIT_EXPECT_NULL(test, xa_load(&ctx->pdev->gem_netdevs, T_GEM_ID));
}

static void pon_gem_newlink_inherit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	t_lower_mtu_set(ctx, T_LOWER_MTU, T_LOWER_MIN_MTU, T_LOWER_MAX_MTU);
	KUNIT_ASSERT_EQ(test, refcount_read(&ctx->pdev->refcnt), 1);

	dev = pon_test_gem_link_new(test, T_GEM_ID);

	KUNIT_EXPECT_EQ(test, dev->mtu, T_LOWER_MTU);
	KUNIT_EXPECT_EQ(test, dev->min_mtu, T_LOWER_MIN_MTU);
	KUNIT_EXPECT_EQ(test, dev->max_mtu, T_LOWER_MAX_MTU);
	KUNIT_EXPECT_TRUE(test, ether_addr_equal(dev->dev_addr,
						 ctx->netdev->dev_addr));
	KUNIT_EXPECT_PTR_EQ(test, dev->dev.parent, ctx->pdev->parent);
	KUNIT_EXPECT_EQ(test, dev_get_iflink(dev), ctx->netdev->ifindex);
	KUNIT_EXPECT_TRUE(test, dev->lltx);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 2);

	pon_test_gem_link_del(dev);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 1);
}

struct t_gem_watch {
	struct notifier_block nb;
	unsigned int events;
	u16 gem_id;
	int err;
};

static int t_gem_register_event(struct notifier_block *nb,
				unsigned long event, void *ptr)
{
	struct t_gem_watch *watch = container_of(nb, struct t_gem_watch, nb);
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (event != NETDEV_REGISTER || dev->rtnl_link_ops != &pon_gem_link_ops)
		return NOTIFY_DONE;

	watch->events++;
	watch->err = pon_gem_netdev_id(dev, &watch->gem_id);

	return NOTIFY_DONE;
}

static void pon_gem_newlink_register_id_test(struct kunit *test)
{
	struct t_gem_watch watch = {
		.nb.notifier_call = t_gem_register_event,
	};
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	KUNIT_ASSERT_EQ(test, register_netdevice_notifier(&watch.nb), 0);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	unregister_netdevice_notifier(&watch.nb);

	KUNIT_EXPECT_EQ(test, watch.events, 1);
	KUNIT_EXPECT_EQ(test, watch.err, 0);
	KUNIT_EXPECT_EQ(test, watch.gem_id, T_GEM_ID);

	pon_test_gem_link_del(dev);
}

static int t_gem_id_validate(struct kunit *test, u32 gem_id)
{
	struct nlattr *nla = pon_test_nla_u32(test, IFLA_GEM_ID, gem_id);

	return nla_validate(nla, nla_total_size(sizeof(u32)),
			    pon_gem_link_ops.maxtype, pon_gem_link_ops.policy,
			    NULL);
}

static void pon_gem_id_range_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			t_gem_id_validate(test,
					  PON_GEM_PORT_ID_ASSIGNABLE_MIN - 1),
			-ERANGE);
	KUNIT_EXPECT_EQ(test,
			t_gem_id_validate(test, PON_GEM_PORT_ID_ASSIGNABLE_MIN),
			0);
	KUNIT_EXPECT_EQ(test,
			t_gem_id_validate(test, PON_GEM_PORT_ID_ASSIGNABLE_MAX),
			0);
	KUNIT_EXPECT_EQ(test,
			t_gem_id_validate(test,
					  PON_GEM_PORT_ID_ASSIGNABLE_MAX + 1),
			-ERANGE);
}

static void pon_gem_dellink_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;
	struct pon_gem *gem;

	gem = pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_PTR_EQ(test, gem->gem_netdev, dev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(&ctx->pdev->gem_netdevs, T_GEM_ID),
			    (void *)dev);

	pon_test_gem_link_del(dev);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_NULL(test, gem->gem_netdev);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_NULL(test, xa_load(&ctx->pdev->gem_netdevs, T_GEM_ID));
}

static void t_xmit(struct kunit *test, struct net_device *dev)
{
	struct sk_buff *skb = pon_test_frame(test, T_FRAME_LEN);
	netdev_tx_t ret;

	local_bh_disable();
	ret = dev->netdev_ops->ndo_start_xmit(skb, dev);
	local_bh_enable();

	KUNIT_EXPECT_EQ(test, ret, NETDEV_TX_OK);
}

static void t_tx_expect(struct kunit *test, struct net_device *dev,
			u64 packets, u64 dropped, u64 errors)
{
	struct rtnl_link_stats64 stats;

	dev_get_stats(dev, &stats);
	KUNIT_EXPECT_EQ(test, stats.tx_packets, packets);
	KUNIT_EXPECT_EQ(test, stats.tx_bytes, packets * T_FRAME_LEN);
	KUNIT_EXPECT_EQ(test, stats.tx_dropped, dropped);
	KUNIT_EXPECT_EQ(test, stats.tx_errors, errors);
}

static struct net_device *t_xmit_dev_new(struct kunit *test)
{
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	return pon_test_gem_link_new(test, T_GEM_ID);
}

static void pon_gem_xmit_no_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev = t_xmit_dev_new(test);

	pon_test_ops_set(ctx, &pon_test_ops);
	t_xmit(test, dev);
	pon_test_ops_set(ctx, &pon_test_ops_full);

	KUNIT_EXPECT_EQ(test, ctx->gem_xmits, 0);
	t_tx_expect(test, dev, 0, 1, 0);

	pon_test_gem_link_del(dev);
}

static void pon_gem_xmit_no_ops_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev = t_xmit_dev_new(test);

	mutex_lock(&ctx->pdev->lock);
	WRITE_ONCE(ctx->pdev->ops, NULL);
	t_xmit(test, dev);
	WRITE_ONCE(ctx->pdev->ops, &pon_test_ops_full);
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_EXPECT_EQ(test, ctx->gem_xmits, 0);
	t_tx_expect(test, dev, 0, 1, 0);

	pon_test_gem_link_del(dev);
}

static void pon_gem_xmit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev = t_xmit_dev_new(test);

	t_xmit(test, dev);

	KUNIT_EXPECT_EQ(test, ctx->gem_xmits, 1);
	KUNIT_EXPECT_EQ(test, ctx->gem_xmit_id, T_GEM_ID);
	t_tx_expect(test, dev, 1, 0, 0);

	pon_test_gem_link_del(dev);
}

static void pon_gem_xmit_error_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev = t_xmit_dev_new(test);

	ctx->gem_xmit_err = -ENETDOWN;
	t_xmit(test, dev);

	KUNIT_EXPECT_EQ(test, ctx->gem_xmits, 1);
	t_tx_expect(test, dev, 0, 1, 0);

	pon_test_gem_link_del(dev);
}

static void t_map_base(struct pon_gem_map_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->gem_id = T_GEM_ID;
	cfg->tag_valid = true;
	cfg->tagged = true;
	cfg->vid_valid = true;
	cfg->vid = T_RULE_VID;
	cfg->pbit_valid = true;
	cfg->pbit = T_RULE_PBIT;
	cfg->dscp_valid = true;
	cfg->dscp = T_RULE_DSCP;
}

static void t_map_vary(struct pon_gem_map_cfg *cfg, enum t_map_field field)
{
	t_map_base(cfg);

	switch (field) {
	case T_MAP_GEM_ID:
		cfg->gem_id++;
		break;
	case T_MAP_TAG_VALID:
		cfg->tag_valid = false;
		break;
	case T_MAP_TAGGED:
		cfg->tagged = false;
		break;
	case T_MAP_VID_VALID:
		cfg->vid_valid = false;
		break;
	case T_MAP_VID:
		cfg->vid++;
		break;
	case T_MAP_PBIT_VALID:
		cfg->pbit_valid = false;
		break;
	case T_MAP_PBIT:
		cfg->pbit++;
		break;
	case T_MAP_DSCP_VALID:
		cfg->dscp_valid = false;
		break;
	case T_MAP_DSCP:
		cfg->dscp++;
		break;
	case T_MAP_FIELDS:
		break;
	}
}

static void pon_gem_map_same_test(struct kunit *test)
{
	struct pon_gem_map_cfg base, other;
	enum t_map_field field;

	t_map_base(&base);
	t_map_base(&other);
	KUNIT_EXPECT_TRUE(test, pon_gem_map_same(&base, &other));

	for (field = T_MAP_GEM_ID; field < T_MAP_FIELDS; field++) {
		t_map_vary(&other, field);
		KUNIT_EXPECT_FALSE_MSG(test, pon_gem_map_same(&base, &other),
				       "field %d", field);
		KUNIT_EXPECT_FALSE_MSG(test, pon_gem_map_same(&other, &base),
				       "field %d", field);
	}
}

static void pon_gem_map_find_test(struct kunit *test)
{
	struct pon_gem_map *maps[T_MAP_FIELDS + 1];
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem_map_cfg cfg;
	enum t_map_field field;

	for (field = T_MAP_GEM_ID; field <= T_MAP_FIELDS; field++) {
		t_map_vary(&cfg, field);
		maps[field] = pon_test_gem_map_add(test, &cfg);
	}

	mutex_lock(&ctx->pdev->lock);
	for (field = T_MAP_GEM_ID; field <= T_MAP_FIELDS; field++) {
		t_map_vary(&cfg, field);
		KUNIT_EXPECT_PTR_EQ_MSG(test, pon_gem_map_find(ctx->pdev, &cfg),
					maps[field], "field %d", field);
	}

	t_map_base(&cfg);
	cfg.gem_id += 2;
	KUNIT_EXPECT_NULL(test, pon_gem_map_find(ctx->pdev, &cfg));
	mutex_unlock(&ctx->pdev->lock);
}

static struct kunit_case pon_gem_test_cases[] = {
	KUNIT_CASE(pon_gem_newlink_no_link_test),
	KUNIT_CASE(pon_gem_newlink_no_gem_id_test),
	KUNIT_CASE(pon_gem_newlink_no_lower_test),
	KUNIT_CASE(pon_gem_newlink_not_pon_test),
	KUNIT_CASE(pon_gem_newlink_gem_lower_test),
	KUNIT_CASE(pon_gem_newlink_unregistered_test),
	KUNIT_CASE(pon_gem_newlink_no_object_test),
	KUNIT_CASE(pon_gem_newlink_busy_test),
	KUNIT_CASE(pon_gem_newlink_mtu_test),
	KUNIT_CASE(pon_gem_newlink_conduit_mtu_test),
	KUNIT_CASE(pon_gem_newlink_inherit_test),
	KUNIT_CASE(pon_gem_newlink_register_id_test),
	KUNIT_CASE(pon_gem_id_range_test),
	KUNIT_CASE(pon_gem_dellink_test),
	KUNIT_CASE(pon_gem_xmit_no_op_test),
	KUNIT_CASE(pon_gem_xmit_no_ops_test),
	KUNIT_CASE(pon_gem_xmit_test),
	KUNIT_CASE(pon_gem_xmit_error_test),
	KUNIT_CASE(pon_gem_map_same_test),
	KUNIT_CASE(pon_gem_map_find_test),
	{}
};

static struct kunit_suite pon_gem_test_suite = {
	.name = "pon_gem",
	.init = pon_test_init_full,
	.exit = pon_test_exit,
	.test_cases = pon_gem_test_cases,
};

kunit_test_suite(pon_gem_test_suite);
