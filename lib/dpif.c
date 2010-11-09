/*
 * Copyright (c) 2008, 2009, 2010 Nicira Networks.
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
#include "dpif-provider.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "coverage.h"
#include "dynamic-string.h"
#include "flow.h"
#include "netlink.h"
#include "odp-util.h"
#include "ofp-print.h"
#include "ofpbuf.h"
#include "packets.h"
#include "poll-loop.h"
#include "shash.h"
#include "svec.h"
#include "util.h"
#include "valgrind.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(dpif)

static const struct dpif_class *base_dpif_classes[] = {
#ifdef HAVE_NETLINK
    &dpif_linux_class,
#endif
    &dpif_netdev_class,
};

struct registered_dpif_class {
    struct dpif_class dpif_class;
    int refcount;
};
static struct shash dpif_classes = SHASH_INITIALIZER(&dpif_classes);

/* Rate limit for individual messages going to or from the datapath, output at
 * DBG level.  This is very high because, if these are enabled, it is because
 * we really need to see them. */
static struct vlog_rate_limit dpmsg_rl = VLOG_RATE_LIMIT_INIT(600, 600);

/* Not really much point in logging many dpif errors. */
static struct vlog_rate_limit error_rl = VLOG_RATE_LIMIT_INIT(60, 5);

static void log_operation(const struct dpif *, const char *operation,
                          int error);
static void log_flow_operation(const struct dpif *, const char *operation,
                               int error, struct odp_flow *flow);
static void log_flow_put(struct dpif *, int error,
                         const struct odp_flow_put *);
static bool should_log_flow_message(int error);
static void check_rw_odp_flow(struct odp_flow *);

static void
dp_initialize(void)
{
    static int status = -1;

    if (status < 0) {
        int i;

        status = 0;
        for (i = 0; i < ARRAY_SIZE(base_dpif_classes); i++) {
            dp_register_provider(base_dpif_classes[i]);
        }
    }
}

/* Performs periodic work needed by all the various kinds of dpifs.
 *
 * If your program opens any dpifs, it must call both this function and
 * netdev_run() within its main poll loop. */
void
dp_run(void)
{
    struct shash_node *node;
    SHASH_FOR_EACH(node, &dpif_classes) {
        const struct registered_dpif_class *registered_class = node->data;
        if (registered_class->dpif_class.run) {
            registered_class->dpif_class.run();
        }
    }
}

/* Arranges for poll_block() to wake up when dp_run() needs to be called.
 *
 * If your program opens any dpifs, it must call both this function and
 * netdev_wait() within its main poll loop. */
void
dp_wait(void)
{
    struct shash_node *node;
    SHASH_FOR_EACH(node, &dpif_classes) {
        const struct registered_dpif_class *registered_class = node->data;
        if (registered_class->dpif_class.wait) {
            registered_class->dpif_class.wait();
        }
    }
}

/* Registers a new datapath provider.  After successful registration, new
 * datapaths of that type can be opened using dpif_open(). */
int
dp_register_provider(const struct dpif_class *new_class)
{
    struct registered_dpif_class *registered_class;

    if (shash_find(&dpif_classes, new_class->type)) {
        VLOG_WARN("attempted to register duplicate datapath provider: %s",
                  new_class->type);
        return EEXIST;
    }

    registered_class = xmalloc(sizeof *registered_class);
    memcpy(&registered_class->dpif_class, new_class,
           sizeof registered_class->dpif_class);
    registered_class->refcount = 0;

    shash_add(&dpif_classes, new_class->type, registered_class);

    return 0;
}

/* Unregisters a datapath provider.  'type' must have been previously
 * registered and not currently be in use by any dpifs.  After unregistration
 * new datapaths of that type cannot be opened using dpif_open(). */
int
dp_unregister_provider(const char *type)
{
    struct shash_node *node;
    struct registered_dpif_class *registered_class;

    node = shash_find(&dpif_classes, type);
    if (!node) {
        VLOG_WARN("attempted to unregister a datapath provider that is not "
                  "registered: %s", type);
        return EAFNOSUPPORT;
    }

    registered_class = node->data;
    if (registered_class->refcount) {
        VLOG_WARN("attempted to unregister in use datapath provider: %s", type);
        return EBUSY;
    }

    shash_delete(&dpif_classes, node);
    free(registered_class);

    return 0;
}

/* Clears 'types' and enumerates the types of all currently registered datapath
 * providers into it.  The caller must first initialize the svec. */
void
dp_enumerate_types(struct svec *types)
{
    struct shash_node *node;

    dp_initialize();
    svec_clear(types);

    SHASH_FOR_EACH(node, &dpif_classes) {
        const struct registered_dpif_class *registered_class = node->data;
        svec_add(types, registered_class->dpif_class.type);
    }
}

/* Clears 'names' and enumerates the names of all known created datapaths with
 * the given 'type'.  The caller must first initialize the svec. Returns 0 if
 * successful, otherwise a positive errno value.
 *
 * Some kinds of datapaths might not be practically enumerable.  This is not
 * considered an error. */
