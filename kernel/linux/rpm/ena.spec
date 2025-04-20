%define kmod_name		ena
%define kmod_driver_version	2.14.0
%define kmod_rpm_release	1
%define kmod_git_hash		3ac3e0bf079b2c0468f759f2213541e214a6dd77
%define kmod_kbuild_dir		kernel/linux/ena
%define kmod_kernel_version     %{?kernel_version:%{kernel_version}}%{!?kernel_version:%{latest_kernel}}

%{!?dist: %define dist .el6}

Source0:	%{kmod_name}-%{kmod_driver_version}.tar
Source1:	%{kmod_name}.files
Source2:	depmodconf
Source3:	find-requires.ksyms
Source4:	find-provides.ksyms
Source5:	kmodtool
Source6:	symbols.greylist-x86_64
Source7:	preamble

%{!?kmodtool: %define kmodtool %{SOURCE5}}
%define __find_requires %_sourcedir/find-requires.ksyms
%define __find_provides %_sourcedir/find-provides.ksyms %{kmod_name} %{?epoch:%{epoch}:}%{version}-%{release}

Name:		%{kmod_name}
Version:	%{kmod_driver_version}
Release:	%{kmod_rpm_release}%{?dist}.62
Summary:	%{kmod_name} kernel module

Group:		System/Kernel
License:	GPLv2
URL:		https://github.com/amzn/amzn-drivers
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)
BuildRequires:	%kernel_module_package_buildreqs
ExclusiveArch:  x86_64 aarch64


# Build only for standard kernel variant(s); for debug packages, append "debug"
# after "default" (separated by space)
%kernel_module_package -s %{SOURCE5} -f %{SOURCE1} -p %{SOURCE7} default

%description
%{kmod_name} - driver

%prep
%setup

set -- *
mkdir source
mv "$@" source/
cp %{SOURCE6} source/
mkdir obj

%build
echo "Building for kernel: %{kernel_version} flavors: '%{flavors_to_build}'"
echo "Build var: kmodtool = %{kmodtool}"
echo "Build var: kverrel = %{kverrel}"
for flavor in %flavors_to_build; do
	rm -rf obj/$flavor
	cp -r source obj/$flavor

	# update symvers file if existing
	symvers=source/Module.symvers-%{_target_cpu}
	if [ -e $symvers ]; then
		cp $symvers obj/$flavor/%{kmod_kbuild_dir}/Module.symvers
	fi

	cd $PWD/obj/$flavor/%{kmod_kbuild_dir}
	./configure.sh --kernel-dir %{kernel_source $flavor}
	cd -

	make -C %{kernel_source $flavor} M=$PWD/obj/$flavor/%{kmod_kbuild_dir} \
		NOSTDINC_FLAGS="-I $PWD/obj/$flavor/include"

	# mark modules executable so that strip-to-file can strip them
	find obj/$flavor/%{kmod_kbuild_dir} -name "*.ko" -type f -exec chmod u+x '{}' +
done

%{SOURCE2} %{name} %{kmod_kernel_version} obj > source/depmod.conf

greylist=source/symbols.greylist-%{_target_cpu}
if [ -f $greylist ]; then
	cp $greylist source/symbols.greylist
else
	touch source/symbols.greylist
fi

%install
export INSTALL_MOD_PATH=$RPM_BUILD_ROOT
export INSTALL_MOD_DIR=extra/%{name}
for flavor in %flavors_to_build ; do
	make -C %{kernel_source $flavor} modules_install \
		M=$PWD/obj/$flavor/%{kmod_kbuild_dir}
	# Cleanup unnecessary kernel-generated module dependency files.
	find $INSTALL_MOD_PATH/lib/modules -iname 'modules.*' -exec rm {} \;
done

install -m 644 -D source/depmod.conf $RPM_BUILD_ROOT/etc/depmod.d/%{kmod_name}.conf
install -m 644 -D source/symbols.greylist $RPM_BUILD_ROOT/usr/share/doc/kmod-%{kmod_name}/greylist.txt
install -m 644 -D source/%{kmod_kbuild_dir}/README.rst $RPM_BUILD_ROOT/usr/share/doc/kmod-%{kmod_name}/
install -m 644 -D source/%{kmod_kbuild_dir}/COPYING $RPM_BUILD_ROOT/usr/share/doc/kmod-%{kmod_name}/
install -m 644 -D source/%{kmod_kbuild_dir}/RELEASENOTES.md $RPM_BUILD_ROOT/usr/share/doc/kmod-%{kmod_name}/

%clean
rm -rf $RPM_BUILD_ROOT

