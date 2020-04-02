# Generated automatically -- do not modify!    -*- buffer-read-only: t -*-
# Spec file for Open vSwitch.

# Copyright (C) 2009, 2010, 2013, 2014, 2015, 2016 Nicira Networks, Inc.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without warranty of any kind.
#
# If tests have to be skipped while building, specify the '--without check'
# option. For example:
#     rpmbuild -bb --without check rhel/openvswitch-fedora.spec
#
# Support for executing kernel data path tests under rpmbuild is
# provided, however this is intended for use only in test environments
# and should not be used otherwise (these tests require root privileges).
# These tests can be executed, for example, via:
#    rpmbuild -rb --with check_datapath_kernel openvswitch-fedora.src.rpm
#
# These tests will use the currently installed OVS kernel modules, when
# testing out of tree kernel modules the appropriate openvswitch-kmod
# package should be installed first.

#%define kernel 2.6.40.4-5.fc15.x86_64

# If libcap-ng isn't available and there is no need for running OVS
# as regular user, specify the '--without libcapng'
%bcond_without libcapng
# To enable DPDK support, specify '--with dpdk' when building
%bcond_with dpdk
# Enable Python 3 by specifying '--with build_python3'.
# This is enabled by default for versions of the distribution that
# have Python 3 by default (Fedora > 22).
%bcond_with build_python3

# Enable PIE, bz#955181
%global _hardened_build 1

# some distros (e.g: RHEL-7) don't define _rundir macro yet
# Fedora 15 onwards uses /run as _rundir
%if 0%{!?_rundir:1}
%define _rundir /run
%endif

# define the python package prefix based on distribution version so that we can
# simultaneously support RHEL-based and later Fedora versions in this spec file.
%if 0%{?fedora} >= 25
%define _py2 python2
%endif

%if 0%{?rhel} || 0%{?fedora} < 25
%define _py2 python
%endif


Name: openvswitch
Summary: Open vSwitch
Group: System Environment/Daemons
URL: http://www.openvswitch.org/
Version: 2.11.3

# Nearly all of openvswitch is ASL 2.0.  The bugtool is LGPLv2+, and the
# lib/sflow*.[ch] files are SISSL
# datapath/ is GPLv2 (although not built into any of the binary packages)
License: ASL 2.0 and LGPLv2+ and SISSL
Release: 1%{?dist}
Source: http://openvswitch.org/releases/%{name}-%{version}.tar.gz

BuildRequires: gcc gcc-c++
BuildRequires: autoconf automake libtool
BuildRequires: systemd-units openssl openssl-devel
BuildRequires: %{_py2}-devel
%if 0%{?fedora} > 22 || %{with build_python3}
BuildRequires: python3-devel
%endif
BuildRequires: desktop-file-utils
BuildRequires: groff graphviz
BuildRequires: checkpolicy, selinux-policy-devel
BuildRequires: %{_py2}-sphinx
# make check dependencies
BuildRequires: %{_py2}-twisted%{?rhel:-core} %{_py2}-zope-interface %{_py2}-six
BuildRequires: procps-ng
%if %{with libcapng}
BuildRequires: libcap-ng libcap-ng-devel
%endif
%if %{with dpdk}
BuildRequires: libpcap-devel numactl-devel
BuildRequires: dpdk-devel >= 17.05.1
Provides: %{name}-dpdk = %{version}-%{release}
%endif
BuildRequires: unbound unbound-devel

Requires: openssl hostname iproute module-init-tools unbound
#Upstream kernel commit 4f647e0a3c37b8d5086214128614a136064110c3
#Requires: kernel >= 3.15.0-0

Requires(pre): shadow-utils
Requires(post): /bin/sed
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units
Obsoletes: openvswitch-controller <= 0:2.1.0-1

# to skip running checks, pass --without check
%bcond_without check
%bcond_with check_datapath_kernel

%description
Open vSwitch provides standard network bridging functions and
support for the OpenFlow protocol for remote per-flow control of
traffic.

%package selinux-policy
Summary: Open vSwitch SELinux policy
License: ASL 2.0
BuildArch: noarch
Requires: selinux-policy-targeted

%description selinux-policy
Tailored Open vSwitch SELinux policy

