/* Generated automatically -- do not modify!    -*- buffer-read-only: t -*- */

#ifndef OVSREC_IDL_HEADER
#define OVSREC_IDL_HEADER 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ovsdb-data.h"
#include "ovsdb-idl-provider.h"
#include "uuid.h"

/* Bridge table. */
struct ovsrec_bridge {
	struct ovsdb_idl_row header_;

	/* controller column. */
	struct ovsrec_controller **controller;
	size_t n_controller;

	/* datapath_id column. */
	char *datapath_id;

	/* datapath_type column. */
	char *datapath_type;	/* Always nonnull. */

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* fail_mode column. */
	char *fail_mode;

	/* flood_vlans column. */
	int64_t *flood_vlans;
	size_t n_flood_vlans;

	/* mirrors column. */
	struct ovsrec_mirror **mirrors;
	size_t n_mirrors;

	/* name column. */
	char *name;	/* Always nonnull. */

	/* netflow column. */
	struct ovsrec_netflow *netflow;

	/* other_config column. */
	char **key_other_config;
	char **value_other_config;
	size_t n_other_config;

	/* ports column. */
	struct ovsrec_port **ports;
	size_t n_ports;

	/* sflow column. */
	struct ovsrec_sflow *sflow;
};

enum {
    OVSREC_BRIDGE_COL_CONTROLLER,
    OVSREC_BRIDGE_COL_DATAPATH_ID,
    OVSREC_BRIDGE_COL_DATAPATH_TYPE,
    OVSREC_BRIDGE_COL_EXTERNAL_IDS,
    OVSREC_BRIDGE_COL_FAIL_MODE,
    OVSREC_BRIDGE_COL_FLOOD_VLANS,
    OVSREC_BRIDGE_COL_MIRRORS,
    OVSREC_BRIDGE_COL_NAME,
    OVSREC_BRIDGE_COL_NETFLOW,
    OVSREC_BRIDGE_COL_OTHER_CONFIG,
    OVSREC_BRIDGE_COL_PORTS,
    OVSREC_BRIDGE_COL_SFLOW,
    OVSREC_BRIDGE_N_COLUMNS
};

#define ovsrec_bridge_col_fail_mode (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_FAIL_MODE])
#define ovsrec_bridge_col_datapath_id (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_DATAPATH_ID])
#define ovsrec_bridge_col_datapath_type (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_DATAPATH_TYPE])
#define ovsrec_bridge_col_sflow (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_SFLOW])
#define ovsrec_bridge_col_mirrors (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_MIRRORS])
#define ovsrec_bridge_col_other_config (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_OTHER_CONFIG])
#define ovsrec_bridge_col_flood_vlans (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_FLOOD_VLANS])
#define ovsrec_bridge_col_controller (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_CONTROLLER])
#define ovsrec_bridge_col_netflow (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_NETFLOW])
#define ovsrec_bridge_col_external_ids (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_EXTERNAL_IDS])
#define ovsrec_bridge_col_ports (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_PORTS])
#define ovsrec_bridge_col_name (ovsrec_bridge_columns[OVSREC_BRIDGE_COL_NAME])

extern struct ovsdb_idl_column ovsrec_bridge_columns[OVSREC_BRIDGE_N_COLUMNS];

const struct ovsrec_bridge *ovsrec_bridge_first(const struct ovsdb_idl *);
const struct ovsrec_bridge *ovsrec_bridge_next(const struct ovsrec_bridge *);
#define OVSREC_BRIDGE_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_bridge_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_bridge_next(ROW))
#define OVSREC_BRIDGE_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_bridge_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_bridge_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_bridge_delete(const struct ovsrec_bridge *);
struct ovsrec_bridge *ovsrec_bridge_insert(struct ovsdb_idl_txn *);

