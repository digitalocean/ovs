/* Copyright (c) 2008, 2009, 2010, 2011 Nicira Networks
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
#include "bridge.h"
#include "byte-order.h"
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <net/if.h>
#include <openflow/openflow.h>
#include <signal.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "bitmap.h"
#include "cfm.h"
#include "classifier.h"
#include "coverage.h"
#include "daemon.h"
#include "dirs.h"
#include "dpif.h"
#include "dynamic-string.h"
#include "flow.h"
#include "hash.h"
#include "hmap.h"
#include "jsonrpc.h"
#include "lacp.h"
#include "list.h"
#include "mac-learning.h"
#include "netdev.h"
#include "netlink.h"
#include "odp-util.h"
#include "ofp-print.h"
#include "ofpbuf.h"
#include "ofproto/netflow.h"
#include "ofproto/ofproto.h"
#include "ovsdb-data.h"
#include "packets.h"
#include "poll-loop.h"
#include "process.h"
#include "sha1.h"
#include "shash.h"
#include "socket-util.h"
#include "stream-ssl.h"
#include "sset.h"
#include "svec.h"
#include "system-stats.h"
#include "timeval.h"
#include "util.h"
#include "unixctl.h"
#include "vconn.h"
#include "vswitchd/vswitch-idl.h"
#include "xenserver.h"
#include "vlog.h"
#include "sflow_api.h"

VLOG_DEFINE_THIS_MODULE(bridge);

COVERAGE_DEFINE(bridge_flush);
COVERAGE_DEFINE(bridge_process_flow);
COVERAGE_DEFINE(bridge_process_cfm);
COVERAGE_DEFINE(bridge_process_lacp);
COVERAGE_DEFINE(bridge_reconfigure);
COVERAGE_DEFINE(bridge_lacp_update);

struct dst {
    uint16_t vlan;
    uint16_t dp_ifidx;
};

struct dst_set {
    struct dst builtin[32];
    struct dst *dsts;
    size_t n, allocated;
};

static void dst_set_init(struct dst_set *);
static void dst_set_add(struct dst_set *, const struct dst *);
static void dst_set_free(struct dst_set *);

struct iface {
    /* These members are always valid. */
    struct list port_elem;      /* Element in struct port's "ifaces" list. */
    struct port *port;          /* Containing port. */
    char *name;                 /* Host network device name. */
    tag_type tag;               /* Tag associated with this interface. */
    long long delay_expires;    /* Time after which 'enabled' may change. */

    /* These members are valid only after bridge_reconfigure() causes them to
     * be initialized. */
    struct hmap_node dp_ifidx_node; /* In struct bridge's "ifaces" hmap. */
    int dp_ifidx;               /* Index within kernel datapath. */
    struct netdev *netdev;      /* Network device. */
    bool enabled;               /* May be chosen for flows? */
    bool up;                    /* Is the interface up? */
    const char *type;           /* Usually same as cfg->type. */
    const struct ovsrec_interface *cfg;

    /* LACP information. */
    uint16_t lacp_priority;     /* LACP port priority. */
};

#define BOND_MASK 0xff
struct bond_entry {
    struct iface *iface;        /* Assigned iface, or NULL if none. */
    uint64_t tx_bytes;          /* Count of bytes recently transmitted. */
    tag_type tag;               /* Tag for bond_entry<->iface association. */
};

enum bond_mode {
    BM_TCP, /* Transport Layer Load Balance. */
    BM_SLB, /* Source Load Balance. */
    BM_AB   /* Active Backup. */
};

#define MAX_MIRRORS 32
typedef uint32_t mirror_mask_t;
#define MIRROR_MASK_C(X) UINT32_C(X)
BUILD_ASSERT_DECL(sizeof(mirror_mask_t) * CHAR_BIT >= MAX_MIRRORS);
struct mirror {
    struct bridge *bridge;
    size_t idx;
    char *name;
    struct uuid uuid;           /* UUID of this "mirror" record in database. */

    /* Selection criteria. */
    struct sset src_ports;      /* Source port names. */
    struct sset dst_ports;      /* Destination port names. */
    int *vlans;
    size_t n_vlans;

    /* Output. */
    struct port *out_port;
    int out_vlan;
};

#define FLOOD_PORT ((struct port *) 1) /* The 'flood' output port. */
struct port {
    struct bridge *bridge;
    struct hmap_node hmap_node; /* Element in struct bridge's "ports" hmap. */
    char *name;

    int vlan;                   /* -1=trunk port, else a 12-bit VLAN ID. */
    unsigned long *trunks;      /* Bitmap of trunked VLANs, if 'vlan' == -1.
                                 * NULL if all VLANs are trunked. */
    const struct ovsrec_port *cfg;

    /* Monitoring. */
    struct netdev_monitor *monitor;   /* Tracks carrier. NULL if miimon. */
    long long int miimon_interval;    /* Miimon status refresh interval. */
    long long int miimon_next_update; /* Time of next miimon update. */

    /* An ordinary bridge port has 1 interface.
     * A bridge port for bonding has at least 2 interfaces. */
    struct list ifaces;         /* List of "struct iface"s. */
    size_t n_ifaces;            /* list_size(ifaces). */

    /* Bonding info. */
    enum bond_mode bond_mode;   /* Type of the bond. BM_SLB is the default. */
    struct iface *active_iface; /* iface on which bcasts accepted, or NULL. */
    tag_type no_ifaces_tag;     /* Tag for flows when all ifaces disabled. */
    int updelay, downdelay;     /* Delay before iface goes up/down, in ms. */
    bool bond_fake_iface;       /* Fake a bond interface for legacy compat? */
    long long int bond_next_fake_iface_update; /* Time of next update. */

    /* LACP information. */
    struct lacp *lacp;          /* LACP object. NULL if LACP is disabled. */
    bool lacp_active;           /* True if LACP is active */
    bool lacp_fast;             /* True if LACP is in fast mode. */
    uint16_t lacp_priority;     /* LACP system priority. */

    /* SLB specific bonding info. */
    struct bond_entry *bond_hash; /* An array of (BOND_MASK + 1) elements. */
    int bond_rebalance_interval; /* Interval between rebalances, in ms. */
    long long int bond_next_rebalance; /* Next rebalancing time. */

    /* Port mirroring info. */
    mirror_mask_t src_mirrors;  /* Mirrors triggered when packet received. */
    mirror_mask_t dst_mirrors;  /* Mirrors triggered when packet sent. */
    bool is_mirror_output_port; /* Does port mirroring send frames here? */
};

struct bridge {
    struct list node;           /* Node in global list of bridges. */
    char *name;                 /* User-specified arbitrary name. */
    struct mac_learning *ml;    /* MAC learning table. */
    uint8_t ea[ETH_ADDR_LEN];   /* Bridge Ethernet Address. */
    uint8_t default_ea[ETH_ADDR_LEN]; /* Default MAC. */
    const struct ovsrec_bridge *cfg;

    /* OpenFlow switch processing. */
    struct ofproto *ofproto;    /* OpenFlow switch. */

    /* Kernel datapath information. */
    struct dpif *dpif;          /* Datapath. */
    struct hmap ifaces;         /* Contains "struct iface"s. */

    /* Bridge ports. */
    struct hmap ports;          /* "struct port"s indexed by name. */
    struct shash iface_by_name; /* "struct iface"s indexed by name. */

    /* Bonding. */
    bool has_bonded_ports;

    /* Flow tracking. */
    bool flush;

    /* Port mirroring. */
    struct mirror *mirrors[MAX_MIRRORS];
};

/* List of all bridges. */
static struct list all_bridges = LIST_INITIALIZER(&all_bridges);

/* OVSDB IDL used to obtain configuration. */
static struct ovsdb_idl *idl;

/* Each time this timer expires, the bridge fetches systems and interface
 * statistics and pushes them into the database. */
#define STATS_INTERVAL (5 * 1000) /* In milliseconds. */
static long long int stats_timer = LLONG_MIN;

/* Stores the time after which CFM statistics may be written to the database.
 * Only updated when changes to the database require rate limiting. */
#define CFM_LIMIT_INTERVAL (1 * 1000) /* In milliseconds. */
static long long int cfm_limiter = LLONG_MIN;

static struct bridge *bridge_create(const struct ovsrec_bridge *br_cfg);
static void bridge_destroy(struct bridge *);
static struct bridge *bridge_lookup(const char *name);
static unixctl_cb_func bridge_unixctl_dump_flows;
static unixctl_cb_func bridge_unixctl_reconnect;
static int bridge_run_one(struct bridge *);
static size_t bridge_get_controllers(const struct bridge *br,
                                     struct ovsrec_controller ***controllersp);
static void bridge_reconfigure_one(struct bridge *);
static void bridge_reconfigure_remotes(struct bridge *,
                                       const struct sockaddr_in *managers,
                                       size_t n_managers);
static void bridge_get_all_ifaces(const struct bridge *, struct shash *ifaces);
static void bridge_fetch_dp_ifaces(struct bridge *);
static void bridge_flush(struct bridge *);
static void bridge_pick_local_hw_addr(struct bridge *,
                                      uint8_t ea[ETH_ADDR_LEN],
                                      struct iface **hw_addr_iface);
static uint64_t bridge_pick_datapath_id(struct bridge *,
                                        const uint8_t bridge_ea[ETH_ADDR_LEN],
                                        struct iface *hw_addr_iface);
static uint64_t dpid_from_hash(const void *, size_t nbytes);

static unixctl_cb_func bridge_unixctl_fdb_show;
static unixctl_cb_func cfm_unixctl_show;
static unixctl_cb_func qos_unixctl_show;

static void bond_init(void);
static void bond_run(struct port *);
static void bond_wait(struct port *);
static void bond_rebalance_port(struct port *);
static void bond_send_learning_packets(struct port *);
static void bond_enable_slave(struct iface *iface, bool enable);

static void port_run(struct port *);
static void port_wait(struct port *);
static struct port *port_create(struct bridge *, const char *name);
static void port_reconfigure(struct port *, const struct ovsrec_port *);
static void port_del_ifaces(struct port *, const struct ovsrec_port *);
static void port_destroy(struct port *);
static struct port *port_lookup(const struct bridge *, const char *name);
static struct iface *port_lookup_iface(const struct port *, const char *name);
static struct iface *port_get_an_iface(const struct port *);
static struct port *port_from_dp_ifidx(const struct bridge *,
                                       uint16_t dp_ifidx);
static void port_update_bonding(struct port *);
static void port_update_lacp(struct port *);

static void mirror_create(struct bridge *, struct ovsrec_mirror *);
static void mirror_destroy(struct mirror *);
static void mirror_reconfigure(struct bridge *);
static void mirror_reconfigure_one(struct mirror *, struct ovsrec_mirror *);
static bool vlan_is_mirrored(const struct mirror *, int vlan);

static struct iface *iface_create(struct port *port,
                                  const struct ovsrec_interface *if_cfg);
static void iface_destroy(struct iface *);
static struct iface *iface_lookup(const struct bridge *, const char *name);
static struct iface *iface_find(const char *name);
static struct iface *iface_from_dp_ifidx(const struct bridge *,
                                         uint16_t dp_ifidx);
static void iface_set_mac(struct iface *);
static void iface_set_ofport(const struct ovsrec_interface *, int64_t ofport);
static void iface_update_qos(struct iface *, const struct ovsrec_qos *);
static void iface_update_cfm(struct iface *);
static bool iface_refresh_cfm_stats(struct iface *iface);
static void iface_update_carrier(struct iface *);
static bool iface_get_carrier(const struct iface *);

static void shash_from_ovs_idl_map(char **keys, char **values, size_t n,
                                   struct shash *);
static void shash_to_ovs_idl_map(struct shash *,
                                 char ***keys, char ***values, size_t *n);

/* Hooks into ofproto processing. */
static struct ofhooks bridge_ofhooks;

/* Public functions. */

/* Initializes the bridge module, configuring it to obtain its configuration
 * from an OVSDB server accessed over 'remote', which should be a string in a
 * form acceptable to ovsdb_idl_create(). */
void
bridge_init(const char *remote)
{
    /* Create connection to database. */
    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, true);

    ovsdb_idl_omit_alert(idl, &ovsrec_open_vswitch_col_cur_cfg);
    ovsdb_idl_omit_alert(idl, &ovsrec_open_vswitch_col_statistics);
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_external_ids);

    ovsdb_idl_omit(idl, &ovsrec_bridge_col_external_ids);

    ovsdb_idl_omit(idl, &ovsrec_port_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_port_col_fake_bridge);

    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_ofport);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_statistics);
    ovsdb_idl_omit(idl, &ovsrec_interface_col_external_ids);

    /* Register unixctl commands. */
    unixctl_command_register("fdb/show", bridge_unixctl_fdb_show, NULL);
    unixctl_command_register("cfm/show", cfm_unixctl_show, NULL);
    unixctl_command_register("qos/show", qos_unixctl_show, NULL);
    unixctl_command_register("bridge/dump-flows", bridge_unixctl_dump_flows,
                             NULL);
    unixctl_command_register("bridge/reconnect", bridge_unixctl_reconnect,
                             NULL);
    lacp_init();
    bond_init();
}

void
bridge_exit(void)
{
    struct bridge *br, *next_br;

    LIST_FOR_EACH_SAFE (br, next_br, node, &all_bridges) {
        bridge_destroy(br);
    }
    ovsdb_idl_destroy(idl);
}

/* Performs configuration that is only necessary once at ovs-vswitchd startup,
 * but for which the ovs-vswitchd configuration 'cfg' is required. */
static void
bridge_configure_once(const struct ovsrec_open_vswitch *cfg)
{
    static bool already_configured_once;
    struct sset bridge_names;
    struct sset dpif_names, dpif_types;
    const char *type;
    size_t i;

    /* Only do this once per ovs-vswitchd run. */
    if (already_configured_once) {
        return;
    }
    already_configured_once = true;

    stats_timer = time_msec() + STATS_INTERVAL;

    /* Get all the configured bridges' names from 'cfg' into 'bridge_names'. */
    sset_init(&bridge_names);
    for (i = 0; i < cfg->n_bridges; i++) {
        sset_add(&bridge_names, cfg->bridges[i]->name);
    }

    /* Iterate over all system dpifs and delete any of them that do not appear
     * in 'cfg'. */
    sset_init(&dpif_names);
    sset_init(&dpif_types);
    dp_enumerate_types(&dpif_types);
    SSET_FOR_EACH (type, &dpif_types) {
        const char *name;

        dp_enumerate_names(type, &dpif_names);

        /* Delete each dpif whose name is not in 'bridge_names'. */
        SSET_FOR_EACH (name, &dpif_names) {
            if (!sset_contains(&bridge_names, name)) {
                struct dpif *dpif;
                int retval;

                retval = dpif_open(name, type, &dpif);
                if (!retval) {
                    dpif_delete(dpif);
                    dpif_close(dpif);
                }
            }
        }
    }
    sset_destroy(&bridge_names);
    sset_destroy(&dpif_names);
    sset_destroy(&dpif_types);
}

/* Callback for iterate_and_prune_ifaces(). */
static bool
check_iface(struct bridge *br, struct iface *iface, void *aux OVS_UNUSED)
{
    if (!iface->netdev) {
        /* We already reported a related error, don't bother duplicating it. */
        return false;
    }

    if (iface->dp_ifidx < 0) {
        VLOG_ERR("%s interface not in %s, dropping",
                 iface->name, dpif_name(br->dpif));
        return false;
    }

    VLOG_DBG("%s has interface %s on port %d", dpif_name(br->dpif),
             iface->name, iface->dp_ifidx);
    return true;
}

/* Callback for iterate_and_prune_ifaces(). */
static bool
set_iface_properties(struct bridge *br OVS_UNUSED, struct iface *iface,
                     void *aux OVS_UNUSED)
{
    /* Set policing attributes. */
    netdev_set_policing(iface->netdev,
                        iface->cfg->ingress_policing_rate,
                        iface->cfg->ingress_policing_burst);

    /* Set MAC address of internal interfaces other than the local
     * interface. */
    if (iface->dp_ifidx != ODPP_LOCAL && !strcmp(iface->type, "internal")) {
        iface_set_mac(iface);
    }

    return true;
}

/* Calls 'cb' for each interfaces in 'br', passing along the 'aux' argument.
 * Deletes from 'br' all the interfaces for which 'cb' returns false, and then
 * deletes from 'br' any ports that no longer have any interfaces. */
static void
iterate_and_prune_ifaces(struct bridge *br,
                         bool (*cb)(struct bridge *, struct iface *,
                                    void *aux),
                         void *aux)
{
    struct port *port, *next_port;

    HMAP_FOR_EACH_SAFE (port, next_port, hmap_node, &br->ports) {
        struct iface *iface, *next_iface;

        LIST_FOR_EACH_SAFE (iface, next_iface, port_elem, &port->ifaces) {
            if (!cb(br, iface, aux)) {
                iface_set_ofport(iface->cfg, -1);
                iface_destroy(iface);
            }
        }

        if (!port->n_ifaces) {
            VLOG_WARN("%s port has no interfaces, dropping", port->name);
            port_destroy(port);
        }
    }
}

/* Looks at the list of managers in 'ovs_cfg' and extracts their remote IP
 * addresses and ports into '*managersp' and '*n_managersp'.  The caller is
 * responsible for freeing '*managersp' (with free()).
 *
 * You may be asking yourself "why does ovs-vswitchd care?", because
 * ovsdb-server is responsible for connecting to the managers, and ovs-vswitchd
 * should not be and in fact is not directly involved in that.  But
 * ovs-vswitchd needs to make sure that ovsdb-server can reach the managers, so
 * it has to tell in-band control where the managers are to enable that.
 * (Thus, only managers connected in-band are collected.)
 */
static void
collect_in_band_managers(const struct ovsrec_open_vswitch *ovs_cfg,
                         struct sockaddr_in **managersp, size_t *n_managersp)
{
    struct sockaddr_in *managers = NULL;
    size_t n_managers = 0;
    struct sset targets;
    size_t i;

    /* Collect all of the potential targets from the "targets" columns of the
     * rows pointed to by "manager_options", excluding any that are
     * out-of-band. */
    sset_init(&targets);
    for (i = 0; i < ovs_cfg->n_manager_options; i++) {
        struct ovsrec_manager *m = ovs_cfg->manager_options[i];

        if (m->connection_mode && !strcmp(m->connection_mode, "out-of-band")) {
            sset_find_and_delete(&targets, m->target);
        } else {
            sset_add(&targets, m->target);
        }
    }

    /* Now extract the targets' IP addresses. */
    if (!sset_is_empty(&targets)) {
        const char *target;

        managers = xmalloc(sset_count(&targets) * sizeof *managers);
        SSET_FOR_EACH (target, &targets) {
            struct sockaddr_in *sin = &managers[n_managers];

            if ((!strncmp(target, "tcp:", 4)
                 && inet_parse_active(target + 4, JSONRPC_TCP_PORT, sin)) ||
                (!strncmp(target, "ssl:", 4)
                 && inet_parse_active(target + 4, JSONRPC_SSL_PORT, sin))) {
                n_managers++;
            }
        }
    }
    sset_destroy(&targets);

    *managersp = managers;
    *n_managersp = n_managers;
}

