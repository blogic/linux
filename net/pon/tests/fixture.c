// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/device.h>
#include <kunit/fwnode.h>
#include <kunit/test.h>
#include <linux/etherdevice.h>
#include <linux/file.h>
#include <linux/if_link.h>
#include <linux/kmsg_dump.h>
#include <linux/net.h>
#include <linux/netlink.h>
#include <linux/property.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <net/genetlink.h>
#include <net/rtnetlink.h>
#include <net/sock.h>

#include "../pon-nl-gen.h"
#include "pon_test.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

const struct pon_dev_caps pon_test_caps = {
	.modes		= BIT(PON_MODE_XGS_PON),
	.max_tconts	= 8,
	.max_gems	= 8,
};

const struct software_node pon_test_mac_node = {
	.name = "pon-test-mac",
};

static const struct property_entry pon_test_conduit_props[] = {
	PROPERTY_ENTRY_REF("pon-handle", &pon_test_mac_node),
	{}
};

const struct software_node pon_test_conduit_node = {
	.name = "pon-test-conduit",
	.properties = pon_test_conduit_props,
};

static struct pon_test_ctx *pon_test_drv(struct pon_dev *pdev)
{
	return pdev->drv_priv;
}

static int pon_test_enable(struct pon_dev *pdev, bool on,
			   struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->enables++;
	if (ctx->enable_err)
		return ctx->enable_err;
	ctx->enable_on = on;

	return 0;
}

static int pon_test_set_identity(struct pon_dev *pdev,
				 const struct pon_identity *id,
				 struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->identities++;
	if (ctx->identity_err)
		return ctx->identity_err;
	ctx->identity = *id;

	return 0;
}

static int pon_test_tcont_set(struct pon_dev *pdev,
			      const struct pon_tcont_cfg *cfg,
			      struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->tcont_sets++;
	ctx->tcont_set_last = *cfg;

	return ctx->tcont_set_err;
}

static int pon_test_tcont_clear(struct pon_dev *pdev,
				const struct pon_tcont_cfg *cfg,
				struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->tcont_clears++;

	return ctx->tcont_clear_err;
}

static int pon_test_tcont_channel(struct pon_dev *pdev,
				  const struct pon_tcont_cfg *cfg)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->channel_reads++;

	return test_bit(cfg->alloc_id, ctx->bound) ? T_CHANNEL(cfg->alloc_id) :
						     -ENOLINK;
}

static int pon_test_gem_add(struct pon_dev *pdev, const struct pon_gem_cfg *cfg,
			    struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->gem_adds++;

	if (cfg->id == ctx->gem_fail_id && cfg->alloc_id == T_OTHER_ALLOC_ID)
		return -EIO;
	if (ctx->gem_add_err)
		return ctx->gem_add_err;

	if (cfg->id >= T_GEM_ID && cfg->id < T_GEM_ID + T_GEM_COUNT)
		ctx->gem_alloc_id[cfg->id - T_GEM_ID] = cfg->alloc_id;

	return 0;
}

static int pon_test_gem_del(struct pon_dev *pdev, u16 gem_id,
			    struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->gem_dels++;

	return ctx->gem_del_err;
}

static int pon_test_gem_stats(struct pon_dev *pdev, u16 gem_id,
			      struct pon_gem_stats *stats)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	if (ctx->gem_stats_err)
		return ctx->gem_stats_err;
	stats->rx_frames = ctx->gem_rx_frames;

	return 0;
}

static int pon_test_fec_stats(struct pon_dev *pdev,
			      struct pon_fec_stats *stats)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	if (ctx->fec_err)
		return ctx->fec_err;

	*stats = ctx->fec;

	return 0;
}

static int pon_test_tc_stats(struct pon_dev *pdev, struct pon_tc_stats *stats)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	if (ctx->tc_stats_err)
		return ctx->tc_stats_err;
	*stats = ctx->tc_stats;

	return 0;
}

static int pon_test_response_time(struct pon_dev *pdev, u32 *ns)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	if (ctx->response_time_err)
		return ctx->response_time_err;
	*ns = ctx->response_time;

	return 0;
}

