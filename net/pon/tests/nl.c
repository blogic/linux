// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/test.h>
#include <linux/if_vlan.h>
#include <linux/kmsg_dump.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include <net/genetlink.h>
#include <net/netlink.h>

#include "../pon-nl-gen.h"
#include "pon_test.h"

#define T_UNSET			U32_MAX
#define T_UNKNOWN_DEV_ID	60000
#define T_SERIAL		"ABCD1234"
#define T_SERIAL_OTHER		"ABCD5678"
#define T_REG_ID		"reg-1"
#define T_REG_ID_OTHER		"reg-two"
#define T_RESPONSE_TIME		35000
#define T_RX_FRAMES		77
#define T_VID			10
#define T_OTHER_VID		20
#define T_REBOOT_DEPTH		2
#define T_REBOOT_IMAGE		1
#define T_REBOOT_CALLS		PON_REBOOT_CALLS_NO_EMERGENCY_CALLS
#define T_TC_DRV_BASE		0x100000000ULL
#define T_TC_CORE_BASE		0x200000000ULL
#define T_TC_COUNT		(sizeof(struct pon_tc_stats) / sizeof(u64))
#define T_DUMP_GEMS		1000
#define T_OMCI_DEVID_OFF	3
#define T_OMCI_DEVID_BASELINE	0x0a
#define T_OMCI_DEVID_EXTENDED	0x0b
#define T_OMCI_CONTENTS_LEN_OFF	8
#define T_OMCI_EXT_HDR_LEN	10
#define T_OMCI_EXT_CONTENTS	100
#define T_GEM_QUEUE		3
#define T_GEM_QUEUE_MAX		7
#define T_KEY_RING_MAX		PON_GEM_KEY_RING_UNICAST_DOWNSTREAM
#define T_PBIT			5
#define T_PBIT_MAX		7
#define T_DSCP			46
#define T_DSCP_MAX		63
#define T_MODE_LOG_MAX		4

static int pon_nl_test_init(struct kunit *test)
{
	int err;

	err = pon_test_init_full(test);
	if (err)
		return err;

	return pon_test_nl_open(test);
}

static void t_put_u32(struct pon_test_ctx *ctx, struct sk_buff *skb, int type,
		      u32 value)
{
	KUNIT_ASSERT_EQ(ctx->test, nla_put_u32(skb, type, value), 0);
}

static void t_put_opt(struct pon_test_ctx *ctx, struct sk_buff *skb, int type,
		      u32 value)
{
	if (value != T_UNSET)
		t_put_u32(ctx, skb, type, value);
}

static void t_put_u8(struct pon_test_ctx *ctx, struct sk_buff *skb, int type,
		     u8 value)
{
	KUNIT_ASSERT_EQ(ctx->test, nla_put_u8(skb, type, value), 0);
}

static void t_put_str(struct pon_test_ctx *ctx, struct sk_buff *skb, int type,
		      const char *value)
{
	KUNIT_ASSERT_EQ(ctx->test, nla_put(skb, type, strlen(value), value), 0);
}

static struct sk_buff *t_msg(struct pon_test_ctx *ctx, u8 cmd)
{
	struct sk_buff *skb;

	skb = pon_test_nl_new(ctx, cmd, 0);
	t_put_u32(ctx, skb, PON_A_DEV_ID, ctx->pdev->id);

	return skb;
}

static int t_request(struct pon_test_ctx *ctx, struct sk_buff *skb)
{
	struct nlmsghdr *reply;
	int err;

	err = pon_test_nl_request(ctx, skb, &reply);
	KUNIT_EXPECT_NULL(ctx->test, reply);

	return err;
}

static int t_get(struct pon_test_ctx *ctx, struct sk_buff *skb,
		 struct nlattr **tb, int maxtype)
{
	struct nlmsghdr *reply;
	int err;

	err = pon_test_nl_request(ctx, skb, &reply);
	if (err)
		return err;

	KUNIT_ASSERT_NOT_NULL(ctx->test, reply);
	KUNIT_ASSERT_EQ(ctx->test, pon_test_nl_parse(reply, tb, maxtype), 0);

	return 0;
}

static void t_dev_get(struct pon_test_ctx *ctx, struct nlattr **tb)
{
	KUNIT_ASSERT_EQ(ctx->test,
			t_get(ctx, t_msg(ctx, PON_CMD_DEV_GET), tb,
			      PON_A_DEV_MAX), 0);
}

static int t_dump(struct pon_test_ctx *ctx, u8 cmd, struct list_head *msgs,
		  bool *intr)
{
	return pon_test_nl_dump(ctx, pon_test_nl_new(ctx, cmd, 0), msgs, intr);
}

static struct nlattr **t_parse(struct pon_test_ctx *ctx,
			       const struct nlmsghdr *nlh, int maxtype)
{
	struct nlattr **tb;

	tb = kunit_kcalloc(ctx->test, maxtype + 1, sizeof(*tb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, tb);
	KUNIT_ASSERT_EQ(ctx->test, pon_test_nl_parse(nlh, tb, maxtype), 0);

	return tb;
}

static struct pon_dev_ops *t_ops_copy(struct pon_test_ctx *ctx,
				      const struct pon_dev_ops *src)
{
	struct pon_dev_ops *ops;

	ops = kunit_kmalloc(ctx->test, sizeof(*ops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, ops);
	*ops = *src;

	return ops;
}

static int t_dev_set_enable(struct pon_test_ctx *ctx, u8 on)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);

	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, on);

	return t_request(ctx, skb);
}

static int t_dev_set_serial(struct pon_test_ctx *ctx)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);

	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL);

	return t_request(ctx, skb);
}

static int t_tcont_set(struct pon_test_ctx *ctx, u32 index, u32 alloc_id)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_TCONT_SET);

	t_put_u32(ctx, skb, PON_A_TCONT_INDEX, index);
	t_put_u32(ctx, skb, PON_A_TCONT_ALLOC_ID, alloc_id);

	return t_request(ctx, skb);
}

static int t_tcont_get(struct pon_test_ctx *ctx, u32 index, u32 *alloc_id)
{
	struct nlattr *tb[PON_A_TCONT_MAX + 1];
	struct sk_buff *skb = t_msg(ctx, PON_CMD_TCONT_GET);
	int err;

	t_put_u32(ctx, skb, PON_A_TCONT_INDEX, index);
	err = t_get(ctx, skb, tb, PON_A_TCONT_MAX);
	if (err)
		return err;

	KUNIT_ASSERT_NOT_NULL(ctx->test, tb[PON_A_TCONT_ALLOC_ID]);
	*alloc_id = nla_get_u32(tb[PON_A_TCONT_ALLOC_ID]);

	return 0;
}

static int t_tcont_del(struct pon_test_ctx *ctx, u32 index)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_TCONT_DEL);

	t_put_u32(ctx, skb, PON_A_TCONT_INDEX, index);

	return t_request(ctx, skb);
}

static int t_gem_new_full(struct pon_test_ctx *ctx, u32 gem_id, u32 dir,
			  u32 tcont_index, u32 key_ring)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_GEM_NEW);

	t_put_opt(ctx, skb, PON_A_GEM_ID, gem_id);
	t_put_opt(ctx, skb, PON_A_GEM_DIR, dir);
	t_put_opt(ctx, skb, PON_A_GEM_TCONT_INDEX, tcont_index);
	t_put_opt(ctx, skb, PON_A_GEM_KEY_RING, key_ring);

	return t_request(ctx, skb);
}

static int t_gem_new(struct pon_test_ctx *ctx, u32 gem_id, u32 tcont_index)
{
	return t_gem_new_full(ctx, gem_id, PON_GEM_DIR_BIDIR, tcont_index,
			      T_UNSET);
}

static int t_gem_new_down(struct pon_test_ctx *ctx, u32 gem_id)
{
	return t_gem_new_full(ctx, gem_id, PON_GEM_DIR_DOWNSTREAM, T_UNSET,
			      T_UNSET);
}

static int t_gem_new_no_offload(struct pon_test_ctx *ctx, u32 gem_id,
				u8 no_offload)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_GEM_NEW);

	t_put_u32(ctx, skb, PON_A_GEM_ID, gem_id);
	t_put_u32(ctx, skb, PON_A_GEM_DIR, PON_GEM_DIR_DOWNSTREAM);
	t_put_u8(ctx, skb, PON_A_GEM_NO_OFFLOAD, no_offload);

	return t_request(ctx, skb);
}

static int t_gem_get(struct pon_test_ctx *ctx, u32 gem_id)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_GEM_GET);

	t_put_u32(ctx, skb, PON_A_GEM_ID, gem_id);

	return pon_test_nl_request(ctx, skb, NULL);
}

static int t_gem_del(struct pon_test_ctx *ctx, u32 gem_id)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_GEM_DEL);

	t_put_u32(ctx, skb, PON_A_GEM_ID, gem_id);

	return t_request(ctx, skb);
}

static struct pon_gem_cfg t_gem_cfg(struct pon_test_ctx *ctx, u16 gem_id)
{
	struct pon_gem_cfg cfg = {};
	struct pon_gem *gem;

	mutex_lock(&ctx->pdev->lock);
	gem = pon_gem_find(ctx->pdev, gem_id);
	if (gem)
		cfg = gem->cfg;
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_ASSERT_NOT_NULL(ctx->test, gem);

	return cfg;
}

static int t_map(struct pon_test_ctx *ctx, u8 cmd, u32 gem_id, u32 tag,
		 u32 vid)
{
	struct sk_buff *skb = t_msg(ctx, cmd);

	t_put_u32(ctx, skb, PON_A_GEM_MAP_GEM_ID, gem_id);
	t_put_opt(ctx, skb, PON_A_GEM_MAP_TAG, tag);
	t_put_opt(ctx, skb, PON_A_GEM_MAP_VID, vid);

	return t_request(ctx, skb);
}

static int t_map_count(struct pon_test_ctx *ctx, struct list_head *msgs)
{
	return t_dump(ctx, PON_CMD_GEM_MAP_GET, msgs, NULL);
}

static struct nlattr **t_map_first(struct pon_test_ctx *ctx)
{
	struct pon_test_nl_msg *msg;
	LIST_HEAD(msgs);

	KUNIT_ASSERT_EQ(ctx->test, t_map_count(ctx, &msgs), 1);
	msg = list_first_entry(&msgs, struct pon_test_nl_msg, list);

	return t_parse(ctx, msg->nlh, PON_A_GEM_MAP_MAX);
}

static int t_simple_get(struct pon_test_ctx *ctx, u8 cmd, struct nlattr **tb,
			int maxtype)
{
	return t_get(ctx, t_msg(ctx, cmd), tb, maxtype);
}

static int t_omci_register(struct pon_test_ctx *ctx)
{
	return t_request(ctx, t_msg(ctx, PON_CMD_OMCI_REGISTER));
}

