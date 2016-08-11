/* Copyright (c) 2015, 2016 Red Hat, Inc.
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

#include "pinctrl.h"

#include "coverage.h"
#include "csum.h"
#include "dirs.h"
#include "dp-packet.h"
#include "flow.h"
#include "lport.h"
#include "nx-match.h"
#include "ovn-controller.h"
#include "lib/packets.h"
#include "lib/sset.h"
#include "openvswitch/ofp-actions.h"
#include "openvswitch/ofp-msgs.h"
#include "openvswitch/ofp-print.h"
#include "openvswitch/ofp-util.h"
#include "openvswitch/vlog.h"

#include "lib/dhcp.h"
#include "ovn-controller.h"
#include "ovn/actions.h"
#include "ovn/lib/logical-fields.h"
#include "ovn/lib/ovn-util.h"
#include "poll-loop.h"
#include "rconn.h"
#include "socket-util.h"
#include "timeval.h"
#include "vswitch-idl.h"

VLOG_DEFINE_THIS_MODULE(pinctrl);

/* OpenFlow connection to the switch. */
static struct rconn *swconn;

/* Last seen sequence number for 'swconn'.  When this differs from
 * rconn_get_connection_seqno(rconn), 'swconn' has reconnected. */
static unsigned int conn_seq_no;

static void pinctrl_handle_put_mac_binding(const struct flow *md,
                                           const struct flow *headers,
                                           bool is_arp);
static void init_put_mac_bindings(void);
static void destroy_put_mac_bindings(void);
static void run_put_mac_bindings(struct controller_ctx *,
                                 const struct lport_index *lports);
static void wait_put_mac_bindings(struct controller_ctx *);
static void flush_put_mac_bindings(void);

static void init_send_garps(void);
static void destroy_send_garps(void);
static void send_garp_wait(void);
static void send_garp_run(const struct ovsrec_bridge *,
                          const char *chassis_id,
                          const struct lport_index *lports,
                          struct hmap *local_datapaths);
static void pinctrl_handle_nd_na(const struct flow *ip_flow,
                                 const struct match *md,
                                 struct ofpbuf *userdata);
static void reload_metadata(struct ofpbuf *ofpacts,
                            const struct match *md);

COVERAGE_DEFINE(pinctrl_drop_put_mac_binding);

void
pinctrl_init(void)
{
    swconn = rconn_create(5, 0, DSCP_DEFAULT, 1 << OFP13_VERSION);
    conn_seq_no = 0;
    init_put_mac_bindings();
    init_send_garps();
}

static ovs_be32
queue_msg(struct ofpbuf *msg)
{
    const struct ofp_header *oh = msg->data;
    ovs_be32 xid = oh->xid;

    rconn_send(swconn, msg, NULL);
    return xid;
}

/* Sets up 'swconn', a newly (re)connected connection to a switch. */
static void
pinctrl_setup(struct rconn *swconn)
{
    /* Fetch the switch configuration.  The response later will allow us to
     * change the miss_send_len to UINT16_MAX, so that we can enable
     * asynchronous messages. */
    queue_msg(ofpraw_alloc(OFPRAW_OFPT_GET_CONFIG_REQUEST,
                           rconn_get_version(swconn), 0));

    /* Set a packet-in format that supports userdata.  */
    queue_msg(ofputil_make_set_packet_in_format(rconn_get_version(swconn),
                                                NXPIF_NXT_PACKET_IN2));
}

static void
set_switch_config(struct rconn *swconn,
                  const struct ofputil_switch_config *config)
{
    enum ofp_version version = rconn_get_version(swconn);
    struct ofpbuf *request = ofputil_encode_set_config(config, version);
    queue_msg(request);
}