static int pon_test_gem_xmit(struct pon_dev *pdev, u16 gem_id,
			     struct sk_buff *skb)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->gem_xmits++;
	ctx->gem_xmit_id = gem_id;
	kfree_skb(skb);

	return ctx->gem_xmit_err;
}

static int pon_test_omci_xmit(struct pon_dev *pdev, struct sk_buff *skb)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->omci_xmits++;
	ctx->omci_xmit_len = skb->len;
	ctx->omci_xmit_bh_off = in_softirq();
	kfree_skb(skb);

	return ctx->omci_xmit_err;
}

static int pon_test_omci_verify(struct pon_dev *pdev, struct sk_buff *skb)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->omci_verifies++;
	if (ctx->omci_verify_err)
		return ctx->omci_verify_err;
	skb_trim(skb, skb->len - PON_OMCI_MIC_LEN);

	return 0;
}

static int pon_test_gem_map_set(struct pon_dev *pdev,
				const struct pon_gem_map_cfg *cfg,
				struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->map_sets++;
	ctx->map_last = *cfg;

	return ctx->map_set_err;
}

static int pon_test_gem_map_del(struct pon_dev *pdev,
				const struct pon_gem_map_cfg *cfg,
				struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->map_dels++;
	ctx->map_last = *cfg;

	return ctx->map_del_err;
}

static int pon_test_gem_resolve(struct pon_dev *pdev,
				const struct pon_flow_key *key, u16 *gem_id)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->resolves++;
	ctx->resolve_key = *key;
	if (ctx->resolve_err)
		return ctx->resolve_err;
	*gem_id = ctx->resolve_gem;

	return 0;
}

static int pon_test_msk_set(struct pon_dev *pdev, const u8 *msk,
			    struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->msk_sets++;
	memcpy(ctx->msk, msk, PON_KEY_LEN);

	return ctx->msk_err;
}

static int pon_test_bcast_key_set(struct pon_dev *pdev, u8 index,
				  const u8 *key,
				  struct netlink_ext_ack *extack)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	ctx->bcast_sets++;
	ctx->bcast_index = index;
	ctx->bcast_has_key = key;
	if (key)
		memcpy(ctx->bcast_key, key, PON_KEY_LEN);

	return ctx->bcast_err;
}

static void pon_test_pcs_link(struct pon_dev *pdev, bool up)
{
	struct pon_test_ctx *ctx = pon_test_drv(pdev);

	if (ctx->pcs_calls < T_PCS_LOG)
		ctx->pcs_log[ctx->pcs_calls] = up;
	ctx->pcs_calls++;
}

const struct pon_dev_ops pon_test_ops = {
	.tcont_set	= pon_test_tcont_set,
	.tcont_clear	= pon_test_tcont_clear,
	.tcont_channel	= pon_test_tcont_channel,
	.gem_add	= pon_test_gem_add,
	.gem_del	= pon_test_gem_del,
	.gem_stats	= pon_test_gem_stats,
	.fec_stats	= pon_test_fec_stats,
	.omci_xmit	= pon_test_omci_xmit,
	.gem_map_set	= pon_test_gem_map_set,
};

const struct pon_dev_ops pon_test_ops_no_channel = {
	.tcont_set	= pon_test_tcont_set,
	.tcont_clear	= pon_test_tcont_clear,
	.gem_add	= pon_test_gem_add,
	.gem_del	= pon_test_gem_del,
	.gem_stats	= pon_test_gem_stats,
	.fec_stats	= pon_test_fec_stats,
	.omci_xmit	= pon_test_omci_xmit,
	.gem_map_set	= pon_test_gem_map_set,
};

const struct pon_dev_ops pon_test_ops_full = {
	.enable		= pon_test_enable,
	.set_identity	= pon_test_set_identity,
	.tcont_set	= pon_test_tcont_set,
	.tcont_clear	= pon_test_tcont_clear,
	.tcont_channel	= pon_test_tcont_channel,
	.gem_add	= pon_test_gem_add,
	.gem_del	= pon_test_gem_del,
	.gem_stats	= pon_test_gem_stats,
	.fec_stats	= pon_test_fec_stats,
	.pcs_link	= pon_test_pcs_link,
	.tc_stats	= pon_test_tc_stats,
	.response_time	= pon_test_response_time,
	.gem_xmit	= pon_test_gem_xmit,
	.omci_xmit	= pon_test_omci_xmit,
	.omci_verify	= pon_test_omci_verify,
	.gem_map_set	= pon_test_gem_map_set,
	.gem_map_del	= pon_test_gem_map_del,
	.gem_resolve	= pon_test_gem_resolve,
	.msk_set	= pon_test_msk_set,
	.bcast_key_set	= pon_test_bcast_key_set,
};

