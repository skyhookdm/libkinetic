## Copyright 2020-2021 Seagate Technology LLC.
#
# This Source Code Form is subject to the terms of the Mozilla
# Public License, v. 2.0. If a copy of the MPL was not
# distributed with this file, You can obtain one at
# https://mozilla.org/MP:/2.0/.
#
# This program is distributed in the hope that it will be useful,
# but is provided AS-IS, WITHOUT ANY WARRANTY; including without
# the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
# FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
# License for more details.
#
TLD :=		$(shell pwd)
BUILDDIR =	$(TLD)/build
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib
BUILDBIN =	$(BUILDDIR)/bin

KCTLDIR     =	$(TLD)/toolbox/kctl
BKVDIR      =	$(TLD)/toolbox/bkv
LISTDIR     =	$(TLD)/vendor/list
PROTOBUFDIR =	$(TLD)/vendor/protobuf-c
GTESTDIR    =	$(TLD)/vendor/googletest
SRCDIR      =	$(TLD)/src
TBDIR       =	$(TLD)/toolbox
TESTDIR     =	$(TLD)/tests

LPROTOBUF =	$(BUILDLIB)/libprotobuf-c.so
LLIST     =	$(BUILDLIB)/liblist.a
LKINETIC  =	$(BUILDLIB)/libkinetic.a
LGTEST    =	$(BUILDLIB)/libgtest.a
KCTL	  =	$(BUILDBIN)/kctl

CC      =	gcc
CFLAGS  =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib

DISTFILES = 				\
	./bin/kctl			\
	./include/kinetic		\
	./include/protobuf-c		\
	./lib/libkinetic.a		\
	./lib/libkinetic.so		\
	./lib/libkinetic.so.1		\
	./lib/libkinetic.so.1.0.0	\
	./src				\

all: $(BUILDDIR) $(LPROTOBUF) $(LLIST) $(LKINETIC) $(TBDIR) # $(TESTDIR) $(LGTEST)

test: $(TESTDIR)

dist: all
	@(								\
	cd $(BUILDDIR);							\
	A=`/usr/bin/arch`;						\
	V=`$(KCTL) -V | grep "Library Vers" | awk '{print $$4}'`;	\
	D=libkinetic-dev_$${V}_$${A};					\
	echo Creating Distribution tarfile in $(BUILDDIR)/$${D}.tgz;	\
	tar -czf $${D}.tgz --xform "s,^\.,$${D}," $(DISTFILES)		\
	)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(BUILDINC)
	@mkdir -p $(BUILDLIB)
	@mkdir -p $(BUILDBIN)

$(TBDIR): FORCE
	(cd $@; BUILDDIR=$(BUILDDIR) make -e all install)

$(TESTDIR): FORCE
	(cd $@; BUILDDIR=$(BUILDDIR) make -e all install)

$(LPROTOBUF): 
	(cd $(PROTOBUFDIR); [ ! -f ./configure ] && ./autogen.sh; true)
	(cd $(PROTOBUFDIR); [ ! -f ./Makefile  ] && ./configure --prefix=$(BUILDDIR); true)
	(cd $(PROTOBUFDIR); make install)

$(LLIST): 
	(cd $(LISTDIR); INCDIR=$(BUILDINC) LIBDIR=$(BUILDLIB) make -e)
	(cd $(LISTDIR); INCDIR=$(BUILDINC) LIBDIR=$(BUILDLIB) make -e install)

$(LKINETIC): FORCE
	(cd $(SRCDIR); BUILDDIR=$(BUILDDIR) make -e all install)

$(LGTEST): FORCE
	(cd $(GTESTDIR); BUILDDIR=$(BUILDDIR) bazel build gtest)
	/usr/bin/install -c -m 755 $(GTESTDIR)/bazel-bin/libgtest.a $(BUILDLIB)

# The sanity target only works if you have a kineticd server running locally.
sanity:
	(cd $(KCTLDIR); make all sanity)
	(cd $(BKVDIR);  make all sanity)

clean:	protobufclean listclean kineticclean toolboxclean # gtestclean testclean
	rm -rf $(BUILDDIR)

protobufclean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make clean; true)

kctlclean:
	(cd $(KCTLDIR); make clean)

listclean:
	(cd $(LISTDIR); make clean)

kineticclean:
	(cd $(SRCDIR); make clean)

gtestclean:
	(cd $(GTESTDIR); bazel clean)

toolboxclean:
	(cd $(TBDIR); make clean)

testclean:
	(cd $(TESTDIR); make clean)

distclean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make distclean; true)
	(cd $(LISTDIR); make clean)
	(cd $(SRCDIR); make clean)
	(cd $(GTESTDIR); bazel clean)
	(cd $(TBDIR); make clean)
	(cd $(TESTDIR); make clean)
	rm -rf $(BUILDDIR)

.PHONY: FORCE
FORCE:

