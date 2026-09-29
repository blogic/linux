// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/bottom_half.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/pkt_sched.h>
#include <linux/property.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <net/flow_offload.h>
#include <net/pkt_cls.h>
#include <net/pon.h>

#include "pon_test.h"

#define T_FRAME_LEN		128
#define T_CONDUIT_MTU_MIN	(PON_OMCI_MAX_LEN + PON_OMCI_MIC_LEN)
#define T_MTU_MAIN		2500
#define T_MTU_GEM		3000
#define T_ETS_BANDS		3
#define T_ETS_QUANTUM		1514
#define T_ETS_MAJOR		0x10000
#define T_GEM_QUEUE		5
#define T_KEY_VID		100
#define T_KEY_PBIT		3
#define T_KEY_DSCP		46

struct t_addr_watch {
	struct notifier_block nb;
	struct net_device *dev;
	unsigned int events;
};

static const struct software_node t_mac_node = {
	.name = "pon-test-mac-2",
};

static const struct property_entry t_conduit_props[] = {
	PROPERTY_ENTRY_REF("pon-handle", &t_mac_node),
	{}
};

static const struct software_node t_conduit_node = {
	.name = "pon-test-conduit-2",
	.properties = t_conduit_props,
};

static const struct software_node t_bare_node = {
	.name = "pon-test-bare",
};

static netdev_tx_t t_conduit_xmit(struct net_device *conduit,
				  struct sk_buff *skb,
				  const struct pon_tx_info *info)
{
	kfree_skb(skb);

	return NETDEV_TX_OK;
}

static const struct pon_conduit_ops t_conduit_ops = {
	.xmit	= t_conduit_xmit,
};

static struct pon_dev *t_pdev_new(struct kunit *test, struct device *parent,
				  struct net_device **netdevp)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *netdev;
	struct pon_dev *pdev;

	netdev = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(test, netdev);

	pdev = pon_dev_create(netdev, parent, &pon_test_ops, &pon_test_caps,
			      PON_MODE_XGS_PON, ctx);
	if (IS_ERR(pdev))
		pon_test_netdev_destroy(netdev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	*netdevp = netdev;

	return pdev;
}

static void t_pdev_del(struct pon_dev *pdev, struct net_device *netdev)
{
	pon_dev_unregister(pdev);
	pon_test_netdev_destroy(netdev);
	pon_dev_put(pdev);
}

static void t_paired_expect(struct kunit *test, struct pon_dev *pdev,
			    struct net_device *conduit)
{
	KUNIT_EXPECT_NOT_NULL(test, rcu_access_pointer(pdev->conduit));
	KUNIT_EXPECT_PTR_EQ(test, rcu_access_pointer(conduit->pon_dev), pdev);
}

static void t_unpaired_expect(struct kunit *test, struct pon_dev *pdev,
			      struct net_device *conduit)
{
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(pdev->conduit));
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(conduit->pon_dev));
}

static int t_rx(struct net_device *dev, struct sk_buff *skb,
		const struct pon_rx_info *info)
{
	int ret;

	local_bh_disable();
	ret = pon_conduit_rx(dev, skb, info);
	local_bh_enable();

	return ret;
}

static void t_rx_stats(struct net_device *dev,
		       struct rtnl_link_stats64 *stats)
{
	memset(stats, 0, sizeof(*stats));
	dev_fetch_sw_netstats(stats, dev->tstats);
}

static void t_dev_open(struct kunit *test, struct net_device *dev)
{
	int err;

	rtnl_lock();
	err = dev_open(dev, NULL);
	rtnl_unlock();

	KUNIT_ASSERT_EQ(test, err, 0);
}

static void pon_rx_omci_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_rx_info info = {
		.oam = true,
	};
	struct rtnl_link_stats64 stats;

	KUNIT_EXPECT_EQ(test,
			t_rx(ctx->netdev,
			     pon_test_frame(test, T_OMCI_BASELINE_LEN), &info),
			0);

	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_dropped), 1);
	t_rx_stats(ctx->netdev, &stats);
	KUNIT_EXPECT_EQ(test, stats.rx_packets, 0);
}

static void pon_rx_gem_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_rx_info info = {
		.gem = T_GEM_ID,
	};
	struct rtnl_link_stats64 stats;
	struct net_device *dev;
	struct sk_buff *skb;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	t_dev_open(test, dev);

	skb = pon_test_frame(test, T_FRAME_LEN);
	KUNIT_EXPECT_EQ(test, t_rx(ctx->netdev, skb, &info), 1);
	KUNIT_EXPECT_PTR_EQ(test, skb->dev, dev);
	KUNIT_EXPECT_EQ(test, skb->protocol, htons(ETH_P_IP));
	kfree_skb(skb);

	t_rx_stats(dev, &stats);
	KUNIT_EXPECT_EQ(test, stats.rx_packets, 1);
	KUNIT_EXPECT_EQ(test, stats.rx_bytes, T_FRAME_LEN);
	t_rx_stats(ctx->netdev, &stats);
	KUNIT_EXPECT_EQ(test, stats.rx_packets, 0);
}

static void pon_rx_unknown_gem_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_rx_info info = {
		.gem = T_GEM_ID,
	};
	struct rtnl_link_stats64 stats;
	struct sk_buff *skb;

	t_dev_open(test, ctx->netdev);

	skb = pon_test_frame(test, T_FRAME_LEN);
	KUNIT_EXPECT_EQ(test, t_rx(ctx->netdev, skb, &info), 1);
	KUNIT_EXPECT_PTR_EQ(test, skb->dev, ctx->netdev);
	kfree_skb(skb);

	t_rx_stats(ctx->netdev, &stats);
	KUNIT_EXPECT_EQ(test, stats.rx_packets, 1);
	KUNIT_EXPECT_EQ(test, stats.rx_bytes, T_FRAME_LEN);
}