static netdev_tx_t pon_test_netdev_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	kfree_skb(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops pon_test_netdev_ops = {
	.ndo_start_xmit		= pon_test_netdev_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
};

struct net_device *pon_test_netdev_create(void)
{
	struct net_device *dev;

	dev = alloc_netdev(0, "pontest%d", NET_NAME_ENUM, ether_setup);
	if (!dev)
		return NULL;

	dev->netdev_ops = &pon_test_netdev_ops;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;
	dev->netns_immutable = true;
	dev->max_mtu = ETH_MAX_MTU;
	eth_hw_addr_random(dev);

	if (register_netdev(dev)) {
		free_netdev(dev);
		return NULL;
	}

	return dev;
}

void pon_test_netdev_destroy(struct net_device *dev)
{
	unregister_netdev(dev);
	free_netdev(dev);
}

static struct pon_test_ctx *pon_test_conduit_ctx(struct net_device *conduit)
{
	return *(struct pon_test_ctx **)netdev_priv(conduit);
}

static netdev_tx_t pon_test_conduit_ndo_xmit(struct sk_buff *skb,
					     struct net_device *dev)
{
	kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int pon_test_conduit_set_mac(struct net_device *dev, void *addr)
{
	struct pon_test_ctx *ctx = pon_test_conduit_ctx(dev);

	ctx->conduit_addr_sets++;
	if (ctx->conduit_addr_err)
		return ctx->conduit_addr_err;

	return eth_mac_addr(dev, addr);
}

static int pon_test_conduit_change_mtu(struct net_device *dev, int mtu)
{
	struct pon_test_ctx *ctx = pon_test_conduit_ctx(dev);

	ctx->conduit_mtu_sets++;
	if (ctx->conduit_mtu_err)
		return ctx->conduit_mtu_err;
	WRITE_ONCE(dev->mtu, mtu);

	return 0;
}

static const struct net_device_ops pon_test_conduit_netdev_ops = {
	.ndo_start_xmit		= pon_test_conduit_ndo_xmit,
	.ndo_set_mac_address	= pon_test_conduit_set_mac,
	.ndo_change_mtu		= pon_test_conduit_change_mtu,
};

static netdev_tx_t pon_test_conduit_xmit(struct net_device *conduit,
					 struct sk_buff *skb,
					 const struct pon_tx_info *info)
{
	struct pon_test_ctx *ctx = pon_test_conduit_ctx(conduit);

	ctx->conduit_xmits++;
	ctx->conduit_tx_info = *info;
	if (ctx->conduit_xmit_ret == NETDEV_TX_OK)
		kfree_skb(skb);

	return ctx->conduit_xmit_ret;
}

static int pon_test_conduit_setup_tc(struct net_device *conduit,
				     unsigned int channel,
				     enum tc_setup_type type, void *type_data)
{
	struct pon_test_ctx *ctx = pon_test_conduit_ctx(conduit);
	struct tc_ets_qopt_offload *opt = type_data;
	unsigned int call;

	ctx->conduit_setup_tcs++;
	ctx->conduit_channel = channel;
	ctx->conduit_tc_type = type;
	if (type == TC_SETUP_QDISC_ETS) {
		ctx->conduit_ets_command = opt->command;
		if (opt->command == TC_ETS_REPLACE)
			ctx->conduit_ets = opt->replace_params;
		call = ctx->conduit_setup_tcs - 1;
		if (call < T_ETS_LOG) {
			ctx->conduit_ets_log[call].channel = channel;
			ctx->conduit_ets_log[call].command = opt->command;
		}
	}

	return ctx->conduit_setup_tc_err;
}

static int pon_test_conduit_flow_setup(struct net_device *conduit,
				       enum tc_setup_type type,
				       void *type_data)
{
	struct pon_test_ctx *ctx = pon_test_conduit_ctx(conduit);

	ctx->conduit_flow_setups++;

	return ctx->conduit_flow_setup_err;
}

static void pon_test_conduit_flow_flush(struct net_device *conduit, u16 gem)
{
	struct pon_test_ctx *ctx = pon_test_conduit_ctx(conduit);

	ctx->conduit_flushes++;
	ctx->conduit_flush_gem = gem;
}

static const struct pon_conduit_ops pon_test_conduit_ops = {
	.xmit		= pon_test_conduit_xmit,
	.setup_tc	= pon_test_conduit_setup_tc,
	.flow_setup	= pon_test_conduit_flow_setup,
	.flow_flush	= pon_test_conduit_flow_flush,
};

struct net_device *pon_test_conduit_create(struct kunit *test,
					   const struct software_node *node)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	dev = alloc_netdev(sizeof(ctx), "pontc%d", NET_NAME_ENUM, ether_setup);
	if (!dev)
		return NULL;

	*(struct pon_test_ctx **)netdev_priv(dev) = ctx;
	dev->netdev_ops = &pon_test_conduit_netdev_ops;
	dev->max_mtu = ETH_MAX_MTU;
	eth_hw_addr_random(dev);

	if (register_netdev(dev)) {
		free_netdev(dev);
		return NULL;
	}

	if (node && device_add_software_node(&dev->dev, node)) {
		unregister_netdev(dev);
		free_netdev(dev);
		return NULL;
	}

	return dev;
}

void pon_test_conduit_destroy(struct net_device *conduit)
{
	pon_conduit_unregister(conduit);
	device_remove_software_node(&conduit->dev);
	unregister_netdev(conduit);
	free_netdev(conduit);
}

int pon_test_conduit_register(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	ctx->conduit = pon_test_conduit_create(test, &pon_test_conduit_node);
	if (!ctx->conduit)
		return -ENOMEM;

	return pon_conduit_register(ctx->conduit, &pon_test_conduit_ops);
}

int pon_test_conduit_up(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	int err;

	err = pon_test_conduit_register(test);
	if (err)
		return err;

	rtnl_lock();
	err = dev_open(ctx->conduit, NULL);
	rtnl_unlock();

	return err;
}

int pon_test_mac_node_add(struct kunit *test, struct device *dev,
			  const struct software_node *node)
{
	return kunit_device_add_software_node(test, dev, node);
}

int pon_test_init_ops(struct kunit *test, const struct pon_dev_ops *ops)
{
	struct pon_test_ctx *ctx;
	int err;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->test = test;
	ctx->conduit_xmit_ret = NETDEV_TX_OK;
	INIT_LIST_HEAD(&ctx->nl_pending);

	ctx->parent = kunit_device_register(test, "pon-test");
	if (IS_ERR(ctx->parent))
		return PTR_ERR(ctx->parent);

	err = pon_test_mac_node_add(test, ctx->parent, &pon_test_mac_node);
	if (err)
		return err;

	ctx->netdev = pon_test_netdev_create();
	if (!ctx->netdev)
		return -ENOMEM;

	ctx->pdev = pon_dev_create(ctx->netdev, ctx->parent, ops,
				   &pon_test_caps, PON_MODE_XGS_PON, ctx);
	if (IS_ERR(ctx->pdev)) {
		pon_test_netdev_destroy(ctx->netdev);
		return PTR_ERR(ctx->pdev);
	}

	test->priv = ctx;

	return 0;
}

int pon_test_init(struct kunit *test)
{
	return pon_test_init_ops(test, &pon_test_ops);
}

int pon_test_init_full(struct kunit *test)
{
	return pon_test_init_ops(test, &pon_test_ops_full);
}

void pon_test_exit(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	if (!ctx)
		return;

	if (ctx->conduit)
		pon_test_conduit_destroy(ctx->conduit);
	if (ctx->nl_file)
		__fput_sync(ctx->nl_file);
	if (!ctx->unregistered)
		pon_dev_unregister(ctx->pdev);
	pon_test_netdev_destroy(ctx->netdev);
	pon_dev_put(ctx->pdev);
}

void pon_test_ops_set(struct pon_test_ctx *ctx, const struct pon_dev_ops *ops)
{
	mutex_lock(&ctx->pdev->lock);
	ctx->pdev->ops = ops;
	mutex_unlock(&ctx->pdev->lock);
}

void pon_test_unregister(struct pon_test_ctx *ctx)
{
	pon_dev_unregister(ctx->pdev);
	ctx->unregistered = true;
}

int pon_test_state_report(struct pon_test_ctx *ctx,
			  enum pon_ploam_state state)
{
	int ret;

	mutex_lock(&ctx->pdev->lock);
	ret = pon_dev_state_report(ctx->pdev, state);
	mutex_unlock(&ctx->pdev->lock);

	return ret;
}

enum pon_ploam_state pon_test_state(struct pon_test_ctx *ctx)
{
	enum pon_ploam_state state;

	mutex_lock(&ctx->pdev->lock);
	state = ctx->pdev->ploam;
	mutex_unlock(&ctx->pdev->lock);

	return state;
}

void pon_test_activate(struct pon_test_ctx *ctx)
{
	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O2);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O4);
	pon_test_state_report(ctx, PON_PLOAM_STATE_O5);
}