static void
pinctrl_handle_arp(const struct flow *ip_flow, const struct match *md,
                   struct ofpbuf *userdata)
{
    /* This action only works for IP packets, and the switch should only send
     * us IP packets this way, but check here just to be sure. */
    if (ip_flow->dl_type != htons(ETH_TYPE_IP)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "ARP action on non-IP packet (Ethertype %"PRIx16")",
                     ntohs(ip_flow->dl_type));
        return;
    }

    /* Compose an ARP packet. */
    uint64_t packet_stub[128 / 8];
    struct dp_packet packet;
    dp_packet_use_stub(&packet, packet_stub, sizeof packet_stub);
    compose_arp__(&packet);

    struct eth_header *eth = dp_packet_l2(&packet);
    eth->eth_dst = ip_flow->dl_dst;
    eth->eth_src = ip_flow->dl_src;

    struct arp_eth_header *arp = dp_packet_l3(&packet);
    arp->ar_op = htons(ARP_OP_REQUEST);
    arp->ar_sha = ip_flow->dl_src;
    put_16aligned_be32(&arp->ar_spa, ip_flow->nw_src);
    arp->ar_tha = eth_addr_zero;
    put_16aligned_be32(&arp->ar_tpa, ip_flow->nw_dst);

    if (ip_flow->vlan_tci & htons(VLAN_CFI)) {
        eth_push_vlan(&packet, htons(ETH_TYPE_VLAN_8021Q), ip_flow->vlan_tci);
    }

    /* Compose actions.
     *
     * First, copy metadata from 'md' into the packet-out via "set_field"
     * actions, then add actions from 'userdata'.
     */
    uint64_t ofpacts_stub[4096 / 8];
    struct ofpbuf ofpacts = OFPBUF_STUB_INITIALIZER(ofpacts_stub);
    enum ofp_version version = rconn_get_version(swconn);

    reload_metadata(&ofpacts, md);
    enum ofperr error = ofpacts_pull_openflow_actions(userdata, userdata->size, version, &ofpacts);
    if (error) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "failed to parse arp actions (%s)",
                     ofperr_to_string(error));
        goto exit;
    }

    struct ofputil_packet_out po = {
        .packet = dp_packet_data(&packet),
        .packet_len = dp_packet_size(&packet),
        .buffer_id = UINT32_MAX,
        .in_port = OFPP_CONTROLLER,
        .ofpacts = ofpacts.data,
        .ofpacts_len = ofpacts.size,
    };
    enum ofputil_protocol proto = ofputil_protocol_from_ofp_version(version);
    queue_msg(ofputil_encode_packet_out(&po, proto));

exit:
    dp_packet_uninit(&packet);
    ofpbuf_uninit(&ofpacts);
}

static void
pinctrl_handle_put_dhcp_opts(
    struct dp_packet *pkt_in, struct ofputil_packet_in *pin,
    struct ofpbuf *userdata, struct ofpbuf *continuation)
{
    enum ofp_version version = rconn_get_version(swconn);
    enum ofputil_protocol proto = ofputil_protocol_from_ofp_version(version);
    struct dp_packet *pkt_out_ptr = NULL;
    uint32_t success = 0;

