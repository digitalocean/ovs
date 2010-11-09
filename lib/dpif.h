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


#ifndef DPIF_H
#define DPIF_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "openflow/openflow.h"
#include "openvswitch/datapath-protocol.h"
#include "util.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct dpif;
struct ofpbuf;
struct svec;
struct dpif_class;

void dp_run(void);
void dp_wait(void);

int dp_register_provider(const struct dpif_class *);
int dp_unregister_provider(const char *type);
void dp_enumerate_types(struct svec *types);

int dp_enumerate_names(const char *type, struct svec *names);
void dp_parse_name(const char *datapath_name, char **name, char **type);

int dpif_open(const char *name, const char *type, struct dpif **);
int dpif_create(const char *name, const char *type, struct dpif **);
int dpif_create_and_open(const char *name, const char *type, struct dpif **);
void dpif_close(struct dpif *);

const char *dpif_name(const struct dpif *);
const char *dpif_base_name(const struct dpif *);
int dpif_get_all_names(const struct dpif *, struct svec *);

int dpif_delete(struct dpif *);

int dpif_get_dp_stats(const struct dpif *, struct odp_stats *);
int dpif_get_drop_frags(const struct dpif *, bool *drop_frags);
int dpif_set_drop_frags(struct dpif *, bool drop_frags);

int dpif_port_add(struct dpif *, const char *devname, uint16_t flags,
                  uint16_t *port_no);
int dpif_port_del(struct dpif *, uint16_t port_no);
int dpif_port_query_by_number(const struct dpif *, uint16_t port_no,
                              struct odp_port *);
int dpif_port_query_by_name(const struct dpif *, const char *devname,
                            struct odp_port *);
int dpif_port_get_name(struct dpif *, uint16_t port_no,
                       char *name, size_t name_size);
int dpif_port_list(const struct dpif *, struct odp_port **, size_t *n_ports);

int dpif_port_poll(const struct dpif *, char **devnamep);
void dpif_port_poll_wait(const struct dpif *);

int dpif_port_group_get(const struct dpif *, uint16_t group,
                        uint16_t **ports, size_t *n_ports);
int dpif_port_group_set(struct dpif *, uint16_t group,
                        const uint16_t ports[], size_t n_ports);

int dpif_flow_flush(struct dpif *);
int dpif_flow_put(struct dpif *, struct odp_flow_put *);
int dpif_flow_del(struct dpif *, struct odp_flow *);
int dpif_flow_get(const struct dpif *, struct odp_flow *);
int dpif_flow_get_multiple(const struct dpif *, struct odp_flow[], size_t n);
int dpif_flow_list(const struct dpif *, struct odp_flow[], size_t n,
                   size_t *n_out);
int dpif_flow_list_all(const struct dpif *,
                       struct odp_flow **flowsp, size_t *np);

int dpif_execute(struct dpif *, uint16_t in_port,
                 const union odp_action[], size_t n_actions,
                 const struct ofpbuf *);

/* Minimum number of bytes of headroom for a packet returned by dpif_recv()
 * member function.  This headroom allows "struct odp_msg" to be replaced by
 * "struct ofp_packet_in" without copying the buffer. */
#define DPIF_RECV_MSG_PADDING (sizeof(struct ofp_packet_in) \
                               - sizeof(struct odp_msg))
BUILD_ASSERT_DECL(sizeof(struct ofp_packet_in) > sizeof(struct odp_msg));
BUILD_ASSERT_DECL(DPIF_RECV_MSG_PADDING % 4 == 0);

int dpif_recv_get_mask(const struct dpif *, int *listen_mask);
int dpif_recv_set_mask(struct dpif *, int listen_mask);
int dpif_get_sflow_probability(const struct dpif *, uint32_t *probability);
int dpif_set_sflow_probability(struct dpif *, uint32_t probability);
int dpif_recv(struct dpif *, struct ofpbuf **);
int dpif_recv_purge(struct dpif *);
void dpif_recv_wait(struct dpif *);

void dpif_get_netflow_ids(const struct dpif *,
                          uint8_t *engine_type, uint8_t *engine_id);

int dpif_queue_to_priority(const struct dpif *, uint32_t queue_id,
                           uint32_t *priority);

#ifdef  __cplusplus
}
#endif

#endif /* dpif.h */