static void
bridge_reconfigure(const struct ovsrec_open_vswitch *ovs_cfg)
{
    struct shash old_br, new_br;
    struct shash_node *node;
    struct bridge *br, *next;
    struct sockaddr_in *managers;
    size_t n_managers;
    size_t i;
    int sflow_bridge_number;

    COVERAGE_INC(bridge_reconfigure);

    collect_in_band_managers(ovs_cfg, &managers, &n_managers);

    /* Collect old and new bridges. */
    shash_init(&old_br);
    shash_init(&new_br);
    LIST_FOR_EACH (br, node, &all_bridges) {
        shash_add(&old_br, br->name, br);
    }
    for (i = 0; i < ovs_cfg->n_bridges; i++) {
        const struct ovsrec_bridge *br_cfg = ovs_cfg->bridges[i];
        if (!shash_add_once(&new_br, br_cfg->name, br_cfg)) {
            VLOG_WARN("more than one bridge named %s", br_cfg->name);
        }
    }

    /* Get rid of deleted bridges and add new bridges. */
    LIST_FOR_EACH_SAFE (br, next, node, &all_bridges) {
        struct ovsrec_bridge *br_cfg = shash_find_data(&new_br, br->name);
        if (br_cfg) {
            br->cfg = br_cfg;
        } else {
            bridge_destroy(br);
        }
    }
    SHASH_FOR_EACH (node, &new_br) {
        const char *br_name = node->name;
        const struct ovsrec_bridge *br_cfg = node->data;
        br = shash_find_data(&old_br, br_name);
        if (br) {
            /* If the bridge datapath type has changed, we need to tear it
             * down and recreate. */
            if (strcmp(br->cfg->datapath_type, br_cfg->datapath_type)) {
                bridge_destroy(br);
                bridge_create(br_cfg);
            }
        } else {
            bridge_create(br_cfg);
        }
    }
    shash_destroy(&old_br);
    shash_destroy(&new_br);

    /* Reconfigure all bridges. */
    LIST_FOR_EACH (br, node, &all_bridges) {
        bridge_reconfigure_one(br);
    }

    /* Add and delete ports on all datapaths.
     *
     * The kernel will reject any attempt to add a given port to a datapath if
     * that port already belongs to a different datapath, so we must do all
     * port deletions before any port additions. */
    LIST_FOR_EACH (br, node, &all_bridges) {
        struct dpif_port_dump dump;
        struct shash want_ifaces;
        struct dpif_port dpif_port;

        bridge_get_all_ifaces(br, &want_ifaces);
        DPIF_PORT_FOR_EACH (&dpif_port, &dump, br->dpif) {
            if (!shash_find(&want_ifaces, dpif_port.name)
                && strcmp(dpif_port.name, br->name)) {
                int retval = dpif_port_del(br->dpif, dpif_port.port_no);
                if (retval) {
                    VLOG_WARN("failed to remove %s interface from %s: %s",
                              dpif_port.name, dpif_name(br->dpif),
                              strerror(retval));
                }
            }
        }
        shash_destroy(&want_ifaces);
    }
    LIST_FOR_EACH (br, node, &all_bridges) {
        struct shash cur_ifaces, want_ifaces;
        struct dpif_port_dump dump;
        struct dpif_port dpif_port;

        /* Get the set of interfaces currently in this datapath. */
        shash_init(&cur_ifaces);
        DPIF_PORT_FOR_EACH (&dpif_port, &dump, br->dpif) {
            struct dpif_port *port_info = xmalloc(sizeof *port_info);
            dpif_port_clone(port_info, &dpif_port);
            shash_add(&cur_ifaces, dpif_port.name, port_info);
        }

        /* Get the set of interfaces we want on this datapath. */
        bridge_get_all_ifaces(br, &want_ifaces);

        hmap_clear(&br->ifaces);
        SHASH_FOR_EACH (node, &want_ifaces) {
            const char *if_name = node->name;
            struct iface *iface = node->data;
            struct dpif_port *dpif_port;
            const char *type;
            int error;

            type = iface ? iface->type : "internal";
            dpif_port = shash_find_data(&cur_ifaces, if_name);

            /* If we have a port or a netdev already, and it's not the type we
             * want, then delete the port (if any) and close the netdev (if
             * any). */
            if ((dpif_port && strcmp(dpif_port->type, type))
                || (iface && iface->netdev
                    && strcmp(type, netdev_get_type(iface->netdev)))) {
                if (dpif_port) {
                    error = ofproto_port_del(br->ofproto, dpif_port->port_no);
                    if (error) {
                        continue;
                    }
                    dpif_port = NULL;
                }
                if (iface) {
                    netdev_close(iface->netdev);
                    iface->netdev = NULL;
                }
            }

            /* If the port doesn't exist or we don't have the netdev open,
             * we need to do more work. */
            if (!dpif_port || (iface && !iface->netdev)) {
                struct netdev_options options;
                struct netdev *netdev;
                struct shash args;

                /* First open the network device. */
                options.name = if_name;
                options.type = type;
                options.args = &args;
                options.ethertype = NETDEV_ETH_TYPE_NONE;

                shash_init(&args);
                if (iface) {
                    shash_from_ovs_idl_map(iface->cfg->key_options,
                                           iface->cfg->value_options,
                                           iface->cfg->n_options, &args);
                }
                error = netdev_open(&options, &netdev);
                shash_destroy(&args);

                if (error) {
                    VLOG_WARN("could not open network device %s (%s)",
                              if_name, strerror(error));
                    continue;
                }

                /* Then add the port if we haven't already. */
                if (!dpif_port) {
                    error = dpif_port_add(br->dpif, netdev, NULL);
                    if (error) {
                        netdev_close(netdev);
                        if (error == EFBIG) {
                            VLOG_ERR("ran out of valid port numbers on %s",
                                     dpif_name(br->dpif));
                            break;
                        } else {
                            VLOG_WARN("failed to add %s interface to %s: %s",
                                      if_name, dpif_name(br->dpif),
                                      strerror(error));
                            continue;
                        }
                    }
                }

                /* Update 'iface'. */
                if (iface) {
                    iface->netdev = netdev;
                    iface->enabled = iface_get_carrier(iface);
                    iface->up = iface->enabled;
                }
            } else if (iface && iface->netdev) {
                struct shash args;

                shash_init(&args);
                shash_from_ovs_idl_map(iface->cfg->key_options,
                                       iface->cfg->value_options,
                                       iface->cfg->n_options, &args);
                netdev_set_config(iface->netdev, &args);
                shash_destroy(&args);
            }
        }
        shash_destroy(&want_ifaces);

        SHASH_FOR_EACH (node, &cur_ifaces) {
            struct dpif_port *port_info = node->data;
            dpif_port_destroy(port_info);
            free(port_info);
        }
        shash_destroy(&cur_ifaces);
    }
    sflow_bridge_number = 0;
    LIST_FOR_EACH (br, node, &all_bridges) {
        uint8_t ea[8];
        uint64_t dpid;
        struct iface *local_iface;
        struct iface *hw_addr_iface;
        char *dpid_string;

        bridge_fetch_dp_ifaces(br);

        iterate_and_prune_ifaces(br, check_iface, NULL);

        /* Pick local port hardware address, datapath ID. */
        bridge_pick_local_hw_addr(br, ea, &hw_addr_iface);
        local_iface = iface_from_dp_ifidx(br, ODPP_LOCAL);
        if (local_iface) {
            int error = netdev_set_etheraddr(local_iface->netdev, ea);
            if (error) {
                static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
                VLOG_ERR_RL(&rl, "bridge %s: failed to set bridge "
                            "Ethernet address: %s",
                            br->name, strerror(error));
            }
        }
        memcpy(br->ea, ea, ETH_ADDR_LEN);

        dpid = bridge_pick_datapath_id(br, ea, hw_addr_iface);
        ofproto_set_datapath_id(br->ofproto, dpid);

        dpid_string = xasprintf("%016"PRIx64, dpid);
        ovsrec_bridge_set_datapath_id(br->cfg, dpid_string);
        free(dpid_string);

        /* Set NetFlow configuration on this bridge. */
        if (br->cfg->netflow) {
            struct ovsrec_netflow *nf_cfg = br->cfg->netflow;
            struct netflow_options opts;

            memset(&opts, 0, sizeof opts);

            dpif_get_netflow_ids(br->dpif, &opts.engine_type, &opts.engine_id);
            if (nf_cfg->engine_type) {
                opts.engine_type = *nf_cfg->engine_type;
            }
            if (nf_cfg->engine_id) {
                opts.engine_id = *nf_cfg->engine_id;
            }

            opts.active_timeout = nf_cfg->active_timeout;
            if (!opts.active_timeout) {
                opts.active_timeout = -1;
            } else if (opts.active_timeout < 0) {
                VLOG_WARN("bridge %s: active timeout interval set to negative "
                          "value, using default instead (%d seconds)", br->name,
                          NF_ACTIVE_TIMEOUT_DEFAULT);
                opts.active_timeout = -1;
            }

            opts.add_id_to_iface = nf_cfg->add_id_to_interface;
            if (opts.add_id_to_iface) {
                if (opts.engine_id > 0x7f) {
                    VLOG_WARN("bridge %s: netflow port mangling may conflict "
                              "with another vswitch, choose an engine id less "
                              "than 128", br->name);
                }
                if (hmap_count(&br->ports) > 508) {
                    VLOG_WARN("bridge %s: netflow port mangling will conflict "
                              "with another port when more than 508 ports are "
                              "used", br->name);
                }
            }

            sset_init(&opts.collectors);
            sset_add_array(&opts.collectors,
                           nf_cfg->targets, nf_cfg->n_targets);
            if (ofproto_set_netflow(br->ofproto, &opts)) {
                VLOG_ERR("bridge %s: problem setting netflow collectors",
                         br->name);
            }
            sset_destroy(&opts.collectors);
        } else {
            ofproto_set_netflow(br->ofproto, NULL);
        }

        /* Set sFlow configuration on this bridge. */
        if (br->cfg->sflow) {
            const struct ovsrec_sflow *sflow_cfg = br->cfg->sflow;
            struct ovsrec_controller **controllers;
            struct ofproto_sflow_options oso;
            size_t n_controllers;

            memset(&oso, 0, sizeof oso);

            sset_init(&oso.targets);
            sset_add_array(&oso.targets,
                           sflow_cfg->targets, sflow_cfg->n_targets);

            oso.sampling_rate = SFL_DEFAULT_SAMPLING_RATE;
            if (sflow_cfg->sampling) {
                oso.sampling_rate = *sflow_cfg->sampling;
            }

            oso.polling_interval = SFL_DEFAULT_POLLING_INTERVAL;
            if (sflow_cfg->polling) {
                oso.polling_interval = *sflow_cfg->polling;
            }

            oso.header_len = SFL_DEFAULT_HEADER_SIZE;
            if (sflow_cfg->header) {
                oso.header_len = *sflow_cfg->header;
            }

            oso.sub_id = sflow_bridge_number++;
            oso.agent_device = sflow_cfg->agent;

            oso.control_ip = NULL;
            n_controllers = bridge_get_controllers(br, &controllers);
            for (i = 0; i < n_controllers; i++) {
                if (controllers[i]->local_ip) {
                    oso.control_ip = controllers[i]->local_ip;
                    break;
                }
            }
            ofproto_set_sflow(br->ofproto, &oso);

            sset_destroy(&oso.targets);
        } else {
            ofproto_set_sflow(br->ofproto, NULL);
        }

        /* Update the controller and related settings.  It would be more
         * straightforward to call this from bridge_reconfigure_one(), but we
         * can't do it there for two reasons.  First, and most importantly, at
         * that point we don't know the dp_ifidx of any interfaces that have
         * been added to the bridge (because we haven't actually added them to
         * the datapath).  Second, at that point we haven't set the datapath ID
         * yet; when a controller is configured, resetting the datapath ID will
         * immediately disconnect from the controller, so it's better to set
         * the datapath ID before the controller. */
        bridge_reconfigure_remotes(br, managers, n_managers);
    }
    LIST_FOR_EACH (br, node, &all_bridges) {
        struct port *port;

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            struct iface *iface;

            if (port->monitor) {
                LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                    netdev_monitor_add(port->monitor, iface->netdev);
                }
            } else {
                port->miimon_next_update = 0;
            }

            port_update_lacp(port);
            port_update_bonding(port);

            LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                iface_update_qos(iface, port->cfg->qos);
            }
        }
    }
    LIST_FOR_EACH (br, node, &all_bridges) {
        iterate_and_prune_ifaces(br, set_iface_properties, NULL);
    }

    /* Some reconfiguration operations require the bridge to have been run at
     * least once.  */
    LIST_FOR_EACH (br, node, &all_bridges) {
        struct iface *iface;

        bridge_run_one(br);

        HMAP_FOR_EACH (iface, dp_ifidx_node, &br->ifaces) {
            iface_update_cfm(iface);
        }
    }

    free(managers);

    /* ovs-vswitchd has completed initialization, so allow the process that
     * forked us to exit successfully. */
    daemonize_complete();
}

static const char *
get_ovsrec_key_value(const struct ovsdb_idl_row *row,
                     const struct ovsdb_idl_column *column,
                     const char *key)
{
    const struct ovsdb_datum *datum;
    union ovsdb_atom atom;
    unsigned int idx;

    datum = ovsdb_idl_get(row, column, OVSDB_TYPE_STRING, OVSDB_TYPE_STRING);
    atom.string = (char *) key;
    idx = ovsdb_datum_find_key(datum, &atom, OVSDB_TYPE_STRING);
    return idx == UINT_MAX ? NULL : datum->values[idx].string;
}

static const char *
bridge_get_other_config(const struct ovsrec_bridge *br_cfg, const char *key)
{
    return get_ovsrec_key_value(&br_cfg->header_,
                                &ovsrec_bridge_col_other_config, key);
}

static void
bridge_pick_local_hw_addr(struct bridge *br, uint8_t ea[ETH_ADDR_LEN],
                          struct iface **hw_addr_iface)
{
    const char *hwaddr;
    struct port *port;
    int error;

    *hw_addr_iface = NULL;

    /* Did the user request a particular MAC? */
    hwaddr = bridge_get_other_config(br->cfg, "hwaddr");
    if (hwaddr && eth_addr_from_string(hwaddr, ea)) {
        if (eth_addr_is_multicast(ea)) {
            VLOG_ERR("bridge %s: cannot set MAC address to multicast "
                     "address "ETH_ADDR_FMT, br->name, ETH_ADDR_ARGS(ea));
        } else if (eth_addr_is_zero(ea)) {
            VLOG_ERR("bridge %s: cannot set MAC address to zero", br->name);
        } else {
            return;
        }
    }

    /* Otherwise choose the minimum non-local MAC address among all of the
     * interfaces. */
    memset(ea, 0xff, ETH_ADDR_LEN);
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        uint8_t iface_ea[ETH_ADDR_LEN];
        struct iface *candidate;
        struct iface *iface;

        /* Mirror output ports don't participate. */
        if (port->is_mirror_output_port) {
            continue;
        }

        /* Choose the MAC address to represent the port. */
        iface = NULL;
        if (port->cfg->mac && eth_addr_from_string(port->cfg->mac, iface_ea)) {
            /* Find the interface with this Ethernet address (if any) so that
             * we can provide the correct devname to the caller. */
            LIST_FOR_EACH (candidate, port_elem, &port->ifaces) {
                uint8_t candidate_ea[ETH_ADDR_LEN];
                if (!netdev_get_etheraddr(candidate->netdev, candidate_ea)
                    && eth_addr_equals(iface_ea, candidate_ea)) {
                    iface = candidate;
                }
            }
        } else {
            /* Choose the interface whose MAC address will represent the port.
             * The Linux kernel bonding code always chooses the MAC address of
             * the first slave added to a bond, and the Fedora networking
             * scripts always add slaves to a bond in alphabetical order, so
             * for compatibility we choose the interface with the name that is
             * first in alphabetical order. */
            LIST_FOR_EACH (candidate, port_elem, &port->ifaces) {
                if (!iface || strcmp(candidate->name, iface->name) < 0) {
                    iface = candidate;
                }
            }

            /* The local port doesn't count (since we're trying to choose its
             * MAC address anyway). */
            if (iface->dp_ifidx == ODPP_LOCAL) {
                continue;
            }

            /* Grab MAC. */
            error = netdev_get_etheraddr(iface->netdev, iface_ea);
            if (error) {
                static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
                VLOG_ERR_RL(&rl, "failed to obtain Ethernet address of %s: %s",
                            iface->name, strerror(error));
                continue;
            }
        }

        /* Compare against our current choice. */
        if (!eth_addr_is_multicast(iface_ea) &&
            !eth_addr_is_local(iface_ea) &&
            !eth_addr_is_reserved(iface_ea) &&
            !eth_addr_is_zero(iface_ea) &&
            eth_addr_compare_3way(iface_ea, ea) < 0)
        {
            memcpy(ea, iface_ea, ETH_ADDR_LEN);
            *hw_addr_iface = iface;
        }
    }
    if (eth_addr_is_multicast(ea)) {
        memcpy(ea, br->default_ea, ETH_ADDR_LEN);
        *hw_addr_iface = NULL;
        VLOG_WARN("bridge %s: using default bridge Ethernet "
                  "address "ETH_ADDR_FMT, br->name, ETH_ADDR_ARGS(ea));
    } else {
        VLOG_DBG("bridge %s: using bridge Ethernet address "ETH_ADDR_FMT,
                 br->name, ETH_ADDR_ARGS(ea));
    }
}

/* Choose and returns the datapath ID for bridge 'br' given that the bridge
 * Ethernet address is 'bridge_ea'.  If 'bridge_ea' is the Ethernet address of
 * an interface on 'br', then that interface must be passed in as
 * 'hw_addr_iface'; if 'bridge_ea' was derived some other way, then
 * 'hw_addr_iface' must be passed in as a null pointer. */
static uint64_t
bridge_pick_datapath_id(struct bridge *br,
                        const uint8_t bridge_ea[ETH_ADDR_LEN],
                        struct iface *hw_addr_iface)
{
    /*
     * The procedure for choosing a bridge MAC address will, in the most
     * ordinary case, also choose a unique MAC that we can use as a datapath
     * ID.  In some special cases, though, multiple bridges will end up with
     * the same MAC address.  This is OK for the bridges, but it will confuse
     * the OpenFlow controller, because each datapath needs a unique datapath
     * ID.
     *
     * Datapath IDs must be unique.  It is also very desirable that they be
     * stable from one run to the next, so that policy set on a datapath
     * "sticks".
     */
    const char *datapath_id;
    uint64_t dpid;

    datapath_id = bridge_get_other_config(br->cfg, "datapath-id");
    if (datapath_id && dpid_from_string(datapath_id, &dpid)) {
        return dpid;
    }

    if (hw_addr_iface) {
        int vlan;
        if (!netdev_get_vlan_vid(hw_addr_iface->netdev, &vlan)) {
            /*
             * A bridge whose MAC address is taken from a VLAN network device
             * (that is, a network device created with vconfig(8) or similar
             * tool) will have the same MAC address as a bridge on the VLAN
             * device's physical network device.
             *
             * Handle this case by hashing the physical network device MAC
             * along with the VLAN identifier.
             */
            uint8_t buf[ETH_ADDR_LEN + 2];
            memcpy(buf, bridge_ea, ETH_ADDR_LEN);
            buf[ETH_ADDR_LEN] = vlan >> 8;
            buf[ETH_ADDR_LEN + 1] = vlan;
            return dpid_from_hash(buf, sizeof buf);
        } else {
            /*
             * Assume that this bridge's MAC address is unique, since it
             * doesn't fit any of the cases we handle specially.
             */
        }
    } else {
        /*
         * A purely internal bridge, that is, one that has no non-virtual
         * network devices on it at all, is more difficult because it has no
         * natural unique identifier at all.
         *
         * When the host is a XenServer, we handle this case by hashing the
         * host's UUID with the name of the bridge.  Names of bridges are
         * persistent across XenServer reboots, although they can be reused if
         * an internal network is destroyed and then a new one is later
         * created, so this is fairly effective.
         *
         * When the host is not a XenServer, we punt by using a random MAC
         * address on each run.
         */
        const char *host_uuid = xenserver_get_host_uuid();
        if (host_uuid) {
            char *combined = xasprintf("%s,%s", host_uuid, br->name);
            dpid = dpid_from_hash(combined, strlen(combined));
            free(combined);
            return dpid;
        }
    }