static int t_omci_tx_pdu(struct pon_test_ctx *ctx, unsigned int len,
			 u8 devid, u16 contents_len)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_OMCI_TX);
	u8 *pdu;

	pdu = kunit_kzalloc(ctx->test, len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, pdu);
	if (len > T_OMCI_DEVID_OFF)
		pdu[T_OMCI_DEVID_OFF] = devid;
	if (len >= T_OMCI_EXT_HDR_LEN)
		put_unaligned_be16(contents_len,
				   pdu + T_OMCI_CONTENTS_LEN_OFF);
	KUNIT_ASSERT_EQ(ctx->test, nla_put(skb, PON_A_OMCI_PDU, len, pdu), 0);

	return t_request(ctx, skb);
}

static int t_omci_tx(struct pon_test_ctx *ctx, unsigned int len)
{
	if (len == T_OMCI_BASELINE_LEN)
		return t_omci_tx_pdu(ctx, len, T_OMCI_DEVID_BASELINE, 0);
	if (len < T_OMCI_EXT_HDR_LEN)
		return t_omci_tx_pdu(ctx, len, T_OMCI_DEVID_EXTENDED, 0);

	return t_omci_tx_pdu(ctx, len, T_OMCI_DEVID_EXTENDED,
			     len - T_OMCI_EXT_HDR_LEN);
}

static void t_event(struct pon_test_ctx *ctx, const struct pon_event *ev)
{
	mutex_lock(&ctx->pdev->lock);
	pon_dev_event(ctx->pdev, ev);
	mutex_unlock(&ctx->pdev->lock);
}

static struct nlattr **t_event_ntf(struct pon_test_ctx *ctx, u32 type)
{
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	ntf = pon_test_nl_ntf(ctx, PON_CMD_EVENT_NTF);
	KUNIT_ASSERT_NOT_NULL(ctx->test, ntf);
	tb = t_parse(ctx, ntf, PON_A_EVENT_MAX);
	KUNIT_EXPECT_EQ(ctx->test, nla_get_u32(tb[PON_A_EVENT_DEV_ID]),
			ctx->pdev->id);
	KUNIT_EXPECT_EQ(ctx->test, nla_get_u32(tb[PON_A_EVENT_TYPE]), type);

	return tb;
}

static void pon_nl_dev_get_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];
	struct nlmsghdr *reply;
	struct sk_buff *skb;

	skb = pon_test_nl_new(ctx, PON_CMD_DEV_GET, 0);
	KUNIT_ASSERT_EQ(test, nla_put_u32(skb, PON_A_DEV_ID, ctx->pdev->id), 0);
	KUNIT_ASSERT_EQ(test, pon_test_nl_request(ctx, skb, &reply), 0);
	KUNIT_ASSERT_NOT_NULL(test, reply);
	KUNIT_ASSERT_EQ(test, pon_test_nl_parse(reply, tb, PON_A_DEV_MAX), 0);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_DEV_ID]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_ID]), ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_IFINDEX]),
			ctx->netdev->ifindex);
}

static void pon_nl_tcont_set_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_TCONT_MAX + 1];
	struct nlmsghdr *ntf;
	struct sk_buff *skb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);

	skb = pon_test_nl_new(ctx, PON_CMD_TCONT_SET, 0);
	KUNIT_ASSERT_EQ(test, nla_put_u32(skb, PON_A_TCONT_DEV_ID,
					  ctx->pdev->id), 0);
	KUNIT_ASSERT_EQ(test, nla_put_u32(skb, PON_A_TCONT_INDEX,
					  T_TCONT_INDEX), 0);
	KUNIT_ASSERT_EQ(test, nla_put_u32(skb, PON_A_TCONT_ALLOC_ID,
					  T_TCONT_ALLOC_ID), 0);
	KUNIT_ASSERT_EQ(test, pon_test_nl_request(ctx, skb, NULL), 0);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 1);

	ntf = pon_test_nl_ntf(ctx, PON_CMD_TCONT_ADD_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	KUNIT_ASSERT_EQ(test, pon_test_nl_parse(ntf, tb, PON_A_TCONT_MAX), 0);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_ALLOC_ID]),
			T_TCONT_ALLOC_ID);
}

static void pon_nl_dev_id_missing_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	skb = pon_test_nl_new(ctx, PON_CMD_DEV_GET, 0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);
}

static void pon_nl_dev_id_unknown_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	skb = pon_test_nl_new(ctx, PON_CMD_DEV_GET, 0);
	t_put_u32(ctx, skb, PON_A_DEV_ID, T_UNKNOWN_DEV_ID);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ENODEV);
}

static void pon_nl_dev_id_unregistered_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_unregister(ctx);
	KUNIT_EXPECT_EQ(test, t_request(ctx, t_msg(ctx, PON_CMD_DEV_GET)),
			-ENODEV);
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			-ENODEV);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 0);
}

static void pon_nl_dev_set_mode_caps_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);

	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_GPON);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->identities, 0);
	KUNIT_EXPECT_EQ(test, ctx->pdev->mode, PON_MODE_XGS_PON);
}

static void pon_nl_dev_set_mode_log_test(struct kunit *test)
{
	char lines[T_MODE_LOG_MAX][T_LOG_TEXT_LEN];
	struct kmsg_dump_iter iter, level_iter;
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	kmsg_dump_rewind(&iter);
	iter.cur_seq = iter.next_seq;

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_GPON);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_XG_PON);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_XGS_PON);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), 0);

	pon_test_log_flush(ctx);
	level_iter = iter;
	KUNIT_ASSERT_EQ(test,
			pon_test_log_lines(test, &iter, "mode ", lines,
					   T_MODE_LOG_MAX), 2);
	KUNIT_EXPECT_STREQ(test, lines[0], "gpon not supported by the device");
	KUNIT_EXPECT_STREQ(test, lines[1],
			   "xg-pon not supported by the device");
	KUNIT_EXPECT_EQ(test,
			pon_test_log_level(test, &level_iter,
					   "mode gpon not supported"),
			LOGLEVEL_ERR);
}

static void pon_nl_dev_set_disabled_busy_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	KUNIT_ASSERT_EQ(test, t_dev_set_enable(ctx, 1), 0);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	KUNIT_ASSERT_EQ(test, nla_put_flag(skb, PON_A_DEV_DISABLED), 0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EBUSY);
	KUNIT_EXPECT_EQ(test, ctx->identities, 0);
}

static void pon_nl_dev_set_empty_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_request(ctx, t_msg(ctx, PON_CMD_DEV_SET)),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->identities, 0);
	KUNIT_EXPECT_EQ(test, ctx->enables, 0);
}

static void pon_nl_dev_set_no_identity_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev_ops *ops = t_ops_copy(ctx, &pon_test_ops_full);
	struct nlattr *tb[PON_A_DEV_MAX + 1];

	ops->set_identity = NULL;
	pon_test_ops_set(ctx, ops);

	KUNIT_EXPECT_EQ(test, t_dev_set_serial(ctx), -EOPNOTSUPP);
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_SERIAL]);
}

static void pon_nl_dev_set_no_enable_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_ops_set(ctx, &pon_test_ops);

	KUNIT_EXPECT_EQ(test, t_dev_set_enable(ctx, 1), -EOPNOTSUPP);
	KUNIT_EXPECT_FALSE(test, ctx->pdev->enabled);
}

static void pon_nl_dev_set_identity_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];

	ctx->identity_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_dev_set_serial(ctx), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->identities, 1);

	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_SERIAL]);
}

static void pon_nl_dev_set_commit_members_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;
	struct sk_buff *skb;

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL);
	t_put_str(ctx, skb, PON_A_DEV_REGISTRATION_ID, T_REG_ID);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_MEMEQ(test, ctx->identity.reg_id, T_REG_ID,
			   strlen(T_REG_ID));

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_XGS_PON);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_TRUE(test, ctx->identity.mode_set);
	KUNIT_EXPECT_FALSE(test, ctx->identity.serial_set);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_REGISTRATION_ID, T_REG_ID_OTHER);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_MEMEQ(test, ctx->identity.reg_id, T_REG_ID_OTHER,
			   strlen(T_REG_ID_OTHER));

	mutex_lock(&pdev->lock);
	KUNIT_EXPECT_TRUE(test, pdev->identity.serial_set);
	KUNIT_EXPECT_MEMEQ(test, pdev->identity.serial, T_SERIAL,
			   PON_SERIAL_LEN);
	KUNIT_EXPECT_EQ(test, pdev->mode, PON_MODE_XGS_PON);
	mutex_unlock(&pdev->lock);
}

static void pon_nl_dev_set_identity_busy_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 1);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_ASSERT_EQ(test, ctx->identities, 1);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL_OTHER);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EBUSY);
	KUNIT_EXPECT_EQ(test, ctx->identities, 1);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL);
	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_XGS_PON);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_EQ(test, ctx->identities, 2);

	mutex_lock(&ctx->pdev->lock);
	ctx->pdev->mode = PON_MODE_GPON;
	mutex_unlock(&ctx->pdev->lock);
	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u32(ctx, skb, PON_A_DEV_MODE, PON_MODE_XGS_PON);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EBUSY);
	KUNIT_EXPECT_EQ(test, ctx->identities, 2);

	KUNIT_ASSERT_EQ(test, t_dev_set_enable(ctx, 0), 0);
	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL_OTHER);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_EQ(test, ctx->identities, 3);
}

static void pon_nl_dev_set_serial_zero_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);
	u8 zero[PON_SERIAL_LEN] = {};

	KUNIT_ASSERT_EQ(test, nla_put(skb, PON_A_DEV_SERIAL, sizeof(zero),
				      zero), 0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->identities, 0);
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_SERIAL]);
}

static void pon_nl_dev_set_reg_id_pad_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);
	size_t len = strlen(T_REG_ID);

	t_put_str(ctx, skb, PON_A_DEV_REGISTRATION_ID, T_REG_ID);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_EQ(test, ctx->identity.reg_id_len, PON_REG_ID_LEN);
	KUNIT_EXPECT_MEMEQ(test, ctx->identity.reg_id, T_REG_ID, len);
	KUNIT_EXPECT_NULL(test, memchr_inv(ctx->identity.reg_id + len, 0,
					   PON_REG_ID_LEN - len));
}

static void pon_nl_dev_set_reg_id_empty_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);

	KUNIT_ASSERT_EQ(test, nla_put(skb, PON_A_DEV_REGISTRATION_ID, 0, NULL),
			0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	KUNIT_ASSERT_EQ(test, nla_put(skb, PON_A_DEV_REGISTRATION_ID, 0, NULL),
			0);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EINVAL);

	KUNIT_EXPECT_EQ(test, ctx->identities, 0);
	KUNIT_EXPECT_EQ(test, ctx->enables, 0);
}

static void pon_nl_dev_set_disabled_not_stored_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);

	KUNIT_ASSERT_EQ(test, nla_put_flag(skb, PON_A_DEV_DISABLED), 0);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_EQ(test, ctx->identities, 1);
	KUNIT_EXPECT_TRUE(test, ctx->identity.disabled);

	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_DISABLED]);
}

