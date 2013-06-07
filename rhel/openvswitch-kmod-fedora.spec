# Generated automatically -- do not modify!    -*- buffer-read-only: t -*-
# Spec file for Open vSwitch.

# Copyright (C) 2009, 2010 Nicira Networks, Inc.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without warranty of any kind.

#%define kernel 3.1.5-1.fc16.x86_64
#define kernel %{kernel_source}
%{?kversion:%define kernel %kversion}

Name: openvswitch-kmod
Summary: Open vSwitch Kernel Modules
Group: System Environment/Daemons
URL: http://www.openvswitch.org/
Vendor: OpenSource Security Ralf Spenneberg <ralf@os-s.net>
Version: 1.9.2

# The entire source code is ASL 2.0 except datapath/ which is GPLv2
License: GPLv2
Release: 1%{?dist}
Source: openvswitch-%{version}.tar.gz
#Source1: openvswitch-init
Buildroot: /tmp/openvswitch-xen-rpm

%description
Open vSwitch provides standard network bridging functions augmented with
support for the OpenFlow protocol for remote per-flow control of
traffic. This package contains the kernel modules.

%prep
%setup -q -n openvswitch-%{version}

%build
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=%{_localstatedir} --with-linux=/lib/modules/%{kernel}/build --enable-ssl %{build_number}
make %{_smp_mflags} -C datapath/linux

%install
rm -rf $RPM_BUILD_ROOT
make -C datapath/linux modules_install

install -d -m 755 $RPM_BUILD_ROOT/lib/modules/%{kernel}/kernel/extra/openvswitch
find datapath/linux -name *.ko -exec install -m 755  \{\} $RPM_BUILD_ROOT/lib/modules/%{kernel}/kernel/extra/openvswitch \;

%clean
rm -rf $RPM_BUILD_ROOT

%preun

%post
# Ensure that modprobe will find our modules.
depmod %{kernel}

%files
%defattr(-,root,root)
/lib/modules/%{kernel}/kernel/extra/openvswitch/openvswitch.ko
/lib/modules/%{kernel}/kernel/extra/openvswitch/brcompat.ko

%changelog
* Wed Sep 21 2011 Kyle Mestery <kmestery@cisco.com>
- Updated for F15
* Wed Jan 12 2011 Ralf Spenneberg <ralf@os-s.net>
- First build on F14
