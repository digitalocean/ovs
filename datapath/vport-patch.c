/*
 * Copyright (c) 2010 Nicira Networks.
 * Distributed under the terms of the GNU GPL version 2.
 *
 * Significant portions of this file may be copied from parts of the Linux
 * kernel, by Linus Torvalds and others.
 */

#include <linux/dcache.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/rtnetlink.h>

#include "datapath.h"
#include "vport.h"
#include "vport-generic.h"

struct device_config {
	struct rcu_head rcu;

	char peer_name[IFNAMSIZ];
	unsigned char eth_addr[ETH_ALEN];
	unsigned int mtu;
};

struct patch_vport {
	struct rcu_head rcu;

	char name[IFNAMSIZ];

	/* Protected by RTNL lock. */
	struct hlist_node hash_node;

	struct vport __rcu *peer;
	struct device_config __rcu *devconf;
};

/* Protected by RTNL lock. */
static struct hlist_head *peer_table;
#define PEER_HASH_BUCKETS 256

static void update_peers(const char *name, struct vport *);

static inline struct patch_vport *patch_vport_priv(const struct vport *vport)
{
	return vport_priv(vport);
}

/* RCU callback. */
static void free_config(struct rcu_head *rcu)
{
	struct device_config *c = container_of(rcu, struct device_config, rcu);
	kfree(c);
}

static void assign_config_rcu(struct vport *vport,
			      struct device_config *new_config)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);
	struct device_config *old_config;

	old_config = rtnl_dereference(patch_vport->devconf);
	rcu_assign_pointer(patch_vport->devconf, new_config);
	call_rcu(&old_config->rcu, free_config);
}

static struct hlist_head *hash_bucket(const char *name)
{
	unsigned int hash = full_name_hash(name, strlen(name));
	return &peer_table[hash & (PEER_HASH_BUCKETS - 1)];
}

static int patch_init(void)
{
	peer_table = kzalloc(PEER_HASH_BUCKETS * sizeof(struct hlist_head),
			    GFP_KERNEL);
	if (!peer_table)
		return -ENOMEM;

	return 0;
}

static void patch_exit(void)
{
	kfree(peer_table);
}

static int set_config(struct vport *vport, const void *config, 
			struct device_config *devconf)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);
	char peer_name[IFNAMSIZ];

	strlcpy(peer_name, config, IFNAMSIZ);

	if (!strcmp(patch_vport->name, peer_name))
		return -EINVAL;

	strcpy(devconf->peer_name, peer_name);

	return 0;
}

static struct vport *patch_create(const struct vport_parms *parms)
{
	struct vport *vport;
	struct patch_vport *patch_vport;
	const char *peer_name;
	struct device_config *devconf;
	int err;

	vport = vport_alloc(sizeof(struct patch_vport), &patch_vport_ops, parms);
	if (IS_ERR(vport)) {
		err = PTR_ERR(vport);
		goto error;
	}

	patch_vport = patch_vport_priv(vport);

	strcpy(patch_vport->name, parms->name);

	devconf = kmalloc(sizeof(struct device_config), GFP_KERNEL);
	if (!devconf) {
		err = -ENOMEM;
		goto error_free_vport;
	}

	err = set_config(vport, parms->config, devconf);
	if (err)
		goto error_free_devconf;

	vport_gen_rand_ether_addr(devconf->eth_addr);

	/* Make the default MTU fairly large so that it doesn't become the
	 * bottleneck on systems using jumbo frames. */
	devconf->mtu = 65535;

	rcu_assign_pointer(patch_vport->devconf, devconf);

	peer_name = devconf->peer_name;
	hlist_add_head(&patch_vport->hash_node, hash_bucket(peer_name));
	rcu_assign_pointer(patch_vport->peer, vport_locate(peer_name));
	update_peers(patch_vport->name, vport);

	return vport;

error_free_devconf:
	kfree(devconf);
error_free_vport:
	vport_free(vport);
error:
	return ERR_PTR(err);
}

static int patch_modify(struct vport *vport, struct odp_port *port)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);
	struct device_config *devconf;
	int err;

	devconf = kmemdup(rtnl_dereference(patch_vport->devconf),
			  sizeof(struct device_config), GFP_KERNEL);
	if (!devconf) {
		err = -ENOMEM;
		goto error;
	}

	err = set_config(vport, port->config, devconf);
	if (err)
		goto error_free;

	assign_config_rcu(vport, devconf);
	
	hlist_del(&patch_vport->hash_node);

	rcu_assign_pointer(patch_vport->peer, vport_locate(devconf->peer_name));
	hlist_add_head(&patch_vport->hash_node, hash_bucket(devconf->peer_name));

	return 0;

