/*
 * Copyright (c) 2009, 2010, 2011 Nicira Networks.
 * Distributed under the terms of the GNU GPL version 2.
 *
 * Significant portions of this file may be copied from parts of the Linux
 * kernel, by Linus Torvalds and others.
 */

#ifndef FLOW_H
#define FLOW_H 1

#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/jiffies.h>
#include <linux/time.h>

#include "openvswitch/datapath-protocol.h"
#include "table.h"

struct sk_buff;

struct sw_flow_actions {
	struct rcu_head rcu;
	u32 actions_len;
	struct nlattr actions[];
};

struct sw_flow_key {
	__be64	tun_id;     /* Encapsulating tunnel ID. */
	union {
		struct {
			__be32	ipv4_src;	 /* IPv4 source address. */
			__be32	ipv4_dst;	 /* IPv4 destination address. */
		};
		struct {
			struct in6_addr	ipv6_src; /* IPv6 source address. */
			struct in6_addr ipv6_dst; /* IPv6 source address. */
		};
	};
	struct in6_addr	nd_target; /* IPv6 ND target address. */
	u16	in_port;    /* Input switch port. */
	__be16	dl_tci;	    /* 0 if no VLAN, VLAN_TAG_PRESENT set otherwise. */
	__be16	dl_type;    /* Ethernet frame type. */
	__be16	tp_src;	    /* TCP/UDP source port. */
	__be16	tp_dst;	    /* TCP/UDP destination port. */
	u8	dl_src[ETH_ALEN]; /* Ethernet source address. */
	u8	dl_dst[ETH_ALEN]; /* Ethernet destination address. */
	u8	nw_proto;   /* IP protocol or lower 8 bits of ARP opcode. */
	u8	nw_tos;	    /* IP ToS (DSCP field, 6 bits). */
	u8	arp_sha[ETH_ALEN]; /* ARP/ND source hardware address. */
	u8	arp_tha[ETH_ALEN]; /* ARP/ND target hardware address. */
};

struct sw_flow {
	struct rcu_head rcu;
	struct tbl_node tbl_node;

	struct sw_flow_key key;
	struct sw_flow_actions __rcu *sf_acts;

	atomic_t refcnt;
	bool dead;

	spinlock_t lock;	/* Lock for values below. */
	unsigned long used;	/* Last used time (in jiffies). */
	u64 packet_count;	/* Number of packets matched. */
	u64 byte_count;		/* Number of bytes matched. */
	u8 tcp_flags;		/* Union of seen TCP flags. */
};

struct arp_eth_header
{
	__be16      ar_hrd;	/* format of hardware address   */
	__be16      ar_pro;	/* format of protocol address   */
	unsigned char   ar_hln;	/* length of hardware address   */
	unsigned char   ar_pln;	/* length of protocol address   */
	__be16      ar_op;	/* ARP opcode (command)     */

	/* Ethernet+IPv4 specific members. */
	unsigned char       ar_sha[ETH_ALEN];	/* sender hardware address  */
	unsigned char       ar_sip[4];		/* sender IP address        */
	unsigned char       ar_tha[ETH_ALEN];	/* target hardware address  */
	unsigned char       ar_tip[4];		/* target IP address        */
} __packed;

int flow_init(void);
void flow_exit(void);

struct sw_flow *flow_alloc(void);
void flow_deferred_free(struct sw_flow *);
void flow_free_tbl(struct tbl_node *);

struct sw_flow_actions *flow_actions_alloc(const struct nlattr *);
void flow_deferred_free_acts(struct sw_flow_actions *);

void flow_hold(struct sw_flow *);
void flow_put(struct sw_flow *);

int flow_extract(struct sk_buff *, u16 in_port, struct sw_flow_key *, bool *is_frag);
void flow_used(struct sw_flow *, struct sk_buff *);
u64 flow_used_time(unsigned long flow_jiffies);

u32 flow_hash(const struct sw_flow_key *);
int flow_cmp(const struct tbl_node *, void *target);

/* Upper bound on the length of a nlattr-formatted flow key.  The longest
 * nlattr-formatted flow key would be:
 *
 *                         struct  pad  nl hdr  total
 *                         ------  ---  ------  -----
 *  ODP_KEY_ATTR_TUN_ID        8    --     4     12
 *  ODP_KEY_ATTR_IN_PORT       4    --     4      8
 *  ODP_KEY_ATTR_ETHERNET     12    --     4     16
 *  ODP_KEY_ATTR_8021Q         4    --     4      8
 *  ODP_KEY_ATTR_ETHERTYPE     2     2     4      8
 *  ODP_KEY_ATTR_IPV6         34     2     4     40
 *  ODP_KEY_ATTR_ICMPV6        2     2     4      8
 *  ODP_KEY_ATTR_ND           28    --     4     32
 *  -------------------------------------------------
 *  total                                       132
 */
#define FLOW_BUFSIZE 132

int flow_to_nlattrs(const struct sw_flow_key *, struct sk_buff *);
int flow_from_nlattrs(struct sw_flow_key *swkey, const struct nlattr *);

static inline struct sw_flow *flow_cast(const struct tbl_node *node)
{
	return container_of(node, struct sw_flow, tbl_node);
}

#endif /* flow.h */