void pon_test_alloc_bind(struct pon_test_ctx *ctx, u16 alloc_id)
{
	mutex_lock(&ctx->pdev->lock);
	set_bit(alloc_id, ctx->bound);
	mutex_unlock(&ctx->pdev->lock);
}

void pon_test_alloc_assign(struct pon_test_ctx *ctx, u16 alloc_id)
{
	struct pon_event ev = {
		.type = PON_EVENT_TYPE_TCONT_ALLOC,
		.alloc_id = alloc_id,
	};

	mutex_lock(&ctx->pdev->lock);
	set_bit(alloc_id, ctx->bound);
	pon_dev_event(ctx->pdev, &ev);
	mutex_unlock(&ctx->pdev->lock);
}

void pon_test_alloc_release(struct pon_test_ctx *ctx, u16 alloc_id)
{
	struct pon_event ev = {
		.type = PON_EVENT_TYPE_TCONT_DEALLOC,
		.alloc_id = alloc_id,
	};

	mutex_lock(&ctx->pdev->lock);
	clear_bit(alloc_id, ctx->bound);
	pon_dev_event(ctx->pdev, &ev);
	mutex_unlock(&ctx->pdev->lock);
}

void pon_test_alloc_drop_all(struct pon_test_ctx *ctx)
{
	mutex_lock(&ctx->pdev->lock);
	bitmap_zero(ctx->bound, PON_PLOAM_ALLOC_ID_MAX + 1);
	mutex_unlock(&ctx->pdev->lock);
}

