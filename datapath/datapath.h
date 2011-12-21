/*
 * Copyright (c) 2009, 2010, 2011 Nicira Networks.
 * Distributed under the terms of the GNU GPL version 2.
 *
 * Significant portions of this file may be copied from parts of the Linux
 * kernel, by Linus Torvalds and others.
 */

/* Interface exported by openvswitch_mod. */

#ifndef DATAPATH_H
#define DATAPATH_H 1

#include <asm/page.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/seqlock.h>
#include <linux/skbuff.h>
#include <linux/version.h>

#include "checksum.h"
#include "compat.h"
#include "flow.h"
#include "dp_sysfs.h"
#include "vlan.h"

struct vport;

#define DP_MAX_PORTS 1024
#define SAMPLE_ACTION_DEPTH 3

/**
 * struct dp_stats_percpu - per-cpu packet processing statistics for a given
 * datapath.
 * @n_hit: Number of received packets for which a matching flow was found in
 * the flow table.
 * @n_miss: Number of received packets that had no matching flow in the flow
 * table.  The sum of @n_hit and @n_miss is the number of packets that have
 * been received by the datapath.
 * @n_lost: Number of received packets that had no matching flow in the flow
 * table that could not be sent to userspace (normally due to an overflow in
 * one of the datapath's queues).
 */
struct dp_stats_percpu {
	u64 n_hit;
	u64 n_missed;
	u64 n_lost;
	seqcount_t seqlock;
};

/**
 * struct datapath - datapath for flow-based packet switching
 * @rcu: RCU callback head for deferred destruction.
 * @list_node: Element in global 'dps' list.
 * @ifobj: Represents /sys/class/net/<devname>/brif.  Protected by RTNL.
 * @n_flows: Number of flows currently in flow table.
 * @table: Current flow table.  Protected by genl_lock and RCU.
 * @ports: Map from port number to &struct vport.  %OVSP_LOCAL port
 * always exists, other ports may be %NULL.  Protected by RTNL and RCU.
 * @port_list: List of all ports in @ports in arbitrary order.  RTNL required
 * to iterate or modify.
 * @stats_percpu: Per-CPU datapath statistics.
 *
 * Context: See the comment on locking at the top of datapath.c for additional
 * locking information.
 */
struct datapath {
	struct rcu_head rcu;
	struct list_head list_node;
	struct kobject ifobj;

	/* Flow table. */
	struct flow_table __rcu *table;

	/* Switch ports. */
	struct vport __rcu *ports[DP_MAX_PORTS];
	struct list_head port_list;

	/* Stats. */
	struct dp_stats_percpu __percpu *stats_percpu;
};

/**
 * struct ovs_skb_cb - OVS data in skb CB
 * @vport: The datapath port on which the skb entered the switch.
 * @flow: The flow associated with this packet.  May be %NULL if no flow.
 * @tun_id: ID of the tunnel that encapsulated this packet.  It is 0 if the
 * @ip_summed: Consistently stores L4 checksumming status across different
 * kernel versions.
 * @csum_start: Stores the offset from which to start checksumming independent
 * of the transport header on all kernel versions.
 * packet was not received on a tunnel.
 * @vlan_tci: Provides a substitute for the skb->vlan_tci field on kernels
 * before 2.6.27.
 */
struct ovs_skb_cb {
	struct vport		*vport;
	struct sw_flow		*flow;
	__be64			tun_id;
#ifdef NEED_CSUM_NORMALIZE
	enum csum_type		ip_summed;
	u16			csum_start;
#endif
#ifdef NEED_VLAN_FIELD
	u16			vlan_tci;
#endif
};
#define OVS_CB(skb) ((struct ovs_skb_cb *)(skb)->cb)

/**
 * struct dp_upcall - metadata to include with a packet to send to userspace
 * @cmd: One of %OVS_PACKET_CMD_*.
 * @key: Becomes %OVS_PACKET_ATTR_KEY.  Must be nonnull.
 * @userdata: If nonnull, its u64 value is extracted and passed to userspace as
 * %OVS_PACKET_ATTR_USERDATA.
 * @pid: Netlink PID to which packet should be sent.  If @pid is 0 then no
 * packet is sent and the packet is accounted in the datapath's @n_lost
 * counter.
 */
struct dp_upcall_info {
	u8 cmd;
	const struct sw_flow_key *key;
	const struct nlattr *userdata;
	u32 pid;
};

extern struct notifier_block dp_device_notifier;
extern struct genl_multicast_group dp_vport_multicast_group;
extern int (*dp_ioctl_hook)(struct net_device *dev, struct ifreq *rq, int cmd);

void dp_process_received_packet(struct vport *, struct sk_buff *);
void dp_detach_port(struct vport *);
int dp_upcall(struct datapath *, struct sk_buff *, const struct dp_upcall_info *);

struct datapath *get_dp(int dp_idx);
const char *dp_name(const struct datapath *dp);
struct sk_buff *ovs_vport_cmd_build_info(struct vport *, u32 pid, u32 seq,
					 u8 cmd);

#endif /* datapath.h */