    /* Parse result field. */
    const struct mf_field *f;
    enum ofperr ofperr = nx_pull_header(userdata, &f, NULL);
    if (ofperr) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "bad result OXM (%s)", ofperr_to_string(ofperr));
        goto exit;
    }

    /* Parse result offset and offer IP. */
    ovs_be32 *ofsp = ofpbuf_try_pull(userdata, sizeof *ofsp);
    ovs_be32 *offer_ip = ofpbuf_try_pull(userdata, sizeof *offer_ip);
    if (!ofsp || !offer_ip) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "offset or offer_ip not present in the userdata");
        goto exit;
    }

    /* Check that the result is valid and writable. */
    struct mf_subfield dst = { .field = f, .ofs = ntohl(*ofsp), .n_bits = 1 };
    ofperr = mf_check_dst(&dst, NULL);
    if (ofperr) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "bad result bit (%s)", ofperr_to_string(ofperr));
        goto exit;
    }

    if (!userdata->size) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "DHCP options not present in the userdata");
        goto exit;
    }

    /* Validate the DHCP request packet.
     * Format of the DHCP packet is
     * ------------------------------------------------------------------------
     *| UDP HEADER  | DHCP HEADER  | 4 Byte DHCP Cookie | DHCP OPTIONS(var len)|
     * ------------------------------------------------------------------------
     */
    if (dp_packet_l4_size(pkt_in) < (UDP_HEADER_LEN +
        sizeof (struct dhcp_header) + sizeof(uint32_t) + 3)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "Invalid or incomplete DHCP packet recieved");
        goto exit;
    }

    struct dhcp_header const *in_dhcp_data = dp_packet_get_udp_payload(pkt_in);
    if (in_dhcp_data->op != DHCP_OP_REQUEST) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "Invalid opcode in the DHCP packet : %d",
                     in_dhcp_data->op);
        goto exit;
    }

    /* DHCP options follow the DHCP header. The first 4 bytes of the DHCP
     * options is the DHCP magic cookie followed by the actual DHCP options.
     */
    const uint8_t *in_dhcp_opt =
        (const uint8_t *)dp_packet_get_udp_payload(pkt_in) +
        sizeof (struct dhcp_header);

    ovs_be32 magic_cookie = htonl(DHCP_MAGIC_COOKIE);
    if (memcmp(in_dhcp_opt, &magic_cookie, sizeof(ovs_be32))) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "DHCP magic cookie not present in the DHCP packet");
        goto exit;
    }

    in_dhcp_opt += 4;
    /* Check that the DHCP Message Type (opt 53) is present or not with
     * valid values - DHCP_MSG_DISCOVER or DHCP_MSG_REQUEST as the first
     * DHCP option.
     */
    if (!(in_dhcp_opt[0] == DHCP_OPT_MSG_TYPE && in_dhcp_opt[1] == 1 && (
            in_dhcp_opt[2] == DHCP_MSG_DISCOVER ||
            in_dhcp_opt[2] == DHCP_MSG_REQUEST))) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "Invalid DHCP message type : opt code = %d,"
                     " opt value = %d", in_dhcp_opt[0], in_dhcp_opt[2]);
        goto exit;
    }

    uint8_t msg_type;
    if (in_dhcp_opt[2] == DHCP_MSG_DISCOVER) {
        msg_type = DHCP_MSG_OFFER;
    } else {
        msg_type = DHCP_MSG_ACK;
    }

    /* Frame the DHCP reply packet
     * Total DHCP options length will be options stored in the userdata +
     * 16 bytes.
     *
     * --------------------------------------------------------------
     *| 4 Bytes (dhcp cookie) | 3 Bytes (option type) | DHCP options |
     * --------------------------------------------------------------
     *| 4 Bytes padding | 1 Byte (option end 0xFF ) | 4 Bytes padding|
     * --------------------------------------------------------------
     */
    uint16_t new_l4_size = UDP_HEADER_LEN + DHCP_HEADER_LEN + \
                           userdata->size + 16;
    size_t new_packet_size = pkt_in->l4_ofs + new_l4_size;

    struct dp_packet pkt_out;
    dp_packet_init(&pkt_out, new_packet_size);
    dp_packet_clear(&pkt_out);
    dp_packet_prealloc_tailroom(&pkt_out, new_packet_size);
    pkt_out_ptr = &pkt_out;

    /* Copy the L2 and L3 headers from the pkt_in as they would remain same*/
    dp_packet_put(
        &pkt_out, dp_packet_pull(pkt_in, pkt_in->l4_ofs), pkt_in->l4_ofs);

    pkt_out.l2_5_ofs = pkt_in->l2_5_ofs;
    pkt_out.l2_pad_size = pkt_in->l2_pad_size;
    pkt_out.l3_ofs = pkt_in->l3_ofs;
    pkt_out.l4_ofs = pkt_in->l4_ofs;

    struct udp_header *udp = dp_packet_put(
        &pkt_out, dp_packet_pull(pkt_in, UDP_HEADER_LEN), UDP_HEADER_LEN);

    struct dhcp_header *dhcp_data = dp_packet_put(
        &pkt_out, dp_packet_pull(pkt_in, DHCP_HEADER_LEN), DHCP_HEADER_LEN);
    dhcp_data->op = DHCP_OP_REPLY;
    dhcp_data->yiaddr = *offer_ip;
    dp_packet_put(&pkt_out, &magic_cookie, sizeof(ovs_be32));

    uint8_t *out_dhcp_opts = dp_packet_put_zeros(&pkt_out,
                                                 userdata->size + 12);
    /* DHCP option - type */
    out_dhcp_opts[0] = DHCP_OPT_MSG_TYPE;
    out_dhcp_opts[1] = 1;
    out_dhcp_opts[2] = msg_type;
    out_dhcp_opts += 3;

    memcpy(out_dhcp_opts, userdata->data, userdata->size);
    out_dhcp_opts += userdata->size;
    /* Padding */
    out_dhcp_opts += 4;
    /* End */
    out_dhcp_opts[0] = DHCP_OPT_END;

    udp->udp_len = htons(new_l4_size);

    struct ip_header *out_ip = dp_packet_l3(&pkt_out);
    out_ip->ip_tot_len = htons(pkt_out.l4_ofs - pkt_out.l3_ofs + new_l4_size);
    udp->udp_csum = 0;
    /* Checksum needs to be initialized to zero. */
    out_ip->ip_csum = 0;
    out_ip->ip_csum = csum(out_ip, sizeof *out_ip);

    pin->packet = dp_packet_data(&pkt_out);
    pin->packet_len = dp_packet_size(&pkt_out);

    success = 1;