static void pon_nl_dev_set_enable_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];
	struct sk_buff *skb;

	ctx->enable_err = -EIO;
	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->enables, 1);

	t_dev_get(ctx, tb);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_DEV_SERIAL]);
	KUNIT_EXPECT_MEMEQ(test, nla_data(tb[PON_A_DEV_SERIAL]), T_SERIAL,
			   PON_SERIAL_LEN);
	KUNIT_EXPECT_EQ(test, nla_get_u8(tb[PON_A_DEV_ENABLE]), 0);
	KUNIT_EXPECT_FALSE(test, ctx->pdev->enabled);
}

static void pon_nl_dev_set_enable_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];

	KUNIT_ASSERT_EQ(test, t_dev_set_enable(ctx, 1), 0);
	KUNIT_EXPECT_TRUE(test, ctx->enable_on);
	KUNIT_EXPECT_EQ(test, ctx->identities, 0);

	t_dev_get(ctx, tb);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_DEV_ENABLE]);
	KUNIT_EXPECT_EQ(test, nla_get_u8(tb[PON_A_DEV_ENABLE]), 1);

	KUNIT_ASSERT_EQ(test, t_dev_set_enable(ctx, 0), 0);
	KUNIT_EXPECT_FALSE(test, ctx->enable_on);
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_EQ(test, nla_get_u8(tb[PON_A_DEV_ENABLE]), 0);
}

static void pon_nl_dev_get_disabled_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_DISABLED]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_PLOAM_STATE]),
			PON_PLOAM_STATE_O1);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O7);
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NOT_NULL(test, tb[PON_A_DEV_DISABLED]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_PLOAM_STATE]),
			PON_PLOAM_STATE_O7);
}

static void pon_nl_dev_get_response_time_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];

	ctx->response_time = T_RESPONSE_TIME;
	t_dev_get(ctx, tb);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_DEV_RESPONSE_TIME]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_RESPONSE_TIME]),
			T_RESPONSE_TIME);

	ctx->response_time_err = -EIO;
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_RESPONSE_TIME]);

	pon_test_ops_set(ctx, &pon_test_ops);
	ctx->response_time_err = 0;
	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_RESPONSE_TIME]);
}

static void pon_nl_dev_get_serial_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];

	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_SERIAL]);

	KUNIT_ASSERT_EQ(test, t_dev_set_serial(ctx), 0);
	t_dev_get(ctx, tb);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_DEV_SERIAL]);
	KUNIT_EXPECT_EQ(test, nla_len(tb[PON_A_DEV_SERIAL]), PON_SERIAL_LEN);
	KUNIT_EXPECT_MEMEQ(test, nla_data(tb[PON_A_DEV_SERIAL]), T_SERIAL,
			   PON_SERIAL_LEN);
}

static void pon_nl_tcont_set_index_range_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u16 max = ctx->pdev->caps->max_tconts;
	u32 alloc_id;

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, max, T_TCONT_ALLOC_ID), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 0);
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, max - 1, T_TCONT_ALLOC_ID), 0);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, max - 1, &alloc_id), 0);
}

static void pon_nl_tcont_set_alloc_range_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX,
					  PON_PLOAM_ALLOC_ID_MAX + 1), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 0);
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX,
					  PON_PLOAM_ALLOC_ID_MAX), 0);
}

static void pon_nl_tcont_set_sn_grant_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	for (alloc_id = PON_PLOAM_ALLOC_ID_SN_GRANT_MIN;
	     alloc_id <= PON_PLOAM_ALLOC_ID_SN_GRANT_2G5; alloc_id++)
		KUNIT_EXPECT_EQ_MSG(test,
				    t_tcont_set(ctx, T_TCONT_INDEX, alloc_id),
				    -EINVAL, "alloc-id %u", alloc_id);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 0);

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX,
					  PON_PLOAM_ALLOC_ID_SN_GRANT_MIN - 1),
			0);
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_OTHER_TCONT_INDEX,
					  PON_PLOAM_ALLOC_ID_SN_GRANT_2G5 + 1),
			0);
}

static void pon_nl_tcont_set_alloc_taken_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_OTHER_TCONT_INDEX,
					  T_TCONT_ALLOC_ID), -EEXIST);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 1);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_OTHER_TCONT_INDEX, &alloc_id),
			-ENOENT);
}

static void pon_nl_tcont_set_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	ctx->tcont_set_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			-EIO);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 1);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_TCONT_INDEX, &alloc_id),
			-ENOENT);
	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_TCONT_GET, NULL, NULL), 0);
}

static void pon_nl_tcont_set_same_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);
	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);
	KUNIT_ASSERT_EQ(test, ctx->gem_adds, 1);
	pon_test_nl_ntf_flush(ctx);

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 2);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 1);
	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_TCONT_ADD_NTF));
	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_TCONT_CHANGE_NTF));
	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_TCONT_GET, NULL, NULL), 1);
}

static void pon_nl_tcont_set_rebind_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);
	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID + 1), 0);

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_OTHER_ALLOC_ID),
			0);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 3);
	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[0], T_OTHER_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_gem_cfg(ctx, T_GEM_ID).alloc_id,
			T_OTHER_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_gem_cfg(ctx, T_GEM_ID + 1).alloc_id, 0);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_TCONT_INDEX, &alloc_id), 0);
	KUNIT_EXPECT_EQ(test, alloc_id, T_OTHER_ALLOC_ID);
}

static void pon_nl_tcont_set_rebind_restore_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);
	ctx->gem_fail_id = T_GEM_ID;

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_OTHER_ALLOC_ID),
			-EIO);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 3);
	KUNIT_EXPECT_EQ(test, ctx->tcont_set_last.index, T_TCONT_INDEX);
	KUNIT_EXPECT_EQ(test, ctx->tcont_set_last.alloc_id, T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_gem_cfg(ctx, T_GEM_ID).alloc_id,
			T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_TCONT_INDEX, &alloc_id), 0);
	KUNIT_EXPECT_EQ(test, alloc_id, T_TCONT_ALLOC_ID);
}

static void pon_nl_tcont_set_ets_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tcont *tcont;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);
	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	pon_test_alloc_bind(ctx, T_OTHER_ALLOC_ID);

	mutex_lock(&ctx->pdev->lock);
	tcont = list_first_entry(&ctx->pdev->tconts, struct pon_tcont, list);
	tcont->ets_set = true;
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 0);

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_OTHER_ALLOC_ID),
			0);
	flush_work(&ctx->pdev->tc_work);
	KUNIT_EXPECT_EQ(test, ctx->conduit_setup_tcs, 1);
	KUNIT_EXPECT_EQ(test, ctx->conduit_channel, T_OTHER_CHANNEL);
	KUNIT_EXPECT_EQ(test, ctx->conduit_ets_command, TC_ETS_REPLACE);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_FALSE(test, tcont->ets_pending);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_nl_tcont_set_change_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);
	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_OTHER_ALLOC_ID),
			0);

	ntf = pon_test_nl_ntf(ctx, PON_CMD_TCONT_ADD_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_TCONT_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_ALLOC_ID]),
			T_TCONT_ALLOC_ID);

	ntf = pon_test_nl_ntf(ctx, PON_CMD_TCONT_CHANGE_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_TCONT_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_DEV_ID]),
			ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_INDEX]),
			T_TCONT_INDEX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_ALLOC_ID]),
			T_OTHER_ALLOC_ID);

	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_TCONT_ADD_NTF));
}

static void pon_nl_tcont_del_unknown_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_tcont_del(ctx, T_TCONT_INDEX), -ENOENT);
	KUNIT_EXPECT_EQ(test, ctx->tcont_clears, 0);
}

static void pon_nl_tcont_del_busy_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);

	KUNIT_EXPECT_EQ(test, t_tcont_del(ctx, T_TCONT_INDEX), -EBUSY);
	KUNIT_EXPECT_EQ(test, ctx->tcont_clears, 0);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_TCONT_INDEX, &alloc_id), 0);
}

static void pon_nl_tcont_del_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u32 alloc_id;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	ctx->tcont_clear_err = -EIO;

	KUNIT_EXPECT_EQ(test, t_tcont_del(ctx, T_TCONT_INDEX), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->tcont_clears, 1);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_TCONT_INDEX, &alloc_id), 0);
	KUNIT_EXPECT_EQ(test, alloc_id, T_TCONT_ALLOC_ID);
}

static void pon_nl_tcont_del_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct nlattr **tb;
	u32 alloc_id;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);
	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);

	KUNIT_EXPECT_EQ(test, t_tcont_del(ctx, T_TCONT_INDEX), 0);
	KUNIT_EXPECT_EQ(test, ctx->tcont_clears, 1);
	KUNIT_EXPECT_EQ(test, t_tcont_get(ctx, T_TCONT_INDEX, &alloc_id),
			-ENOENT);

	ntf = pon_test_nl_ntf(ctx, PON_CMD_TCONT_DEL_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_TCONT_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_INDEX]),
			T_TCONT_INDEX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_ALLOC_ID]),
			T_TCONT_ALLOC_ID);
}

static void pon_nl_gem_new_missing_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_UNSET, PON_GEM_DIR_BIDIR,
					     T_UNSET, T_UNSET), -EINVAL);
	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID, T_UNSET, T_UNSET,
					     T_UNSET), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);
}

static void pon_nl_gem_new_broadcast_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev_ops *ops = t_ops_copy(ctx, &pon_test_ops_full);

	ops->bcast_key_set = NULL;
	pon_test_ops_set(ctx, ops);
	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_DOWNSTREAM, T_UNSET,
					     PON_GEM_KEY_RING_BROADCAST),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);
	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_DOWNSTREAM, T_UNSET,
					     PON_GEM_KEY_RING_UNICAST), 0);
}

static void pon_nl_gem_new_broadcast_served_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_DOWNSTREAM, T_UNSET,
					     PON_GEM_KEY_RING_BROADCAST), 0);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 1);
}

static void pon_nl_gem_new_unknown_tcont_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), -ENOENT);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);
	KUNIT_EXPECT_EQ(test, t_gem_get(ctx, T_GEM_ID), -ENOENT);
}

static void pon_nl_gem_new_alloc_id_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem_cfg cfg;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);

	KUNIT_EXPECT_EQ(test, ctx->gem_alloc_id[0], T_TCONT_ALLOC_ID);
	cfg = t_gem_cfg(ctx, T_GEM_ID);
	KUNIT_EXPECT_TRUE(test, cfg.tcont_valid);
	KUNIT_EXPECT_EQ(test, cfg.tcont_index, T_TCONT_INDEX);
	KUNIT_EXPECT_EQ(test, cfg.alloc_id, T_TCONT_ALLOC_ID);
}

static void pon_nl_gem_new_untied_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_test_nl_msg *msg;
	struct pon_gem_cfg cfg;
	struct nlattr **tb;
	LIST_HEAD(msgs);

	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	cfg = t_gem_cfg(ctx, T_GEM_ID);
	KUNIT_EXPECT_FALSE(test, cfg.tcont_valid);
	KUNIT_EXPECT_EQ(test, cfg.alloc_id, 0);

	KUNIT_ASSERT_EQ(test, t_dump(ctx, PON_CMD_GEM_GET, &msgs, NULL), 1);
	msg = list_first_entry(&msgs, struct pon_test_nl_msg, list);
	tb = t_parse(ctx, msg->nlh, PON_A_GEM_MAX);
	KUNIT_EXPECT_NULL(test, tb[PON_A_GEM_TCONT_INDEX]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_ID]), T_GEM_ID);
}

