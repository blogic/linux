/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 John Crispin <john@phrozen.org> */

#ifndef _NET_PON_TESTS_PON_TEST_H
#define _NET_PON_TESTS_PON_TEST_H

#include <kunit/test.h>
#include <linux/bitmap.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/workqueue.h>
#include <net/pkt_cls.h>
#include <net/pon.h>

#include "../pon.h"

struct kmsg_dump_iter;
struct software_node;

#define T_TCONT_INDEX		1
#define T_TCONT_ALLOC_ID	1027
#define T_OTHER_ALLOC_ID	1155
#define T_ONU_ID		5
#define T_CHANNEL(alloc_id)	((alloc_id) & 0xff)
#define T_TCONT_CHANNEL		T_CHANNEL(T_TCONT_ALLOC_ID)
#define T_OTHER_CHANNEL		T_CHANNEL(T_OTHER_ALLOC_ID)
#define T_ETS_LOG		8
#define T_GEM_ID		2306
#define T_GEM_COUNT		3
#define T_OTHER_TCONT_INDEX	2
#define T_OMCI_BASELINE_LEN	44
#define T_OMCI_PORTID		4242
#define T_NL_BUF_LEN		32768
#define T_PCS_LOG		8
#define T_LOG_LINE_LEN		256
#define T_LOG_TEXT_LEN		48

struct pon_test_ets_call {
	unsigned int channel;
	enum tc_ets_command command;
};

struct pon_test_nl_msg {
	struct list_head list;
	struct nlmsghdr *nlh;
};

struct pon_test_ctx {
	struct kunit *test;
	struct device *parent;
	struct net_device *netdev;
	struct pon_dev *pdev;
	bool unregistered;
	struct work_struct unregister_work;

	struct pon_fec_stats fec;
	int fec_err;
	unsigned int gem_adds;
	u16 gem_alloc_id[T_GEM_COUNT];
	u16 gem_fail_id;
	int gem_add_err;
	unsigned int map_sets;
	int map_set_err;
	struct pon_gem_map_cfg map_last;
	unsigned int map_dels;
	int map_del_err;
	unsigned int channel_reads;
	DECLARE_BITMAP(bound, PON_PLOAM_ALLOC_ID_MAX + 1);

	unsigned int enables;
	bool enable_on;
	int enable_err;
	unsigned int identities;
	struct pon_identity identity;
	int identity_err;
	unsigned int tcont_sets;
	struct pon_tcont_cfg tcont_set_last;
	int tcont_set_err;
	unsigned int tcont_clears;
	int tcont_clear_err;
	unsigned int gem_dels;
	int gem_del_err;
	u64 gem_rx_frames;
	int gem_stats_err;
	struct pon_tc_stats tc_stats;
	int tc_stats_err;
	u32 response_time;
	int response_time_err;
	unsigned int gem_xmits;
	u16 gem_xmit_id;
	int gem_xmit_err;
	unsigned int omci_xmits;
	unsigned int omci_xmit_len;
	bool omci_xmit_bh_off;
	int omci_xmit_err;
	unsigned int omci_verifies;
	int omci_verify_err;
	unsigned int resolves;
	struct pon_flow_key resolve_key;
	u16 resolve_gem;
	int resolve_err;
	unsigned int msk_sets;
	u8 msk[PON_KEY_LEN];
	int msk_err;
	unsigned int bcast_sets;
	u8 bcast_index;
	bool bcast_has_key;
	u8 bcast_key[PON_KEY_LEN];
	int bcast_err;
	struct pon_pcs *pcs;
	unsigned int pcs_calls;
	bool pcs_log[T_PCS_LOG];

	struct net_device *conduit;
	unsigned int conduit_xmits;
	struct pon_tx_info conduit_tx_info;
	netdev_tx_t conduit_xmit_ret;
	unsigned int conduit_setup_tcs;
	unsigned int conduit_channel;
	enum tc_setup_type conduit_tc_type;
	enum tc_ets_command conduit_ets_command;
	struct tc_ets_qopt_offload_replace_params conduit_ets;
	struct pon_test_ets_call conduit_ets_log[T_ETS_LOG];
	int conduit_setup_tc_err;
	unsigned int conduit_flow_setups;
	int conduit_flow_setup_err;
	unsigned int conduit_flushes;
	u16 conduit_flush_gem;
	unsigned int conduit_addr_sets;
	int conduit_addr_err;
	unsigned int conduit_mtu_sets;
	int conduit_mtu_err;