static void pon_rx_down_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_rx_info info = {
		.gem = T_GEM_ID,
	};
	struct rtnl_link_stats64 stats;
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	t_dev_open(test, ctx->netdev);

	KUNIT_EXPECT_EQ(test,
			t_rx(ctx->netdev, pon_test_frame(test, T_FRAME_LEN),
			     &info),
			0);
	dev_get_stats(dev, &stats);
	KUNIT_EXPECT_EQ(test, stats.rx_packets, 0);
	KUNIT_EXPECT_EQ(test, stats.rx_dropped, 1);

	rtnl_lock();
	dev_close(ctx->netdev);
	rtnl_unlock();

	info.gem = T_GEM_ID + 1;
	KUNIT_EXPECT_EQ(test,
			t_rx(ctx->netdev, pon_test_frame(test, T_FRAME_LEN),
			     &info),
			0);
	dev_get_stats(ctx->netdev, &stats);
	KUNIT_EXPECT_EQ(test, stats.rx_packets, 0);
	KUNIT_EXPECT_EQ(test, stats.rx_dropped, 1);
}

static void pon_rx_not_pon_test(struct kunit *test)
{
	struct pon_rx_info info = {
		.gem = T_GEM_ID,
	};
	struct net_device *dev;
	struct sk_buff *skb;

	dev = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(test, dev);

	skb = pon_test_frame(test, T_FRAME_LEN);
	KUNIT_EXPECT_EQ(test, t_rx(dev, skb, &info), -ENODEV);
	KUNIT_EXPECT_NULL(test, skb->dev);
	KUNIT_EXPECT_EQ(test, skb->len, T_FRAME_LEN);
	kfree_skb(skb);

	pon_test_netdev_destroy(dev);
}

static int t_xmit(struct kunit *test, const struct pon_tx_info *info)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;
	int ret;

	skb = skb_get(pon_test_frame(test, T_FRAME_LEN));
	ret = pon_conduit_xmit(ctx->pdev, skb, info);
	KUNIT_EXPECT_EQ(test, refcount_read(&skb->users), 1);
	kfree_skb(skb);

	return ret;
}

static void pon_xmit_no_conduit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tx_info info = {
		.gem = T_GEM_ID,
	};

	KUNIT_EXPECT_EQ(test, t_xmit(test, &info), -ENETDOWN);
	KUNIT_EXPECT_EQ(test, ctx->conduit_xmits, 0);
}

static void pon_xmit_info_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tx_info info = {
		.gem = T_GEM_ID,
		.channel = T_TCONT_CHANNEL,
		.queue = T_GEM_QUEUE,
		.mic_index = 1,
		.oam = true,
	};

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	KUNIT_EXPECT_EQ(test, t_xmit(test, &info), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_xmits, 1);
	KUNIT_EXPECT_EQ(test, ctx->conduit_tx_info.gem, info.gem);
	KUNIT_EXPECT_EQ(test, ctx->conduit_tx_info.channel, info.channel);
	KUNIT_EXPECT_EQ(test, ctx->conduit_tx_info.queue, info.queue);
	KUNIT_EXPECT_EQ(test, ctx->conduit_tx_info.mic_index, info.mic_index);
	KUNIT_EXPECT_EQ(test, ctx->conduit_tx_info.oam, info.oam);
}

static void pon_xmit_busy_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tx_info info = {
		.gem = T_GEM_ID,
	};

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	ctx->conduit_xmit_ret = NETDEV_TX_BUSY;

	KUNIT_EXPECT_EQ(test, t_xmit(test, &info), -EBUSY);
	KUNIT_EXPECT_EQ(test, ctx->conduit_xmits, 1);
}

static void pon_pair_conduit_later_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->pdev->conduit));

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	t_paired_expect(test, ctx->pdev, ctx->conduit);
}

static void pon_pair_pdev_later_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *conduit, *netdev;
	struct device *parent;
	struct pon_dev *pdev;

	parent = kunit_device_register(test, "pon-test-2");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);
	KUNIT_ASSERT_EQ(test, pon_test_mac_node_add(test, parent, &t_mac_node),
			0);

	conduit = pon_test_conduit_create(test, &t_conduit_node);
	KUNIT_ASSERT_NOT_NULL(test, conduit);
	KUNIT_EXPECT_EQ(test, pon_conduit_register(conduit, &t_conduit_ops), 0);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(conduit->pon_dev));
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->pdev->conduit));

	pdev = t_pdev_new(test, parent, &netdev);
	t_paired_expect(test, pdev, conduit);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->pdev->conduit));
	KUNIT_EXPECT_TRUE(test, ether_addr_equal(conduit->dev_addr,
						 netdev->dev_addr));
	KUNIT_EXPECT_EQ(test, READ_ONCE(conduit->mtu), T_CONDUIT_MTU_MIN);

	pon_test_conduit_destroy(conduit);
	t_pdev_del(pdev, netdev);
}

static void pon_pair_busy_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	KUNIT_EXPECT_EQ(test, pon_conduit_register(ctx->conduit,
						   &t_conduit_ops), -EBUSY);
	t_paired_expect(test, ctx->pdev, ctx->conduit);
}

static void pon_pair_no_handle_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *conduit;

	conduit = pon_test_conduit_create(test, NULL);
	KUNIT_ASSERT_NOT_NULL(test, conduit);
	KUNIT_EXPECT_EQ(test, pon_conduit_register(conduit, &t_conduit_ops),
			-ENOENT);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(conduit->pon_dev));
	pon_test_conduit_destroy(conduit);

	conduit = pon_test_conduit_create(test, &t_bare_node);
	KUNIT_ASSERT_NOT_NULL(test, conduit);
	KUNIT_EXPECT_EQ(test, pon_conduit_register(conduit, &t_conduit_ops),
			-ENOENT);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(conduit->pon_dev));
	pon_test_conduit_destroy(conduit);

	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->pdev->conduit));
}