static void pon_nl_gem_new_tcont_dir_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);

	KUNIT_EXPECT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_UNSET), -EINVAL);
	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_UPSTREAM, T_UNSET,
					     T_UNSET), -EINVAL);
	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_DOWNSTREAM,
					     T_TCONT_INDEX, T_UNSET), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);

	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_UPSTREAM,
					     T_TCONT_INDEX, T_UNSET), 0);
	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, T_GEM_ID + 1), 0);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 2);
}

static void t_gems_fill(struct pon_test_ctx *ctx)
{
	u16 i, max = ctx->pdev->caps->max_gems;
	int err;

	for (i = 0; i < max; i++) {
		err = t_gem_new_down(ctx, T_GEM_ID + i);
		KUNIT_ASSERT_EQ(ctx->test, err, 0);
	}
	KUNIT_ASSERT_EQ(ctx->test, ctx->gem_adds, max);
}

static void pon_nl_gem_new_same_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	t_gems_fill(ctx);

	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, ctx->pdev->caps->max_gems);
	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_GEM_GET, NULL, NULL),
			ctx->pdev->caps->max_gems);
}

static void pon_nl_gem_new_no_offload_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_conduit_register(test), 0);

	KUNIT_EXPECT_EQ(test, t_gem_new_no_offload(ctx, T_GEM_ID, 0), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 0);

	KUNIT_EXPECT_EQ(test, t_gem_new_no_offload(ctx, T_GEM_ID + 1, 1), 0);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flushes, 1);
	KUNIT_EXPECT_EQ(test, ctx->conduit_flush_gem, PON_GEM_ANY);
}

static void pon_nl_gem_new_other_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_gem_cfg cfg;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);

	KUNIT_EXPECT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), -EEXIST);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 1);
	cfg = t_gem_cfg(ctx, T_GEM_ID);
	KUNIT_EXPECT_EQ(test, cfg.dir, PON_GEM_DIR_DOWNSTREAM);
}

static void pon_nl_gem_new_full_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u16 max = ctx->pdev->caps->max_gems;

	t_gems_fill(ctx);

	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, T_GEM_ID + max), -ENOSPC);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, max);
	KUNIT_EXPECT_EQ(test, t_gem_get(ctx, T_GEM_ID + max), -ENOENT);
}

static void pon_nl_gem_new_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	ctx->gem_add_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 1);
	KUNIT_EXPECT_EQ(test, t_gem_get(ctx, T_GEM_ID), -ENOENT);
	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_GEM_GET, NULL, NULL), 0);
}

static void pon_nl_gem_new_id_range_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, 1020), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, 65535), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);

	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, 1021), 0);
	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, 65534), 0);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 2);
}

static void pon_nl_gem_new_dir_range_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID, 0, T_UNSET,
					     T_UNSET), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_BIDIR + 1, T_UNSET,
					     T_UNSET), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);
	KUNIT_EXPECT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
}

static void pon_nl_gem_del_unknown_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_gem_del(ctx, T_GEM_ID), -ENOENT);
	KUNIT_EXPECT_EQ(test, ctx->gem_dels, 0);
}

static void pon_nl_gem_del_netdev_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *dev;

	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	dev = pon_test_gem_link_new(test, T_GEM_ID);

	KUNIT_EXPECT_EQ(test, t_gem_del(ctx, T_GEM_ID), -EBUSY);
	KUNIT_EXPECT_EQ(test, ctx->gem_dels, 0);
	KUNIT_EXPECT_EQ(test, t_gem_get(ctx, T_GEM_ID), 0);

	pon_test_gem_link_del(dev);
	KUNIT_EXPECT_EQ(test, t_gem_del(ctx, T_GEM_ID), 0);
	KUNIT_EXPECT_EQ(test, ctx->gem_dels, 1);
	KUNIT_EXPECT_EQ(test, t_gem_get(ctx, T_GEM_ID), -ENOENT);
}

static void pon_nl_gem_del_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	ctx->gem_del_err = -EIO;

	KUNIT_EXPECT_EQ(test, t_gem_del(ctx, T_GEM_ID), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->gem_dels, 1);
	KUNIT_EXPECT_EQ(test, t_gem_get(ctx, T_GEM_ID), 0);
}

static void pon_nl_gem_map_no_del_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_ops_set(ctx, &pon_test_ops);
	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_UNSET), 0);

	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    T_UNSET, T_UNSET), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_map_count(ctx, NULL), 1);
}

static void pon_nl_gem_map_tag_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    PON_GEM_MAP_TAG_TAGGED, T_UNSET), 0);
	KUNIT_EXPECT_TRUE(test, ctx->map_last.tag_valid);
	KUNIT_EXPECT_TRUE(test, ctx->map_last.tagged);
	KUNIT_EXPECT_FALSE(test, ctx->map_last.vid_valid);

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    PON_GEM_MAP_TAG_UNTAGGED, T_UNSET), 0);
	KUNIT_EXPECT_TRUE(test, ctx->map_last.tag_valid);
	KUNIT_EXPECT_FALSE(test, ctx->map_last.tagged);

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	KUNIT_EXPECT_FALSE(test, ctx->map_last.tag_valid);
	KUNIT_EXPECT_TRUE(test, ctx->map_last.vid_valid);
	KUNIT_EXPECT_EQ(test, ctx->map_last.vid, T_VID);

	KUNIT_EXPECT_EQ(test, t_map_count(ctx, NULL), 3);
}

static void pon_nl_gem_map_same_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);
	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    PON_GEM_MAP_TAG_TAGGED, T_VID), 0);
	KUNIT_EXPECT_NOT_NULL(test,
			      pon_test_nl_ntf(ctx, PON_CMD_GEM_MAP_ADD_NTF));

	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    PON_GEM_MAP_TAG_TAGGED, T_VID), 0);
	KUNIT_EXPECT_EQ(test, ctx->map_sets, 2);
	KUNIT_EXPECT_EQ(test, t_map_count(ctx, NULL), 1);
	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_GEM_MAP_ADD_NTF));
}

static void pon_nl_gem_map_fail_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	ctx->map_set_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->map_sets, 1);
	KUNIT_EXPECT_EQ(test, t_map_count(ctx, NULL), 0);
}

static void pon_nl_gem_map_del_vid_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), 0);

	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    T_UNSET, T_OTHER_VID), -ENOENT);
	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    T_UNSET, T_UNSET), -ENOENT);
	KUNIT_EXPECT_EQ(test, ctx->map_dels, 0);
	KUNIT_EXPECT_EQ(test, t_map_count(ctx, NULL), 1);
}

static void pon_nl_gem_map_del_exact_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_OTHER_VID), 0);

	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	KUNIT_EXPECT_EQ(test, ctx->map_dels, 1);
	KUNIT_EXPECT_EQ(test, ctx->map_last.vid, T_VID);

	tb = t_map_first(ctx);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_GEM_MAP_VID]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_VID]), T_OTHER_VID);
}

static void pon_nl_gem_map_dump_tag_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    PON_GEM_MAP_TAG_UNTAGGED, T_UNSET), 0);
	tb = t_map_first(ctx);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_GEM_ID]), T_GEM_ID);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_GEM_MAP_TAG]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_TAG]),
			PON_GEM_MAP_TAG_UNTAGGED);
	KUNIT_EXPECT_NULL(test, tb[PON_A_GEM_MAP_VID]);

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    PON_GEM_MAP_TAG_UNTAGGED, T_UNSET), 0);
	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    PON_GEM_MAP_TAG_TAGGED, T_UNSET), 0);
	tb = t_map_first(ctx);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_GEM_MAP_TAG]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_TAG]),
			PON_GEM_MAP_TAG_TAGGED);
}

static int t_gem_stats_get(struct pon_test_ctx *ctx, struct nlattr **tb)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_GEM_STATS_GET);

	t_put_u32(ctx, skb, PON_A_GEM_STATS_GEM_ID, T_GEM_ID);

	return t_get(ctx, skb, tb, PON_A_GEM_STATS_MAX);
}

static void pon_nl_gem_stats_zeroed_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_GEM_STATS_MAX + 1];

	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	ctx->gem_rx_frames = T_RX_FRAMES;

	KUNIT_ASSERT_EQ(test, t_gem_stats_get(ctx, tb), 0);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_STATS_GEM_ID]),
			T_GEM_ID);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_GEM_STATS_RX_FRAMES]),
			T_RX_FRAMES);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_GEM_STATS_RX_BYTES]), 0);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_GEM_STATS_TX_FRAMES]), 0);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_GEM_STATS_TX_BYTES]), 0);
}

static void pon_nl_gem_stats_error_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_GEM_STATS_MAX + 1];

	KUNIT_EXPECT_EQ(test, t_gem_stats_get(ctx, tb), -ENOENT);

	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	ctx->gem_stats_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_gem_stats_get(ctx, tb), -EIO);
	KUNIT_EXPECT_EQ(test,
			t_dump(ctx, PON_CMD_GEM_STATS_GET, NULL, NULL), -EIO);
}

static void t_tc_core_set(struct pon_test_ctx *ctx, struct pon_tc_stats *want)
{
	struct pon_dev *pdev = ctx->pdev;

	mutex_lock(&pdev->lock);
	pdev->lods_events = T_TC_CORE_BASE + 1;
	pdev->lods_restored = T_TC_CORE_BASE + 2;
	pdev->lods_reactivations = T_TC_CORE_BASE + 3;
	pdev->omci_rx = T_TC_CORE_BASE + 4;
	atomic64_set(&pdev->omci_rx_dropped, T_TC_CORE_BASE + 5);
	atomic64_set(&pdev->omci_rx_errors, T_TC_CORE_BASE + 6);
	pdev->omci_tx = T_TC_CORE_BASE + 7;
	pdev->omci_tx_errors = T_TC_CORE_BASE + 8;
	mutex_unlock(&pdev->lock);

	want->lods_events = T_TC_CORE_BASE + 1;
	want->lods_restored = T_TC_CORE_BASE + 2;
	want->lods_reactivations = T_TC_CORE_BASE + 3;
	want->omci_rx = T_TC_CORE_BASE + 4;
	want->omci_rx_dropped = T_TC_CORE_BASE + 5;
	want->omci_rx_errors = T_TC_CORE_BASE + 6;
	want->omci_tx = T_TC_CORE_BASE + 7;
	want->omci_tx_errors = T_TC_CORE_BASE + 8;
}