void ovsrec_bridge_verify_controller(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_datapath_id(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_datapath_type(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_external_ids(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_fail_mode(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_flood_vlans(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_mirrors(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_name(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_netflow(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_other_config(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_ports(const struct ovsrec_bridge *);
void ovsrec_bridge_verify_sflow(const struct ovsrec_bridge *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_bridge directly.) */
const struct ovsdb_datum *ovsrec_bridge_get_controller(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_datapath_id(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_datapath_type(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_external_ids(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_bridge_get_fail_mode(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_flood_vlans(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_mirrors(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_name(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_netflow(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_other_config(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_bridge_get_ports(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_bridge_get_sflow(const struct ovsrec_bridge *, enum ovsdb_atomic_type key_type);

void ovsrec_bridge_set_controller(const struct ovsrec_bridge *, struct ovsrec_controller **controller, size_t n_controller);
void ovsrec_bridge_set_datapath_id(const struct ovsrec_bridge *, const char *datapath_id);
void ovsrec_bridge_set_datapath_type(const struct ovsrec_bridge *, const char *datapath_type);
void ovsrec_bridge_set_external_ids(const struct ovsrec_bridge *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_bridge_set_fail_mode(const struct ovsrec_bridge *, const char *fail_mode);
void ovsrec_bridge_set_flood_vlans(const struct ovsrec_bridge *, const int64_t *flood_vlans, size_t n_flood_vlans);
void ovsrec_bridge_set_mirrors(const struct ovsrec_bridge *, struct ovsrec_mirror **mirrors, size_t n_mirrors);
void ovsrec_bridge_set_name(const struct ovsrec_bridge *, const char *name);
void ovsrec_bridge_set_netflow(const struct ovsrec_bridge *, const struct ovsrec_netflow *netflow);
void ovsrec_bridge_set_other_config(const struct ovsrec_bridge *, char **key_other_config, char **value_other_config, size_t n_other_config);
void ovsrec_bridge_set_ports(const struct ovsrec_bridge *, struct ovsrec_port **ports, size_t n_ports);
void ovsrec_bridge_set_sflow(const struct ovsrec_bridge *, const struct ovsrec_sflow *sflow);

/* Capability table. */
struct ovsrec_capability {
	struct ovsdb_idl_row header_;

	/* details column. */
	char **key_details;
	char **value_details;
	size_t n_details;
};

enum {
    OVSREC_CAPABILITY_COL_DETAILS,
    OVSREC_CAPABILITY_N_COLUMNS
};

#define ovsrec_capability_col_details (ovsrec_capability_columns[OVSREC_CAPABILITY_COL_DETAILS])

extern struct ovsdb_idl_column ovsrec_capability_columns[OVSREC_CAPABILITY_N_COLUMNS];

const struct ovsrec_capability *ovsrec_capability_first(const struct ovsdb_idl *);
const struct ovsrec_capability *ovsrec_capability_next(const struct ovsrec_capability *);
#define OVSREC_CAPABILITY_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_capability_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_capability_next(ROW))
#define OVSREC_CAPABILITY_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_capability_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_capability_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_capability_delete(const struct ovsrec_capability *);
struct ovsrec_capability *ovsrec_capability_insert(struct ovsdb_idl_txn *);

void ovsrec_capability_verify_details(const struct ovsrec_capability *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_capability directly.) */
const struct ovsdb_datum *ovsrec_capability_get_details(const struct ovsrec_capability *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);

void ovsrec_capability_set_details(const struct ovsrec_capability *, char **key_details, char **value_details, size_t n_details);

/* Controller table. */
struct ovsrec_controller {
	struct ovsdb_idl_row header_;

	/* connection_mode column. */
	char *connection_mode;

	/* controller_burst_limit column. */
	int64_t *controller_burst_limit;
	size_t n_controller_burst_limit;

	/* controller_rate_limit column. */
	int64_t *controller_rate_limit;
	size_t n_controller_rate_limit;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* inactivity_probe column. */
	int64_t *inactivity_probe;
	size_t n_inactivity_probe;

	/* is_connected column. */
	bool is_connected;

	/* local_gateway column. */
	char *local_gateway;

	/* local_ip column. */
	char *local_ip;

	/* local_netmask column. */
	char *local_netmask;

	/* max_backoff column. */
	int64_t *max_backoff;
	size_t n_max_backoff;

	/* role column. */
	char *role;

	/* status column. */
	char **key_status;
	char **value_status;
	size_t n_status;

	/* target column. */
	char *target;	/* Always nonnull. */
};

enum {
    OVSREC_CONTROLLER_COL_CONNECTION_MODE,
    OVSREC_CONTROLLER_COL_CONTROLLER_BURST_LIMIT,
    OVSREC_CONTROLLER_COL_CONTROLLER_RATE_LIMIT,
    OVSREC_CONTROLLER_COL_EXTERNAL_IDS,
    OVSREC_CONTROLLER_COL_INACTIVITY_PROBE,
    OVSREC_CONTROLLER_COL_IS_CONNECTED,
    OVSREC_CONTROLLER_COL_LOCAL_GATEWAY,
    OVSREC_CONTROLLER_COL_LOCAL_IP,
    OVSREC_CONTROLLER_COL_LOCAL_NETMASK,
    OVSREC_CONTROLLER_COL_MAX_BACKOFF,
    OVSREC_CONTROLLER_COL_ROLE,
    OVSREC_CONTROLLER_COL_STATUS,
    OVSREC_CONTROLLER_COL_TARGET,
    OVSREC_CONTROLLER_N_COLUMNS
};

#define ovsrec_controller_col_max_backoff (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_MAX_BACKOFF])
#define ovsrec_controller_col_status (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_STATUS])
#define ovsrec_controller_col_target (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_TARGET])
#define ovsrec_controller_col_local_ip (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_LOCAL_IP])
#define ovsrec_controller_col_connection_mode (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_CONNECTION_MODE])
#define ovsrec_controller_col_controller_rate_limit (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_CONTROLLER_RATE_LIMIT])
#define ovsrec_controller_col_inactivity_probe (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_INACTIVITY_PROBE])
#define ovsrec_controller_col_local_netmask (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_LOCAL_NETMASK])
#define ovsrec_controller_col_role (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_ROLE])
#define ovsrec_controller_col_controller_burst_limit (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_CONTROLLER_BURST_LIMIT])
#define ovsrec_controller_col_external_ids (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_EXTERNAL_IDS])
#define ovsrec_controller_col_local_gateway (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_LOCAL_GATEWAY])
#define ovsrec_controller_col_is_connected (ovsrec_controller_columns[OVSREC_CONTROLLER_COL_IS_CONNECTED])

extern struct ovsdb_idl_column ovsrec_controller_columns[OVSREC_CONTROLLER_N_COLUMNS];

const struct ovsrec_controller *ovsrec_controller_first(const struct ovsdb_idl *);
const struct ovsrec_controller *ovsrec_controller_next(const struct ovsrec_controller *);
#define OVSREC_CONTROLLER_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_controller_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_controller_next(ROW))
#define OVSREC_CONTROLLER_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_controller_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_controller_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_controller_delete(const struct ovsrec_controller *);
struct ovsrec_controller *ovsrec_controller_insert(struct ovsdb_idl_txn *);

void ovsrec_controller_verify_connection_mode(const struct ovsrec_controller *);
void ovsrec_controller_verify_controller_burst_limit(const struct ovsrec_controller *);
void ovsrec_controller_verify_controller_rate_limit(const struct ovsrec_controller *);
void ovsrec_controller_verify_external_ids(const struct ovsrec_controller *);
void ovsrec_controller_verify_inactivity_probe(const struct ovsrec_controller *);
void ovsrec_controller_verify_is_connected(const struct ovsrec_controller *);
void ovsrec_controller_verify_local_gateway(const struct ovsrec_controller *);
void ovsrec_controller_verify_local_ip(const struct ovsrec_controller *);
void ovsrec_controller_verify_local_netmask(const struct ovsrec_controller *);
void ovsrec_controller_verify_max_backoff(const struct ovsrec_controller *);
void ovsrec_controller_verify_role(const struct ovsrec_controller *);
void ovsrec_controller_verify_status(const struct ovsrec_controller *);
void ovsrec_controller_verify_target(const struct ovsrec_controller *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_controller directly.) */
const struct ovsdb_datum *ovsrec_controller_get_connection_mode(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_controller_burst_limit(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_controller_rate_limit(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_external_ids(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_controller_get_inactivity_probe(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_is_connected(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_local_gateway(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_local_ip(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_local_netmask(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_max_backoff(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_role(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_controller_get_status(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_controller_get_target(const struct ovsrec_controller *, enum ovsdb_atomic_type key_type);

void ovsrec_controller_set_connection_mode(const struct ovsrec_controller *, const char *connection_mode);
void ovsrec_controller_set_controller_burst_limit(const struct ovsrec_controller *, const int64_t *controller_burst_limit, size_t n_controller_burst_limit);
void ovsrec_controller_set_controller_rate_limit(const struct ovsrec_controller *, const int64_t *controller_rate_limit, size_t n_controller_rate_limit);
void ovsrec_controller_set_external_ids(const struct ovsrec_controller *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_controller_set_inactivity_probe(const struct ovsrec_controller *, const int64_t *inactivity_probe, size_t n_inactivity_probe);
void ovsrec_controller_set_is_connected(const struct ovsrec_controller *, bool is_connected);
void ovsrec_controller_set_local_gateway(const struct ovsrec_controller *, const char *local_gateway);
void ovsrec_controller_set_local_ip(const struct ovsrec_controller *, const char *local_ip);
void ovsrec_controller_set_local_netmask(const struct ovsrec_controller *, const char *local_netmask);
void ovsrec_controller_set_max_backoff(const struct ovsrec_controller *, const int64_t *max_backoff, size_t n_max_backoff);
void ovsrec_controller_set_role(const struct ovsrec_controller *, const char *role);
void ovsrec_controller_set_status(const struct ovsrec_controller *, char **key_status, char **value_status, size_t n_status);
void ovsrec_controller_set_target(const struct ovsrec_controller *, const char *target);

/* Interface table. */
struct ovsrec_interface {
	struct ovsdb_idl_row header_;

	/* admin_state column. */
	char *admin_state;

	/* duplex column. */
	char *duplex;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* ingress_policing_burst column. */
	int64_t ingress_policing_burst;

	/* ingress_policing_rate column. */
	int64_t ingress_policing_rate;

	/* link_speed column. */
	int64_t *link_speed;
	size_t n_link_speed;

	/* link_state column. */
	char *link_state;

	/* mac column. */
	char *mac;

	/* monitor column. */
	struct ovsrec_monitor *monitor;

	/* mtu column. */
	int64_t *mtu;
	size_t n_mtu;

	/* name column. */
	char *name;	/* Always nonnull. */

	/* ofport column. */
	int64_t *ofport;
	size_t n_ofport;

	/* options column. */
	char **key_options;
	char **value_options;
	size_t n_options;

	/* other_config column. */
	char **key_other_config;
	char **value_other_config;
	size_t n_other_config;

	/* statistics column. */
	char **key_statistics;
	int64_t *value_statistics;
	size_t n_statistics;

	/* status column. */
	char **key_status;
	char **value_status;
	size_t n_status;

	/* type column. */
	char *type;	/* Always nonnull. */
};

enum {
    OVSREC_INTERFACE_COL_ADMIN_STATE,
    OVSREC_INTERFACE_COL_DUPLEX,
    OVSREC_INTERFACE_COL_EXTERNAL_IDS,
    OVSREC_INTERFACE_COL_INGRESS_POLICING_BURST,
    OVSREC_INTERFACE_COL_INGRESS_POLICING_RATE,
    OVSREC_INTERFACE_COL_LINK_SPEED,
    OVSREC_INTERFACE_COL_LINK_STATE,
    OVSREC_INTERFACE_COL_MAC,
    OVSREC_INTERFACE_COL_MONITOR,
    OVSREC_INTERFACE_COL_MTU,
    OVSREC_INTERFACE_COL_NAME,
    OVSREC_INTERFACE_COL_OFPORT,
    OVSREC_INTERFACE_COL_OPTIONS,
    OVSREC_INTERFACE_COL_OTHER_CONFIG,
    OVSREC_INTERFACE_COL_STATISTICS,
    OVSREC_INTERFACE_COL_STATUS,
    OVSREC_INTERFACE_COL_TYPE,
    OVSREC_INTERFACE_N_COLUMNS
};

#define ovsrec_interface_col_ofport (ovsrec_interface_columns[OVSREC_INTERFACE_COL_OFPORT])
#define ovsrec_interface_col_status (ovsrec_interface_columns[OVSREC_INTERFACE_COL_STATUS])
#define ovsrec_interface_col_statistics (ovsrec_interface_columns[OVSREC_INTERFACE_COL_STATISTICS])
#define ovsrec_interface_col_monitor (ovsrec_interface_columns[OVSREC_INTERFACE_COL_MONITOR])
#define ovsrec_interface_col_ingress_policing_burst (ovsrec_interface_columns[OVSREC_INTERFACE_COL_INGRESS_POLICING_BURST])
#define ovsrec_interface_col_duplex (ovsrec_interface_columns[OVSREC_INTERFACE_COL_DUPLEX])
#define ovsrec_interface_col_other_config (ovsrec_interface_columns[OVSREC_INTERFACE_COL_OTHER_CONFIG])
#define ovsrec_interface_col_link_speed (ovsrec_interface_columns[OVSREC_INTERFACE_COL_LINK_SPEED])
#define ovsrec_interface_col_mtu (ovsrec_interface_columns[OVSREC_INTERFACE_COL_MTU])
#define ovsrec_interface_col_link_state (ovsrec_interface_columns[OVSREC_INTERFACE_COL_LINK_STATE])
#define ovsrec_interface_col_mac (ovsrec_interface_columns[OVSREC_INTERFACE_COL_MAC])
#define ovsrec_interface_col_admin_state (ovsrec_interface_columns[OVSREC_INTERFACE_COL_ADMIN_STATE])
#define ovsrec_interface_col_external_ids (ovsrec_interface_columns[OVSREC_INTERFACE_COL_EXTERNAL_IDS])
#define ovsrec_interface_col_ingress_policing_rate (ovsrec_interface_columns[OVSREC_INTERFACE_COL_INGRESS_POLICING_RATE])
#define ovsrec_interface_col_type (ovsrec_interface_columns[OVSREC_INTERFACE_COL_TYPE])
#define ovsrec_interface_col_options (ovsrec_interface_columns[OVSREC_INTERFACE_COL_OPTIONS])
#define ovsrec_interface_col_name (ovsrec_interface_columns[OVSREC_INTERFACE_COL_NAME])

extern struct ovsdb_idl_column ovsrec_interface_columns[OVSREC_INTERFACE_N_COLUMNS];

const struct ovsrec_interface *ovsrec_interface_first(const struct ovsdb_idl *);
const struct ovsrec_interface *ovsrec_interface_next(const struct ovsrec_interface *);
#define OVSREC_INTERFACE_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_interface_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_interface_next(ROW))
#define OVSREC_INTERFACE_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_interface_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_interface_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_interface_delete(const struct ovsrec_interface *);
struct ovsrec_interface *ovsrec_interface_insert(struct ovsdb_idl_txn *);

void ovsrec_interface_verify_admin_state(const struct ovsrec_interface *);
void ovsrec_interface_verify_duplex(const struct ovsrec_interface *);
void ovsrec_interface_verify_external_ids(const struct ovsrec_interface *);
void ovsrec_interface_verify_ingress_policing_burst(const struct ovsrec_interface *);
void ovsrec_interface_verify_ingress_policing_rate(const struct ovsrec_interface *);
void ovsrec_interface_verify_link_speed(const struct ovsrec_interface *);
void ovsrec_interface_verify_link_state(const struct ovsrec_interface *);
void ovsrec_interface_verify_mac(const struct ovsrec_interface *);
void ovsrec_interface_verify_monitor(const struct ovsrec_interface *);
void ovsrec_interface_verify_mtu(const struct ovsrec_interface *);
void ovsrec_interface_verify_name(const struct ovsrec_interface *);
void ovsrec_interface_verify_ofport(const struct ovsrec_interface *);
void ovsrec_interface_verify_options(const struct ovsrec_interface *);
void ovsrec_interface_verify_other_config(const struct ovsrec_interface *);
void ovsrec_interface_verify_statistics(const struct ovsrec_interface *);
void ovsrec_interface_verify_status(const struct ovsrec_interface *);
void ovsrec_interface_verify_type(const struct ovsrec_interface *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_interface directly.) */
const struct ovsdb_datum *ovsrec_interface_get_admin_state(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_duplex(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_external_ids(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_interface_get_ingress_policing_burst(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_ingress_policing_rate(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_link_speed(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_link_state(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_mac(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_monitor(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_mtu(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_name(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_ofport(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_interface_get_options(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_interface_get_other_config(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_interface_get_statistics(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_interface_get_status(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_interface_get_type(const struct ovsrec_interface *, enum ovsdb_atomic_type key_type);

void ovsrec_interface_set_admin_state(const struct ovsrec_interface *, const char *admin_state);
void ovsrec_interface_set_duplex(const struct ovsrec_interface *, const char *duplex);
void ovsrec_interface_set_external_ids(const struct ovsrec_interface *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_interface_set_ingress_policing_burst(const struct ovsrec_interface *, int64_t ingress_policing_burst);
void ovsrec_interface_set_ingress_policing_rate(const struct ovsrec_interface *, int64_t ingress_policing_rate);
void ovsrec_interface_set_link_speed(const struct ovsrec_interface *, const int64_t *link_speed, size_t n_link_speed);
void ovsrec_interface_set_link_state(const struct ovsrec_interface *, const char *link_state);
void ovsrec_interface_set_mac(const struct ovsrec_interface *, const char *mac);
void ovsrec_interface_set_monitor(const struct ovsrec_interface *, const struct ovsrec_monitor *monitor);
void ovsrec_interface_set_mtu(const struct ovsrec_interface *, const int64_t *mtu, size_t n_mtu);
void ovsrec_interface_set_name(const struct ovsrec_interface *, const char *name);
void ovsrec_interface_set_ofport(const struct ovsrec_interface *, const int64_t *ofport, size_t n_ofport);
void ovsrec_interface_set_options(const struct ovsrec_interface *, char **key_options, char **value_options, size_t n_options);
void ovsrec_interface_set_other_config(const struct ovsrec_interface *, char **key_other_config, char **value_other_config, size_t n_other_config);
void ovsrec_interface_set_statistics(const struct ovsrec_interface *, char **key_statistics, const int64_t *value_statistics, size_t n_statistics);
void ovsrec_interface_set_status(const struct ovsrec_interface *, char **key_status, char **value_status, size_t n_status);
void ovsrec_interface_set_type(const struct ovsrec_interface *, const char *type);

/* Maintenance_Point table. */
struct ovsrec_maintenance_point {
	struct ovsdb_idl_row header_;

	/* fault column. */
	bool *fault;
	size_t n_fault;

	/* mpid column. */
	int64_t mpid;
};

enum {
    OVSREC_MAINTENANCE_POINT_COL_FAULT,
    OVSREC_MAINTENANCE_POINT_COL_MPID,
    OVSREC_MAINTENANCE_POINT_N_COLUMNS
};

#define ovsrec_maintenance_point_col_fault (ovsrec_maintenance_point_columns[OVSREC_MAINTENANCE_POINT_COL_FAULT])
#define ovsrec_maintenance_point_col_mpid (ovsrec_maintenance_point_columns[OVSREC_MAINTENANCE_POINT_COL_MPID])

extern struct ovsdb_idl_column ovsrec_maintenance_point_columns[OVSREC_MAINTENANCE_POINT_N_COLUMNS];

const struct ovsrec_maintenance_point *ovsrec_maintenance_point_first(const struct ovsdb_idl *);
const struct ovsrec_maintenance_point *ovsrec_maintenance_point_next(const struct ovsrec_maintenance_point *);
#define OVSREC_MAINTENANCE_POINT_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_maintenance_point_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_maintenance_point_next(ROW))
#define OVSREC_MAINTENANCE_POINT_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_maintenance_point_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_maintenance_point_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_maintenance_point_delete(const struct ovsrec_maintenance_point *);
struct ovsrec_maintenance_point *ovsrec_maintenance_point_insert(struct ovsdb_idl_txn *);

void ovsrec_maintenance_point_verify_fault(const struct ovsrec_maintenance_point *);
void ovsrec_maintenance_point_verify_mpid(const struct ovsrec_maintenance_point *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_maintenance_point directly.) */
const struct ovsdb_datum *ovsrec_maintenance_point_get_fault(const struct ovsrec_maintenance_point *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_maintenance_point_get_mpid(const struct ovsrec_maintenance_point *, enum ovsdb_atomic_type key_type);

void ovsrec_maintenance_point_set_fault(const struct ovsrec_maintenance_point *, const bool *fault, size_t n_fault);
void ovsrec_maintenance_point_set_mpid(const struct ovsrec_maintenance_point *, int64_t mpid);

/* Manager table. */
struct ovsrec_manager {
	struct ovsdb_idl_row header_;

	/* connection_mode column. */
	char *connection_mode;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* inactivity_probe column. */
	int64_t *inactivity_probe;
	size_t n_inactivity_probe;

	/* is_connected column. */
	bool is_connected;

	/* max_backoff column. */
	int64_t *max_backoff;
	size_t n_max_backoff;

	/* status column. */
	char **key_status;
	char **value_status;
	size_t n_status;

	/* target column. */
	char *target;	/* Always nonnull. */
};

enum {
    OVSREC_MANAGER_COL_CONNECTION_MODE,
    OVSREC_MANAGER_COL_EXTERNAL_IDS,
    OVSREC_MANAGER_COL_INACTIVITY_PROBE,
    OVSREC_MANAGER_COL_IS_CONNECTED,
    OVSREC_MANAGER_COL_MAX_BACKOFF,
    OVSREC_MANAGER_COL_STATUS,
    OVSREC_MANAGER_COL_TARGET,
    OVSREC_MANAGER_N_COLUMNS
};

#define ovsrec_manager_col_max_backoff (ovsrec_manager_columns[OVSREC_MANAGER_COL_MAX_BACKOFF])
#define ovsrec_manager_col_status (ovsrec_manager_columns[OVSREC_MANAGER_COL_STATUS])
#define ovsrec_manager_col_target (ovsrec_manager_columns[OVSREC_MANAGER_COL_TARGET])
#define ovsrec_manager_col_connection_mode (ovsrec_manager_columns[OVSREC_MANAGER_COL_CONNECTION_MODE])
#define ovsrec_manager_col_inactivity_probe (ovsrec_manager_columns[OVSREC_MANAGER_COL_INACTIVITY_PROBE])
#define ovsrec_manager_col_external_ids (ovsrec_manager_columns[OVSREC_MANAGER_COL_EXTERNAL_IDS])
#define ovsrec_manager_col_is_connected (ovsrec_manager_columns[OVSREC_MANAGER_COL_IS_CONNECTED])

extern struct ovsdb_idl_column ovsrec_manager_columns[OVSREC_MANAGER_N_COLUMNS];

const struct ovsrec_manager *ovsrec_manager_first(const struct ovsdb_idl *);
const struct ovsrec_manager *ovsrec_manager_next(const struct ovsrec_manager *);
#define OVSREC_MANAGER_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_manager_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_manager_next(ROW))
#define OVSREC_MANAGER_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_manager_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_manager_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_manager_delete(const struct ovsrec_manager *);
struct ovsrec_manager *ovsrec_manager_insert(struct ovsdb_idl_txn *);

void ovsrec_manager_verify_connection_mode(const struct ovsrec_manager *);
void ovsrec_manager_verify_external_ids(const struct ovsrec_manager *);
void ovsrec_manager_verify_inactivity_probe(const struct ovsrec_manager *);
void ovsrec_manager_verify_is_connected(const struct ovsrec_manager *);
void ovsrec_manager_verify_max_backoff(const struct ovsrec_manager *);
void ovsrec_manager_verify_status(const struct ovsrec_manager *);
void ovsrec_manager_verify_target(const struct ovsrec_manager *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_manager directly.) */
const struct ovsdb_datum *ovsrec_manager_get_connection_mode(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_manager_get_external_ids(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_manager_get_inactivity_probe(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_manager_get_is_connected(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_manager_get_max_backoff(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_manager_get_status(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_manager_get_target(const struct ovsrec_manager *, enum ovsdb_atomic_type key_type);

void ovsrec_manager_set_connection_mode(const struct ovsrec_manager *, const char *connection_mode);
void ovsrec_manager_set_external_ids(const struct ovsrec_manager *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_manager_set_inactivity_probe(const struct ovsrec_manager *, const int64_t *inactivity_probe, size_t n_inactivity_probe);
void ovsrec_manager_set_is_connected(const struct ovsrec_manager *, bool is_connected);
void ovsrec_manager_set_max_backoff(const struct ovsrec_manager *, const int64_t *max_backoff, size_t n_max_backoff);
void ovsrec_manager_set_status(const struct ovsrec_manager *, char **key_status, char **value_status, size_t n_status);
void ovsrec_manager_set_target(const struct ovsrec_manager *, const char *target);

/* Mirror table. */
struct ovsrec_mirror {
	struct ovsdb_idl_row header_;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* name column. */
	char *name;	/* Always nonnull. */

	/* output_port column. */
	struct ovsrec_port *output_port;

	/* output_vlan column. */
	int64_t *output_vlan;
	size_t n_output_vlan;

	/* select_all column. */
	bool select_all;

	/* select_dst_port column. */
	struct ovsrec_port **select_dst_port;
	size_t n_select_dst_port;

	/* select_src_port column. */
	struct ovsrec_port **select_src_port;
	size_t n_select_src_port;

	/* select_vlan column. */
	int64_t *select_vlan;
	size_t n_select_vlan;
};

enum {
    OVSREC_MIRROR_COL_EXTERNAL_IDS,
    OVSREC_MIRROR_COL_NAME,
    OVSREC_MIRROR_COL_OUTPUT_PORT,
    OVSREC_MIRROR_COL_OUTPUT_VLAN,
    OVSREC_MIRROR_COL_SELECT_ALL,
    OVSREC_MIRROR_COL_SELECT_DST_PORT,
    OVSREC_MIRROR_COL_SELECT_SRC_PORT,
    OVSREC_MIRROR_COL_SELECT_VLAN,
    OVSREC_MIRROR_N_COLUMNS
};

#define ovsrec_mirror_col_output_port (ovsrec_mirror_columns[OVSREC_MIRROR_COL_OUTPUT_PORT])
#define ovsrec_mirror_col_select_src_port (ovsrec_mirror_columns[OVSREC_MIRROR_COL_SELECT_SRC_PORT])
#define ovsrec_mirror_col_name (ovsrec_mirror_columns[OVSREC_MIRROR_COL_NAME])
#define ovsrec_mirror_col_select_all (ovsrec_mirror_columns[OVSREC_MIRROR_COL_SELECT_ALL])
#define ovsrec_mirror_col_select_dst_port (ovsrec_mirror_columns[OVSREC_MIRROR_COL_SELECT_DST_PORT])
#define ovsrec_mirror_col_external_ids (ovsrec_mirror_columns[OVSREC_MIRROR_COL_EXTERNAL_IDS])
#define ovsrec_mirror_col_output_vlan (ovsrec_mirror_columns[OVSREC_MIRROR_COL_OUTPUT_VLAN])
#define ovsrec_mirror_col_select_vlan (ovsrec_mirror_columns[OVSREC_MIRROR_COL_SELECT_VLAN])

extern struct ovsdb_idl_column ovsrec_mirror_columns[OVSREC_MIRROR_N_COLUMNS];

const struct ovsrec_mirror *ovsrec_mirror_first(const struct ovsdb_idl *);
const struct ovsrec_mirror *ovsrec_mirror_next(const struct ovsrec_mirror *);
#define OVSREC_MIRROR_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_mirror_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_mirror_next(ROW))
#define OVSREC_MIRROR_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_mirror_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_mirror_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_mirror_delete(const struct ovsrec_mirror *);
struct ovsrec_mirror *ovsrec_mirror_insert(struct ovsdb_idl_txn *);

void ovsrec_mirror_verify_external_ids(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_name(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_output_port(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_output_vlan(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_select_all(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_select_dst_port(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_select_src_port(const struct ovsrec_mirror *);
void ovsrec_mirror_verify_select_vlan(const struct ovsrec_mirror *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_mirror directly.) */
const struct ovsdb_datum *ovsrec_mirror_get_external_ids(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_mirror_get_name(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_mirror_get_output_port(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_mirror_get_output_vlan(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_mirror_get_select_all(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_mirror_get_select_dst_port(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_mirror_get_select_src_port(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_mirror_get_select_vlan(const struct ovsrec_mirror *, enum ovsdb_atomic_type key_type);

void ovsrec_mirror_set_external_ids(const struct ovsrec_mirror *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_mirror_set_name(const struct ovsrec_mirror *, const char *name);
void ovsrec_mirror_set_output_port(const struct ovsrec_mirror *, const struct ovsrec_port *output_port);
void ovsrec_mirror_set_output_vlan(const struct ovsrec_mirror *, const int64_t *output_vlan, size_t n_output_vlan);
void ovsrec_mirror_set_select_all(const struct ovsrec_mirror *, bool select_all);
void ovsrec_mirror_set_select_dst_port(const struct ovsrec_mirror *, struct ovsrec_port **select_dst_port, size_t n_select_dst_port);
void ovsrec_mirror_set_select_src_port(const struct ovsrec_mirror *, struct ovsrec_port **select_src_port, size_t n_select_src_port);
void ovsrec_mirror_set_select_vlan(const struct ovsrec_mirror *, const int64_t *select_vlan, size_t n_select_vlan);

/* Monitor table. */
struct ovsrec_monitor {
	struct ovsdb_idl_row header_;

	/* fault column. */
	bool *fault;
	size_t n_fault;

	/* interval column. */
	int64_t *interval;
	size_t n_interval;

	/* ma_name column. */
	char *ma_name;

	/* md_name column. */
	char *md_name;

	/* mpid column. */
	int64_t mpid;

	/* remote_mps column. */
	struct ovsrec_maintenance_point **remote_mps;
	size_t n_remote_mps;
};

enum {
    OVSREC_MONITOR_COL_FAULT,
    OVSREC_MONITOR_COL_INTERVAL,
    OVSREC_MONITOR_COL_MA_NAME,
    OVSREC_MONITOR_COL_MD_NAME,
    OVSREC_MONITOR_COL_MPID,
    OVSREC_MONITOR_COL_REMOTE_MPS,
    OVSREC_MONITOR_N_COLUMNS
};

#define ovsrec_monitor_col_interval (ovsrec_monitor_columns[OVSREC_MONITOR_COL_INTERVAL])
#define ovsrec_monitor_col_md_name (ovsrec_monitor_columns[OVSREC_MONITOR_COL_MD_NAME])
#define ovsrec_monitor_col_fault (ovsrec_monitor_columns[OVSREC_MONITOR_COL_FAULT])
#define ovsrec_monitor_col_ma_name (ovsrec_monitor_columns[OVSREC_MONITOR_COL_MA_NAME])
#define ovsrec_monitor_col_mpid (ovsrec_monitor_columns[OVSREC_MONITOR_COL_MPID])
#define ovsrec_monitor_col_remote_mps (ovsrec_monitor_columns[OVSREC_MONITOR_COL_REMOTE_MPS])

extern struct ovsdb_idl_column ovsrec_monitor_columns[OVSREC_MONITOR_N_COLUMNS];

const struct ovsrec_monitor *ovsrec_monitor_first(const struct ovsdb_idl *);
const struct ovsrec_monitor *ovsrec_monitor_next(const struct ovsrec_monitor *);
#define OVSREC_MONITOR_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_monitor_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_monitor_next(ROW))
#define OVSREC_MONITOR_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_monitor_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_monitor_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_monitor_delete(const struct ovsrec_monitor *);
struct ovsrec_monitor *ovsrec_monitor_insert(struct ovsdb_idl_txn *);

void ovsrec_monitor_verify_fault(const struct ovsrec_monitor *);
void ovsrec_monitor_verify_interval(const struct ovsrec_monitor *);
void ovsrec_monitor_verify_ma_name(const struct ovsrec_monitor *);
void ovsrec_monitor_verify_md_name(const struct ovsrec_monitor *);
void ovsrec_monitor_verify_mpid(const struct ovsrec_monitor *);
void ovsrec_monitor_verify_remote_mps(const struct ovsrec_monitor *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_monitor directly.) */
const struct ovsdb_datum *ovsrec_monitor_get_fault(const struct ovsrec_monitor *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_monitor_get_interval(const struct ovsrec_monitor *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_monitor_get_ma_name(const struct ovsrec_monitor *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_monitor_get_md_name(const struct ovsrec_monitor *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_monitor_get_mpid(const struct ovsrec_monitor *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_monitor_get_remote_mps(const struct ovsrec_monitor *, enum ovsdb_atomic_type key_type);

void ovsrec_monitor_set_fault(const struct ovsrec_monitor *, const bool *fault, size_t n_fault);
void ovsrec_monitor_set_interval(const struct ovsrec_monitor *, const int64_t *interval, size_t n_interval);
void ovsrec_monitor_set_ma_name(const struct ovsrec_monitor *, const char *ma_name);
void ovsrec_monitor_set_md_name(const struct ovsrec_monitor *, const char *md_name);
void ovsrec_monitor_set_mpid(const struct ovsrec_monitor *, int64_t mpid);
void ovsrec_monitor_set_remote_mps(const struct ovsrec_monitor *, struct ovsrec_maintenance_point **remote_mps, size_t n_remote_mps);

/* NetFlow table. */
struct ovsrec_netflow {
	struct ovsdb_idl_row header_;

	/* active_timeout column. */
	int64_t active_timeout;

	/* add_id_to_interface column. */
	bool add_id_to_interface;

	/* engine_id column. */
	int64_t *engine_id;
	size_t n_engine_id;

	/* engine_type column. */
	int64_t *engine_type;
	size_t n_engine_type;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* targets column. */
	char **targets;
	size_t n_targets;
};

enum {
    OVSREC_NETFLOW_COL_ACTIVE_TIMEOUT,
    OVSREC_NETFLOW_COL_ADD_ID_TO_INTERFACE,
    OVSREC_NETFLOW_COL_ENGINE_ID,
    OVSREC_NETFLOW_COL_ENGINE_TYPE,
    OVSREC_NETFLOW_COL_EXTERNAL_IDS,
    OVSREC_NETFLOW_COL_TARGETS,
    OVSREC_NETFLOW_N_COLUMNS
};

#define ovsrec_netflow_col_engine_id (ovsrec_netflow_columns[OVSREC_NETFLOW_COL_ENGINE_ID])
#define ovsrec_netflow_col_active_timeout (ovsrec_netflow_columns[OVSREC_NETFLOW_COL_ACTIVE_TIMEOUT])
#define ovsrec_netflow_col_add_id_to_interface (ovsrec_netflow_columns[OVSREC_NETFLOW_COL_ADD_ID_TO_INTERFACE])
#define ovsrec_netflow_col_external_ids (ovsrec_netflow_columns[OVSREC_NETFLOW_COL_EXTERNAL_IDS])
#define ovsrec_netflow_col_targets (ovsrec_netflow_columns[OVSREC_NETFLOW_COL_TARGETS])
#define ovsrec_netflow_col_engine_type (ovsrec_netflow_columns[OVSREC_NETFLOW_COL_ENGINE_TYPE])

extern struct ovsdb_idl_column ovsrec_netflow_columns[OVSREC_NETFLOW_N_COLUMNS];

const struct ovsrec_netflow *ovsrec_netflow_first(const struct ovsdb_idl *);
const struct ovsrec_netflow *ovsrec_netflow_next(const struct ovsrec_netflow *);
#define OVSREC_NETFLOW_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_netflow_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_netflow_next(ROW))
#define OVSREC_NETFLOW_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_netflow_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_netflow_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_netflow_delete(const struct ovsrec_netflow *);
struct ovsrec_netflow *ovsrec_netflow_insert(struct ovsdb_idl_txn *);

void ovsrec_netflow_verify_active_timeout(const struct ovsrec_netflow *);
void ovsrec_netflow_verify_add_id_to_interface(const struct ovsrec_netflow *);
void ovsrec_netflow_verify_engine_id(const struct ovsrec_netflow *);
void ovsrec_netflow_verify_engine_type(const struct ovsrec_netflow *);
void ovsrec_netflow_verify_external_ids(const struct ovsrec_netflow *);
void ovsrec_netflow_verify_targets(const struct ovsrec_netflow *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_netflow directly.) */
const struct ovsdb_datum *ovsrec_netflow_get_active_timeout(const struct ovsrec_netflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_netflow_get_add_id_to_interface(const struct ovsrec_netflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_netflow_get_engine_id(const struct ovsrec_netflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_netflow_get_engine_type(const struct ovsrec_netflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_netflow_get_external_ids(const struct ovsrec_netflow *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_netflow_get_targets(const struct ovsrec_netflow *, enum ovsdb_atomic_type key_type);

void ovsrec_netflow_set_active_timeout(const struct ovsrec_netflow *, int64_t active_timeout);
void ovsrec_netflow_set_add_id_to_interface(const struct ovsrec_netflow *, bool add_id_to_interface);
void ovsrec_netflow_set_engine_id(const struct ovsrec_netflow *, const int64_t *engine_id, size_t n_engine_id);
void ovsrec_netflow_set_engine_type(const struct ovsrec_netflow *, const int64_t *engine_type, size_t n_engine_type);
void ovsrec_netflow_set_external_ids(const struct ovsrec_netflow *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_netflow_set_targets(const struct ovsrec_netflow *, char **targets, size_t n_targets);

/* Open_vSwitch table. */
struct ovsrec_open_vswitch {
	struct ovsdb_idl_row header_;

	/* bridges column. */
	struct ovsrec_bridge **bridges;
	size_t n_bridges;

	/* capabilities column. */
	char **key_capabilities;
	struct ovsrec_capability **value_capabilities;
	size_t n_capabilities;

	/* cur_cfg column. */
	int64_t cur_cfg;

	/* db_version column. */
	char *db_version;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* manager_options column. */
	struct ovsrec_manager **manager_options;
	size_t n_manager_options;

	/* next_cfg column. */
	int64_t next_cfg;

	/* ovs_version column. */
	char *ovs_version;

	/* ssl column. */
	struct ovsrec_ssl *ssl;

	/* statistics column. */
	char **key_statistics;
	char **value_statistics;
	size_t n_statistics;

	/* system_type column. */
	char *system_type;

	/* system_version column. */
	char *system_version;
};

enum {
    OVSREC_OPEN_VSWITCH_COL_BRIDGES,
    OVSREC_OPEN_VSWITCH_COL_CAPABILITIES,
    OVSREC_OPEN_VSWITCH_COL_CUR_CFG,
    OVSREC_OPEN_VSWITCH_COL_DB_VERSION,
    OVSREC_OPEN_VSWITCH_COL_EXTERNAL_IDS,
    OVSREC_OPEN_VSWITCH_COL_MANAGER_OPTIONS,
    OVSREC_OPEN_VSWITCH_COL_NEXT_CFG,
    OVSREC_OPEN_VSWITCH_COL_OVS_VERSION,
    OVSREC_OPEN_VSWITCH_COL_SSL,
    OVSREC_OPEN_VSWITCH_COL_STATISTICS,
    OVSREC_OPEN_VSWITCH_COL_SYSTEM_TYPE,
    OVSREC_OPEN_VSWITCH_COL_SYSTEM_VERSION,
    OVSREC_OPEN_VSWITCH_N_COLUMNS
};

#define ovsrec_open_vswitch_col_bridges (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_BRIDGES])
#define ovsrec_open_vswitch_col_statistics (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_STATISTICS])
#define ovsrec_open_vswitch_col_db_version (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_DB_VERSION])
#define ovsrec_open_vswitch_col_next_cfg (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_NEXT_CFG])
#define ovsrec_open_vswitch_col_ovs_version (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_OVS_VERSION])
#define ovsrec_open_vswitch_col_capabilities (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_CAPABILITIES])
#define ovsrec_open_vswitch_col_ssl (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_SSL])
#define ovsrec_open_vswitch_col_manager_options (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_MANAGER_OPTIONS])
#define ovsrec_open_vswitch_col_external_ids (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_EXTERNAL_IDS])
#define ovsrec_open_vswitch_col_system_version (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_SYSTEM_VERSION])
#define ovsrec_open_vswitch_col_cur_cfg (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_CUR_CFG])
#define ovsrec_open_vswitch_col_system_type (ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_COL_SYSTEM_TYPE])

extern struct ovsdb_idl_column ovsrec_open_vswitch_columns[OVSREC_OPEN_VSWITCH_N_COLUMNS];

const struct ovsrec_open_vswitch *ovsrec_open_vswitch_first(const struct ovsdb_idl *);
const struct ovsrec_open_vswitch *ovsrec_open_vswitch_next(const struct ovsrec_open_vswitch *);
#define OVSREC_OPEN_VSWITCH_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_open_vswitch_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_open_vswitch_next(ROW))
#define OVSREC_OPEN_VSWITCH_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_open_vswitch_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_open_vswitch_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_open_vswitch_delete(const struct ovsrec_open_vswitch *);
struct ovsrec_open_vswitch *ovsrec_open_vswitch_insert(struct ovsdb_idl_txn *);

void ovsrec_open_vswitch_verify_bridges(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_capabilities(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_cur_cfg(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_db_version(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_external_ids(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_manager_options(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_next_cfg(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_ovs_version(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_ssl(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_statistics(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_system_type(const struct ovsrec_open_vswitch *);
void ovsrec_open_vswitch_verify_system_version(const struct ovsrec_open_vswitch *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_open_vswitch directly.) */
const struct ovsdb_datum *ovsrec_open_vswitch_get_bridges(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_capabilities(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_cur_cfg(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_db_version(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_external_ids(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_manager_options(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_next_cfg(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_ovs_version(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_ssl(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_statistics(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_system_type(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_open_vswitch_get_system_version(const struct ovsrec_open_vswitch *, enum ovsdb_atomic_type key_type);

void ovsrec_open_vswitch_set_bridges(const struct ovsrec_open_vswitch *, struct ovsrec_bridge **bridges, size_t n_bridges);
void ovsrec_open_vswitch_set_capabilities(const struct ovsrec_open_vswitch *, char **key_capabilities, struct ovsrec_capability **value_capabilities, size_t n_capabilities);
void ovsrec_open_vswitch_set_cur_cfg(const struct ovsrec_open_vswitch *, int64_t cur_cfg);
void ovsrec_open_vswitch_set_db_version(const struct ovsrec_open_vswitch *, const char *db_version);
void ovsrec_open_vswitch_set_external_ids(const struct ovsrec_open_vswitch *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_open_vswitch_set_manager_options(const struct ovsrec_open_vswitch *, struct ovsrec_manager **manager_options, size_t n_manager_options);
void ovsrec_open_vswitch_set_next_cfg(const struct ovsrec_open_vswitch *, int64_t next_cfg);
void ovsrec_open_vswitch_set_ovs_version(const struct ovsrec_open_vswitch *, const char *ovs_version);
void ovsrec_open_vswitch_set_ssl(const struct ovsrec_open_vswitch *, const struct ovsrec_ssl *ssl);
void ovsrec_open_vswitch_set_statistics(const struct ovsrec_open_vswitch *, char **key_statistics, char **value_statistics, size_t n_statistics);
void ovsrec_open_vswitch_set_system_type(const struct ovsrec_open_vswitch *, const char *system_type);
void ovsrec_open_vswitch_set_system_version(const struct ovsrec_open_vswitch *, const char *system_version);

/* Port table. */
struct ovsrec_port {
	struct ovsdb_idl_row header_;

	/* bond_downdelay column. */
	int64_t bond_downdelay;

	/* bond_fake_iface column. */
	bool bond_fake_iface;

	/* bond_mode column. */
	char *bond_mode;

	/* bond_updelay column. */
	int64_t bond_updelay;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* fake_bridge column. */
	bool fake_bridge;

	/* interfaces column. */
	struct ovsrec_interface **interfaces;
	size_t n_interfaces;

	/* lacp column. */
	char *lacp;

	/* mac column. */
	char *mac;

	/* name column. */
	char *name;	/* Always nonnull. */

	/* other_config column. */
	char **key_other_config;
	char **value_other_config;
	size_t n_other_config;

	/* qos column. */
	struct ovsrec_qos *qos;

	/* tag column. */
	int64_t *tag;
	size_t n_tag;

	/* trunks column. */
	int64_t *trunks;
	size_t n_trunks;
};

enum {
    OVSREC_PORT_COL_BOND_DOWNDELAY,
    OVSREC_PORT_COL_BOND_FAKE_IFACE,
    OVSREC_PORT_COL_BOND_MODE,
    OVSREC_PORT_COL_BOND_UPDELAY,
    OVSREC_PORT_COL_EXTERNAL_IDS,
    OVSREC_PORT_COL_FAKE_BRIDGE,
    OVSREC_PORT_COL_INTERFACES,
    OVSREC_PORT_COL_LACP,
    OVSREC_PORT_COL_MAC,
    OVSREC_PORT_COL_NAME,
    OVSREC_PORT_COL_OTHER_CONFIG,
    OVSREC_PORT_COL_QOS,
    OVSREC_PORT_COL_TAG,
    OVSREC_PORT_COL_TRUNKS,
    OVSREC_PORT_N_COLUMNS
};

#define ovsrec_port_col_bond_mode (ovsrec_port_columns[OVSREC_PORT_COL_BOND_MODE])
#define ovsrec_port_col_trunks (ovsrec_port_columns[OVSREC_PORT_COL_TRUNKS])
#define ovsrec_port_col_qos (ovsrec_port_columns[OVSREC_PORT_COL_QOS])
#define ovsrec_port_col_name (ovsrec_port_columns[OVSREC_PORT_COL_NAME])
#define ovsrec_port_col_bond_downdelay (ovsrec_port_columns[OVSREC_PORT_COL_BOND_DOWNDELAY])
#define ovsrec_port_col_interfaces (ovsrec_port_columns[OVSREC_PORT_COL_INTERFACES])
#define ovsrec_port_col_other_config (ovsrec_port_columns[OVSREC_PORT_COL_OTHER_CONFIG])
#define ovsrec_port_col_bond_fake_iface (ovsrec_port_columns[OVSREC_PORT_COL_BOND_FAKE_IFACE])
#define ovsrec_port_col_lacp (ovsrec_port_columns[OVSREC_PORT_COL_LACP])
#define ovsrec_port_col_mac (ovsrec_port_columns[OVSREC_PORT_COL_MAC])
#define ovsrec_port_col_tag (ovsrec_port_columns[OVSREC_PORT_COL_TAG])
#define ovsrec_port_col_external_ids (ovsrec_port_columns[OVSREC_PORT_COL_EXTERNAL_IDS])
#define ovsrec_port_col_fake_bridge (ovsrec_port_columns[OVSREC_PORT_COL_FAKE_BRIDGE])
#define ovsrec_port_col_bond_updelay (ovsrec_port_columns[OVSREC_PORT_COL_BOND_UPDELAY])

extern struct ovsdb_idl_column ovsrec_port_columns[OVSREC_PORT_N_COLUMNS];

const struct ovsrec_port *ovsrec_port_first(const struct ovsdb_idl *);
const struct ovsrec_port *ovsrec_port_next(const struct ovsrec_port *);
#define OVSREC_PORT_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_port_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_port_next(ROW))
#define OVSREC_PORT_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_port_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_port_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_port_delete(const struct ovsrec_port *);
struct ovsrec_port *ovsrec_port_insert(struct ovsdb_idl_txn *);

void ovsrec_port_verify_bond_downdelay(const struct ovsrec_port *);
void ovsrec_port_verify_bond_fake_iface(const struct ovsrec_port *);
void ovsrec_port_verify_bond_mode(const struct ovsrec_port *);
void ovsrec_port_verify_bond_updelay(const struct ovsrec_port *);
void ovsrec_port_verify_external_ids(const struct ovsrec_port *);
void ovsrec_port_verify_fake_bridge(const struct ovsrec_port *);
void ovsrec_port_verify_interfaces(const struct ovsrec_port *);
void ovsrec_port_verify_lacp(const struct ovsrec_port *);
void ovsrec_port_verify_mac(const struct ovsrec_port *);
void ovsrec_port_verify_name(const struct ovsrec_port *);
void ovsrec_port_verify_other_config(const struct ovsrec_port *);
void ovsrec_port_verify_qos(const struct ovsrec_port *);
void ovsrec_port_verify_tag(const struct ovsrec_port *);
void ovsrec_port_verify_trunks(const struct ovsrec_port *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_port directly.) */
const struct ovsdb_datum *ovsrec_port_get_bond_downdelay(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_bond_fake_iface(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_bond_mode(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_bond_updelay(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_external_ids(const struct ovsrec_port *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_port_get_fake_bridge(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_interfaces(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_lacp(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_mac(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_name(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_other_config(const struct ovsrec_port *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_port_get_qos(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_tag(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_port_get_trunks(const struct ovsrec_port *, enum ovsdb_atomic_type key_type);

void ovsrec_port_set_bond_downdelay(const struct ovsrec_port *, int64_t bond_downdelay);
void ovsrec_port_set_bond_fake_iface(const struct ovsrec_port *, bool bond_fake_iface);
void ovsrec_port_set_bond_mode(const struct ovsrec_port *, const char *bond_mode);
void ovsrec_port_set_bond_updelay(const struct ovsrec_port *, int64_t bond_updelay);
void ovsrec_port_set_external_ids(const struct ovsrec_port *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_port_set_fake_bridge(const struct ovsrec_port *, bool fake_bridge);
void ovsrec_port_set_interfaces(const struct ovsrec_port *, struct ovsrec_interface **interfaces, size_t n_interfaces);
void ovsrec_port_set_lacp(const struct ovsrec_port *, const char *lacp);
void ovsrec_port_set_mac(const struct ovsrec_port *, const char *mac);
void ovsrec_port_set_name(const struct ovsrec_port *, const char *name);
void ovsrec_port_set_other_config(const struct ovsrec_port *, char **key_other_config, char **value_other_config, size_t n_other_config);
void ovsrec_port_set_qos(const struct ovsrec_port *, const struct ovsrec_qos *qos);
void ovsrec_port_set_tag(const struct ovsrec_port *, const int64_t *tag, size_t n_tag);
void ovsrec_port_set_trunks(const struct ovsrec_port *, const int64_t *trunks, size_t n_trunks);

/* QoS table. */
struct ovsrec_qos {
	struct ovsdb_idl_row header_;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* other_config column. */
	char **key_other_config;
	char **value_other_config;
	size_t n_other_config;

	/* queues column. */
	int64_t *key_queues;
	struct ovsrec_queue **value_queues;
	size_t n_queues;

	/* type column. */
	char *type;	/* Always nonnull. */
};

enum {
    OVSREC_QOS_COL_EXTERNAL_IDS,
    OVSREC_QOS_COL_OTHER_CONFIG,
    OVSREC_QOS_COL_QUEUES,
    OVSREC_QOS_COL_TYPE,
    OVSREC_QOS_N_COLUMNS
};

#define ovsrec_qos_col_external_ids (ovsrec_qos_columns[OVSREC_QOS_COL_EXTERNAL_IDS])
#define ovsrec_qos_col_other_config (ovsrec_qos_columns[OVSREC_QOS_COL_OTHER_CONFIG])
#define ovsrec_qos_col_type (ovsrec_qos_columns[OVSREC_QOS_COL_TYPE])
#define ovsrec_qos_col_queues (ovsrec_qos_columns[OVSREC_QOS_COL_QUEUES])

extern struct ovsdb_idl_column ovsrec_qos_columns[OVSREC_QOS_N_COLUMNS];

const struct ovsrec_qos *ovsrec_qos_first(const struct ovsdb_idl *);
const struct ovsrec_qos *ovsrec_qos_next(const struct ovsrec_qos *);
#define OVSREC_QOS_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_qos_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_qos_next(ROW))
#define OVSREC_QOS_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_qos_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_qos_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_qos_delete(const struct ovsrec_qos *);
struct ovsrec_qos *ovsrec_qos_insert(struct ovsdb_idl_txn *);

void ovsrec_qos_verify_external_ids(const struct ovsrec_qos *);
void ovsrec_qos_verify_other_config(const struct ovsrec_qos *);
void ovsrec_qos_verify_queues(const struct ovsrec_qos *);
void ovsrec_qos_verify_type(const struct ovsrec_qos *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_qos directly.) */
const struct ovsdb_datum *ovsrec_qos_get_external_ids(const struct ovsrec_qos *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_qos_get_other_config(const struct ovsrec_qos *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_qos_get_queues(const struct ovsrec_qos *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_qos_get_type(const struct ovsrec_qos *, enum ovsdb_atomic_type key_type);

void ovsrec_qos_set_external_ids(const struct ovsrec_qos *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_qos_set_other_config(const struct ovsrec_qos *, char **key_other_config, char **value_other_config, size_t n_other_config);
void ovsrec_qos_set_queues(const struct ovsrec_qos *, const int64_t *key_queues, struct ovsrec_queue **value_queues, size_t n_queues);
void ovsrec_qos_set_type(const struct ovsrec_qos *, const char *type);

/* Queue table. */
struct ovsrec_queue {
	struct ovsdb_idl_row header_;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* other_config column. */
	char **key_other_config;
	char **value_other_config;
	size_t n_other_config;
};

enum {
    OVSREC_QUEUE_COL_EXTERNAL_IDS,
    OVSREC_QUEUE_COL_OTHER_CONFIG,
    OVSREC_QUEUE_N_COLUMNS
};

#define ovsrec_queue_col_external_ids (ovsrec_queue_columns[OVSREC_QUEUE_COL_EXTERNAL_IDS])
#define ovsrec_queue_col_other_config (ovsrec_queue_columns[OVSREC_QUEUE_COL_OTHER_CONFIG])

extern struct ovsdb_idl_column ovsrec_queue_columns[OVSREC_QUEUE_N_COLUMNS];

const struct ovsrec_queue *ovsrec_queue_first(const struct ovsdb_idl *);
const struct ovsrec_queue *ovsrec_queue_next(const struct ovsrec_queue *);
#define OVSREC_QUEUE_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_queue_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_queue_next(ROW))
#define OVSREC_QUEUE_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_queue_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_queue_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_queue_delete(const struct ovsrec_queue *);
struct ovsrec_queue *ovsrec_queue_insert(struct ovsdb_idl_txn *);

void ovsrec_queue_verify_external_ids(const struct ovsrec_queue *);
void ovsrec_queue_verify_other_config(const struct ovsrec_queue *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_queue directly.) */
const struct ovsdb_datum *ovsrec_queue_get_external_ids(const struct ovsrec_queue *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_queue_get_other_config(const struct ovsrec_queue *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);

void ovsrec_queue_set_external_ids(const struct ovsrec_queue *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_queue_set_other_config(const struct ovsrec_queue *, char **key_other_config, char **value_other_config, size_t n_other_config);

/* SSL table. */
struct ovsrec_ssl {
	struct ovsdb_idl_row header_;

	/* bootstrap_ca_cert column. */
	bool bootstrap_ca_cert;

	/* ca_cert column. */
	char *ca_cert;	/* Always nonnull. */

	/* certificate column. */
	char *certificate;	/* Always nonnull. */

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* private_key column. */
	char *private_key;	/* Always nonnull. */
};

enum {
    OVSREC_SSL_COL_BOOTSTRAP_CA_CERT,
    OVSREC_SSL_COL_CA_CERT,
    OVSREC_SSL_COL_CERTIFICATE,
    OVSREC_SSL_COL_EXTERNAL_IDS,
    OVSREC_SSL_COL_PRIVATE_KEY,
    OVSREC_SSL_N_COLUMNS
};

#define ovsrec_ssl_col_ca_cert (ovsrec_ssl_columns[OVSREC_SSL_COL_CA_CERT])
#define ovsrec_ssl_col_private_key (ovsrec_ssl_columns[OVSREC_SSL_COL_PRIVATE_KEY])
#define ovsrec_ssl_col_bootstrap_ca_cert (ovsrec_ssl_columns[OVSREC_SSL_COL_BOOTSTRAP_CA_CERT])
#define ovsrec_ssl_col_external_ids (ovsrec_ssl_columns[OVSREC_SSL_COL_EXTERNAL_IDS])
#define ovsrec_ssl_col_certificate (ovsrec_ssl_columns[OVSREC_SSL_COL_CERTIFICATE])

extern struct ovsdb_idl_column ovsrec_ssl_columns[OVSREC_SSL_N_COLUMNS];

const struct ovsrec_ssl *ovsrec_ssl_first(const struct ovsdb_idl *);
const struct ovsrec_ssl *ovsrec_ssl_next(const struct ovsrec_ssl *);
#define OVSREC_SSL_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_ssl_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_ssl_next(ROW))
#define OVSREC_SSL_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_ssl_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_ssl_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_ssl_delete(const struct ovsrec_ssl *);
struct ovsrec_ssl *ovsrec_ssl_insert(struct ovsdb_idl_txn *);

void ovsrec_ssl_verify_bootstrap_ca_cert(const struct ovsrec_ssl *);
void ovsrec_ssl_verify_ca_cert(const struct ovsrec_ssl *);
void ovsrec_ssl_verify_certificate(const struct ovsrec_ssl *);
void ovsrec_ssl_verify_external_ids(const struct ovsrec_ssl *);
void ovsrec_ssl_verify_private_key(const struct ovsrec_ssl *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_ssl directly.) */
const struct ovsdb_datum *ovsrec_ssl_get_bootstrap_ca_cert(const struct ovsrec_ssl *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_ssl_get_ca_cert(const struct ovsrec_ssl *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_ssl_get_certificate(const struct ovsrec_ssl *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_ssl_get_external_ids(const struct ovsrec_ssl *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_ssl_get_private_key(const struct ovsrec_ssl *, enum ovsdb_atomic_type key_type);

void ovsrec_ssl_set_bootstrap_ca_cert(const struct ovsrec_ssl *, bool bootstrap_ca_cert);
void ovsrec_ssl_set_ca_cert(const struct ovsrec_ssl *, const char *ca_cert);
void ovsrec_ssl_set_certificate(const struct ovsrec_ssl *, const char *certificate);
void ovsrec_ssl_set_external_ids(const struct ovsrec_ssl *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_ssl_set_private_key(const struct ovsrec_ssl *, const char *private_key);

/* sFlow table. */
struct ovsrec_sflow {
	struct ovsdb_idl_row header_;

	/* agent column. */
	char *agent;

	/* external_ids column. */
	char **key_external_ids;
	char **value_external_ids;
	size_t n_external_ids;

	/* header column. */
	int64_t *header;
	size_t n_header;

	/* polling column. */
	int64_t *polling;
	size_t n_polling;

	/* sampling column. */
	int64_t *sampling;
	size_t n_sampling;

	/* targets column. */
	char **targets;
	size_t n_targets;
};

enum {
    OVSREC_SFLOW_COL_AGENT,
    OVSREC_SFLOW_COL_EXTERNAL_IDS,
    OVSREC_SFLOW_COL_HEADER,
    OVSREC_SFLOW_COL_POLLING,
    OVSREC_SFLOW_COL_SAMPLING,
    OVSREC_SFLOW_COL_TARGETS,
    OVSREC_SFLOW_N_COLUMNS
};

#define ovsrec_sflow_col_agent (ovsrec_sflow_columns[OVSREC_SFLOW_COL_AGENT])
#define ovsrec_sflow_col_sampling (ovsrec_sflow_columns[OVSREC_SFLOW_COL_SAMPLING])
#define ovsrec_sflow_col_header (ovsrec_sflow_columns[OVSREC_SFLOW_COL_HEADER])
#define ovsrec_sflow_col_polling (ovsrec_sflow_columns[OVSREC_SFLOW_COL_POLLING])
#define ovsrec_sflow_col_external_ids (ovsrec_sflow_columns[OVSREC_SFLOW_COL_EXTERNAL_IDS])
#define ovsrec_sflow_col_targets (ovsrec_sflow_columns[OVSREC_SFLOW_COL_TARGETS])

extern struct ovsdb_idl_column ovsrec_sflow_columns[OVSREC_SFLOW_N_COLUMNS];

const struct ovsrec_sflow *ovsrec_sflow_first(const struct ovsdb_idl *);
const struct ovsrec_sflow *ovsrec_sflow_next(const struct ovsrec_sflow *);
#define OVSREC_SFLOW_FOR_EACH(ROW, IDL) \
        for ((ROW) = ovsrec_sflow_first(IDL); \
             (ROW); \
             (ROW) = ovsrec_sflow_next(ROW))
#define OVSREC_SFLOW_FOR_EACH_SAFE(ROW, NEXT, IDL) \
        for ((ROW) = ovsrec_sflow_first(IDL); \
             (ROW) ? ((NEXT) = ovsrec_sflow_next(ROW), 1) : 0; \
             (ROW) = (NEXT))

void ovsrec_sflow_delete(const struct ovsrec_sflow *);
struct ovsrec_sflow *ovsrec_sflow_insert(struct ovsdb_idl_txn *);

void ovsrec_sflow_verify_agent(const struct ovsrec_sflow *);
void ovsrec_sflow_verify_external_ids(const struct ovsrec_sflow *);
void ovsrec_sflow_verify_header(const struct ovsrec_sflow *);
void ovsrec_sflow_verify_polling(const struct ovsrec_sflow *);
void ovsrec_sflow_verify_sampling(const struct ovsrec_sflow *);
void ovsrec_sflow_verify_targets(const struct ovsrec_sflow *);

/* Functions for fetching columns as "struct ovsdb_datum"s.  (This is
   rarely useful.  More often, it is easier to access columns by using
   the members of ovsrec_sflow directly.) */
const struct ovsdb_datum *ovsrec_sflow_get_agent(const struct ovsrec_sflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_sflow_get_external_ids(const struct ovsrec_sflow *, enum ovsdb_atomic_type key_type, enum ovsdb_atomic_type value_type);
const struct ovsdb_datum *ovsrec_sflow_get_header(const struct ovsrec_sflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_sflow_get_polling(const struct ovsrec_sflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_sflow_get_sampling(const struct ovsrec_sflow *, enum ovsdb_atomic_type key_type);
const struct ovsdb_datum *ovsrec_sflow_get_targets(const struct ovsrec_sflow *, enum ovsdb_atomic_type key_type);

void ovsrec_sflow_set_agent(const struct ovsrec_sflow *, const char *agent);
void ovsrec_sflow_set_external_ids(const struct ovsrec_sflow *, char **key_external_ids, char **value_external_ids, size_t n_external_ids);
void ovsrec_sflow_set_header(const struct ovsrec_sflow *, const int64_t *header, size_t n_header);
void ovsrec_sflow_set_polling(const struct ovsrec_sflow *, const int64_t *polling, size_t n_polling);
void ovsrec_sflow_set_sampling(const struct ovsrec_sflow *, const int64_t *sampling, size_t n_sampling);
void ovsrec_sflow_set_targets(const struct ovsrec_sflow *, char **targets, size_t n_targets);

enum {
    OVSREC_TABLE_BRIDGE,
    OVSREC_TABLE_CAPABILITY,
    OVSREC_TABLE_CONTROLLER,
    OVSREC_TABLE_INTERFACE,
    OVSREC_TABLE_MAINTENANCE_POINT,
    OVSREC_TABLE_MANAGER,
    OVSREC_TABLE_MIRROR,
    OVSREC_TABLE_MONITOR,
    OVSREC_TABLE_NETFLOW,
    OVSREC_TABLE_OPEN_VSWITCH,
    OVSREC_TABLE_PORT,
    OVSREC_TABLE_QOS,
    OVSREC_TABLE_QUEUE,
    OVSREC_TABLE_SSL,
    OVSREC_TABLE_SFLOW,
    OVSREC_N_TABLES
};

#define ovsrec_table_bridge (ovsrec_table_classes[OVSREC_TABLE_BRIDGE])
#define ovsrec_table_qos (ovsrec_table_classes[OVSREC_TABLE_QOS])
#define ovsrec_table_monitor (ovsrec_table_classes[OVSREC_TABLE_MONITOR])
#define ovsrec_table_sflow (ovsrec_table_classes[OVSREC_TABLE_SFLOW])
#define ovsrec_table_open_vswitch (ovsrec_table_classes[OVSREC_TABLE_OPEN_VSWITCH])
#define ovsrec_table_controller (ovsrec_table_classes[OVSREC_TABLE_CONTROLLER])
#define ovsrec_table_queue (ovsrec_table_classes[OVSREC_TABLE_QUEUE])
#define ovsrec_table_ssl (ovsrec_table_classes[OVSREC_TABLE_SSL])
#define ovsrec_table_manager (ovsrec_table_classes[OVSREC_TABLE_MANAGER])
#define ovsrec_table_capability (ovsrec_table_classes[OVSREC_TABLE_CAPABILITY])
#define ovsrec_table_mirror (ovsrec_table_classes[OVSREC_TABLE_MIRROR])
#define ovsrec_table_interface (ovsrec_table_classes[OVSREC_TABLE_INTERFACE])
#define ovsrec_table_netflow (ovsrec_table_classes[OVSREC_TABLE_NETFLOW])
#define ovsrec_table_maintenance_point (ovsrec_table_classes[OVSREC_TABLE_MAINTENANCE_POINT])
#define ovsrec_table_port (ovsrec_table_classes[OVSREC_TABLE_PORT])

extern struct ovsdb_idl_table_class ovsrec_table_classes[OVSREC_N_TABLES];

extern struct ovsdb_idl_class ovsrec_idl_class;

void ovsrec_init(void);

#endif /* OVSREC_IDL_HEADER */