int
dp_enumerate_names(const char *type, struct svec *names)
{
    const struct registered_dpif_class *registered_class;
    const struct dpif_class *dpif_class;
    int error;

    dp_initialize();
    svec_clear(names);

    registered_class = shash_find_data(&dpif_classes, type);
    if (!registered_class) {
        VLOG_WARN("could not enumerate unknown type: %s", type);
        return EAFNOSUPPORT;
    }

    dpif_class = &registered_class->dpif_class;
    error = dpif_class->enumerate ? dpif_class->enumerate(names) : 0;

    if (error) {
        VLOG_WARN("failed to enumerate %s datapaths: %s", dpif_class->type,
                   strerror(error));
    }

    return error;
}

/* Parses 'datapath name', which is of the form type@name into its
 * component pieces.  'name' and 'type' must be freed by the caller. */
void
dp_parse_name(const char *datapath_name_, char **name, char **type)
{
    char *datapath_name = xstrdup(datapath_name_);
    char *separator;

    separator = strchr(datapath_name, '@');
    if (separator) {
        *separator = '\0';
        *type = datapath_name;
        *name = xstrdup(separator + 1);
    } else {
        *name = datapath_name;
        *type = NULL;
    }
}

static int
do_open(const char *name, const char *type, bool create, struct dpif **dpifp)
{
    struct dpif *dpif = NULL;
    int error;
    struct registered_dpif_class *registered_class;

    dp_initialize();

    if (!type || *type == '\0') {
        type = "system";
    }

    registered_class = shash_find_data(&dpif_classes, type);
    if (!registered_class) {
        VLOG_WARN("could not create datapath %s of unknown type %s", name,
                  type);
        error = EAFNOSUPPORT;
        goto exit;
    }

    error = registered_class->dpif_class.open(name, type, create, &dpif);
    if (!error) {
        registered_class->refcount++;
    }

exit:
    *dpifp = error ? NULL : dpif;
    return error;
}

/* Tries to open an existing datapath named 'name' and type 'type'.  Will fail
 * if no datapath with 'name' and 'type' exists.  'type' may be either NULL or
 * the empty string to specify the default system type.  Returns 0 if
 * successful, otherwise a positive errno value.  On success stores a pointer
 * to the datapath in '*dpifp', otherwise a null pointer. */
int
dpif_open(const char *name, const char *type, struct dpif **dpifp)
{
    return do_open(name, type, false, dpifp);
}

/* Tries to create and open a new datapath with the given 'name' and 'type'.
 * 'type' may be either NULL or the empty string to specify the default system
 * type.  Will fail if a datapath with 'name' and 'type' already exists.
 * Returns 0 if successful, otherwise a positive errno value.  On success
 * stores a pointer to the datapath in '*dpifp', otherwise a null pointer. */
int
dpif_create(const char *name, const char *type, struct dpif **dpifp)
{
    return do_open(name, type, true, dpifp);
}

/* Tries to open a datapath with the given 'name' and 'type', creating it if it
 * does not exist.  'type' may be either NULL or the empty string to specify
 * the default system type.  Returns 0 if successful, otherwise a positive
 * errno value. On success stores a pointer to the datapath in '*dpifp',
 * otherwise a null pointer. */
int
dpif_create_and_open(const char *name, const char *type, struct dpif **dpifp)
{
    int error;

    error = dpif_create(name, type, dpifp);
    if (error == EEXIST || error == EBUSY) {
        error = dpif_open(name, type, dpifp);
        if (error) {
            VLOG_WARN("datapath %s already exists but cannot be opened: %s",
                      name, strerror(error));
        }
    } else if (error) {
        VLOG_WARN("failed to create datapath %s: %s", name, strerror(error));
    }
    return error;
}

/* Closes and frees the connection to 'dpif'.  Does not destroy the datapath
 * itself; call dpif_delete() first, instead, if that is desirable. */
void
dpif_close(struct dpif *dpif)
{
    if (dpif) {
        struct registered_dpif_class *registered_class;

        registered_class = shash_find_data(&dpif_classes,
                dpif->dpif_class->type);
        assert(registered_class);
        assert(registered_class->refcount);

        registered_class->refcount--;
        dpif_uninit(dpif, true);
    }
}

/* Returns the name of datapath 'dpif' prefixed with the type
 * (for use in log messages). */
const char *
dpif_name(const struct dpif *dpif)
{
    return dpif->full_name;
}

/* Returns the name of datapath 'dpif' without the type
 * (for use in device names). */
const char *
dpif_base_name(const struct dpif *dpif)
{
    return dpif->base_name;
}

/* Enumerates all names that may be used to open 'dpif' into 'all_names'.  The
 * Linux datapath, for example, supports opening a datapath both by number,
 * e.g. "dp0", and by the name of the datapath's local port.  For some
 * datapaths, this might be an infinite set (e.g. in a file name, slashes may
 * be duplicated any number of times), in which case only the names most likely
 * to be used will be enumerated.
 *
 * The caller must already have initialized 'all_names'.  Any existing names in
 * 'all_names' will not be disturbed. */
int
dpif_get_all_names(const struct dpif *dpif, struct svec *all_names)
{
    if (dpif->dpif_class->get_all_names) {
        int error = dpif->dpif_class->get_all_names(dpif, all_names);
        if (error) {
            VLOG_WARN_RL(&error_rl,
                         "failed to retrieve names for datpath %s: %s",
                         dpif_name(dpif), strerror(error));
        }
        return error;
    } else {
        svec_add(all_names, dpif_base_name(dpif));
        return 0;
    }
}