struct pon_tcont *pon_test_tcont_new(struct kunit *test, u16 index,
				     u16 alloc_id)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	tcont = kzalloc_obj(*tcont, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tcont);
	tcont->cfg.index = index;
	tcont->cfg.alloc_id = alloc_id;

	mutex_lock(&ctx->pdev->lock);
	list_add_tail(&tcont->list, &ctx->pdev->tconts);
	mutex_unlock(&ctx->pdev->lock);

	return tcont;
}

struct pon_gem *pon_test_gem_new(struct kunit *test, u16 gem_id,
				 u16 tcont_index, u16 alloc_id)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem *gem;

	gem = kzalloc_obj(*gem, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gem);
	gem->cfg.id = gem_id;
	gem->cfg.dir = PON_GEM_DIR_BIDIR;
	gem->cfg.tcont_index = tcont_index;
	gem->cfg.alloc_id = alloc_id;
	gem->cfg.tcont_valid = true;

	mutex_lock(&ctx->pdev->lock);
	list_add_tail(&gem->list, &ctx->pdev->gems);
	mutex_unlock(&ctx->pdev->lock);

	return gem;
}

struct pon_gem_map *pon_test_gem_map_add(struct kunit *test,
					 const struct pon_gem_map_cfg *cfg)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem_map *map;

	map = kzalloc_obj(*map, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, map);
	map->cfg = *cfg;

	mutex_lock(&ctx->pdev->lock);
	list_add_tail(&map->list, &ctx->pdev->gem_maps);
	mutex_unlock(&ctx->pdev->lock);

	return map;
}

void pon_test_gem_map_new(struct kunit *test, u16 gem_id)
{
	struct pon_gem_map_cfg cfg = {
		.gem_id = gem_id,
	};

	pon_test_gem_map_add(test, &cfg);
}