static void t_tc_stats_expect(struct pon_test_ctx *ctx,
			      const struct pon_tc_stats *want)
{
	struct nlattr *tb[PON_A_TC_STATS_MAX + 1];
	const u64 *val = (const u64 *)want;
	unsigned int i;
	int attr;

	BUILD_BUG_ON(T_TC_COUNT !=
		     PON_A_TC_STATS_MAX - PON_A_TC_STATS_PSBD_HEC_ERRORS + 1);

	KUNIT_ASSERT_EQ(ctx->test,
			t_simple_get(ctx, PON_CMD_TC_STATS_GET, tb,
				     PON_A_TC_STATS_MAX), 0);
	KUNIT_EXPECT_EQ(ctx->test, nla_get_u32(tb[PON_A_TC_STATS_DEV_ID]),
			ctx->pdev->id);

	for (i = 0; i < T_TC_COUNT; i++) {
		attr = PON_A_TC_STATS_PSBD_HEC_ERRORS + i;
		if (val[i] == PON_STAT_NOT_SET) {
			KUNIT_EXPECT_NULL_MSG(ctx->test, tb[attr],
					      "attribute %d", attr);
			continue;
		}
		KUNIT_EXPECT_NOT_NULL_MSG(ctx->test, tb[attr],
					  "attribute %d", attr);
		if (tb[attr])
			KUNIT_EXPECT_EQ_MSG(ctx->test, nla_get_uint(tb[attr]),
					    val[i], "attribute %d", attr);
	}
}

static void pon_nl_tc_stats_core_only_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tc_stats want;

	pon_test_ops_set(ctx, &pon_test_ops);
	memset(&want, 0xff, sizeof(want));
	t_tc_core_set(ctx, &want);

	t_tc_stats_expect(ctx, &want);
}

static void pon_nl_tc_stats_map_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u64 *drv = (u64 *)&ctx->tc_stats;
	struct pon_tc_stats want;
	unsigned int i;

	for (i = 0; i < T_TC_COUNT; i++)
		drv[i] = T_TC_DRV_BASE + i;
	want = ctx->tc_stats;
	t_tc_core_set(ctx, &want);

	t_tc_stats_expect(ctx, &want);
}

static void pon_nl_tc_stats_not_set_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_tc_stats want;

	memset(&ctx->tc_stats, 0xff, sizeof(ctx->tc_stats));
	ctx->tc_stats.xgem_key_errors = T_TC_DRV_BASE;
	ctx->tc_stats.tx_sleep_request = 0;
	want = ctx->tc_stats;
	t_tc_core_set(ctx, &want);

	t_tc_stats_expect(ctx, &want);
}

static void pon_nl_tc_stats_error_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_TC_STATS_MAX + 1];

	ctx->tc_stats_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_simple_get(ctx, PON_CMD_TC_STATS_GET, tb,
					   PON_A_TC_STATS_MAX), -EIO);
}

static void pon_nl_fec_error_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_FEC_MAX + 1];

	ctx->fec_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_simple_get(ctx, PON_CMD_FEC_GET, tb,
					   PON_A_FEC_MAX), -EIO);
}

static void pon_nl_fec_totals_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_FEC_MAX + 1];

	ctx->fec.corrected_bytes = 1000;
	ctx->fec.corrected_codewords = 2000;
	ctx->fec.uncorrectable_codewords = 3000;
	ctx->fec.total_codewords = 4000;
	ctx->fec.seconds = 5000;
	KUNIT_ASSERT_EQ(test, t_simple_get(ctx, PON_CMD_FEC_GET, tb,
					   PON_A_FEC_MAX), 0);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_FEC_CORRECTED_BYTES]), 0);

	ctx->fec.corrected_bytes += 1;
	ctx->fec.corrected_codewords += 2;
	ctx->fec.uncorrectable_codewords += 3;
	ctx->fec.total_codewords += 4;
	ctx->fec.seconds += 5;
	KUNIT_ASSERT_EQ(test, t_simple_get(ctx, PON_CMD_FEC_GET, tb,
					   PON_A_FEC_MAX), 0);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_FEC_DEV_ID]), ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_FEC_CORRECTED_BYTES]), 1);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_FEC_CORRECTED]), 2);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_FEC_UNCORRECTABLE]), 3);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_FEC_TOTAL_CODEWORDS]), 4);
	KUNIT_EXPECT_EQ(test, nla_get_uint(tb[PON_A_FEC_SECONDS]), 5);
}

static void pon_nl_event_alloc_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_STATE), 0);

	pon_test_alloc_assign(ctx, T_TCONT_ALLOC_ID);
	tb = t_event_ntf(ctx, PON_EVENT_TYPE_TCONT_ALLOC);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_EVENT_ALLOC_ID]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_EVENT_ALLOC_ID]),
			T_TCONT_ALLOC_ID);
	KUNIT_EXPECT_NULL(test, tb[PON_A_EVENT_DEPTH]);

	pon_test_alloc_release(ctx, T_OTHER_ALLOC_ID);
	tb = t_event_ntf(ctx, PON_EVENT_TYPE_TCONT_DEALLOC);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_EVENT_ALLOC_ID]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_EVENT_ALLOC_ID]),
			T_OTHER_ALLOC_ID);
}

static void pon_nl_event_reboot_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_event ev = {
		.type = PON_EVENT_TYPE_REBOOT_REQ,
		.reboot.depth = T_REBOOT_DEPTH,
		.reboot.image = T_REBOOT_IMAGE,
		.reboot.calls = T_REBOOT_CALLS,
	};
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_STATE), 0);

	t_event(ctx, &ev);
	tb = t_event_ntf(ctx, PON_EVENT_TYPE_REBOOT_REQ);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_EVENT_DEPTH]);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_EVENT_IMAGE]);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_EVENT_CALLS]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_EVENT_DEPTH]),
			T_REBOOT_DEPTH);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_EVENT_IMAGE]),
			T_REBOOT_IMAGE);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_EVENT_CALLS]),
			T_REBOOT_CALLS);
	KUNIT_EXPECT_NULL(test, tb[PON_A_EVENT_ALLOC_ID]);
}

static void pon_nl_event_mib_reset_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_event ev = {
		.type = PON_EVENT_TYPE_MIB_RESET_REQ,
		.reboot.calls = T_REBOOT_CALLS,
	};
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_STATE), 0);

	t_event(ctx, &ev);
	tb = t_event_ntf(ctx, PON_EVENT_TYPE_MIB_RESET_REQ);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_EVENT_CALLS]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_EVENT_CALLS]),
			T_REBOOT_CALLS);
	KUNIT_EXPECT_NULL(test, tb[PON_A_EVENT_ALLOC_ID]);
	KUNIT_EXPECT_NULL(test, tb[PON_A_EVENT_DEPTH]);
	KUNIT_EXPECT_NULL(test, tb[PON_A_EVENT_IMAGE]);
}

static void pon_nl_alarm_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_STATE), 0);

	pon_dev_alarm_set(ctx->pdev, PON_ALARM_LODS, true);
	flush_workqueue(ctx->pdev->wq);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_ALARM_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_ALARM_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_ALARM_DEV_ID]),
			ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_ALARM_ALARMS]),
			BIT(PON_ALARM_LODS));

	pon_dev_alarm_set(ctx->pdev, PON_ALARM_LODS, false);
	flush_workqueue(ctx->pdev->wq);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_ALARM_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_ALARM_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_ALARM_ALARMS]), 0);
}

static void pon_nl_ploam_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_STATE), 0);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_PLOAM_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_DEV_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_ID]), ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_PLOAM_STATE]),
			PON_PLOAM_STATE_O1);

	pon_test_state_report(ctx, PON_PLOAM_STATE_O1);
	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_PLOAM_NTF));
}

static void pon_nl_omci_register_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->pdev->omci_portid),
			ctx->nl_portid);
	KUNIT_EXPECT_EQ(test, t_omci_register(ctx), 0);
}

static void pon_nl_omci_tx_not_owner_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, T_OMCI_BASELINE_LEN), -EPERM);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_ASSERT_EQ(test, pon_omci_register(ctx->pdev, T_OMCI_PORTID), 0);
	mutex_unlock(&ctx->pdev->lock);

	KUNIT_EXPECT_EQ(test, t_omci_register(ctx), -EBUSY);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, T_OMCI_BASELINE_LEN), -EPERM);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);
}

static void pon_nl_omci_tx_short_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, PON_OMCI_MIN_LEN - 1), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, PON_OMCI_MIN_LEN), 0);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len, PON_OMCI_MIN_LEN);
}

static void pon_nl_omci_tx_long_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, PON_OMCI_MAX_LEN + 1), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, PON_OMCI_MAX_LEN), 0);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len, PON_OMCI_MAX_LEN);
}

static void pon_nl_omci_tx_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, T_OMCI_BASELINE_LEN), 0);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 1);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len, T_OMCI_BASELINE_LEN);
	KUNIT_EXPECT_TRUE(test, ctx->omci_xmit_bh_off);

	ctx->omci_xmit_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, T_OMCI_BASELINE_LEN), -EIO);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, ctx->pdev->omci_tx, 1);
	KUNIT_EXPECT_EQ(test, ctx->pdev->omci_tx_errors, 1);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_nl_write_ack_only_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, t_dev_set_serial(ctx), 0);
	KUNIT_EXPECT_EQ(test, t_dev_set_enable(ctx, 1), 0);
	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);
	KUNIT_EXPECT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);
	KUNIT_EXPECT_EQ(test, t_gem_new(ctx, T_GEM_ID, T_TCONT_INDEX), 0);
	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	KUNIT_EXPECT_EQ(test, t_gem_del(ctx, T_GEM_ID), 0);
	KUNIT_EXPECT_EQ(test, t_tcont_del(ctx, T_TCONT_INDEX), 0);
	KUNIT_EXPECT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test, t_omci_tx(ctx, T_OMCI_BASELINE_LEN), 0);
}

static void pon_nl_omci_tx_baseline_mic_test(struct kunit *test)
{
	unsigned int len = T_OMCI_BASELINE_LEN + PON_OMCI_MIC_LEN;
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test,
			t_omci_tx_pdu(ctx, len, T_OMCI_DEVID_BASELINE, 0),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);
}

static void pon_nl_omci_tx_extended_short_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test,
			t_omci_tx_pdu(ctx, T_OMCI_EXT_HDR_LEN,
				      T_OMCI_DEVID_EXTENDED,
				      T_OMCI_EXT_CONTENTS),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			t_omci_tx_pdu(ctx,
				      T_OMCI_EXT_HDR_LEN +
				      T_OMCI_EXT_CONTENTS - 1,
				      T_OMCI_DEVID_EXTENDED,
				      T_OMCI_EXT_CONTENTS),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);

	KUNIT_EXPECT_EQ(test,
			t_omci_tx_pdu(ctx,
				      T_OMCI_EXT_HDR_LEN + T_OMCI_EXT_CONTENTS,
				      T_OMCI_DEVID_EXTENDED,
				      T_OMCI_EXT_CONTENTS),
			0);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len,
			T_OMCI_EXT_HDR_LEN + T_OMCI_EXT_CONTENTS);
}

