# Copyright 2019-2025 Amazon.com, Inc. or its affiliates. All rights reserved

%define name			efa
%define driver_name		efa
%define debug_package		%{nil}

Name:		%{name}
Version:	%{driver_version}
Release:	1%{?dist}
Summary:	%{name} kernel module

Group:		System/Kernel
License:	Dual BSD/GPL
URL:		https://github.com/amzn/amzn-drivers/
Source0:	%{name}-%{version}.tar

Requires:	dkms %kernel_module_package_buildreqs cmake
# RHEL 8.4 has a broken dependency between cmake and libarchive which
# causes libarchive to not be updated properly in the update case. Express the
# dependency so that our install does not break.
%if 0%{?rhel} >= 8
Requires: libarchive >= 3.3.3
%endif

%if 0%{?amzn} == 2
Requires: cmake3
%endif

# Replace the compile time dependency efa-gdr package
Obsoletes:	efa-gdr

%define install_path /usr/src/%{driver_name}-%{version}

%description
%{name} kernel module source and DKMS scripts to build the kernel module.

%prep
%setup -n %{name}-%{version} -q

%post
cd %{install_path}
dkms add -m %{driver_name} -v %{driver_version}
for kernel in $(/bin/ls /lib/modules); do
	if [ -e /lib/modules/$kernel/build/include ]; then
		dkms build -m %{driver_name} -v %{driver_version} -k $kernel
		dkms install --force -m %{driver_name} -v %{driver_version} -k $kernel
	fi
done

%preun
dkms remove -m %{driver_name} -v %{driver_version} --all

%build

%install
cd kernel/linux/efa
mkdir -p %{buildroot}%{install_path}
mkdir -p %{buildroot}%{install_path}/config
mkdir -p %{buildroot}%{install_path}/src
install -D -m 644 conf/efa.conf		%{buildroot}/etc/modules-load.d/efa.conf
install -D -m 644 conf/efa-modprobe.conf	%{buildroot}/etc/modprobe.d/efa.conf
install -m 644 conf/dkms.conf		%{buildroot}%{install_path}
install -m 744 conf/configure-dkms.sh	%{buildroot}%{install_path}
install -m 644 CMakeLists.txt		%{buildroot}%{install_path}
install -m 644 README			%{buildroot}%{install_path}
install -m 644 RELEASENOTES.md		%{buildroot}%{install_path}
install -m 644 config/Makefile		%{buildroot}%{install_path}/config
install -m 644 config/main.c.in		%{buildroot}%{install_path}/config
install -m 744 config/compile_conftest.sh	%{buildroot}%{install_path}/config
install -m 644 config/efa.cmake		%{buildroot}%{install_path}/config
install -m 744 config/runbg.sh		%{buildroot}%{install_path}/config
install -m 744 config/wait_for_pid.sh	%{buildroot}%{install_path}/config
cd src
install -m 644 efa_com.c		%{buildroot}%{install_path}/src
install -m 644 efa_com_cmd.c		%{buildroot}%{install_path}/src
install -m 644 efa_main.c		%{buildroot}%{install_path}/src
install -m 644 efa_sysfs.c		%{buildroot}%{install_path}/src
install -m 644 efa_verbs.c		%{buildroot}%{install_path}/src
install -m 644 efa_data_verbs.c		%{buildroot}%{install_path}/src
install -m 644 efa-abi.h 		%{buildroot}%{install_path}/src
install -m 644 efa_admin_cmds_defs.h 	%{buildroot}%{install_path}/src
install -m 644 efa_admin_defs.h 	%{buildroot}%{install_path}/src
install -m 644 efa_com_cmd.h		%{buildroot}%{install_path}/src
install -m 644 efa_com.h		%{buildroot}%{install_path}/src
install -m 644 efa_common_defs.h	%{buildroot}%{install_path}/src
install -m 644 efa_io_defs.h		%{buildroot}%{install_path}/src
install -m 644 efa.h			%{buildroot}%{install_path}/src
install -m 644 efa_verbs.h		%{buildroot}%{install_path}/src
install -m 644 efa_regs_defs.h		%{buildroot}%{install_path}/src
install -m 644 efa_sysfs.h		%{buildroot}%{install_path}/src
install -m 644 kcompat.h		%{buildroot}%{install_path}/src
install -m 644 CMakeLists.txt		%{buildroot}%{install_path}/src
install -m 644 Kbuild.in		%{buildroot}%{install_path}/src
install -m 644 efa_p2p.c		%{buildroot}%{install_path}/src
install -m 644 efa_p2p.h		%{buildroot}%{install_path}/src
install -m 644 efa_gdr.c		%{buildroot}%{install_path}/src
install -m 644 nv-p2p.h			%{buildroot}%{install_path}/src
install -m 644 efa_neuron.c		%{buildroot}%{install_path}/src
install -m 644 neuron_p2p.h		%{buildroot}%{install_path}/src