struct nlattr *pon_test_nla_u32(struct kunit *test, int type, u32 value)
{
	struct nlattr *nla;

	nla = kunit_kzalloc(test, nla_total_size(sizeof(value)), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, nla);
	nla->nla_type = type;
	nla->nla_len = nla_attr_size(sizeof(value));
	memcpy(nla_data(nla), &value, sizeof(value));

	return nla;
}

int pon_test_gem_link_try(struct kunit *test, struct nlattr **tb,
			  struct nlattr **data, struct net_device **devp)
{
	struct rtnl_newlink_params params = {};
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;
	int err;

	params.src_net = dev_net(ctx->netdev);
	params.tb = tb;
	params.data = data;

	rtnl_lock();
	dev = rtnl_create_link(params.src_net, "gem%d", NET_NAME_ENUM,
			       &pon_gem_link_ops, tb, NULL);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
	} else {
		err = pon_gem_link_ops.newlink(dev, &params, NULL);
		if (err)
			free_netdev(dev);
		else
			err = rtnl_configure_link(dev, NULL, 0, NULL);
	}
	rtnl_unlock();

	if (devp)
		*devp = err ? NULL : dev;

	return err;
}

struct net_device *pon_test_gem_link_new(struct kunit *test, u16 gem_id)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;
	struct nlattr **data;
	struct nlattr **tb;

	tb = kunit_kcalloc(test, IFLA_MAX + 1, sizeof(*tb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tb);
	data = kunit_kcalloc(test, IFLA_GEM_MAX + 1, sizeof(*data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data);

	tb[IFLA_LINK] = pon_test_nla_u32(test, IFLA_LINK, ctx->netdev->ifindex);
	data[IFLA_GEM_ID] = pon_test_nla_u32(test, IFLA_GEM_ID, gem_id);

	KUNIT_ASSERT_EQ(test, pon_test_gem_link_try(test, tb, data, &dev), 0);

	return dev;
}

void pon_test_gem_link_del(struct net_device *dev)
{
	rtnl_lock();
	rtnl_delete_link(dev, 0, NULL);
	rtnl_unlock();
}

struct sk_buff *pon_test_frame(struct kunit *test, unsigned int len)
{
	struct sk_buff *skb;
	struct ethhdr *eth;

	skb = alloc_skb(len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put_zero(skb, len);
	if (len >= ETH_HLEN) {
		eth = (struct ethhdr *)skb->data;
		eth_broadcast_addr(eth->h_dest);
		eth->h_proto = htons(ETH_P_IP);
	}

	return skb;
}

unsigned int pon_test_log_lines(struct kunit *test,
				struct kmsg_dump_iter *iter, const char *what,
				char lines[][T_LOG_TEXT_LEN], unsigned int max)
{
	struct pon_test_ctx *ctx = test->priv;
	char prefix[IFNAMSIZ + T_LOG_TEXT_LEN];
	unsigned int count = 0;
	char *line, *found;
	size_t len;

	snprintf(prefix, sizeof(prefix), "%s: %s", netdev_name(ctx->netdev),
		 what);
	line = kunit_kzalloc(test, T_LOG_LINE_LEN, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, line);

	while (kmsg_dump_get_line(iter, false, line, T_LOG_LINE_LEN, &len)) {
		line[min_t(size_t, len, T_LOG_LINE_LEN - 1)] = '\0';
		found = strstr(strim(line), prefix);
		if (!found)
			continue;
		if (count < max)
			strscpy(lines[count], found + strlen(prefix),
				T_LOG_TEXT_LEN);
		count++;
	}

	return count;
}

int pon_test_log_level(struct kunit *test, struct kmsg_dump_iter *iter,
		       const char *text)
{
	unsigned int prefix;
	char *line;
	size_t len;

	line = kunit_kzalloc(test, T_LOG_LINE_LEN, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, line);

	while (kmsg_dump_get_line(iter, true, line, T_LOG_LINE_LEN, &len)) {
		line[min_t(size_t, len, T_LOG_LINE_LEN - 1)] = '\0';
		if (!strstr(line, text))
			continue;
		if (sscanf(line, "<%u>", &prefix) != 1)
			return -EINVAL;
		return prefix & 7;
	}

	return -ENOENT;
}

void pon_test_log_flush(struct pon_test_ctx *ctx)
{
	flush_work(&ctx->pdev->log_work);
}

int pon_test_nl_open(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};
	struct socket *sock;
	struct file *file;
	int err;

	ctx->nl_buf = kunit_kzalloc(test, T_NL_BUF_LEN, GFP_KERNEL);
	if (!ctx->nl_buf)
		return -ENOMEM;

	err = sock_create_kern(dev_net(ctx->netdev), PF_NETLINK, SOCK_RAW,
			       NETLINK_GENERIC, &sock);
	if (err)
		return err;

	file = sock_alloc_file(sock, 0, NULL);
	if (IS_ERR(file))
		return PTR_ERR(file);
	ctx->nl_sock = sock;
	ctx->nl_file = file;

	err = kernel_bind(sock, (struct sockaddr_unsized *)&addr, sizeof(addr));
	if (err)
		return err;

	err = kernel_getsockname(sock, (struct sockaddr *)&addr);
	if (err < 0)
		return err;
	ctx->nl_portid = addr.nl_pid;

	return 0;
}

int pon_test_nl_join(struct pon_test_ctx *ctx, unsigned int group)
{
	int id = pon_nl_family_get()->mcgrp_offset + group;

	return ctx->nl_sock->ops->setsockopt(ctx->nl_sock, SOL_NETLINK,
					     NETLINK_ADD_MEMBERSHIP,
					     KERNEL_SOCKPTR(&id), sizeof(id));
}

struct sk_buff *pon_test_nl_new(struct pon_test_ctx *ctx, u8 cmd, u16 flags)
{
	struct sk_buff *skb;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, skb);
	KUNIT_ASSERT_NOT_NULL(ctx->test,
			      genlmsg_put(skb, ctx->nl_portid, ++ctx->nl_seq,
					  pon_nl_family_get(),
					  NLM_F_REQUEST | flags, cmd));

	return skb;
}

