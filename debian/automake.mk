EXTRA_DIST += \
	debian/changelog \
	debian/compat \
	debian/control \
	debian/copyright \
	debian/copyright.in \
	debian/dirs \
	debian/openvswitch-common.dirs \
	debian/openvswitch-common.docs \
	debian/openvswitch-common.install \
	debian/openvswitch-common.manpages \
	debian/openvswitch-ipsec.dirs \
	debian/openvswitch-ipsec.init \
	debian/openvswitch-ipsec.install \
	debian/openvswitch-pki.dirs \
	debian/openvswitch-pki.postinst \
	debian/openvswitch-pki.postrm \
	debian/openvswitch-switch.README.Debian \
	debian/openvswitch-switch.dirs \
	debian/openvswitch-switch.init \
	debian/openvswitch-switch.install \
	debian/openvswitch-switch.logrotate \
	debian/openvswitch-switch.manpages \
	debian/openvswitch-switch.postinst \
	debian/openvswitch-switch.postrm \
	debian/openvswitch-switch.default \
	debian/openvswitch-switch.links \
	debian/openvswitch-test.install \
	debian/openvswitch-test.manpages \
	debian/openvswitch-vtep.default \
	debian/openvswitch-vtep.dirs \
	debian/openvswitch-vtep.init \
	debian/openvswitch-vtep.install \
	debian/openvswitch-vtep.manpages \
	debian/ovs-monitor-ipsec \
	debian/python-openvswitch.install \
	debian/rules \
	debian/ifupdown.sh \
	debian/source/format

check-debian-changelog-version:
	@DEB_VERSION=`echo '$(VERSION)' | sed 's/pre/~pre/'`;		     \
	if $(FGREP) '($(DEB_VERSION)' $(srcdir)/debian/changelog >/dev/null; \
	then								     \
	  :;								     \
	else								     \
	  echo "Update debian/changelog to mention version $(VERSION)";	     \
	  exit 1;							     \
	fi
ALL_LOCAL += check-debian-changelog-version
DIST_HOOKS += check-debian-changelog-version

$(srcdir)/debian/copyright: AUTHORS debian/copyright.in
	{ sed -n -e '/%AUTHORS%/q' -e p < $(srcdir)/debian/copyright.in;   \
	  sed '1,/^$$/d' $(srcdir)/AUTHORS |				   \
		sed -n -e '/^$$/q' -e 's/^/  /p';			   \
	  sed -e '1,/%AUTHORS%/d' $(srcdir)/debian/copyright.in;	   \
	} > $@

DISTCLEANFILES += debian/copyright
