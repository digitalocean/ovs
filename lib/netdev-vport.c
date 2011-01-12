/*
 * Copyright (c) 2010 Nicira Networks.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "netdev-vport.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "byte-order.h"
#include "hash.h"
#include "hmap.h"
#include "list.h"
#include "netdev-provider.h"
#include "netlink.h"
#include "netlink-socket.h"
#include "ofpbuf.h"
#include "openvswitch/datapath-protocol.h"
#include "openvswitch/tunnel.h"
#include "packets.h"
#include "rtnetlink.h"
#include "rtnetlink-route.h"
#include "rtnetlink-link.h"
#include "shash.h"
#include "socket-util.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(netdev_vport);

static struct hmap name_map;
static struct hmap route_map;
static struct rtnetlink_notifier netdev_vport_link_notifier;
static struct rtnetlink_notifier netdev_vport_route_notifier;

struct route_node {
    struct hmap_node node;      /* Node in route_map. */
    int rta_oif;                /* Egress interface index. */
    uint32_t rta_dst;           /* Destination address in host byte order. */
    unsigned char rtm_dst_len;  /* Destination address length. */
};

struct name_node {
    struct hmap_node node; /* Node in name_map. */
    uint32_t ifi_index;    /* Kernel interface index. */

    char ifname[IFNAMSIZ]; /* Interface name. */
};

struct netdev_vport_notifier {
    struct netdev_notifier notifier;
    struct list list_node;
    struct shash_node *shash_node;
};

struct netdev_dev_vport {
    struct netdev_dev netdev_dev;
    uint64_t config[VPORT_CONFIG_SIZE / 8];
};

struct netdev_vport {
    struct netdev netdev;
};

struct vport_class {
    struct netdev_class netdev_class;
    int (*parse_config)(const struct netdev_dev *, const struct shash *args,
                        void *config);
};

static struct shash netdev_vport_notifiers =
                                    SHASH_INITIALIZER(&netdev_vport_notifiers);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static int netdev_vport_do_ioctl(int cmd, void *arg);
static int netdev_vport_create(const struct netdev_class *, const char *,
                               const struct shash *, struct netdev_dev **);
static void netdev_vport_poll_notify(const struct netdev *);

static void netdev_vport_tnl_iface_init(void);
static void netdev_vport_route_change(const struct rtnetlink_route_change *,
                                      void *);
static void netdev_vport_link_change(const struct rtnetlink_link_change *,
                                     void *);
static const char *netdev_vport_get_tnl_iface(const struct netdev *netdev);

static bool
is_vport_class(const struct netdev_class *class)
{
    return class->create == netdev_vport_create;
}

static const struct vport_class *
vport_class_cast(const struct netdev_class *class)
{
    assert(is_vport_class(class));
    return CONTAINER_OF(class, struct vport_class, netdev_class);
}

static struct netdev_dev_vport *
netdev_dev_vport_cast(const struct netdev_dev *netdev_dev)
{
    assert(is_vport_class(netdev_dev_get_class(netdev_dev)));
    return CONTAINER_OF(netdev_dev, struct netdev_dev_vport, netdev_dev);
}

static struct netdev_vport *
netdev_vport_cast(const struct netdev *netdev)
{
    struct netdev_dev *netdev_dev = netdev_get_dev(netdev);
    assert(is_vport_class(netdev_dev_get_class(netdev_dev)));
    return CONTAINER_OF(netdev, struct netdev_vport, netdev);
}

/* If 'netdev' is a vport netdev, copies its kernel configuration into
 * 'config'.  Otherwise leaves 'config' untouched. */
void
netdev_vport_get_config(const struct netdev *netdev, void *config)
{
    const struct netdev_dev *dev = netdev_get_dev(netdev);

    if (is_vport_class(netdev_dev_get_class(dev))) {
        const struct netdev_dev_vport *vport = netdev_dev_vport_cast(dev);
        memcpy(config, vport->config, VPORT_CONFIG_SIZE);
    }
}

static int
netdev_vport_init(void)
{
    netdev_vport_tnl_iface_init();
    return 0;
}