static int pon_test_nl_send(struct pon_test_ctx *ctx, struct sk_buff *skb)
{
	struct sockaddr_nl dst = {
		.nl_family = AF_NETLINK,
	};
	struct msghdr msg = {
		.msg_name = &dst,
		.msg_namelen = sizeof(dst),
	};
	struct kvec iov;
	int ret;

	nlmsg_hdr(skb)->nlmsg_len = skb->len;
	iov.iov_base = skb->data;
	iov.iov_len = skb->len;
	ret = kernel_sendmsg(ctx->nl_sock, &msg, &iov, 1, skb->len);
	consume_skb(skb);

	return ret < 0 ? ret : 0;
}

static int pon_test_nl_recv(struct pon_test_ctx *ctx)
{
	struct msghdr msg = {};
	struct kvec iov = {
		.iov_base = ctx->nl_buf,
		.iov_len = T_NL_BUF_LEN,
	};

	return kernel_recvmsg(ctx->nl_sock, &msg, &iov, 1, T_NL_BUF_LEN,
			      MSG_DONTWAIT);
}

static struct nlmsghdr *pon_test_nl_copy(struct pon_test_ctx *ctx,
					 const struct nlmsghdr *nlh)
{
	struct nlmsghdr *copy;

	copy = kunit_kmalloc(ctx->test, nlh->nlmsg_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, copy);
	memcpy(copy, nlh, nlh->nlmsg_len);

	return copy;
}

static void pon_test_nl_keep(struct pon_test_ctx *ctx, struct list_head *list,
			     const struct nlmsghdr *nlh)
{
	struct pon_test_nl_msg *msg;

	msg = kunit_kzalloc(ctx->test, sizeof(*msg), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, msg);
	msg->nlh = pon_test_nl_copy(ctx, nlh);
	list_add_tail(&msg->list, list);
}

static int pon_test_nl_ack(const struct nlmsghdr *nlh)
{
	const struct nlmsgerr *ack = nlmsg_data(nlh);

	return ack->error;
}

int pon_test_nl_request(struct pon_test_ctx *ctx, struct sk_buff *skb,
			struct nlmsghdr **reply)
{
	u32 seq = nlmsg_hdr(skb)->nlmsg_seq;
	struct nlmsghdr *nlh;
	int len, err;

	if (reply)
		*reply = NULL;