exit:
    if (!ofperr) {
        union mf_subvalue sv;
        sv.u8_val = success;
        mf_write_subfield(&dst, &sv, &pin->flow_metadata);
    }
    queue_msg(ofputil_encode_resume(pin, continuation, proto));
    if (pkt_out_ptr) {
        dp_packet_uninit(pkt_out_ptr);
    }
}

static void
process_packet_in(const struct ofp_header *msg)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

    struct ofputil_packet_in pin;
    struct ofpbuf continuation;
    enum ofperr error = ofputil_decode_packet_in(msg, true, &pin,
                                                 NULL, NULL, &continuation);

    if (error) {
        VLOG_WARN_RL(&rl, "error decoding packet-in: %s",
                     ofperr_to_string(error));
        return;
    }
    if (pin.reason != OFPR_ACTION) {
        return;
    }

    struct ofpbuf userdata = ofpbuf_const_initializer(pin.userdata,
                                                      pin.userdata_len);
    const struct action_header *ah = ofpbuf_pull(&userdata, sizeof *ah);
    if (!ah) {
        VLOG_WARN_RL(&rl, "packet-in userdata lacks action header");
        return;
    }

    struct dp_packet packet;
    dp_packet_use_const(&packet, pin.packet, pin.packet_len);
    struct flow headers;
    flow_extract(&packet, &headers);

    switch (ntohl(ah->opcode)) {
    case ACTION_OPCODE_ARP:
        pinctrl_handle_arp(&headers, &pin.flow_metadata, &userdata);
        break;

    case ACTION_OPCODE_PUT_ARP:
        pinctrl_handle_put_mac_binding(&pin.flow_metadata.flow, &headers,
                                       true);
        break;

    case ACTION_OPCODE_PUT_DHCP_OPTS:
        pinctrl_handle_put_dhcp_opts(&packet, &pin, &userdata, &continuation);
        break;

    case ACTION_OPCODE_ND_NA:
        pinctrl_handle_nd_na(&headers, &pin.flow_metadata, &userdata);
        break;

    case ACTION_OPCODE_PUT_ND:
        pinctrl_handle_put_mac_binding(&pin.flow_metadata.flow, &headers,
                                       false);
        break;

    default:
        VLOG_WARN_RL(&rl, "unrecognized packet-in opcode %"PRIu32,
                     ntohl(ah->opcode));
        break;
    }
}

static void
pinctrl_recv(const struct ofp_header *oh, enum ofptype type)
{
    if (type == OFPTYPE_ECHO_REQUEST) {
        queue_msg(make_echo_reply(oh));
    } else if (type == OFPTYPE_GET_CONFIG_REPLY) {
        /* Enable asynchronous messages (see "Asynchronous Messages" in
         * DESIGN.md for more information). */
        struct ofputil_switch_config config;

        ofputil_decode_get_config_reply(oh, &config);
        config.miss_send_len = UINT16_MAX;
        set_switch_config(swconn, &config);
    } else if (type == OFPTYPE_PACKET_IN) {
        process_packet_in(oh);
    } else if (type != OFPTYPE_ECHO_REPLY && type != OFPTYPE_BARRIER_REPLY) {
        if (VLOG_IS_DBG_ENABLED()) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(30, 300);

            char *s = ofp_to_string(oh, ntohs(oh->length), 2);

            VLOG_DBG_RL(&rl, "OpenFlow packet ignored: %s", s);
            free(s);
        }
    }
}

void
pinctrl_run(struct controller_ctx *ctx, const struct lport_index *lports,
            const struct ovsrec_bridge *br_int,
            const char *chassis_id,
            struct hmap *local_datapaths)
{
    if (br_int) {
        char *target;

        target = xasprintf("unix:%s/%s.mgmt", ovs_rundir(), br_int->name);
        if (strcmp(target, rconn_get_target(swconn))) {
            VLOG_INFO("%s: connecting to switch", target);
            rconn_connect(swconn, target, target);
        }
        free(target);
    } else {
        rconn_disconnect(swconn);
    }

    rconn_run(swconn);

    if (rconn_is_connected(swconn)) {
        if (conn_seq_no != rconn_get_connection_seqno(swconn)) {
            pinctrl_setup(swconn);
            conn_seq_no = rconn_get_connection_seqno(swconn);
            flush_put_mac_bindings();
        }

        /* Process a limited number of messages per call. */
        for (int i = 0; i < 50; i++) {
            struct ofpbuf *msg = rconn_recv(swconn);
            if (!msg) {
                break;
            }

            const struct ofp_header *oh = msg->data;
            enum ofptype type;

            ofptype_decode(&type, oh);
            pinctrl_recv(oh, type);
            ofpbuf_delete(msg);
        }
    }