/* Destroys the datapath that 'dpif' is connected to, first removing all of its
 * ports.  After calling this function, it does not make sense to pass 'dpif'
 * to any functions other than dpif_name() or dpif_close(). */
int
dpif_delete(struct dpif *dpif)
{
    int error;

    COVERAGE_INC(dpif_destroy);

    error = dpif->dpif_class->destroy(dpif);
    log_operation(dpif, "delete", error);
    return error;
}

/* Retrieves statistics for 'dpif' into 'stats'.  Returns 0 if successful,
 * otherwise a positive errno value. */
int
dpif_get_dp_stats(const struct dpif *dpif, struct odp_stats *stats)
{
    int error = dpif->dpif_class->get_stats(dpif, stats);
    if (error) {
        memset(stats, 0, sizeof *stats);
    }
    log_operation(dpif, "get_stats", error);
    return error;
}

/* Retrieves the current IP fragment handling policy for 'dpif' into
 * '*drop_frags': true indicates that fragments are dropped, false indicates
 * that fragments are treated in the same way as other IP packets (except that
 * the L4 header cannot be read).  Returns 0 if successful, otherwise a
 * positive errno value. */
int
dpif_get_drop_frags(const struct dpif *dpif, bool *drop_frags)
{
    int error = dpif->dpif_class->get_drop_frags(dpif, drop_frags);
    if (error) {
        *drop_frags = false;
    }
    log_operation(dpif, "get_drop_frags", error);
    return error;
}

/* Changes 'dpif''s treatment of IP fragments to 'drop_frags', whose meaning is
 * the same as for the get_drop_frags member function.  Returns 0 if
 * successful, otherwise a positive errno value. */
int
dpif_set_drop_frags(struct dpif *dpif, bool drop_frags)
{
    int error = dpif->dpif_class->set_drop_frags(dpif, drop_frags);
    log_operation(dpif, "set_drop_frags", error);
    return error;
}

/* Attempts to add 'devname' as a port on 'dpif', given the combination of
 * ODP_PORT_* flags in 'flags'.  If successful, returns 0 and sets '*port_nop'
 * to the new port's port number (if 'port_nop' is non-null).  On failure,
 * returns a positive errno value and sets '*port_nop' to UINT16_MAX (if
 * 'port_nop' is non-null). */
int
dpif_port_add(struct dpif *dpif, const char *devname, uint16_t flags,
              uint16_t *port_nop)
{
    uint16_t port_no;
    int error;

    COVERAGE_INC(dpif_port_add);

    error = dpif->dpif_class->port_add(dpif, devname, flags, &port_no);
    if (!error) {
        VLOG_DBG_RL(&dpmsg_rl, "%s: added %s as port %"PRIu16,
                    dpif_name(dpif), devname, port_no);
    } else {
        VLOG_WARN_RL(&error_rl, "%s: failed to add %s as port: %s",
                     dpif_name(dpif), devname, strerror(error));
        port_no = UINT16_MAX;
    }
    if (port_nop) {
        *port_nop = port_no;
    }
    return error;
}

/* Attempts to remove 'dpif''s port number 'port_no'.  Returns 0 if successful,
 * otherwise a positive errno value. */
int
dpif_port_del(struct dpif *dpif, uint16_t port_no)
{
    int error;

    COVERAGE_INC(dpif_port_del);

    error = dpif->dpif_class->port_del(dpif, port_no);
    log_operation(dpif, "port_del", error);
    return error;
}

/* Looks up port number 'port_no' in 'dpif'.  On success, returns 0 and
 * initializes '*port' appropriately; on failure, returns a positive errno
 * value. */
int
dpif_port_query_by_number(const struct dpif *dpif, uint16_t port_no,
                          struct odp_port *port)
{
    int error = dpif->dpif_class->port_query_by_number(dpif, port_no, port);
    if (!error) {
        VLOG_DBG_RL(&dpmsg_rl, "%s: port %"PRIu16" is device %s",
                    dpif_name(dpif), port_no, port->devname);
    } else {
        memset(port, 0, sizeof *port);
        VLOG_WARN_RL(&error_rl, "%s: failed to query port %"PRIu16": %s",
                     dpif_name(dpif), port_no, strerror(error));
    }
    return error;
}

/* Looks up port named 'devname' in 'dpif'.  On success, returns 0 and
 * initializes '*port' appropriately; on failure, returns a positive errno
 * value. */
int
dpif_port_query_by_name(const struct dpif *dpif, const char *devname,
                        struct odp_port *port)
{
    int error = dpif->dpif_class->port_query_by_name(dpif, devname, port);
    if (!error) {
        VLOG_DBG_RL(&dpmsg_rl, "%s: device %s is on port %"PRIu16,
                    dpif_name(dpif), devname, port->port);
    } else {
        memset(port, 0, sizeof *port);

        /* Log level is DBG here because all the current callers are interested
         * in whether 'dpif' actually has a port 'devname', so that it's not an
         * issue worth logging if it doesn't. */
        VLOG_DBG_RL(&error_rl, "%s: failed to query port %s: %s",
                    dpif_name(dpif), devname, strerror(error));
    }
    return error;
}

