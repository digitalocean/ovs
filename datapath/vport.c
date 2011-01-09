/*
 * Copyright (c) 2010 Nicira Networks.
 * Distributed under the terms of the GNU GPL version 2.
 *
 * Significant portions of this file may be copied from parts of the Linux
 * kernel, by Linus Torvalds and others.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dcache.h>
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/rtnetlink.h>
#include <linux/compat.h>
#include <linux/version.h>

#include "vport.h"
#include "vport-internal_dev.h"

/* List of statically compiled vport implementations.  Don't forget to also
 * add yours to the list at the bottom of vport.h. */
static const struct vport_ops *base_vport_ops_list[] = {
	&netdev_vport_ops,
	&internal_vport_ops,
	&patch_vport_ops,
	&gre_vport_ops,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
	&capwap_vport_ops,
#endif
};

static const struct vport_ops **vport_ops_list;
static int n_vport_types;

static struct hlist_head *dev_table;
#define VPORT_HASH_BUCKETS 1024

/* Both RTNL lock and vport_mutex need to be held when updating dev_table.
 *
 * If you use vport_locate and then perform some operations, you need to hold
 * one of these locks if you don't want the vport to be deleted out from under
 * you.
 *
 * If you get a reference to a vport through a datapath, it is protected
 * by RCU and you need to hold rcu_read_lock instead when reading.
 *
 * If multiple locks are taken, the hierarchy is:
 * 1. RTNL
 * 2. DP
 * 3. vport
 */
static DEFINE_MUTEX(vport_mutex);

/**
 *	vport_lock - acquire vport lock
 *
 * Acquire global vport lock.  See above comment about locking requirements
 * and specific function definitions.  May sleep.
 */
void vport_lock(void)
{
	mutex_lock(&vport_mutex);
}

/**
 *	vport_unlock - release vport lock
 *
 * Release lock acquired with vport_lock.
 */
void vport_unlock(void)
{
	mutex_unlock(&vport_mutex);
}

#define ASSERT_VPORT()						\
do {								\
	if (unlikely(!mutex_is_locked(&vport_mutex))) {		\
		pr_err("vport lock not held at %s (%d)\n",	\
		       __FILE__, __LINE__);			\
		dump_stack();					\
	}							\
} while (0)

/**
 *	vport_init - initialize vport subsystem
 *
 * Called at module load time to initialize the vport subsystem and any
 * compiled in vport types.
 */
int vport_init(void)
{
	int err;
	int i;

	dev_table = kzalloc(VPORT_HASH_BUCKETS * sizeof(struct hlist_head),
			    GFP_KERNEL);
	if (!dev_table) {
		err = -ENOMEM;
		goto error;
	}

	vport_ops_list = kmalloc(ARRAY_SIZE(base_vport_ops_list) *
				 sizeof(struct vport_ops *), GFP_KERNEL);
	if (!vport_ops_list) {
		err = -ENOMEM;
		goto error_dev_table;
	}

	for (i = 0; i < ARRAY_SIZE(base_vport_ops_list); i++) {
		const struct vport_ops *new_ops = base_vport_ops_list[i];

		if (new_ops->init)
			err = new_ops->init();
		else
			err = 0;

		if (!err)
			vport_ops_list[n_vport_types++] = new_ops;
		else if (new_ops->flags & VPORT_F_REQUIRED) {
			vport_exit();
			goto error;
		}
	}

	return 0;

error_dev_table:
	kfree(dev_table);
error:
	return err;
}

static void vport_del_all(void)
{
	int i;

	rtnl_lock();
	vport_lock();

	for (i = 0; i < VPORT_HASH_BUCKETS; i++) {
		struct hlist_head *bucket = &dev_table[i];
		struct vport *vport;
		struct hlist_node *node, *next;

		hlist_for_each_entry_safe(vport, node, next, bucket, hash_node)
			vport_del(vport);
	}

	vport_unlock();
	rtnl_unlock();
}