    return eth_addr_to_uint64(bridge_ea);
}

static uint64_t
dpid_from_hash(const void *data, size_t n)
{
    uint8_t hash[SHA1_DIGEST_SIZE];

    BUILD_ASSERT_DECL(sizeof hash >= ETH_ADDR_LEN);
    sha1_bytes(data, n, hash);
    eth_addr_mark_random(hash);
    return eth_addr_to_uint64(hash);
}

static void
iface_refresh_status(struct iface *iface)
{
    struct shash sh;

    enum netdev_flags flags;
    uint32_t current;
    int64_t bps;
    int mtu;
    int64_t mtu_64;
    int error;

    shash_init(&sh);

    if (!netdev_get_status(iface->netdev, &sh)) {
        size_t n;
        char **keys, **values;

        shash_to_ovs_idl_map(&sh, &keys, &values, &n);
        ovsrec_interface_set_status(iface->cfg, keys, values, n);

        free(keys);
        free(values);
    } else {
        ovsrec_interface_set_status(iface->cfg, NULL, NULL, 0);
    }

    shash_destroy_free_data(&sh);

    error = netdev_get_flags(iface->netdev, &flags);
    if (!error) {
        ovsrec_interface_set_admin_state(iface->cfg, flags & NETDEV_UP ? "up" : "down");
    }
    else {
        ovsrec_interface_set_admin_state(iface->cfg, NULL);
    }

    error = netdev_get_features(iface->netdev, &current, NULL, NULL, NULL);
    if (!error) {
        ovsrec_interface_set_duplex(iface->cfg,
                                    netdev_features_is_full_duplex(current)
                                    ? "full" : "half");
        /* warning: uint64_t -> int64_t conversion */
        bps = netdev_features_to_bps(current);
        ovsrec_interface_set_link_speed(iface->cfg, &bps, 1);
    }
    else {
        ovsrec_interface_set_duplex(iface->cfg, NULL);
        ovsrec_interface_set_link_speed(iface->cfg, NULL, 0);
    }


    ovsrec_interface_set_link_state(iface->cfg,
                                    iface_get_carrier(iface) ? "up" : "down");

    error = netdev_get_mtu(iface->netdev, &mtu);
    if (!error && mtu != INT_MAX) {
        mtu_64 = mtu;
        ovsrec_interface_set_mtu(iface->cfg, &mtu_64, 1);
    }
    else {
        ovsrec_interface_set_mtu(iface->cfg, NULL, 0);
    }
}

/* Writes 'iface''s CFM statistics to the database.  Returns true if anything
 * changed, false otherwise. */
static bool
iface_refresh_cfm_stats(struct iface *iface)
{
    const struct ovsrec_monitor *mon;
    const struct cfm *cfm;
    bool changed = false;
    size_t i;

    mon = iface->cfg->monitor;
    cfm = ofproto_iface_get_cfm(iface->port->bridge->ofproto, iface->dp_ifidx);

    if (!cfm || !mon) {
        return false;
    }

    for (i = 0; i < mon->n_remote_mps; i++) {
        const struct ovsrec_maintenance_point *mp;
        const struct remote_mp *rmp;

        mp = mon->remote_mps[i];
        rmp = cfm_get_remote_mp(cfm, mp->mpid);

        if (mp->n_fault != 1 || mp->fault[0] != rmp->fault) {
            ovsrec_maintenance_point_set_fault(mp, &rmp->fault, 1);
            changed = true;
        }
    }

    if (mon->n_fault != 1 || mon->fault[0] != cfm->fault) {
        ovsrec_monitor_set_fault(mon, &cfm->fault, 1);
        changed = true;
    }

    return changed;
}

static void
iface_refresh_stats(struct iface *iface)
{
    struct iface_stat {
        char *name;
        int offset;
    };
    static const struct iface_stat iface_stats[] = {
        { "rx_packets", offsetof(struct netdev_stats, rx_packets) },
        { "tx_packets", offsetof(struct netdev_stats, tx_packets) },
        { "rx_bytes", offsetof(struct netdev_stats, rx_bytes) },
        { "tx_bytes", offsetof(struct netdev_stats, tx_bytes) },
        { "rx_dropped", offsetof(struct netdev_stats, rx_dropped) },
        { "tx_dropped", offsetof(struct netdev_stats, tx_dropped) },
        { "rx_errors", offsetof(struct netdev_stats, rx_errors) },
        { "tx_errors", offsetof(struct netdev_stats, tx_errors) },
        { "rx_frame_err", offsetof(struct netdev_stats, rx_frame_errors) },
        { "rx_over_err", offsetof(struct netdev_stats, rx_over_errors) },
        { "rx_crc_err", offsetof(struct netdev_stats, rx_crc_errors) },
        { "collisions", offsetof(struct netdev_stats, collisions) },
    };
    enum { N_STATS = ARRAY_SIZE(iface_stats) };
    const struct iface_stat *s;

    char *keys[N_STATS];
    int64_t values[N_STATS];
    int n;

    struct netdev_stats stats;

    /* Intentionally ignore return value, since errors will set 'stats' to
     * all-1s, and we will deal with that correctly below. */
    netdev_get_stats(iface->netdev, &stats);

    n = 0;
    for (s = iface_stats; s < &iface_stats[N_STATS]; s++) {
        uint64_t value = *(uint64_t *) (((char *) &stats) + s->offset);
        if (value != UINT64_MAX) {
            keys[n] = s->name;
            values[n] = value;
            n++;
        }
    }

    ovsrec_interface_set_statistics(iface->cfg, keys, values, n);
}

static void
refresh_system_stats(const struct ovsrec_open_vswitch *cfg)
{
    struct ovsdb_datum datum;
    struct shash stats;

    shash_init(&stats);
    get_system_stats(&stats);

    ovsdb_datum_from_shash(&datum, &stats);
    ovsdb_idl_txn_write(&cfg->header_, &ovsrec_open_vswitch_col_statistics,
                        &datum);
}

static inline const char *
nx_role_to_str(enum nx_role role)
{
    switch (role) {
    case NX_ROLE_OTHER:
        return "other";
    case NX_ROLE_MASTER:
        return "master";
    case NX_ROLE_SLAVE:
        return "slave";
    default:
        return "*** INVALID ROLE ***";
    }
}

static void
bridge_refresh_controller_status(const struct bridge *br)
{
    struct shash info;
    const struct ovsrec_controller *cfg;

    ofproto_get_ofproto_controller_info(br->ofproto, &info);

    OVSREC_CONTROLLER_FOR_EACH(cfg, idl) {
        struct ofproto_controller_info *cinfo =
            shash_find_data(&info, cfg->target);

        if (cinfo) {
            ovsrec_controller_set_is_connected(cfg, cinfo->is_connected);
            ovsrec_controller_set_role(cfg, nx_role_to_str(cinfo->role));
            ovsrec_controller_set_status(cfg, (char **) cinfo->pairs.keys,
                                         (char **) cinfo->pairs.values,
                                         cinfo->pairs.n);
        } else {
            ovsrec_controller_set_is_connected(cfg, false);
            ovsrec_controller_set_role(cfg, NULL);
            ovsrec_controller_set_status(cfg, NULL, NULL, 0);
        }
    }

    ofproto_free_ofproto_controller_info(&info);
}

void
bridge_run(void)
{
    const struct ovsrec_open_vswitch *cfg;

    bool datapath_destroyed;
    bool database_changed;
    struct bridge *br;

    /* Let each bridge do the work that it needs to do. */
    datapath_destroyed = false;
    LIST_FOR_EACH (br, node, &all_bridges) {
        int error = bridge_run_one(br);
        if (error) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
            VLOG_ERR_RL(&rl, "bridge %s: datapath was destroyed externally, "
                        "forcing reconfiguration", br->name);
            datapath_destroyed = true;
        }
    }

    /* (Re)configure if necessary. */
    database_changed = ovsdb_idl_run(idl);
    cfg = ovsrec_open_vswitch_first(idl);
#ifdef HAVE_OPENSSL
    /* Re-configure SSL.  We do this on every trip through the main loop,
     * instead of just when the database changes, because the contents of the
     * key and certificate files can change without the database changing.
     *
     * We do this before bridge_reconfigure() because that function might
     * initiate SSL connections and thus requires SSL to be configured. */
    if (cfg && cfg->ssl) {
        const struct ovsrec_ssl *ssl = cfg->ssl;

        stream_ssl_set_key_and_cert(ssl->private_key, ssl->certificate);
        stream_ssl_set_ca_cert_file(ssl->ca_cert, ssl->bootstrap_ca_cert);
    }
#endif
    if (database_changed || datapath_destroyed) {
        if (cfg) {
            struct ovsdb_idl_txn *txn = ovsdb_idl_txn_create(idl);

            bridge_configure_once(cfg);
            bridge_reconfigure(cfg);

            ovsrec_open_vswitch_set_cur_cfg(cfg, cfg->next_cfg);
            ovsdb_idl_txn_commit(txn);
            ovsdb_idl_txn_destroy(txn); /* XXX */
        } else {
            /* We still need to reconfigure to avoid dangling pointers to
             * now-destroyed ovsrec structures inside bridge data. */
            static const struct ovsrec_open_vswitch null_cfg;

            bridge_reconfigure(&null_cfg);
        }
    }

    /* Refresh system and interface stats if necessary. */
    if (time_msec() >= stats_timer) {
        if (cfg) {
            struct ovsdb_idl_txn *txn;

            txn = ovsdb_idl_txn_create(idl);
            LIST_FOR_EACH (br, node, &all_bridges) {
                struct port *port;

                HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_stats(iface);
                        iface_refresh_status(iface);
                    }
                }
                bridge_refresh_controller_status(br);
            }
            refresh_system_stats(cfg);
            ovsdb_idl_txn_commit(txn);
            ovsdb_idl_txn_destroy(txn); /* XXX */
        }

        stats_timer = time_msec() + STATS_INTERVAL;
    }

    if (time_msec() >= cfm_limiter) {
        struct ovsdb_idl_txn *txn;
        bool changed = false;

        txn = ovsdb_idl_txn_create(idl);
        LIST_FOR_EACH (br, node, &all_bridges) {
            struct port *port;

            HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                struct iface *iface;

                LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                    changed = iface_refresh_cfm_stats(iface) || changed;
                }
            }
        }

        if (changed) {
            cfm_limiter = time_msec() + CFM_LIMIT_INTERVAL;
        }

        ovsdb_idl_txn_commit(txn);
        ovsdb_idl_txn_destroy(txn);
    }
}

void
bridge_wait(void)
{
    struct bridge *br;

    LIST_FOR_EACH (br, node, &all_bridges) {
        struct port *port;

        ofproto_wait(br->ofproto);
        mac_learning_wait(br->ml);
        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            port_wait(port);
        }
    }
    ovsdb_idl_wait(idl);
    poll_timer_wait_until(stats_timer);

    if (cfm_limiter > time_msec()) {
        poll_timer_wait_until(cfm_limiter);
    }
}

/* Forces 'br' to revalidate all of its flows.  This is appropriate when 'br''s
 * configuration changes.  */
static void
bridge_flush(struct bridge *br)
{
    COVERAGE_INC(bridge_flush);
    br->flush = true;
}

/* Bridge unixctl user interface functions. */
static void
bridge_unixctl_fdb_show(struct unixctl_conn *conn,
                        const char *args, void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct bridge *br;
    const struct mac_entry *e;

    br = bridge_lookup(args);
    if (!br) {
        unixctl_command_reply(conn, 501, "no such bridge");
        return;
    }

    ds_put_cstr(&ds, " port  VLAN  MAC                Age\n");
    LIST_FOR_EACH (e, lru_node, &br->ml->lrus) {
        struct port *port = e->port.p;
        ds_put_format(&ds, "%5d  %4d  "ETH_ADDR_FMT"  %3d\n",
                      port_get_an_iface(port)->dp_ifidx,
                      e->vlan, ETH_ADDR_ARGS(e->mac), mac_entry_age(e));
    }
    unixctl_command_reply(conn, 200, ds_cstr(&ds));
    ds_destroy(&ds);
}

/* CFM unixctl user interface functions. */
static void
cfm_unixctl_show(struct unixctl_conn *conn,
                 const char *args, void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct iface *iface;
    const struct cfm *cfm;

    iface = iface_find(args);
    if (!iface) {
        unixctl_command_reply(conn, 501, "no such interface");
        return;
    }

    cfm = ofproto_iface_get_cfm(iface->port->bridge->ofproto, iface->dp_ifidx);

    if (!cfm) {
        unixctl_command_reply(conn, 501, "CFM not enabled");
        return;
    }

    cfm_dump_ds(cfm, &ds);
    unixctl_command_reply(conn, 200, ds_cstr(&ds));
    ds_destroy(&ds);
}

/* QoS unixctl user interface functions. */

struct qos_unixctl_show_cbdata {
    struct ds *ds;
    struct iface *iface;
};

static void
qos_unixctl_show_cb(unsigned int queue_id,
                    const struct shash *details,
                    void *aux)
{
    struct qos_unixctl_show_cbdata *data = aux;
    struct ds *ds = data->ds;
    struct iface *iface = data->iface;
    struct netdev_queue_stats stats;
    struct shash_node *node;
    int error;

    ds_put_cstr(ds, "\n");
    if (queue_id) {
        ds_put_format(ds, "Queue %u:\n", queue_id);
    } else {
        ds_put_cstr(ds, "Default:\n");
    }

    SHASH_FOR_EACH (node, details) {
        ds_put_format(ds, "\t%s: %s\n", node->name, (char *)node->data);
    }

    error = netdev_get_queue_stats(iface->netdev, queue_id, &stats);
    if (!error) {
        if (stats.tx_packets != UINT64_MAX) {
            ds_put_format(ds, "\ttx_packets: %"PRIu64"\n", stats.tx_packets);
        }

        if (stats.tx_bytes != UINT64_MAX) {
            ds_put_format(ds, "\ttx_bytes: %"PRIu64"\n", stats.tx_bytes);
        }

        if (stats.tx_errors != UINT64_MAX) {
            ds_put_format(ds, "\ttx_errors: %"PRIu64"\n", stats.tx_errors);
        }
    } else {
        ds_put_format(ds, "\tFailed to get statistics for queue %u: %s",
                      queue_id, strerror(error));
    }
}

static void
qos_unixctl_show(struct unixctl_conn *conn,
                 const char *args, void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct shash sh = SHASH_INITIALIZER(&sh);
    struct iface *iface;
    const char *type;
    struct shash_node *node;
    struct qos_unixctl_show_cbdata data;
    int error;

    iface = iface_find(args);
    if (!iface) {
        unixctl_command_reply(conn, 501, "no such interface");
        return;
    }

    netdev_get_qos(iface->netdev, &type, &sh);

    if (*type != '\0') {
        ds_put_format(&ds, "QoS: %s %s\n", iface->name, type);

        SHASH_FOR_EACH (node, &sh) {
            ds_put_format(&ds, "%s: %s\n", node->name, (char *)node->data);
        }

        data.ds = &ds;
        data.iface = iface;
        error = netdev_dump_queues(iface->netdev, qos_unixctl_show_cb, &data);

        if (error) {
            ds_put_format(&ds, "failed to dump queues: %s", strerror(error));
        }
        unixctl_command_reply(conn, 200, ds_cstr(&ds));
    } else {
        ds_put_format(&ds, "QoS not configured on %s\n", iface->name);
        unixctl_command_reply(conn, 501, ds_cstr(&ds));
    }

    shash_destroy_free_data(&sh);
    ds_destroy(&ds);
}

/* Bridge reconfiguration functions. */
static struct bridge *
bridge_create(const struct ovsrec_bridge *br_cfg)
{
    struct bridge *br;
    int error;

    assert(!bridge_lookup(br_cfg->name));
    br = xzalloc(sizeof *br);

    error = dpif_create_and_open(br_cfg->name, br_cfg->datapath_type,
                                 &br->dpif);
    if (error) {
        free(br);
        return NULL;
    }
    dpif_flow_flush(br->dpif);

    error = ofproto_create(br_cfg->name, br_cfg->datapath_type, &bridge_ofhooks,
                           br, &br->ofproto);
    if (error) {
        VLOG_ERR("failed to create switch %s: %s", br_cfg->name,
                 strerror(error));
        dpif_delete(br->dpif);
        dpif_close(br->dpif);
        free(br);
        return NULL;
    }

    br->name = xstrdup(br_cfg->name);
    br->cfg = br_cfg;
    br->ml = mac_learning_create();
    eth_addr_nicira_random(br->default_ea);

    hmap_init(&br->ports);
    hmap_init(&br->ifaces);
    shash_init(&br->iface_by_name);

    br->flush = false;

    list_push_back(&all_bridges, &br->node);

    VLOG_INFO("created bridge %s on %s", br->name, dpif_name(br->dpif));

    return br;
}

static void
bridge_destroy(struct bridge *br)
{
    if (br) {
        struct port *port, *next;
        int error;

        HMAP_FOR_EACH_SAFE (port, next, hmap_node, &br->ports) {
            port_destroy(port);
        }
        list_remove(&br->node);
        ofproto_destroy(br->ofproto);
        error = dpif_delete(br->dpif);
        if (error && error != ENOENT) {
            VLOG_ERR("failed to delete %s: %s",
                     dpif_name(br->dpif), strerror(error));
        }
        dpif_close(br->dpif);
        mac_learning_destroy(br->ml);
        hmap_destroy(&br->ifaces);
        hmap_destroy(&br->ports);
        shash_destroy(&br->iface_by_name);
        free(br->name);
        free(br);
    }
}

static struct bridge *
bridge_lookup(const char *name)
{
    struct bridge *br;

    LIST_FOR_EACH (br, node, &all_bridges) {
        if (!strcmp(br->name, name)) {
            return br;
        }
    }
    return NULL;
}

/* Handle requests for a listing of all flows known by the OpenFlow
 * stack, including those normally hidden. */
static void
bridge_unixctl_dump_flows(struct unixctl_conn *conn,
                          const char *args, void *aux OVS_UNUSED)
{
    struct bridge *br;
    struct ds results;

    br = bridge_lookup(args);
    if (!br) {
        unixctl_command_reply(conn, 501, "Unknown bridge");
        return;
    }

    ds_init(&results);
    ofproto_get_all_flows(br->ofproto, &results);

    unixctl_command_reply(conn, 200, ds_cstr(&results));
    ds_destroy(&results);
}

/* "bridge/reconnect [BRIDGE]": makes BRIDGE drop all of its controller
 * connections and reconnect.  If BRIDGE is not specified, then all bridges
 * drop their controller connections and reconnect. */
static void
bridge_unixctl_reconnect(struct unixctl_conn *conn,
                         const char *args, void *aux OVS_UNUSED)
{
    struct bridge *br;
    if (args[0] != '\0') {
        br = bridge_lookup(args);
        if (!br) {
            unixctl_command_reply(conn, 501, "Unknown bridge");
            return;
        }
        ofproto_reconnect_controllers(br->ofproto);
    } else {
        LIST_FOR_EACH (br, node, &all_bridges) {
            ofproto_reconnect_controllers(br->ofproto);
        }
    }
    unixctl_command_reply(conn, 200, NULL);
}

