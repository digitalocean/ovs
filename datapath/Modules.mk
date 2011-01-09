# Some modules should be built and distributed, e.g. openvswitch.
#
# Some modules should be distributed but not built, e.g. we do not build
# veth if the kernel in question already has it.
#
# Some modules should be built but not distributed, e.g. third-party
# hwtable modules.
both_modules = openvswitch
build_modules = $(both_modules)	# Modules to build
dist_modules = $(both_modules)	# Modules to distribute

openvswitch_sources = \
	actions.c \
	checksum.c \
	datapath.c \
	dp_notify.c \
	dp_sysfs_dp.c \
	dp_sysfs_if.c \
	flow.c \
	loop_counter.c \
	table.c \
	tunnel.c \
	vport.c \
	vport-capwap.c \
	vport-generic.c \
	vport-gre.c \
	vport-internal_dev.c \
	vport-netdev.c \
	vport-patch.c

openvswitch_headers = \
	actions.h \
	checksum.h \
	compat.h \
	datapath.h \
	dp_sysfs.h \
	flow.h \
	loop_counter.h \
	odp-compat.h \
	table.h \
	tunnel.h \
	vport.h \
	vport-generic.h \
	vport-internal_dev.h \
	vport-netdev.h

dist_sources = $(foreach module,$(dist_modules),$($(module)_sources))
dist_headers = $(foreach module,$(dist_modules),$($(module)_headers))
build_sources = $(foreach module,$(build_modules),$($(module)_sources))
build_headers = $(foreach module,$(build_modules),$($(module)_headers))
build_links = $(notdir $(build_sources))
build_objects = $(notdir $(patsubst %.c,%.o,$(build_sources)))