    run_put_mac_bindings(ctx, lports);
    send_garp_run(br_int, chassis_id, lports, local_datapaths);
}

void
pinctrl_wait(struct controller_ctx *ctx)
{
    wait_put_mac_bindings(ctx);
    rconn_run_wait(swconn);
    rconn_recv_wait(swconn);
    send_garp_wait();
}

void
pinctrl_destroy(void)
{
    rconn_destroy(swconn);
    destroy_put_mac_bindings();
    destroy_send_garps();
}

/* Implementation of the "put_arp" and "put_nd" OVN actions.  These
 * actions send a packet to ovn-controller, using the flow as an API
 * (see actions.h for details).  This code implements the actions by
 * updating the MAC_Binding table in the southbound database.
 *
 * This code could be a lot simpler if the database could always be updated,
 * but in fact we can only update it when ctx->ovnsb_idl_txn is nonnull.  Thus,
 * we buffer up a few put_mac_bindings (but we don't keep them longer
 * than 1 second) and apply them whenever a database transaction is
 * available. */

/* Buffered "put_mac_binding" operation. */
struct put_mac_binding {
    struct hmap_node hmap_node; /* In 'put_mac_bindings'. */

    long long int timestamp;    /* In milliseconds. */

    /* Key. */
    uint32_t dp_key;
    uint32_t port_key;
    char ip_s[INET6_ADDRSTRLEN + 1];

    /* Value. */
    struct eth_addr mac;
};

/* Contains "struct put_mac_binding"s. */
static struct hmap put_mac_bindings;

static void
init_put_mac_bindings(void)
{
    hmap_init(&put_mac_bindings);
}

static void
destroy_put_mac_bindings(void)
{
    flush_put_mac_bindings();
    hmap_destroy(&put_mac_bindings);
}

static struct put_mac_binding *
pinctrl_find_put_mac_binding(uint32_t dp_key, uint32_t port_key,
                             const char *ip_s, uint32_t hash)
{
    struct put_mac_binding *pa;
    HMAP_FOR_EACH_WITH_HASH (pa, hmap_node, hash, &put_mac_bindings) {
        if (pa->dp_key == dp_key
            && pa->port_key == port_key
            && !strcmp(pa->ip_s, ip_s)) {
            return pa;
        }
    }
    return NULL;
}

static void
pinctrl_handle_put_mac_binding(const struct flow *md,
                               const struct flow *headers, bool is_arp)
{
    uint32_t dp_key = ntohll(md->metadata);
    uint32_t port_key = md->regs[MFF_LOG_INPORT - MFF_REG0];
    char ip_s[INET6_ADDRSTRLEN];

    if (is_arp) {
        ovs_be32 ip = htonl(md->regs[0]);
        inet_ntop(AF_INET, &ip, ip_s, sizeof(ip_s));
    } else {
        ovs_be128 ip6 = hton128(flow_get_xxreg(md, 0));
        inet_ntop(AF_INET6, &ip6, ip_s, sizeof(ip_s));
    }
    uint32_t hash = hash_string(ip_s, hash_2words(dp_key, port_key));
    struct put_mac_binding *pmb
        = pinctrl_find_put_mac_binding(dp_key, port_key, ip_s, hash);
    if (!pmb) {
        if (hmap_count(&put_mac_bindings) >= 1000) {
            COVERAGE_INC(pinctrl_drop_put_mac_binding);
            return;
        }

        pmb = xmalloc(sizeof *pmb);
        hmap_insert(&put_mac_bindings, &pmb->hmap_node, hash);
        pmb->dp_key = dp_key;
        pmb->port_key = port_key;
        ovs_strlcpy(pmb->ip_s, ip_s, sizeof pmb->ip_s);
    }
    pmb->timestamp = time_msec();
    pmb->mac = headers->dl_src;
}

