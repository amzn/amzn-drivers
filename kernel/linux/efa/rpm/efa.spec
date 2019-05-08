# Copyright 2019 Amazon.com, Inc. or its affiliates. All rights reserved

%define name			efa
%define debug_package		%{nil}

Name:		%{name}
Version:	%{driver_version}
Release:	1%{?dist}
Summary:	%{name} kernel module

Group:		System/Kernel
License:	Dual BSD/GPL
URL:		https://github.com/amzn/amzn-drivers/
Source0:	%{name}-%{version}.tar

Requires:	dkms %kernel_module_package_buildreqs

%define install_path /usr/src/%{name}-%{version}

%description
%{name} kernel module source and DKMS scripts to build the kernel module.

%prep
%setup -n %{name}-%{version} -q

%post
dkms add -m %{name} -v %{driver_version}
for kernel in $(/bin/ls /lib/modules); do
	dkms build -m %{name} -v %{driver_version} -k $kernel
	dkms install -m %{name} -v %{driver_version} -k $kernel
done

%preun
dkms remove -m %{name} -v %{driver_version} --all

%build

%install
cd kernel/linux/efa
mkdir -p %{buildroot}%{install_path}
install -D -m 644 conf/efa.conf		%{buildroot}/etc/modules-load.d/efa.conf
install -D -m 644 conf/efa-modprobe.conf	%{buildroot}/etc/modprobe.d/efa.conf
install -m 644 conf/dkms.conf		%{buildroot}%{install_path}
install -m 644 efa_com.c		%{buildroot}%{install_path}
install -m 644 efa_com_cmd.c		%{buildroot}%{install_path}
install -m 644 efa_main.c		%{buildroot}%{install_path}
install -m 644 efa_sysfs.c		%{buildroot}%{install_path}
install -m 644 efa_verbs.c		%{buildroot}%{install_path}
install -m 644 efa-abi.h 		%{buildroot}%{install_path}
install -m 644 efa_admin_cmds_defs.h 	%{buildroot}%{install_path}
install -m 644 efa_admin_defs.h 	%{buildroot}%{install_path}
install -m 644 efa_com_cmd.h		%{buildroot}%{install_path}
install -m 644 efa_com.h		%{buildroot}%{install_path}
install -m 644 efa_common_defs.h	%{buildroot}%{install_path}
install -m 644 efa.h			%{buildroot}%{install_path}
install -m 644 efa_regs_defs.h		%{buildroot}%{install_path}
install -m 644 efa_sysfs.h		%{buildroot}%{install_path}
install -m 644 kcompat.h		%{buildroot}%{install_path}
install -m 644 Makefile			%{buildroot}%{install_path}
install -m 644 README			%{buildroot}%{install_path}
install -m 644 RELEASENOTES.md		%{buildroot}%{install_path}

%files
%{install_path}
/etc/modules-load.d/efa.conf
/etc/modprobe.d/efa.conf

%changelog
* Tue May 7 2019 Jie Zhang <zhngaj@amazon.com> - 0.9.2
- Add a separate configuration file to load ib_uverbs as a soft dependency module
  on non-systemd based systems

* Tue Apr 2 2019 Robert Wespetal <wesper@amazon.com> - 0.9.1
- Update EFA post install script to install module for all kernels

* Fri Mar 8 2019 Robert Wespetal <wesper@amazon.com> - 0.9.0
- initial build for RHEL