static int
netdev_vport_create(const struct netdev_class *netdev_class, const char *name,
                    const struct shash *args,
                    struct netdev_dev **netdev_devp)
{
    const struct vport_class *vport_class = vport_class_cast(netdev_class);
    struct netdev_dev_vport *dev;
    int error;

    dev = xmalloc(sizeof *dev);
    *netdev_devp = &dev->netdev_dev;
    netdev_dev_init(&dev->netdev_dev, name, netdev_class);

    memset(dev->config, 0, sizeof dev->config);
    error = vport_class->parse_config(&dev->netdev_dev, args, dev->config);

    if (error) {
        netdev_dev_uninit(&dev->netdev_dev, true);
    }
    return error;
}

static void
netdev_vport_destroy(struct netdev_dev *netdev_dev_)
{
    struct netdev_dev_vport *netdev_dev = netdev_dev_vport_cast(netdev_dev_);

    free(netdev_dev);
}

static int
netdev_vport_open(struct netdev_dev *netdev_dev_, int ethertype OVS_UNUSED,
                struct netdev **netdevp)
{
    struct netdev_vport *netdev;

    netdev = xmalloc(sizeof *netdev);
    netdev_init(&netdev->netdev, netdev_dev_);

    *netdevp = &netdev->netdev;
    return 0;
}

static void
netdev_vport_close(struct netdev *netdev_)
{
    struct netdev_vport *netdev = netdev_vport_cast(netdev_);
    free(netdev);
}

static int
netdev_vport_reconfigure(struct netdev_dev *dev_,
                         const struct shash *args)
{
    const struct netdev_class *netdev_class = netdev_dev_get_class(dev_);
    const struct vport_class *vport_class = vport_class_cast(netdev_class);
    struct netdev_dev_vport *dev = netdev_dev_vport_cast(dev_);
    struct odp_port port;
    int error;

    memset(&port, 0, sizeof port);
    strncpy(port.devname, netdev_dev_get_name(dev_), sizeof port.devname);
    strncpy(port.type, netdev_dev_get_type(dev_), sizeof port.type);
    error = vport_class->parse_config(dev_, args, port.config);
    if (!error && memcmp(port.config, dev->config, sizeof dev->config)) {
        error = netdev_vport_do_ioctl(ODP_VPORT_MOD, &port);
        if (!error || error == ENODEV) {
            /* Either reconfiguration succeeded or this vport is not installed
             * in the kernel (e.g. it hasn't been added to a dpif yet with
             * dpif_port_add()). */
            memcpy(dev->config, port.config, sizeof dev->config);
        }
    }
    return error;
}

static int
netdev_vport_set_etheraddr(struct netdev *netdev,
                           const uint8_t mac[ETH_ADDR_LEN])
{
    struct odp_vport_ether vport_ether;
    int err;

    ovs_strlcpy(vport_ether.devname, netdev_get_name(netdev),
                sizeof vport_ether.devname);

    memcpy(vport_ether.ether_addr, mac, ETH_ADDR_LEN);

    err = netdev_vport_do_ioctl(ODP_VPORT_ETHER_SET, &vport_ether);
    if (err) {
        return err;
    }

    netdev_vport_poll_notify(netdev);
    return 0;
}

static int
netdev_vport_get_etheraddr(const struct netdev *netdev,
                           uint8_t mac[ETH_ADDR_LEN])
{
    struct odp_vport_ether vport_ether;
    int err;

    ovs_strlcpy(vport_ether.devname, netdev_get_name(netdev),
                sizeof vport_ether.devname);

    err = netdev_vport_do_ioctl(ODP_VPORT_ETHER_GET, &vport_ether);
    if (err) {
        return err;
    }

    memcpy(mac, vport_ether.ether_addr, ETH_ADDR_LEN);
    return 0;
}

static int
netdev_vport_get_mtu(const struct netdev *netdev, int *mtup)
{
    struct odp_vport_mtu vport_mtu;
    int err;

    ovs_strlcpy(vport_mtu.devname, netdev_get_name(netdev),
                sizeof vport_mtu.devname);

    err = netdev_vport_do_ioctl(ODP_VPORT_MTU_GET, &vport_mtu);
    if (err) {
        return err;
    }

    *mtup = vport_mtu.mtu;
    return 0;
}