static void pon_pair_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tx_info info = {};

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	pon_conduit_unregister(ctx->conduit);
	t_unpaired_expect(test, ctx->pdev, ctx->conduit);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, pon_dev_is_registered(ctx->pdev));
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, pon_test_state_report(ctx, PON_PLOAM_STATE_O1),
			0);
	KUNIT_EXPECT_EQ(test, t_xmit(test, &info), -ENETDOWN);
	KUNIT_EXPECT_EQ(test, ctx->conduit_xmits, 0);
}

static void pon_pair_pdev_again_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *netdev;
	struct pon_dev *pdev;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	pon_test_unregister(ctx);
	t_unpaired_expect(test, ctx->pdev, ctx->conduit);

	pdev = t_pdev_new(test, ctx->parent, &netdev);
	t_paired_expect(test, pdev, ctx->conduit);
	KUNIT_EXPECT_TRUE(test, ether_addr_equal(ctx->conduit->dev_addr,
						 netdev->dev_addr));

	t_pdev_del(pdev, netdev);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->conduit->pon_dev));
}

static void t_conduit_state_set(struct pon_test_ctx *ctx, bool up)
{
	rtnl_lock();
	if (up)
		KUNIT_EXPECT_EQ(ctx->test, dev_open(ctx->conduit, NULL), 0);
	else
		dev_close(ctx->conduit);
	rtnl_unlock();
}

static void pon_carrier_conduit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	pon_test_activate(ctx);
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));

	t_conduit_state_set(ctx, true);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(dev));

	t_conduit_state_set(ctx, false);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(dev));

	t_conduit_state_set(ctx, true);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));

	pon_conduit_unregister(ctx->conduit);
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(ctx->netdev));
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(dev));

	KUNIT_EXPECT_EQ(test,
			pon_conduit_register(ctx->conduit, &t_conduit_ops), 0);
	KUNIT_EXPECT_TRUE(test, netif_carrier_ok(ctx->netdev));
}

static int t_mtu_set(struct net_device *dev, int mtu)
{
	int err;

	rtnl_lock();
	err = dev_set_mtu(dev, mtu);
	rtnl_unlock();

	return err;
}

static void t_going_away_set(struct pon_test_ctx *ctx, bool away)
{
	mutex_lock(&ctx->pdev->lock);
	WRITE_ONCE(ctx->pdev->going_away, away);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_pair_sync_going_away_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_ASSERT_EQ(test, t_mtu_set(ctx->conduit, ETH_DATA_LEN), 0);
	KUNIT_ASSERT_EQ(test, ctx->conduit_mtu_sets, 2);

	t_going_away_set(ctx, true);
	pon_conduit_sync(ctx->pdev);
	t_going_away_set(ctx, false);

	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), ETH_DATA_LEN);
	KUNIT_EXPECT_EQ(test, ctx->conduit_mtu_sets, 2);
	KUNIT_EXPECT_EQ(test, ctx->conduit_addr_sets, 1);
}

static void pon_pair_sync_refused_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u8 addr[ETH_ALEN];

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_ASSERT_EQ(test, t_mtu_set(ctx->conduit, ETH_DATA_LEN), 0);
	eth_random_addr(addr);
	rtnl_lock();
	dev_addr_set(ctx->conduit, addr);
	rtnl_unlock();

	ctx->conduit_addr_err = -EBUSY;
	ctx->conduit_mtu_err = -EIO;
	pon_conduit_sync(ctx->pdev);

	KUNIT_EXPECT_EQ(test, ctx->conduit_addr_sets, 2);
	KUNIT_EXPECT_EQ(test, ctx->conduit_mtu_sets, 3);
	KUNIT_EXPECT_TRUE(test, ether_addr_equal(ctx->conduit->dev_addr, addr));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), ETH_DATA_LEN);
	t_paired_expect(test, ctx->pdev, ctx->conduit);
}

static void pon_pair_rebind_unregistered_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_unregister(ctx);

	pon_tc_rebind_sched(ctx->pdev);
	KUNIT_EXPECT_FALSE(test, work_pending(&ctx->pdev->tc_work));
	flush_work(&ctx->pdev->tc_work);
}

static void t_main_mtu_set(struct pon_test_ctx *ctx, int mtu)
{
	rtnl_lock();
	KUNIT_EXPECT_EQ(ctx->test, dev_set_mtu(ctx->netdev, mtu), 0);
	KUNIT_EXPECT_EQ(ctx->test, pon_conduit_mtu_set(ctx->pdev, NULL, 0), 0);
	rtnl_unlock();
}

static void pon_mtu_floor_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_CONDUIT_MTU_MIN);
	KUNIT_EXPECT_EQ(test, ctx->conduit_mtu_sets, 1);
}

static void pon_mtu_largest_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_CONDUIT_MTU_MIN);

	t_main_mtu_set(ctx, T_MTU_MAIN);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_MTU_MAIN);

	KUNIT_EXPECT_EQ(test, t_mtu_set(dev, T_MTU_GEM), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(dev->mtu), T_MTU_GEM);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_MTU_GEM);

	KUNIT_EXPECT_EQ(test, t_mtu_set(dev, ETH_DATA_LEN), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_MTU_MAIN);

	t_main_mtu_set(ctx, ETH_DATA_LEN);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_CONDUIT_MTU_MIN);
}

static void pon_mtu_refused_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	ctx->conduit_mtu_err = -EIO;

	KUNIT_EXPECT_EQ(test, t_mtu_set(dev, T_MTU_GEM), -EIO);
	KUNIT_EXPECT_EQ(test, READ_ONCE(dev->mtu), ETH_DATA_LEN);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->conduit->mtu), T_CONDUIT_MTU_MIN);
}