static int
bridge_run_one(struct bridge *br)
{
    struct port *port;
    int error;

    error = ofproto_run1(br->ofproto);
    if (error) {
        return error;
    }

    mac_learning_run(br->ml, ofproto_get_revalidate_set(br->ofproto));

    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        port_run(port);
    }

    error = ofproto_run2(br->ofproto, br->flush);
    br->flush = false;

    return error;
}

static size_t
bridge_get_controllers(const struct bridge *br,
                       struct ovsrec_controller ***controllersp)
{
    struct ovsrec_controller **controllers;
    size_t n_controllers;

    controllers = br->cfg->controller;
    n_controllers = br->cfg->n_controller;

    if (n_controllers == 1 && !strcmp(controllers[0]->target, "none")) {
        controllers = NULL;
        n_controllers = 0;
    }

    if (controllersp) {
        *controllersp = controllers;
    }
    return n_controllers;
}

static void
bridge_reconfigure_one(struct bridge *br)
{
    enum ofproto_fail_mode fail_mode;
    struct port *port, *next;
    struct shash_node *node;
    struct shash new_ports;
    size_t i;

    /* Collect new ports. */
    shash_init(&new_ports);
    for (i = 0; i < br->cfg->n_ports; i++) {
        const char *name = br->cfg->ports[i]->name;
        if (!shash_add_once(&new_ports, name, br->cfg->ports[i])) {
            VLOG_WARN("bridge %s: %s specified twice as bridge port",
                      br->name, name);
        }
    }

    /* If we have a controller, then we need a local port.  Complain if the
     * user didn't specify one.
     *
     * XXX perhaps we should synthesize a port ourselves in this case. */
    if (bridge_get_controllers(br, NULL)) {
        char local_name[IF_NAMESIZE];
        int error;

        error = dpif_port_get_name(br->dpif, ODPP_LOCAL,
                                   local_name, sizeof local_name);
        if (!error && !shash_find(&new_ports, local_name)) {
            VLOG_WARN("bridge %s: controller specified but no local port "
                      "(port named %s) defined",
                      br->name, local_name);
        }
    }

    /* Get rid of deleted ports.
     * Get rid of deleted interfaces on ports that still exist. */
    HMAP_FOR_EACH_SAFE (port, next, hmap_node, &br->ports) {
        const struct ovsrec_port *port_cfg;

        port_cfg = shash_find_data(&new_ports, port->name);
        if (!port_cfg) {
            port_destroy(port);
        } else {
            port_del_ifaces(port, port_cfg);
        }
    }

    /* Create new ports.
     * Add new interfaces to existing ports.
     * Reconfigure existing ports. */
    SHASH_FOR_EACH (node, &new_ports) {
        struct port *port = port_lookup(br, node->name);
        if (!port) {
            port = port_create(br, node->name);
        }

        port_reconfigure(port, node->data);
        if (!port->n_ifaces) {
            VLOG_WARN("bridge %s: port %s has no interfaces, dropping",
                      br->name, port->name);
            port_destroy(port);
        }
    }
    shash_destroy(&new_ports);

    /* Set the fail-mode */
    fail_mode = !br->cfg->fail_mode
                || !strcmp(br->cfg->fail_mode, "standalone")
                    ? OFPROTO_FAIL_STANDALONE
                    : OFPROTO_FAIL_SECURE;
    if (ofproto_get_fail_mode(br->ofproto) != fail_mode
        && !ofproto_has_primary_controller(br->ofproto)) {
        ofproto_flush_flows(br->ofproto);
    }
    ofproto_set_fail_mode(br->ofproto, fail_mode);

    /* Delete all flows if we're switching from connected to standalone or vice
     * versa.  (XXX Should we delete all flows if we are switching from one
     * controller to another?) */

    /* Configure OpenFlow controller connection snooping. */
    if (!ofproto_has_snoops(br->ofproto)) {
        struct sset snoops;

        sset_init(&snoops);
        sset_add_and_free(&snoops, xasprintf("punix:%s/%s.snoop",
                                             ovs_rundir(), br->name));
        ofproto_set_snoops(br->ofproto, &snoops);
        sset_destroy(&snoops);
    }

    mirror_reconfigure(br);
}

/* Initializes 'oc' appropriately as a management service controller for
 * 'br'.
 *
 * The caller must free oc->target when it is no longer needed. */
static void
bridge_ofproto_controller_for_mgmt(const struct bridge *br,
                                   struct ofproto_controller *oc)
{
    oc->target = xasprintf("punix:%s/%s.mgmt", ovs_rundir(), br->name);
    oc->max_backoff = 0;
    oc->probe_interval = 60;
    oc->band = OFPROTO_OUT_OF_BAND;
    oc->rate_limit = 0;
    oc->burst_limit = 0;
}

/* Converts ovsrec_controller 'c' into an ofproto_controller in 'oc'.  */
static void
bridge_ofproto_controller_from_ovsrec(const struct ovsrec_controller *c,
                                      struct ofproto_controller *oc)
{
    oc->target = c->target;
    oc->max_backoff = c->max_backoff ? *c->max_backoff / 1000 : 8;
    oc->probe_interval = c->inactivity_probe ? *c->inactivity_probe / 1000 : 5;
    oc->band = (!c->connection_mode || !strcmp(c->connection_mode, "in-band")
                ? OFPROTO_IN_BAND : OFPROTO_OUT_OF_BAND);
    oc->rate_limit = c->controller_rate_limit ? *c->controller_rate_limit : 0;
    oc->burst_limit = (c->controller_burst_limit
                       ? *c->controller_burst_limit : 0);
}

/* Configures the IP stack for 'br''s local interface properly according to the
 * configuration in 'c'.  */
static void
bridge_configure_local_iface_netdev(struct bridge *br,
                                    struct ovsrec_controller *c)
{
    struct netdev *netdev;
    struct in_addr mask, gateway;

    struct iface *local_iface;
    struct in_addr ip;

    /* If there's no local interface or no IP address, give up. */
    local_iface = iface_from_dp_ifidx(br, ODPP_LOCAL);
    if (!local_iface || !c->local_ip || !inet_aton(c->local_ip, &ip)) {
        return;
    }

    /* Bring up the local interface. */
    netdev = local_iface->netdev;
    netdev_turn_flags_on(netdev, NETDEV_UP, true);

    /* Configure the IP address and netmask. */
    if (!c->local_netmask
        || !inet_aton(c->local_netmask, &mask)
        || !mask.s_addr) {
        mask.s_addr = guess_netmask(ip.s_addr);
    }
    if (!netdev_set_in4(netdev, ip, mask)) {
        VLOG_INFO("bridge %s: configured IP address "IP_FMT", netmask "IP_FMT,
                  br->name, IP_ARGS(&ip.s_addr), IP_ARGS(&mask.s_addr));
    }

    /* Configure the default gateway. */
    if (c->local_gateway
        && inet_aton(c->local_gateway, &gateway)
        && gateway.s_addr) {
        if (!netdev_add_router(netdev, gateway)) {
            VLOG_INFO("bridge %s: configured gateway "IP_FMT,
                      br->name, IP_ARGS(&gateway.s_addr));
        }
    }
}

static void
bridge_reconfigure_remotes(struct bridge *br,
                           const struct sockaddr_in *managers,
                           size_t n_managers)
{
    const char *disable_ib_str, *queue_id_str;
    bool disable_in_band = false;
    int queue_id;

    struct ovsrec_controller **controllers;
    size_t n_controllers;
    bool had_primary;

    struct ofproto_controller *ocs;
    size_t n_ocs;
    size_t i;

    /* Check if we should disable in-band control on this bridge. */
    disable_ib_str = bridge_get_other_config(br->cfg, "disable-in-band");
    if (disable_ib_str && !strcmp(disable_ib_str, "true")) {
        disable_in_band = true;
    }

    /* Set OpenFlow queue ID for in-band control. */
    queue_id_str = bridge_get_other_config(br->cfg, "in-band-queue");
    queue_id = queue_id_str ? strtol(queue_id_str, NULL, 10) : -1;
    ofproto_set_in_band_queue(br->ofproto, queue_id);

    if (disable_in_band) {
        ofproto_set_extra_in_band_remotes(br->ofproto, NULL, 0);
    } else {
        ofproto_set_extra_in_band_remotes(br->ofproto, managers, n_managers);
    }
    had_primary = ofproto_has_primary_controller(br->ofproto);

    n_controllers = bridge_get_controllers(br, &controllers);

    ocs = xmalloc((n_controllers + 1) * sizeof *ocs);
    n_ocs = 0;

    bridge_ofproto_controller_for_mgmt(br, &ocs[n_ocs++]);
    for (i = 0; i < n_controllers; i++) {
        struct ovsrec_controller *c = controllers[i];

        if (!strncmp(c->target, "punix:", 6)
            || !strncmp(c->target, "unix:", 5)) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

            /* Prevent remote ovsdb-server users from accessing arbitrary Unix
             * domain sockets and overwriting arbitrary local files. */
            VLOG_ERR_RL(&rl, "%s: not adding Unix domain socket controller "
                        "\"%s\" due to possibility for remote exploit",
                        dpif_name(br->dpif), c->target);
            continue;
        }

        bridge_configure_local_iface_netdev(br, c);
        bridge_ofproto_controller_from_ovsrec(c, &ocs[n_ocs]);
        if (disable_in_band) {
            ocs[n_ocs].band = OFPROTO_OUT_OF_BAND;
        }
        n_ocs++;
    }

    ofproto_set_controllers(br->ofproto, ocs, n_ocs);
    free(ocs[0].target); /* From bridge_ofproto_controller_for_mgmt(). */
    free(ocs);

    if (had_primary != ofproto_has_primary_controller(br->ofproto)) {
        ofproto_flush_flows(br->ofproto);
    }

    /* If there are no controllers and the bridge is in standalone
     * mode, set up a flow that matches every packet and directs
     * them to OFPP_NORMAL (which goes to us).  Otherwise, the
     * switch is in secure mode and we won't pass any traffic until
     * a controller has been defined and it tells us to do so. */
    if (!n_controllers
        && ofproto_get_fail_mode(br->ofproto) == OFPROTO_FAIL_STANDALONE) {
        union ofp_action action;
        struct cls_rule rule;

        memset(&action, 0, sizeof action);
        action.type = htons(OFPAT_OUTPUT);
        action.output.len = htons(sizeof action);
        action.output.port = htons(OFPP_NORMAL);
        cls_rule_init_catchall(&rule, 0);
        ofproto_add_flow(br->ofproto, &rule, &action, 1);
    }
}

static void
bridge_get_all_ifaces(const struct bridge *br, struct shash *ifaces)
{
    struct port *port;

    shash_init(ifaces);
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        struct iface *iface;

        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            shash_add_once(ifaces, iface->name, iface);
        }
        if (port->n_ifaces > 1 && port->cfg->bond_fake_iface) {
            shash_add_once(ifaces, port->name, NULL);
        }
    }
}

/* For robustness, in case the administrator moves around datapath ports behind
 * our back, we re-check all the datapath port numbers here.
 *
 * This function will set the 'dp_ifidx' members of interfaces that have
 * disappeared to -1, so only call this function from a context where those
 * 'struct iface's will be removed from the bridge.  Otherwise, the -1
 * 'dp_ifidx'es will cause trouble later when we try to send them to the
 * datapath, which doesn't support UINT16_MAX+1 ports. */
static void
bridge_fetch_dp_ifaces(struct bridge *br)
{
    struct dpif_port_dump dump;
    struct dpif_port dpif_port;
    struct port *port;

    /* Reset all interface numbers. */
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        struct iface *iface;

        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            iface->dp_ifidx = -1;
        }
    }
    hmap_clear(&br->ifaces);

    DPIF_PORT_FOR_EACH (&dpif_port, &dump, br->dpif) {
        struct iface *iface = iface_lookup(br, dpif_port.name);
        if (iface) {
            if (iface->dp_ifidx >= 0) {
                VLOG_WARN("%s reported interface %s twice",
                          dpif_name(br->dpif), dpif_port.name);
            } else if (iface_from_dp_ifidx(br, dpif_port.port_no)) {
                VLOG_WARN("%s reported interface %"PRIu16" twice",
                          dpif_name(br->dpif), dpif_port.port_no);
            } else {
                iface->dp_ifidx = dpif_port.port_no;
                hmap_insert(&br->ifaces, &iface->dp_ifidx_node,
                            hash_int(iface->dp_ifidx, 0));
            }

            iface_set_ofport(iface->cfg,
                             (iface->dp_ifidx >= 0
                              ? odp_port_to_ofp_port(iface->dp_ifidx)
                              : -1));
        }
    }
}

/* Bridge packet processing functions. */

static bool
bond_is_tcp_hash(const struct port *port)
{
    return port->bond_mode == BM_TCP && lacp_negotiated(port->lacp);
}

static int
bond_hash_src(const uint8_t mac[ETH_ADDR_LEN], uint16_t vlan)
{
    return hash_bytes(mac, ETH_ADDR_LEN, vlan) & BOND_MASK;
}

static int bond_hash_tcp(const struct flow *flow, uint16_t vlan)
{
    struct flow hash_flow;

    memcpy(&hash_flow, flow, sizeof hash_flow);
    hash_flow.vlan_tci = 0;

    /* The symmetric quality of this hash function is not required, but
     * flow_hash_symmetric_l4 already exists, and is sufficient for our
     * purposes, so we use it out of convenience. */
    return flow_hash_symmetric_l4(&hash_flow, vlan) & BOND_MASK;
}

static struct bond_entry *
lookup_bond_entry(const struct port *port, const struct flow *flow,
                  uint16_t vlan)
{
    assert(port->bond_mode != BM_AB);

    if (bond_is_tcp_hash(port)) {
        return &port->bond_hash[bond_hash_tcp(flow, vlan)];
    } else {
        return &port->bond_hash[bond_hash_src(flow->dl_src, vlan)];
    }
}

static struct iface *
bond_choose_iface(const struct port *port)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);
    struct iface *best_down_slave;
    struct iface *iface;

    best_down_slave = NULL;
    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        if (iface->enabled) {
            return iface;
        } else if ((!best_down_slave
                    || iface->delay_expires < best_down_slave->delay_expires)
                   && lacp_slave_may_enable(port->lacp, iface)) {
            best_down_slave = iface;
        }
    }

    if (best_down_slave) {
        VLOG_INFO_RL(&rl, "interface %s: skipping remaining %lli ms updelay "
                     "since no other interface is up",
                     best_down_slave->name,
                     best_down_slave->delay_expires - time_msec());
        bond_enable_slave(best_down_slave, true);
    }

    return best_down_slave;
}

static bool
choose_output_iface(const struct port *port, const struct flow *flow,
                    uint16_t vlan, uint16_t *dp_ifidx, tag_type *tags)
{
    struct iface *iface;

    assert(port->n_ifaces);
    if (port->n_ifaces == 1) {
        iface = port_get_an_iface(port);
    } else if (port->bond_mode == BM_AB) {
        iface = port->active_iface;
        if (!iface) {
            *tags |= port->no_ifaces_tag;
            return false;
        }
    } else {
        struct bond_entry *e = lookup_bond_entry(port, flow, vlan);
        if (!e->iface || !e->iface->enabled) {
            /* XXX select interface properly.  The current interface selection
             * is only good for testing the rebalancing code. */
            e->iface = bond_choose_iface(port);
            if (!e->iface) {
                *tags |= port->no_ifaces_tag;
                return false;
            }
            e->tag = tag_create_random();
        }
        *tags |= e->tag;
        iface = e->iface;
    }
    *dp_ifidx = iface->dp_ifidx;
    *tags |= iface->tag;        /* Currently only used for bonding. */
    return true;
}

static void
bond_link_status_update(struct iface *iface)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);
    struct port *port = iface->port;
    bool up = iface->up && lacp_slave_may_enable(port->lacp, iface);
    int updelay, downdelay;

    updelay = port->updelay;
    downdelay = port->downdelay;

    if (lacp_negotiated(port->lacp)) {
        downdelay = 0;
        updelay = 0;
    }

    if ((up == iface->enabled) == (iface->delay_expires == LLONG_MAX)) {
        /* Nothing to do. */
        return;
    }
    VLOG_INFO_RL(&rl, "interface %s: link state %s",
                 iface->name, up ? "up" : "down");
    if (up == iface->enabled) {
        iface->delay_expires = LLONG_MAX;
        VLOG_INFO_RL(&rl, "interface %s: will not be %s",
                     iface->name, up ? "disabled" : "enabled");
    } else if (up && !port->active_iface) {
        bond_enable_slave(iface, true);
        if (updelay) {
            VLOG_INFO_RL(&rl, "interface %s: skipping %d ms updelay since no "
                         "other interface is up", iface->name, updelay);
        }
    } else {
        int delay = up ? updelay : downdelay;
        iface->delay_expires = time_msec() + delay;
        if (delay) {
            VLOG_INFO_RL(&rl,
                         "interface %s: will be %s if it stays %s for %d ms",
                         iface->name,
                         up ? "enabled" : "disabled",
                         up ? "up" : "down",
                         delay);
        }
    }
}

static void
bond_choose_active_iface(struct port *port)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

    port->active_iface = bond_choose_iface(port);
    if (port->active_iface) {
        VLOG_INFO_RL(&rl, "port %s: active interface is now %s",
                     port->name, port->active_iface->name);
    } else {
        VLOG_WARN_RL(&rl, "port %s: all ports disabled, no active interface",
                     port->name);
    }
}

static void
bond_enable_slave(struct iface *iface, bool enable)
{
    struct port *port = iface->port;
    struct bridge *br = port->bridge;

    /* This acts as a recursion check.  If the act of disabling a slave
     * causes a different slave to be enabled, the flag will allow us to
     * skip redundant work when we reenter this function.  It must be
     * cleared on exit to keep things safe with multiple bonds. */
    static bool moving_active_iface = false;

    iface->delay_expires = LLONG_MAX;
    if (enable == iface->enabled) {
        return;
    }

    iface->enabled = enable;
    if (!iface->enabled) {
        VLOG_WARN("interface %s: disabled", iface->name);
        ofproto_revalidate(br->ofproto, iface->tag);
        if (iface == port->active_iface) {
            /* Disabling a slave can lead to another slave being immediately
             * enabled if there will be no active slaves but one is waiting
             * on an updelay.  In this case we do not need to run most of the
             * code for the newly enabled slave since there was no period
             * without an active slave and it is redundant with the disabling
             * path. */
            moving_active_iface = true;
            bond_choose_active_iface(port);
        }
        bond_send_learning_packets(port);
    } else {
        VLOG_WARN("interface %s: enabled", iface->name);
        if (!port->active_iface && !moving_active_iface) {
            ofproto_revalidate(br->ofproto, port->no_ifaces_tag);
            bond_choose_active_iface(port);
            bond_send_learning_packets(port);
        }
        iface->tag = tag_create_random();
    }

    moving_active_iface = false;
}

/* Attempts to make the sum of the bond slaves' statistics appear on the fake
 * bond interface. */
