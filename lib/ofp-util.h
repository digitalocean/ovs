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

#ifndef OFP_UTIL_H
#define OFP_UTIL_H 1

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "classifier.h"
#include "flow.h"
#include "openflow/nicira-ext.h"
#include "openvswitch/types.h"

struct cls_rule;
struct ofpbuf;

/* Basic decoding and length validation of OpenFlow messages. */
enum ofputil_msg_code {
    OFPUTIL_INVALID,

    /* OFPT_* messages. */
    OFPUTIL_OFPT_HELLO,
    OFPUTIL_OFPT_ERROR,
    OFPUTIL_OFPT_ECHO_REQUEST,
    OFPUTIL_OFPT_ECHO_REPLY,
    OFPUTIL_OFPT_FEATURES_REQUEST,
    OFPUTIL_OFPT_FEATURES_REPLY,
    OFPUTIL_OFPT_GET_CONFIG_REQUEST,
    OFPUTIL_OFPT_GET_CONFIG_REPLY,
    OFPUTIL_OFPT_SET_CONFIG,
    OFPUTIL_OFPT_PACKET_IN,
    OFPUTIL_OFPT_FLOW_REMOVED,
    OFPUTIL_OFPT_PORT_STATUS,
    OFPUTIL_OFPT_PACKET_OUT,
    OFPUTIL_OFPT_FLOW_MOD,
    OFPUTIL_OFPT_PORT_MOD,
    OFPUTIL_OFPT_BARRIER_REQUEST,
    OFPUTIL_OFPT_BARRIER_REPLY,
    OFPUTIL_OFPT_QUEUE_GET_CONFIG_REQUEST,
    OFPUTIL_OFPT_QUEUE_GET_CONFIG_REPLY,

    /* OFPST_* stat requests. */
    OFPUTIL_OFPST_DESC_REQUEST,
    OFPUTIL_OFPST_FLOW_REQUEST,
    OFPUTIL_OFPST_AGGREGATE_REQUEST,
    OFPUTIL_OFPST_TABLE_REQUEST,
    OFPUTIL_OFPST_PORT_REQUEST,
    OFPUTIL_OFPST_QUEUE_REQUEST,

    /* OFPST_* stat replies. */
    OFPUTIL_OFPST_DESC_REPLY,
    OFPUTIL_OFPST_FLOW_REPLY,
    OFPUTIL_OFPST_QUEUE_REPLY,
    OFPUTIL_OFPST_PORT_REPLY,
    OFPUTIL_OFPST_TABLE_REPLY,
    OFPUTIL_OFPST_AGGREGATE_REPLY,

    /* NXT_* messages. */
    OFPUTIL_NXT_STATUS_REQUEST,
    OFPUTIL_NXT_STATUS_REPLY,
    OFPUTIL_NXT_TUN_ID_FROM_COOKIE,
    OFPUTIL_NXT_ROLE_REQUEST,
    OFPUTIL_NXT_ROLE_REPLY,
    OFPUTIL_NXT_SET_FLOW_FORMAT,
    OFPUTIL_NXT_FLOW_MOD,
    OFPUTIL_NXT_FLOW_REMOVED,

    /* NXST_* stat requests. */
    OFPUTIL_NXST_FLOW_REQUEST,
    OFPUTIL_NXST_AGGREGATE_REQUEST,

    /* NXST_* stat replies. */
    OFPUTIL_NXST_FLOW_REPLY,
    OFPUTIL_NXST_AGGREGATE_REPLY
};

struct ofputil_msg_type;
int ofputil_decode_msg_type(const struct ofp_header *,
                            const struct ofputil_msg_type **);
enum ofputil_msg_code ofputil_msg_type_code(const struct ofputil_msg_type *);
const char *ofputil_msg_type_name(const struct ofputil_msg_type *);

/* Converting OFPFW_NW_SRC_MASK and OFPFW_NW_DST_MASK wildcard bit counts to
 * and from IP bitmasks. */