static void pon_nl_omci_rx_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct sk_buff *skb;
	struct nlattr **tb;
	unsigned int i;
	u8 *pdu;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);

	pdu = kunit_kmalloc(test, T_OMCI_BASELINE_LEN, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pdu);
	for (i = 0; i < T_OMCI_BASELINE_LEN; i++)
		pdu[i] = i + 1;
	skb = alloc_skb(T_OMCI_BASELINE_LEN, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put_data(skb, pdu, T_OMCI_BASELINE_LEN);

	KUNIT_ASSERT_EQ(test, pon_omci_conduit_rx(ctx->pdev, skb, false), 0);
	flush_workqueue(ctx->pdev->wq);

	ntf = pon_test_nl_ntf(ctx, PON_CMD_OMCI_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_OMCI_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_OMCI_DEV_ID]),
			ctx->pdev->id);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_OMCI_PDU]);
	KUNIT_EXPECT_EQ(test, nla_len(tb[PON_A_OMCI_PDU]), T_OMCI_BASELINE_LEN);
	KUNIT_EXPECT_MEMEQ(test, nla_data(tb[PON_A_OMCI_PDU]), pdu,
			   T_OMCI_BASELINE_LEN);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, ctx->pdev->omci_rx, 1);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_nl_tcont_dump_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u16 i, max = ctx->pdev->caps->max_tconts;
	struct pon_test_nl_msg *msg;
	unsigned long seen = 0;
	struct nlattr **tb;
	LIST_HEAD(msgs);
	u32 index;

	for (i = 0; i < max; i++)
		KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, i, T_TCONT_ALLOC_ID + i),
				0);

	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_TCONT_GET, &msgs, NULL), max);
	list_for_each_entry(msg, &msgs, list) {
		tb = t_parse(ctx, msg->nlh, PON_A_TCONT_MAX);
		index = nla_get_u32(tb[PON_A_TCONT_INDEX]);
		KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_DEV_ID]),
				ctx->pdev->id);
		KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_TCONT_ALLOC_ID]),
				T_TCONT_ALLOC_ID + index);
		seen |= BIT(index);
	}
	KUNIT_EXPECT_EQ(test, seen, GENMASK(max - 1, 0));
}

static void pon_nl_dump_unknown_dev_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);

	skb = pon_test_nl_new(ctx, PON_CMD_TCONT_GET, 0);
	t_put_u32(ctx, skb, PON_A_TCONT_DEV_ID, T_UNKNOWN_DEV_ID);
	KUNIT_EXPECT_EQ(test, pon_test_nl_dump(ctx, skb, NULL, NULL), -ENODEV);

	skb = pon_test_nl_new(ctx, PON_CMD_GEM_MAP_GET, 0);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_DEV_ID, T_UNKNOWN_DEV_ID);
	KUNIT_EXPECT_EQ(test, pon_test_nl_dump(ctx, skb, NULL, NULL), -ENODEV);

	skb = pon_test_nl_new(ctx, PON_CMD_TCONT_GET, 0);
	t_put_u32(ctx, skb, PON_A_TCONT_DEV_ID, ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, pon_test_nl_dump(ctx, skb, NULL, NULL), 1);
}

static int t_gem_stats_gen(struct pon_dev *pdev, u16 gem_id,
			   struct pon_gem_stats *stats)
{
	pon_nl_obj_gen_inc();

	return 0;
}

static void t_dump_gems_add(struct kunit *test)
{
	unsigned int i;

	for (i = 0; i < T_DUMP_GEMS; i++)
		pon_test_gem_new(test, T_GEM_ID + i, T_TCONT_INDEX,
				 T_TCONT_ALLOC_ID);
}

static void pon_nl_dump_consistent_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	bool intr;

	t_dump_gems_add(test);

	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_GEM_STATS_GET, NULL, &intr),
			T_DUMP_GEMS);
	KUNIT_EXPECT_FALSE(test, intr);
}

static void pon_nl_dump_intr_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev_ops *ops = t_ops_copy(ctx, &pon_test_ops_full);
	bool intr;

	t_dump_gems_add(test);
	ops->gem_stats = t_gem_stats_gen;
	pon_test_ops_set(ctx, ops);

	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_GEM_STATS_GET, NULL, &intr),
			T_DUMP_GEMS);
	KUNIT_EXPECT_TRUE(test, intr);
}

static void t_gems_drop(struct pon_test_ctx *ctx)
{
	struct pon_gem *gem, *next;

	mutex_lock(&ctx->pdev->lock);
	list_for_each_entry_safe(gem, next, &ctx->pdev->gems, list) {
		list_del(&gem->list);
		kfree(gem);
	}
	pon_nl_obj_gen_inc();
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_nl_dump_intr_empty_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	bool intr;
	int count;

	t_dump_gems_add(test);

	count = pon_test_nl_dump_split(ctx,
				       pon_test_nl_new(ctx, PON_CMD_GEM_GET, 0),
				       NULL, &intr, t_gems_drop);
	KUNIT_EXPECT_GT(test, count, 0);
	KUNIT_EXPECT_LT(test, count, T_DUMP_GEMS);
	KUNIT_EXPECT_TRUE(test, intr);
}

static struct pon_dev *t_dev_second_new(struct pon_test_ctx *ctx,
					struct net_device **netdevp)
{
	struct net_device *netdev;
	struct pon_dev *pdev;

	netdev = pon_test_netdev_create();
	KUNIT_ASSERT_NOT_NULL(ctx->test, netdev);
	pdev = pon_dev_create(netdev, ctx->parent, &pon_test_ops_full,
			      &pon_test_caps, PON_MODE_XGS_PON, ctx);
	if (IS_ERR(pdev))
		pon_test_netdev_destroy(netdev);
	KUNIT_ASSERT_FALSE(ctx->test, IS_ERR(pdev));
	*netdevp = netdev;

	return pdev;
}

static void t_dev_second_del(struct pon_dev *pdev, struct net_device *netdev)
{
	pon_dev_unregister(pdev);
	pon_test_netdev_destroy(netdev);
	pon_dev_put(pdev);
}

static u32 t_ntf_dev_id(struct pon_test_ctx *ctx, u8 cmd)
{
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	ntf = pon_test_nl_ntf(ctx, cmd);
	KUNIT_ASSERT_NOT_NULL(ctx->test, ntf);
	tb = t_parse(ctx, ntf, PON_A_DEV_MAX);
	KUNIT_ASSERT_NOT_NULL(ctx->test, tb[PON_A_DEV_ID]);

	return nla_get_u32(tb[PON_A_DEV_ID]);
}

static void pon_nl_alarm_get_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_ALARM_MAX + 1];
	struct pon_test_nl_msg *msg;
	struct nlattr **dumped;
	LIST_HEAD(msgs);

	KUNIT_ASSERT_EQ(test, t_simple_get(ctx, PON_CMD_ALARM_GET, tb,
					   PON_A_ALARM_MAX), 0);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_ALARM_DEV_ID]),
			ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_ALARM_ALARMS]), 0);

	pon_dev_alarm_set(ctx->pdev, PON_ALARM_LODS, true);
	flush_workqueue(ctx->pdev->wq);
	KUNIT_ASSERT_EQ(test, t_simple_get(ctx, PON_CMD_ALARM_GET, tb,
					   PON_A_ALARM_MAX), 0);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_ALARM_ALARMS]),
			BIT(PON_ALARM_LODS));

	KUNIT_ASSERT_EQ(test, t_dump(ctx, PON_CMD_ALARM_GET, &msgs, NULL), 1);
	msg = list_first_entry(&msgs, struct pon_test_nl_msg, list);
	dumped = t_parse(ctx, msg->nlh, PON_A_ALARM_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(dumped[PON_A_ALARM_DEV_ID]),
			ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, nla_get_u32(dumped[PON_A_ALARM_ALARMS]),
			BIT(PON_ALARM_LODS));
}

static void pon_nl_dev_get_dump_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *netdev;
	struct pon_test_nl_msg *msg;
	unsigned int seen = 0;
	struct pon_dev *other;
	struct nlattr **tb;
	LIST_HEAD(msgs);
	u32 id;

	other = t_dev_second_new(ctx, &netdev);

	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_DEV_GET, &msgs, NULL), 2);
	list_for_each_entry(msg, &msgs, list) {
		tb = t_parse(ctx, msg->nlh, PON_A_DEV_MAX);
		id = nla_get_u32(tb[PON_A_DEV_ID]);
		if (id == ctx->pdev->id)
			seen |= BIT(0);
		else if (id == other->id)
			seen |= BIT(1);
	}
	KUNIT_EXPECT_EQ(test, seen, 3);

	t_dev_second_del(other, netdev);
	KUNIT_EXPECT_EQ(test, t_dump(ctx, PON_CMD_DEV_GET, NULL, NULL), 1);
}

static void pon_nl_dev_add_del_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net_device *netdev;
	struct pon_dev *other;
	u32 id;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);

	other = t_dev_second_new(ctx, &netdev);
	id = other->id;
	KUNIT_EXPECT_EQ(test, t_ntf_dev_id(ctx, PON_CMD_DEV_ADD_NTF), id);

	t_dev_second_del(other, netdev);
	KUNIT_EXPECT_EQ(test, t_ntf_dev_id(ctx, PON_CMD_DEV_DEL_NTF), id);
}

static void pon_nl_dev_change_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct sk_buff *skb;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);

	KUNIT_ASSERT_EQ(test, t_dev_set_serial(ctx), 0);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_DEV_CHANGE_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_DEV_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_DEV_ID]), ctx->pdev->id);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_DEV_SERIAL]);
	KUNIT_EXPECT_MEMEQ(test, nla_data(tb[PON_A_DEV_SERIAL]), T_SERIAL,
			   PON_SERIAL_LEN);

	ctx->enable_err = -EIO;
	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_str(ctx, skb, PON_A_DEV_SERIAL, T_SERIAL);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 1);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), -EIO);
	KUNIT_EXPECT_NOT_NULL(test,
			      pon_test_nl_ntf(ctx, PON_CMD_DEV_CHANGE_NTF));

	KUNIT_ASSERT_EQ(test, t_dev_set_enable(ctx, 1), -EIO);
	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_DEV_CHANGE_NTF));
}

static void pon_nl_gem_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);

	KUNIT_ASSERT_EQ(test, t_gem_new_down(ctx, T_GEM_ID), 0);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_GEM_ADD_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_GEM_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_ID]), T_GEM_ID);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_DIR]),
			PON_GEM_DIR_DOWNSTREAM);

	KUNIT_ASSERT_EQ(test, t_gem_del(ctx, T_GEM_ID), 0);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_GEM_DEL_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_GEM_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_ID]), T_GEM_ID);
}

static void pon_nl_gem_map_del_ntf_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	struct nlattr **tb;

	KUNIT_ASSERT_EQ(test, pon_test_nl_join(ctx, PON_NLGRP_MGMT), 0);
	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	pon_test_nl_ntf_flush(ctx);

	KUNIT_ASSERT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_DEL, T_GEM_ID,
				    T_UNSET, T_VID), 0);
	ntf = pon_test_nl_ntf(ctx, PON_CMD_GEM_MAP_DEL_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	tb = t_parse(ctx, ntf, PON_A_GEM_MAP_MAX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_GEM_ID]), T_GEM_ID);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_GEM_MAP_VID]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_VID]), T_VID);
}