static void
bond_update_fake_iface_stats(struct port *port)
{
    struct netdev_stats bond_stats;
    struct netdev *bond_dev;
    struct iface *iface;

    memset(&bond_stats, 0, sizeof bond_stats);

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        struct netdev_stats slave_stats;

        if (!netdev_get_stats(iface->netdev, &slave_stats)) {
            /* XXX: We swap the stats here because they are swapped back when
             * reported by the internal device.  The reason for this is
             * internal devices normally represent packets going into the system
             * but when used as fake bond device they represent packets leaving
             * the system.  We really should do this in the internal device
             * itself because changing it here reverses the counts from the
             * perspective of the switch.  However, the internal device doesn't
             * know what type of device it represents so we have to do it here
             * for now. */
            bond_stats.tx_packets += slave_stats.rx_packets;
            bond_stats.tx_bytes += slave_stats.rx_bytes;
            bond_stats.rx_packets += slave_stats.tx_packets;
            bond_stats.rx_bytes += slave_stats.tx_bytes;
        }
    }

    if (!netdev_open_default(port->name, &bond_dev)) {
        netdev_set_stats(bond_dev, &bond_stats);
        netdev_close(bond_dev);
    }
}

static void
bond_run(struct port *port)
{
    struct iface *iface;

    if (port->n_ifaces < 2) {
        return;
    }

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        bond_link_status_update(iface);
    }

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        if (time_msec() >= iface->delay_expires) {
            bond_enable_slave(iface, !iface->enabled);
        }
    }

    if (port->bond_fake_iface
        && time_msec() >= port->bond_next_fake_iface_update) {
        bond_update_fake_iface_stats(port);
        port->bond_next_fake_iface_update = time_msec() + 1000;
    }
}

static void
bond_wait(struct port *port)
{
    struct iface *iface;

    if (port->n_ifaces < 2) {
        return;
    }

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        if (iface->delay_expires != LLONG_MAX) {
            poll_timer_wait_until(iface->delay_expires);
        }
    }

    if (port->bond_fake_iface) {
        poll_timer_wait_until(port->bond_next_fake_iface_update);
    }
}

static bool
set_dst(struct dst *dst, const struct flow *flow,
        const struct port *in_port, const struct port *out_port,
        tag_type *tags)
{
    dst->vlan = (out_port->vlan >= 0 ? OFP_VLAN_NONE
              : in_port->vlan >= 0 ? in_port->vlan
              : flow->vlan_tci == 0 ? OFP_VLAN_NONE
              : vlan_tci_to_vid(flow->vlan_tci));
    return choose_output_iface(out_port, flow, dst->vlan,
                               &dst->dp_ifidx, tags);
}

static void
swap_dst(struct dst *p, struct dst *q)
{
    struct dst tmp = *p;
    *p = *q;
    *q = tmp;
}

/* Moves all the dsts with vlan == 'vlan' to the front of the 'n_dsts' in
 * 'dsts'.  (This may help performance by reducing the number of VLAN changes
 * that we push to the datapath.  We could in fact fully sort the array by
 * vlan, but in most cases there are at most two different vlan tags so that's
 * possibly overkill.) */
static void
partition_dsts(struct dst_set *set, int vlan)
{
    struct dst *first = set->dsts;
    struct dst *last = set->dsts + set->n;

    while (first != last) {
        /* Invariants:
         *      - All dsts < first have vlan == 'vlan'.
         *      - All dsts >= last have vlan != 'vlan'.
         *      - first < last. */
        while (first->vlan == vlan) {
            if (++first == last) {
                return;
            }
        }

        /* Same invariants, plus one additional:
         *      - first->vlan != vlan.
         */
        while (last[-1].vlan != vlan) {
            if (--last == first) {
                return;
            }
        }

        /* Same invariants, plus one additional:
         *      - last[-1].vlan == vlan.*/
        swap_dst(first++, --last);
    }
}

static int
mirror_mask_ffs(mirror_mask_t mask)
{
    BUILD_ASSERT_DECL(sizeof(unsigned int) >= sizeof(mask));
    return ffs(mask);
}

static void
dst_set_init(struct dst_set *set)
{
    set->dsts = set->builtin;
    set->n = 0;
    set->allocated = ARRAY_SIZE(set->builtin);
}

static void
dst_set_add(struct dst_set *set, const struct dst *dst)
{
    if (set->n >= set->allocated) {
        size_t new_allocated;
        struct dst *new_dsts;

        new_allocated = set->allocated * 2;
        new_dsts = xmalloc(new_allocated * sizeof *new_dsts);
        memcpy(new_dsts, set->dsts, set->n * sizeof *new_dsts);

        dst_set_free(set);

        set->dsts = new_dsts;
        set->allocated = new_allocated;
    }
    set->dsts[set->n++] = *dst;
}

static void
dst_set_free(struct dst_set *set)
{
    if (set->dsts != set->builtin) {
        free(set->dsts);
    }
}

static bool
dst_is_duplicate(const struct dst_set *set, const struct dst *test)
{
    size_t i;
    for (i = 0; i < set->n; i++) {
        if (set->dsts[i].vlan == test->vlan
            && set->dsts[i].dp_ifidx == test->dp_ifidx) {
            return true;
        }
    }
    return false;
}

static bool
port_trunks_vlan(const struct port *port, uint16_t vlan)
{
    return (port->vlan < 0
            && (!port->trunks || bitmap_is_set(port->trunks, vlan)));
}

static bool
port_includes_vlan(const struct port *port, uint16_t vlan)
{
    return vlan == port->vlan || port_trunks_vlan(port, vlan);
}

static bool
port_is_floodable(const struct port *port)
{
    struct iface *iface;

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        if (!ofproto_port_is_floodable(port->bridge->ofproto,
                                       iface->dp_ifidx)) {
            return false;
        }
    }
    return true;
}

/* Returns the tag for 'port''s active iface, or 'port''s no_ifaces_tag if
 * there is no active iface. */
static tag_type
port_get_active_iface_tag(const struct port *port)
{
    return (port->active_iface
            ? port->active_iface->tag
            : port->no_ifaces_tag);
}

/* Returns an arbitrary interface within 'port'.
 *
 * 'port' must have at least one interface. */
static struct iface *
port_get_an_iface(const struct port *port)
{
    return CONTAINER_OF(list_front(&port->ifaces), struct iface, port_elem);
}

static void
compose_dsts(const struct bridge *br, const struct flow *flow, uint16_t vlan,
             const struct port *in_port, const struct port *out_port,
             struct dst_set *set, tag_type *tags, uint16_t *nf_output_iface)
{
    mirror_mask_t mirrors = in_port->src_mirrors;
    struct dst dst;
    int flow_vlan;

    flow_vlan = vlan_tci_to_vid(flow->vlan_tci);
    if (flow_vlan == 0) {
        flow_vlan = OFP_VLAN_NONE;
    }

    if (out_port == FLOOD_PORT) {
        struct port *port;

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            if (port != in_port
                && port_is_floodable(port)
                && port_includes_vlan(port, vlan)
                && !port->is_mirror_output_port
                && set_dst(&dst, flow, in_port, port, tags)) {
                mirrors |= port->dst_mirrors;
                dst_set_add(set, &dst);
            }
        }
        *nf_output_iface = NF_OUT_FLOOD;
    } else if (out_port && set_dst(&dst, flow, in_port, out_port, tags)) {
        dst_set_add(set, &dst);
        *nf_output_iface = dst.dp_ifidx;
        mirrors |= out_port->dst_mirrors;
    }

    while (mirrors) {
        struct mirror *m = br->mirrors[mirror_mask_ffs(mirrors) - 1];
        if (!m->n_vlans || vlan_is_mirrored(m, vlan)) {
            if (m->out_port) {
                if (set_dst(&dst, flow, in_port, m->out_port, tags)
                    && !dst_is_duplicate(set, &dst)) {
                    dst_set_add(set, &dst);
                }
            } else {
                struct port *port;

                HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                    if (port_includes_vlan(port, m->out_vlan)
                        && set_dst(&dst, flow, in_port, port, tags))
                    {
                        if (port->vlan < 0) {
                            dst.vlan = m->out_vlan;
                        }
                        if (dst_is_duplicate(set, &dst)) {
                            continue;
                        }

                        /* Use the vlan tag on the original flow instead of
                         * the one passed in the vlan parameter.  This ensures
                         * that we compare the vlan from before any implicit
                         * tagging tags place. This is necessary because
                         * dst->vlan is the final vlan, after removing implicit
                         * tags. */
                        if (port == in_port && dst.vlan == flow_vlan) {
                            /* Don't send out input port on same VLAN. */
                            continue;
                        }
                        dst_set_add(set, &dst);
                    }
                }
            }
        }
        mirrors &= mirrors - 1;
    }

    partition_dsts(set, flow_vlan);
}

static void OVS_UNUSED
print_dsts(const struct dst_set *set)
{
    size_t i;

    for (i = 0; i < set->n; i++) {
        const struct dst *dst = &set->dsts[i];

        printf(">p%"PRIu16, dst->dp_ifidx);
        if (dst->vlan != OFP_VLAN_NONE) {
            printf("v%"PRIu16, dst->vlan);
        }
    }
}

static void
compose_actions(struct bridge *br, const struct flow *flow, uint16_t vlan,
                const struct port *in_port, const struct port *out_port,
                tag_type *tags, struct ofpbuf *actions,
                uint16_t *nf_output_iface)
{
    struct dst_set set;
    uint16_t cur_vlan;
    size_t i;

    dst_set_init(&set);
    compose_dsts(br, flow, vlan, in_port, out_port, &set, tags,
                 nf_output_iface);

    cur_vlan = vlan_tci_to_vid(flow->vlan_tci);
    if (cur_vlan == 0) {
        cur_vlan = OFP_VLAN_NONE;
    }
    for (i = 0; i < set.n; i++) {
        const struct dst *dst = &set.dsts[i];
        if (dst->vlan != cur_vlan) {
            if (dst->vlan == OFP_VLAN_NONE) {
                nl_msg_put_flag(actions, ODP_ACTION_ATTR_STRIP_VLAN);
            } else {
                ovs_be16 tci;
                tci = htons(dst->vlan & VLAN_VID_MASK);
                tci |= flow->vlan_tci & htons(VLAN_PCP_MASK);
                nl_msg_put_be16(actions, ODP_ACTION_ATTR_SET_DL_TCI, tci);
            }
            cur_vlan = dst->vlan;
        }
        nl_msg_put_u32(actions, ODP_ACTION_ATTR_OUTPUT, dst->dp_ifidx);
    }
    dst_set_free(&set);
}

/* Returns the effective vlan of a packet, taking into account both the
 * 802.1Q header and implicitly tagged ports.  A value of 0 indicates that
 * the packet is untagged and -1 indicates it has an invalid header and
 * should be dropped. */
static int flow_get_vlan(struct bridge *br, const struct flow *flow,
                         struct port *in_port, bool have_packet)
{
    int vlan = vlan_tci_to_vid(flow->vlan_tci);
    if (in_port->vlan >= 0) {
        if (vlan) {
            /* XXX support double tagging? */
            if (have_packet) {
                static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
                VLOG_WARN_RL(&rl, "bridge %s: dropping VLAN %d tagged "
                             "packet received on port %s configured with "
                             "implicit VLAN %"PRIu16,
                             br->name, vlan, in_port->name, in_port->vlan);
            }
            return -1;
        }
        vlan = in_port->vlan;
    } else {
        if (!port_includes_vlan(in_port, vlan)) {
            if (have_packet) {
                static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
                VLOG_WARN_RL(&rl, "bridge %s: dropping VLAN %d tagged "
                             "packet received on port %s not configured for "
                             "trunking VLAN %d",
                             br->name, vlan, in_port->name, vlan);
            }
            return -1;
        }
    }

    return vlan;
}

/* A VM broadcasts a gratuitous ARP to indicate that it has resumed after
 * migration.  Older Citrix-patched Linux DomU used gratuitous ARP replies to
 * indicate this; newer upstream kernels use gratuitous ARP requests. */
static bool
is_gratuitous_arp(const struct flow *flow)
{
    return (flow->dl_type == htons(ETH_TYPE_ARP)
            && eth_addr_is_broadcast(flow->dl_dst)
            && (flow->nw_proto == ARP_OP_REPLY
                || (flow->nw_proto == ARP_OP_REQUEST
                    && flow->nw_src == flow->nw_dst)));
}

static void
update_learning_table(struct bridge *br, const struct flow *flow, int vlan,
                      struct port *in_port)
{
    struct mac_entry *mac;

    if (!mac_learning_may_learn(br->ml, flow->dl_src, vlan)) {
        return;
    }

    mac = mac_learning_insert(br->ml, flow->dl_src, vlan);
    if (is_gratuitous_arp(flow)) {
        /* We don't want to learn from gratuitous ARP packets that are
         * reflected back over bond slaves so we lock the learning table. */
        if (in_port->n_ifaces == 1) {
            mac_entry_set_grat_arp_lock(mac);
        } else if (mac_entry_is_grat_arp_locked(mac)) {
            return;
        }
    }

    if (mac_entry_is_new(mac) || mac->port.p != in_port) {
        /* The log messages here could actually be useful in debugging,
         * so keep the rate limit relatively high. */
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(30, 300);
        VLOG_DBG_RL(&rl, "bridge %s: learned that "ETH_ADDR_FMT" is "
                    "on port %s in VLAN %d",
                    br->name, ETH_ADDR_ARGS(flow->dl_src),
                    in_port->name, vlan);

        mac->port.p = in_port;
        ofproto_revalidate(br->ofproto, mac_learning_changed(br->ml, mac));
    }
}

/* Determines whether packets in 'flow' within 'br' should be forwarded or
 * dropped.  Returns true if they may be forwarded, false if they should be
 * dropped.
 *
 * If 'have_packet' is true, it indicates that the caller is processing a
 * received packet.  If 'have_packet' is false, then the caller is just
 * revalidating an existing flow because configuration has changed.  Either
 * way, 'have_packet' only affects logging (there is no point in logging errors
 * during revalidation).
 *
 * Sets '*in_portp' to the input port.  This will be a null pointer if
 * flow->in_port does not designate a known input port (in which case
 * is_admissible() returns false).
 *
 * When returning true, sets '*vlanp' to the effective VLAN of the input
 * packet, as returned by flow_get_vlan().
 *
 * May also add tags to '*tags', although the current implementation only does
 * so in one special case.
 */
static bool
is_admissible(struct bridge *br, const struct flow *flow, bool have_packet,
              tag_type *tags, int *vlanp, struct port **in_portp)
{
    struct iface *in_iface;
    struct port *in_port;
    int vlan;

    /* Find the interface and port structure for the received packet. */
    in_iface = iface_from_dp_ifidx(br, flow->in_port);
    if (!in_iface) {
        /* No interface?  Something fishy... */
        if (have_packet) {
            /* Odd.  A few possible reasons here:
             *
             * - We deleted an interface but there are still a few packets
             *   queued up from it.
             *
             * - Someone externally added an interface (e.g. with "ovs-dpctl
             *   add-if") that we don't know about.
             *
             * - Packet arrived on the local port but the local port is not
             *   one of our bridge ports.
             */
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

            VLOG_WARN_RL(&rl, "bridge %s: received packet on unknown "
                         "interface %"PRIu16, br->name, flow->in_port);
        }

        *in_portp = NULL;
        return false;
    }
    *in_portp = in_port = in_iface->port;
    *vlanp = vlan = flow_get_vlan(br, flow, in_port, have_packet);
    if (vlan < 0) {
        return false;
    }

    /* Drop frames for reserved multicast addresses. */
    if (eth_addr_is_reserved(flow->dl_dst)) {
        return false;
    }

    /* Drop frames on ports reserved for mirroring. */
    if (in_port->is_mirror_output_port) {
        if (have_packet) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
            VLOG_WARN_RL(&rl, "bridge %s: dropping packet received on port "
                         "%s, which is reserved exclusively for mirroring",
                         br->name, in_port->name);
        }
        return false;
    }

    /* When using LACP, do not accept packets from disabled interfaces. */
    if (lacp_negotiated(in_port->lacp) && !in_iface->enabled) {
        return false;
    }

    /* Packets received on non-LACP bonds need special attention to avoid
     * duplicates. */
    if (in_port->n_ifaces > 1 && !lacp_negotiated(in_port->lacp)) {
        struct mac_entry *mac;

        if (eth_addr_is_multicast(flow->dl_dst)) {
            *tags |= port_get_active_iface_tag(in_port);
            if (in_port->active_iface != in_iface) {
                /* Drop all multicast packets on inactive slaves. */
                return false;
            }
        }

        /* Drop all packets for which we have learned a different input
         * port, because we probably sent the packet on one slave and got
         * it back on the other.  Gratuitous ARP packets are an exception
         * to this rule: the host has moved to another switch.  The exception
         * to the exception is if we locked the learning table to avoid
         * reflections on bond slaves.  If this is the case, just drop the
         * packet now. */
        mac = mac_learning_lookup(br->ml, flow->dl_src, vlan, NULL);
        if (mac && mac->port.p != in_port &&
            (!is_gratuitous_arp(flow) || mac_entry_is_grat_arp_locked(mac))) {
                return false;
        }
    }

    return true;
}

/* If the composed actions may be applied to any packet in the given 'flow',
 * returns true.  Otherwise, the actions should only be applied to 'packet', or
 * not at all, if 'packet' was NULL. */
static bool
process_flow(struct bridge *br, const struct flow *flow,
             const struct ofpbuf *packet, struct ofpbuf *actions,
             tag_type *tags, uint16_t *nf_output_iface)
{
    struct port *in_port;
    struct port *out_port;
    struct mac_entry *mac;
    int vlan;

    /* Check whether we should drop packets in this flow. */
    if (!is_admissible(br, flow, packet != NULL, tags, &vlan, &in_port)) {
        out_port = NULL;
        goto done;
    }

    /* Learn source MAC (but don't try to learn from revalidation). */
    if (packet) {
        update_learning_table(br, flow, vlan, in_port);
    }

    /* Determine output port. */
    mac = mac_learning_lookup(br->ml, flow->dl_dst, vlan, tags);
    if (mac) {
        out_port = mac->port.p;
    } else if (!packet && !eth_addr_is_multicast(flow->dl_dst)) {
        /* If we are revalidating but don't have a learning entry then
         * eject the flow.  Installing a flow that floods packets opens
         * up a window of time where we could learn from a packet reflected
         * on a bond and blackhole packets before the learning table is
         * updated to reflect the correct port. */
        return false;
    } else {
        out_port = FLOOD_PORT;
    }

    /* Don't send packets out their input ports. */
    if (in_port == out_port) {
        out_port = NULL;
    }

done:
    if (in_port) {
        compose_actions(br, flow, vlan, in_port, out_port, tags, actions,
                        nf_output_iface);
    }

    return true;
}

static bool
bridge_normal_ofhook_cb(const struct flow *flow, const struct ofpbuf *packet,
                        struct ofpbuf *actions, tag_type *tags,
                        uint16_t *nf_output_iface, void *br_)
{
    struct bridge *br = br_;

    COVERAGE_INC(bridge_process_flow);
    return process_flow(br, flow, packet, actions, tags, nf_output_iface);
}

static bool
bridge_special_ofhook_cb(const struct flow *flow,
                         const struct ofpbuf *packet, void *br_)
{
    struct iface *iface;
    struct bridge *br = br_;

    iface = iface_from_dp_ifidx(br, flow->in_port);

    if (flow->dl_type == htons(ETH_TYPE_LACP)) {

        if (iface && iface->port->lacp && packet) {
            const struct lacp_pdu *pdu = parse_lacp_packet(packet);

            if (pdu) {
                COVERAGE_INC(bridge_process_lacp);
                lacp_process_pdu(iface->port->lacp, iface, pdu);
            }
        }
        return false;
    }

    return true;
}