/* Looks up port number 'port_no' in 'dpif'.  On success, returns 0 and copies
 * the port's name into the 'name_size' bytes in 'name', ensuring that the
 * result is null-terminated.  On failure, returns a positive errno value and
 * makes 'name' the empty string. */
int
dpif_port_get_name(struct dpif *dpif, uint16_t port_no,
                   char *name, size_t name_size)
{
    struct odp_port port;
    int error;

    assert(name_size > 0);

    error = dpif_port_query_by_number(dpif, port_no, &port);
    if (!error) {
        ovs_strlcpy(name, port.devname, name_size);
    } else {
        *name = '\0';
    }
    return error;
}

/* Obtains a list of all the ports in 'dpif'.
 *
 * If successful, returns 0 and sets '*portsp' to point to an array of
 * appropriately initialized port structures and '*n_portsp' to the number of
 * ports in the array.  The caller is responsible for freeing '*portp' by
 * calling free().
 *
 * On failure, returns a positive errno value and sets '*portsp' to NULL and
 * '*n_portsp' to 0. */
int
dpif_port_list(const struct dpif *dpif,
               struct odp_port **portsp, size_t *n_portsp)
{
    struct odp_port *ports;
    size_t n_ports = 0;
    int error;

    for (;;) {
        struct odp_stats stats;
        int retval;

        error = dpif_get_dp_stats(dpif, &stats);
        if (error) {
            goto exit;
        }

        ports = xcalloc(stats.n_ports, sizeof *ports);
        retval = dpif->dpif_class->port_list(dpif, ports, stats.n_ports);
        if (retval < 0) {
            /* Hard error. */
            error = -retval;
            free(ports);
            goto exit;
        } else if (retval <= stats.n_ports) {
            /* Success. */
            error = 0;
            n_ports = retval;
            goto exit;
        } else {
            /* Soft error: port count increased behind our back.  Try again. */
            free(ports);
        }
    }

exit:
    if (error) {
        *portsp = NULL;
        *n_portsp = 0;
    } else {
        *portsp = ports;
        *n_portsp = n_ports;
    }
    log_operation(dpif, "port_list", error);
    return error;
}

/* Polls for changes in the set of ports in 'dpif'.  If the set of ports in
 * 'dpif' has changed, this function does one of the following:
 *
 * - Stores the name of the device that was added to or deleted from 'dpif' in
 *   '*devnamep' and returns 0.  The caller is responsible for freeing
 *   '*devnamep' (with free()) when it no longer needs it.
 *
 * - Returns ENOBUFS and sets '*devnamep' to NULL.
 *
 * This function may also return 'false positives', where it returns 0 and
 * '*devnamep' names a device that was not actually added or deleted or it
 * returns ENOBUFS without any change.
 *
 * Returns EAGAIN if the set of ports in 'dpif' has not changed.  May also
 * return other positive errno values to indicate that something has gone
 * wrong. */
int
dpif_port_poll(const struct dpif *dpif, char **devnamep)
{
    int error = dpif->dpif_class->port_poll(dpif, devnamep);
    if (error) {
        *devnamep = NULL;
    }
    return error;
}

/* Arranges for the poll loop to wake up when port_poll(dpif) will return a
 * value other than EAGAIN. */
void
dpif_port_poll_wait(const struct dpif *dpif)
{
    dpif->dpif_class->port_poll_wait(dpif);
}

/* Retrieves a list of the port numbers in port group 'group' in 'dpif'.
 *
 * On success, returns 0 and points '*ports' to a newly allocated array of
 * integers, each of which is a 'dpif' port number for a port in
 * 'group'.  Stores the number of elements in the array in '*n_ports'.  The
 * caller is responsible for freeing '*ports' by calling free().
 *
 * On failure, returns a positive errno value and sets '*ports' to NULL and
 * '*n_ports' to 0. */
int
dpif_port_group_get(const struct dpif *dpif, uint16_t group,
                    uint16_t **ports, size_t *n_ports)
{
    int error;

    *ports = NULL;
    *n_ports = 0;
    for (;;) {
        int retval = dpif->dpif_class->port_group_get(dpif, group,
                                                      *ports, *n_ports);
        if (retval < 0) {
            /* Hard error. */
            error = -retval;
            free(*ports);
            *ports = NULL;
            *n_ports = 0;
            break;
        } else if (retval <= *n_ports) {
            /* Success. */
            error = 0;
            *n_ports = retval;
            break;
        } else {
            /* Soft error: there were more ports than we expected in the
             * group.  Try again. */
            free(*ports);
            *ports = xcalloc(retval, sizeof **ports);
            *n_ports = retval;
        }
    }
    log_operation(dpif, "port_group_get", error);
    return error;
}

/* Updates port group 'group' in 'dpif', making it contain the 'n_ports' ports
 * whose 'dpif' port numbers are given in 'n_ports'.  Returns 0 if
 * successful, otherwise a positive errno value.
 *
 * Behavior is undefined if the values in ports[] are not unique. */
int
dpif_port_group_set(struct dpif *dpif, uint16_t group,
                    const uint16_t ports[], size_t n_ports)
{
    int error;

    COVERAGE_INC(dpif_port_group_set);

    error = dpif->dpif_class->port_group_set(dpif, group, ports, n_ports);
    log_operation(dpif, "port_group_set", error);
    return error;
}