static void pon_nl_other_netns_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *attrs[PON_A_DEV_MAX + 1] = {};
	struct genl_info info = {};
	struct net *other;
	int err;

	if (!IS_ENABLED(CONFIG_NET_NS))
		kunit_skip(test, "needs CONFIG_NET_NS");

	other = kunit_kzalloc(test, sizeof(*other), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other);
	attrs[PON_A_DEV_ID] = pon_test_nla_u32(test, PON_A_DEV_ID,
					       ctx->pdev->id);
	info.attrs = attrs;

	genl_info_net_set(&info, other);
	err = pon_device_get_locked(NULL, NULL, &info);
	KUNIT_EXPECT_EQ(test, err, -ENODEV);
	if (!err)
		pon_device_unlock(NULL, NULL, &info);

	genl_info_net_set(&info, dev_net(ctx->netdev));
	KUNIT_ASSERT_EQ(test, pon_device_get_locked(NULL, NULL, &info), 0);
	KUNIT_EXPECT_PTR_EQ(test, info.user_ptr[0], (void *)ctx->pdev);
	pon_device_unlock(NULL, NULL, &info);
}

static void pon_nl_gem_get_attrs_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_GEM_MAX + 1];
	struct sk_buff *skb;

	KUNIT_ASSERT_EQ(test, t_tcont_set(ctx, T_TCONT_INDEX, T_TCONT_ALLOC_ID),
			0);

	skb = t_msg(ctx, PON_CMD_GEM_NEW);
	t_put_u32(ctx, skb, PON_A_GEM_ID, T_GEM_ID);
	t_put_u32(ctx, skb, PON_A_GEM_DIR, PON_GEM_DIR_BIDIR);
	t_put_u32(ctx, skb, PON_A_GEM_TCONT_INDEX, T_TCONT_INDEX);
	t_put_u32(ctx, skb, PON_A_GEM_KEY_RING, PON_GEM_KEY_RING_UNICAST);
	t_put_u32(ctx, skb, PON_A_GEM_QUEUE, T_GEM_QUEUE);
	t_put_u8(ctx, skb, PON_A_GEM_NO_OFFLOAD, 1);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_EQ(test, t_gem_cfg(ctx, T_GEM_ID).queue, T_GEM_QUEUE);

	skb = t_msg(ctx, PON_CMD_GEM_GET);
	t_put_u32(ctx, skb, PON_A_GEM_ID, T_GEM_ID);
	KUNIT_ASSERT_EQ(test, t_get(ctx, skb, tb, PON_A_GEM_MAX), 0);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_DIR]),
			PON_GEM_DIR_BIDIR);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_TCONT_INDEX]),
			T_TCONT_INDEX);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_KEY_RING]),
			PON_GEM_KEY_RING_UNICAST);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_QUEUE]), T_GEM_QUEUE);
	KUNIT_EXPECT_EQ(test, nla_get_u8(tb[PON_A_GEM_NO_OFFLOAD]), 1);
}

static void pon_nl_gem_map_pbit_dscp_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr **tb;
	struct sk_buff *skb;

	skb = t_msg(ctx, PON_CMD_GEM_MAP_NEW);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_GEM_ID, T_GEM_ID);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_PBIT, T_PBIT);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_DSCP, T_DSCP);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_TRUE(test, ctx->map_last.pbit_valid);
	KUNIT_EXPECT_EQ(test, ctx->map_last.pbit, T_PBIT);
	KUNIT_EXPECT_TRUE(test, ctx->map_last.dscp_valid);
	KUNIT_EXPECT_EQ(test, ctx->map_last.dscp, T_DSCP);

	tb = t_map_first(ctx);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_GEM_MAP_PBIT]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_PBIT]), T_PBIT);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_GEM_MAP_DSCP]);
	KUNIT_EXPECT_EQ(test, nla_get_u32(tb[PON_A_GEM_MAP_DSCP]), T_DSCP);
	KUNIT_EXPECT_NULL(test, tb[PON_A_GEM_MAP_VID]);
}

static void pon_nl_dev_set_disabled_enable_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);

	KUNIT_ASSERT_EQ(test, nla_put_flag(skb, PON_A_DEV_DISABLED), 0);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 1);
	KUNIT_ASSERT_EQ(test, t_request(ctx, skb), 0);
	KUNIT_EXPECT_EQ(test, ctx->identities, 1);
	KUNIT_EXPECT_TRUE(test, ctx->identity.disabled);
	KUNIT_EXPECT_EQ(test, ctx->enables, 1);
	KUNIT_EXPECT_TRUE(test, ctx->enable_on);
	KUNIT_EXPECT_TRUE(test, ctx->pdev->enabled);
}

static void pon_nl_gem_map_no_set_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev_ops *ops = t_ops_copy(ctx, &pon_test_ops_full);

	ops->gem_map_set = NULL;
	pon_test_ops_set(ctx, ops);

	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, T_VID), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->map_sets, 0);
	KUNIT_EXPECT_EQ(test, t_map_count(ctx, NULL), 0);
}

static void pon_nl_omci_tx_no_pdu_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, t_omci_register(ctx), 0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, t_msg(ctx, PON_CMD_OMCI_TX)),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);
}

static int t_msk_set(struct pon_test_ctx *ctx, u8 fill)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_MSK_SET);
	u8 msk[PON_KEY_LEN];

	memset(msk, fill, sizeof(msk));
	KUNIT_ASSERT_EQ(ctx->test,
			nla_put(skb, PON_A_DEV_MSK, sizeof(msk), msk), 0);

	return t_request(ctx, skb);
}

static int t_bcast_key_set(struct pon_test_ctx *ctx, u32 index, int fill)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_BCAST_KEY_SET);
	u8 key[PON_KEY_LEN];

	t_put_opt(ctx, skb, PON_A_DEV_BCAST_KEY_INDEX, index);
	if (fill >= 0) {
		memset(key, fill, sizeof(key));
		KUNIT_ASSERT_EQ(ctx->test, nla_put(skb, PON_A_DEV_BCAST_KEY,
						   sizeof(key), key), 0);
	}

	return t_request(ctx, skb);
}

static void pon_nl_msk_set_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct nlattr *tb[PON_A_DEV_MAX + 1];
	u8 expect[PON_KEY_LEN];

	KUNIT_EXPECT_EQ(test, t_request(ctx, t_msg(ctx, PON_CMD_MSK_SET)),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, t_msk_set(ctx, 0x5a), -ENETDOWN);
	KUNIT_EXPECT_EQ(test, ctx->msk_sets, 0);

	pon_test_activate(ctx);
	KUNIT_ASSERT_EQ(test, pon_test_state(ctx), PON_PLOAM_STATE_O5);
	KUNIT_EXPECT_EQ(test, t_msk_set(ctx, 0x5a), 0);
	KUNIT_EXPECT_EQ(test, ctx->msk_sets, 1);
	memset(expect, 0x5a, sizeof(expect));
	KUNIT_EXPECT_MEMEQ(test, ctx->msk, expect, PON_KEY_LEN);

	ctx->msk_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_msk_set(ctx, 0x5b), -EIO);

	t_dev_get(ctx, tb);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_MSK]);
	KUNIT_EXPECT_NULL(test, tb[PON_A_DEV_BCAST_KEY]);
	KUNIT_EXPECT_NULL(test, pon_test_nl_ntf(ctx, PON_CMD_DEV_CHANGE_NTF));
}

static void pon_nl_msk_set_no_op_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_ops_set(ctx, &pon_test_ops);
	pon_test_activate(ctx);
	KUNIT_EXPECT_EQ(test, t_msk_set(ctx, 0x5a), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 1, 0x11), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 1, -1), -EOPNOTSUPP);
}

static void pon_nl_bcast_key_set_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	u8 expect[PON_KEY_LEN];

	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, T_UNSET, 0x11), -EINVAL);
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 0, 0x11), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 3, 0x11), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 1, 0x11), -ENETDOWN);
	KUNIT_EXPECT_EQ(test, ctx->bcast_sets, 0);

	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 2, -1), 0);
	KUNIT_EXPECT_EQ(test, ctx->bcast_sets, 1);
	KUNIT_EXPECT_EQ(test, ctx->bcast_index, 2);
	KUNIT_EXPECT_FALSE(test, ctx->bcast_has_key);

	pon_test_activate(ctx);
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 1, 0x11), 0);
	KUNIT_EXPECT_EQ(test, ctx->bcast_sets, 2);
	KUNIT_EXPECT_EQ(test, ctx->bcast_index, 1);
	KUNIT_EXPECT_TRUE(test, ctx->bcast_has_key);
	memset(expect, 0x11, sizeof(expect));
	KUNIT_EXPECT_MEMEQ(test, ctx->bcast_key, expect, PON_KEY_LEN);

	ctx->bcast_err = -EIO;
	KUNIT_EXPECT_EQ(test, t_bcast_key_set(ctx, 2, 0x22), -EIO);
}

static void pon_nl_policy_key_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;
	u8 key[PON_KEY_LEN + 1] = {};

	pon_test_activate(ctx);
	skb = t_msg(ctx, PON_CMD_MSK_SET);
	KUNIT_ASSERT_EQ(test, nla_put(skb, PON_A_DEV_MSK, PON_KEY_LEN - 1, key),
			0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);
	skb = t_msg(ctx, PON_CMD_BCAST_KEY_SET);
	t_put_u32(ctx, skb, PON_A_DEV_BCAST_KEY_INDEX, 1);
	KUNIT_ASSERT_EQ(test, nla_put(skb, PON_A_DEV_BCAST_KEY, PON_KEY_LEN + 1,
				      key), 0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->msk_sets, 0);
	KUNIT_EXPECT_EQ(test, ctx->bcast_sets, 0);
}

static int t_bound(struct pon_test_ctx *ctx, u8 cmd, int type, u32 value)
{
	struct sk_buff *skb = t_msg(ctx, cmd);

	t_put_u32(ctx, skb, type, value);

	return t_request(ctx, skb);
}

static int t_bound_bin(struct pon_test_ctx *ctx, int type, unsigned int len)
{
	struct sk_buff *skb = t_msg(ctx, PON_CMD_DEV_SET);
	u8 *data;

	data = kunit_kzalloc(ctx->test, len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(ctx->test, data);
	KUNIT_ASSERT_EQ(ctx->test, nla_put(skb, type, len, data), 0);

	return t_request(ctx, skb);
}

static void pon_nl_policy_dev_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u8(ctx, skb, PON_A_DEV_ENABLE, 2);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);

	skb = t_msg(ctx, PON_CMD_DEV_SET);
	t_put_u8(ctx, skb, PON_A_DEV_DISABLED, 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);

	KUNIT_EXPECT_EQ(test, t_bound_bin(ctx, PON_A_DEV_SERIAL,
					  PON_SERIAL_LEN - 1), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_bound_bin(ctx, PON_A_DEV_SERIAL,
					  PON_SERIAL_LEN + 1), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_bound_bin(ctx, PON_A_DEV_REGISTRATION_ID,
					  PON_REG_ID_LEN + 1), -ERANGE);
	KUNIT_EXPECT_EQ(test, t_bound(ctx, PON_CMD_DEV_SET, PON_A_DEV_MODE,
				      PON_MODE_XGS_PON + 1), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->identities, 0);
	KUNIT_EXPECT_EQ(test, ctx->enables, 0);

	skb = pon_test_nl_new(ctx, PON_CMD_DEV_GET, 0);
	t_put_u32(ctx, skb, PON_A_DEV_ID, 0);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);
}