static void
run_put_mac_binding(struct controller_ctx *ctx,
                    const struct lport_index *lports,
                    const struct put_mac_binding *pmb)
{
    if (time_msec() > pmb->timestamp + 1000) {
        return;
    }

    /* Convert logical datapath and logical port key into lport. */
    const struct sbrec_port_binding *pb
        = lport_lookup_by_key(lports, pmb->dp_key, pmb->port_key);
    if (!pb) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

        VLOG_WARN_RL(&rl, "unknown logical port with datapath %"PRIu32" "
                     "and port %"PRIu32, pmb->dp_key, pmb->port_key);
        return;
    }

    /* Convert ethernet argument to string form for database. */
    char mac_string[ETH_ADDR_STRLEN + 1];
    snprintf(mac_string, sizeof mac_string,
             ETH_ADDR_FMT, ETH_ADDR_ARGS(pmb->mac));

    /* Check for an update an existing IP-MAC binding for this logical
     * port.
     *
     * XXX This is not very efficient. */
    const struct sbrec_mac_binding *b;
    SBREC_MAC_BINDING_FOR_EACH (b, ctx->ovnsb_idl) {
        if (!strcmp(b->logical_port, pb->logical_port)
            && !strcmp(b->ip, pmb->ip_s)) {
            if (strcmp(b->mac, mac_string)) {
                sbrec_mac_binding_set_mac(b, mac_string);
            }
            return;
        }
    }

    /* Add new IP-MAC binding for this logical port. */
    b = sbrec_mac_binding_insert(ctx->ovnsb_idl_txn);
    sbrec_mac_binding_set_logical_port(b, pb->logical_port);
    sbrec_mac_binding_set_ip(b, pmb->ip_s);
    sbrec_mac_binding_set_mac(b, mac_string);
    sbrec_mac_binding_set_datapath(b, pb->datapath);
}

static void
run_put_mac_bindings(struct controller_ctx *ctx,
                     const struct lport_index *lports)
{
    if (!ctx->ovnsb_idl_txn) {
        return;
    }

    const struct put_mac_binding *pmb;
    HMAP_FOR_EACH (pmb, hmap_node, &put_mac_bindings) {
        run_put_mac_binding(ctx, lports, pmb);
    }
    flush_put_mac_bindings();
}

static void
wait_put_mac_bindings(struct controller_ctx *ctx)
{
    if (ctx->ovnsb_idl_txn && !hmap_is_empty(&put_mac_bindings)) {
        poll_immediate_wake();
    }
}

static void
flush_put_mac_bindings(void)
{
    struct put_mac_binding *pmb;
    HMAP_FOR_EACH_POP (pmb, hmap_node, &put_mac_bindings) {
        free(pmb);
    }
}

/*
 * Send gratuitous ARP for vif on localnet.
 *
 * When a new vif on localnet is added, gratuitous ARPs are sent announcing
 * the port's mac,ip mapping.  On localnet, such announcements are needed for
 * switches and routers on the broadcast segment to update their port-mac
 * and ARP tables.
 */
struct garp_data {
    struct eth_addr ea;          /* Ethernet address of port. */
    ovs_be32 ipv4;               /* Ipv4 address of port. */
    long long int announce_time; /* Next announcement in ms. */
    int backoff;                 /* Backoff for the next announcement. */
    ofp_port_t ofport;           /* ofport used to output this GARP. */
};

/* Contains GARPs to be sent. */
static struct shash send_garp_data;

/* Next GARP announcement in ms. */
static long long int send_garp_time;

static void
init_send_garps(void)
{
    shash_init(&send_garp_data);
    send_garp_time = LLONG_MAX;
}

static void
destroy_send_garps(void)
{
    shash_destroy_free_data(&send_garp_data);
}

/* Add or update a vif for which GARPs need to be announced. */
static void
send_garp_update(const struct sbrec_port_binding *binding_rec,
                 struct simap *localnet_ofports, struct hmap *local_datapaths)
{
    /* Find the localnet ofport to send this GARP. */
    struct local_datapath *ld
        = get_local_datapath(local_datapaths,
                             binding_rec->datapath->tunnel_key);
    if (!ld || !ld->localnet_port) {
        return;
    }
    ofp_port_t ofport = u16_to_ofp(simap_get(localnet_ofports,
                                             ld->localnet_port->logical_port));

    /* Update GARP if it exists. */
    struct garp_data *garp = shash_find_data(&send_garp_data,
                                             binding_rec->logical_port);
    if (garp) {
        garp->ofport = ofport;
        return;
    }

    /* Add GARP for new vif. */
    int i;
    for (i = 0; i < binding_rec->n_mac; i++) {
        struct lport_addresses laddrs;
        if (!extract_lsp_addresses(binding_rec->mac[i], &laddrs)
            || !laddrs.n_ipv4_addrs) {
            continue;
        }

        struct garp_data *garp = xmalloc(sizeof *garp);
        garp->ea = laddrs.ea;
        garp->ipv4 = laddrs.ipv4_addrs[0].addr;
        garp->announce_time = time_msec() + 1000;
        garp->backoff = 1;
        garp->ofport = ofport;
        shash_add(&send_garp_data, binding_rec->logical_port, garp);

        destroy_lport_addresses(&laddrs);
        break;
    }
}

