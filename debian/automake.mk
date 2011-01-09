EXTRA_DIST += \
	debian/changelog \
	debian/compat \
	debian/control \
	debian/control.modules.in \
	debian/copyright \
	debian/copyright.in \
	debian/dirs \
	debian/openvswitch-common.dirs \
	debian/openvswitch-common.install \
	debian/openvswitch-common.manpages \
	debian/openvswitch-controller.README.Debian \
	debian/openvswitch-controller.default \
	debian/openvswitch-controller.dirs \
	debian/openvswitch-controller.init \
	debian/openvswitch-controller.install \
	debian/openvswitch-controller.manpages \
	debian/openvswitch-controller.postinst \
	debian/openvswitch-datapath-module-_KVERS_.postinst.modules.in \
	debian/openvswitch-datapath-source.README.Debian \
	debian/openvswitch-datapath-source.copyright \
	debian/openvswitch-datapath-source.dirs \
	debian/openvswitch-datapath-source.install \
	debian/openvswitch-ipsec.dirs \
	debian/openvswitch-ipsec.init \
	debian/openvswitch-ipsec.install \
	debian/openvswitch-pki.postinst \
	debian/openvswitch-switch.README.Debian \
	debian/openvswitch-switch.dirs \
	debian/openvswitch-switch.init \
	debian/openvswitch-switch.install \
	debian/openvswitch-switch.logrotate \
	debian/openvswitch-switch.manpages \
	debian/openvswitch-switch.postinst \
	debian/openvswitch-switch.postrm \
	debian/openvswitch-switch.template \
	debian/ovs-bugtool \
	debian/ovs-bugtool.8 \
	debian/ovs-monitor-ipsec \
	debian/python-openvswitch.dirs \
	debian/python-openvswitch.install \
	debian/rules \
	debian/rules.modules

$(srcdir)/debian/copyright: AUTHORS debian/copyright.in
	{ sed -n -e '/%AUTHORS%/q' -e p < $(srcdir)/debian/copyright.in;   \
	  sed '1,/^$$/d' $(srcdir)/AUTHORS |				   \
		sed -n -e '/^$$/q' -e 's/^/  /p';			   \
	  sed -e '1,/%AUTHORS%/d' $(srcdir)/debian/copyright.in;	   \
	} > $@

DISTCLEANFILES += debian/copyright
