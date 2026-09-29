// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#include <kunit/test.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include <net/netlink.h>
#include <net/pon.h>

#include "pon_test.h"

#define T_OMCI_DEVID_OFF	3
#define T_OMCI_DEVID_BASELINE	0x0a
#define T_OMCI_DEVID_EXTENDED	0x0b
#define T_OMCI_DEVID_RESERVED	0x0c
#define T_OMCI_CONTENTS_LEN_OFF	8

static struct sk_buff *t_omci_skb(struct kunit *test, unsigned int len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put_zero(skb, len);

	return skb;
}

static void t_omci_rx(struct kunit *test, unsigned int len, bool unverified)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ(test,
			pon_omci_conduit_rx(ctx->pdev, t_omci_skb(test, len),
					    unverified),
			0);
}

static void t_omci_counts(struct kunit *test, u64 rx, u64 dropped, u64 errors)
{
	struct pon_test_ctx *ctx = test->priv;

	flush_workqueue(ctx->pdev->wq);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, ctx->pdev->omci_rx, rx);
	mutex_unlock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_dropped),
			dropped);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_errors),
			errors);
}

static void t_omci_owner_set(struct pon_test_ctx *ctx, u32 portid)
{
	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(ctx->test, pon_omci_register(ctx->pdev, portid), 0);
	mutex_unlock(&ctx->pdev->lock);
}

static void pon_omci_rx_no_owner_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	mutex_lock(&ctx->pdev->lock);
	t_omci_rx(test, T_OMCI_BASELINE_LEN, false);
	KUNIT_EXPECT_EQ(test, skb_queue_len(&ctx->pdev->omci_rxq), 0);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_dropped), 1);
	mutex_unlock(&ctx->pdev->lock);

	t_omci_counts(test, 0, 1, 0);
}

static void pon_omci_rx_len_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_ops_set(ctx, &pon_test_ops);
	t_omci_owner_set(ctx, T_OMCI_PORTID);

	mutex_lock(&ctx->pdev->lock);
	t_omci_rx(test, PON_OMCI_MIN_LEN - 1, false);
	t_omci_rx(test, PON_OMCI_MAX_LEN + 1, false);
	t_omci_rx(test, PON_OMCI_MIN_LEN + PON_OMCI_MIC_LEN - 1, true);
	t_omci_rx(test, PON_OMCI_MAX_LEN + PON_OMCI_MIC_LEN + 1, true);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_dropped), 4);
	t_omci_rx(test, PON_OMCI_MAX_LEN + PON_OMCI_MIC_LEN, true);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_dropped), 4);
	KUNIT_EXPECT_EQ(test, skb_queue_len(&ctx->pdev->omci_rxq), 0);
	mutex_unlock(&ctx->pdev->lock);

	t_omci_counts(test, 0, 4, 1);
}

static void pon_omci_rx_unchecked_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	pon_test_ops_set(ctx, &pon_test_ops);
	t_omci_owner_set(ctx, T_OMCI_PORTID);
	t_omci_rx(test, T_OMCI_BASELINE_LEN, true);
	t_omci_counts(test, 0, 0, 1);
}

static void pon_omci_rx_queue_limit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	unsigned int i;

	t_omci_owner_set(ctx, T_OMCI_PORTID);

	mutex_lock(&ctx->pdev->lock);
	for (i = 0; i <= PON_OMCI_QUEUE_MAX; i++)
		t_omci_rx(test, T_OMCI_BASELINE_LEN, false);
	KUNIT_EXPECT_EQ(test, atomic64_read(&ctx->pdev->omci_rx_dropped), 1);
	mutex_unlock(&ctx->pdev->lock);

	t_omci_counts(test, 0, PON_OMCI_QUEUE_MAX + 1, 0);
}

static void t_omci_release(struct net *net, u32 portid, int protocol,
			   unsigned long state)
{
	struct netlink_notify notify = {
		.net = net,
		.portid = portid,
		.protocol = protocol,
	};

	pon_omci_netlink_notify(NULL, state, &notify);
}

static void pon_omci_release_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct net *net = dev_net(ctx->netdev);
	struct net *other;

	other = kunit_kzalloc(test, sizeof(*other), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, pon_omci_register(ctx->pdev, T_OMCI_PORTID), 0);
	KUNIT_EXPECT_EQ(test, pon_omci_register(ctx->pdev, T_OMCI_PORTID), 0);
	KUNIT_EXPECT_EQ(test, pon_omci_register(ctx->pdev, T_OMCI_PORTID + 1),
			-EBUSY);
	mutex_unlock(&ctx->pdev->lock);

	t_omci_release(net, T_OMCI_PORTID + 1, NETLINK_GENERIC,
		       NETLINK_URELEASE);
	t_omci_release(net, T_OMCI_PORTID, NETLINK_ROUTE,
		       NETLINK_URELEASE);
	t_omci_release(net, T_OMCI_PORTID, NETLINK_GENERIC, 0);
	if (IS_ENABLED(CONFIG_NET_NS))
		t_omci_release(other, T_OMCI_PORTID, NETLINK_GENERIC,
			       NETLINK_URELEASE);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->pdev->omci_portid), T_OMCI_PORTID);

	t_omci_release(net, T_OMCI_PORTID, NETLINK_GENERIC,
		       NETLINK_URELEASE);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ctx->pdev->omci_portid), 0);

	mutex_lock(&ctx->pdev->lock);
	KUNIT_EXPECT_EQ(test, pon_omci_register(ctx->pdev, T_OMCI_PORTID + 1),
			0);
	mutex_unlock(&ctx->pdev->lock);
}

static void t_omci_rx_unchecked(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct sk_buff *skb;

	skb = pon_test_frame(test, T_OMCI_BASELINE_LEN + PON_OMCI_MIC_LEN);
	KUNIT_EXPECT_EQ(test, pon_omci_conduit_rx(ctx->pdev, skb, true), 0);
}

static void pon_omci_rx_verify_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	t_omci_owner_set(ctx, T_OMCI_PORTID);
	t_omci_rx_unchecked(test);
	t_omci_counts(test, 0, 1, 0);
	KUNIT_EXPECT_EQ(test, ctx->omci_verifies, 1);
}

static void pon_omci_rx_verify_error_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	ctx->omci_verify_err = -EBADMSG;
	t_omci_owner_set(ctx, T_OMCI_PORTID);
	t_omci_rx_unchecked(test);
	t_omci_counts(test, 0, 0, 1);
	KUNIT_EXPECT_EQ(test, ctx->omci_verifies, 1);
}

static void t_omci_rx_marked(struct kunit *test, u8 mark, bool unverified)
{
	struct pon_test_ctx *ctx = test->priv;
	unsigned int len = T_OMCI_BASELINE_LEN;
	struct sk_buff *skb;

	if (unverified)
		len += PON_OMCI_MIC_LEN;
	skb = alloc_skb(len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	memset(skb_put(skb, len), mark, len);

	KUNIT_EXPECT_EQ(test, pon_omci_conduit_rx(ctx->pdev, skb, unverified),
			0);
}

static void t_omci_ntf_expect(struct kunit *test, u8 mark)
{
	struct nlattr *tb[PON_A_OMCI_MAX + 1];
	struct pon_test_ctx *ctx = test->priv;
	struct nlmsghdr *ntf;
	const u8 *pdu;

	ntf = pon_test_nl_ntf(ctx, PON_CMD_OMCI_NTF);
	KUNIT_ASSERT_NOT_NULL(test, ntf);
	KUNIT_ASSERT_EQ(test, pon_test_nl_parse(ntf, tb, PON_A_OMCI_MAX), 0);
	KUNIT_ASSERT_NOT_NULL(test, tb[PON_A_OMCI_PDU]);
	KUNIT_EXPECT_EQ(test, nla_len(tb[PON_A_OMCI_PDU]), T_OMCI_BASELINE_LEN);
	pdu = nla_data(tb[PON_A_OMCI_PDU]);
	KUNIT_EXPECT_EQ(test, pdu[0], mark);
}

static void pon_omci_rx_order_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pon_test_nl_open(test), 0);
	t_omci_owner_set(ctx, ctx->nl_portid);

	mutex_lock(&ctx->pdev->lock);
	t_omci_rx_marked(test, 1, true);
	t_omci_rx_marked(test, 2, false);
	t_omci_rx_marked(test, 3, true);
	mutex_unlock(&ctx->pdev->lock);

	t_omci_counts(test, 3, 0, 0);
	KUNIT_EXPECT_EQ(test, ctx->omci_verifies, 2);
	t_omci_ntf_expect(test, 1);
	t_omci_ntf_expect(test, 2);
	t_omci_ntf_expect(test, 3);
}

static u8 *t_omci_pdu(struct kunit *test, u8 devid, u16 contents_len)
{
	u8 *pdu;

	pdu = kunit_kzalloc(test, PON_OMCI_MAX_LEN + 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pdu);
	pdu[T_OMCI_DEVID_OFF] = devid;
	put_unaligned_be16(contents_len, pdu + T_OMCI_CONTENTS_LEN_OFF);

	return pdu;
}