static void
bridge_account_flow_ofhook_cb(const struct flow *flow, tag_type tags,
                              const struct nlattr *actions,
                              size_t actions_len,
                              uint64_t n_bytes, void *br_)
{
    struct bridge *br = br_;
    const struct nlattr *a;
    struct port *in_port;
    tag_type dummy = 0;
    unsigned int left;
    int vlan;

    /* Feed information from the active flows back into the learning table to
     * ensure that table is always in sync with what is actually flowing
     * through the datapath.
     *
     * We test that 'tags' is nonzero to ensure that only flows that include an
     * OFPP_NORMAL action are used for learning.  This works because
     * bridge_normal_ofhook_cb() always sets a nonzero tag value. */
    if (tags && is_admissible(br, flow, false, &dummy, &vlan, &in_port)) {
        update_learning_table(br, flow, vlan, in_port);
    }

    /* Account for bond slave utilization. */
    if (!br->has_bonded_ports) {
        return;
    }
    NL_ATTR_FOR_EACH_UNSAFE (a, left, actions, actions_len) {
        if (nl_attr_type(a) == ODP_ACTION_ATTR_OUTPUT) {
            struct port *out_port = port_from_dp_ifidx(br, nl_attr_get_u32(a));
            if (out_port && out_port->n_ifaces >= 2 &&
                out_port->bond_mode != BM_AB) {
                uint16_t vlan = (flow->vlan_tci
                                 ? vlan_tci_to_vid(flow->vlan_tci)
                                 : OFP_VLAN_NONE);
                struct bond_entry *e = lookup_bond_entry(out_port, flow, vlan);
                e->tx_bytes += n_bytes;
            }
        }
    }
}

static void
bridge_account_checkpoint_ofhook_cb(void *br_)
{
    struct bridge *br = br_;
    struct port *port;
    long long int now;

    if (!br->has_bonded_ports) {
        return;
    }

    now = time_msec();
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        if (port->n_ifaces > 1 && port->bond_mode != BM_AB
            && now >= port->bond_next_rebalance) {
            port->bond_next_rebalance = now + port->bond_rebalance_interval;
            bond_rebalance_port(port);
        }
    }
}

static struct ofhooks bridge_ofhooks = {
    bridge_normal_ofhook_cb,
    bridge_special_ofhook_cb,
    bridge_account_flow_ofhook_cb,
    bridge_account_checkpoint_ofhook_cb,
};

/* Bonding functions. */

/* Statistics for a single interface on a bonded port, used for load-based
 * bond rebalancing.  */
struct slave_balance {
    struct iface *iface;        /* The interface. */
    uint64_t tx_bytes;          /* Sum of hashes[*]->tx_bytes. */

    /* All the "bond_entry"s that are assigned to this interface, in order of
     * increasing tx_bytes. */
    struct bond_entry **hashes;
    size_t n_hashes;
};

static const char *
bond_mode_to_string(enum bond_mode bm) {
    static char *bm_slb = "balance-slb";
    static char *bm_ab  = "active-backup";
    static char *bm_tcp = "balance-tcp";

    switch (bm) {
    case BM_SLB: return bm_slb;
    case BM_AB:  return bm_ab;
    case BM_TCP: return bm_tcp;
    }

    NOT_REACHED();
    return NULL;
}

/* Sorts pointers to pointers to bond_entries in ascending order by the
 * interface to which they are assigned, and within a single interface in
 * ascending order of bytes transmitted. */
static int
compare_bond_entries(const void *a_, const void *b_)
{
    const struct bond_entry *const *ap = a_;
    const struct bond_entry *const *bp = b_;
    const struct bond_entry *a = *ap;
    const struct bond_entry *b = *bp;
    if (a->iface != b->iface) {
        return a->iface > b->iface ? 1 : -1;
    } else if (a->tx_bytes != b->tx_bytes) {
        return a->tx_bytes > b->tx_bytes ? 1 : -1;
    } else {
        return 0;
    }
}

/* Sorts slave_balances so that enabled ports come first, and otherwise in
 * *descending* order by number of bytes transmitted. */
static int
compare_slave_balance(const void *a_, const void *b_)
{
    const struct slave_balance *a = a_;
    const struct slave_balance *b = b_;
    if (a->iface->enabled != b->iface->enabled) {
        return a->iface->enabled ? -1 : 1;
    } else if (a->tx_bytes != b->tx_bytes) {
        return a->tx_bytes > b->tx_bytes ? -1 : 1;
    } else {
        return 0;
    }
}

static void
swap_bals(struct slave_balance *a, struct slave_balance *b)
{
    struct slave_balance tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Restores the 'n_bals' slave_balance structures in 'bals' to sorted order
 * given that 'p' (and only 'p') might be in the wrong location.
 *
 * This function invalidates 'p', since it might now be in a different memory
 * location. */
static void
resort_bals(struct slave_balance *p,
            struct slave_balance bals[], size_t n_bals)
{
    if (n_bals > 1) {
        for (; p > bals && p->tx_bytes > p[-1].tx_bytes; p--) {
            swap_bals(p, p - 1);
        }
        for (; p < &bals[n_bals - 1] && p->tx_bytes < p[1].tx_bytes; p++) {
            swap_bals(p, p + 1);
        }
    }
}

static void
log_bals(const struct slave_balance *bals, size_t n_bals, struct port *port)
{
    if (VLOG_IS_DBG_ENABLED()) {
        struct ds ds = DS_EMPTY_INITIALIZER;
        const struct slave_balance *b;

        for (b = bals; b < bals + n_bals; b++) {
            size_t i;

            if (b > bals) {
                ds_put_char(&ds, ',');
            }
            ds_put_format(&ds, " %s %"PRIu64"kB",
                          b->iface->name, b->tx_bytes / 1024);

            if (!b->iface->enabled) {
                ds_put_cstr(&ds, " (disabled)");
            }
            if (b->n_hashes > 0) {
                ds_put_cstr(&ds, " (");
                for (i = 0; i < b->n_hashes; i++) {
                    const struct bond_entry *e = b->hashes[i];
                    if (i > 0) {
                        ds_put_cstr(&ds, " + ");
                    }
                    ds_put_format(&ds, "h%td: %"PRIu64"kB",
                                  e - port->bond_hash, e->tx_bytes / 1024);
                }
                ds_put_cstr(&ds, ")");
            }
        }
        VLOG_DBG("bond %s:%s", port->name, ds_cstr(&ds));
        ds_destroy(&ds);
    }
}

/* Shifts 'hash' from 'from' to 'to' within 'port'. */
static void
bond_shift_load(struct slave_balance *from, struct slave_balance *to,
                int hash_idx)
{
    struct bond_entry *hash = from->hashes[hash_idx];
    struct port *port = from->iface->port;
    uint64_t delta = hash->tx_bytes;

    assert(port->bond_mode != BM_AB);

    VLOG_INFO("bond %s: shift %"PRIu64"kB of load (with hash %td) "
              "from %s to %s (now carrying %"PRIu64"kB and "
              "%"PRIu64"kB load, respectively)",
              port->name, delta / 1024, hash - port->bond_hash,
              from->iface->name, to->iface->name,
              (from->tx_bytes - delta) / 1024,
              (to->tx_bytes + delta) / 1024);

    /* Delete element from from->hashes.
     *
     * We don't bother to add the element to to->hashes because not only would
     * it require more work, the only purpose it would be to allow that hash to
     * be migrated to another slave in this rebalancing run, and there is no
     * point in doing that.  */
    if (hash_idx == 0) {
        from->hashes++;
    } else {
        memmove(from->hashes + hash_idx, from->hashes + hash_idx + 1,
                (from->n_hashes - (hash_idx + 1)) * sizeof *from->hashes);
    }
    from->n_hashes--;

    /* Shift load away from 'from' to 'to'. */
    from->tx_bytes -= delta;
    to->tx_bytes += delta;

    /* Arrange for flows to be revalidated. */
    ofproto_revalidate(port->bridge->ofproto, hash->tag);
    hash->iface = to->iface;
    hash->tag = tag_create_random();
}

static void
bond_rebalance_port(struct port *port)
{
    struct slave_balance *bals;
    size_t n_bals;
    struct bond_entry *hashes[BOND_MASK + 1];
    struct slave_balance *b, *from, *to;
    struct bond_entry *e;
    struct iface *iface;
    size_t i;

    assert(port->bond_mode != BM_AB);

    /* Sets up 'bals' to describe each of the port's interfaces, sorted in
     * descending order of tx_bytes, so that bals[0] represents the most
     * heavily loaded slave and bals[n_bals - 1] represents the least heavily
     * loaded slave.
     *
     * The code is a bit tricky: to avoid dynamically allocating a 'hashes'
     * array for each slave_balance structure, we sort our local array of
     * hashes in order by slave, so that all of the hashes for a given slave
     * become contiguous in memory, and then we point each 'hashes' members of
     * a slave_balance structure to the start of a contiguous group. */
    n_bals = port->n_ifaces;
    b = bals = xmalloc(n_bals * sizeof *bals);
    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        b->iface = iface;
        b->tx_bytes = 0;
        b->hashes = NULL;
        b->n_hashes = 0;
        b++;
    }
    assert(b == &bals[n_bals]);
    for (i = 0; i <= BOND_MASK; i++) {
        hashes[i] = &port->bond_hash[i];
    }
    qsort(hashes, BOND_MASK + 1, sizeof *hashes, compare_bond_entries);
    for (i = 0; i <= BOND_MASK; i++) {
        e = hashes[i];
        if (!e->iface) {
            continue;
        }

        for (b = bals; b < &bals[n_bals]; b++) {
            if (b->iface == e->iface) {
                b->tx_bytes += e->tx_bytes;
                if (!b->hashes) {
                    b->hashes = &hashes[i];
                }
                b->n_hashes++;
                break;
            }
        }
    }
    qsort(bals, n_bals, sizeof *bals, compare_slave_balance);
    log_bals(bals, n_bals, port);

    /* Discard slaves that aren't enabled (which were sorted to the back of the
     * array earlier). */
    while (!bals[n_bals - 1].iface->enabled) {
        n_bals--;
        if (!n_bals) {
            goto exit;
        }
    }

    /* Shift load from the most-loaded slaves to the least-loaded slaves. */
    to = &bals[n_bals - 1];
    for (from = bals; from < to; ) {
        uint64_t overload = from->tx_bytes - to->tx_bytes;
        if (overload < to->tx_bytes >> 5 || overload < 100000) {
            /* The extra load on 'from' (and all less-loaded slaves), compared
             * to that of 'to' (the least-loaded slave), is less than ~3%, or
             * it is less than ~1Mbps.  No point in rebalancing. */
            break;
        } else if (from->n_hashes == 1) {
            /* 'from' only carries a single MAC hash, so we can't shift any
             * load away from it, even though we want to. */
            from++;
        } else {
            /* 'from' is carrying significantly more load than 'to', and that
             * load is split across at least two different hashes.  Pick a hash
             * to migrate to 'to' (the least-loaded slave), given that doing so
             * must decrease the ratio of the load on the two slaves by at
             * least 0.1.
             *
             * The sort order we use means that we prefer to shift away the
             * smallest hashes instead of the biggest ones.  There is little
             * reason behind this decision; we could use the opposite sort
             * order to shift away big hashes ahead of small ones. */
            bool order_swapped;

            for (i = 0; i < from->n_hashes; i++) {
                double old_ratio, new_ratio;
                uint64_t delta = from->hashes[i]->tx_bytes;

                if (delta == 0 || from->tx_bytes - delta == 0) {
                    /* Pointless move. */
                    continue;
                }

                order_swapped = from->tx_bytes - delta < to->tx_bytes + delta;

                if (to->tx_bytes == 0) {
                    /* Nothing on the new slave, move it. */
                    break;
                }

                old_ratio = (double)from->tx_bytes / to->tx_bytes;
                new_ratio = (double)(from->tx_bytes - delta) /
                            (to->tx_bytes + delta);

                if (new_ratio == 0) {
                    /* Should already be covered but check to prevent division
                     * by zero. */
                    continue;
                }

                if (new_ratio < 1) {
                    new_ratio = 1 / new_ratio;
                }

                if (old_ratio - new_ratio > 0.1) {
                    /* Would decrease the ratio, move it. */
                    break;
                }
            }
            if (i < from->n_hashes) {
                bond_shift_load(from, to, i);

                /* If the result of the migration changed the relative order of
                 * 'from' and 'to' swap them back to maintain invariants. */
                if (order_swapped) {
                    swap_bals(from, to);
                }

                /* Re-sort 'bals'.  Note that this may make 'from' and 'to'
                 * point to different slave_balance structures.  It is only
                 * valid to do these two operations in a row at all because we
                 * know that 'from' will not move past 'to' and vice versa. */
                resort_bals(from, bals, n_bals);
                resort_bals(to, bals, n_bals);
            } else {
                from++;
            }
        }
    }

    /* Implement exponentially weighted moving average.  A weight of 1/2 causes
     * historical data to decay to <1% in 7 rebalancing runs.  */
    for (e = &port->bond_hash[0]; e <= &port->bond_hash[BOND_MASK]; e++) {
        e->tx_bytes /= 2;
        if (!e->tx_bytes) {
            e->iface = NULL;
        }
    }

exit:
    free(bals);
}

static void
bond_send_learning_packets(struct port *port)
{
    struct bridge *br = port->bridge;
    struct mac_entry *e;
    struct ofpbuf packet;
    int error, n_packets, n_errors;

    if (!port->n_ifaces || !port->active_iface || bond_is_tcp_hash(port)) {
        return;
    }

    ofpbuf_init(&packet, 128);
    error = n_packets = n_errors = 0;
    LIST_FOR_EACH (e, lru_node, &br->ml->lrus) {
        tag_type tags = 0;
        uint16_t dp_ifidx;
        struct flow flow;
        int retval;

        if (e->port.p == port) {
            continue;
        }

        compose_benign_packet(&packet, "Open vSwitch Bond Failover", 0xf177,
                              e->mac);
        flow_extract(&packet, 0, ODPP_NONE, &flow);

        if (!choose_output_iface(port, &flow, e->vlan, &dp_ifidx, &tags)) {
            continue;
        }

        /* Send packet. */
        n_packets++;
        retval = ofproto_send_packet(br->ofproto, dp_ifidx, e->vlan, &packet);
        if (retval) {
            error = retval;
            n_errors++;
        }
    }
    ofpbuf_uninit(&packet);

    if (n_errors) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "bond %s: %d errors sending %d gratuitous learning "
                     "packets, last error was: %s",
                     port->name, n_errors, n_packets, strerror(error));
    } else {
        VLOG_DBG("bond %s: sent %d gratuitous learning packets",
                 port->name, n_packets);
    }
}

/* Bonding unixctl user interface functions. */

static void
bond_unixctl_list(struct unixctl_conn *conn,
                  const char *args OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct bridge *br;

    ds_put_cstr(&ds, "bridge\tbond\ttype\tslaves\n");

    LIST_FOR_EACH (br, node, &all_bridges) {
        struct port *port;

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            if (port->n_ifaces > 1) {
                struct iface *iface;

                ds_put_format(&ds, "%s\t%s\t%s\t", br->name, port->name,
                              bond_mode_to_string(port->bond_mode));
                LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                    if (&iface->port_elem != list_front(&port->ifaces)) {
                        ds_put_cstr(&ds, ", ");
                    }
                    ds_put_cstr(&ds, iface->name);
                }
                ds_put_char(&ds, '\n');
            }
        }
    }
    unixctl_command_reply(conn, 200, ds_cstr(&ds));
    ds_destroy(&ds);
}

static struct port *
bond_find(const char *name)
{
    const struct bridge *br;

    LIST_FOR_EACH (br, node, &all_bridges) {
        struct port *port;

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            if (!strcmp(port->name, name) && port->n_ifaces > 1) {
                return port;
            }
        }
    }
    return NULL;
}

static void
bond_unixctl_show(struct unixctl_conn *conn,
                  const char *args, void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct port *port;
    struct iface *iface;

    port = bond_find(args);
    if (!port) {
        unixctl_command_reply(conn, 501, "no such bond");
        return;
    }

    ds_put_format(&ds, "bond_mode: %s\n",
                  bond_mode_to_string(port->bond_mode));

    if (port->lacp) {
        ds_put_format(&ds, "lacp: %s\n",
                      port->lacp_active ? "active" : "passive");
    } else {
        ds_put_cstr(&ds, "lacp: off\n");
    }

    if (port->bond_mode != BM_AB) {
        ds_put_format(&ds, "bond-hash-algorithm: %s\n",
                      bond_is_tcp_hash(port) ? "balance-tcp" : "balance-slb");
    }


    ds_put_format(&ds, "bond-detect-mode: %s\n",
                  port->monitor ? "carrier" : "miimon");

    if (!port->monitor) {
        ds_put_format(&ds, "bond-miimon-interval: %lld\n",
                      port->miimon_interval);
    }

    ds_put_format(&ds, "updelay: %d ms\n", port->updelay);
    ds_put_format(&ds, "downdelay: %d ms\n", port->downdelay);

    if (port->bond_mode != BM_AB) {
        ds_put_format(&ds, "next rebalance: %lld ms\n",
                      port->bond_next_rebalance - time_msec());
    }

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        struct bond_entry *be;
        struct flow flow;

        /* Basic info. */
        ds_put_format(&ds, "\nslave %s: %s\n",
                      iface->name, iface->enabled ? "enabled" : "disabled");
        if (iface == port->active_iface) {
            ds_put_cstr(&ds, "\tactive slave\n");
        }
        if (iface->delay_expires != LLONG_MAX) {
            ds_put_format(&ds, "\t%s expires in %lld ms\n",
                          iface->enabled ? "downdelay" : "updelay",
                          iface->delay_expires - time_msec());
        }

        if (port->bond_mode == BM_AB) {
            continue;
        }

        /* Hashes. */
        memset(&flow, 0, sizeof flow);
        for (be = port->bond_hash; be <= &port->bond_hash[BOND_MASK]; be++) {
            int hash = be - port->bond_hash;
            struct mac_entry *me;

            if (be->iface != iface) {
                continue;
            }

            ds_put_format(&ds, "\thash %d: %"PRIu64" kB load\n",
                          hash, be->tx_bytes / 1024);

            if (port->bond_mode != BM_SLB) {
                continue;
            }

            /* MACs. */
            LIST_FOR_EACH (me, lru_node, &port->bridge->ml->lrus) {
                uint16_t dp_ifidx;
                tag_type tags = 0;

                memcpy(flow.dl_src, me->mac, ETH_ADDR_LEN);
                if (bond_hash_src(me->mac, me->vlan) == hash
                    && me->port.p != port
                    && choose_output_iface(port, &flow, me->vlan,
                                           &dp_ifidx, &tags)
                    && dp_ifidx == iface->dp_ifidx)
                {
                    ds_put_format(&ds, "\t\t"ETH_ADDR_FMT"\n",
                                  ETH_ADDR_ARGS(me->mac));
                }
            }
        }
    }
    unixctl_command_reply(conn, 200, ds_cstr(&ds));
    ds_destroy(&ds);
}