%changelog
* Tue Apr 15 2025 Ofir Tabachnik ofirt@amazon.com - 2.14.0-1.62
- Update ENA driver to version 2.14.0

* Sun Jan 19 2025 David Arinzon darinzon@amazon.com - 2.13.3-1.61
- Update ENA driver to version 2.13.3

* Thu Dec 26 2024 David Arinzon darinzon@amazon.com - 2.13.2-1.60
- Update ENA driver to version 2.13.2

* Sun Nov 10 2024 David Arinzon darinzon@amazon.com - 2.13.1-1.59
- Update ENA driver to version 2.13.1

* Wed Aug 21 2024 Shay Agroskin shayagr@amazon.com - 2.13.0-1.58
- Update ENA driver to version 2.13.0

* Fri Jun 28 2024 Arthur Kiyanovski akiyano@amazon.com - 2.12.3-1.57
- Update ENA driver to version 2.12.3

* Thu Jun 27 2024 David Arinzon darinzon@amazon.com - 2.12.2-1.56
- Update ENA driver to version 2.12.2

* Wed May 22 2024 David Arinzon darinzon@amazon.com - 2.12.1-1.55
- Update ENA driver to version 2.12.1

* Thu Feb 08 2024 Evgeny Ostrovsky evostrov@amazon.com - 2.12.0-1.54
- Update ENA driver to version 2.12.0

* Wed Feb 07 2024 David Arinzon darinzon@amazon.com - 2.11.1-1.53
- Update ENA driver to version 2.11.1

* Thu Nov 09 2023 David Arinzon darinzon@amazon.com - 2.11.0-1.52
- Update ENA driver to version 2.11.0

* Sun Oct 15 2023 David Arinzon darinzon@amazon.com - 2.10.0-1.51
- Update ENA driver to version 2.10.0

* Mon Sep 4 2023 Osama Abboud osamaabb@amazon.com - 2.9.1-1.50
- Update ENA driver to version 2.9.1

* Tue Aug 9 2023 Osama Abboud osamaabb@amazon.com - 2.9.0-1.49
- Update ENA driver to version 2.9.0

* Sun Aug 6 2023 David Arinzon darinzon@amazon.com - 2.8.9-1.48
- Update ENA driver to version 2.8.9

* Sun Jun 25 2023 Arthur Kiyanovski akiyano@amazon.com - 2.8.8-1.47
- Update ENA driver to version 2.8.8

* Tue May 23 2023 David Arinzon darinzon@amazon.com - 2.8.7-1.46
- Update ENA driver to version 2.8.7

* Tue May 16 2023 Arthur Kiyanovski akiyano@amazon.com - 2.8.6-1.45
- Update ENA driver to version 2.8.6

* Tue Apr 18 2023 David Arinzon darinzon@amazon.com - 2.8.5-1.44
- Update ENA driver to version 2.8.5

* Wed Mar 22 2023 David Arinzon darinzon@amazon.com - 2.8.4-1.43
- Update ENA driver to version 2.8.4

* Mon Jan 30 2023 David Arinzon darinzon@amazon.com - 2.8.3-1.42
- Update ENA driver to version 2.8.3

* Sun Jan 18 2023 David Arinzon darinzon@amazon.com - 2.8.2-1.41
- Update ENA driver to version 2.8.2

* Sun Nov 06 2022 Shahar Itzko itzko@amazon.com - 2.8.1-1.40
- Update ENA driver to version 2.8.1

* Mon Aug 29 2022 Arthur Kiyanovski akiyano@amazon.com - 2.8.0-1.39
- Update ENA driver to version 2.8.0

* Mon Jul 25 2022 David Arinzon darinzon@amazon.com - 2.7.4-1.38
- Update ENA driver to version 2.7.4

* Wed Jun 15 2022 Shay Agroskin shayagr@amazon.com - 2.7.3-1.37
- Update ENA driver to version 2.7.3

* Mon May 16 2022 Shay Agroskin shayagr@amazon.com - 2.7.2-1.36
- Update ENA driver to version 2.7.2

* Mon Apr 04 2022 David Arinzon darinzon@amazon.com - 2.7.1-1.35
- Update ENA driver to version 2.7.1

* Mon Mar 28 2022 Shay Agroskin shayagr@amazon.com - 2.7.0-1.34
- Update ENA driver to version 2.7.0

* Wed Jan 19 2022 Arthur Kiyanovski akiyano@amazon.com - 2.6.1-1.33
- Update ENA driver to version 2.6.1

* Wed Sep 29 2021 Shay Agroskin shayagr@amazon.com - 2.6.0-1.32
- Update ENA driver to version 2.6.0