/* Deletes all flows from 'dpif'.  Returns 0 if successful, otherwise a
 * positive errno value.  */
int
dpif_flow_flush(struct dpif *dpif)
{
    int error;

    COVERAGE_INC(dpif_flow_flush);

    error = dpif->dpif_class->flow_flush(dpif);
    log_operation(dpif, "flow_flush", error);
    return error;
}

/* Queries 'dpif' for a flow entry matching 'flow->key'.
 *
 * If a flow matching 'flow->key' exists in 'dpif', stores statistics for the
 * flow into 'flow->stats'.  If 'flow->n_actions' is zero, then 'flow->actions'
 * is ignored.  If 'flow->n_actions' is nonzero, then 'flow->actions' should
 * point to an array of the specified number of actions.  At most that many of
 * the flow's actions will be copied into that array.  'flow->n_actions' will
 * be updated to the number of actions actually present in the flow, which may
 * be greater than the number stored if the flow has more actions than space
 * available in the array.
 *
 * If no flow matching 'flow->key' exists in 'dpif', returns ENOENT.  On other
 * failure, returns a positive errno value. */
int
dpif_flow_get(const struct dpif *dpif, struct odp_flow *flow)
{
    int error;

    COVERAGE_INC(dpif_flow_get);

    check_rw_odp_flow(flow);
    error = dpif->dpif_class->flow_get(dpif, flow, 1);
    if (!error) {
        error = flow->stats.error;
    }
    if (error) {
        /* Make the results predictable on error. */
        memset(&flow->stats, 0, sizeof flow->stats);
        flow->n_actions = 0;
    }
    if (should_log_flow_message(error)) {
        log_flow_operation(dpif, "flow_get", error, flow);
    }
    return error;
}

/* For each flow 'flow' in the 'n' flows in 'flows':
 *
 * - If a flow matching 'flow->key' exists in 'dpif':
 *
 *     Stores 0 into 'flow->stats.error' and stores statistics for the flow
 *     into 'flow->stats'.
 *
 *     If 'flow->n_actions' is zero, then 'flow->actions' is ignored.  If
 *     'flow->n_actions' is nonzero, then 'flow->actions' should point to an
 *     array of the specified number of actions.  At most that many of the
 *     flow's actions will be copied into that array.  'flow->n_actions' will
 *     be updated to the number of actions actually present in the flow, which
 *     may be greater than the number stored if the flow has more actions than
 *     space available in the array.
 *
 * - Flow-specific errors are indicated by a positive errno value in
 *   'flow->stats.error'.  In particular, ENOENT indicates that no flow
 *   matching 'flow->key' exists in 'dpif'.  When an error value is stored, the
 *   contents of 'flow->key' are preserved but other members of 'flow' should
 *   be treated as indeterminate.
 *
 * Returns 0 if all 'n' flows in 'flows' were updated (whether they were
 * individually successful or not is indicated by 'flow->stats.error',
 * however).  Returns a positive errno value if an error that prevented this
 * update occurred, in which the caller must not depend on any elements in
 * 'flows' being updated or not updated.
 */
int
dpif_flow_get_multiple(const struct dpif *dpif,
                       struct odp_flow flows[], size_t n)
{
    int error;
    size_t i;

    COVERAGE_ADD(dpif_flow_get, n);

    for (i = 0; i < n; i++) {
        check_rw_odp_flow(&flows[i]);
    }

    error = dpif->dpif_class->flow_get(dpif, flows, n);
    log_operation(dpif, "flow_get_multiple", error);
    return error;
}

/* Adds or modifies a flow in 'dpif' as specified in 'put':
 *
 * - If the flow specified in 'put->flow' does not exist in 'dpif', then
 *   behavior depends on whether ODPPF_CREATE is specified in 'put->flags': if
 *   it is, the flow will be added, otherwise the operation will fail with
 *   ENOENT.
 *
 * - Otherwise, the flow specified in 'put->flow' does exist in 'dpif'.
 *   Behavior in this case depends on whether ODPPF_MODIFY is specified in
 *   'put->flags': if it is, the flow's actions will be updated, otherwise the
 *   operation will fail with EEXIST.  If the flow's actions are updated, then
 *   its statistics will be zeroed if ODPPF_ZERO_STATS is set in 'put->flags',
 *   left as-is otherwise.
 *
 * Returns 0 if successful, otherwise a positive errno value.
 */
int
dpif_flow_put(struct dpif *dpif, struct odp_flow_put *put)
{
    int error;

    COVERAGE_INC(dpif_flow_put);

    error = dpif->dpif_class->flow_put(dpif, put);
    if (should_log_flow_message(error)) {
        log_flow_put(dpif, error, put);
    }
    return error;
}

/* Deletes a flow matching 'flow->key' from 'dpif' or returns ENOENT if 'dpif'
 * does not contain such a flow.
 *
 * If successful, updates 'flow->stats', 'flow->n_actions', and 'flow->actions'
 * as described for dpif_flow_get(). */