static void
bond_unixctl_migrate(struct unixctl_conn *conn, const char *args_,
                     void *aux OVS_UNUSED)
{
    char *args = (char *) args_;
    char *save_ptr = NULL;
    char *bond_s, *hash_s, *slave_s;
    struct port *port;
    struct iface *iface;
    struct bond_entry *entry;
    int hash;

    bond_s = strtok_r(args, " ", &save_ptr);
    hash_s = strtok_r(NULL, " ", &save_ptr);
    slave_s = strtok_r(NULL, " ", &save_ptr);
    if (!slave_s) {
        unixctl_command_reply(conn, 501,
                              "usage: bond/migrate BOND HASH SLAVE");
        return;
    }

    port = bond_find(bond_s);
    if (!port) {
        unixctl_command_reply(conn, 501, "no such bond");
        return;
    }

    if (port->bond_mode != BM_SLB) {
        unixctl_command_reply(conn, 501, "not an SLB bond");
        return;
    }

    if (strspn(hash_s, "0123456789") == strlen(hash_s)) {
        hash = atoi(hash_s) & BOND_MASK;
    } else {
        unixctl_command_reply(conn, 501, "bad hash");
        return;
    }

    iface = port_lookup_iface(port, slave_s);
    if (!iface) {
        unixctl_command_reply(conn, 501, "no such slave");
        return;
    }

    if (!iface->enabled) {
        unixctl_command_reply(conn, 501, "cannot migrate to disabled slave");
        return;
    }

    entry = &port->bond_hash[hash];
    ofproto_revalidate(port->bridge->ofproto, entry->tag);
    entry->iface = iface;
    entry->tag = tag_create_random();
    unixctl_command_reply(conn, 200, "migrated");
}

static void
bond_unixctl_set_active_slave(struct unixctl_conn *conn, const char *args_,
                              void *aux OVS_UNUSED)
{
    char *args = (char *) args_;
    char *save_ptr = NULL;
    char *bond_s, *slave_s;
    struct port *port;
    struct iface *iface;

    bond_s = strtok_r(args, " ", &save_ptr);
    slave_s = strtok_r(NULL, " ", &save_ptr);
    if (!slave_s) {
        unixctl_command_reply(conn, 501,
                              "usage: bond/set-active-slave BOND SLAVE");
        return;
    }

    port = bond_find(bond_s);
    if (!port) {
        unixctl_command_reply(conn, 501, "no such bond");
        return;
    }

    iface = port_lookup_iface(port, slave_s);
    if (!iface) {
        unixctl_command_reply(conn, 501, "no such slave");
        return;
    }

    if (!iface->enabled) {
        unixctl_command_reply(conn, 501, "cannot make disabled slave active");
        return;
    }

    if (port->active_iface != iface) {
        ofproto_revalidate(port->bridge->ofproto,
                           port_get_active_iface_tag(port));
        port->active_iface = iface;
        VLOG_INFO("port %s: active interface is now %s",
                  port->name, iface->name);
        bond_send_learning_packets(port);
        unixctl_command_reply(conn, 200, "done");
    } else {
        unixctl_command_reply(conn, 200, "no change");
    }
}

static void
enable_slave(struct unixctl_conn *conn, const char *args_, bool enable)
{
    char *args = (char *) args_;
    char *save_ptr = NULL;
    char *bond_s, *slave_s;
    struct port *port;
    struct iface *iface;

    bond_s = strtok_r(args, " ", &save_ptr);
    slave_s = strtok_r(NULL, " ", &save_ptr);
    if (!slave_s) {
        unixctl_command_reply(conn, 501,
                              "usage: bond/enable/disable-slave BOND SLAVE");
        return;
    }

    port = bond_find(bond_s);
    if (!port) {
        unixctl_command_reply(conn, 501, "no such bond");
        return;
    }

    iface = port_lookup_iface(port, slave_s);
    if (!iface) {
        unixctl_command_reply(conn, 501, "no such slave");
        return;
    }

    bond_enable_slave(iface, enable);
    unixctl_command_reply(conn, 501, enable ? "enabled" : "disabled");
}

static void
bond_unixctl_enable_slave(struct unixctl_conn *conn, const char *args,
                          void *aux OVS_UNUSED)
{
    enable_slave(conn, args, true);
}

static void
bond_unixctl_disable_slave(struct unixctl_conn *conn, const char *args,
                           void *aux OVS_UNUSED)
{
    enable_slave(conn, args, false);
}

static void
bond_unixctl_hash(struct unixctl_conn *conn, const char *args_,
                  void *aux OVS_UNUSED)
{
    char *args = (char *) args_;
    uint8_t mac[ETH_ADDR_LEN];
    uint8_t hash;
    char *hash_cstr;
    unsigned int vlan;
    char *mac_s, *vlan_s;
    char *save_ptr = NULL;

    mac_s  = strtok_r(args, " ", &save_ptr);
    vlan_s = strtok_r(NULL, " ", &save_ptr);

    if (vlan_s) {
        if (sscanf(vlan_s, "%u", &vlan) != 1) {
            unixctl_command_reply(conn, 501, "invalid vlan");
            return;
        }
    } else {
        vlan = OFP_VLAN_NONE;
    }

    if (sscanf(mac_s, ETH_ADDR_SCAN_FMT, ETH_ADDR_SCAN_ARGS(mac))
        == ETH_ADDR_SCAN_COUNT) {
        hash = bond_hash_src(mac, vlan);

        hash_cstr = xasprintf("%u", hash);
        unixctl_command_reply(conn, 200, hash_cstr);
        free(hash_cstr);
    } else {
        unixctl_command_reply(conn, 501, "invalid mac");
    }
}

static void
bond_init(void)
{
    unixctl_command_register("bond/list", bond_unixctl_list, NULL);
    unixctl_command_register("bond/show", bond_unixctl_show, NULL);
    unixctl_command_register("bond/migrate", bond_unixctl_migrate, NULL);
    unixctl_command_register("bond/set-active-slave",
                             bond_unixctl_set_active_slave, NULL);
    unixctl_command_register("bond/enable-slave", bond_unixctl_enable_slave,
                             NULL);
    unixctl_command_register("bond/disable-slave", bond_unixctl_disable_slave,
                             NULL);
    unixctl_command_register("bond/hash", bond_unixctl_hash, NULL);
}

/* Port functions. */

static void
lacp_send_pdu_cb(void *aux, const struct lacp_pdu *pdu)
{
    struct iface *iface = aux;
    uint8_t ea[ETH_ADDR_LEN];
    int error;

    error = netdev_get_etheraddr(iface->netdev, ea);
    if (!error) {
        struct ofpbuf packet;
        struct lacp_pdu *packet_pdu;

        ofpbuf_init(&packet, 0);
        packet_pdu = compose_packet(&packet, eth_addr_lacp, ea, ETH_TYPE_LACP,
                                    sizeof *packet_pdu);
        memcpy(packet_pdu, pdu, sizeof *packet_pdu);
        ofproto_send_packet(iface->port->bridge->ofproto,
                            iface->dp_ifidx, 0, &packet);
        ofpbuf_uninit(&packet);
    } else {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 10);
        VLOG_ERR_RL(&rl, "iface %s: failed to obtain Ethernet address "
                    "(%s)", iface->name, strerror(error));
    }
}

static void
port_run(struct port *port)
{
    if (port->monitor) {
        char *devname;

        /* Track carrier going up and down on interfaces. */
        while (!netdev_monitor_poll(port->monitor, &devname)) {
            struct iface *iface;

            iface = port_lookup_iface(port, devname);
            if (iface) {
                iface_update_carrier(iface);
            }
            free(devname);
        }
    } else if (time_msec() >= port->miimon_next_update) {
        struct iface *iface;

        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            iface_update_carrier(iface);
        }
        port->miimon_next_update = time_msec() + port->miimon_interval;
    }

    if (port->lacp) {
        struct iface *iface;

        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            lacp_slave_enable(port->lacp, iface, iface->enabled);
        }

        lacp_run(port->lacp, lacp_send_pdu_cb);
    }

    bond_run(port);
}

static void
port_wait(struct port *port)
{
    if (port->monitor) {
        netdev_monitor_poll_wait(port->monitor);
    } else {
        poll_timer_wait_until(port->miimon_next_update);
    }

    if (port->lacp) {
        lacp_wait(port->lacp);
    }

    bond_wait(port);
}

static struct port *
port_create(struct bridge *br, const char *name)
{
    struct port *port;

    port = xzalloc(sizeof *port);
    port->bridge = br;
    port->vlan = -1;
    port->trunks = NULL;
    port->name = xstrdup(name);
    port->active_iface = NULL;
    list_init(&port->ifaces);

    hmap_insert(&br->ports, &port->hmap_node, hash_string(port->name, 0));

    VLOG_INFO("created port %s on bridge %s", port->name, br->name);
    bridge_flush(br);

    return port;
}

static const char *
get_port_other_config(const struct ovsrec_port *port, const char *key,
                      const char *default_value)
{
    const char *value;

    value = get_ovsrec_key_value(&port->header_, &ovsrec_port_col_other_config,
                                 key);
    return value ? value : default_value;
}

static const char *
get_interface_other_config(const struct ovsrec_interface *iface,
                           const char *key, const char *default_value)
{
    const char *value;

    value = get_ovsrec_key_value(&iface->header_,
                                 &ovsrec_interface_col_other_config, key);
    return value ? value : default_value;
}

static void
port_del_ifaces(struct port *port, const struct ovsrec_port *cfg)
{
    struct iface *iface, *next;
    struct sset new_ifaces;
    size_t i;

    /* Collect list of new interfaces. */
    sset_init(&new_ifaces);
    for (i = 0; i < cfg->n_interfaces; i++) {
        const char *name = cfg->interfaces[i]->name;
        sset_add(&new_ifaces, name);
    }

    /* Get rid of deleted interfaces. */
    LIST_FOR_EACH_SAFE (iface, next, port_elem, &port->ifaces) {
        if (!sset_contains(&new_ifaces, iface->name)) {
            iface_destroy(iface);
        }
    }

    sset_destroy(&new_ifaces);
}

/* Expires all MAC learning entries associated with 'port' and forces ofproto
 * to revalidate every flow. */
static void
port_flush_macs(struct port *port)
{
    struct bridge *br = port->bridge;
    struct mac_learning *ml = br->ml;
    struct mac_entry *mac, *next_mac;

    bridge_flush(br);
    LIST_FOR_EACH_SAFE (mac, next_mac, lru_node, &ml->lrus) {
        if (mac->port.p == port) {
            mac_learning_expire(ml, mac);
        }
    }
}

static void
port_reconfigure(struct port *port, const struct ovsrec_port *cfg)
{
    const char *detect_mode;
    struct sset new_ifaces;
    long long int next_rebalance, miimon_next_update, lacp_priority;
    bool need_flush = false;
    unsigned long *trunks;
    int vlan;
    size_t i;

    port->cfg = cfg;

    /* Update settings. */
    port->updelay = cfg->bond_updelay;
    if (port->updelay < 0) {
        port->updelay = 0;
    }
    port->downdelay = cfg->bond_downdelay;
    if (port->downdelay < 0) {
        port->downdelay = 0;
    }
    port->bond_rebalance_interval = atoi(
        get_port_other_config(cfg, "bond-rebalance-interval", "10000"));
    if (port->bond_rebalance_interval < 1000) {
        port->bond_rebalance_interval = 1000;
    }
    next_rebalance = time_msec() + port->bond_rebalance_interval;
    if (port->bond_next_rebalance > next_rebalance) {
        port->bond_next_rebalance = next_rebalance;
    }

    detect_mode = get_port_other_config(cfg, "bond-detect-mode",
                                        "carrier");

    netdev_monitor_destroy(port->monitor);
    port->monitor = NULL;

    if (strcmp(detect_mode, "miimon")) {
        port->monitor = netdev_monitor_create();

        if (strcmp(detect_mode, "carrier")) {
            VLOG_WARN("port %s: unsupported bond-detect-mode %s, "
                      "defaulting to carrier", port->name, detect_mode);
        }
    }

    port->miimon_interval = atoi(
        get_port_other_config(cfg, "bond-miimon-interval", "200"));
    if (port->miimon_interval < 100) {
        port->miimon_interval = 100;
    }
    miimon_next_update = time_msec() + port->miimon_interval;
    if (port->miimon_next_update > miimon_next_update) {
        port->miimon_next_update = miimon_next_update;
    }

    if (!port->cfg->bond_mode ||
        !strcmp(port->cfg->bond_mode, bond_mode_to_string(BM_SLB))) {
        port->bond_mode = BM_SLB;
    } else if (!strcmp(port->cfg->bond_mode, bond_mode_to_string(BM_AB))) {
        port->bond_mode = BM_AB;
    } else if (!strcmp(port->cfg->bond_mode, bond_mode_to_string(BM_TCP))) {
        port->bond_mode = BM_TCP;
    } else {
        port->bond_mode = BM_SLB;
        VLOG_WARN("port %s: unknown bond_mode %s, defaulting to %s",
                  port->name, port->cfg->bond_mode,
                  bond_mode_to_string(port->bond_mode));
    }

    /* Add new interfaces and update 'cfg' member of existing ones. */
    sset_init(&new_ifaces);
    for (i = 0; i < cfg->n_interfaces; i++) {
        const struct ovsrec_interface *if_cfg = cfg->interfaces[i];
        struct iface *iface;

        if (!sset_add(&new_ifaces, if_cfg->name)) {
            VLOG_WARN("port %s: %s specified twice as port interface",
                      port->name, if_cfg->name);
            iface_set_ofport(if_cfg, -1);
            continue;
        }

        iface = iface_lookup(port->bridge, if_cfg->name);
        if (iface) {
            if (iface->port != port) {
                VLOG_ERR("bridge %s: %s interface is on multiple ports, "
                         "removing from %s",
                         port->bridge->name, if_cfg->name, iface->port->name);
                continue;
            }
            iface->cfg = if_cfg;
        } else {
            iface = iface_create(port, if_cfg);
        }

        /* Determine interface type.  The local port always has type
         * "internal".  Other ports take their type from the database and
         * default to "system" if none is specified. */
        iface->type = (!strcmp(if_cfg->name, port->bridge->name) ? "internal"
                       : if_cfg->type[0] ? if_cfg->type
                       : "system");

        lacp_priority =
            atoi(get_interface_other_config(if_cfg, "lacp-port-priority",
                                            "0"));

        if (lacp_priority <= 0 || lacp_priority > UINT16_MAX) {
            iface->lacp_priority = UINT16_MAX;
        } else {
            iface->lacp_priority = lacp_priority;
        }
    }
    sset_destroy(&new_ifaces);

    port->lacp_fast = !strcmp(get_port_other_config(cfg, "lacp-time", "slow"),
                             "fast");

    lacp_priority =
        atoi(get_port_other_config(cfg, "lacp-system-priority", "0"));

    if (lacp_priority <= 0 || lacp_priority > UINT16_MAX) {
        /* Prefer bondable links if unspecified. */
        port->lacp_priority = port->n_ifaces > 1 ? UINT16_MAX - 1 : UINT16_MAX;
    } else {
        port->lacp_priority = lacp_priority;
    }

    if (!port->cfg->lacp) {
        /* XXX when LACP implementation has been sufficiently tested, enable by
         * default and make active on bonded ports. */
        lacp_destroy(port->lacp);
        port->lacp = NULL;
    } else if (!strcmp(port->cfg->lacp, "off")) {
        lacp_destroy(port->lacp);
        port->lacp = NULL;
    } else if (!strcmp(port->cfg->lacp, "active")) {
        if (!port->lacp) {
            port->lacp = lacp_create();
        }
        port->lacp_active = true;
    } else if (!strcmp(port->cfg->lacp, "passive")) {
        if (!port->lacp) {
            port->lacp = lacp_create();
        }
        port->lacp_active = false;
    } else {
        VLOG_WARN("port %s: unknown LACP mode %s",
                  port->name, port->cfg->lacp);
        lacp_destroy(port->lacp);
        port->lacp = NULL;
    }

    /* Get VLAN tag. */
    vlan = -1;
    if (cfg->tag) {
        if (port->n_ifaces < 2) {
            vlan = *cfg->tag;
            if (vlan >= 0 && vlan <= 4095) {
                VLOG_DBG("port %s: assigning VLAN tag %d", port->name, vlan);
            } else {
                vlan = -1;
            }
        } else {
            /* It's possible that bonded, VLAN-tagged ports make sense.  Maybe
             * they even work as-is.  But they have not been tested. */
            VLOG_WARN("port %s: VLAN tags not supported on bonded ports",
                      port->name);
        }
    }
    if (port->vlan != vlan) {
        port->vlan = vlan;
        need_flush = true;
    }

    /* Get trunked VLANs. */
    trunks = NULL;
    if (vlan < 0 && cfg->n_trunks) {
        size_t n_errors;

        trunks = bitmap_allocate(4096);
        n_errors = 0;
        for (i = 0; i < cfg->n_trunks; i++) {
            int trunk = cfg->trunks[i];
            if (trunk >= 0) {
                bitmap_set1(trunks, trunk);
            } else {
                n_errors++;
            }
        }
        if (n_errors) {
            VLOG_ERR("port %s: invalid values for %zu trunk VLANs",
                     port->name, cfg->n_trunks);
        }
        if (n_errors == cfg->n_trunks) {
            VLOG_ERR("port %s: no valid trunks, trunking all VLANs",
                     port->name);
            bitmap_free(trunks);
            trunks = NULL;
        }
    } else if (vlan >= 0 && cfg->n_trunks) {
        VLOG_ERR("port %s: ignoring trunks in favor of implicit vlan",
                 port->name);
    }
    if (trunks == NULL
        ? port->trunks != NULL
        : port->trunks == NULL || !bitmap_equal(trunks, port->trunks, 4096)) {
        need_flush = true;
    }
    bitmap_free(port->trunks);
    port->trunks = trunks;

    if (need_flush) {
        port_flush_macs(port);
    }
}

static void
port_destroy(struct port *port)
{
    if (port) {
        struct bridge *br = port->bridge;
        struct iface *iface, *next;
        int i;

        for (i = 0; i < MAX_MIRRORS; i++) {
            struct mirror *m = br->mirrors[i];
            if (m && m->out_port == port) {
                mirror_destroy(m);
            }
        }

        LIST_FOR_EACH_SAFE (iface, next, port_elem, &port->ifaces) {
            iface_destroy(iface);
        }

        hmap_remove(&br->ports, &port->hmap_node);

        VLOG_INFO("destroyed port %s on bridge %s", port->name, br->name);

        port_flush_macs(port);

        lacp_destroy(port->lacp);
        netdev_monitor_destroy(port->monitor);
        bitmap_free(port->trunks);
        free(port->bond_hash);
        free(port->name);
        free(port);
    }
}

static struct port *
port_from_dp_ifidx(const struct bridge *br, uint16_t dp_ifidx)
{
    struct iface *iface = iface_from_dp_ifidx(br, dp_ifidx);
    return iface ? iface->port : NULL;
}

static struct port *
port_lookup(const struct bridge *br, const char *name)
{
    struct port *port;

    HMAP_FOR_EACH_WITH_HASH (port, hmap_node, hash_string(name, 0),
                             &br->ports) {
        if (!strcmp(port->name, name)) {
            return port;
        }
    }
    return NULL;
}

static struct iface *
port_lookup_iface(const struct port *port, const char *name)
{
    struct iface *iface = iface_lookup(port->bridge, name);
    return iface && iface->port == port ? iface : NULL;
}

static void
port_update_lacp(struct port *port)
{
    if (port->lacp) {
        struct iface *iface;

        lacp_configure(port->lacp, port->name,
                       port->bridge->ea, port->lacp_priority,
                       port->lacp_active, port->lacp_fast);

        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            lacp_slave_register(port->lacp, iface, iface->name,
                                iface->dp_ifidx, iface->lacp_priority);
        }
    }
}