ovs_be32 ofputil_wcbits_to_netmask(int wcbits);
int ofputil_netmask_to_wcbits(ovs_be32 netmask);

/* Work with OpenFlow 1.0 ofp_match. */
void ofputil_cls_rule_from_match(const struct ofp_match *,
                                 unsigned int priority, enum nx_flow_format,
                                 ovs_be64 cookie, struct cls_rule *);
void ofputil_cls_rule_to_match(const struct cls_rule *, enum nx_flow_format,
                               struct ofp_match *,
                               ovs_be64 cookie_in, ovs_be64 *cookie_out);
void normalize_match(struct ofp_match *);
char *ofp_match_to_literal_string(const struct ofp_match *match);

/* Flow formats. */
bool ofputil_flow_format_is_valid(enum nx_flow_format);
const char *ofputil_flow_format_to_string(enum nx_flow_format);
int ofputil_flow_format_from_string(const char *);
enum nx_flow_format ofputil_min_flow_format(const struct cls_rule *,
                                            bool cookie_support,
                                            ovs_be64 cookie);

struct ofpbuf *ofputil_make_set_flow_format(enum nx_flow_format);

/* Flow format independent flow_mod. */
struct flow_mod {
    struct cls_rule cr;
    ovs_be64 cookie;
    uint16_t command;
    uint16_t idle_timeout;
    uint16_t hard_timeout;
    uint32_t buffer_id;
    uint16_t out_port;
    uint16_t flags;
    union ofp_action *actions;
    size_t n_actions;
};

int ofputil_decode_flow_mod(struct flow_mod *, const struct ofp_header *,
                            enum nx_flow_format);
struct ofpbuf *ofputil_encode_flow_mod(const struct flow_mod *,
                                       enum nx_flow_format);

/* Flow stats or aggregate stats request, independent of flow format. */
struct flow_stats_request {
    bool aggregate;             /* Aggregate results? */
    struct cls_rule match;
    uint16_t out_port;
    uint8_t table_id;
};

int ofputil_decode_flow_stats_request(struct flow_stats_request *,
                                      const struct ofp_header *,
                                      enum nx_flow_format);
struct ofpbuf *ofputil_encode_flow_stats_request(
    const struct flow_stats_request *, enum nx_flow_format);

/* Flow removed message, independent of flow format. */
struct ofputil_flow_removed {
    struct cls_rule rule;
    ovs_be64 cookie;
    uint8_t reason;             /* One of OFPRR_*. */
    uint32_t duration_sec;
    uint32_t duration_nsec;
    uint16_t idle_timeout;
    uint64_t packet_count;
    uint64_t byte_count;
};

int ofputil_decode_flow_removed(struct ofputil_flow_removed *,
                                const struct ofp_header *,
                                enum nx_flow_format);

/* OpenFlow protocol utility functions. */
void *make_openflow(size_t openflow_len, uint8_t type, struct ofpbuf **);
void *make_nxmsg(size_t openflow_len, uint32_t subtype, struct ofpbuf **);

void *make_openflow_xid(size_t openflow_len, uint8_t type,
                        ovs_be32 xid, struct ofpbuf **);
void *make_nxmsg_xid(size_t openflow_len, uint32_t subtype, ovs_be32 xid,
                     struct ofpbuf **);

void *put_openflow(size_t openflow_len, uint8_t type, struct ofpbuf *);
void *put_openflow_xid(size_t openflow_len, uint8_t type, ovs_be32 xid,
                       struct ofpbuf *);

void *put_nxmsg(size_t openflow_len, uint32_t subtype, struct ofpbuf *);
void *put_nxmsg_xid(size_t openflow_len, uint32_t subtype, ovs_be32 xid,
                    struct ofpbuf *);

void update_openflow_length(struct ofpbuf *);

void *ofputil_make_stats_request(size_t body_len, uint16_t type,
                                 struct ofpbuf **);
void *ofputil_make_nxstats_request(size_t openflow_len, uint32_t subtype,
                                   struct ofpbuf **);