int
dpif_flow_del(struct dpif *dpif, struct odp_flow *flow)
{
    int error;

    COVERAGE_INC(dpif_flow_del);

    check_rw_odp_flow(flow);
    memset(&flow->stats, 0, sizeof flow->stats);

    error = dpif->dpif_class->flow_del(dpif, flow);
    if (should_log_flow_message(error)) {
        log_flow_operation(dpif, "delete flow", error, flow);
    }
    return error;
}

/* Stores up to 'n' flows in 'dpif' into 'flows', including their statistics
 * but not including any information about their actions.  If successful,
 * returns 0 and sets '*n_out' to the number of flows actually present in
 * 'dpif', which might be greater than the number stored (if 'dpif' has more
 * than 'n' flows).  On failure, returns a negative errno value and sets
 * '*n_out' to 0. */
int
dpif_flow_list(const struct dpif *dpif, struct odp_flow flows[], size_t n,
               size_t *n_out)
{
    uint32_t i;
    int retval;

    COVERAGE_INC(dpif_flow_query_list);
    if (RUNNING_ON_VALGRIND) {
        memset(flows, 0, n * sizeof *flows);
    } else {
        for (i = 0; i < n; i++) {
            flows[i].actions = NULL;
            flows[i].n_actions = 0;
        }
    }
    retval = dpif->dpif_class->flow_list(dpif, flows, n);
    if (retval < 0) {
        *n_out = 0;
        VLOG_WARN_RL(&error_rl, "%s: flow list failed (%s)",
                     dpif_name(dpif), strerror(-retval));
        return -retval;
    } else {
        COVERAGE_ADD(dpif_flow_query_list_n, retval);
        *n_out = MIN(n, retval);
        VLOG_DBG_RL(&dpmsg_rl, "%s: listed %zu flows (of %d)",
                    dpif_name(dpif), *n_out, retval);
        return 0;
    }
}

/* Retrieves all of the flows in 'dpif'.
 *
 * If successful, returns 0 and stores in '*flowsp' a pointer to a newly
 * allocated array of flows, including their statistics but not including any
 * information about their actions, and sets '*np' to the number of flows in
 * '*flowsp'.  The caller is responsible for freeing '*flowsp' by calling
 * free().
 *
 * On failure, returns a positive errno value and sets '*flowsp' to NULL and
 * '*np' to 0. */
int
dpif_flow_list_all(const struct dpif *dpif,
                   struct odp_flow **flowsp, size_t *np)
{
    struct odp_stats stats;
    struct odp_flow *flows;
    size_t n_flows;
    int error;

    *flowsp = NULL;
    *np = 0;

    error = dpif_get_dp_stats(dpif, &stats);
    if (error) {
        return error;
    }

    flows = xmalloc(sizeof *flows * stats.n_flows);
    error = dpif_flow_list(dpif, flows, stats.n_flows, &n_flows);
    if (error) {
        free(flows);
        return error;
    }

    if (stats.n_flows != n_flows) {
        VLOG_WARN_RL(&error_rl, "%s: datapath stats reported %"PRIu32" "
                     "flows but flow listing reported %zu",
                     dpif_name(dpif), stats.n_flows, n_flows);
    }
    *flowsp = flows;
    *np = n_flows;
    return 0;
}

/* Causes 'dpif' to perform the 'n_actions' actions in 'actions' on the
 * Ethernet frame specified in 'packet'.
 *
 * Pretends that the frame was originally received on the port numbered
 * 'in_port'.  This affects only ODPAT_OUTPUT_GROUP actions, which will not
 * send a packet out their input port.  Specify the number of an unused port
 * (e.g. UINT16_MAX is currently always unused) to avoid this behavior.
 *
 * Returns 0 if successful, otherwise a positive errno value. */
int
dpif_execute(struct dpif *dpif, uint16_t in_port,
             const union odp_action actions[], size_t n_actions,
             const struct ofpbuf *buf)
{
    int error;

    COVERAGE_INC(dpif_execute);
    if (n_actions > 0) {
        error = dpif->dpif_class->execute(dpif, in_port, actions,
                                          n_actions, buf);
    } else {
        error = 0;
    }

    if (!(error ? VLOG_DROP_WARN(&error_rl) : VLOG_DROP_DBG(&dpmsg_rl))) {
        struct ds ds = DS_EMPTY_INITIALIZER;
        char *packet = ofp_packet_to_string(buf->data, buf->size, buf->size);
        ds_put_format(&ds, "%s: execute ", dpif_name(dpif));
        format_odp_actions(&ds, actions, n_actions);
        if (error) {
            ds_put_format(&ds, " failed (%s)", strerror(error));
        }
        ds_put_format(&ds, " on packet %s", packet);
        vlog(THIS_MODULE, error ? VLL_WARN : VLL_DBG, "%s", ds_cstr(&ds));
        ds_destroy(&ds);
        free(packet);
    }
    return error;
}

/* Retrieves 'dpif''s "listen mask" into '*listen_mask'.  Each ODPL_* bit set
 * in '*listen_mask' indicates that dpif_recv() will receive messages of that
 * type.  Returns 0 if successful, otherwise a positive errno value. */
int
dpif_recv_get_mask(const struct dpif *dpif, int *listen_mask)
{
    int error = dpif->dpif_class->recv_get_mask(dpif, listen_mask);
    if (error) {
        *listen_mask = 0;
    }
    log_operation(dpif, "recv_get_mask", error);
    return error;
}