static void
port_update_bonding(struct port *port)
{
    if (port->n_ifaces < 2) {
        /* Not a bonded port. */
        free(port->bond_hash);
        port->bond_hash = NULL;
        port->bond_fake_iface = false;
        port->active_iface = NULL;
        port->no_ifaces_tag = 0;
    } else {
        size_t i;

        if (port->bond_mode != BM_AB && !port->bond_hash) {
            port->bond_hash = xcalloc(BOND_MASK + 1, sizeof *port->bond_hash);
            for (i = 0; i <= BOND_MASK; i++) {
                struct bond_entry *e = &port->bond_hash[i];
                e->iface = NULL;
                e->tx_bytes = 0;
            }
            port->bond_next_rebalance
                = time_msec() + port->bond_rebalance_interval;
        } else if (port->bond_mode == BM_AB) {
            free(port->bond_hash);
            port->bond_hash = NULL;
        }

        if (!port->no_ifaces_tag) {
            port->no_ifaces_tag = tag_create_random();
        }

        if (!port->active_iface) {
            bond_choose_active_iface(port);
        }

        port->bond_fake_iface = port->cfg->bond_fake_iface;
        if (port->bond_fake_iface) {
            port->bond_next_fake_iface_update = time_msec();
        }

    }
}

/* Interface functions. */

static struct iface *
iface_create(struct port *port, const struct ovsrec_interface *if_cfg)
{
    struct bridge *br = port->bridge;
    struct iface *iface;
    char *name = if_cfg->name;

    iface = xzalloc(sizeof *iface);
    iface->port = port;
    iface->name = xstrdup(name);
    iface->dp_ifidx = -1;
    iface->tag = tag_create_random();
    iface->delay_expires = LLONG_MAX;
    iface->netdev = NULL;
    iface->cfg = if_cfg;

    shash_add_assert(&br->iface_by_name, iface->name, iface);

    list_push_back(&port->ifaces, &iface->port_elem);
    port->n_ifaces++;

    if (port->n_ifaces > 1) {
        br->has_bonded_ports = true;
    }

    VLOG_DBG("attached network device %s to port %s", iface->name, port->name);

    bridge_flush(br);

    return iface;
}

static void
iface_destroy(struct iface *iface)
{
    if (iface) {
        struct port *port = iface->port;
        struct bridge *br = port->bridge;
        bool del_active = port->active_iface == iface;

        if (port->bond_hash) {
            struct bond_entry *e;
            for (e = port->bond_hash; e <= &port->bond_hash[BOND_MASK]; e++) {
                if (e->iface == iface) {
                    e->iface = NULL;
                }
            }
        }

        if (iface->port->lacp) {
            lacp_slave_unregister(iface->port->lacp, iface);
        }

        if (port->monitor && iface->netdev) {
            netdev_monitor_remove(port->monitor, iface->netdev);
        }

        shash_find_and_delete_assert(&br->iface_by_name, iface->name);

        if (iface->dp_ifidx >= 0) {
            hmap_remove(&br->ifaces, &iface->dp_ifidx_node);
        }

        list_remove(&iface->port_elem);
        port->n_ifaces--;

        netdev_close(iface->netdev);

        if (del_active) {
            bond_choose_active_iface(port);
            bond_send_learning_packets(port);
        }

        free(iface->name);
        free(iface);

        bridge_flush(port->bridge);
    }
}

static struct iface *
iface_lookup(const struct bridge *br, const char *name)
{
    return shash_find_data(&br->iface_by_name, name);
}

static struct iface *
iface_find(const char *name)
{
    const struct bridge *br;

    LIST_FOR_EACH (br, node, &all_bridges) {
        struct iface *iface = iface_lookup(br, name);

        if (iface) {
            return iface;
        }
    }
    return NULL;
}

static struct iface *
iface_from_dp_ifidx(const struct bridge *br, uint16_t dp_ifidx)
{
    struct iface *iface;

    HMAP_FOR_EACH_IN_BUCKET (iface, dp_ifidx_node,
                             hash_int(dp_ifidx, 0), &br->ifaces) {
        if (iface->dp_ifidx == dp_ifidx) {
            return iface;
        }
    }
    return NULL;
}

/* Set Ethernet address of 'iface', if one is specified in the configuration
 * file. */
static void
iface_set_mac(struct iface *iface)
{
    uint8_t ea[ETH_ADDR_LEN];

    if (iface->cfg->mac && eth_addr_from_string(iface->cfg->mac, ea)) {
        if (eth_addr_is_multicast(ea)) {
            VLOG_ERR("interface %s: cannot set MAC to multicast address",
                     iface->name);
        } else if (iface->dp_ifidx == ODPP_LOCAL) {
            VLOG_ERR("ignoring iface.%s.mac; use bridge.%s.mac instead",
                     iface->name, iface->name);
        } else {
            int error = netdev_set_etheraddr(iface->netdev, ea);
            if (error) {
                VLOG_ERR("interface %s: setting MAC failed (%s)",
                         iface->name, strerror(error));
            }
        }
    }
}

/* Sets the ofport column of 'if_cfg' to 'ofport'. */
static void
iface_set_ofport(const struct ovsrec_interface *if_cfg, int64_t ofport)
{
    if (if_cfg) {
        ovsrec_interface_set_ofport(if_cfg, &ofport, 1);
    }
}

/* Adds the 'n' key-value pairs in 'keys' in 'values' to 'shash'.
 *
 * The value strings in '*shash' are taken directly from values[], not copied,
 * so the caller should not modify or free them. */
static void
shash_from_ovs_idl_map(char **keys, char **values, size_t n,
                       struct shash *shash)
{
    size_t i;

    shash_init(shash);
    for (i = 0; i < n; i++) {
        shash_add(shash, keys[i], values[i]);
    }
}

/* Creates 'keys' and 'values' arrays from 'shash'.
 *
 * Sets 'keys' and 'values' to heap allocated arrays representing the key-value
 * pairs in 'shash'.  The caller takes ownership of 'keys' and 'values'.  They
 * are populated with with strings taken directly from 'shash' and thus have
 * the same ownership of the key-value pairs in shash.
 */
static void
shash_to_ovs_idl_map(struct shash *shash,
                     char ***keys, char ***values, size_t *n)
{
    size_t i, count;
    char **k, **v;
    struct shash_node *sn;

    count = shash_count(shash);

    k = xmalloc(count * sizeof *k);
    v = xmalloc(count * sizeof *v);

    i = 0;
    SHASH_FOR_EACH(sn, shash) {
        k[i] = sn->name;
        v[i] = sn->data;
        i++;
    }

    *n      = count;
    *keys   = k;
    *values = v;
}

struct iface_delete_queues_cbdata {
    struct netdev *netdev;
    const struct ovsdb_datum *queues;
};

static bool
queue_ids_include(const struct ovsdb_datum *queues, int64_t target)
{
    union ovsdb_atom atom;

    atom.integer = target;
    return ovsdb_datum_find_key(queues, &atom, OVSDB_TYPE_INTEGER) != UINT_MAX;
}

static void
iface_delete_queues(unsigned int queue_id,
                    const struct shash *details OVS_UNUSED, void *cbdata_)
{
    struct iface_delete_queues_cbdata *cbdata = cbdata_;

    if (!queue_ids_include(cbdata->queues, queue_id)) {
        netdev_delete_queue(cbdata->netdev, queue_id);
    }
}

static void
iface_update_carrier(struct iface *iface)
{
    bool carrier = iface_get_carrier(iface);
    if (carrier == iface->up) {
        return;
    }

    iface->up = carrier;
    if (iface->port->lacp) {
        lacp_slave_carrier_changed(iface->port->lacp, iface);
    }
}

static void
iface_update_qos(struct iface *iface, const struct ovsrec_qos *qos)
{
    if (!qos || qos->type[0] == '\0') {
        netdev_set_qos(iface->netdev, NULL, NULL);
    } else {
        struct iface_delete_queues_cbdata cbdata;
        struct shash details;
        size_t i;

        /* Configure top-level Qos for 'iface'. */
        shash_from_ovs_idl_map(qos->key_other_config, qos->value_other_config,
                               qos->n_other_config, &details);
        netdev_set_qos(iface->netdev, qos->type, &details);
        shash_destroy(&details);

        /* Deconfigure queues that were deleted. */
        cbdata.netdev = iface->netdev;
        cbdata.queues = ovsrec_qos_get_queues(qos, OVSDB_TYPE_INTEGER,
                                              OVSDB_TYPE_UUID);
        netdev_dump_queues(iface->netdev, iface_delete_queues, &cbdata);

        /* Configure queues for 'iface'. */
        for (i = 0; i < qos->n_queues; i++) {
            const struct ovsrec_queue *queue = qos->value_queues[i];
            unsigned int queue_id = qos->key_queues[i];

            shash_from_ovs_idl_map(queue->key_other_config,
                                   queue->value_other_config,
                                   queue->n_other_config, &details);
            netdev_set_queue(iface->netdev, queue_id, &details);
            shash_destroy(&details);
        }
    }
}

static void
iface_update_cfm(struct iface *iface)
{
    size_t i;
    struct cfm cfm;
    uint16_t *remote_mps;
    struct ovsrec_monitor *mon;
    uint8_t maid[CCM_MAID_LEN];

    mon = iface->cfg->monitor;

    if (!mon) {
        ofproto_iface_clear_cfm(iface->port->bridge->ofproto, iface->dp_ifidx);
        return;
    }

    if (!cfm_generate_maid(mon->md_name, mon->ma_name, maid)) {
        VLOG_WARN("interface %s: Failed to generate MAID.", iface->name);
        return;
    }

    cfm.mpid     = mon->mpid;
    cfm.interval = mon->interval ? *mon->interval : 1000;

    memcpy(cfm.maid, maid, sizeof cfm.maid);

    remote_mps = xzalloc(mon->n_remote_mps * sizeof *remote_mps);
    for(i = 0; i < mon->n_remote_mps; i++) {
        remote_mps[i] = mon->remote_mps[i]->mpid;
    }

    ofproto_iface_set_cfm(iface->port->bridge->ofproto, iface->dp_ifidx,
                          &cfm, remote_mps, mon->n_remote_mps);
    free(remote_mps);
}

/* Read carrier or miimon status directly from 'iface''s netdev, according to
 * how 'iface''s port is configured.
 *
 * Returns true if 'iface' is up, false otherwise. */
static bool
iface_get_carrier(const struct iface *iface)
{
    return (iface->port->monitor
            ? netdev_get_carrier(iface->netdev)
            : netdev_get_miimon(iface->netdev));
}

/* Port mirroring. */

static struct mirror *
mirror_find_by_uuid(struct bridge *br, const struct uuid *uuid)
{
    int i;

    for (i = 0; i < MAX_MIRRORS; i++) {
        struct mirror *m = br->mirrors[i];
        if (m && uuid_equals(uuid, &m->uuid)) {
            return m;
        }
    }
    return NULL;
}

static void
mirror_reconfigure(struct bridge *br)
{
    unsigned long *rspan_vlans;
    struct port *port;
    int i;

    /* Get rid of deleted mirrors. */
    for (i = 0; i < MAX_MIRRORS; i++) {
        struct mirror *m = br->mirrors[i];
        if (m) {
            const struct ovsdb_datum *mc;
            union ovsdb_atom atom;

            mc = ovsrec_bridge_get_mirrors(br->cfg, OVSDB_TYPE_UUID);
            atom.uuid = br->mirrors[i]->uuid;
            if (ovsdb_datum_find_key(mc, &atom, OVSDB_TYPE_UUID) == UINT_MAX) {
                mirror_destroy(m);
            }
        }
    }

    /* Add new mirrors and reconfigure existing ones. */
    for (i = 0; i < br->cfg->n_mirrors; i++) {
        struct ovsrec_mirror *cfg = br->cfg->mirrors[i];
        struct mirror *m = mirror_find_by_uuid(br, &cfg->header_.uuid);
        if (m) {
            mirror_reconfigure_one(m, cfg);
        } else {
            mirror_create(br, cfg);
        }
    }

    /* Update port reserved status. */
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        port->is_mirror_output_port = false;
    }
    for (i = 0; i < MAX_MIRRORS; i++) {
        struct mirror *m = br->mirrors[i];
        if (m && m->out_port) {
            m->out_port->is_mirror_output_port = true;
        }
    }

    /* Update flooded vlans (for RSPAN). */
    rspan_vlans = NULL;
    if (br->cfg->n_flood_vlans) {
        rspan_vlans = bitmap_allocate(4096);

        for (i = 0; i < br->cfg->n_flood_vlans; i++) {
            int64_t vlan = br->cfg->flood_vlans[i];
            if (vlan >= 0 && vlan < 4096) {
                bitmap_set1(rspan_vlans, vlan);
                VLOG_INFO("bridge %s: disabling learning on vlan %"PRId64,
                          br->name, vlan);
            } else {
                VLOG_ERR("bridge %s: invalid value %"PRId64 "for flood VLAN",
                         br->name, vlan);
            }
        }
    }
    if (mac_learning_set_flood_vlans(br->ml, rspan_vlans)) {
        bridge_flush(br);
        mac_learning_flush(br->ml);
    }
}

static void
mirror_create(struct bridge *br, struct ovsrec_mirror *cfg)
{
    struct mirror *m;
    size_t i;

    for (i = 0; ; i++) {
        if (i >= MAX_MIRRORS) {
            VLOG_WARN("bridge %s: maximum of %d port mirrors reached, "
                      "cannot create %s", br->name, MAX_MIRRORS, cfg->name);
            return;
        }
        if (!br->mirrors[i]) {
            break;
        }
    }

    VLOG_INFO("created port mirror %s on bridge %s", cfg->name, br->name);
    bridge_flush(br);
    mac_learning_flush(br->ml);

    br->mirrors[i] = m = xzalloc(sizeof *m);
    m->bridge = br;
    m->idx = i;
    m->name = xstrdup(cfg->name);
    sset_init(&m->src_ports);
    sset_init(&m->dst_ports);
    m->vlans = NULL;
    m->n_vlans = 0;
    m->out_vlan = -1;
    m->out_port = NULL;

    mirror_reconfigure_one(m, cfg);
}

static void
mirror_destroy(struct mirror *m)
{
    if (m) {
        struct bridge *br = m->bridge;
        struct port *port;

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            port->src_mirrors &= ~(MIRROR_MASK_C(1) << m->idx);
            port->dst_mirrors &= ~(MIRROR_MASK_C(1) << m->idx);
        }

        sset_destroy(&m->src_ports);
        sset_destroy(&m->dst_ports);
        free(m->vlans);

        m->bridge->mirrors[m->idx] = NULL;
        free(m->name);
        free(m);

        bridge_flush(br);
        mac_learning_flush(br->ml);
    }
}

static void
mirror_collect_ports(struct mirror *m, struct ovsrec_port **ports, int n_ports,
                     struct sset *names)
{
    size_t i;

    for (i = 0; i < n_ports; i++) {
        const char *name = ports[i]->name;
        if (port_lookup(m->bridge, name)) {
            sset_add(names, name);
        } else {
            VLOG_WARN("bridge %s: mirror %s cannot match on nonexistent "
                      "port %s", m->bridge->name, m->name, name);
        }
    }
}

static size_t
mirror_collect_vlans(struct mirror *m, const struct ovsrec_mirror *cfg,
                     int **vlans)
{
    size_t n_vlans;
    size_t i;

    *vlans = xmalloc(sizeof **vlans * cfg->n_select_vlan);
    n_vlans = 0;
    for (i = 0; i < cfg->n_select_vlan; i++) {
        int64_t vlan = cfg->select_vlan[i];
        if (vlan < 0 || vlan > 4095) {
            VLOG_WARN("bridge %s: mirror %s selects invalid VLAN %"PRId64,
                      m->bridge->name, m->name, vlan);
        } else {
            (*vlans)[n_vlans++] = vlan;
        }
    }
    return n_vlans;
}

static bool
vlan_is_mirrored(const struct mirror *m, int vlan)
{
    size_t i;

    for (i = 0; i < m->n_vlans; i++) {
        if (m->vlans[i] == vlan) {
            return true;
        }
    }
    return false;
}

static bool
port_trunks_any_mirrored_vlan(const struct mirror *m, const struct port *p)
{
    size_t i;

    for (i = 0; i < m->n_vlans; i++) {
        if (port_trunks_vlan(p, m->vlans[i])) {
            return true;
        }
    }
    return false;
}

static void
mirror_reconfigure_one(struct mirror *m, struct ovsrec_mirror *cfg)
{
    struct sset src_ports, dst_ports;
    mirror_mask_t mirror_bit;
    struct port *out_port;
    struct port *port;
    int out_vlan;
    size_t n_vlans;
    int *vlans;

    /* Set name. */
    if (strcmp(cfg->name, m->name)) {
        free(m->name);
        m->name = xstrdup(cfg->name);
    }

    /* Get output port. */
    if (cfg->output_port) {
        out_port = port_lookup(m->bridge, cfg->output_port->name);
        if (!out_port) {
            VLOG_ERR("bridge %s: mirror %s outputs to port not on bridge",
                     m->bridge->name, m->name);
            mirror_destroy(m);
            return;
        }
        out_vlan = -1;

        if (cfg->output_vlan) {
            VLOG_ERR("bridge %s: mirror %s specifies both output port and "
                     "output vlan; ignoring output vlan",
                     m->bridge->name, m->name);
        }
    } else if (cfg->output_vlan) {
        out_port = NULL;
        out_vlan = *cfg->output_vlan;
    } else {
        VLOG_ERR("bridge %s: mirror %s does not specify output; ignoring",
                 m->bridge->name, m->name);
        mirror_destroy(m);
        return;
    }

    sset_init(&src_ports);
    sset_init(&dst_ports);
    if (cfg->select_all) {
        HMAP_FOR_EACH (port, hmap_node, &m->bridge->ports) {
            sset_add(&src_ports, port->name);
            sset_add(&dst_ports, port->name);
        }
        vlans = NULL;
        n_vlans = 0;
    } else {
        /* Get ports, and drop duplicates and ports that don't exist. */
        mirror_collect_ports(m, cfg->select_src_port, cfg->n_select_src_port,
                             &src_ports);
        mirror_collect_ports(m, cfg->select_dst_port, cfg->n_select_dst_port,
                             &dst_ports);

        /* Get all the vlans, and drop duplicate and invalid vlans. */
        n_vlans = mirror_collect_vlans(m, cfg, &vlans);
    }

    /* Update mirror data. */
    if (!sset_equals(&m->src_ports, &src_ports)
        || !sset_equals(&m->dst_ports, &dst_ports)
        || m->n_vlans != n_vlans
        || memcmp(m->vlans, vlans, sizeof *vlans * n_vlans)
        || m->out_port != out_port
        || m->out_vlan != out_vlan) {
        bridge_flush(m->bridge);
        mac_learning_flush(m->bridge->ml);
    }
    sset_swap(&m->src_ports, &src_ports);
    sset_swap(&m->dst_ports, &dst_ports);
    free(m->vlans);
    m->vlans = vlans;
    m->n_vlans = n_vlans;
    m->out_port = out_port;
    m->out_vlan = out_vlan;

    /* Update ports. */
    mirror_bit = MIRROR_MASK_C(1) << m->idx;
    HMAP_FOR_EACH (port, hmap_node, &m->bridge->ports) {
        if (sset_contains(&m->src_ports, port->name)
            || (m->n_vlans
                && (!port->vlan
                    ? port_trunks_any_mirrored_vlan(m, port)
                    : vlan_is_mirrored(m, port->vlan)))) {
            port->src_mirrors |= mirror_bit;
        } else {
            port->src_mirrors &= ~mirror_bit;
        }

        if (sset_contains(&m->dst_ports, port->name)) {
            port->dst_mirrors |= mirror_bit;
        } else {
            port->dst_mirrors &= ~mirror_bit;
        }
    }

    /* Clean up. */
    sset_destroy(&src_ports);
    sset_destroy(&dst_ports);
}
