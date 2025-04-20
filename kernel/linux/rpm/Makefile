# Makefile for creating a source rpm of the Amazon ENA driver
#
# Author: Cristian Gafton <gafton@amazon.com>

NAME	= ena
VERSION	= 2.14.0

TOPDIR	:= $(shell git rev-parse --show-toplevel)

TAG	= $(NAME)_linux_$(VERSION)
TARBALL	= $(NAME)-$(VERSION).tar

RPMDEFS = --define '_topdir  %(pwd)' \
	  --define '_ntopdir %(pwd)' \
	  --define '_builddir  %{_ntopdir}/build' \
	  --define '_buildrootdir %{_builddir}' \
	  --define '_sourcedir %{_ntopdir}' \
	  --define '_specdir   %{_ntopdir}' \
	  --define '_rpmdir    %{_ntopdir}' \
	  --define '_srcrpmdir %{_ntopdir}'

all : srpm

tarball : $(TARBALL)
$(TARBALL) : always
	(cd $(TOPDIR) && \
		git archive --format=tar --prefix=$(NAME)-$(VERSION)/ $(TAG) \
		kernel/linux/{common,ena}) > $@

srpm : $(TARBALL) Makefile
	rpmbuild -bs $(RPMDEFS) $(wildcard *.spec)

rpm : $(TARBALL) Makefile
	rpmbuild -ba $(RPMDEFS) $(wildcard *.spec)

clean :
	rm -f $(TARBALL) *.src.rpm

always :

.PHONY : srpm clean always

