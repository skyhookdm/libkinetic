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
SRCDIR      =	$(TLD)/src
TBDIR       =	$(TLD)/toolbox

LPROTOBUF =	$(BUILDLIB)/libprotobuf-c.so
LLIST     =	$(BUILDLIB)/liblist.a
LKINETIC  =	$(BUILDLIB)/libkinetic.a
KCTL	  =	$(BUILDBIN)/kctl

# These are being deprecated for now
# TESTDIR   =	$(TLD)/tests
# GTESTDIR  =	$(TLD)/vendor/googletest
# LGTEST    =	$(BUILDLIB)/libgtest.a

CC      =	gcc
CFLAGS  =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib

DISTFILES = 				\
	./bin/kctl			\
	./bin/bkv			\
	./include/kinetic		\
	./include/protobuf-c		\
	./lib/libkinetic.a		\
	./lib/libkinetic.so		\
	./lib/libkinetic.so.1		\
	./lib/libkinetic.so.1.0.0	\
	./lib/libprotobuf-c.a		\
	./lib/libprotobuf-c.so.1	\
	./lib/libprotobuf-c.so.1.0.0	\
	./src				\

all: $(BUILDDIR) $(LPROTOBUF) $(LLIST) $(LKINETIC) $(TBDIR) # $(TESTDIR) $(LGTEST)

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

$(LPROTOBUF): 
	(cd $(PROTOBUFDIR); [ ! -f ./configure ] && ./autogen.sh; true)
	(cd $(PROTOBUFDIR); [ ! -f ./Makefile  ] && ./configure --prefix=$(BUILDDIR); true)
	(cd $(PROTOBUFDIR); make install)

$(LLIST): 
	(cd $(LISTDIR); INCDIR=$(BUILDINC) LIBDIR=$(BUILDLIB) make -e)
	(cd $(LISTDIR); INCDIR=$(BUILDINC) LIBDIR=$(BUILDLIB) make -e install)

$(LKINETIC): FORCE
	(cd $(SRCDIR); BUILDDIR=$(BUILDDIR) make -e all install)

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

toolboxclean:
	(cd $(TBDIR); make clean)

distclean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make distclean; true)
	(cd $(LISTDIR); make clean)
	(cd $(SRCDIR); make clean)
	(cd $(TBDIR); make clean)
	# (cd $(TESTDIR); make clean)
	# (cd $(GTESTDIR); bazel clean)
	rm -rf $(BUILDDIR)

.PHONY: FORCE
FORCE:


# ------------------------------
# Deprecated portions, here for reference (and to make bringing it back in the future easier)

# We are deprecating googletest unit tests for now
# $(LGTEST): FORCE
# 	(cd $(GTESTDIR); BUILDDIR=$(BUILDDIR) bazel build gtest)
# 	/usr/bin/install -c -m 755 $(GTESTDIR)/bazel-bin/libgtest.a $(BUILDLIB)

# gtestclean:
# 	(cd $(GTESTDIR); bazel clean)

# Deprecating the test directory, which will only contain google tests
# test: $(TESTDIR)

# $(TESTDIR): FORCE
# 	(cd $@; BUILDDIR=$(BUILDDIR) make -e all install)

# testclean:
# 	(cd $(TESTDIR); make clean)
