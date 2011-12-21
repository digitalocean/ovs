/*
 * Copyright (c) 2011 Nicira Networks.
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

#include "meta-flow.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>

#include "classifier.h"
#include "dynamic-string.h"
#include "ofp-util.h"
#include "packets.h"
#include "random.h"
#include "shash.h"
#include "socket-util.h"
#include "unaligned.h"

#define MF_FIELD_SIZES(MEMBER)                  \
    sizeof ((union mf_value *)0)->MEMBER,       \
    8 * sizeof ((union mf_value *)0)->MEMBER

static const struct mf_field mf_fields[MFF_N_IDS] = {
    /* ## -------- ## */
    /* ## metadata ## */
    /* ## -------- ## */

    {
        MFF_TUN_ID, "tun_id", NULL,
        MF_FIELD_SIZES(be64),
        MFM_FULLY, 0,
        MFS_HEXADECIMAL,
        MFP_NONE,
        NXM_NX_TUN_ID,
    }, {
        MFF_IN_PORT, "in_port", NULL,
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_IN_PORT,
        MFS_OFP_PORT,
        MFP_NONE,
        NXM_OF_IN_PORT,
    },

#define REGISTER(IDX)                           \
    {                                           \
        MFF_REG##IDX, "reg" #IDX, NULL,         \
        MF_FIELD_SIZES(be32),                   \
        MFM_FULLY, 0,                           \
        MFS_HEXADECIMAL,                        \
        MFP_NONE,                               \
        NXM_NX_REG(IDX),                        \
    }
#if FLOW_N_REGS > 0
    REGISTER(0),
#endif
#if FLOW_N_REGS > 1
    REGISTER(1),
#endif
#if FLOW_N_REGS > 2
    REGISTER(2),
#endif
#if FLOW_N_REGS > 3
    REGISTER(3),
#endif
#if FLOW_N_REGS > 4
    REGISTER(4),
#endif
#if FLOW_N_REGS > 5
#error
#endif

    /* ## -- ## */
    /* ## L2 ## */
    /* ## -- ## */

    {
        MFF_ETH_SRC, "eth_src", "dl_src",
        MF_FIELD_SIZES(mac),
        MFM_NONE, FWW_DL_SRC,
        MFS_ETHERNET,
        MFP_NONE,
        NXM_OF_ETH_SRC,
    }, {
        MFF_ETH_DST, "eth_dst", "dl_dst",
        MF_FIELD_SIZES(mac),
        MFM_MCAST, 0,
        MFS_ETHERNET,
        MFP_NONE,
        NXM_OF_ETH_DST,
    }, {
        MFF_ETH_TYPE, "eth_type", "dl_type",
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_DL_TYPE,
        MFS_HEXADECIMAL,
        MFP_NONE,
        NXM_OF_ETH_TYPE,
    },

    {
        MFF_VLAN_TCI, "vlan_tci", NULL,
        MF_FIELD_SIZES(be16),
        MFM_FULLY, 0,
        MFS_HEXADECIMAL,
        MFP_NONE,
        NXM_OF_VLAN_TCI,
    }, {
        MFF_VLAN_VID, "dl_vlan", NULL,
        sizeof(ovs_be16), 12,
        MFM_NONE, 0,
        MFS_DECIMAL,
        MFP_NONE,
        0,
    }, {
        MFF_VLAN_PCP, "dl_vlan_pcp", NULL,
        1, 3,
        MFM_NONE, 0,
        MFS_DECIMAL,
        MFP_NONE,
        0,
    },

    /* ## -- ## */
    /* ## L3 ## */
    /* ## -- ## */

    {
        MFF_IPV4_SRC, "ip_src", "nw_src",
        MF_FIELD_SIZES(be32),
        MFM_CIDR, 0,
        MFS_IPV4,
        MFP_IPV4,
        NXM_OF_IP_SRC,
    }, {
        MFF_IPV4_DST, "ip_dst", "nw_dst",
        MF_FIELD_SIZES(be32),
        MFM_CIDR, 0,
        MFS_IPV4,
        MFP_IPV4,
        NXM_OF_IP_DST,
    },

    {
        MFF_IPV6_SRC, "ipv6_src", NULL,
        MF_FIELD_SIZES(ipv6),
        MFM_CIDR, 0,
        MFS_IPV6,
        MFP_IPV6,
        NXM_NX_IPV6_SRC,
    }, {
        MFF_IPV6_DST, "ipv6_dst", NULL,
        MF_FIELD_SIZES(ipv6),
        MFM_CIDR, 0,
        MFS_IPV6,
        MFP_IPV6,
        NXM_NX_IPV6_DST,
    },

    {
        MFF_IP_PROTO, "nw_proto", NULL,
        MF_FIELD_SIZES(u8),
        MFM_NONE, FWW_NW_PROTO,
        MFS_DECIMAL,
        MFP_IP_ANY,
        NXM_OF_IP_PROTO,
    }, {
        MFF_IP_TOS, "nw_tos", NULL,
        MF_FIELD_SIZES(u8),
        MFM_NONE, 0,
        MFS_DECIMAL,
        MFP_IP_ANY,
        NXM_OF_IP_TOS,
    }, {
        MFF_IP_FRAG, "ip_frag", NULL,
        1, 2,
        MFM_FULLY, 0,
        MFS_FRAG,
        MFP_IP_ANY,
        NXM_NX_IP_FRAG,
    },

    {
        MFF_ARP_OP, "arp_op", NULL,
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_NW_PROTO,
        MFS_DECIMAL,
        MFP_ARP,
        NXM_OF_ARP_OP,
    }, {
        MFF_ARP_SPA, "arp_spa", NULL,
        MF_FIELD_SIZES(be32),
        MFM_CIDR, 0,
        MFS_IPV4,
        MFP_ARP,
        NXM_OF_ARP_SPA,
    }, {
        MFF_ARP_TPA, "arp_tpa", NULL,
        MF_FIELD_SIZES(be32),
        MFM_CIDR, 0,
        MFS_IPV4,
        MFP_ARP,
        NXM_OF_ARP_TPA,
    }, {
        MFF_ARP_SHA, "arp_sha", NULL,
        MF_FIELD_SIZES(mac),
        MFM_NONE, FWW_ARP_SHA,
        MFS_ETHERNET,
        MFP_ARP,
        NXM_NX_ARP_SHA,
    }, {
        MFF_ARP_THA, "arp_tha", NULL,
        MF_FIELD_SIZES(mac),
        MFM_NONE, FWW_ARP_THA,
        MFS_ETHERNET,
        MFP_ARP,
        NXM_NX_ARP_THA,
    },

    /* ## -- ## */
    /* ## L4 ## */
    /* ## -- ## */

    {
        MFF_TCP_SRC, "tcp_src", "tp_src",
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_TP_SRC,
        MFS_DECIMAL,
        MFP_TCP,
        NXM_OF_TCP_SRC,
    }, {
        MFF_TCP_DST, "tcp_dst", "tp_dst",
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_TP_DST,
        MFS_DECIMAL,
        MFP_TCP,
        NXM_OF_TCP_DST,
    },

    {
        MFF_UDP_SRC, "udp_src", NULL,
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_TP_SRC,
        MFS_DECIMAL,
        MFP_UDP,
        NXM_OF_UDP_SRC,
    }, {
        MFF_UDP_DST, "udp_dst", NULL,
        MF_FIELD_SIZES(be16),
        MFM_NONE, FWW_TP_DST,
        MFS_DECIMAL,
        MFP_UDP,
        NXM_OF_UDP_DST,
    },

    {
        MFF_ICMP_TYPE, "icmp_type", NULL,
        MF_FIELD_SIZES(u8),
        MFM_NONE, FWW_TP_SRC,
        MFS_DECIMAL,
        MFP_ICMP_ANY,
        NXM_OF_ICMP_TYPE,
    }, {
        MFF_ICMP_CODE, "icmp_code", NULL,
        MF_FIELD_SIZES(u8),
        MFM_NONE, FWW_TP_DST,
        MFS_DECIMAL,
        MFP_ICMP_ANY,
        NXM_OF_ICMP_CODE,
    },

    /* ## ---- ## */
    /* ## L"5" ## */
    /* ## ---- ## */

    {
        MFF_ND_TARGET, "nd_target", NULL,
        MF_FIELD_SIZES(ipv6),
        MFM_NONE, FWW_ND_TARGET,
        MFS_IPV6,
        MFP_ND,
        NXM_NX_ND_TARGET,
    }, {
        MFF_ND_SLL, "nd_sll", NULL,
        MF_FIELD_SIZES(mac),
        MFM_NONE, FWW_ARP_SHA,
        MFS_ETHERNET,
        MFP_ND_SOLICIT,
        NXM_NX_ND_SLL,
    }, {
        MFF_ND_TLL, "nd_tll", NULL,
        MF_FIELD_SIZES(mac),
        MFM_NONE, FWW_ARP_THA,
        MFS_ETHERNET,
        MFP_ND_ADVERT,
        NXM_NX_ND_TLL,
    }
};

/* Returns the field with the given 'id'. */
const struct mf_field *
mf_from_id(enum mf_field_id id)
{
    assert((unsigned int) id < MFF_N_IDS);
    return &mf_fields[id];
}

/* Returns the field with the given 'name', or a null pointer if no field has
 * that name. */
const struct mf_field *
mf_from_name(const char *name)
{
    static struct shash mf_by_name = SHASH_INITIALIZER(&mf_by_name);

    if (shash_is_empty(&mf_by_name)) {
        const struct mf_field *mf;

        for (mf = mf_fields; mf < &mf_fields[MFF_N_IDS]; mf++) {
            shash_add_once(&mf_by_name, mf->name, mf);
            if (mf->extra_name) {
                shash_add_once(&mf_by_name, mf->extra_name, mf);
            }
        }
    }

    return shash_find_data(&mf_by_name, name);
}

/* Returns true if 'wc' wildcards all the bits in field 'mf', false if 'wc'
 * specifies at least one bit in the field.
 *
 * The caller is responsible for ensuring that 'wc' corresponds to a flow that
 * meets 'mf''s prerequisites. */
bool
mf_is_all_wild(const struct mf_field *mf, const struct flow_wildcards *wc)
{
    switch (mf->id) {
    case MFF_IN_PORT:
    case MFF_ETH_SRC:
    case MFF_ETH_TYPE:
    case MFF_IP_PROTO:
    case MFF_ARP_OP:
    case MFF_ARP_SHA:
    case MFF_ARP_THA:
    case MFF_TCP_SRC:
    case MFF_TCP_DST:
    case MFF_UDP_SRC:
    case MFF_UDP_DST:
    case MFF_ICMP_TYPE:
    case MFF_ICMP_CODE:
    case MFF_ND_TARGET:
    case MFF_ND_SLL:
    case MFF_ND_TLL:
        assert(mf->fww_bit != 0);
        return (wc->wildcards & mf->fww_bit) != 0;

    case MFF_TUN_ID:
        return !wc->tun_id_mask;

#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
        return !wc->reg_masks[mf->id - MFF_REG0];

    case MFF_ETH_DST:
        return ((wc->wildcards & (FWW_ETH_MCAST | FWW_DL_DST))
                == (FWW_ETH_MCAST | FWW_DL_DST));

    case MFF_VLAN_TCI:
        return !wc->vlan_tci_mask;
    case MFF_VLAN_VID:
        return !(wc->vlan_tci_mask & htons(VLAN_VID_MASK));
    case MFF_VLAN_PCP:
        return !(wc->vlan_tci_mask & htons(VLAN_PCP_MASK));

    case MFF_IPV4_SRC:
        return !wc->nw_src_mask;
    case MFF_IPV4_DST:
        return !wc->nw_dst_mask;

    case MFF_IPV6_SRC:
        return ipv6_mask_is_any(&wc->ipv6_src_mask);
    case MFF_IPV6_DST:
        return ipv6_mask_is_any(&wc->ipv6_dst_mask);

    case MFF_IP_TOS:
        return !(wc->tos_frag_mask & IP_DSCP_MASK);
    case MFF_IP_FRAG:
        return !(wc->tos_frag_mask & FLOW_FRAG_MASK);

    case MFF_ARP_SPA:
        return !wc->nw_src_mask;
    case MFF_ARP_TPA:
        return !wc->nw_dst_mask;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Initializes 'mask' with the wildcard bit pattern for field 'mf' within 'wc'.
 * Each bit in 'mask' will be set to 1 if the bit is significant for matching
 * purposes, or to 0 if it is wildcarded.
 *
 * The caller is responsible for ensuring that 'wc' corresponds to a flow that
 * meets 'mf''s prerequisites. */
void
mf_get_mask(const struct mf_field *mf, const struct flow_wildcards *wc,
            union mf_value *mask)
{
    switch (mf->id) {
    case MFF_IN_PORT:
    case MFF_ETH_SRC:
    case MFF_ETH_TYPE:
    case MFF_IP_PROTO:
    case MFF_ARP_OP:
    case MFF_ARP_SHA:
    case MFF_ARP_THA:
    case MFF_TCP_SRC:
    case MFF_TCP_DST:
    case MFF_UDP_SRC:
    case MFF_UDP_DST:
    case MFF_ICMP_TYPE:
    case MFF_ICMP_CODE:
    case MFF_ND_TARGET:
    case MFF_ND_SLL:
    case MFF_ND_TLL:
        assert(mf->fww_bit != 0);
        memset(mask, wc->wildcards & mf->fww_bit ? 0x00 : 0xff, mf->n_bytes);
        break;

    case MFF_TUN_ID:
        mask->be64 = wc->tun_id_mask;
        break;

#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
        mask->be32 = htonl(wc->reg_masks[mf->id - MFF_REG0]);
        break;

    case MFF_ETH_DST:
        memcpy(mask->mac, flow_wildcards_to_dl_dst_mask(wc->wildcards),
               ETH_ADDR_LEN);
        break;

    case MFF_VLAN_TCI:
        mask->be16 = wc->vlan_tci_mask;
        break;
    case MFF_VLAN_VID:
        mask->be16 = wc->vlan_tci_mask & htons(VLAN_VID_MASK);
        break;
    case MFF_VLAN_PCP:
        mask->u8 = vlan_tci_to_pcp(wc->vlan_tci_mask);
        break;

    case MFF_IPV4_SRC:
        mask->be32 = wc->nw_src_mask;
        break;
    case MFF_IPV4_DST:
        mask->be32 = wc->nw_dst_mask;
        break;

    case MFF_IPV6_SRC:
        mask->ipv6 = wc->ipv6_src_mask;
        break;
    case MFF_IPV6_DST:
        mask->ipv6 = wc->ipv6_dst_mask;
        break;

    case MFF_IP_TOS:
        mask->u8 = wc->tos_frag_mask & IP_DSCP_MASK;
        break;
    case MFF_IP_FRAG:
        mask->u8 = wc->tos_frag_mask & FLOW_FRAG_MASK;
        break;

    case MFF_ARP_SPA:
        mask->be32 = wc->nw_src_mask;
        break;
    case MFF_ARP_TPA:
        mask->be32 = wc->nw_dst_mask;
        break;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Tests whether 'mask' is a valid wildcard bit pattern for 'mf'.  Returns true
 * if the mask is valid, false otherwise. */
bool
mf_is_mask_valid(const struct mf_field *mf, const union mf_value *mask)
{
    switch (mf->maskable) {
    case MFM_NONE:
        return (is_all_zeros((const uint8_t *) mask, mf->n_bytes) ||
                is_all_ones((const uint8_t *) mask, mf->n_bytes));

    case MFM_FULLY:
        return true;

    case MFM_CIDR:
        return (mf->n_bytes == 4
                ? ip_is_cidr(mask->be32)
                : ipv6_is_cidr(&mask->ipv6));

    case MFM_MCAST:
        return flow_wildcards_is_dl_dst_mask_valid(mask->mac);
    }

    NOT_REACHED();
}

static bool
is_ip_any(const struct flow *flow)
{
    return (flow->dl_type == htons(ETH_TYPE_IP) ||
            flow->dl_type == htons(ETH_TYPE_IPV6));
}

static bool
is_icmpv4(const struct flow *flow)
{
    return (flow->dl_type == htons(ETH_TYPE_IP)
            && flow->nw_proto == IPPROTO_ICMP);
}

static bool
is_icmpv6(const struct flow *flow)
{
    return (flow->dl_type == htons(ETH_TYPE_IPV6)
            && flow->nw_proto == IPPROTO_ICMPV6);
}

/* Returns true if 'flow' meets the prerequisites for 'mf', false otherwise. */
bool
mf_are_prereqs_ok(const struct mf_field *mf, const struct flow *flow)
{
    switch (mf->prereqs) {
    case MFP_NONE:
        return true;

    case MFP_ARP:
        return flow->dl_type == htons(ETH_TYPE_ARP);
    case MFP_IPV4:
        return flow->dl_type == htons(ETH_TYPE_IP);
    case MFP_IPV6:
        return flow->dl_type == htons(ETH_TYPE_IPV6);
    case MFP_IP_ANY:
        return is_ip_any(flow);

    case MFP_TCP:
        return is_ip_any(flow) && flow->nw_proto == IPPROTO_TCP;
    case MFP_UDP:
        return is_ip_any(flow) && flow->nw_proto == IPPROTO_UDP;
    case MFP_ICMPV6:
        return is_icmpv6(flow);
    case MFP_ICMP_ANY:
        return is_icmpv4(flow) || is_icmpv6(flow);

    case MFP_ND:
        return (is_icmpv6(flow)
                && flow->tp_dst == htons(0)
                && (flow->tp_src == htons(ND_NEIGHBOR_SOLICIT) ||
                    flow->tp_src == htons(ND_NEIGHBOR_ADVERT)));
    case MFP_ND_SOLICIT:
        return (is_icmpv6(flow)
                && flow->tp_dst == htons(0)
                && (flow->tp_src == htons(ND_NEIGHBOR_SOLICIT)));
    case MFP_ND_ADVERT:
        return (is_icmpv6(flow)
                && flow->tp_dst == htons(0)
                && (flow->tp_src == htons(ND_NEIGHBOR_ADVERT)));
    }

    NOT_REACHED();
}

/* Returns true if 'value' may be a valid value *as part of a masked match*,
 * false otherwise.
 *
 * A value is not rejected just because it is not valid for the field in
 * question, but only if it doesn't make sense to test the bits in question at
 * all.  For example, the MFF_VLAN_TCI field will never have a nonzero value
 * without the VLAN_CFI bit being set, but we can't reject those values because
 * it is still legitimate to test just for those bits (see the documentation
 * for NXM_OF_VLAN_TCI in nicira-ext.h).  On the other hand, there is never a
 * reason to set the low bit of MFF_IP_TOS to 1, so we reject that. */
bool
mf_is_value_valid(const struct mf_field *mf, const union mf_value *value)
{
    switch (mf->id) {
    case MFF_TUN_ID:
    case MFF_IN_PORT:
#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
    case MFF_ETH_SRC:
    case MFF_ETH_DST:
    case MFF_ETH_TYPE:
    case MFF_VLAN_TCI:
    case MFF_IPV4_SRC:
    case MFF_IPV4_DST:
    case MFF_IPV6_SRC:
    case MFF_IPV6_DST:
    case MFF_IP_PROTO:
    case MFF_ARP_SPA:
    case MFF_ARP_TPA:
    case MFF_ARP_SHA:
    case MFF_ARP_THA:
    case MFF_TCP_SRC:
    case MFF_TCP_DST:
    case MFF_UDP_SRC:
    case MFF_UDP_DST:
    case MFF_ICMP_TYPE:
    case MFF_ICMP_CODE:
    case MFF_ND_TARGET:
    case MFF_ND_SLL:
    case MFF_ND_TLL:
        return true;

    case MFF_IP_TOS:
        return !(value->u8 & ~IP_DSCP_MASK);
    case MFF_IP_FRAG:
        return !(value->u8 & ~FLOW_FRAG_MASK);

    case MFF_ARP_OP:
        return !(value->be16 & htons(0xff00));

    case MFF_VLAN_VID:
        return !(value->be16 & htons(VLAN_CFI | VLAN_PCP_MASK));

    case MFF_VLAN_PCP:
        return !(value->u8 & ~7);

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Copies the value of field 'mf' from 'flow' into 'value'.  The caller is
 * responsible for ensuring that 'flow' meets 'mf''s prerequisites. */
void
mf_get_value(const struct mf_field *mf, const struct flow *flow,
             union mf_value *value)
{
    switch (mf->id) {
    case MFF_TUN_ID:
        value->be64 = flow->tun_id;
        break;

    case MFF_IN_PORT:
        value->be16 = htons(flow->in_port);
        break;

#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
        value->be32 = htonl(flow->regs[mf->id - MFF_REG0]);
        break;

    case MFF_ETH_SRC:
        memcpy(value->mac, flow->dl_src, ETH_ADDR_LEN);
        break;

    case MFF_ETH_DST:
        memcpy(value->mac, flow->dl_dst, ETH_ADDR_LEN);
        break;

    case MFF_ETH_TYPE:
        value->be16 = flow->dl_type;
        break;

    case MFF_VLAN_TCI:
        value->be16 = flow->vlan_tci;
        break;

    case MFF_VLAN_VID:
        value->be16 = flow->vlan_tci & htons(VLAN_VID_MASK);
        break;

    case MFF_VLAN_PCP:
        value->u8 = vlan_tci_to_pcp(flow->vlan_tci);
        break;

    case MFF_IPV4_SRC:
        value->be32 = flow->nw_src;
        break;

    case MFF_IPV4_DST:
        value->be32 = flow->nw_dst;
        break;

    case MFF_IPV6_SRC:
        value->ipv6 = flow->ipv6_src;
        break;

    case MFF_IPV6_DST:
        value->ipv6 = flow->ipv6_dst;
        break;

    case MFF_IP_PROTO:
        value->u8 = flow->nw_proto;
        break;

    case MFF_IP_TOS:
        value->u8 = flow->tos_frag & IP_DSCP_MASK;
        break;

    case MFF_IP_FRAG:
        value->u8 = flow->tos_frag & FLOW_FRAG_MASK;
        break;

    case MFF_ARP_OP:
        value->be16 = htons(flow->nw_proto);
        break;

    case MFF_ARP_SPA:
        value->be32 = flow->nw_src;
        break;

    case MFF_ARP_TPA:
        value->be32 = flow->nw_dst;
        break;

    case MFF_ARP_SHA:
    case MFF_ND_SLL:
        memcpy(value->mac, flow->arp_sha, ETH_ADDR_LEN);
        break;

    case MFF_ARP_THA:
    case MFF_ND_TLL:
        memcpy(value->mac, flow->arp_tha, ETH_ADDR_LEN);
        break;

    case MFF_TCP_SRC:
        value->be16 = flow->tp_src;
        break;

    case MFF_TCP_DST:
        value->be16 = flow->tp_dst;
        break;

    case MFF_UDP_SRC:
        value->be16 = flow->tp_src;
        break;

    case MFF_UDP_DST:
        value->be16 = flow->tp_dst;
        break;

    case MFF_ICMP_TYPE:
        value->u8 = ntohs(flow->tp_src);
        break;

    case MFF_ICMP_CODE:
        value->u8 = ntohs(flow->tp_dst);
        break;

    case MFF_ND_TARGET:
        value->ipv6 = flow->nd_target;
        break;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Makes 'rule' match field 'mf' exactly, with the value matched taken from
 * 'value'.  The caller is responsible for ensuring that 'rule' meets 'mf''s
 * prerequisites. */
void
mf_set_value(const struct mf_field *mf,
             const union mf_value *value, struct cls_rule *rule)
{
    switch (mf->id) {
    case MFF_TUN_ID:
        cls_rule_set_tun_id(rule, value->be64);
        break;

    case MFF_IN_PORT:
        cls_rule_set_in_port(rule, ntohs(value->be16));
        break;

#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
#if FLOW_N_REGS > 0
        cls_rule_set_reg(rule, mf->id - MFF_REG0, ntohl(value->be32));
        break;
#endif

    case MFF_ETH_SRC:
        cls_rule_set_dl_src(rule, value->mac);
        break;

    case MFF_ETH_DST:
        cls_rule_set_dl_dst(rule, value->mac);
        break;

    case MFF_ETH_TYPE:
        cls_rule_set_dl_type(rule, value->be16);
        break;

    case MFF_VLAN_TCI:
        cls_rule_set_dl_tci(rule, value->be16);
        break;

    case MFF_VLAN_VID:
        cls_rule_set_dl_vlan(rule, value->be16);
        break;

    case MFF_VLAN_PCP:
        cls_rule_set_dl_vlan_pcp(rule, value->u8);
        break;

    case MFF_IPV4_SRC:
        cls_rule_set_nw_src(rule, value->be32);
        break;

    case MFF_IPV4_DST:
        cls_rule_set_nw_dst(rule, value->be32);
        break;

    case MFF_IPV6_SRC:
        cls_rule_set_ipv6_src(rule, &value->ipv6);
        break;

    case MFF_IPV6_DST:
        cls_rule_set_ipv6_dst(rule, &value->ipv6);
        break;

    case MFF_IP_PROTO:
        cls_rule_set_nw_proto(rule, value->u8);
        break;

    case MFF_IP_TOS:
        cls_rule_set_nw_tos(rule, value->u8);
        break;

    case MFF_IP_FRAG:
        cls_rule_set_frag(rule, value->u8);
        break;

    case MFF_ARP_OP:
        cls_rule_set_nw_proto(rule, ntohs(value->be16));
        break;

    case MFF_ARP_SPA:
        cls_rule_set_nw_src(rule, value->be32);
        break;

    case MFF_ARP_TPA:
        cls_rule_set_nw_dst(rule, value->be32);
        break;

    case MFF_ARP_SHA:
    case MFF_ND_SLL:
        cls_rule_set_arp_sha(rule, value->mac);
        break;

    case MFF_ARP_THA:
    case MFF_ND_TLL:
        cls_rule_set_arp_tha(rule, value->mac);
        break;

    case MFF_TCP_SRC:
        cls_rule_set_tp_src(rule, value->be16);
        break;

    case MFF_TCP_DST:
        cls_rule_set_tp_dst(rule, value->be16);
        break;

    case MFF_UDP_SRC:
        cls_rule_set_tp_src(rule, value->be16);
        break;

    case MFF_UDP_DST:
        cls_rule_set_tp_dst(rule, value->be16);
        break;

    case MFF_ICMP_TYPE:
        cls_rule_set_icmp_type(rule, value->u8);
        break;

    case MFF_ICMP_CODE:
        cls_rule_set_icmp_code(rule, value->u8);
        break;

    case MFF_ND_TARGET:
        cls_rule_set_nd_target(rule, &value->ipv6);
        break;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Makes 'rule' wildcard field 'mf'.
 *
 * The caller is responsible for ensuring that 'rule' meets 'mf''s
 * prerequisites. */
void
mf_set_wild(const struct mf_field *mf, struct cls_rule *rule)
{
    switch (mf->id) {
    case MFF_TUN_ID:
        cls_rule_set_tun_id_masked(rule, htonll(0), htonll(0));
        break;

    case MFF_IN_PORT:
        rule->wc.wildcards |= FWW_IN_PORT;
        rule->flow.in_port = 0;
        break;

#if FLOW_N_REGS > 0
    case MFF_REG0:
        cls_rule_set_reg_masked(rule, 0, 0, 0);
        break;
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
        cls_rule_set_reg_masked(rule, 1, 0, 0);
        break;
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
        cls_rule_set_reg_masked(rule, 2, 0, 0);
        break;
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
        cls_rule_set_reg_masked(rule, 3, 0, 0);
        break;
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
        cls_rule_set_reg_masked(rule, 4, 0, 0);
        break;
#endif
#if FLOW_N_REGS > 5
#error
#endif

    case MFF_ETH_SRC:
        rule->wc.wildcards |= FWW_DL_SRC;
        memset(rule->flow.dl_src, 0, sizeof rule->flow.dl_src);
        break;

    case MFF_ETH_DST:
        rule->wc.wildcards |= FWW_DL_DST | FWW_ETH_MCAST;
        memset(rule->flow.dl_dst, 0, sizeof rule->flow.dl_dst);
        break;

    case MFF_ETH_TYPE:
        rule->wc.wildcards |= FWW_DL_TYPE;
        rule->flow.dl_type = htons(0);
        break;

    case MFF_VLAN_TCI:
        cls_rule_set_dl_tci_masked(rule, htons(0), htons(0));
        break;

    case MFF_VLAN_VID:
        cls_rule_set_any_vid(rule);
        break;

    case MFF_VLAN_PCP:
        cls_rule_set_any_pcp(rule);
        break;

    case MFF_IPV4_SRC:
    case MFF_ARP_SPA:
        cls_rule_set_nw_src_masked(rule, htonl(0), htonl(0));
        break;

    case MFF_IPV4_DST:
    case MFF_ARP_TPA:
        cls_rule_set_nw_dst_masked(rule, htonl(0), htonl(0));
        break;

    case MFF_IPV6_SRC:
        memset(&rule->wc.ipv6_src_mask, 0, sizeof rule->wc.ipv6_src_mask);
        memset(&rule->flow.ipv6_src, 0, sizeof rule->flow.ipv6_src);
        break;

    case MFF_IPV6_DST:
        memset(&rule->wc.ipv6_dst_mask, 0, sizeof rule->wc.ipv6_dst_mask);
        memset(&rule->flow.ipv6_dst, 0, sizeof rule->flow.ipv6_dst);
        break;

    case MFF_IP_PROTO:
        rule->wc.wildcards |= FWW_NW_PROTO;
        rule->flow.nw_proto = 0;
        break;

    case MFF_IP_TOS:
        rule->wc.tos_frag_mask |= IP_DSCP_MASK;
        rule->flow.tos_frag &= ~IP_DSCP_MASK;
        break;

    case MFF_IP_FRAG:
        rule->wc.tos_frag_mask |= FLOW_FRAG_MASK;
        rule->flow.tos_frag &= ~FLOW_FRAG_MASK;
        break;

    case MFF_ARP_OP:
        rule->wc.wildcards |= FWW_NW_PROTO;
        rule->flow.nw_proto = 0;
        break;

    case MFF_ARP_SHA:
    case MFF_ND_SLL:
        rule->wc.wildcards |= FWW_ARP_SHA;
        memset(rule->flow.arp_sha, 0, sizeof rule->flow.arp_sha);
        break;

    case MFF_ARP_THA:
    case MFF_ND_TLL:
        rule->wc.wildcards |= FWW_ARP_THA;
        memset(rule->flow.arp_tha, 0, sizeof rule->flow.arp_tha);
        break;

    case MFF_TCP_SRC:
    case MFF_UDP_SRC:
    case MFF_ICMP_TYPE:
        rule->wc.wildcards |= FWW_TP_SRC;
        rule->flow.tp_src = htons(0);
        break;

    case MFF_TCP_DST:
    case MFF_UDP_DST:
    case MFF_ICMP_CODE:
        rule->wc.wildcards |= FWW_TP_DST;
        rule->flow.tp_dst = htons(0);
        break;

    case MFF_ND_TARGET:
        rule->wc.wildcards |= FWW_ND_TARGET;
        memset(&rule->flow.nd_target, 0, sizeof rule->flow.nd_target);
        break;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Makes 'rule' match field 'mf' with the specified 'value' and 'mask'.
 * 'value' specifies a value to match and 'mask' specifies a wildcard pattern,
 * with a 1-bit indicating that the corresponding value bit must match and a
 * 0-bit indicating a don't-care.
 *
 * If 'mask' is NULL or points to all-1-bits, then this call is equivalent to
 * mf_set_value(mf, value, rule).  If 'mask' points to all-0-bits, then this
 * call is equivalent to mf_set_wild(mf, rule).
 *
 * 'mask' must be a valid mask for 'mf' (see mf_is_mask_valid()).  The caller
 * is responsible for ensuring that 'rule' meets 'mf''s prerequisites. */
void
mf_set(const struct mf_field *mf,
       const union mf_value *value, const union mf_value *mask,
       struct cls_rule *rule)
{
    if (!mask || is_all_ones((const uint8_t *) mask, mf->n_bytes)) {
        mf_set_value(mf, value, rule);
        return;
    } else if (is_all_zeros((const uint8_t *) mask, mf->n_bytes)) {
        mf_set_wild(mf, rule);
        return;
    }

    switch (mf->id) {
    case MFF_IN_PORT:
    case MFF_ETH_SRC:
    case MFF_ETH_TYPE:
    case MFF_VLAN_VID:
    case MFF_VLAN_PCP:
    case MFF_IP_PROTO:
    case MFF_IP_TOS:
    case MFF_ARP_OP:
    case MFF_ARP_SHA:
    case MFF_ARP_THA:
    case MFF_TCP_SRC:
    case MFF_TCP_DST:
    case MFF_UDP_SRC:
    case MFF_UDP_DST:
    case MFF_ICMP_TYPE:
    case MFF_ICMP_CODE:
    case MFF_ND_TARGET:
    case MFF_ND_SLL:
    case MFF_ND_TLL:
        NOT_REACHED();

    case MFF_TUN_ID:
        cls_rule_set_tun_id_masked(rule, value->be64, mask->be64);
        break;

#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
        cls_rule_set_reg_masked(rule, mf->id - MFF_REG0,
                                ntohl(value->be32), ntohl(mask->be32));
        break;

    case MFF_ETH_DST:
        if (flow_wildcards_is_dl_dst_mask_valid(mask->mac)) {
            cls_rule_set_dl_dst_masked(rule, value->mac, mask->mac);
        }
        break;

    case MFF_VLAN_TCI:
        cls_rule_set_dl_tci_masked(rule, value->be16, mask->be16);
        break;

    case MFF_IPV4_SRC:
        cls_rule_set_nw_src_masked(rule, value->be32, mask->be32);
        break;

    case MFF_IPV4_DST:
        cls_rule_set_nw_dst_masked(rule, value->be32, mask->be32);
        break;

    case MFF_IPV6_SRC:
        cls_rule_set_ipv6_src_masked(rule, &value->ipv6, &mask->ipv6);
        break;

    case MFF_IPV6_DST:
        cls_rule_set_ipv6_dst_masked(rule, &value->ipv6, &mask->ipv6);
        break;

    case MFF_IP_FRAG:
        cls_rule_set_frag_masked(rule, value->u8, mask->u8);
        break;

    case MFF_ARP_SPA:
        cls_rule_set_nw_src_masked(rule, value->be32, mask->be32);
        break;

    case MFF_ARP_TPA:
        cls_rule_set_nw_dst_masked(rule, value->be32, mask->be32);
        break;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

/* Makes a subfield starting at bit offset 'ofs' and continuing for 'n_bits' in
 * 'rule''s field 'mf' exactly match the 'n_bits' least-significant bits of
 * 'x'.
 *
 * Example: suppose that 'mf' is originally the following 2-byte field in
 * 'rule':
 *
 *     value == 0xe00a == 2#1110000000001010
 *      mask == 0xfc3f == 2#1111110000111111
 *
 * The call mf_set_subfield(mf, 0x55, 8, 7, rule) would have the following
 * effect (note that 0x55 is 2#1010101):
 *
 *     value == 0xd50a == 2#1101010100001010
 *      mask == 0xff3f == 2#1111111100111111
 *
 * The caller is responsible for ensuring that the result will be a valid
 * wildcard pattern for 'mf'.  The caller is responsible for ensuring that
 * 'rule' meets 'mf''s prerequisites. */
void
mf_set_subfield(const struct mf_field *mf, uint64_t x, unsigned int ofs,
                unsigned int n_bits, struct cls_rule *rule)
{
    if (ofs == 0 && mf->n_bytes * 8 == n_bits) {
        union mf_value value;
        int i;

        for (i = mf->n_bytes - 1; i >= 0; i--) {
            ((uint8_t *) &value)[i] = x;
            x >>= 8;
        }
        mf_set_value(mf, &value, rule);
    } else {
        union mf_value value, mask;
        uint8_t *vp, *mp;
        unsigned int byte_ofs;

        mf_get(mf, rule, &value, &mask);

        byte_ofs = mf->n_bytes - ofs / 8;
        vp = &((uint8_t *) &value)[byte_ofs];
        mp = &((uint8_t *) &mask)[byte_ofs];
        if (ofs % 8) {
            unsigned int chunk = MIN(8 - ofs % 8, n_bits);
            uint8_t chunk_mask = ((1 << chunk) - 1) << (ofs % 8);

            *--vp &= ~chunk_mask;
            *vp   |= chunk_mask & (x << (ofs % 8));
            *--mp |= chunk_mask;

            x >>= chunk;
            n_bits -= chunk;
            ofs += chunk;
        }
        while (n_bits >= 8) {
            *--vp = x;
            *--mp = 0xff;
            x >>= 8;
            n_bits -= 8;
            ofs += 8;
        }
        if (n_bits) {
            uint8_t chunk_mask = (1 << n_bits) - 1;

            *--vp &= ~chunk_mask;
            *vp   |= chunk_mask & x;
            *--mp |= chunk_mask;
        }

        mf_set(mf, &value, &mask, rule);
    }
}

/* Copies the value and wildcard bit pattern for 'mf' from 'rule' into the
 * 'value' and 'mask', respectively. */
void
mf_get(const struct mf_field *mf, const struct cls_rule *rule,
       union mf_value *value, union mf_value *mask)
{
    mf_get_value(mf, &rule->flow, value);
    mf_get_mask(mf, &rule->wc, mask);
}

/* Assigns a random value for field 'mf' to 'value'. */
void
mf_random_value(const struct mf_field *mf, union mf_value *value)
{
    random_bytes(value, mf->n_bytes);

    switch (mf->id) {
    case MFF_TUN_ID:
    case MFF_IN_PORT:
#if FLOW_N_REGS > 0
    case MFF_REG0:
#endif
#if FLOW_N_REGS > 1
    case MFF_REG1:
#endif
#if FLOW_N_REGS > 2
    case MFF_REG2:
#endif
#if FLOW_N_REGS > 3
    case MFF_REG3:
#endif
#if FLOW_N_REGS > 4
    case MFF_REG4:
#endif
#if FLOW_N_REGS > 5
#error
#endif
    case MFF_ETH_SRC:
    case MFF_ETH_DST:
    case MFF_ETH_TYPE:
    case MFF_VLAN_TCI:
    case MFF_IPV4_SRC:
    case MFF_IPV4_DST:
    case MFF_IPV6_SRC:
    case MFF_IPV6_DST:
    case MFF_IP_PROTO:
    case MFF_ARP_SPA:
    case MFF_ARP_TPA:
    case MFF_ARP_SHA:
    case MFF_ARP_THA:
    case MFF_TCP_SRC:
    case MFF_TCP_DST:
    case MFF_UDP_SRC:
    case MFF_UDP_DST:
    case MFF_ICMP_TYPE:
    case MFF_ICMP_CODE:
    case MFF_ND_TARGET:
    case MFF_ND_SLL:
    case MFF_ND_TLL:
        break;

    case MFF_IP_TOS:
        value->u8 &= ~0x03;
        break;

    case MFF_IP_FRAG:
        value->u8 &= FLOW_FRAG_MASK;
        break;

    case MFF_ARP_OP:
        value->be16 &= htons(0xff);
        break;

    case MFF_VLAN_VID:
        value->be16 &= htons(VLAN_VID_MASK);
        break;

    case MFF_VLAN_PCP:
        value->u8 &= 0x07;
        break;

    case MFF_N_IDS:
    default:
        NOT_REACHED();
    }
}

static char *
mf_from_integer_string(const struct mf_field *mf, const char *s,
                       uint8_t *valuep, uint8_t *maskp)
{
    unsigned long long int integer, mask;
    char *tail;
    int i;

    errno = 0;
    integer = strtoull(s, &tail, 0);
    if (errno || (*tail != '\0' && *tail != '/')) {
        goto syntax_error;
    }

    if (*tail == '/') {
        mask = strtoull(tail + 1, &tail, 0);
        if (errno || *tail != '\0') {
            goto syntax_error;
        }
    } else {
        mask = ULLONG_MAX;
    }

    for (i = mf->n_bytes - 1; i >= 0; i--) {
        valuep[i] = integer;
        maskp[i] = mask;
        integer >>= 8;
        mask >>= 8;
    }
    if (integer) {
        return xasprintf("%s: value too large for %u-byte field %s",
                         s, mf->n_bytes, mf->name);
    }
    return NULL;

syntax_error:
    return xasprintf("%s: bad syntax for %s", s, mf->name);
}

static char *
mf_from_ethernet_string(const struct mf_field *mf, const char *s,
                        uint8_t mac[ETH_ADDR_LEN],
                        uint8_t mask[ETH_ADDR_LEN])
{
    assert(mf->n_bytes == ETH_ADDR_LEN);

    switch (sscanf(s, ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT,
                   ETH_ADDR_SCAN_ARGS(mac), ETH_ADDR_SCAN_ARGS(mask))){
    case ETH_ADDR_SCAN_COUNT * 2:
        return NULL;

    case ETH_ADDR_SCAN_COUNT:
        memset(mask, 0xff, ETH_ADDR_LEN);
        return NULL;

    default:
        return xasprintf("%s: invalid Ethernet address", s);
    }
}

static char *
mf_from_ipv4_string(const struct mf_field *mf, const char *s,
                    ovs_be32 *ip, ovs_be32 *mask)
{
    int prefix;

    assert(mf->n_bytes == sizeof *ip);

    if (sscanf(s, IP_SCAN_FMT"/"IP_SCAN_FMT,
               IP_SCAN_ARGS(ip), IP_SCAN_ARGS(mask)) == IP_SCAN_COUNT * 2) {
        /* OK. */
    } else if (sscanf(s, IP_SCAN_FMT"/%d",
                      IP_SCAN_ARGS(ip), &prefix) == IP_SCAN_COUNT + 1) {
        if (prefix <= 0 || prefix > 32) {
            return xasprintf("%s: network prefix bits not between 1 and "
                             "32", s);
        } else if (prefix == 32) {
            *mask = htonl(UINT32_MAX);
        } else {
            *mask = htonl(((1u << prefix) - 1) << (32 - prefix));
        }
    } else if (sscanf(s, IP_SCAN_FMT, IP_SCAN_ARGS(ip)) == IP_SCAN_COUNT) {
        *mask = htonl(UINT32_MAX);
    } else {
        return xasprintf("%s: invalid IP address", s);
    }
    return NULL;
}

static char *
mf_from_ipv6_string(const struct mf_field *mf, const char *s,
                    struct in6_addr *value, struct in6_addr *mask)
{
    char *str = xstrdup(s);
    char *save_ptr = NULL;
    const char *name, *netmask;
    int retval;

    assert(mf->n_bytes == sizeof *value);

    name = strtok_r(str, "/", &save_ptr);
    retval = name ? lookup_ipv6(name, value) : EINVAL;
    if (retval) {
        char *err;

        err = xasprintf("%s: could not convert to IPv6 address", str);
        free(str);

        return err;
    }

    netmask = strtok_r(NULL, "/", &save_ptr);
    if (netmask) {
        int prefix = atoi(netmask);
        if (prefix <= 0 || prefix > 128) {
            free(str);
            return xasprintf("%s: prefix bits not between 1 and 128", s);
        } else {
            *mask = ipv6_create_mask(prefix);
        }
    } else {
        *mask = in6addr_exact;
    }
    free(str);

    return NULL;
}

static char *
mf_from_ofp_port_string(const struct mf_field *mf, const char *s,
                        ovs_be16 *valuep, ovs_be16 *maskp)
{
    uint16_t port;

    assert(mf->n_bytes == sizeof(ovs_be16));
    if (ofputil_port_from_string(s, &port)) {
        *valuep = htons(port);
        *maskp = htons(UINT16_MAX);
        return NULL;
    } else {
        return mf_from_integer_string(mf, s,
                                      (uint8_t *) valuep, (uint8_t *) maskp);
    }
}

struct frag_handling {
    const char *name;
    uint8_t mask;
    uint8_t value;
};

static const struct frag_handling all_frags[] = {
#define A FLOW_FRAG_ANY
#define L FLOW_FRAG_LATER
    /* name               mask  value */

    { "no",               A|L,  0     },
    { "first",            A|L,  A     },
    { "later",            A|L,  A|L   },

    { "no",               A,    0     },
    { "yes",              A,    A     },

    { "not_later",        L,    0     },
    { "later",            L,    L     },
#undef A
#undef L
};

static char *
mf_from_frag_string(const char *s, uint8_t *valuep, uint8_t *maskp)
{
    const struct frag_handling *h;

    for (h = all_frags; h < &all_frags[ARRAY_SIZE(all_frags)]; h++) {
        if (!strcasecmp(s, h->name)) {
            /* We force the upper bits of the mask on to make mf_parse_value()
             * happy (otherwise it will never think it's an exact match.) */
            *maskp = h->mask | ~FLOW_FRAG_MASK;
            *valuep = h->value;
            return NULL;
        }
    }

    return xasprintf("%s: unknown fragment type (valid types are \"no\", "
                     "\"yes\", \"first\", \"later\", \"not_first\"", s);
}

/* Parses 's', a string value for field 'mf', into 'value' and 'mask'.  Returns
 * NULL if successful, otherwise a malloc()'d string describing the error. */
char *
mf_parse(const struct mf_field *mf, const char *s,
         union mf_value *value, union mf_value *mask)
{
    if (!strcasecmp(s, "any") || !strcmp(s, "*")) {
        memset(value, 0, mf->n_bytes);
        memset(mask, 0, mf->n_bytes);
        return NULL;
    }

    switch (mf->string) {
    case MFS_DECIMAL:
    case MFS_HEXADECIMAL:
        return mf_from_integer_string(mf, s,
                                      (uint8_t *) value, (uint8_t *) mask);

    case MFS_ETHERNET:
        return mf_from_ethernet_string(mf, s, value->mac, mask->mac);

    case MFS_IPV4:
        return mf_from_ipv4_string(mf, s, &value->be32, &mask->be32);

    case MFS_IPV6:
        return mf_from_ipv6_string(mf, s, &value->ipv6, &mask->ipv6);

    case MFS_OFP_PORT:
        return mf_from_ofp_port_string(mf, s, &value->be16, &mask->be16);

    case MFS_FRAG:
        return mf_from_frag_string(s, &value->u8, &mask->u8);
    }
    NOT_REACHED();
}

/* Parses 's', a string value for field 'mf', into 'value'.  Returns NULL if
 * successful, otherwise a malloc()'d string describing the error. */
char *
mf_parse_value(const struct mf_field *mf, const char *s, union mf_value *value)
{
    union mf_value mask;
    char *error;

    error = mf_parse(mf, s, value, &mask);
    if (error) {
        return error;
    }

    if (!is_all_ones((const uint8_t *) &mask, mf->n_bytes)) {
        return xasprintf("%s: wildcards not allowed here", s);
    }
    return NULL;
}

static void
mf_format_integer_string(const struct mf_field *mf, const uint8_t *valuep,
                         const uint8_t *maskp, struct ds *s)
{
    unsigned long long int integer;
    int i;

    assert(mf->n_bytes <= 8);

    integer = 0;
    for (i = 0; i < mf->n_bytes; i++) {
        integer = (integer << 8) | valuep[i];
    }
    if (mf->string == MFS_HEXADECIMAL) {
        ds_put_format(s, "%#llx", integer);
    } else {
        ds_put_format(s, "%lld", integer);
    }

    if (maskp) {
        unsigned long long int mask;

        mask = 0;
        for (i = 0; i < mf->n_bytes; i++) {
            mask = (mask << 8) | maskp[i];
        }

        /* I guess we could write the mask in decimal for MFS_DECIMAL but I'm
         * not sure that that a bit-mask written in decimal is ever easier to
         * understand than the same bit-mask written in hexadecimal. */
        ds_put_format(s, "/%#llx", mask);
    }
}

static void
mf_format_frag_string(const uint8_t *valuep, const uint8_t *maskp,
                      struct ds *s)
{
    const struct frag_handling *h;
    uint8_t value = *valuep;
    uint8_t mask = *maskp;

    value &= mask;
    mask &= FLOW_FRAG_MASK;

    for (h = all_frags; h < &all_frags[ARRAY_SIZE(all_frags)]; h++) {
        if (value == h->value && mask == h->mask) {
            ds_put_cstr(s, h->name);
            return;
        }
    }
    ds_put_cstr(s, "<error>");
}

/* Appends to 's' a string representation of field 'mf' whose value is in
 * 'value' and 'mask'.  'mask' may be NULL to indicate an exact match. */
void
mf_format(const struct mf_field *mf,
          const union mf_value *value, const union mf_value *mask,
          struct ds *s)
{
    if (mask) {
        if (is_all_zeros((const uint8_t *) mask, mf->n_bytes)) {
            ds_put_cstr(s, "ANY");
            return;
        } else if (is_all_ones((const uint8_t *) mask, mf->n_bytes)) {
            mask = NULL;
        }
    }

    switch (mf->string) {
    case MFS_OFP_PORT:
        if (!mask) {
            ofputil_format_port(ntohs(value->be16), s);
            break;
        }
        /* fall through */
    case MFS_DECIMAL:
    case MFS_HEXADECIMAL:
        mf_format_integer_string(mf, (uint8_t *) value, (uint8_t *) mask, s);
        break;

    case MFS_ETHERNET:
        ds_put_format(s, ETH_ADDR_FMT, ETH_ADDR_ARGS(value->mac));
        if (mask) {
            ds_put_format(s, "/"ETH_ADDR_FMT, ETH_ADDR_ARGS(mask->mac));
        }
        break;

    case MFS_IPV4:
        ip_format_masked(value->be32, mask ? mask->be32 : htonl(UINT32_MAX),
                         s);
        break;

    case MFS_IPV6:
        print_ipv6_masked(s, &value->ipv6, mask ? &mask->ipv6 : NULL);
        break;

    case MFS_FRAG:
        mf_format_frag_string(&value->u8, &mask->u8, s);
        break;

    default:
        NOT_REACHED();
    }
}