	nlmsg_hdr(skb)->nlmsg_flags |= NLM_F_ACK;
	err = pon_test_nl_send(ctx, skb);
	if (err)
		return err;

	for (;;) {
		len = pon_test_nl_recv(ctx);
		if (len < 0)
			return -ETIMEDOUT;

		nlh = (struct nlmsghdr *)ctx->nl_buf;
		for (; nlmsg_ok(nlh, len); nlh = nlmsg_next(nlh, &len)) {
			if (nlh->nlmsg_seq != seq) {
				pon_test_nl_keep(ctx, &ctx->nl_pending, nlh);
				continue;
			}
			if (nlh->nlmsg_type == NLMSG_ERROR)
				return pon_test_nl_ack(nlh);
			if (reply)
				*reply = pon_test_nl_copy(ctx, nlh);
		}
	}
}

int pon_test_nl_dump_split(struct pon_test_ctx *ctx, struct sk_buff *skb,
			   struct list_head *msgs, bool *intr,
			   void (*between)(struct pon_test_ctx *ctx))
{
	u32 seq = nlmsg_hdr(skb)->nlmsg_seq;
	struct nlmsghdr *nlh;
	int len, err, count = 0;

	if (intr)
		*intr = false;

	nlmsg_hdr(skb)->nlmsg_flags |= NLM_F_DUMP;
	err = pon_test_nl_send(ctx, skb);
	if (err)
		return err;

	if (between)
		between(ctx);

	for (;;) {
		len = pon_test_nl_recv(ctx);
		if (len < 0)
			return -ETIMEDOUT;

		nlh = (struct nlmsghdr *)ctx->nl_buf;
		for (; nlmsg_ok(nlh, len); nlh = nlmsg_next(nlh, &len)) {
			if (nlh->nlmsg_seq != seq) {
				pon_test_nl_keep(ctx, &ctx->nl_pending, nlh);
				continue;
			}
			if (intr && (nlh->nlmsg_flags & NLM_F_DUMP_INTR))
				*intr = true;
			if (nlh->nlmsg_type == NLMSG_ERROR)
				return pon_test_nl_ack(nlh);
			if (nlh->nlmsg_type == NLMSG_DONE) {
				err = *(int *)nlmsg_data(nlh);
				return err ? err : count;
			}
			if (msgs)
				pon_test_nl_keep(ctx, msgs, nlh);
			count++;
		}
	}
}

int pon_test_nl_dump(struct pon_test_ctx *ctx, struct sk_buff *skb,
		     struct list_head *msgs, bool *intr)
{
	return pon_test_nl_dump_split(ctx, skb, msgs, intr, NULL);
}

static void pon_test_nl_drain(struct pon_test_ctx *ctx)
{
	struct nlmsghdr *nlh;
	int len;

	while ((len = pon_test_nl_recv(ctx)) > 0) {
		nlh = (struct nlmsghdr *)ctx->nl_buf;
		for (; nlmsg_ok(nlh, len); nlh = nlmsg_next(nlh, &len))
			pon_test_nl_keep(ctx, &ctx->nl_pending, nlh);
	}
}

struct nlmsghdr *pon_test_nl_ntf(struct pon_test_ctx *ctx, u8 cmd)
{
	struct pon_test_nl_msg *msg;

	pon_test_nl_drain(ctx);

	list_for_each_entry(msg, &ctx->nl_pending, list) {
		if (msg->nlh->nlmsg_type != pon_nl_family_get()->id ||
		    pon_test_nl_cmd(msg->nlh) != cmd)
			continue;
		list_del(&msg->list);
		return msg->nlh;
	}

	return NULL;
}

void pon_test_nl_ntf_flush(struct pon_test_ctx *ctx)
{
	pon_test_nl_drain(ctx);
	INIT_LIST_HEAD(&ctx->nl_pending);
}

int pon_test_nl_parse(const struct nlmsghdr *nlh, struct nlattr **tb,
		      int maxtype)
{
	return nlmsg_parse_deprecated(nlh, GENL_HDRLEN, tb, maxtype, NULL,
				      NULL);
}

u8 pon_test_nl_cmd(const struct nlmsghdr *nlh)
{
	const struct genlmsghdr *hdr = nlmsg_data(nlh);

	return hdr->cmd;
}