* Wed Mar 03 2021 Arthur Kiyanovski akiyano@amazon.com - 2.5.0-1.31
- Update ENA driver to version 2.5.0

* Tue Feb 02 2021 Arthur Kiyanovski akiyano@amazon.com - 2.4.1-1.30
- Update ENA driver to version 2.4.1

* Wed Dec 02 2020 Sameeh Jubran sameehj@amazon.com - 2.4.0-1.29
- Update ENA driver to version 2.4.0

* Mon Oct 19 2020 Sameeh Jubran sameehj@amazon.com - 2.3.0-1.28
- Update ENA driver to version 2.3.0

* Thu Jul 23 2020 Shay Agroskin shayagr@amazon.com - 2.2.11-1.27
- Update ENA driver to version 2.2.11

* Sun May 31 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.10-1.26
- Update ENA driver to version 2.2.10

* Wed May 13 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.9-1.25
- Update ENA driver to version 2.2.9

* Sun May 10 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.8-1.24
- Update ENA driver to version 2.2.8

* Wed Apr 15 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.7-1.23
- Update ENA driver to version 2.2.7

* Wed Apr 01 2020 Sameeh Jubran sameehj@amazon.com - 2.2.6-1.22
- Update ENA driver to version 2.2.6

* Thu Mar 19 2020 Sameeh Jubran sameehj@amazon.com - 2.2.5-1.21
- Update ENA driver to version 2.2.5

* Thu Mar 19 2020 Sameeh Jubran sameehj@amazon.com - 2.2.4-1.20
- Update ENA driver to version 2.2.4

* Tue Feb 04 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.3-1.19
- Update ENA driver to version 2.2.3

* Sun Jan 26 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.2-1.18
- Update ENA driver to version 2.2.2

* Fri Jan 17 2020 Arthur Kiyanovski akiyano@amazon.com - 2.2.1-1.17
- Update ENA driver to version 2.2.1

* Sun Dec 22 2019 Arthur Kiyanovski akiyano@amazon.com - 2.2.0-1.16
- Update ENA driver to version 2.2.0

* Thu Dec 12 2019 Arthur Kiyanovski akiyano@amazon.com - 2.1.4-1.15
- Update ENA driver to version 2.1.4

* Wed Oct 23 2019 Arthur Kiyanovski akiyano@amazon.com - 2.1.3-1.14
- Update ENA driver to version 2.1.3

* Tue Jul 16 2019 Arthur Kiyanovski akiyano@amazon.com - 2.1.2-1.13
- Update ENA driver to version 2.1.2

* Thu Jun 13 2019 Sameeh Jubran sameehj@amazon.com - 2.1.1-1.12
- Update ENA driver to version 2.1.1

* Thu Mar 21 2019 Sameeh Jubran sameehj@amazon.com - 2.1.0-1.11
- Update ENA driver to version 2.1.0

* Mon Mar 18 2019 Sameeh Jubran sameehj@amazon.com - 2.0.3-1.10
- Update ENA driver to version 2.0.3

* Sun Oct 28 2018 Arthur Kiyanovski akiyano@amazon.com - 2.0.2-1.9
- Update ENA driver to version 2.0.2

* Sun Sep 09 2018 Netanel Belgazal netanel@amazon.com - 1.6.0-1.8
- Update ENA driver to version 1.6.0

* Tue May 01 2018 Arthur Kiyanovski akiyano@amazon.com - 1.5.3-1.7
- Update ENA driver to version 1.5.3

* Tue Mar 20 2018 Arthur Kiyanovski akiyano@amazon.com - 1.5.2-1.6
- Update ENA driver to version 1.5.2

* Tue Feb 27 2018 Arthur Kiyanovski akiyano@amazon.com - 1.5.0-1.5
- Update ENA driver to version 1.5.1

* Wed Nov 29 2017 Netanel Belgazal netanel@amazon.com - 1.4.0-1.4
- Update ENA driver to version 1.5.0

* Mon Nov 13 2017 Netanel Belgazal netanel@amazon.com - 1.4.0-1.3
- Update ENA driver to version 1.4.0

* Tue Sep 19 2017 Netanel Belgazal netanel@amazon.com - 1.3.0-1.2
- Update ENA driver to version 1.3.0

* Thu Jul 13 2017 Netanel Belgazal netanel@amazon.com - 1.2.0-1.1
- Update ENA driver to version 1.2.0

* Thu Jan 19 2017 Cristian Gafton <gafton@amazon.com> - 1.1.3-1%{?dist}
- initial build for RHEL