static void t_omci_tx(struct kunit *test, const u8 *pdu, unsigned int len,
		      int err, bool refused)
{
	struct netlink_ext_ack extack = {};
	struct pon_test_ctx *ctx = test->priv;

	KUNIT_EXPECT_EQ_MSG(test, pon_omci_xmit(ctx->pdev, pdu, len, &extack),
			    err, "length %u", len);
	KUNIT_EXPECT_EQ_MSG(test, !!extack._msg, refused, "length %u", len);
}

static void pon_omci_xmit_test(struct kunit *test)
{
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;
	u8 *empty, *full;

	empty = t_omci_pdu(test, T_OMCI_DEVID_EXTENDED, 0);
	full = t_omci_pdu(test, T_OMCI_DEVID_EXTENDED,
			  PON_OMCI_MAX_LEN - PON_OMCI_MIN_LEN);

	mutex_lock(&pdev->lock);
	t_omci_tx(test, empty, PON_OMCI_MIN_LEN - 1, -EINVAL, true);
	t_omci_tx(test, full, PON_OMCI_MAX_LEN + 1, -EINVAL, true);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);

	t_omci_tx(test, empty, PON_OMCI_MIN_LEN, 0, false);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len, PON_OMCI_MIN_LEN);
	t_omci_tx(test, full, PON_OMCI_MAX_LEN, 0, false);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len, PON_OMCI_MAX_LEN);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 2);
	KUNIT_EXPECT_TRUE(test, ctx->omci_xmit_bh_off);
	KUNIT_EXPECT_EQ(test, pdev->omci_tx, 2);
	KUNIT_EXPECT_EQ(test, pdev->omci_tx_errors, 0);

	ctx->omci_xmit_err = -EIO;
	t_omci_tx(test, empty, PON_OMCI_MIN_LEN, -EIO, false);
	ctx->omci_xmit_err = -EINVAL;
	t_omci_tx(test, empty, PON_OMCI_MIN_LEN, -EINVAL, false);
	KUNIT_EXPECT_EQ(test, pdev->omci_tx, 2);
	KUNIT_EXPECT_EQ(test, pdev->omci_tx_errors, 2);
	mutex_unlock(&pdev->lock);
}

static void pon_omci_xmit_framing_test(struct kunit *test)
{
	unsigned int ext_len = PON_OMCI_MIN_LEN + T_OMCI_BASELINE_LEN;
	struct pon_test_ctx *ctx = test->priv;
	struct pon_dev *pdev = ctx->pdev;
	u8 *baseline, *extended, *reserved;

	baseline = t_omci_pdu(test, T_OMCI_DEVID_BASELINE, 0);
	extended = t_omci_pdu(test, T_OMCI_DEVID_EXTENDED, T_OMCI_BASELINE_LEN);
	reserved = t_omci_pdu(test, T_OMCI_DEVID_RESERVED, 0);

	mutex_lock(&pdev->lock);
	t_omci_tx(test, baseline, T_OMCI_BASELINE_LEN + PON_OMCI_MIC_LEN,
		  -EINVAL, true);
	t_omci_tx(test, baseline, PON_OMCI_MIN_LEN, -EINVAL, true);
	t_omci_tx(test, extended, ext_len - 1, -EINVAL, true);
	t_omci_tx(test, extended, ext_len + 1, -EINVAL, true);
	t_omci_tx(test, reserved, T_OMCI_BASELINE_LEN, -EINVAL, true);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 0);
	KUNIT_EXPECT_EQ(test, pdev->omci_tx_errors, 0);

	t_omci_tx(test, baseline, T_OMCI_BASELINE_LEN, 0, false);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmit_len, T_OMCI_BASELINE_LEN);
	t_omci_tx(test, extended, ext_len, 0, false);
	KUNIT_EXPECT_EQ(test, ctx->omci_xmits, 2);
	mutex_unlock(&pdev->lock);
}

static struct kunit_case pon_omci_test_cases[] = {
	KUNIT_CASE(pon_omci_rx_no_owner_test),
	KUNIT_CASE(pon_omci_rx_len_test),
	KUNIT_CASE(pon_omci_rx_unchecked_test),
	KUNIT_CASE(pon_omci_rx_queue_limit_test),
	KUNIT_CASE(pon_omci_rx_verify_test),
	KUNIT_CASE(pon_omci_rx_verify_error_test),
	KUNIT_CASE(pon_omci_rx_order_test),
	KUNIT_CASE(pon_omci_release_test),
	KUNIT_CASE(pon_omci_xmit_test),
	KUNIT_CASE(pon_omci_xmit_framing_test),
	{}
};

static struct kunit_suite pon_omci_test_suite = {
	.name = "pon_omci",
	.init = pon_test_init_full,
	.exit = pon_test_exit,
	.test_cases = pon_omci_test_cases,
};

kunit_test_suite(pon_omci_test_suite);