int
netdev_vport_get_stats(const struct netdev *netdev, struct netdev_stats *stats)
{
    const char *name = netdev_get_name(netdev);
    struct odp_vport_stats_req ovsr;
    int err;

    ovs_strlcpy(ovsr.devname, name, sizeof ovsr.devname);
    err = netdev_vport_do_ioctl(ODP_VPORT_STATS_GET, &ovsr);
    if (err) {
        return err;
    }

    stats->rx_packets = ovsr.stats.rx_packets;
    stats->tx_packets = ovsr.stats.tx_packets;
    stats->rx_bytes = ovsr.stats.rx_bytes;
    stats->tx_bytes = ovsr.stats.tx_bytes;
    stats->rx_errors = ovsr.stats.rx_errors;
    stats->tx_errors = ovsr.stats.tx_errors;
    stats->rx_dropped = ovsr.stats.rx_dropped;
    stats->tx_dropped = ovsr.stats.tx_dropped;
    stats->multicast = ovsr.stats.multicast;
    stats->collisions = ovsr.stats.collisions;
    stats->rx_length_errors = ovsr.stats.rx_length_errors;
    stats->rx_over_errors = ovsr.stats.rx_over_errors;
    stats->rx_crc_errors = ovsr.stats.rx_crc_errors;
    stats->rx_frame_errors = ovsr.stats.rx_frame_errors;
    stats->rx_fifo_errors = ovsr.stats.rx_fifo_errors;
    stats->rx_missed_errors = ovsr.stats.rx_missed_errors;
    stats->tx_aborted_errors = ovsr.stats.tx_aborted_errors;
    stats->tx_carrier_errors = ovsr.stats.tx_carrier_errors;
    stats->tx_fifo_errors = ovsr.stats.tx_fifo_errors;
    stats->tx_heartbeat_errors = ovsr.stats.tx_heartbeat_errors;
    stats->tx_window_errors = ovsr.stats.tx_window_errors;

    return 0;
}

int
netdev_vport_set_stats(struct netdev *netdev, const struct netdev_stats *stats)
{
    struct odp_vport_stats_req ovsr;
    int err;

    ovs_strlcpy(ovsr.devname, netdev_get_name(netdev), sizeof ovsr.devname);

    ovsr.stats.rx_packets = stats->rx_packets;
    ovsr.stats.tx_packets = stats->tx_packets;
    ovsr.stats.rx_bytes = stats->rx_bytes;
    ovsr.stats.tx_bytes = stats->tx_bytes;
    ovsr.stats.rx_errors = stats->rx_errors;
    ovsr.stats.tx_errors = stats->tx_errors;
    ovsr.stats.rx_dropped = stats->rx_dropped;
    ovsr.stats.tx_dropped = stats->tx_dropped;
    ovsr.stats.multicast = stats->multicast;
    ovsr.stats.collisions = stats->collisions;
    ovsr.stats.rx_length_errors = stats->rx_length_errors;
    ovsr.stats.rx_over_errors = stats->rx_over_errors;
    ovsr.stats.rx_crc_errors = stats->rx_crc_errors;
    ovsr.stats.rx_frame_errors = stats->rx_frame_errors;
    ovsr.stats.rx_fifo_errors = stats->rx_fifo_errors;
    ovsr.stats.rx_missed_errors = stats->rx_missed_errors;
    ovsr.stats.tx_aborted_errors = stats->tx_aborted_errors;
    ovsr.stats.tx_carrier_errors = stats->tx_carrier_errors;
    ovsr.stats.tx_fifo_errors = stats->tx_fifo_errors;
    ovsr.stats.tx_heartbeat_errors = stats->tx_heartbeat_errors;
    ovsr.stats.tx_window_errors = stats->tx_window_errors;

    err = netdev_vport_do_ioctl(ODP_VPORT_STATS_SET, &ovsr);

    /* If the vport layer doesn't know about the device, that doesn't mean it
     * doesn't exist (after all were able to open it when netdev_open() was
     * called), it just means that it isn't attached and we'll be getting
     * stats a different way. */
    if (err == ENODEV) {
        err = EOPNOTSUPP;
    }

    return err;
}