error_free:
	kfree(devconf);
error:
	return err;
}

static void free_port_rcu(struct rcu_head *rcu)
{
	struct patch_vport *patch_vport = container_of(rcu,
					  struct patch_vport, rcu);

	kfree((struct device_config __force *)patch_vport->devconf);
	vport_free(vport_from_priv(patch_vport));
}

static int patch_destroy(struct vport *vport)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);

	update_peers(patch_vport->name, NULL);
	hlist_del(&patch_vport->hash_node);
	call_rcu(&patch_vport->rcu, free_port_rcu);

	return 0;
}

static void update_peers(const char *name, struct vport *vport)
{
	struct hlist_head *bucket = hash_bucket(name);
	struct patch_vport *peer_vport;
	struct hlist_node *node;

	hlist_for_each_entry(peer_vport, node, bucket, hash_node) {
		const char *peer_name;

		peer_name = rtnl_dereference(peer_vport->devconf)->peer_name;
		if (!strcmp(peer_name, name))
			rcu_assign_pointer(peer_vport->peer, vport);
	}
}

static int patch_set_mtu(struct vport *vport, int mtu)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);
	struct device_config *devconf;

	devconf = kmemdup(rtnl_dereference(patch_vport->devconf),
			  sizeof(struct device_config), GFP_KERNEL);
	if (!devconf)
		return -ENOMEM;

	devconf->mtu = mtu;
	assign_config_rcu(vport, devconf);

	return 0;
}

static int patch_set_addr(struct vport *vport, const unsigned char *addr)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);
	struct device_config *devconf;

	devconf = kmemdup(rtnl_dereference(patch_vport->devconf),
			  sizeof(struct device_config), GFP_KERNEL);
	if (!devconf)
		return -ENOMEM;

	memcpy(devconf->eth_addr, addr, ETH_ALEN);
	assign_config_rcu(vport, devconf);

	return 0;
}


static const char *patch_get_name(const struct vport *vport)
{
	const struct patch_vport *patch_vport = patch_vport_priv(vport);
	return patch_vport->name;
}

static const unsigned char *patch_get_addr(const struct vport *vport)
{
	const struct patch_vport *patch_vport = patch_vport_priv(vport);
	return rcu_dereference_rtnl(patch_vport->devconf)->eth_addr;
}

static void patch_get_config(const struct vport *vport, void *config)
{
	const struct patch_vport *patch_vport = patch_vport_priv(vport);
	const char *peer_name;

	peer_name = rcu_dereference_rtnl(patch_vport->devconf)->peer_name;
	strlcpy(config, peer_name, VPORT_CONFIG_SIZE);
}

static int patch_get_mtu(const struct vport *vport)
{
	const struct patch_vport *patch_vport = patch_vport_priv(vport);
	return rcu_dereference_rtnl(patch_vport->devconf)->mtu;
}

static int patch_send(struct vport *vport, struct sk_buff *skb)
{
	struct patch_vport *patch_vport = patch_vport_priv(vport);
	struct vport *peer = rcu_dereference(patch_vport->peer);
	int skb_len = skb->len;

	if (!peer) {
		kfree_skb(skb);
		vport_record_error(vport, VPORT_E_TX_DROPPED);

		return 0;
	}

	vport_receive(peer, skb);
	return skb_len;
}

const struct vport_ops patch_vport_ops = {
	.type		= "patch",
	.flags		= VPORT_F_GEN_STATS,
	.init		= patch_init,
	.exit		= patch_exit,
	.create		= patch_create,
	.modify		= patch_modify,
	.destroy	= patch_destroy,
	.set_mtu	= patch_set_mtu,
	.set_addr	= patch_set_addr,
	.get_name	= patch_get_name,
	.get_addr	= patch_get_addr,
	.get_config	= patch_get_config,
	.get_dev_flags	= vport_gen_get_dev_flags,
	.is_running	= vport_gen_is_running,
	.get_operstate	= vport_gen_get_operstate,
	.get_mtu	= patch_get_mtu,
	.send		= patch_send,
};
