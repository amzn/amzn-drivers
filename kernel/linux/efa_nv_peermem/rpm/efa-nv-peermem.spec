# Copyright 2023-2025 Amazon.com, Inc. or its affiliates. All rights reserved

%define name			efa-nv-peermem
%define driver_name		efa_nv_peermem
%define debug_package		%{nil}

Name:		%{name}
Version:	%{driver_version}
Release:	1%{?dist}
Summary:	%{driver_name} kernel module

Group:		System/Kernel
License:	Dual BSD/GPL
URL:		https://github.com/amzn/amzn-drivers/
Source0:	%{name}-%{version}.tar

Requires:	dkms %kernel_module_package_buildreqs cmake
%if %{defined kernel_module_package_buildreqs}
Requires: %kernel_module_package_buildreqs
%endif
# RHEL 8.4 has a broken dependency between cmake and libarchive which
# causes libarchive to not be updated properly in the update case. Express the
# dependency so that our install does not break.
%if 0%{?rhel} >= 8
Requires: libarchive >= 3.3.3
%endif

%if 0%{?amzn} == 2
Requires: cmake3
%endif

%define install_path /usr/src/%{name}-%{version}

%description
%{driver_name} kernel module source and DKMS scripts to build the kernel module.

%prep
%setup -n %{name}-%{version} -q

%post
cd %{install_path}
dkms add -m %{name} -v %{driver_version}
for kernel in $(/bin/ls /lib/modules); do
	if [ -e /lib/modules/$kernel/build/include ]; then
		dkms build -m %{name} -v %{driver_version} -k $kernel
		dkms install --force -m %{name} -v %{driver_version} -k $kernel
	fi
done

%preun
dkms remove -m %{name} -v %{driver_version} --all

%build

%install
cd kernel/linux/efa_nv_peermem
mkdir -p %{buildroot}%{install_path}
mkdir -p %{buildroot}%{install_path}/config
mkdir -p %{buildroot}%{install_path}/src
install -D -m 644 conf/efa_nv_peermem.conf		%{buildroot}/etc/modules-load.d/efa_nv_peermem.conf
install -m 644 conf/dkms.conf				%{buildroot}%{install_path}
install -m 744 conf/configure-dkms.sh			%{buildroot}%{install_path}
install -m 644 CMakeLists.txt				%{buildroot}%{install_path}
install -m 644 README					%{buildroot}%{install_path}
install -m 644 RELEASENOTES.md				%{buildroot}%{install_path}
cd src
install -m 644 efa_nv_peermem_main.c			%{buildroot}%{install_path}/src
install -m 644 efa_nv_peermem.h				%{buildroot}%{install_path}/src
install -m 644 nv-p2p.h					%{buildroot}%{install_path}/src
install -m 644 CMakeLists.txt				%{buildroot}%{install_path}/src
install -m 644 Kbuild.in				%{buildroot}%{install_path}/src

%files
%{install_path}
/etc/modules-load.d/efa_nv_peermem.conf

%changelog
* Tue Jun 03 2025 Yonatan Nachum <ynachum@amazon.com> - 1.2.0
- Fix cmake 4.0 compatibility failure
- Add interface definitions header

* Thu Feb 15 2024 Michael Margolin <mrgolin@amazon.com> - 1.1.1
- Reduce build process output to stdout

* Wed Oct 25 2023 Michael Margolin <mrgolin@amazon.com> - 1.1.0
- initial build for RHEL