static void pon_addr_pair_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	KUNIT_EXPECT_TRUE(test, ether_addr_equal(ctx->conduit->dev_addr,
						 ctx->netdev->dev_addr));
	KUNIT_EXPECT_EQ(test, ctx->conduit_addr_sets, 1);
}

static int t_addr_event(struct notifier_block *nb, unsigned long event,
			void *ptr)
{
	struct t_addr_watch *watch = container_of(nb, struct t_addr_watch, nb);

	if (netdev_notifier_info_to_dev(ptr) != watch->dev)
		return NOTIFY_DONE;
	if (event == NETDEV_PRE_CHANGEADDR || event == NETDEV_CHANGEADDR)
		watch->events++;

	return NOTIFY_DONE;
}

static void pon_addr_equal_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct t_addr_watch watch = {
		.nb.notifier_call = t_addr_event,
	};

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_ASSERT_EQ(test, ctx->conduit_addr_sets, 1);

	watch.dev = ctx->conduit;
	KUNIT_ASSERT_EQ(test, register_netdevice_notifier(&watch.nb), 0);

	pon_conduit_unregister(ctx->conduit);
	KUNIT_EXPECT_EQ(test, pon_conduit_register(ctx->conduit,
						   &t_conduit_ops), 0);
	t_paired_expect(test, ctx->pdev, ctx->conduit);

	rtnl_lock();
	KUNIT_EXPECT_EQ(test, pon_conduit_addr_set(ctx->pdev,
						   ctx->netdev->dev_addr), 0);
	rtnl_unlock();

	unregister_netdevice_notifier(&watch.nb);

	KUNIT_EXPECT_EQ(test, ctx->conduit_addr_sets, 1);
	KUNIT_EXPECT_EQ(test, watch.events, 0);
}

static void t_ets_opt(struct tc_ets_qopt_offload *opt,
		      enum tc_ets_command command, u32 parent)
{
	unsigned int band;

	memset(opt, 0, sizeof(*opt));
	opt->command = command;
	opt->parent = parent;
	if (command != TC_ETS_REPLACE)
		return;

	opt->replace_params.bands = T_ETS_BANDS;
	for (band = 0; band < T_ETS_BANDS; band++) {
		opt->replace_params.quanta[band] = T_ETS_QUANTUM * (band + 1);
		opt->replace_params.weights[band] = band + 1;
	}
}

static int t_ets(struct pon_test_ctx *ctx, enum tc_ets_command command,
		 u32 parent)
{
	struct tc_ets_qopt_offload opt;
	int err;

	t_ets_opt(&opt, command, parent);

	rtnl_lock();
	err = pon_dev_setup_tc(ctx->pdev, TC_SETUP_QDISC_ETS, &opt);
	rtnl_unlock();

	return err;
}

static int t_ets_index(struct pon_test_ctx *ctx, enum tc_ets_command command,
		       u16 index)
{
	return t_ets(ctx, command, TC_H_MAKE(T_ETS_MAJOR, index + 1));
}

static int t_ets_tcont(struct pon_test_ctx *ctx, enum tc_ets_command command)
{
	return t_ets_index(ctx, command, T_TCONT_INDEX);
}

static void t_ets_call_expect(struct kunit *test, unsigned int call,
			      enum tc_ets_command command, unsigned int channel)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_LT(test, call, T_ETS_LOG);
	KUNIT_EXPECT_EQ_MSG(test, ctx->conduit_ets_log[call].command, command,
			    "call %u", call);
	KUNIT_EXPECT_EQ_MSG(test, ctx->conduit_ets_log[call].channel, channel,
			    "call %u", call);
}

static void
t_ets_params_expect(struct kunit *test,
		    const struct tc_ets_qopt_offload_replace_params *p)
{
	unsigned int band;

	KUNIT_EXPECT_EQ(test, p->bands, T_ETS_BANDS);
	for (band = 0; band < T_ETS_BANDS; band++) {
		KUNIT_EXPECT_EQ(test, p->quanta[band],
				T_ETS_QUANTUM * (band + 1));
		KUNIT_EXPECT_EQ(test, p->weights[band], band + 1);
	}
}

static void t_ets_state_expect(struct kunit *test, struct pon_tcont *tcont,
			       bool set, bool pending)
{
	struct pon_test_ctx *ctx = test->priv;

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, tcont->ets_set, set);
	KUNIT_EXPECT_EQ(test, tcont->ets_pending, pending);
	if (set)
		t_ets_params_expect(test, &tcont->ets);
	mutex_unlock(&ctx->pdev->lock);
}

static void t_conduit_ets_expect(struct kunit *test, unsigned int setup_tcs)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, setup_tcs);
	KUNIT_EXPECT_EQ(test, ctx->conduit_channel, T_TCONT_CHANNEL);
	KUNIT_EXPECT_EQ(test, ctx->conduit_tc_type, TC_SETUP_QDISC_ETS);
	KUNIT_EXPECT_EQ(test, ctx->conduit_ets_command, TC_ETS_REPLACE);
	t_ets_params_expect(test, &ctx->conduit_ets);
}

static void pon_ets_refused_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct tc_ets_qopt_offload opt;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);

	KUNIT_EXPECT_EQ(test, t_ets(ctx, TC_ETS_REPLACE, TC_H_ROOT),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_ets(ctx, TC_ETS_REPLACE,
				    TC_H_MAKE(T_ETS_MAJOR, 0)), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_GRAFT), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_ets(ctx, TC_ETS_REPLACE,
				    TC_H_MAKE(T_ETS_MAJOR,
					      T_OTHER_TCONT_INDEX + 1)),
			-ENOENT);

	t_ets_opt(&opt, TC_ETS_REPLACE,
		  TC_H_MAKE(T_ETS_MAJOR, T_TCONT_INDEX + 1));
	KUNIT_EXPECT_EQ(test, pon_dev_setup_tc(ctx->pdev, TC_SETUP_QDISC_TBF,
					       &opt), -EOPNOTSUPP);

	pon_test_ops_set(ctx, &pon_test_ops_no_channel);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), -EOPNOTSUPP);
	pon_test_ops_set(ctx, &pon_test_ops);

	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 0);
	t_ets_state_expect(test, tcont, false, false);
}