/* Sets 'dpif''s "listen mask" to 'listen_mask'.  Each ODPL_* bit set in
 * '*listen_mask' requests that dpif_recv() receive messages of that type.
 * Returns 0 if successful, otherwise a positive errno value. */
int
dpif_recv_set_mask(struct dpif *dpif, int listen_mask)
{
    int error = dpif->dpif_class->recv_set_mask(dpif, listen_mask);
    log_operation(dpif, "recv_set_mask", error);
    return error;
}

/* Retrieve the sFlow sampling probability.  '*probability' is expressed as the
 * number of packets out of UINT_MAX to sample, e.g. probability/UINT_MAX is
 * the probability of sampling a given packet.
 *
 * Returns 0 if successful, otherwise a positive errno value.  EOPNOTSUPP
 * indicates that 'dpif' does not support sFlow sampling. */
int
dpif_get_sflow_probability(const struct dpif *dpif, uint32_t *probability)
{
    int error = (dpif->dpif_class->get_sflow_probability
                 ? dpif->dpif_class->get_sflow_probability(dpif, probability)
                 : EOPNOTSUPP);
    if (error) {
        *probability = 0;
    }
    log_operation(dpif, "get_sflow_probability", error);
    return error;
}

/* Set the sFlow sampling probability.  'probability' is expressed as the
 * number of packets out of UINT_MAX to sample, e.g. probability/UINT_MAX is
 * the probability of sampling a given packet.
 *
 * Returns 0 if successful, otherwise a positive errno value.  EOPNOTSUPP
 * indicates that 'dpif' does not support sFlow sampling. */
int
dpif_set_sflow_probability(struct dpif *dpif, uint32_t probability)
{
    int error = (dpif->dpif_class->set_sflow_probability
                 ? dpif->dpif_class->set_sflow_probability(dpif, probability)
                 : EOPNOTSUPP);
    log_operation(dpif, "set_sflow_probability", error);
    return error;
}

/* Attempts to receive a message from 'dpif'.  If successful, stores the
 * message into '*packetp'.  The message, if one is received, will begin with
 * 'struct odp_msg' as a header, and will have at least DPIF_RECV_MSG_PADDING
 * bytes of headroom.  Only messages of the types selected with
 * dpif_set_listen_mask() will ordinarily be received (but if a message type is
 * enabled and then later disabled, some stragglers might pop up).
 *
 * Returns 0 if successful, otherwise a positive errno value.  Returns EAGAIN
 * if no message is immediately available. */
int
dpif_recv(struct dpif *dpif, struct ofpbuf **packetp)
{
    int error = dpif->dpif_class->recv(dpif, packetp);
    if (!error) {
        struct ofpbuf *buf = *packetp;

        assert(ofpbuf_headroom(buf) >= DPIF_RECV_MSG_PADDING);
        if (VLOG_IS_DBG_ENABLED()) {
            struct odp_msg *msg = buf->data;
            void *payload = msg + 1;
            size_t payload_len = buf->size - sizeof *msg;
            char *s = ofp_packet_to_string(payload, payload_len, payload_len);
            VLOG_DBG_RL(&dpmsg_rl, "%s: received %s message of length "
                        "%zu on port %"PRIu16": %s", dpif_name(dpif),
                        (msg->type == _ODPL_MISS_NR ? "miss"
                         : msg->type == _ODPL_ACTION_NR ? "action"
                         : msg->type == _ODPL_SFLOW_NR ? "sFlow"
                         : "<unknown>"),
                        payload_len, msg->port, s);
            free(s);
        }
    } else {
        *packetp = NULL;
    }
    return error;
}

/* Discards all messages that would otherwise be received by dpif_recv() on
 * 'dpif'.  Returns 0 if successful, otherwise a positive errno value. */
int
dpif_recv_purge(struct dpif *dpif)
{
    struct odp_stats stats;
    unsigned int i;
    int error;

    COVERAGE_INC(dpif_purge);

    error = dpif_get_dp_stats(dpif, &stats);
    if (error) {
        return error;
    }

    for (i = 0; i < stats.max_miss_queue + stats.max_action_queue + stats.max_sflow_queue; i++) {
        struct ofpbuf *buf;
        error = dpif_recv(dpif, &buf);
        if (error) {
            return error == EAGAIN ? 0 : error;
        }
        ofpbuf_delete(buf);
    }
    return 0;
}

/* Arranges for the poll loop to wake up when 'dpif' has a message queued to be
 * received with dpif_recv(). */
void
dpif_recv_wait(struct dpif *dpif)
{
    dpif->dpif_class->recv_wait(dpif);
}

/* Obtains the NetFlow engine type and engine ID for 'dpif' into '*engine_type'
 * and '*engine_id', respectively. */
void
dpif_get_netflow_ids(const struct dpif *dpif,
                     uint8_t *engine_type, uint8_t *engine_id)
{
    *engine_type = dpif->netflow_engine_type;
    *engine_id = dpif->netflow_engine_id;
}

/* Translates OpenFlow queue ID 'queue_id' (in host byte order) into a priority
 * value for use in the ODPAT_SET_PRIORITY action.  On success, returns 0 and
 * stores the priority into '*priority'.  On failure, returns a positive errno
 * value and stores 0 into '*priority'. */