static int
netdev_vport_get_status(const struct netdev *netdev, struct shash *sh)
{
    const char *iface = netdev_vport_get_tnl_iface(netdev);

    if (iface) {
        shash_add(sh, "tunnel_egress_iface", xstrdup(iface));
    }

    return 0;
}

static int
netdev_vport_update_flags(struct netdev *netdev OVS_UNUSED,
                        enum netdev_flags off, enum netdev_flags on OVS_UNUSED,
                        enum netdev_flags *old_flagsp)
{
    if (off & (NETDEV_UP | NETDEV_PROMISC)) {
        return EOPNOTSUPP;
    }

    *old_flagsp = NETDEV_UP | NETDEV_PROMISC;
    return 0;
}

static char *
make_poll_name(const struct netdev *netdev)
{
    return xasprintf("%s:%s", netdev_get_type(netdev), netdev_get_name(netdev));
}

static int
netdev_vport_poll_add(struct netdev *netdev,
                      void (*cb)(struct netdev_notifier *), void *aux,
                      struct netdev_notifier **notifierp)
{
    char *poll_name = make_poll_name(netdev);
    struct netdev_vport_notifier *notifier;
    struct list *list;
    struct shash_node *shash_node;

    shash_node = shash_find_data(&netdev_vport_notifiers, poll_name);
    if (!shash_node) {
        list = xmalloc(sizeof *list);
        list_init(list);
        shash_node = shash_add(&netdev_vport_notifiers, poll_name, list);
    } else {
        list = shash_node->data;
    }

    notifier = xmalloc(sizeof *notifier);
    netdev_notifier_init(&notifier->notifier, netdev, cb, aux);
    list_push_back(list, &notifier->list_node);
    notifier->shash_node = shash_node;

    *notifierp = &notifier->notifier;
    free(poll_name);

    return 0;
}

static void
netdev_vport_poll_remove(struct netdev_notifier *notifier_)
{
    struct netdev_vport_notifier *notifier =
                CONTAINER_OF(notifier_, struct netdev_vport_notifier, notifier);

    struct list *list;

    list = list_remove(&notifier->list_node);
    if (list_is_empty(list)) {
        shash_delete(&netdev_vport_notifiers, notifier->shash_node);
        free(list);
    }

    free(notifier);
}

static void
netdev_vport_run(void)
{
    rtnetlink_link_notifier_run();
    rtnetlink_route_notifier_run();
}

static void
netdev_vport_wait(void)
{
    rtnetlink_link_notifier_wait();
    rtnetlink_route_notifier_wait();
}

/* get_tnl_iface() implementation. */

static struct name_node *
name_node_lookup(int ifi_index)
{
    struct name_node *nn;

    HMAP_FOR_EACH_WITH_HASH(nn, node, hash_int(ifi_index, 0), &name_map) {
        if (nn->ifi_index == ifi_index) {
            return nn;
        }
    }

    return NULL;
}

static struct route_node *
route_node_lookup(int rta_oif, uint32_t rta_dst, unsigned char rtm_dst_len)
{
    uint32_t hash;
    struct route_node *rn;

    hash = hash_3words(rta_oif, rta_dst, rtm_dst_len);
    HMAP_FOR_EACH_WITH_HASH(rn, node, hash, &route_map) {
        if (rn->rta_oif     == rn->rta_oif &&
            rn->rta_dst     == rn->rta_dst &&
            rn->rtm_dst_len == rn->rtm_dst_len) {
            return rn;
        }
    }

    return NULL;
}

/* Resets the name or route map depending on the value of 'is_name'.  Clears
 * the appropriate map, makes an rtnetlink dump request, and calls the change
 * callback for each reply from the kernel. One should probably use
 * netdev_vport_reset_routes or netdev_vport_reset_names instead. */