static void pon_ets_after_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	pon_test_unregister(ctx);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_DESTROY), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 0);
}

static void pon_ets_unbound_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	t_ets_state_expect(test, tcont, true, true);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_DESTROY), 0);
	t_ets_state_expect(test, tcont, false, false);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 0);

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	t_conduit_ets_expect(test, 1);

	pon_test_alloc_release(ctx, T_TCONT_ALLOC_ID);
	t_ets_state_expect(test, tcont, true, true);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_DESTROY), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 2);
	t_ets_call_expect(test, 1, TC_ETS_DESTROY, T_TCONT_CHANNEL);
	t_ets_state_expect(test, tcont, false, false);
}

static void t_tcont_move(struct pon_test_ctx *ctx, struct pon_tcont *tcont,
			 u16 alloc_id)
{
	mutex_lock(&ctx->pdev->lock);
	tcont->cfg.alloc_id = alloc_id;
	tcont->ets_pending = true;
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_ets_moved_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_OTHER_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);

	t_tcont_move(ctx, tcont, T_OTHER_ALLOC_ID);
	pon_tc_rebind_sched(ctx->pdev);
	flush_work(&ctx->pdev->tc_work);

	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 3);
	t_ets_call_expect(test, 0, TC_ETS_REPLACE, T_TCONT_CHANNEL);
	t_ets_call_expect(test, 1, TC_ETS_DESTROY, T_TCONT_CHANNEL);
	t_ets_call_expect(test, 2, TC_ETS_REPLACE, T_OTHER_CHANNEL);
	t_ets_state_expect(test, tcont, true, false);
}

static void pon_ets_swapped_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont, *other;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	other = pon_test_tcont_new(test, T_OTHER_TCONT_INDEX, T_OTHER_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_OTHER_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	KUNIT_EXPECT_EQ(test,
			t_ets_index(ctx, TC_ETS_REPLACE, T_OTHER_TCONT_INDEX),
			0);

	t_tcont_move(ctx, tcont, T_OTHER_ALLOC_ID);
	t_tcont_move(ctx, other, T_TCONT_ALLOC_ID);
	pon_tc_rebind_sched(ctx->pdev);
	flush_work(&ctx->pdev->tc_work);

	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 5);
	t_ets_call_expect(test, 2, TC_ETS_DESTROY, T_TCONT_CHANNEL);
	t_ets_call_expect(test, 3, TC_ETS_REPLACE, T_OTHER_CHANNEL);
	t_ets_call_expect(test, 4, TC_ETS_REPLACE, T_TCONT_CHANNEL);
}

static void pon_ets_tcont_del_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);

	mutex_lock(&ctx->pdev->lock);
	pon_tc_tcont_release(ctx->pdev, tcont);
	list_del(&tcont->list);
	kfree(tcont);
	mutex_unlock(&ctx->pdev->lock);
	flush_work(&ctx->pdev->tc_work);

	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 2);
	t_ets_call_expect(test, 1, TC_ETS_DESTROY, T_TCONT_CHANNEL);
}

static void pon_ets_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);

	pon_test_unregister(ctx);

	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 2);
	t_ets_call_expect(test, 1, TC_ETS_DESTROY, T_TCONT_CHANNEL);
}

static void pon_ets_bound_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	t_conduit_ets_expect(test, 1);
	t_ets_state_expect(test, tcont, true, false);
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_NULL(test, tcont->ets.qstats);
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_DESTROY), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 2);
	KUNIT_EXPECT_EQ(test, ctx->conduit_ets_command, TC_ETS_DESTROY);
	t_ets_state_expect(test, tcont, false, false);
}

static void pon_ets_replace_refused_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	t_ets_state_expect(test, tcont, true, true);

	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	ctx->conduit_setup_tc_err = -EINVAL;
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 1);
	t_ets_state_expect(test, tcont, true, true);
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_TRUE(test, tcont->ets_refused);
	mutex_unlock(&ctx->pdev->lock);

	ctx->conduit_setup_tc_err = 0;
	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	t_conduit_ets_expect(test, 2);
	t_ets_state_expect(test, tcont, true, false);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, tcont->ets_refused);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_ets_conduit_errno_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	ctx->conduit_setup_tc_err = -ENETDOWN;

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), -ENETDOWN);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 1);
	t_ets_state_expect(test, tcont, false, false);
}

static void pon_ets_stats_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);

	KUNIT_EXPECT_EQ(test,
			t_ets_index(ctx, TC_ETS_STATS, T_OTHER_TCONT_INDEX),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), -EOPNOTSUPP);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), -EOPNOTSUPP);

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), 0);

	ctx->conduit_setup_tc_err = -EINVAL;
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), -EINVAL);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), -EOPNOTSUPP);

	ctx->conduit_setup_tc_err = 0;
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), 0);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_DESTROY), 0);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_STATS), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 4);
}

static void pon_ets_no_conduit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);

	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	t_ets_state_expect(test, tcont, true, true);

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	flush_work(&ctx->pdev->tc_work);

	t_conduit_ets_expect(test, 1);
	t_ets_state_expect(test, tcont, true, false);
}

static void pon_ets_conduit_replaced_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	tcont = pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_ets_tcont(ctx, TC_ETS_REPLACE), 0);
	t_conduit_ets_expect(test, 1);

	pon_test_conduit_destroy(ctx->conduit);
	ctx->conduit = NULL;
	memset(&ctx->conduit_ets, 0, sizeof(ctx->conduit_ets));
	ctx->conduit_channel = 0;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	flush_work(&ctx->pdev->tc_work);

	t_conduit_ets_expect(test, 2);
	t_ets_state_expect(test, tcont, true, false);
}