%package -n %{_py2}-openvswitch
Summary: Open vSwitch python2 bindings
License: ASL 2.0
BuildArch: noarch
Requires: %{_py2}
Requires: %{_py2}-six
%{?python_provide:%python_provide python2-openvswitch = %{version}-%{release}}
%description -n %{_py2}-openvswitch
Python bindings for the Open vSwitch database

%if 0%{?fedora} > 22 || %{with build_python3}
%package -n python3-openvswitch
Summary: Open vSwitch python3 bindings
License: ASL 2.0
BuildArch: noarch
Requires: python3
Requires: python3-six
%{?python_provide:%python_provide python3-openvswitch = %{version}-%{release}}

%description -n python3-openvswitch
Python bindings for the Open vSwitch database
%endif

%package test
Summary: Open vSwitch testing utilities
License: ASL 2.0
BuildArch: noarch
Requires: %{_py2}-openvswitch = %{version}-%{release}
Requires: %{_py2} %{_py2}-netifaces %{_py2}-twisted

%description test
Utilities that are useful to diagnose performance and connectivity
issues in Open vSwitch setup.

%package devel
Summary: Open vSwitch OpenFlow development package (library, headers)
License: ASL 2.0

%description devel
This provides shared library, libopenswitch.so and the openvswitch header
files needed to build an external application.

%if 0%{?rhel} > 7 || 0%{?fedora} > 28
%package -n network-scripts-%{name}
Summary: Open vSwitch legacy network service support
License: ASL 2.0
Requires: network-scripts
Supplements: (%{name} and network-scripts)

%description -n network-scripts-%{name}
This provides the ifup and ifdown scripts for use with the legacy network
service.
%endif

%package ipsec
Summary: Open vSwitch IPsec tunneling support
License: ASL 2.0
Requires: openvswitch %{_py2}-openvswitch libreswan

%description ipsec
This package provides IPsec tunneling support for OVS tunnels.

%prep
%setup -q

%build
%configure \
%if %{with libcapng}
        --enable-libcapng \
%else
        --disable-libcapng \