	struct socket *nl_sock;
	struct file *nl_file;
	u32 nl_portid;
	u32 nl_seq;
	u8 *nl_buf;
	struct list_head nl_pending;
};

extern const struct pon_dev_caps pon_test_caps;
extern const struct pon_dev_ops pon_test_ops;
extern const struct pon_dev_ops pon_test_ops_no_channel;
extern const struct pon_dev_ops pon_test_ops_full;
extern const struct software_node pon_test_mac_node;
extern const struct software_node pon_test_conduit_node;

struct net_device *pon_test_netdev_create(void);
void pon_test_netdev_destroy(struct net_device *dev);

int pon_test_init_ops(struct kunit *test, const struct pon_dev_ops *ops);
int pon_test_init(struct kunit *test);
int pon_test_init_full(struct kunit *test);
void pon_test_exit(struct kunit *test);
void pon_test_ops_set(struct pon_test_ctx *ctx, const struct pon_dev_ops *ops);
void pon_test_unregister(struct pon_test_ctx *ctx);

int pon_test_state_report(struct pon_test_ctx *ctx,
			  enum pon_ploam_state state);
enum pon_ploam_state pon_test_state(struct pon_test_ctx *ctx);
void pon_test_activate(struct pon_test_ctx *ctx);
void pon_test_alloc_bind(struct pon_test_ctx *ctx, u16 alloc_id);
void pon_test_alloc_assign(struct pon_test_ctx *ctx, u16 alloc_id);
void pon_test_alloc_release(struct pon_test_ctx *ctx, u16 alloc_id);
void pon_test_alloc_drop_all(struct pon_test_ctx *ctx);

struct pon_tcont *pon_test_tcont_new(struct kunit *test, u16 index,
				     u16 alloc_id);
struct pon_gem *pon_test_gem_new(struct kunit *test, u16 gem_id,
				 u16 tcont_index, u16 alloc_id);
struct pon_gem_map *pon_test_gem_map_add(struct kunit *test,
					 const struct pon_gem_map_cfg *cfg);
void pon_test_gem_map_new(struct kunit *test, u16 gem_id);

struct nlattr *pon_test_nla_u32(struct kunit *test, int type, u32 value);
struct net_device *pon_test_gem_link_new(struct kunit *test, u16 gem_id);
int pon_test_gem_link_try(struct kunit *test, struct nlattr **tb,
			  struct nlattr **data, struct net_device **devp);
void pon_test_gem_link_del(struct net_device *dev);

struct sk_buff *pon_test_frame(struct kunit *test, unsigned int len);
unsigned int pon_test_log_lines(struct kunit *test,
				struct kmsg_dump_iter *iter, const char *what,
				char lines[][T_LOG_TEXT_LEN], unsigned int max);
int pon_test_log_level(struct kunit *test, struct kmsg_dump_iter *iter,
		       const char *text);
void pon_test_log_flush(struct pon_test_ctx *ctx);

struct net_device *pon_test_conduit_create(struct kunit *test,
					   const struct software_node *node);
void pon_test_conduit_destroy(struct net_device *conduit);
int pon_test_conduit_register(struct kunit *test);
int pon_test_conduit_up(struct kunit *test);
int pon_test_mac_node_add(struct kunit *test, struct device *dev,
			  const struct software_node *node);

int pon_test_nl_open(struct kunit *test);
int pon_test_nl_join(struct pon_test_ctx *ctx, unsigned int group);
struct sk_buff *pon_test_nl_new(struct pon_test_ctx *ctx, u8 cmd, u16 flags);
int pon_test_nl_request(struct pon_test_ctx *ctx, struct sk_buff *skb,
			struct nlmsghdr **reply);
int pon_test_nl_dump(struct pon_test_ctx *ctx, struct sk_buff *skb,
		     struct list_head *msgs, bool *intr);
int pon_test_nl_dump_split(struct pon_test_ctx *ctx, struct sk_buff *skb,
			   struct list_head *msgs, bool *intr,
			   void (*between)(struct pon_test_ctx *ctx));
struct nlmsghdr *pon_test_nl_ntf(struct pon_test_ctx *ctx, u8 cmd);
void pon_test_nl_ntf_flush(struct pon_test_ctx *ctx);
int pon_test_nl_parse(const struct nlmsghdr *nlh, struct nlattr **tb,
		      int maxtype);
u8 pon_test_nl_cmd(const struct nlmsghdr *nlh);

#endif