static void pon_offload_blocked_plain_test(struct kunit *test)
{
	struct net_device *dev;

	dev = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(test, dev);

	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(dev));

	pon_test_netdev_destroy(dev);
}

static void t_gem_no_offload_set(struct pon_test_ctx *ctx,
				 struct pon_gem *gem, bool no_offload)
{
	mutex_lock(&ctx->pdev->lock);
	gem->cfg.no_offload = no_offload;
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_offload_blocked_gem_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;
	struct pon_gem *gem;

	gem = pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(dev));

	t_gem_no_offload_set(ctx, gem, true);
	KUNIT_EXPECT_TRUE(test, pon_netdev_offload_blocked(dev));
	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(ctx->netdev));

	t_gem_no_offload_set(ctx, gem, false);
	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(dev));
}

static void pon_offload_blocked_main_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem *gem;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	gem = pon_test_gem_new(test, T_GEM_ID + 1, T_TCONT_INDEX,
			       T_TCONT_ALLOC_ID);
	t_gem_no_offload_set(ctx, gem, true);
	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(ctx->netdev));

	pon_test_gem_map_new(test, T_GEM_ID);
	pon_test_gem_map_new(test, T_GEM_ID + 2);
	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(ctx->netdev));

	pon_test_gem_map_new(test, T_GEM_ID + 1);
	KUNIT_EXPECT_TRUE(test, pon_netdev_offload_blocked(ctx->netdev));

	t_gem_no_offload_set(ctx, gem, false);
	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(ctx->netdev));
}

static void pon_offload_blocked_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem *gem;

	gem = pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	t_gem_no_offload_set(ctx, gem, true);
	pon_test_gem_map_new(test, T_GEM_ID);
	KUNIT_EXPECT_TRUE(test, pon_netdev_offload_blocked(ctx->netdev));

	pon_test_unregister(ctx);
	KUNIT_EXPECT_FALSE(test, pon_netdev_offload_blocked(ctx->netdev));
}

static void pon_offload_info_plain_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_flow_key key = {};
	struct pon_netdev_info info = {};
	struct net_device *dev;

	dev = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(test, dev);
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(dev, &key, &info), -ENODEV);
	pon_test_netdev_destroy(dev);

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	pon_test_ops_set(ctx, &pon_test_ops_full);
	ctx->resolve_gem = T_GEM_ID;

	pon_test_unregister(ctx);
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(ctx->netdev, &key, &info),
			-ENODEV);
	KUNIT_EXPECT_EQ(test, ctx->resolves, 0);
	KUNIT_EXPECT_NULL(test, info.conduit);
}

static void pon_offload_info_gem_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_netdev_info info = {};
	struct net_device *dev;
	struct pon_gem *gem;

	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	gem = pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	mutex_lock(&ctx->pdev->lock);
	gem->cfg.queue = T_GEM_QUEUE;
	gem->cfg.no_offload = true;
	mutex_unlock(&ctx->pdev->lock);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(dev, NULL, &info),
			-EOPNOTSUPP);

	t_gem_no_offload_set(ctx, gem, false);
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(dev, NULL, &info), -ENOLINK);

	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(dev, NULL, &info),
			-ENETDOWN);
	KUNIT_EXPECT_NULL(test, info.conduit);

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(dev, NULL, &info), 0);
	KUNIT_EXPECT_EQ(test, info.channel, T_TCONT_CHANNEL);
	KUNIT_EXPECT_EQ(test, info.queue, T_GEM_QUEUE);
	KUNIT_EXPECT_EQ(test, info.gem, T_GEM_ID);
	KUNIT_EXPECT_PTR_EQ(test, info.conduit, ctx->conduit);
	if (info.conduit)
		pon_netdev_info_put(&info);
	KUNIT_EXPECT_NULL(test, info.conduit);
}

static void pon_offload_info_main_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_flow_key key = {
		.vid = T_KEY_VID,
		.tagged = true,
		.pbit = T_KEY_PBIT,
		.dscp = T_KEY_DSCP,
		.vid_valid = true,
		.pbit_valid = true,
		.dscp_valid = true,
	};
	struct pon_netdev_info info = {};

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_alloc_bind(ctx, T_TCONT_ALLOC_ID);
	ctx->resolve_gem = T_GEM_ID;

	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(ctx->netdev, &key, &info),
			-EOPNOTSUPP);

	pon_test_ops_set(ctx, &pon_test_ops_full);
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(ctx->netdev, NULL, &info),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->resolves, 0);

	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(ctx->netdev, &key, &info), 0);
	KUNIT_EXPECT_EQ(test, ctx->resolves, 1);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.vid, key.vid);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.tagged, key.tagged);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.pbit, key.pbit);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.dscp, key.dscp);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.vid_valid, key.vid_valid);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.pbit_valid, key.pbit_valid);
	KUNIT_EXPECT_EQ(test, ctx->resolve_key.dscp_valid, key.dscp_valid);
	KUNIT_EXPECT_EQ(test, info.gem, T_GEM_ID);
	KUNIT_EXPECT_EQ(test, info.channel, T_TCONT_CHANNEL);
	KUNIT_EXPECT_PTR_EQ(test, info.conduit, ctx->conduit);
	if (info.conduit)
		pon_netdev_info_put(&info);

	ctx->resolve_err = -EIO;
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(ctx->netdev, &key, &info),
			-EIO);

	ctx->resolve_err = 0;
	ctx->resolve_gem = T_GEM_ID + 1;
	KUNIT_EXPECT_EQ(test, pon_netdev_info_get(ctx->netdev, &key, &info),
			-ENOENT);
	KUNIT_EXPECT_NULL(test, info.conduit);
}