static int
netdev_vport_reset_name_else_route(bool is_name)
{
    int error;
    int nlmsg_type;
    struct nl_dump dump;
    struct rtgenmsg *rtmsg;
    struct ofpbuf request, reply;
    static struct nl_sock *rtnl_sock;

    if (is_name) {
        struct name_node *nn, *nn_next;

        HMAP_FOR_EACH_SAFE(nn, nn_next, node, &name_map) {
            hmap_remove(&name_map, &nn->node);
            free(nn);
        }
    } else {
        struct route_node *rn, *rn_next;

        HMAP_FOR_EACH_SAFE(rn, rn_next, node, &route_map) {
            hmap_remove(&route_map, &rn->node);
            free(rn);
        }
    }

    error = nl_sock_create(NETLINK_ROUTE, 0, 0, 0, &rtnl_sock);
    if (error) {
        VLOG_WARN_RL(&rl, "Failed to create NETLINK_ROUTE socket");
        return error;
    }

    ofpbuf_init(&request, 0);

    nlmsg_type = is_name ? RTM_GETLINK : RTM_GETROUTE;
    nl_msg_put_nlmsghdr(&request, sizeof *rtmsg, nlmsg_type, NLM_F_REQUEST);

    rtmsg = ofpbuf_put_zeros(&request, sizeof *rtmsg);
    rtmsg->rtgen_family = AF_INET;

    nl_dump_start(&dump, rtnl_sock, &request);

    while (nl_dump_next(&dump, &reply)) {
        if (is_name) {
            struct rtnetlink_link_change change;

            if (rtnetlink_link_parse(&reply, &change)) {
                netdev_vport_link_change(&change, NULL);
            }
        } else {
            struct rtnetlink_route_change change;

            if (rtnetlink_route_parse(&reply, &change)) {
                netdev_vport_route_change(&change, NULL);
            }
        }
    }

    error = nl_dump_done(&dump);
    nl_sock_destroy(rtnl_sock);

    return error;
}

static int
netdev_vport_reset_routes(void)
{
    return netdev_vport_reset_name_else_route(false);
}

static int
netdev_vport_reset_names(void)
{
    return netdev_vport_reset_name_else_route(true);
}

static void
netdev_vport_route_change(const struct rtnetlink_route_change *change,
                         void *aux OVS_UNUSED)
{

    if (!change) {
        netdev_vport_reset_routes();
    } else if (change->nlmsg_type == RTM_NEWROUTE) {
        uint32_t hash;
        struct route_node *rn;

        if (route_node_lookup(change->rta_oif, change->rta_dst,
                              change->rtm_dst_len)) {
            return;
        }

        rn              = xzalloc(sizeof *rn);
        rn->rta_oif     = change->rta_oif;
        rn->rta_dst     = change->rta_dst;
        rn->rtm_dst_len = change->rtm_dst_len;

        hash = hash_3words(rn->rta_oif, rn->rta_dst, rn->rtm_dst_len);
        hmap_insert(&route_map, &rn->node, hash);
    } else if (change->nlmsg_type == RTM_DELROUTE) {
        struct route_node *rn;

        rn = route_node_lookup(change->rta_oif, change->rta_dst,
                               change->rtm_dst_len);

        if (rn) {
            hmap_remove(&route_map, &rn->node);
            free(rn);
        }
    } else {
        VLOG_WARN_RL(&rl, "Received unexpected rtnetlink message type %d",
                     change->nlmsg_type);
    }
}

static void
netdev_vport_link_change(const struct rtnetlink_link_change *change,
                         void *aux OVS_UNUSED)
{

    if (!change) {
        netdev_vport_reset_names();
    } else if (change->nlmsg_type == RTM_NEWLINK) {
        struct name_node *nn;

        if (name_node_lookup(change->ifi_index)) {
            return;
        }

        nn            = xzalloc(sizeof *nn);
        nn->ifi_index = change->ifi_index;

        strncpy(nn->ifname, change->ifname, IFNAMSIZ);
        nn->ifname[IFNAMSIZ - 1] = '\0';

        hmap_insert(&name_map, &nn->node, hash_int(nn->ifi_index, 0));
    } else if (change->nlmsg_type == RTM_DELLINK) {
        struct name_node *nn;

        nn = name_node_lookup(change->ifi_index);

        if (nn) {
            hmap_remove(&name_map, &nn->node);
            free(nn);
        }

        /* Link deletions do not result in all of the RTM_DELROUTE messages one
         * would expect.  For now, go ahead and reset route_map whenever a link
         * is deleted. */
        netdev_vport_reset_routes();
    } else {
        VLOG_WARN_RL(&rl, "Received unexpected rtnetlink message type %d",
                     change->nlmsg_type);
    }
}