%files
%{install_path}
/etc/modules-load.d/efa.conf
/etc/modprobe.d/efa.conf

%changelog
* Tue Jul 15 2025 Yonatan Nachum <ynachum@amazon.com> - 2.17.0
- Add Network HW statistics counters
- Add CQ with external memory support

* Wed Jun 04 2025 Michael Margolin <mrgolin@amazon.com> - 2.15.3
- Fix cmake dependency on Amazon Linux 2

* Tue Jun 03 2025 Yonatan Nachum <ynachum@amazon.com> - 2.15.2
- Fix cmake 4.0 compatibility failure

* Mon May 26 2025 Yonatan Nachum <ynachum@amazon.com> - 2.15.1
- Cleanup destroy CQ kernel compatibility
- Remove backports for kernels older than 4.14
- Fix support for RHEL 9.6 compatibility

* Mon Mar 03 2025 Michael Margolin <mrgolin@amazon.com> - 2.15.0
- Fix page size optimization for large physically contiguous MRs
- Cleanup interrupt related code
- Reset the device if driver initialization failed

* Sun Jan 12 2025 Michael Margolin <mrgolin@amazon.com> - 2.13.1
- Adjust dmabuf MR registration interface for mainline 6.12 kernels

* Wed Oct 30 2024 Michael Margolin <mrgolin@amazon.com> - 2.13.0
- Add an option to create QP with specific service level
- Report link speed according to device parameters

* Sun Oct 06 2024 Michael Margolin <mrgolin@amazon.com> - 2.12.1
- Fix RNR configuration for SRD kernel QPs

* Tue Oct 01 2024 Michael Margolin <mrgolin@amazon.com> - 2.12.0
- Introduce EFA kernel verbs support
- Add 0xefa3 device support
- Report device node GUID
- Adjust CQ creation interface for mainline 6.11 kernels

* Wed Jun 05 2024 Michael Margolin <mrgolin@amazon.com> - 2.10.0
- Introduce QP with unsolicited write with immediate receive
- Add gracefull shutdown
- Limit EQs to available MSI-X vectors
- Improve admin completions error handling
- Improve error handling on missing BARs

* Thu Feb 15 2024 Michael Margolin <mrgolin@amazon.com> - 2.8.0
- Introduce Query MR support
- Expose underlying interconnects used to reach memory regions
- Fix compilation issues for mainline 6.8 kernels

* Thu Nov 09 2023 Michael Margolin <mrgolin@amazon.com> - 2.6.0
- Fix wrong resources destruction order
- Enable Nvidia GDR using P2P on up-to-date kernels
- Expose accelerator memory P2P provider in sysfs

* Tue Jul 11 2023 Michael Margolin <mrgolin@amazon.com> - 2.5.0
- Add RDMA write statistics

* Thu Jun 01 2023 Michael Margolin <mrgolin@amazon.com> - 2.4.1
- Fix memory registration for systems with PAGE_SIZE > 4K

* Thu May 04 2023 Michael Margolin <mrgolin@amazon.com> - 2.4.0
- Add data polling support
- Add RDMA write support

* Tue Nov 08 2022 Michael Margolin <mrgolin@amazon.com> - 2.1.1
- Fix dmabuf backport for some kernels

* Thu Oct 27 2022 Michael Margolin <mrgolin@amazon.com> - 2.1.0
- Add support for CQ receive entries with source GID
- Add 0xefa2 device support
- Fix sysfs show on older kernels

* Mon Jan 24 2022 Firas Jahjah <firasj@amazon.com> - 1.16.0
- Add CQ notifications
- Add support for dmabuf
- Add NeuronLink RDMA support
- Remove gdr from module info keys

* Sun Oct 17 2021 Firas Jahjah <firasj@amazon.com> - 1.14.2
- Various GDR fixes

* Thu Sep 23 2021 Firas Jahjah <firasj@amazon.com> - 1.14.1
- Make gdr sysfs return zero in case nvidia symbols are not available