%endif
%if %{with dpdk}
        --with-dpdk=$(dirname %{_datadir}/dpdk/*/.config) \
%endif
        --enable-ssl \
        --disable-static \
        --enable-shared \
        --with-pkidir=%{_sharedstatedir}/openvswitch/pki \
%if 0%{?fedora} > 22 || %{with build_python3}
        PYTHON3=%{__python3} \
        PYTHON=%{__python2}
%else
        PYTHON=%{__python}
%endif

build-aux/dpdkstrip.py \
%if %{with dpdk}
        --dpdk \
%else
        --nodpdk \
%endif
        < rhel/usr_lib_systemd_system_ovs-vswitchd.service.in \
        > rhel/usr_lib_systemd_system_ovs-vswitchd.service

make %{?_smp_mflags}
make selinux-policy

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

install -d -m 0755 $RPM_BUILD_ROOT%{_rundir}/openvswitch
install -d -m 0750 $RPM_BUILD_ROOT%{_localstatedir}/log/openvswitch
install -d -m 0755 $RPM_BUILD_ROOT%{_sysconfdir}/openvswitch
copy_headers() {
    src=$1
    dst=$RPM_BUILD_ROOT/$2
    install -d -m 0755 $dst
    install -m 0644 $src/*.h $dst
}
copy_headers include %{_includedir}/openvswitch
copy_headers include/openflow %{_includedir}/openvswitch/openflow
copy_headers include/openvswitch %{_includedir}/openvswitch/openvswitch
copy_headers include/sparse %{_includedir}/openvswitch/sparse
copy_headers include/sparse/arpa %{_includedir}/openvswitch/sparse/arpa
copy_headers include/sparse/netinet %{_includedir}/openvswitch/sparse/netinet
copy_headers include/sparse/sys %{_includedir}/openvswitch/sparse/sys
copy_headers lib %{_includedir}/openvswitch/lib

%if %{with dpdk}
install -p -D -m 0644 rhel/usr_lib_udev_rules.d_91-vfio.rules \
        $RPM_BUILD_ROOT%{_prefix}/lib/udev/rules.d/91-vfio.rules
%endif

install -p -D -m 0644 \
        rhel/usr_share_openvswitch_scripts_systemd_sysconfig.template \
        $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig/openvswitch
for service in openvswitch ovsdb-server ovs-vswitchd ovs-delete-transient-ports \
                openvswitch-ipsec; do
        install -p -D -m 0644 \
                        rhel/usr_lib_systemd_system_${service}.service \
                        $RPM_BUILD_ROOT%{_unitdir}/${service}.service
done
install -m 0755 rhel/etc_init.d_openvswitch \
        $RPM_BUILD_ROOT%{_datadir}/openvswitch/scripts/openvswitch.init

install -p -D -m 0644 rhel/etc_openvswitch_default.conf \
        $RPM_BUILD_ROOT/%{_sysconfdir}/openvswitch/default.conf

install -p -D -m 0644 rhel/etc_logrotate.d_openvswitch \
        $RPM_BUILD_ROOT/%{_sysconfdir}/logrotate.d/openvswitch

install -m 0644 vswitchd/vswitch.ovsschema \
        $RPM_BUILD_ROOT/%{_datadir}/openvswitch/vswitch.ovsschema

install -d -m 0755 $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig/network-scripts/
install -p -m 0755 rhel/etc_sysconfig_network-scripts_ifdown-ovs \
        $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig/network-scripts/ifdown-ovs
install -p -m 0755 rhel/etc_sysconfig_network-scripts_ifup-ovs \
        $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig/network-scripts/ifup-ovs

install -d -m 0755 $RPM_BUILD_ROOT%{python2_sitelib}
cp -a $RPM_BUILD_ROOT/%{_datadir}/openvswitch/python/* \
   $RPM_BUILD_ROOT%{python2_sitelib}

%if 0%{?fedora} > 22 || %{with build_python3}
install -d -m 0755 $RPM_BUILD_ROOT%{python3_sitelib}
cp -a $RPM_BUILD_ROOT/%{_datadir}/openvswitch/python/ovs \
   $RPM_BUILD_ROOT%{python3_sitelib}
%endif

rm -rf $RPM_BUILD_ROOT/%{_datadir}/openvswitch/python/

install -d -m 0755 $RPM_BUILD_ROOT/%{_sharedstatedir}/openvswitch

touch $RPM_BUILD_ROOT%{_sysconfdir}/openvswitch/conf.db
touch $RPM_BUILD_ROOT%{_sysconfdir}/openvswitch/.conf.db.~lock~
touch $RPM_BUILD_ROOT%{_sysconfdir}/openvswitch/system-id.conf

install -p -m 644 -D selinux/openvswitch-custom.pp \
        $RPM_BUILD_ROOT%{_datadir}/selinux/packages/%{name}/openvswitch-custom.pp

install -d $RPM_BUILD_ROOT%{_prefix}/lib/firewalld/services/

install -p -D -m 0755 \
        rhel/usr_share_openvswitch_scripts_ovs-systemd-reload \
        $RPM_BUILD_ROOT%{_datadir}/openvswitch/scripts/ovs-systemd-reload

# remove unpackaged files
rm -f $RPM_BUILD_ROOT%{_bindir}/ovs-parse-backtrace \
        $RPM_BUILD_ROOT%{_sbindir}/ovs-vlan-bug-workaround \
        $RPM_BUILD_ROOT%{_mandir}/man8/ovs-vlan-bug-workaround.8

# remove ovn unpackages files
rm -f $RPM_BUILD_ROOT%{_bindir}/ovn*
rm -f $RPM_BUILD_ROOT%{_mandir}/man1/ovn*
rm -f $RPM_BUILD_ROOT%{_mandir}/man5/ovn*
rm -f $RPM_BUILD_ROOT%{_mandir}/man7/ovn*
rm -f $RPM_BUILD_ROOT%{_mandir}/man8/ovn*
rm -f $RPM_BUILD_ROOT%{_datadir}/openvswitch/ovn*
rm -f $RPM_BUILD_ROOT%{_datadir}/openvswitch/scripts/ovn*
rm -f $RPM_BUILD_ROOT%{_includedir}/ovn/*
rm -f $RPM_BUILD_ROOT%{_libdir}/libovn*

%check
%if %{with check}
    if make check TESTSUITEFLAGS='%{_smp_mflags}' RECHECK=yes; then :;
    else
        cat tests/testsuite.log
        exit 1
    fi
%endif
%if %{with check_datapath_kernel}
    if make check-kernel RECHECK=yes; then :;
    else
        cat tests/system-kmod-testsuite.log
        exit 1
    fi
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%pre selinux-policy
%selinux_relabel_pre -s targeted

%preun
%if 0%{?systemd_preun:1}
    %systemd_preun %{name}.service
%else
    if [ $1 -eq 0 ] ; then
        # Package removal, not upgrade
        /bin/systemctl --no-reload disable %{name}.service >/dev/null 2>&1 || :
        /bin/systemctl stop %{name}.service >/dev/null 2>&1 || :
    fi
%endif

%pre
getent group openvswitch >/dev/null || groupadd -r openvswitch
getent passwd openvswitch >/dev/null || \
    useradd -r -g openvswitch -d / -s /sbin/nologin \
    -c "Open vSwitch Daemons" openvswitch

%if %{with dpdk}
    getent group hugetlbfs >/dev/null || groupadd -r hugetlbfs
    usermod -a -G hugetlbfs openvswitch
%endif
exit 0

%post
if [ $1 -eq 1 ]; then
    sed -i 's:^#OVS_USER_ID=:OVS_USER_ID=:' /etc/sysconfig/openvswitch
    sed -i 's:\(.*su\).*:\1 openvswitch openvswitch:' %{_sysconfdir}/logrotate.d/openvswitch

%if %{with dpdk}
    sed -i \
        's@OVS_USER_ID="openvswitch:openvswitch"@OVS_USER_ID="openvswitch:hugetlbfs"@'\
        /etc/sysconfig/openvswitch
%endif

    # In the case of upgrade, this is not needed.
    chown -R openvswitch:openvswitch /etc/openvswitch
    chown -R openvswitch:openvswitch /var/log/openvswitch
fi

%if 0%{?systemd_post:1}
    %systemd_post %{name}.service
%else
    # Package install, not upgrade
    if [ $1 -eq 1 ]; then
        /bin/systemctl daemon-reload >dev/null || :
    fi
%endif

%post selinux-policy
%selinux_modules_install -s targeted %{_datadir}/selinux/packages/%{name}/openvswitch-custom.pp

%postun
%if 0%{?systemd_postun:1}
    %systemd_postun %{name}.service
%else
    /bin/systemctl daemon-reload >/dev/null 2>&1 || :
%endif

%postun selinux-policy
if [ $1 -eq 0 ] ; then
  %selinux_modules_uninstall -s targeted openvswitch-custom
fi

%posttrans selinux-policy
%selinux_relabel_post -s targeted

%files selinux-policy
%defattr(-,root,root)
%{_datadir}/selinux/packages/%{name}/openvswitch-custom.pp

%files -n %{_py2}-openvswitch
%{python2_sitelib}/ovs

%if 0%{?fedora} > 22 || %{with build_python3}
%files -n python3-openvswitch
%{python3_sitelib}/ovs
%endif

%files test
%{_bindir}/ovs-test
%{_bindir}/ovs-vlan-test
%{_bindir}/ovs-l3ping
%{_bindir}/ovs-pcap
%{_bindir}/ovs-tcpdump
%{_bindir}/ovs-tcpundump
%{_mandir}/man8/ovs-test.8*
%{_mandir}/man8/ovs-vlan-test.8*
%{_mandir}/man8/ovs-l3ping.8*
%{_mandir}/man1/ovs-pcap.1*
%{_mandir}/man8/ovs-tcpdump.8*
%{_mandir}/man1/ovs-tcpundump.1*
%{python2_sitelib}/ovstest

%files devel
%{_libdir}/lib*.so
%{_libdir}/pkgconfig/*.pc
%{_includedir}/openvswitch/*
%{_includedir}/openflow/*
%exclude %{_libdir}/*.la

%if 0%{?rhel} > 7 || 0%{?fedora} > 28
%files -n network-scripts-%{name}
%{_sysconfdir}/sysconfig/network-scripts/ifup-ovs
%{_sysconfdir}/sysconfig/network-scripts/ifdown-ovs
%endif

%files
%defattr(-,openvswitch,openvswitch)
%dir %{_sysconfdir}/openvswitch
%{_sysconfdir}/openvswitch/default.conf
%config %ghost %{_sysconfdir}/openvswitch/conf.db
%ghost %{_sysconfdir}/openvswitch/.conf.db.~lock~
%config %ghost %{_sysconfdir}/openvswitch/system-id.conf
%config(noreplace) %{_sysconfdir}/sysconfig/openvswitch
%defattr(-,root,root)
%{_sysconfdir}/bash_completion.d/ovs-appctl-bashcomp.bash
%{_sysconfdir}/bash_completion.d/ovs-vsctl-bashcomp.bash
%config(noreplace) %{_sysconfdir}/logrotate.d/openvswitch
%{_unitdir}/openvswitch.service
%{_unitdir}/ovsdb-server.service
%{_unitdir}/ovs-vswitchd.service
%{_unitdir}/ovs-delete-transient-ports.service
%{_datadir}/openvswitch/scripts/openvswitch.init
%if ! (0%{?rhel} > 7 || 0%{?fedora} > 28)
%{_sysconfdir}/sysconfig/network-scripts/ifup-ovs
%{_sysconfdir}/sysconfig/network-scripts/ifdown-ovs
%endif
%{_datadir}/openvswitch/bugtool-plugins/
%{_datadir}/openvswitch/scripts/ovs-bugtool-*
%{_datadir}/openvswitch/scripts/ovs-check-dead-ifs
%{_datadir}/openvswitch/scripts/ovs-lib
%{_datadir}/openvswitch/scripts/ovs-save
%{_datadir}/openvswitch/scripts/ovs-vtep
%{_datadir}/openvswitch/scripts/ovs-ctl
%{_datadir}/openvswitch/scripts/ovs-kmod-ctl
%{_datadir}/openvswitch/scripts/ovs-systemd-reload
%config %{_datadir}/openvswitch/vswitch.ovsschema
%config %{_datadir}/openvswitch/vtep.ovsschema
%{_bindir}/ovs-appctl
%{_bindir}/ovs-docker
%{_bindir}/ovs-dpctl
%{_bindir}/ovs-dpctl-top
%{_bindir}/ovs-ofctl
%{_bindir}/ovs-vsctl
%{_bindir}/ovsdb-client
%{_bindir}/ovsdb-tool
%{_bindir}/ovs-testcontroller
%{_bindir}/ovs-pki
%{_bindir}/vtep-ctl
%{_libdir}/lib*.so.*
%{_sbindir}/ovs-bugtool
%{_sbindir}/ovs-vswitchd
%{_sbindir}/ovsdb-server
%{_mandir}/man1/ovsdb-client.1*
%{_mandir}/man1/ovsdb-server.1*
%{_mandir}/man1/ovsdb-tool.1*
%{_mandir}/man5/ovsdb-server.5*
%{_mandir}/man5/ovs-vswitchd.conf.db.5*
%{_mandir}/man5/ovsdb.5*
%{_mandir}/man5/vtep.5*
%{_mandir}/man7/ovs-actions.7*
%{_mandir}/man7/ovs-fields.7*
%{_mandir}/man7/ovsdb.7*
%{_mandir}/man7/ovsdb-server.7*
%{_mandir}/man8/vtep-ctl.8*
%{_mandir}/man8/ovs-appctl.8*
%{_mandir}/man8/ovs-bugtool.8*
%{_mandir}/man8/ovs-ctl.8*
%{_mandir}/man8/ovs-dpctl.8*
%{_mandir}/man8/ovs-dpctl-top.8*
%{_mandir}/man8/ovs-kmod-ctl.8*
%{_mandir}/man8/ovs-ofctl.8*
%{_mandir}/man8/ovs-pki.8*
%{_mandir}/man8/ovs-vsctl.8*
%{_mandir}/man8/ovs-vswitchd.8*
%{_mandir}/man8/ovs-parse-backtrace.8*
%{_mandir}/man8/ovs-testcontroller.8*
%if %{with dpdk}
%{_prefix}/lib/udev/rules.d/91-vfio.rules
%endif
%doc NOTICE README.rst NEWS rhel/README.RHEL.rst
/var/lib/openvswitch
%attr(750,root,root) /var/log/openvswitch
%ghost %attr(755,root,root) %{_rundir}/openvswitch

%files ipsec
%{_datadir}/openvswitch/scripts/ovs-monitor-ipsec
%{_unitdir}/openvswitch-ipsec.service

%changelog
* Wed Jan 12 2011 Ralf Spenneberg <ralf@os-s.net>
- First build on F14