static void
netdev_vport_tnl_iface_init(void)
{
    static bool tnl_iface_is_init = false;

    if (!tnl_iface_is_init) {
        hmap_init(&name_map);
        hmap_init(&route_map);

        rtnetlink_link_notifier_register(&netdev_vport_link_notifier,
                                          netdev_vport_link_change, NULL);

        rtnetlink_route_notifier_register(&netdev_vport_route_notifier,
                                          netdev_vport_route_change, NULL);

        netdev_vport_reset_names();
        netdev_vport_reset_routes();
        tnl_iface_is_init = true;
    }
}

static const char *
netdev_vport_get_tnl_iface(const struct netdev *netdev)
{
    int dst_len;
    uint32_t route;
    struct netdev_dev_vport *ndv;
    struct tnl_port_config *config;
    struct route_node *rn, *rn_def, *rn_iter;

    ndv = netdev_dev_vport_cast(netdev_get_dev(netdev));
    config = (struct tnl_port_config *) ndv->config;
    route = ntohl(config->daddr);

    dst_len = 0;
    rn      = NULL;
    rn_def  = NULL;

    HMAP_FOR_EACH(rn_iter, node, &route_map) {
        if (rn_iter->rtm_dst_len == 0 && rn_iter->rta_dst == 0) {
            /* Default route. */
            rn_def = rn_iter;
        } else if (rn_iter->rtm_dst_len > dst_len) {
            uint32_t mask = 0xffffffff << (32 - rn_iter->rtm_dst_len);
            if ((route & mask) == (rn_iter->rta_dst & mask)) {
                rn      = rn_iter;
                dst_len = rn_iter->rtm_dst_len;
            }
        }
    }

    if (!rn) {
        rn = rn_def;
    }

    if (rn) {
        uint32_t hash;
        struct name_node *nn;

        hash = hash_int(rn->rta_oif, 0);
        HMAP_FOR_EACH_WITH_HASH(nn, node, hash, &name_map) {
            if (nn->ifi_index == rn->rta_oif) {
                return nn->ifname;
            }
        }
    }

    return NULL;
}

/* Helper functions. */

static int
netdev_vport_do_ioctl(int cmd, void *arg)
{
    static int ioctl_fd = -1;

    if (ioctl_fd < 0) {
        ioctl_fd = open("/dev/net/dp0", O_RDONLY | O_NONBLOCK);
        if (ioctl_fd < 0) {
            VLOG_ERR_RL(&rl, "failed to open ioctl fd: %s", strerror(errno));
            return errno;
        }
    }

    return ioctl(ioctl_fd, cmd, arg) ? errno : 0;
}

static void
netdev_vport_poll_notify(const struct netdev *netdev)
{
    char *poll_name = make_poll_name(netdev);
    struct list *list = shash_find_data(&netdev_vport_notifiers,
                                        poll_name);

    if (list) {
        struct netdev_vport_notifier *notifier;

        LIST_FOR_EACH (notifier, list_node, list) {
            struct netdev_notifier *n = &notifier->notifier;
            n->cb(n);
        }
    }

    free(poll_name);
}

/* Code specific to individual vport types. */