* Tue Sep 14 2021 Gal Pressman <galpress@amazon.com> - 1.14.0
- Unify the standard and GDR packages
- Split hardware stats to device and port stats
- Fix unfree'd IRQ vectors on error flow
- Fix potential deadlock in GDR flow

* Tue Jul 06 2021 Gal Pressman <galpress@amazon.com> - 1.13.0
- Remove static dependency of nvidia module in GDR driver
- Fix potential memory leak in GDR memory registration error flow
- Upstream kernel alignments
- Remove old kernel APIs

* Sun Jun 27 2021 Gal Pressman <galpress@amazon.com> - 1.12.3
- Fix potential NULL pointer dereference when using GDR on newer kernel version

* Wed Jun 16 2021 Gal Pressman <galpress@amazon.com> - 1.12.2
- Fix mmap flow for applications compiled with EXEC permissions
- Couple of packaging fixes

* Mon May 10 2021 Gal Pressman <galpress@amazon.com> - 1.12.1
- Limit number of CMake processes to prevent exhaustion of system resources

* Mon Mar 29 2021 Gal Pressman <galpress@amazon.com> - 1.12.0
- Add COPYING file
- Switch to CMake build system
- Align to upstream kernel

* Thu Jan 28 2021 Gal Pressman <galpress@amazon.com> - 1.11.1
- Fix GDR driver packaging issues

* Sun Dec 06 2020 Gal Pressman <galpress@amazon.com> - 1.11.0
- Fix wrong modify QP parameters
- Align to upstream kernel changes

* Sun Oct 11 2020 Gal Pressman <galpress@amazon.com> - 1.10.2
- Fix possible use of uninitialized variable in GDR error flow

* Wed Sep 30 2020 Gal Pressman <galpress@amazon.com> - 1.10.1
- Misc fixes to GDR package installation
- Expose messages and RDMA read statistics
- Fix an error when registering MR on older kernels

* Wed Sep 09 2020 Gal Pressman <galpress@amazon.com> - 1.10.0
- SRD RNR retry support
- Remove a wrong warning triggered by GDR cleanup
- Fix GDR driver compilation on Ubuntu 16.04
- Add GDR driver packaging (rpm/deb)

* Mon Aug 03 2020 Gal Pressman <galpress@amazon.com> - 1.9.0
- Adapt to upstream kernel
- Refactor locking scheme in GDR flows
- Report create CQ error counter
- Report mmap error counter
- Report admin commands error counter
- Add a sysfs indication to GDR drivers
- Add 0xefa1 device support

* Wed Feb 26 2020 Gal Pressman <galpress@amazon.com> - 1.6.0
- Add NVIDIA GPUDirect RDMA support
- Add a configure script to the compilation process and use it to test for kernel funcionality
- Change directory structure, the source files are now located under src/
- Fix compilation on certain kernels of SuSE15.1
- Backport changes from upstream kernel

* Thu Jan 02 2020 Gal Pressman <galpress@amazon.com> - 1.5.1
- Fix SuSE ioctl flow backport

* Wed Dec 11 2019 Gal Pressman <galpress@amazon.com> - 1.5.0
- RDMA read support
- Make ib_uverbs a soft dependency
- Fix ioctl flows on older kernels
- SuSE 15.1 support

* Fri Sep 20 2019 Gal Pressman <galpress@amazon.com> - 1.4.1
- Fix Incorrect error print
- Add support for CentOS 7.7

* Thu Sep 5 2019 Gal Pressman <galpress@amazon.com> - 1.4.0
- Expose device statistics
- Rate limit admin queue error prints
- Properly assign err variable on everbs device creation failure

* Thu Aug 8 2019 Gal Pressman <galpress@amazon.com> - 1.3.1
- Fix build issue in debian/rules file
- Fix kcompat issue (usage before include)

* Sun Jul 7 2019 Gal Pressman <galpress@amazon.com> - 1.3.0
- Align to the driver that was merged upstream
- Fix a bug where failed functions would return success return value
- Fix modify QP udata check backport
- Fix locking issues in mmap flow
- Add Debian packaging files

* Tue May 7 2019 Jie Zhang <zhngaj@amazon.com> - 0.9.2
- Add a separate configuration file to load ib_uverbs as a soft dependency module
  on non-systemd based systems

* Tue Apr 2 2019 Robert Wespetal <wesper@amazon.com> - 0.9.1
- Update EFA post install script to install module for all kernels

* Fri Mar 8 2019 Robert Wespetal <wesper@amazon.com> - 0.9.0
- initial build for RHEL