const void *ofputil_stats_body(const struct ofp_header *);
size_t ofputil_stats_body_len(const struct ofp_header *);

const void *ofputil_nxstats_body(const struct ofp_header *);
size_t ofputil_nxstats_body_len(const struct ofp_header *);

struct ofpbuf *make_flow_mod(uint16_t command, const struct cls_rule *,
                             size_t actions_len);
struct ofpbuf *make_add_flow(const struct cls_rule *, uint32_t buffer_id,
                             uint16_t max_idle, size_t actions_len);
struct ofpbuf *make_del_flow(const struct cls_rule *);
struct ofpbuf *make_add_simple_flow(const struct cls_rule *,
                                    uint32_t buffer_id, uint16_t out_port,
                                    uint16_t max_idle);
struct ofpbuf *make_packet_in(uint32_t buffer_id, uint16_t in_port,
                              uint8_t reason,
                              const struct ofpbuf *payload, int max_send_len);
struct ofpbuf *make_packet_out(const struct ofpbuf *packet, uint32_t buffer_id,
                               uint16_t in_port,
                               const struct ofp_action_header *,
                               size_t n_actions);
struct ofpbuf *make_buffered_packet_out(uint32_t buffer_id,
                                        uint16_t in_port, uint16_t out_port);
struct ofpbuf *make_unbuffered_packet_out(const struct ofpbuf *packet,
                                          uint16_t in_port, uint16_t out_port);
struct ofpbuf *make_echo_request(void);
struct ofpbuf *make_echo_reply(const struct ofp_header *rq);

struct flow_stats_iterator {
    const uint8_t *pos, *end;
};
const struct ofp_flow_stats *flow_stats_first(struct flow_stats_iterator *,
                                              const struct ofp_stats_reply *);
const struct ofp_flow_stats *flow_stats_next(struct flow_stats_iterator *);

/* Actions. */

#define OFP_ACTION_ALIGN 8      /* Alignment of ofp_actions. */

struct actions_iterator {
    const union ofp_action *pos, *end;
};
const union ofp_action *actions_first(struct actions_iterator *,
                                      const union ofp_action *,
                                      size_t n_actions);
const union ofp_action *actions_next(struct actions_iterator *);

int validate_actions(const union ofp_action *, size_t n_actions,
                     const struct flow *, int max_ports);
bool action_outputs_to_port(const union ofp_action *, uint16_t port);

int ofputil_pull_actions(struct ofpbuf *, unsigned int actions_len,
                         union ofp_action **, size_t *);

/* OpenFlow vendors.
 *
 * These functions map OpenFlow 32-bit vendor IDs (as used in struct
 * ofp_vendor_header) into 4-bit values to embed in an "int".  The 4-bit values
 * are only used internally in Open vSwitch and never appear on the wire, so
 * particular codes used are not important.
 */

/* Vendor error numbers currently used in Open vSwitch. */
#define OFPUTIL_VENDORS                                     \
    /*             vendor name              vendor value */ \
    OFPUTIL_VENDOR(OFPUTIL_VENDOR_OPENFLOW, 0x00000000)     \
    OFPUTIL_VENDOR(OFPUTIL_VENDOR_NICIRA,   NX_VENDOR_ID)

/* OFPUTIL_VENDOR_* definitions. */
enum ofputil_vendor_codes {
#define OFPUTIL_VENDOR(NAME, VENDOR_ID) NAME,
    OFPUTIL_VENDORS
    OFPUTIL_N_VENDORS
#undef OFPUTIL_VENDOR
};