static void pon_nl_policy_obj_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	KUNIT_EXPECT_EQ(test, t_tcont_set(ctx, 65535, T_TCONT_ALLOC_ID),
			-ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->tcont_sets, 0);

	KUNIT_EXPECT_EQ(test, t_gem_new_full(ctx, T_GEM_ID,
					     PON_GEM_DIR_DOWNSTREAM, T_UNSET,
					     T_KEY_RING_MAX + 1), -ERANGE);
	skb = t_msg(ctx, PON_CMD_GEM_NEW);
	t_put_u32(ctx, skb, PON_A_GEM_ID, T_GEM_ID);
	t_put_u32(ctx, skb, PON_A_GEM_DIR, PON_GEM_DIR_DOWNSTREAM);
	t_put_u32(ctx, skb, PON_A_GEM_QUEUE, T_GEM_QUEUE_MAX + 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->gem_adds, 0);

	KUNIT_EXPECT_EQ(test, t_map(ctx, PON_CMD_GEM_MAP_NEW, T_GEM_ID,
				    T_UNSET, VLAN_N_VID - 1), -ERANGE);
	skb = t_msg(ctx, PON_CMD_GEM_MAP_NEW);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_GEM_ID, T_GEM_ID);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_PBIT, T_PBIT_MAX + 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);
	skb = t_msg(ctx, PON_CMD_GEM_MAP_NEW);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_GEM_ID, T_GEM_ID);
	t_put_u32(ctx, skb, PON_A_GEM_MAP_DSCP, T_DSCP_MAX + 1);
	KUNIT_EXPECT_EQ(test, t_request(ctx, skb), -ERANGE);
	KUNIT_EXPECT_EQ(test, ctx->map_sets, 0);
}

static void pon_nl_dump_filter_resume_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;
	bool intr;

	t_dump_gems_add(test);

	skb = pon_test_nl_new(ctx, PON_CMD_GEM_GET, 0);
	t_put_u32(ctx, skb, PON_A_GEM_DEV_ID, ctx->pdev->id);
	KUNIT_EXPECT_EQ(test, pon_test_nl_dump(ctx, skb, NULL, &intr),
			T_DUMP_GEMS);
	KUNIT_EXPECT_FALSE(test, intr);
}

static struct kunit_case pon_nl_test_cases[] = {
	KUNIT_CASE(pon_nl_dev_get_test),
	KUNIT_CASE(pon_nl_tcont_set_ntf_test),
	KUNIT_CASE(pon_nl_dev_id_missing_test),
	KUNIT_CASE(pon_nl_dev_id_unknown_test),
	KUNIT_CASE(pon_nl_dev_id_unregistered_test),
	KUNIT_CASE(pon_nl_dev_set_mode_caps_test),
	KUNIT_CASE(pon_nl_dev_set_mode_log_test),
	KUNIT_CASE(pon_nl_dev_set_disabled_busy_test),
	KUNIT_CASE(pon_nl_dev_set_empty_test),
	KUNIT_CASE(pon_nl_dev_set_no_identity_op_test),
	KUNIT_CASE(pon_nl_dev_set_no_enable_op_test),
	KUNIT_CASE(pon_nl_dev_set_identity_fail_test),
	KUNIT_CASE(pon_nl_dev_set_commit_members_test),
	KUNIT_CASE(pon_nl_dev_set_identity_busy_test),
	KUNIT_CASE(pon_nl_dev_set_serial_zero_test),
	KUNIT_CASE(pon_nl_dev_set_reg_id_pad_test),
	KUNIT_CASE(pon_nl_dev_set_reg_id_empty_test),
	KUNIT_CASE(pon_nl_dev_set_disabled_not_stored_test),
	KUNIT_CASE(pon_nl_dev_set_enable_fail_test),
	KUNIT_CASE(pon_nl_dev_set_enable_test),
	KUNIT_CASE(pon_nl_dev_get_disabled_test),
	KUNIT_CASE(pon_nl_dev_get_response_time_test),
	KUNIT_CASE(pon_nl_dev_get_serial_test),
	KUNIT_CASE(pon_nl_tcont_set_index_range_test),
	KUNIT_CASE(pon_nl_tcont_set_alloc_range_test),
	KUNIT_CASE(pon_nl_tcont_set_sn_grant_test),
	KUNIT_CASE(pon_nl_tcont_set_alloc_taken_test),
	KUNIT_CASE(pon_nl_tcont_set_fail_test),
	KUNIT_CASE(pon_nl_tcont_set_same_test),
	KUNIT_CASE(pon_nl_tcont_set_rebind_test),
	KUNIT_CASE(pon_nl_tcont_set_rebind_restore_test),
	KUNIT_CASE(pon_nl_tcont_set_ets_test),
	KUNIT_CASE(pon_nl_tcont_set_change_ntf_test),
	KUNIT_CASE(pon_nl_tcont_del_unknown_test),
	KUNIT_CASE(pon_nl_tcont_del_busy_test),
	KUNIT_CASE(pon_nl_tcont_del_fail_test),
	KUNIT_CASE(pon_nl_tcont_del_test),
	KUNIT_CASE(pon_nl_gem_new_missing_test),
	KUNIT_CASE(pon_nl_gem_new_broadcast_test),
	KUNIT_CASE(pon_nl_gem_new_broadcast_served_test),
	KUNIT_CASE(pon_nl_gem_new_unknown_tcont_test),
	KUNIT_CASE(pon_nl_gem_new_alloc_id_test),
	KUNIT_CASE(pon_nl_gem_new_untied_test),
	KUNIT_CASE(pon_nl_gem_new_tcont_dir_test),
	KUNIT_CASE(pon_nl_gem_new_same_test),
	KUNIT_CASE(pon_nl_gem_new_no_offload_test),
	KUNIT_CASE(pon_nl_gem_new_other_test),
	KUNIT_CASE(pon_nl_gem_new_full_test),
	KUNIT_CASE(pon_nl_gem_new_fail_test),
	KUNIT_CASE(pon_nl_gem_new_id_range_test),
	KUNIT_CASE(pon_nl_gem_new_dir_range_test),
	KUNIT_CASE(pon_nl_gem_del_unknown_test),
	KUNIT_CASE(pon_nl_gem_del_netdev_test),
	KUNIT_CASE(pon_nl_gem_del_fail_test),
	KUNIT_CASE(pon_nl_gem_map_no_del_op_test),
	KUNIT_CASE(pon_nl_gem_map_tag_test),
	KUNIT_CASE(pon_nl_gem_map_same_test),
	KUNIT_CASE(pon_nl_gem_map_fail_test),
	KUNIT_CASE(pon_nl_gem_map_del_vid_test),
	KUNIT_CASE(pon_nl_gem_map_del_exact_test),
	KUNIT_CASE(pon_nl_gem_map_dump_tag_test),
	KUNIT_CASE(pon_nl_gem_stats_zeroed_test),
	KUNIT_CASE(pon_nl_gem_stats_error_test),
	KUNIT_CASE(pon_nl_tc_stats_core_only_test),
	KUNIT_CASE(pon_nl_tc_stats_map_test),
	KUNIT_CASE(pon_nl_tc_stats_not_set_test),
	KUNIT_CASE(pon_nl_tc_stats_error_test),
	KUNIT_CASE(pon_nl_fec_error_test),
	KUNIT_CASE(pon_nl_fec_totals_test),
	KUNIT_CASE(pon_nl_event_alloc_test),
	KUNIT_CASE(pon_nl_event_reboot_test),
	KUNIT_CASE(pon_nl_event_mib_reset_test),
	KUNIT_CASE(pon_nl_alarm_ntf_test),
	KUNIT_CASE(pon_nl_ploam_ntf_test),
	KUNIT_CASE(pon_nl_omci_register_test),
	KUNIT_CASE(pon_nl_omci_tx_not_owner_test),
	KUNIT_CASE(pon_nl_omci_tx_short_test),
	KUNIT_CASE(pon_nl_omci_tx_long_test),
	KUNIT_CASE(pon_nl_omci_tx_test),
	KUNIT_CASE(pon_nl_write_ack_only_test),
	KUNIT_CASE(pon_nl_omci_tx_baseline_mic_test),
	KUNIT_CASE(pon_nl_omci_tx_extended_short_test),
	KUNIT_CASE(pon_nl_omci_rx_ntf_test),
	KUNIT_CASE(pon_nl_tcont_dump_test),
	KUNIT_CASE(pon_nl_dump_unknown_dev_test),
	KUNIT_CASE(pon_nl_dump_consistent_test),
	KUNIT_CASE(pon_nl_dump_intr_test),
	KUNIT_CASE(pon_nl_dump_intr_empty_test),
	KUNIT_CASE(pon_nl_alarm_get_test),
	KUNIT_CASE(pon_nl_dev_get_dump_test),
	KUNIT_CASE(pon_nl_dev_add_del_ntf_test),
	KUNIT_CASE(pon_nl_dev_change_ntf_test),
	KUNIT_CASE(pon_nl_gem_ntf_test),
	KUNIT_CASE(pon_nl_gem_map_del_ntf_test),
	KUNIT_CASE(pon_nl_other_netns_test),
	KUNIT_CASE(pon_nl_gem_get_attrs_test),
	KUNIT_CASE(pon_nl_gem_map_pbit_dscp_test),
	KUNIT_CASE(pon_nl_dev_set_disabled_enable_test),
	KUNIT_CASE(pon_nl_gem_map_no_set_op_test),
	KUNIT_CASE(pon_nl_omci_tx_no_pdu_test),
	KUNIT_CASE(pon_nl_policy_dev_test),
	KUNIT_CASE(pon_nl_policy_obj_test),
	KUNIT_CASE(pon_nl_dump_filter_resume_test),
	KUNIT_CASE(pon_nl_msk_set_test),
	KUNIT_CASE(pon_nl_msk_set_no_op_test),
	KUNIT_CASE(pon_nl_bcast_key_set_test),
	KUNIT_CASE(pon_nl_policy_key_test),
	{}
};

static struct kunit_suite pon_nl_test_suite = {
	.name = "pon_nl",
	.init = pon_nl_test_init,
	.exit = pon_test_exit,
	.test_cases = pon_nl_test_cases,
};

kunit_test_suite(pon_nl_test_suite);
