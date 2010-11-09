/*
 * Copyright (c) 2009, 2010 Nicira Networks.
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

#ifndef OFPROTO_H
#define OFPROTO_H 1

#include <sys/types.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flow.h"
#include "netflow.h"
#include "tag.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct odp_actions;
struct ofhooks;
struct ofproto;
struct svec;

enum {
    DP_GROUP_FLOOD = 0,
    DP_GROUP_ALL = 1
};

struct ofexpired {
    flow_t flow;
    uint64_t packet_count;      /* Packets from subrules. */
    uint64_t byte_count;        /* Bytes from subrules. */
    long long int used;         /* Last-used time (0 if never used). */
};

struct ofproto_sflow_options {
    struct svec targets;
    uint32_t sampling_rate;
    uint32_t polling_interval;
    uint32_t header_len;
    uint32_t sub_id;
    char *agent_device;
    char *control_ip;
};

/* How the switch should act if the controller cannot be contacted. */
enum ofproto_fail_mode {
    OFPROTO_FAIL_SECURE,        /* Preserve flow table. */
    OFPROTO_FAIL_STANDALONE     /* Act as a standalone switch. */
};

enum ofproto_band {
    OFPROTO_IN_BAND,            /* In-band connection to controller. */
    OFPROTO_OUT_OF_BAND         /* Out-of-band connection to controller. */
};

struct ofproto_controller {
    char *target;               /* e.g. "tcp:127.0.0.1" */
    int max_backoff;            /* Maximum reconnection backoff, in seconds. */
    int probe_interval;         /* Max idle time before probing, in seconds. */
    enum ofproto_band band;      /* In-band or out-of-band? */

    /* Discovery options. */
    char *accept_re;            /* Regexp for acceptable controllers.  */
    bool update_resolv_conf;    /* Update /etc/resolv.conf? */

    /* OpenFlow packet-in rate-limiting. */
    int rate_limit;             /* Max packet-in rate in packets per second. */
    int burst_limit;            /* Limit on accumulating packet credits. */
};

#define DEFAULT_MFR_DESC "Nicira Networks, Inc."
#define DEFAULT_HW_DESC "Open vSwitch"
#define DEFAULT_SW_DESC VERSION BUILDNR
#define DEFAULT_SERIAL_DESC "None"
#define DEFAULT_DP_DESC "None"

int ofproto_create(const char *datapath, const char *datapath_type,
                   const struct ofhooks *, void *aux,
                   struct ofproto **ofprotop);
void ofproto_destroy(struct ofproto *);
int ofproto_run(struct ofproto *);
int ofproto_run1(struct ofproto *);
int ofproto_run2(struct ofproto *, bool revalidate_all);
void ofproto_wait(struct ofproto *);
bool ofproto_is_alive(const struct ofproto *);

/* Configuration. */
void ofproto_set_datapath_id(struct ofproto *, uint64_t datapath_id);
void ofproto_set_controllers(struct ofproto *,
                             const struct ofproto_controller *, size_t n);
void ofproto_set_fail_mode(struct ofproto *, enum ofproto_fail_mode fail_mode);
void ofproto_reconnect_controllers(struct ofproto *);
void ofproto_set_extra_in_band_remotes(struct ofproto *,
                                       const struct sockaddr_in *, size_t n);
void ofproto_set_desc(struct ofproto *,
                      const char *mfr_desc, const char *hw_desc,
                      const char *sw_desc, const char *serial_desc,
                      const char *dp_desc);
int ofproto_set_snoops(struct ofproto *, const struct svec *snoops);
int ofproto_set_netflow(struct ofproto *,
                        const struct netflow_options *nf_options);
void ofproto_set_sflow(struct ofproto *, const struct ofproto_sflow_options *);

/* Configuration querying. */
uint64_t ofproto_get_datapath_id(const struct ofproto *);
bool ofproto_has_primary_controller(const struct ofproto *);
enum ofproto_fail_mode ofproto_get_fail_mode(const struct ofproto *);
void ofproto_get_listeners(const struct ofproto *, struct svec *);
void ofproto_get_snoops(const struct ofproto *, struct svec *);
void ofproto_get_all_flows(struct ofproto *p, struct ds *);

/* Functions for use by ofproto implementation modules, not by clients. */
int ofproto_send_packet(struct ofproto *, const flow_t *,
                        const union ofp_action *, size_t n_actions,
                        const struct ofpbuf *);
void ofproto_add_flow(struct ofproto *, const flow_t *, uint32_t wildcards,
                      unsigned int priority,
                      const union ofp_action *, size_t n_actions,
                      int idle_timeout);
void ofproto_delete_flow(struct ofproto *, const flow_t *, uint32_t wildcards,
                         unsigned int priority);
void ofproto_flush_flows(struct ofproto *);

/* Hooks for ovs-vswitchd. */
struct ofhooks {
    void (*port_changed_cb)(enum ofp_port_reason, const struct ofp_phy_port *,
                            void *aux);
    bool (*normal_cb)(const flow_t *, const struct ofpbuf *packet,
                      struct odp_actions *, tag_type *,
                      uint16_t *nf_output_iface, void *aux);
    void (*account_flow_cb)(const flow_t *, tag_type tags,
                            const union odp_action *, size_t n_actions,
                            unsigned long long int n_bytes, void *aux);
    void (*account_checkpoint_cb)(void *aux);
};
void ofproto_revalidate(struct ofproto *, tag_type);
struct tag_set *ofproto_get_revalidate_set(struct ofproto *);

#ifdef  __cplusplus
}
#endif

#endif /* ofproto.h */