/**
 *	vport_exit - shutdown vport subsystem
 *
 * Called at module exit time to shutdown the vport subsystem and any
 * initialized vport types.
 */
void vport_exit(void)
{
	int i;

	vport_del_all();

	for (i = 0; i < n_vport_types; i++) {
		if (vport_ops_list[i]->exit)
			vport_ops_list[i]->exit();
	}

	kfree(vport_ops_list);
	kfree(dev_table);
}

/**
 *	vport_user_mod - modify existing vport device (for userspace callers)
 *
 * @uport: New configuration for vport
 *
 * Modifies an existing device with the specified configuration (which is
 * dependent on device type).  This function is for userspace callers and
 * assumes no locks are held.
 */
int vport_user_mod(const struct odp_port __user *uport)
{
	struct odp_port port;
	struct vport *vport;
	int err;

	if (copy_from_user(&port, uport, sizeof(port)))
		return -EFAULT;

	port.devname[IFNAMSIZ - 1] = '\0';

	rtnl_lock();

	vport = vport_locate(port.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	vport_lock();
	err = vport_mod(vport, &port);
	vport_unlock();

out:
	rtnl_unlock();
	return err;
}

/**
 *	vport_user_stats_get - retrieve device stats (for userspace callers)
 *
 * @ustats_req: Stats request parameters.
 *
 * Retrieves transmit, receive, and error stats for the given device.  This
 * function is for userspace callers and assumes no locks are held.
 */
int vport_user_stats_get(struct odp_vport_stats_req __user *ustats_req)
{
	struct odp_vport_stats_req stats_req;
	struct vport *vport;
	int err;

	if (copy_from_user(&stats_req, ustats_req, sizeof(struct odp_vport_stats_req)))
		return -EFAULT;

	stats_req.devname[IFNAMSIZ - 1] = '\0';

	vport_lock();

	vport = vport_locate(stats_req.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	err = vport_get_stats(vport, &stats_req.stats);

out:
	vport_unlock();

	if (!err)
		if (copy_to_user(ustats_req, &stats_req, sizeof(struct odp_vport_stats_req)))
			err = -EFAULT;

	return err;
}

/**
 *	vport_user_stats_set - sets offset device stats (for userspace callers)
 *
 * @ustats_req: Stats set parameters.
 *
 * Provides a set of transmit, receive, and error stats to be added as an
 * offset to the collect data when stats are retreived.  Some devices may not
 * support setting the stats, in which case the result will always be
 * -EOPNOTSUPP.  This function is for userspace callers and assumes no locks
 * are held.
 */
int vport_user_stats_set(struct odp_vport_stats_req __user *ustats_req)
{
	struct odp_vport_stats_req stats_req;
	struct vport *vport;
	int err;

	if (copy_from_user(&stats_req, ustats_req, sizeof(struct odp_vport_stats_req)))
		return -EFAULT;

	stats_req.devname[IFNAMSIZ - 1] = '\0';

	rtnl_lock();
	vport_lock();

	vport = vport_locate(stats_req.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	err = vport_set_stats(vport, &stats_req.stats);

out:
	vport_unlock();
	rtnl_unlock();
	return err;
}


/**
 *	vport_user_ether_get - retrieve device Ethernet address (for userspace callers)
 *
 * @uvport_ether: Ethernet address request parameters.
 *
 * Retrieves the Ethernet address of the given device.  This function is for
 * userspace callers and assumes no locks are held.
 */
int vport_user_ether_get(struct odp_vport_ether __user *uvport_ether)
{
	struct odp_vport_ether vport_ether;
	struct vport *vport;
	int err = 0;

	if (copy_from_user(&vport_ether, uvport_ether, sizeof(struct odp_vport_ether)))
		return -EFAULT;

	vport_ether.devname[IFNAMSIZ - 1] = '\0';

	vport_lock();

	vport = vport_locate(vport_ether.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	rcu_read_lock();
	memcpy(vport_ether.ether_addr, vport_get_addr(vport), ETH_ALEN);
	rcu_read_unlock();

out:
	vport_unlock();

	if (!err)
		if (copy_to_user(uvport_ether, &vport_ether, sizeof(struct odp_vport_ether)))
			err = -EFAULT;

	return err;
}

/**
 *	vport_user_ether_set - set device Ethernet address (for userspace callers)
 *
 * @uvport_ether: Ethernet address request parameters.
 *
 * Sets the Ethernet address of the given device.  Some devices may not support
 * setting the Ethernet address, in which case the result will always be
 * -EOPNOTSUPP.  This function is for userspace callers and assumes no locks
 * are held.
 */
int vport_user_ether_set(struct odp_vport_ether __user *uvport_ether)
{
	struct odp_vport_ether vport_ether;
	struct vport *vport;
	int err;

	if (copy_from_user(&vport_ether, uvport_ether, sizeof(struct odp_vport_ether)))
		return -EFAULT;

	vport_ether.devname[IFNAMSIZ - 1] = '\0';

	rtnl_lock();
	vport_lock();

	vport = vport_locate(vport_ether.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	err = vport_set_addr(vport, vport_ether.ether_addr);

out:
	vport_unlock();
	rtnl_unlock();
	return err;
}

/**
 *	vport_user_mtu_get - retrieve device MTU (for userspace callers)
 *
 * @uvport_mtu: MTU request parameters.
 *
 * Retrieves the MTU of the given device.  This function is for userspace
 * callers and assumes no locks are held.
 */
int vport_user_mtu_get(struct odp_vport_mtu __user *uvport_mtu)
{
	struct odp_vport_mtu vport_mtu;
	struct vport *vport;
	int err = 0;

	if (copy_from_user(&vport_mtu, uvport_mtu, sizeof(struct odp_vport_mtu)))
		return -EFAULT;

	vport_mtu.devname[IFNAMSIZ - 1] = '\0';

	vport_lock();

	vport = vport_locate(vport_mtu.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	vport_mtu.mtu = vport_get_mtu(vport);

out:
	vport_unlock();

	if (!err)
		if (copy_to_user(uvport_mtu, &vport_mtu, sizeof(struct odp_vport_mtu)))
			err = -EFAULT;

	return err;
}

/**
 *	vport_user_mtu_set - set device MTU (for userspace callers)
 *
 * @uvport_mtu: MTU request parameters.
 *
 * Sets the MTU of the given device.  Some devices may not support setting the
 * MTU, in which case the result will always be -EOPNOTSUPP.  This function is
 * for userspace callers and assumes no locks are held.
 */
int vport_user_mtu_set(struct odp_vport_mtu __user *uvport_mtu)
{
	struct odp_vport_mtu vport_mtu;
	struct vport *vport;
	int err;

	if (copy_from_user(&vport_mtu, uvport_mtu, sizeof(struct odp_vport_mtu)))
		return -EFAULT;

	vport_mtu.devname[IFNAMSIZ - 1] = '\0';

	rtnl_lock();
	vport_lock();

	vport = vport_locate(vport_mtu.devname);
	if (!vport) {
		err = -ENODEV;
		goto out;
	}

	err = vport_set_mtu(vport, vport_mtu.mtu);

out:
	vport_unlock();
	rtnl_unlock();
	return err;
}

static struct hlist_head *hash_bucket(const char *name)
{
	unsigned int hash = full_name_hash(name, strlen(name));
	return &dev_table[hash & (VPORT_HASH_BUCKETS - 1)];
}

/**
 *	vport_locate - find a port that has already been created
 *
 * @name: name of port to find
 *
 * Either RTNL or vport lock must be acquired before calling this function
 * and held while using the found port.  See the locking comments at the
 * top of the file.
 */
struct vport *vport_locate(const char *name)
{
	struct hlist_head *bucket = hash_bucket(name);
	struct vport *vport;
	struct hlist_node *node;

	if (unlikely(!mutex_is_locked(&vport_mutex) && !rtnl_is_locked())) {
		pr_err("neither RTNL nor vport lock held in vport_locate\n");
		dump_stack();
	}

	rcu_read_lock();

	hlist_for_each_entry(vport, node, bucket, hash_node)
		if (!strcmp(name, vport_get_name(vport)))
			goto out;

	vport = NULL;

out:
	rcu_read_unlock();
	return vport;
}

static void register_vport(struct vport *vport)
{
	hlist_add_head(&vport->hash_node, hash_bucket(vport_get_name(vport)));
}

static void unregister_vport(struct vport *vport)
{
	hlist_del(&vport->hash_node);
}

static void release_vport(struct kobject *kobj)
{
	struct vport *p = container_of(kobj, struct vport, kobj);
	kfree(p);
}

static struct kobj_type brport_ktype = {
#ifdef CONFIG_SYSFS
	.sysfs_ops = &brport_sysfs_ops,
#endif
	.release = release_vport
};

/**
 *	vport_alloc - allocate and initialize new vport
 *
 * @priv_size: Size of private data area to allocate.
 * @ops: vport device ops
 *
 * Allocate and initialize a new vport defined by @ops.  The vport will contain
 * a private data area of size @priv_size that can be accessed using
 * vport_priv().  vports that are no longer needed should be released with
 * vport_free().
 */
struct vport *vport_alloc(int priv_size, const struct vport_ops *ops, const struct vport_parms *parms)
{
	struct vport *vport;
	size_t alloc_size;

	alloc_size = sizeof(struct vport);
	if (priv_size) {
		alloc_size = ALIGN(alloc_size, VPORT_ALIGN);
		alloc_size += priv_size;
	}

	vport = kzalloc(alloc_size, GFP_KERNEL);
	if (!vport)
		return ERR_PTR(-ENOMEM);

	vport->dp = parms->dp;
	vport->port_no = parms->port_no;
	atomic_set(&vport->sflow_pool, 0);
	vport->ops = ops;

	/* Initialize kobject for bridge.  This will be added as
	 * /sys/class/net/<devname>/brport later, if sysfs is enabled. */
	vport->kobj.kset = NULL;
	kobject_init(&vport->kobj, &brport_ktype);

	if (vport->ops->flags & VPORT_F_GEN_STATS) {
		vport->percpu_stats = alloc_percpu(struct vport_percpu_stats);
		if (!vport->percpu_stats)
			return ERR_PTR(-ENOMEM);

		spin_lock_init(&vport->stats_lock);
	}

	return vport;
}

/**
 *	vport_free - uninitialize and free vport
 *
 * @vport: vport to free
 *
 * Frees a vport allocated with vport_alloc() when it is no longer needed.
 */
void vport_free(struct vport *vport)
{
	if (vport->ops->flags & VPORT_F_GEN_STATS)
		free_percpu(vport->percpu_stats);

	kobject_put(&vport->kobj);
}

/**
 *	vport_add - add vport device (for kernel callers)
 *
 * @parms: Information about new vport.
 *
 * Creates a new vport with the specified configuration (which is dependent on
 * device type) and attaches it to a datapath.  Both RTNL and vport locks must
 * be held.
 */
struct vport *vport_add(const struct vport_parms *parms)
{
	struct vport *vport;
	int err = 0;
	int i;

	ASSERT_RTNL();
	ASSERT_VPORT();

	for (i = 0; i < n_vport_types; i++) {
		if (!strcmp(vport_ops_list[i]->type, parms->type)) {
			vport = vport_ops_list[i]->create(parms);
			if (IS_ERR(vport)) {
				err = PTR_ERR(vport);
				goto out;
			}

			register_vport(vport);
			return vport;
		}
	}

	err = -EAFNOSUPPORT;

out:
	return ERR_PTR(err);
}

/**
 *	vport_mod - modify existing vport device (for kernel callers)
 *
 * @vport: vport to modify.
 * @port: New configuration.
 *
 * Modifies an existing device with the specified configuration (which is
 * dependent on device type).  Both RTNL and vport locks must be held.
 */
int vport_mod(struct vport *vport, struct odp_port *port)
{
	ASSERT_RTNL();
	ASSERT_VPORT();

	if (vport->ops->modify)
		return vport->ops->modify(vport, port);
	else
		return -EOPNOTSUPP;
}

/**
 *	vport_del - delete existing vport device (for kernel callers)
 *
 * @vport: vport to delete.
 *
 * Detaches @vport from its datapath and destroys it.  It is possible to fail
 * for reasons such as lack of memory.  Both RTNL and vport locks must be held.
 */
int vport_del(struct vport *vport)
{
	ASSERT_RTNL();
	ASSERT_VPORT();

	unregister_vport(vport);

	return vport->ops->destroy(vport);
}

/**
 *	vport_set_mtu - set device MTU (for kernel callers)
 *
 * @vport: vport on which to set MTU.
 * @mtu: New MTU.
 *
 * Sets the MTU of the given device.  Some devices may not support setting the
 * MTU, in which case the result will always be -EOPNOTSUPP.  RTNL lock must
 * be held.
 */
int vport_set_mtu(struct vport *vport, int mtu)
{
	ASSERT_RTNL();

	if (mtu < 68)
		return -EINVAL;

	if (vport->ops->set_mtu) {
		int ret;

		ret = vport->ops->set_mtu(vport, mtu);

		if (!ret && !is_internal_vport(vport))
			set_internal_devs_mtu(vport->dp);

		return ret;
	} else
		return -EOPNOTSUPP;
}

/**
 *	vport_set_addr - set device Ethernet address (for kernel callers)
 *
 * @vport: vport on which to set Ethernet address.
 * @addr: New address.
 *
 * Sets the Ethernet address of the given device.  Some devices may not support
 * setting the Ethernet address, in which case the result will always be
 * -EOPNOTSUPP.  RTNL lock must be held.
 */
int vport_set_addr(struct vport *vport, const unsigned char *addr)
{
	ASSERT_RTNL();

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	if (vport->ops->set_addr)
		return vport->ops->set_addr(vport, addr);
	else
		return -EOPNOTSUPP;
}

/**
 *	vport_set_stats - sets offset device stats (for kernel callers)
 *
 * @vport: vport on which to set stats
 * @stats: stats to set
 *
 * Provides a set of transmit, receive, and error stats to be added as an
 * offset to the collect data when stats are retreived.  Some devices may not
 * support setting the stats, in which case the result will always be
 * -EOPNOTSUPP.  RTNL lock must be held.
 */
int vport_set_stats(struct vport *vport, struct rtnl_link_stats64 *stats)
{
	ASSERT_RTNL();

	if (vport->ops->flags & VPORT_F_GEN_STATS) {
		spin_lock_bh(&vport->stats_lock);
		vport->offset_stats = *stats;
		spin_unlock_bh(&vport->stats_lock);

		return 0;
	} else if (vport->ops->set_stats)
		return vport->ops->set_stats(vport, stats);
	else
		return -EOPNOTSUPP;
}

/**
 *	vport_get_name - retrieve device name
 *
 * @vport: vport from which to retrieve the name.
 *
 * Retrieves the name of the given device.  Either RTNL lock or rcu_read_lock
 * must be held for the entire duration that the name is in use.
 */
const char *vport_get_name(const struct vport *vport)
{
	return vport->ops->get_name(vport);
}

/**
 *	vport_get_type - retrieve device type
 *
 * @vport: vport from which to retrieve the type.
 *
 * Retrieves the type of the given device.  Either RTNL lock or rcu_read_lock
 * must be held for the entire duration that the type is in use.
 */
const char *vport_get_type(const struct vport *vport)
{
	return vport->ops->type;
}

/**
 *	vport_get_addr - retrieve device Ethernet address (for kernel callers)
 *
 * @vport: vport from which to retrieve the Ethernet address.
 *
 * Retrieves the Ethernet address of the given device.  Either RTNL lock or
 * rcu_read_lock must be held for the entire duration that the Ethernet address
 * is in use.
 */
const unsigned char *vport_get_addr(const struct vport *vport)
{
	return vport->ops->get_addr(vport);
}

/**
 *	vport_get_kobj - retrieve associated kobj
 *
 * @vport: vport from which to retrieve the associated kobj
 *
 * Retrieves the associated kobj or null if no kobj.  The returned kobj is
 * valid for as long as the vport exists.
 */
struct kobject *vport_get_kobj(const struct vport *vport)
{
	if (vport->ops->get_kobj)
		return vport->ops->get_kobj(vport);
	else
		return NULL;
}

/**
 *	vport_get_stats - retrieve device stats (for kernel callers)
 *
 * @vport: vport from which to retrieve the stats
 * @stats: location to store stats
 *
 * Retrieves transmit, receive, and error stats for the given device.
 */
int vport_get_stats(struct vport *vport, struct rtnl_link_stats64 *stats)
{
	struct rtnl_link_stats64 dev_stats;
	struct rtnl_link_stats64 *dev_statsp = NULL;
	int err;

	if (vport->ops->get_stats) {
		if (vport->ops->flags & VPORT_F_GEN_STATS)
			dev_statsp = &dev_stats;
		else
			dev_statsp = stats;

		rcu_read_lock();
		err = vport->ops->get_stats(vport, dev_statsp);
		rcu_read_unlock();

		if (err)
			goto out;
	}

	if (vport->ops->flags & VPORT_F_GEN_STATS) {
		int i;

		/* We potentially have 3 sources of stats that need to be
		 * combined: those we have collected (split into err_stats and
		 * percpu_stats), offset_stats from set_stats(), and device
		 * error stats from get_stats() (for errors that happen
		 * downstream and therefore aren't reported through our
		 * vport_record_error() function). */

		spin_lock_bh(&vport->stats_lock);

		*stats = vport->offset_stats;

		stats->rx_errors	+= vport->err_stats.rx_errors;
		stats->tx_errors	+= vport->err_stats.tx_errors;
		stats->tx_dropped	+= vport->err_stats.tx_dropped;
		stats->rx_dropped	+= vport->err_stats.rx_dropped;

		spin_unlock_bh(&vport->stats_lock);

		if (dev_statsp) {
			stats->rx_errors           += dev_statsp->rx_errors;
			stats->tx_errors           += dev_statsp->tx_errors;
			stats->rx_dropped          += dev_statsp->rx_dropped;
			stats->tx_dropped          += dev_statsp->tx_dropped;
			stats->multicast           += dev_statsp->multicast;
			stats->collisions          += dev_statsp->collisions;
			stats->rx_length_errors    += dev_statsp->rx_length_errors;
			stats->rx_over_errors      += dev_statsp->rx_over_errors;
			stats->rx_crc_errors       += dev_statsp->rx_crc_errors;
			stats->rx_frame_errors     += dev_statsp->rx_frame_errors;
			stats->rx_fifo_errors      += dev_statsp->rx_fifo_errors;
			stats->rx_missed_errors    += dev_statsp->rx_missed_errors;
			stats->tx_aborted_errors   += dev_statsp->tx_aborted_errors;
			stats->tx_carrier_errors   += dev_statsp->tx_carrier_errors;
			stats->tx_fifo_errors      += dev_statsp->tx_fifo_errors;
			stats->tx_heartbeat_errors += dev_statsp->tx_heartbeat_errors;
			stats->tx_window_errors    += dev_statsp->tx_window_errors;
			stats->rx_compressed       += dev_statsp->rx_compressed;
			stats->tx_compressed       += dev_statsp->tx_compressed;
		}

		for_each_possible_cpu(i) {
			const struct vport_percpu_stats *percpu_stats;
			struct vport_percpu_stats local_stats;
			unsigned seqcount;

			percpu_stats = per_cpu_ptr(vport->percpu_stats, i);

			do {
				seqcount = read_seqcount_begin(&percpu_stats->seqlock);
				local_stats = *percpu_stats;
			} while (read_seqcount_retry(&percpu_stats->seqlock, seqcount));

			stats->rx_bytes		+= local_stats.rx_bytes;
			stats->rx_packets	+= local_stats.rx_packets;
			stats->tx_bytes		+= local_stats.tx_bytes;
			stats->tx_packets	+= local_stats.tx_packets;
		}

		err = 0;
	} else
		err = -EOPNOTSUPP;

out:
	return err;
}

/**
 *	vport_get_flags - retrieve device flags
 *
 * @vport: vport from which to retrieve the flags
 *
 * Retrieves the flags of the given device.  Either RTNL lock or rcu_read_lock
 * must be held.
 */
unsigned vport_get_flags(const struct vport *vport)
{
	return vport->ops->get_dev_flags(vport);
}

/**
 *	vport_get_flags - check whether device is running
 *
 * @vport: vport on which to check status.
 *
 * Checks whether the given device is running.  Either RTNL lock or
 * rcu_read_lock must be held.
 */
int vport_is_running(const struct vport *vport)
{
	return vport->ops->is_running(vport);
}

/**
 *	vport_get_flags - retrieve device operating state
 *
 * @vport: vport from which to check status
 *
 * Retrieves the RFC2863 operstate of the given device.  Either RTNL lock or
 * rcu_read_lock must be held.
 */
unsigned char vport_get_operstate(const struct vport *vport)
{
	return vport->ops->get_operstate(vport);
}

/**
 *	vport_get_ifindex - retrieve device system interface index
 *
 * @vport: vport from which to retrieve index
 *
 * Retrieves the system interface index of the given device or 0 if
 * the device does not have one (in the case of virtual ports).
 * Returns a negative index on error. Either RTNL lock or
 * rcu_read_lock must be held.
 */
int vport_get_ifindex(const struct vport *vport)
{
	if (vport->ops->get_ifindex)
		return vport->ops->get_ifindex(vport);
	else
		return 0;
}

/**
 *	vport_get_iflink - retrieve device system link index
 *
 * @vport: vport from which to retrieve index
 *
 * Retrieves the system link index of the given device.  The link is the index
 * of the interface on which the packet will actually be sent.  In most cases
 * this is the same as the ifindex but may be different for tunnel devices.
 * Returns a negative index on error.  Either RTNL lock or rcu_read_lock must
 * be held.
 */
int vport_get_iflink(const struct vport *vport)
{
	if (vport->ops->get_iflink)
		return vport->ops->get_iflink(vport);

	/* If we don't have an iflink, use the ifindex.  In most cases they
	 * are the same. */
	return vport_get_ifindex(vport);
}

/**
 *	vport_get_mtu - retrieve device MTU (for kernel callers)
 *
 * @vport: vport from which to retrieve MTU
 *
 * Retrieves the MTU of the given device.  Either RTNL lock or rcu_read_lock
 * must be held.
 */
int vport_get_mtu(const struct vport *vport)
{
	return vport->ops->get_mtu(vport);
}

/**
 *	vport_get_config - retrieve device configuration
 *
 * @vport: vport from which to retrieve the configuration.
 * @config: buffer to store config, which must be at least the length
 *          of VPORT_CONFIG_SIZE.
 *
 * Retrieves the configuration of the given device.  Either RTNL lock or
 * rcu_read_lock must be held.
 */
void vport_get_config(const struct vport *vport, void *config)
{
	if (vport->ops->get_config)
		vport->ops->get_config(vport, config);
}

/**
 *	vport_receive - pass up received packet to the datapath for processing
 *
 * @vport: vport that received the packet
 * @skb: skb that was received
 *
 * Must be called with rcu_read_lock.  The packet cannot be shared and
 * skb->data should point to the Ethernet header.  The caller must have already
 * called compute_ip_summed() to initialize the checksumming fields.
 */
void vport_receive(struct vport *vport, struct sk_buff *skb)
{
	if (vport->ops->flags & VPORT_F_GEN_STATS) {
		struct vport_percpu_stats *stats;

		local_bh_disable();
		stats = per_cpu_ptr(vport->percpu_stats, smp_processor_id());

		write_seqcount_begin(&stats->seqlock);
		stats->rx_packets++;
		stats->rx_bytes += skb->len;
		write_seqcount_end(&stats->seqlock);

		local_bh_enable();
	}

	if (!(vport->ops->flags & VPORT_F_FLOW))
		OVS_CB(skb)->flow = NULL;

	if (!(vport->ops->flags & VPORT_F_TUN_ID))
		OVS_CB(skb)->tun_id = 0;

	dp_process_received_packet(vport, skb);
}

static inline unsigned packet_length(const struct sk_buff *skb)
{
	unsigned length = skb->len - ETH_HLEN;

	if (skb->protocol == htons(ETH_P_8021Q))
		length -= VLAN_HLEN;

	return length;
}

/**
 *	vport_send - send a packet on a device
 *
 * @vport: vport on which to send the packet
 * @skb: skb to send
 *
 * Sends the given packet and returns the length of data sent.  Either RTNL
 * lock or rcu_read_lock must be held.
 */
int vport_send(struct vport *vport, struct sk_buff *skb)
{
	int mtu;
	int sent;

	mtu = vport_get_mtu(vport);
	if (unlikely(packet_length(skb) > mtu && !skb_is_gso(skb))) {
		if (net_ratelimit())
			pr_warn("%s: dropped over-mtu packet: %d > %d\n",
				dp_name(vport->dp), packet_length(skb), mtu);
		goto error;
	}

	sent = vport->ops->send(vport, skb);

	if (vport->ops->flags & VPORT_F_GEN_STATS && sent > 0) {
		struct vport_percpu_stats *stats;

		local_bh_disable();
		stats = per_cpu_ptr(vport->percpu_stats, smp_processor_id());

		write_seqcount_begin(&stats->seqlock);
		stats->tx_packets++;
		stats->tx_bytes += sent;
		write_seqcount_end(&stats->seqlock);

		local_bh_enable();
	}

	return sent;

error:
	kfree_skb(skb);
	vport_record_error(vport, VPORT_E_TX_DROPPED);
	return 0;
}

/**
 *	vport_record_error - indicate device error to generic stats layer
 *
 * @vport: vport that encountered the error
 * @err_type: one of enum vport_err_type types to indicate the error type
 *
 * If using the vport generic stats layer indicate that an error of the given
 * type has occured.
 */
void vport_record_error(struct vport *vport, enum vport_err_type err_type)
{
	if (vport->ops->flags & VPORT_F_GEN_STATS) {

		spin_lock_bh(&vport->stats_lock);

		switch (err_type) {
		case VPORT_E_RX_DROPPED:
			vport->err_stats.rx_dropped++;
			break;

		case VPORT_E_RX_ERROR:
			vport->err_stats.rx_errors++;
			break;

		case VPORT_E_TX_DROPPED:
			vport->err_stats.tx_dropped++;
			break;

		case VPORT_E_TX_ERROR:
			vport->err_stats.tx_errors++;
			break;
		};

		spin_unlock_bh(&vport->stats_lock);
	}
}