int
dpif_queue_to_priority(const struct dpif *dpif, uint32_t queue_id,
                       uint32_t *priority)
{
    int error = (dpif->dpif_class->queue_to_priority
                 ? dpif->dpif_class->queue_to_priority(dpif, queue_id,
                                                       priority)
                 : EOPNOTSUPP);
    if (error) {
        *priority = 0;
    }
    log_operation(dpif, "queue_to_priority", error);
    return error;
}

void
dpif_init(struct dpif *dpif, const struct dpif_class *dpif_class,
          const char *name,
          uint8_t netflow_engine_type, uint8_t netflow_engine_id)
{
    dpif->dpif_class = dpif_class;
    dpif->base_name = xstrdup(name);
    dpif->full_name = xasprintf("%s@%s", dpif_class->type, name);
    dpif->netflow_engine_type = netflow_engine_type;
    dpif->netflow_engine_id = netflow_engine_id;
}

/* Undoes the results of initialization.
 *
 * Normally this function only needs to be called from dpif_close().
 * However, it may be called by providers due to an error on opening
 * that occurs after initialization.  It this case dpif_close() would
 * never be called. */
void
dpif_uninit(struct dpif *dpif, bool close)
{
    char *base_name = dpif->base_name;
    char *full_name = dpif->full_name;

    if (close) {
        dpif->dpif_class->close(dpif);
    }

    free(base_name);
    free(full_name);
}

static void
log_operation(const struct dpif *dpif, const char *operation, int error)
{
    if (!error) {
        VLOG_DBG_RL(&dpmsg_rl, "%s: %s success", dpif_name(dpif), operation);
    } else {
        VLOG_WARN_RL(&error_rl, "%s: %s failed (%s)",
                     dpif_name(dpif), operation, strerror(error));
    }
}

static enum vlog_level
flow_message_log_level(int error)
{
    return error ? VLL_WARN : VLL_DBG;
}

static bool
should_log_flow_message(int error)
{
    return !vlog_should_drop(THIS_MODULE, flow_message_log_level(error),
                             error ? &error_rl : &dpmsg_rl);
}

static void
log_flow_message(const struct dpif *dpif, int error, const char *operation,
                 const flow_t *flow, const struct odp_flow_stats *stats,
                 const union odp_action *actions, size_t n_actions)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    ds_put_format(&ds, "%s: ", dpif_name(dpif));
    if (error) {
        ds_put_cstr(&ds, "failed to ");
    }
    ds_put_format(&ds, "%s ", operation);
    if (error) {
        ds_put_format(&ds, "(%s) ", strerror(error));
    }
    flow_format(&ds, flow);
    if (stats) {
        ds_put_cstr(&ds, ", ");
        format_odp_flow_stats(&ds, stats);
    }
    if (actions || n_actions) {
        ds_put_cstr(&ds, ", actions:");
        format_odp_actions(&ds, actions, n_actions);
    }
    vlog(THIS_MODULE, flow_message_log_level(error), "%s", ds_cstr(&ds));
    ds_destroy(&ds);
}

static void
log_flow_operation(const struct dpif *dpif, const char *operation, int error,
                   struct odp_flow *flow)
{
    if (error) {
        flow->n_actions = 0;
    }
    log_flow_message(dpif, error, operation, &flow->key,
                     !error ? &flow->stats : NULL,
                     flow->actions, flow->n_actions);
}

static void
log_flow_put(struct dpif *dpif, int error, const struct odp_flow_put *put)
{
    enum { ODPPF_ALL = ODPPF_CREATE | ODPPF_MODIFY | ODPPF_ZERO_STATS };
    struct ds s;

    ds_init(&s);
    ds_put_cstr(&s, "put");
    if (put->flags & ODPPF_CREATE) {
        ds_put_cstr(&s, "[create]");
    }
    if (put->flags & ODPPF_MODIFY) {
        ds_put_cstr(&s, "[modify]");
    }
    if (put->flags & ODPPF_ZERO_STATS) {
        ds_put_cstr(&s, "[zero]");
    }
    if (put->flags & ~ODPPF_ALL) {
        ds_put_format(&s, "[%x]", put->flags & ~ODPPF_ALL);
    }
    log_flow_message(dpif, error, ds_cstr(&s), &put->flow.key,
                     !error ? &put->flow.stats : NULL,
                     put->flow.actions, put->flow.n_actions);
    ds_destroy(&s);
}

/* There is a tendency to construct odp_flow objects on the stack and to
 * forget to properly initialize their "actions" and "n_actions" members.
 * When this happens, we get memory corruption because the kernel
 * writes through the random pointer that is in the "actions" member.
 *
 * This function attempts to combat the problem by:
 *
 *      - Forcing a segfault if "actions" points to an invalid region (instead
 *        of just getting back EFAULT, which can be easily missed in the log).
 *
 *      - Storing a distinctive value that is likely to cause an
 *        easy-to-identify error later if it is dereferenced, etc.
 *
 *      - Triggering a warning on uninitialized memory from Valgrind if
 *        "actions" or "n_actions" was not initialized.
 */
static void
check_rw_odp_flow(struct odp_flow *flow)
{
    if (flow->n_actions) {
        memset(&flow->actions[0], 0xcc, sizeof flow->actions[0]);
    }
}