/* Error codes.
 *
 * We embed system errno values and OpenFlow standard and vendor extension
 * error codes into a single 31-bit space using the following encoding.
 * (Bit 31 is unused and assumed 0 to avoid negative "int" values.)
 *
 *   30                                                   0
 *  +------------------------------------------------------+
 *  |                           0                          |  success
 *  +------------------------------------------------------+
 *
 *   30 29                                                0
 *  +--+---------------------------------------------------+
 *  | 0|                    errno value                    |  errno value
 *  +--+---------------------------------------------------+
 *
 *   30 29   26 25            16 15                       0
 *  +--+-------+----------------+--------------------------+
 *  | 1|   0   |      type      |           code           |  standard OpenFlow
 *  +--+-------+----------------+--------------------------+  error
 *
 *   30 29   26 25            16 15                       0
 *  +--+-------+----------------+--------------------------+  Nicira
 *  | 1| vendor|      type      |           code           |  NXET_VENDOR
 *  +--+-------+----------------+--------------------------+  error extension
 *
 * C and POSIX say that errno values are positive.  We assume that they are
 * less than 2**29.  They are actually less than 65536 on at least Linux,
 * FreeBSD, OpenBSD, and Windows.
 *
 * The 'vendor' field holds one of the OFPUTIL_VENDOR_* codes defined above.
 * It must be nonzero.
 *
 * Negative values are not defined.
 */

/* Currently 4 bits are allocated to the "vendor" field.  Make sure that all
 * the vendor codes can fit. */
BUILD_ASSERT_DECL(OFPUTIL_N_VENDORS <= 16);

/* These are macro versions of the functions defined below.  The macro versions
 * are intended for use in contexts where function calls are not allowed,
 * e.g. static initializers and case labels. */
#define OFP_MKERR(TYPE, CODE) ((1 << 30) | ((TYPE) << 16) | (CODE))
#define OFP_MKERR_VENDOR(VENDOR, TYPE, CODE) \
        ((1 << 30) | ((VENDOR) << 26) | ((TYPE) << 16) | (CODE))
#define OFP_MKERR_NICIRA(TYPE, CODE) \
        OFP_MKERR_VENDOR(OFPUTIL_VENDOR_NICIRA, TYPE, CODE)

/* Returns the standard OpenFlow error with the specified 'type' and 'code' as
 * an integer. */
static inline int
ofp_mkerr(uint16_t type, uint16_t code)
{
    return OFP_MKERR(type, code);
}

/* Returns the OpenFlow vendor error with the specified 'vendor', 'type', and
 * 'code' as an integer.  'vendor' must be an OFPUTIL_VENDOR_* constant. */
static inline int
ofp_mkerr_vendor(uint8_t vendor, uint16_t type, uint16_t code)
{
    assert(vendor < OFPUTIL_N_VENDORS);
    return OFP_MKERR_VENDOR(vendor, type, code);
}

/* Returns the OpenFlow vendor error with Nicira as vendor, with the specific
 * 'type' and 'code', as an integer. */
static inline int
ofp_mkerr_nicira(uint16_t type, uint16_t code)
{
    return OFP_MKERR_NICIRA(type, code);
}

/* Returns true if 'error' encodes an OpenFlow standard or vendor extension
 * error codes as documented above. */
static inline bool
is_ofp_error(int error)
{
    return (error & (1 << 30)) != 0;
}

/* Returns true if 'error' appears to be a system errno value. */
static inline bool
is_errno(int error)
{
    return !is_ofp_error(error);
}

/* Returns the "vendor" part of the OpenFlow error code 'error' (which must be
 * in the format explained above).  This is normally one of the
 * OFPUTIL_VENDOR_* constants.  Returns OFPUTIL_VENDOR_OPENFLOW (0) for a
 * standard OpenFlow error. */
static inline uint8_t
get_ofp_err_vendor(int error)
{
    return (error >> 26) & 0xf;
}

/* Returns the "type" part of the OpenFlow error code 'error' (which must be in
 * the format explained above). */
static inline uint16_t
get_ofp_err_type(int error)
{
    return (error >> 16) & 0x3ff;
}

/* Returns the "code" part of the OpenFlow error code 'error' (which must be in
 * the format explained above). */
static inline uint16_t
get_ofp_err_code(int error)
{
    return error & 0xffff;
}

struct ofpbuf *make_ofp_error_msg(int error, const struct ofp_header *);

#endif /* ofp-util.h */