static void t_flow_block_cmd(struct flow_block_offload *offload,
			     struct flow_block *block,
			     enum flow_block_command command)
{
	memset(offload, 0, sizeof(*offload));
	offload->command = command;
	offload->binder_type = FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	offload->block = block;
	offload->cb_list_head = &block->cb_list;
	INIT_LIST_HEAD(&offload->cb_list);
}

static struct flow_block_cb *t_flow_block_bind(struct kunit *test,
					       struct flow_block *block)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block_offload offload;

	t_flow_block_cmd(&offload, block, FLOW_BLOCK_BIND);
	KUNIT_ASSERT_EQ(test, pon_flow_block_setup(ctx->pdev, &offload), 0);
	list_splice_init(&offload.cb_list, &block->cb_list);

	return list_first_entry_or_null(&block->cb_list, struct flow_block_cb,
					list);
}

static void t_flow_block_free(struct flow_block_offload *offload)
{
	struct flow_block_cb *block_cb, *next;

	list_for_each_entry_safe(block_cb, next, &offload->cb_list, list) {
		list_del(&block_cb->list);
		flow_block_cb_free(block_cb);
	}
}

static void t_flow_block_unbind(struct kunit *test, struct flow_block *block)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block_offload offload;

	t_flow_block_cmd(&offload, block, FLOW_BLOCK_UNBIND);
	KUNIT_EXPECT_EQ(test, pon_flow_block_setup(ctx->pdev, &offload), 0);
	t_flow_block_free(&offload);
}

static void pon_flow_block_binder_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block_offload offload;
	unsigned int refs;
	struct flow_block block;

	flow_block_init(&block);
	refs = refcount_read(&ctx->pdev->refcnt);

	t_flow_block_cmd(&offload, &block, FLOW_BLOCK_BIND);
	offload.binder_type = FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS;
	KUNIT_EXPECT_EQ(test, pon_flow_block_setup(ctx->pdev, &offload),
			-EOPNOTSUPP);
	offload.binder_type = FLOW_BLOCK_BINDER_TYPE_UNSPEC;
	KUNIT_EXPECT_EQ(test, pon_flow_block_setup(ctx->pdev, &offload),
			-EOPNOTSUPP);

	KUNIT_EXPECT_TRUE(test, list_empty(&offload.cb_list));
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), refs);
}

static void pon_flow_block_unbound_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block_offload offload;
	struct flow_block block;

	flow_block_init(&block);

	t_flow_block_cmd(&offload, &block, FLOW_BLOCK_UNBIND);
	KUNIT_EXPECT_EQ(test, pon_flow_block_setup(ctx->pdev, &offload),
			-ENOENT);
	KUNIT_EXPECT_TRUE(test, list_empty(&offload.cb_list));
}

static void pon_flow_block_refcount_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block_cb *block_cb;
	struct flow_block block;
	unsigned int refs;

	flow_block_init(&block);
	refs = refcount_read(&ctx->pdev->refcnt);

	block_cb = t_flow_block_bind(test, &block);
	KUNIT_ASSERT_NOT_NULL(test, block_cb);
	KUNIT_EXPECT_EQ(test, block_cb->refcnt, 1);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), refs + 1);

	KUNIT_EXPECT_PTR_EQ(test, t_flow_block_bind(test, &block), block_cb);
	KUNIT_EXPECT_TRUE(test, list_is_singular(&block.cb_list));
	KUNIT_EXPECT_EQ(test, block_cb->refcnt, 2);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), refs + 1);

	t_flow_block_unbind(test, &block);
	KUNIT_EXPECT_TRUE(test, list_is_singular(&block.cb_list));
	KUNIT_EXPECT_EQ(test, block_cb->refcnt, 1);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), refs + 1);

	t_flow_block_unbind(test, &block);
	KUNIT_EXPECT_TRUE(test, list_empty(&block.cb_list));
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), refs);
}

static int t_flow_block_dev_bind(struct net_device *dev,
				 struct flow_block *block)
{
	struct flow_block_offload offload;
	int err;

	t_flow_block_cmd(&offload, block, FLOW_BLOCK_BIND);
	err = dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_FT, &offload);
	list_splice_init(&offload.cb_list, &block->cb_list);

	return err;
}

static void pon_flow_block_unregister_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block main_block, gem_block;
	struct flow_block_offload offload;
	struct net_device *dev;

	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	dev = pon_test_gem_link_new(test, T_GEM_ID);
	KUNIT_ASSERT_EQ(test, refcount_read(&ctx->pdev->refcnt), 2);

	flow_block_init(&main_block);
	flow_block_init(&gem_block);
	t_flow_block_cmd(&offload, &main_block, FLOW_BLOCK_BIND);
	KUNIT_ASSERT_EQ(test, pon_dev_setup_tc(ctx->pdev, TC_SETUP_FT,
					       &offload), 0);
	list_splice_init(&offload.cb_list, &main_block.cb_list);
	KUNIT_ASSERT_EQ(test, t_flow_block_dev_bind(dev, &gem_block), 0);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 4);

	pon_test_unregister(ctx);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 3);

	t_flow_block_cmd(&offload, &main_block, FLOW_BLOCK_UNBIND);
	KUNIT_EXPECT_EQ(test, pon_dev_setup_tc(ctx->pdev, TC_SETUP_FT,
					       &offload), 0);
	t_flow_block_free(&offload);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 2);

	t_flow_block_unbind(test, &gem_block);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pdev->refcnt), 1);
}

