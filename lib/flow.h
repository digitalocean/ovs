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
#ifndef FLOW_H
#define FLOW_H 1

#include <sys/types.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "openflow/nicira-ext.h"
#include "openflow/openflow.h"
#include "hash.h"
#include "openvswitch/datapath-protocol.h"
#include "util.h"

struct ds;
struct flow_wildcards;
struct ofp_match;
struct ofpbuf;

#define FLOW_N_REGS 4
BUILD_ASSERT_DECL(FLOW_N_REGS <= NXM_NX_MAX_REGS);

struct flow {
    ovs_be64 tun_id;            /* Encapsulating tunnel ID. */
    uint32_t regs[FLOW_N_REGS]; /* Registers. */
    ovs_be32 nw_src;            /* IP source address. */
    ovs_be32 nw_dst;            /* IP destination address. */
    uint16_t in_port;           /* Input switch port. */
    ovs_be16 vlan_tci;          /* If 802.1Q, TCI | VLAN_CFI; otherwise 0. */
    ovs_be16 dl_type;           /* Ethernet frame type. */
    ovs_be16 tp_src;            /* TCP/UDP source port. */
    ovs_be16 tp_dst;            /* TCP/UDP destination port. */
    uint8_t dl_src[6];          /* Ethernet source address. */
    uint8_t dl_dst[6];          /* Ethernet destination address. */
    uint8_t nw_proto;           /* IP protocol or low 8 bits of ARP opcode. */
    uint8_t nw_tos;             /* IP ToS (DSCP field, 6 bits). */
};

/* Assert that there are FLOW_SIG_SIZE bytes of significant data in "struct
 * flow", followed by FLOW_PAD_SIZE bytes of padding. */
#define FLOW_SIG_SIZE (40 + FLOW_N_REGS * 4)
#define FLOW_PAD_SIZE 0
BUILD_ASSERT_DECL(offsetof(struct flow, nw_tos) == FLOW_SIG_SIZE - 1);
BUILD_ASSERT_DECL(sizeof(((struct flow *)0)->nw_tos) == 1);
BUILD_ASSERT_DECL(sizeof(struct flow) == FLOW_SIG_SIZE + FLOW_PAD_SIZE);

int flow_extract(struct ofpbuf *, uint64_t tun_id, uint16_t in_port,
                 struct flow *);
void flow_extract_stats(const struct flow *flow, struct ofpbuf *packet,
        struct odp_flow_stats *stats);
char *flow_to_string(const struct flow *);
void flow_format(struct ds *, const struct flow *);
void flow_print(FILE *, const struct flow *);
static inline int flow_compare(const struct flow *, const struct flow *);
static inline bool flow_equal(const struct flow *, const struct flow *);
static inline size_t flow_hash(const struct flow *, uint32_t basis);

static inline int
flow_compare(const struct flow *a, const struct flow *b)
{
    return memcmp(a, b, FLOW_SIG_SIZE);
}

static inline bool
flow_equal(const struct flow *a, const struct flow *b)
{
    return !flow_compare(a, b);
}

static inline size_t
flow_hash(const struct flow *flow, uint32_t basis)
{
    return hash_bytes(flow, FLOW_SIG_SIZE, basis);
}

/* Open vSwitch flow wildcard bits.
 *
 * These are used only internally to Open vSwitch, in the 'wildcards' member of
 * struct flow_wildcards.  They never appear in the wire protocol in this
 * form. */

typedef unsigned int OVS_BITWISE flow_wildcards_t;

/* Same values and meanings as corresponding OFPFW_* bits. */
#define FWW_IN_PORT     ((OVS_FORCE flow_wildcards_t) (1 << 0))
#define FWW_DL_SRC      ((OVS_FORCE flow_wildcards_t) (1 << 2))
#define FWW_DL_DST      ((OVS_FORCE flow_wildcards_t) (1 << 3))
                                              /* excluding the multicast bit */
#define FWW_DL_TYPE     ((OVS_FORCE flow_wildcards_t) (1 << 4))
#define FWW_NW_PROTO    ((OVS_FORCE flow_wildcards_t) (1 << 5))
#define FWW_TP_SRC      ((OVS_FORCE flow_wildcards_t) (1 << 6))
#define FWW_TP_DST      ((OVS_FORCE flow_wildcards_t) (1 << 7))
/* Same meanings as corresponding OFPFW_* bits, but differ in value. */
#define FWW_NW_TOS      ((OVS_FORCE flow_wildcards_t) (1 << 1))
/* No OFPFW_* bits, but they do have corresponding OVSFW_* bits. */
#define FWW_TUN_ID      ((OVS_FORCE flow_wildcards_t) (1 << 8))
/* No corresponding OFPFW_* or OVSFW_* bits. */
#define FWW_ETH_MCAST   ((OVS_FORCE flow_wildcards_t) (1 << 9))
                                                       /* multicast bit only */
#define FWW_ALL         ((OVS_FORCE flow_wildcards_t) (((1 << 10)) - 1))

/* Information on wildcards for a flow, as a supplement to "struct flow".
 *
 * Note that the meaning of 1-bits in 'wildcards' is opposite that of 1-bits in
 * the rest of the members. */
struct flow_wildcards {
    flow_wildcards_t wildcards; /* 1-bit in each FWW_* wildcarded field. */
    uint32_t reg_masks[FLOW_N_REGS]; /* 1-bit in each significant regs bit. */
    ovs_be32 nw_src_mask;       /* 1-bit in each significant nw_src bit. */
    ovs_be32 nw_dst_mask;       /* 1-bit in each significant nw_dst bit. */
    ovs_be16 vlan_tci_mask;     /* 1-bit in each significant vlan_tci bit. */
    uint16_t zero;              /* Padding field set to zero. */
};

void flow_wildcards_init_catchall(struct flow_wildcards *);
void flow_wildcards_init_exact(struct flow_wildcards *);

bool flow_wildcards_is_exact(const struct flow_wildcards *);

bool flow_wildcards_set_nw_src_mask(struct flow_wildcards *, ovs_be32);
bool flow_wildcards_set_nw_dst_mask(struct flow_wildcards *, ovs_be32);
void flow_wildcards_set_reg_mask(struct flow_wildcards *,
                                 int idx, uint32_t mask);

void flow_wildcards_combine(struct flow_wildcards *dst,
                            const struct flow_wildcards *src1,
                            const struct flow_wildcards *src2);
bool flow_wildcards_has_extra(const struct flow_wildcards *,
                              const struct flow_wildcards *);

uint32_t flow_wildcards_hash(const struct flow_wildcards *);
bool flow_wildcards_equal(const struct flow_wildcards *,
                          const struct flow_wildcards *);

#endif /* flow.h */