static int
parse_tunnel_config(const struct netdev_dev *dev, const struct shash *args,
                    void *configp)
{
    const char *name = netdev_dev_get_name(dev);
    const char *type = netdev_dev_get_type(dev);
    bool is_gre = false;
    bool is_ipsec = false;
    struct tnl_port_config config;
    struct shash_node *node;
    bool ipsec_mech_set = false;

    memset(&config, 0, sizeof config);
    config.flags |= TNL_F_PMTUD;
    config.flags |= TNL_F_HDR_CACHE;

    if (!strcmp(type, "gre")) {
        is_gre = true;
    } else if (!strcmp(type, "ipsec_gre")) {
        is_gre = true;
        is_ipsec = true;

        config.flags |= TNL_F_IPSEC;

        /* IPsec doesn't work when header caching is enabled. */
        config.flags &= ~TNL_F_HDR_CACHE;
    }

    SHASH_FOR_EACH (node, args) {
        if (!strcmp(node->name, "remote_ip")) {
            struct in_addr in_addr;
            if (lookup_ip(node->data, &in_addr)) {
                VLOG_WARN("%s: bad %s 'remote_ip'", name, type);
            } else {
                config.daddr = in_addr.s_addr;
            }
        } else if (!strcmp(node->name, "local_ip")) {
            struct in_addr in_addr;
            if (lookup_ip(node->data, &in_addr)) {
                VLOG_WARN("%s: bad %s 'local_ip'", name, type);
            } else {
                config.saddr = in_addr.s_addr;
            }
        } else if (!strcmp(node->name, "key") && is_gre) {
            if (!strcmp(node->data, "flow")) {
                config.flags |= TNL_F_IN_KEY_MATCH;
                config.flags |= TNL_F_OUT_KEY_ACTION;
            } else {
                uint64_t key = strtoull(node->data, NULL, 0);
                config.out_key = config.in_key = htonll(key);
            }
        } else if (!strcmp(node->name, "in_key") && is_gre) {
            if (!strcmp(node->data, "flow")) {
                config.flags |= TNL_F_IN_KEY_MATCH;
            } else {
                config.in_key = htonll(strtoull(node->data, NULL, 0));
            }
        } else if (!strcmp(node->name, "out_key") && is_gre) {
            if (!strcmp(node->data, "flow")) {
                config.flags |= TNL_F_OUT_KEY_ACTION;
            } else {
                config.out_key = htonll(strtoull(node->data, NULL, 0));
            }
        } else if (!strcmp(node->name, "tos")) {
            if (!strcmp(node->data, "inherit")) {
                config.flags |= TNL_F_TOS_INHERIT;
            } else {
                config.tos = atoi(node->data);
            }
        } else if (!strcmp(node->name, "ttl")) {
            if (!strcmp(node->data, "inherit")) {
                config.flags |= TNL_F_TTL_INHERIT;
            } else {
                config.ttl = atoi(node->data);
            }
        } else if (!strcmp(node->name, "csum") && is_gre) {
            if (!strcmp(node->data, "true")) {
                config.flags |= TNL_F_CSUM;
            }
        } else if (!strcmp(node->name, "pmtud")) {
            if (!strcmp(node->data, "false")) {
                config.flags &= ~TNL_F_PMTUD;
            }
        } else if (!strcmp(node->name, "header_cache")) {
            if (!strcmp(node->data, "false")) {
                config.flags &= ~TNL_F_HDR_CACHE;
            }
        } else if (!strcmp(node->name, "peer_cert") && is_ipsec) {
            if (shash_find(args, "certificate")) {
                ipsec_mech_set = true;
            } else {
                const char *use_ssl_cert;

                /* If the "use_ssl_cert" is true, then "certificate" and
                 * "private_key" will be pulled from the SSL table.  The
                 * use of this option is strongly discouraged, since it
                 * will like be removed when multiple SSL configurations
                 * are supported by OVS.
                 */
                use_ssl_cert = shash_find_data(args, "use_ssl_cert");
                if (!use_ssl_cert || strcmp(use_ssl_cert, "true")) {
                    VLOG_WARN("%s: 'peer_cert' requires 'certificate' argument",
                              name);
                    return EINVAL;
                }
                ipsec_mech_set = true;
            }
        } else if (!strcmp(node->name, "psk") && is_ipsec) {
            ipsec_mech_set = true;
        } else if (is_ipsec
                && (!strcmp(node->name, "certificate")
                    || !strcmp(node->name, "private_key")
                    || !strcmp(node->name, "use_ssl_cert"))) {
            /* Ignore options not used by the netdev. */
        } else {
            VLOG_WARN("%s: unknown %s argument '%s'",
                      name, type, node->name);
        }
    }

    if (is_ipsec) {
        if (shash_find(args, "peer_cert") && shash_find(args, "psk")) {
            VLOG_WARN("%s: cannot define both 'peer_cert' and 'psk'", name);
            return EINVAL;
        }

        if (!ipsec_mech_set) {
            VLOG_WARN("%s: IPsec requires an 'peer_cert' or psk' argument",
                      name);
            return EINVAL;
        }
    }

    if (!config.daddr) {
        VLOG_WARN("%s: %s type requires valid 'remote_ip' argument",
                  name, type);
        return EINVAL;
    }

    BUILD_ASSERT(sizeof config <= VPORT_CONFIG_SIZE);
    memcpy(configp, &config, sizeof config);
    return 0;
}

