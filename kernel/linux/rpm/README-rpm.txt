This directory includes a Makefile and scripts necessary to create a
source rpm (src.rpm) kmod package for installing the Amazon Elastic
Adapter kernel driver on a Red Hat Enterprise Linux (RHEL) compatible
distribution. Currently this script supports building source rpms
which build and install on RHEL versions 7.x, 8.x and 9.x.


PRE-REQUISITES

In order for this script to work correctly, you need to have the
following pre-requisites installed on your RHEL build environment:

1. kernel-devel and kernel-headers packages which match the kernel you
   intend to run this kmod on. If the kernel version matches the
   currently running kernel version (or you are building the kmod for
   the currently running kernel) then you can install the required
   dependencies by using the following command line:

   bash$ sudo yum -y install kernel-{devel,headers}-$(uname -r)


2. A minimally working build environment. At a minimum, you will need
   the rpm-build, make and gcc packages installed:


   bash$ sudo yum -y install gcc make rpm-build

   for RHEL 8.x also install:
   bash$ sudo dnf install kernel-abi-whitelists kernel-rpm-macros

   for RHEL 9.x also install:
   bash$ sudo dnf install kernel-abi-stablelists kernel-rpm-macros

BUILDING A SOURCE RPM

Once the pre-requisites have been installed, you can simply issue a
"make" in this directory to build the kmod src.rpm package:

bash$ make
cd .. && git archive --format=tar --prefix=ena-2.14.0/ -o rpm/ena-2.14.0.tar ena_linux_2.14.0
rpmbuild -bs \
            --define '_topdir %(pwd)' --define '_ntopdir %(pwd)' \
            --define '_builddir  %{_ntopdir}' \
            --define '_sourcedir %{_ntopdir}' \
            --define '_specdir   %{_ntopdir}' \
            --define '_rpmdir    %{_ntopdir}' \
            --define '_srcrpmdir %{_ntopdir}' \
        ena.spec
Wrote: /home/ec2-user/amzn-drivers/rpm/ena-2.14.0-1.el7.3.src.rpm
bash$ _


COMPILING AND INSTALLING

Once the src.rpm has been created, you can build binary packages for
the installed kernel-devel and kernel-headers environments:

bash$ rpmbuild --rebuild ena-2.14.0-1.el7.3.src.rpm
[....]
Wrote: /home/ec2-user/rpmbuild/RPMS/x86_64/kmod-ena-2.14.0-1.el7.3.x86_64.rpm
Wrote: /home/ec2-user/rpmbuild/RPMS/x86_64/ena-debuginfo-2.14.0-1.el7.3.x86_64.rpm
[...]
bash$ _

Now you should be able to install/deploy the resulting binary rpm
packages using your preferred rpm install too. For example, using yum:

bash$ sudo yum -y localinstall
/home/ec2-user/rpmbuild/RPMS/x86_64/kmod-ena-2.14.0-1.el7.3.x86_64.rpm