/* Remove a vif from GARP announcements. */
static void
send_garp_delete(const char *lport)
{
    struct garp_data *garp = shash_find_and_delete(&send_garp_data, lport);
    free(garp);
}

static long long int
send_garp(struct garp_data *garp, long long int current_time)
{
    if (current_time < garp->announce_time) {
        return garp->announce_time;
    }

    /* Compose a GARP request packet. */
    uint64_t packet_stub[128 / 8];
    struct dp_packet packet;
    dp_packet_use_stub(&packet, packet_stub, sizeof packet_stub);
    compose_arp(&packet, ARP_OP_REQUEST, garp->ea, eth_addr_zero,
                true, garp->ipv4, garp->ipv4);

    /* Compose actions.  The garp request is output on localnet ofport. */
    uint64_t ofpacts_stub[4096 / 8];
    struct ofpbuf ofpacts = OFPBUF_STUB_INITIALIZER(ofpacts_stub);
    enum ofp_version version = rconn_get_version(swconn);
    ofpact_put_OUTPUT(&ofpacts)->port = garp->ofport;

    struct ofputil_packet_out po = {
        .packet = dp_packet_data(&packet),
        .packet_len = dp_packet_size(&packet),
        .buffer_id = UINT32_MAX,
        .in_port = OFPP_CONTROLLER,
        .ofpacts = ofpacts.data,
        .ofpacts_len = ofpacts.size,
    };
    enum ofputil_protocol proto = ofputil_protocol_from_ofp_version(version);
    queue_msg(ofputil_encode_packet_out(&po, proto));
    dp_packet_uninit(&packet);
    ofpbuf_uninit(&ofpacts);

    /* Set the next announcement.  At most 5 announcements are sent for a
     * vif. */
    if (garp->backoff < 16) {
        garp->backoff *= 2;
        garp->announce_time = current_time + garp->backoff * 1000;
    } else {
        garp->announce_time = LLONG_MAX;
    }
    return garp->announce_time;
}

/* Get localnet vifs, and ofport for localnet patch ports. */
static void
get_localnet_vifs(const struct ovsrec_bridge *br_int,
                  const char *this_chassis_id,
                  const struct lport_index *lports,
                  struct hmap *local_datapaths,
                  struct sset *localnet_vifs,
                  struct simap *localnet_ofports)
{
    for (int i = 0; i < br_int->n_ports; i++) {
        const struct ovsrec_port *port_rec = br_int->ports[i];
        if (!strcmp(port_rec->name, br_int->name)) {
            continue;
        }
        const char *chassis_id = smap_get(&port_rec->external_ids,
                                          "ovn-chassis-id");
        if (chassis_id && !strcmp(chassis_id, this_chassis_id)) {
            continue;
        }
        const char *localnet = smap_get(&port_rec->external_ids,
                                        "ovn-localnet-port");
        for (int j = 0; j < port_rec->n_interfaces; j++) {
            const struct ovsrec_interface *iface_rec = port_rec->interfaces[j];
            if (!iface_rec->n_ofport) {
                continue;
            }
            if (localnet) {
                int64_t ofport = iface_rec->ofport[0];
                if (ofport < 1 || ofport > ofp_to_u16(OFPP_MAX)) {
                    continue;
                }
                simap_put(localnet_ofports, localnet, ofport);
                continue;
            }
            const char *iface_id = smap_get(&iface_rec->external_ids,
                                            "iface-id");
            if (!iface_id) {
                continue;
            }
            const struct sbrec_port_binding *pb
                = lport_lookup_by_name(lports, iface_id);
            if (!pb) {
                continue;
            }
            struct local_datapath *ld
                = get_local_datapath(local_datapaths,
                                     pb->datapath->tunnel_key);
            if (ld && ld->localnet_port) {
                sset_add(localnet_vifs, iface_id);
            }
        }
    }
}

static void
send_garp_wait(void)
{
    poll_timer_wait_until(send_garp_time);
}

static void
send_garp_run(const struct ovsrec_bridge *br_int, const char *chassis_id,
              const struct lport_index *lports,
              struct hmap *local_datapaths)
{
    struct sset localnet_vifs = SSET_INITIALIZER(&localnet_vifs);
    struct simap localnet_ofports = SIMAP_INITIALIZER(&localnet_ofports);

