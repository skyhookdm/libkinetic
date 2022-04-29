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
# IBUILDDIR is necessary for the install target
# as you can override BUILDDIR with you env
IBUILDDIR =	../../build
BUILDDIR =	$(IBUILDDIR)
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib
BUILDBIN =	$(BUILDDIR)/bin
BUILDSRC =	$(BUILDDIR)/src
BUILDMAN =	$(BUILDDIR)/man

SRCS = 		kctl.c util.c info.c get.c put.c del.c	\
		range.c batch.c stats.c ping.c flush.c	\
		exec.c device.c pin.c acl.c mjson.c	\
		upgrade.c cluster.c
HDRS =		kctl.h
OBJS =		$(SRCS:.c=.o)
DEPS = 		$(SRCS:.c=.d)

#	cluster.o lock.o acl.o  \
#	histogram.o

KCTLDEPS =	$(BUILDLIB)/libkinetic.a

KCTL=		kctl
CPPFLAGS = 	-I$(BUILDINC)
CFLAGS =	-g -Wall
LDFLAGS =	-L$(BUILDLIB)
STATIC = 	-Wl,-Bstatic
DYNAMIC =	-Wl,-Bdynamic

# By default this make statically links against libkinetic
# removing the STATIC var below permits dynamic linking, but
# the shared libs will need to be installed or LD_LIBRARY_PATH
# used to locate the shared libs
LDLIBS = 	$(STATIC) -lreadline			\
		-lkinetic 				\
		$(DYNAMIC) -lcurses -lpthread -lssl -lcrypto -lm

SANITY =	Sanity.kctl
SANITYO =	Sanity.kctl.out
SANITYV =	Sanity.kctl.verified

all:		$(KCTL)

$(KCTL):	$(OBJS) $(KCTLDEPS)
		$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install: $(KCTL) $(SRCS) $(HDRS) $(SANITY) $(SANITYV) Makefile
	mkdir -p $(BUILDBIN)
	mkdir -p $(BUILDSRC)/kctl
	/usr/bin/install -c -m 755  $(KCTL) $(BUILDBIN)/kctl
	for f in $(SRCS) $(HDRS); do					\
		/usr/bin/install -c -m 644 $${f} $(BUILDSRC)/kctl;	\
	done
	sed 's?$(IBUILDDIR)?../..?g' Makefile > $(BUILDSRC)/kctl/Makefile
	/usr/bin/install -c -m 755  $(SANITY) $(BUILDSRC)/kctl/$(SANITY)
	/usr/bin/install -c -m 644  $(SANITYV) $(BUILDSRC)/kctl/$(SANITYV)
	touch install

clean:
	rm -rf a.out $(KCTL) *.[od] $(SANITYO) install

clobber: clean
	rm -rf *.d

# The sanity target only works if you have a kineticd server
# running locally. The stdbuf utility is used to interleave
# stdout and stderr to get a consistent comparable output file.
sanity:
	stdbuf -oL -eL ./$(SANITY) > $(SANITYO) 2>&1
	cmp $(SANITYO) $(SANITYV)
	@echo "*** KCTL Sanity Success"

$(OBJS) $(DEPS):

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(SRCS:.c=.d) 