static int
parse_patch_config(const struct netdev_dev *dev, const struct shash *args,
                   void *configp)
{
    const char *name = netdev_dev_get_name(dev);
    const char *peer;

    peer = shash_find_data(args, "peer");
    if (!peer) {
        VLOG_WARN("%s: patch type requires valid 'peer' argument", name);
        return EINVAL;
    }

    if (shash_count(args) > 1) {
        VLOG_WARN("%s: patch type takes only a 'peer' argument", name);
        return EINVAL;
    }

    if (strlen(peer) >= MIN(IFNAMSIZ, VPORT_CONFIG_SIZE)) {
        VLOG_WARN("%s: patch 'peer' arg too long", name);
        return EINVAL;
    }

    if (!strcmp(name, peer)) {
        VLOG_WARN("%s: patch peer must not be self", name);
        return EINVAL;
    }

    strncpy(configp, peer, VPORT_CONFIG_SIZE);

    return 0;
}

#define VPORT_FUNCTIONS(GET_STATUS)                         \
    netdev_vport_init,                                      \
    netdev_vport_run,                                       \
    netdev_vport_wait,                                      \
                                                            \
    netdev_vport_create,                                    \
    netdev_vport_destroy,                                   \
    netdev_vport_reconfigure,                               \
                                                            \
    netdev_vport_open,                                      \
    netdev_vport_close,                                     \
                                                            \
    NULL,                       /* enumerate */             \
                                                            \
    NULL,                       /* recv */                  \
    NULL,                       /* recv_wait */             \
    NULL,                       /* drain */                 \
                                                            \
    NULL,                       /* send */                  \
    NULL,                       /* send_wait */             \
                                                            \
    netdev_vport_set_etheraddr,                             \
    netdev_vport_get_etheraddr,                             \
    netdev_vport_get_mtu,                                   \
    NULL,                       /* get_ifindex */           \
    NULL,                       /* get_carrier */           \
    netdev_vport_get_stats,                                 \
    netdev_vport_set_stats,                                 \
                                                            \
    NULL,                       /* get_features */          \
    NULL,                       /* set_advertisements */    \
    NULL,                       /* get_vlan_vid */          \
                                                            \
    NULL,                       /* set_policing */          \
    NULL,                       /* get_qos_types */         \
    NULL,                       /* get_qos_capabilities */  \
    NULL,                       /* get_qos */               \
    NULL,                       /* set_qos */               \
    NULL,                       /* get_queue */             \
    NULL,                       /* set_queue */             \
    NULL,                       /* delete_queue */          \
    NULL,                       /* get_queue_stats */       \
    NULL,                       /* dump_queues */           \
    NULL,                       /* dump_queue_stats */      \
                                                            \
    NULL,                       /* get_in4 */               \
    NULL,                       /* set_in4 */               \
    NULL,                       /* get_in6 */               \
    NULL,                       /* add_router */            \
    NULL,                       /* get_next_hop */          \
    GET_STATUS,                                             \
    NULL,                       /* arp_lookup */            \
                                                            \
    netdev_vport_update_flags,                              \
                                                            \
    netdev_vport_poll_add,                                  \
    netdev_vport_poll_remove,

void
netdev_vport_register(void)
{
    static const struct vport_class vport_classes[] = {
        { { "gre", VPORT_FUNCTIONS(netdev_vport_get_status) },
            parse_tunnel_config },
        { { "ipsec_gre", VPORT_FUNCTIONS(netdev_vport_get_status) },
            parse_tunnel_config },
        { { "capwap", VPORT_FUNCTIONS(netdev_vport_get_status) },
            parse_tunnel_config },
        { { "patch", VPORT_FUNCTIONS(NULL) }, parse_patch_config }
    };

    int i;

    for (i = 0; i < ARRAY_SIZE(vport_classes); i++) {
        netdev_register_provider(&vport_classes[i].netdev_class);
    }
}