    get_localnet_vifs(br_int, chassis_id, lports, local_datapaths,
                      &localnet_vifs, &localnet_ofports);

    /* For deleted ports, remove from send_garp_data. */
    struct shash_node *iter, *next;
    SHASH_FOR_EACH_SAFE (iter, next, &send_garp_data) {
        if (!sset_contains(&localnet_vifs, iter->name)) {
            send_garp_delete(iter->name);
        }
    }

    /* Update send_garp_data. */
    const char *iface_id;
    SSET_FOR_EACH (iface_id, &localnet_vifs) {
        const struct sbrec_port_binding *pb = lport_lookup_by_name(lports,
                                                                   iface_id);
        if (pb) {
            send_garp_update(pb, &localnet_ofports, local_datapaths);
        }
    }

    /* Send GARPs, and update the next announcement. */
    long long int current_time = time_msec();
    send_garp_time = LLONG_MAX;
    SHASH_FOR_EACH (iter, &send_garp_data) {
        long long int next_announce = send_garp(iter->data, current_time);
        if (send_garp_time > next_announce) {
            send_garp_time = next_announce;
        }
    }
    sset_destroy(&localnet_vifs);
    simap_destroy(&localnet_ofports);
}

static void
reload_metadata(struct ofpbuf *ofpacts, const struct match *md)
{
    enum mf_field_id md_fields[] = {
#if FLOW_N_REGS == 16
        MFF_REG0,
        MFF_REG1,
        MFF_REG2,
        MFF_REG3,
        MFF_REG4,
        MFF_REG5,
        MFF_REG6,
        MFF_REG7,
        MFF_REG8,
        MFF_REG9,
        MFF_REG10,
        MFF_REG11,
        MFF_REG12,
        MFF_REG13,
        MFF_REG14,
        MFF_REG15,
#else
#error
#endif
        MFF_METADATA,
    };
    for (size_t i = 0; i < ARRAY_SIZE(md_fields); i++) {
        const struct mf_field *field = mf_from_id(md_fields[i]);
        if (!mf_is_all_wild(field, &md->wc)) {
            struct ofpact_set_field *sf = ofpact_put_SET_FIELD(ofpacts);
            sf->field = field;
            sf->flow_has_vlan = false;
            mf_get_value(field, &md->flow, &sf->value);
            memset(&sf->mask, 0xff, field->n_bytes);
        }
    }
}

static void
pinctrl_handle_nd_na(const struct flow *ip_flow, const struct match *md,
                     struct ofpbuf *userdata)
{
    /* This action only works for IPv6 ND packets, and the switch should only
     * send us ND packets this way, but check here just to be sure. */
    if (!is_nd(ip_flow, NULL)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "NA action on non-ND packet");
        return;
    }

    enum ofp_version version = rconn_get_version(swconn);
    enum ofputil_protocol proto = ofputil_protocol_from_ofp_version(version);

    uint64_t packet_stub[128 / 8];
    struct dp_packet packet;
    dp_packet_use_stub(&packet, packet_stub, sizeof packet_stub);

    /* xxx These flags are not exactly correct.  Look at section 7.2.4
     * xxx of RFC 4861.  For example, we need to set ND_RSO_ROUTER for
     * xxx router's interfaces and ND_RSO_SOLICITED only if it was
     * xxx requested. */
    compose_nd_na(&packet, ip_flow->dl_dst, ip_flow->dl_src,
                  &ip_flow->nd_target, &ip_flow->ipv6_src,
                  htonl(ND_RSO_SOLICITED | ND_RSO_OVERRIDE));

    /* Reload previous packet metadata. */
    uint64_t ofpacts_stub[4096 / 8];
    struct ofpbuf ofpacts = OFPBUF_STUB_INITIALIZER(ofpacts_stub);
    reload_metadata(&ofpacts, md);

    enum ofperr error = ofpacts_pull_openflow_actions(userdata, userdata->size,
                                                      version, &ofpacts);
    if (error) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "failed to parse actions for 'na' (%s)",
                     ofperr_to_string(error));
        goto exit;
    }

    struct ofputil_packet_out po = {
        .packet = dp_packet_data(&packet),
        .packet_len = dp_packet_size(&packet),
        .buffer_id = UINT32_MAX,
        .in_port = OFPP_CONTROLLER,
        .ofpacts = ofpacts.data,
        .ofpacts_len = ofpacts.size,
    };

    queue_msg(ofputil_encode_packet_out(&po, proto));

exit:
    dp_packet_uninit(&packet);
    ofpbuf_uninit(&ofpacts);
}
