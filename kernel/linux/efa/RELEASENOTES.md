# EFA Linux Kernel Driver Release Notes

## Supported Kernel Versions and Distributions
The driver was tested on the following distributions:

**Amazon Linux:**
* Amazon Linux
* Amazon Linux 2

**CentOS:**
* CentOS 7
* CentOS 8

**Red Hat Enterprise Linux:**
* Red Hat Enterprise Linux 7.6
* Red Hat Enterprise Linux 7.7
* Red Hat Enterprise Linux 7.8
* Red Hat Enterprise Linux 8.2
* Red Hat Enterprise Linux 8.3

**Ubuntu:**
* Ubuntu 16.04
* Ubuntu 18.04
* Ubuntu 20.04

**SuSE:**
* SUSE Linux Enterprise 15 SP2
* openSUSE Leap 15.2

## r1.12.3 release notes
* Fix potential NULL pointer dereference when using GDR on newer kernel version

## r1.12.2 release notes
* Fix mmap flow for applications compiled with EXEC permissions
* Couple of packaging fixes

## r1.12.1 release notes
* Limit number of CMake processes to prevent exhaustion of system resources

## r1.12.0 release notes
* Add COPYING file
* Switch to CMake build system
* Align to upstream kernel

## r1.11.1 release notes
* Fix GDR driver packaging issues

## r1.11.0 release notes
* Fix wrong modify QP parameters
* Align to upstream kernel changes

## r1.10.2 release notes
* Fix possible use of uninitialized variable in GDR error flow

## r1.10.1 release notes
* Misc fixes to GDR package installation
* Expose messages and RDMA read statistics
* Fix an error when registering MR on older kernels

## r1.10.0 release notes
* SRD RNR retry support
* Remove a wrong warning triggered by GDR cleanup
* Fix GDR driver compilation on Ubuntu 16.04
* Add GDR driver packaging (rpm/deb)

## r1.9.0 release notes
* Adapt to upstream kernel
* Refactor locking scheme in GDR flows
* Report create CQ error counter
* Report mmap error counter
* Report admin commands error counter
* Add a sysfs indication to GDR drivers
* Add 0xefa1 device support

## r1.6.0 release notes
* Add NVIDIA GPUDirect RDMA support
* Add a configure script to the compilation process and use it to test for kernel functionality
* Change directory structure, the source files are now located under src/
* Fix compilation on certain kernels of SuSE15.1
* Backport changes from upstream kernel

## r1.5.1 release notes
* Fix SuSE ioctl flow backport

## r1.5.0 release notes
* RDMA read support
* Make ib_uverbs a soft dependency
* Fix ioctl flows on older kernels
* SuSE 15.1 support

## r1.4.1 release notes
* Fix Incorrect error print
* Add support for CentOS 7.7

## r1.4.0 release notes
* Expose device statistics
* Rate limit admin queue error prints
* Properly assign err variable on everbs device creation failure

## r1.3.1 release notes

* Fix build issue in debian/rules file
* Fix kcompat issue (usage before include)

## r1.3.0 release notes

* Align to the driver that was merged upstream
* Fix a bug where failed functions would return success return value
* Fix modify QP udata check backport
* Fix locking issues in mmap flow
* Add Debian packaging files

## r0.9.2 release notes

* Bug fix module load issue

## r0.9.1 release notes

* Bug fix in EFA spec file
* Upstream review cleanups

## r0.9.0 release notes

Initial commit
