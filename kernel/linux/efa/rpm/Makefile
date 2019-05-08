# Makefile for creating rpm of the Amazon EFA driver

NAME	= efa
VERSION	= 0.9.2

TOPDIR  := $(shell git rev-parse --show-toplevel)
TAG	= master
TARBALL	= $(NAME)-$(VERSION).tar

RPMDEFS = --define '_topdir  %(pwd)' \
          --define '_ntopdir %(pwd)' \
          --define '_builddir  %{_ntopdir}/build' \
          --define '_buildrootdir %{_builddir}' \
          --define '_sourcedir %{_ntopdir}' \
          --define '_specdir   %{_ntopdir}' \
          --define '_rpmdir    %{_ntopdir}' \
          --define '_srcrpmdir %{_ntopdir}' \
	  --define "driver_version $(VERSION)"

all : rpm

tarball : $(TARBALL)
$(TARBALL) : always
	(cd $(TOPDIR) && \
	git archive --format=tar --prefix=$(NAME)-$(VERSION)/ $(TAG) \
	kernel/linux/efa/) > $@

srpm : $(TARBALL) Makefile
	rpmbuild -bs $(RPMDEFS) efa.spec

rpm : $(TARBALL) Makefile
	rpmbuild -ba $(RPMDEFS) efa.spec

clean :
	rm -f $(TARBALL) *.src.rpm

always:

.PHONY : srpm clean always