static void pon_flow_block_conduit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct flow_block_cb *block_cb;
	struct flow_block block;

	flow_block_init(&block);
	block_cb = t_flow_block_bind(test, &block);
	KUNIT_ASSERT_NOT_NULL(test, block_cb);
	KUNIT_EXPECT_PTR_EQ(test, block_cb->cb_priv, (void *)ctx->pdev);

	KUNIT_EXPECT_EQ(test, block_cb->cb(TC_SETUP_CLSFLOWER, NULL,
					   block_cb->cb_priv), -ENETDOWN);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flow_setups, 0);

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_EXPECT_EQ(test, block_cb->cb(TC_SETUP_CLSFLOWER, NULL,
					   block_cb->cb_priv), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flow_setups, 1);

	ctx->conduit_flow_setup_err = -EIO;
	KUNIT_EXPECT_EQ(test, block_cb->cb(TC_SETUP_CLSFLOWER, NULL,
					   block_cb->cb_priv), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flow_setups, 2);

	t_flow_block_unbind(test, &block);
	KUNIT_EXPECT_TRUE(test, list_empty(&block.cb_list));
}

static void t_flow_flush(struct pon_test_ctx *ctx, u16 gem)
{
	mutex_lock(&ctx->pdev->lock);
	pon_conduit_flow_flush(ctx->pdev, gem);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_flush_forward_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	t_flow_flush(ctx, T_GEM_ID);

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 0);

	t_flow_flush(ctx, T_GEM_ID);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 1);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flush_gem, T_GEM_ID);

	t_flow_flush(ctx, PON_GEM_ANY);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 2);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flush_gem, PON_GEM_ANY);
}

static void pon_flush_edge_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 0);

	KUNIT_EXPECT_EQ(test, pon_test_state_report(ctx, PON_PLOAM_STATE_O1),
			0);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 1);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flush_gem, PON_GEM_ANY);

	KUNIT_EXPECT_EQ(test, pon_test_state_report(ctx, PON_PLOAM_STATE_O1),
			0);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 1);

	KUNIT_EXPECT_EQ(test, pon_test_state_report(ctx, PON_PLOAM_STATE_O2),
			0);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 2);
}

static void pon_flush_dealloc_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	unsigned int flushes;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	pon_test_tcont_new(test, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_gem_new(test, T_GEM_ID, T_TCONT_INDEX, T_TCONT_ALLOC_ID);
	pon_test_activate(ctx);
	flush_workqueue(ctx->pdev->wq);
	flushes = ctx->conduit_flushes;

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, flushes);

	pon_test_alloc_release(ctx, T_TCONT_ALLOC_ID);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, flushes + 1);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flush_gem, PON_GEM_ANY);
}

static struct kunit_case pon_conduit_test_cases[] = {
	KUNIT_CASE(pon_rx_omci_test),
	KUNIT_CASE(pon_rx_gem_test),
	KUNIT_CASE(pon_rx_unknown_gem_test),
	KUNIT_CASE(pon_rx_down_test),
	KUNIT_CASE(pon_rx_not_pon_test),
	KUNIT_CASE(pon_xmit_no_conduit_test),
	KUNIT_CASE(pon_xmit_info_test),
	KUNIT_CASE(pon_xmit_busy_test),
	KUNIT_CASE(pon_pair_conduit_later_test),
	KUNIT_CASE(pon_pair_pdev_later_test),
	KUNIT_CASE(pon_pair_busy_test),
	KUNIT_CASE(pon_pair_no_handle_test),
	KUNIT_CASE(pon_pair_unregister_test),
	KUNIT_CASE(pon_pair_pdev_again_test),
	KUNIT_CASE(pon_pair_sync_going_away_test),
	KUNIT_CASE(pon_pair_sync_refused_test),
	KUNIT_CASE(pon_pair_rebind_unregistered_test),
	KUNIT_CASE(pon_carrier_conduit_test),
	KUNIT_CASE(pon_mtu_floor_test),
	KUNIT_CASE(pon_mtu_largest_test),
	KUNIT_CASE(pon_mtu_refused_test),
	KUNIT_CASE(pon_addr_pair_test),
	KUNIT_CASE(pon_addr_equal_test),
	KUNIT_CASE(pon_ets_refused_test),
	KUNIT_CASE(pon_ets_after_unregister_test),
	KUNIT_CASE(pon_ets_unbound_test),
	KUNIT_CASE(pon_ets_moved_test),
	KUNIT_CASE(pon_ets_swapped_test),
	KUNIT_CASE(pon_ets_tcont_del_test),
	KUNIT_CASE(pon_ets_unregister_test),
	KUNIT_CASE(pon_ets_bound_test),
	KUNIT_CASE(pon_ets_replace_refused_test),
	KUNIT_CASE(pon_ets_stats_test),
	KUNIT_CASE(pon_ets_conduit_errno_test),
	KUNIT_CASE(pon_ets_no_conduit_test),
	KUNIT_CASE(pon_ets_conduit_replaced_test),
	KUNIT_CASE(pon_offload_blocked_plain_test),
	KUNIT_CASE(pon_offload_blocked_gem_test),
	KUNIT_CASE(pon_offload_blocked_main_test),
	KUNIT_CASE(pon_offload_blocked_unregister_test),
	KUNIT_CASE(pon_offload_info_plain_test),
	KUNIT_CASE(pon_offload_info_gem_test),
	KUNIT_CASE(pon_offload_info_main_test),
	KUNIT_CASE(pon_flow_block_binder_test),
	KUNIT_CASE(pon_flow_block_unbound_test),
	KUNIT_CASE(pon_flow_block_refcount_test),
	KUNIT_CASE(pon_flow_block_conduit_test),
	KUNIT_CASE(pon_flow_block_unregister_test),
	KUNIT_CASE(pon_flush_forward_test),
	KUNIT_CASE(pon_flush_edge_test),
	KUNIT_CASE(pon_flush_dealloc_test),
	{}
};

static struct kunit_suite pon_conduit_test_suite = {
	.name = "pon_conduit",
	.init = pon_test_init,
	.exit = pon_test_exit,
	.test_cases = pon_conduit_test_cases,
};

kunit_test_suite(pon_conduit_test_suite);
