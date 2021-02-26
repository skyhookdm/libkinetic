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
BUILDDIR =	../../build
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib
BUILDBIN =	$(BUILDDIR)/bin

SRCS = 		kctl.c util.c info.c get.c put.c del.c	\
		range.c batch.c
OBJS =		$(SRCS:.c=.o)
DEPS = 		$(SRCS:.c=.d)

#	ping.o cluster.o lock.o acl.o  \
#	histogram.o

KCTLDEPS =	$(BUILDLIB)/libkinetic.a

KCTL=		kctl
CPPFLAGS = 	-I$(BUILDINC)
CFLAGS =	-g -Wall
LDFLAGS =	-L$(BUILDLIB)
STATIC = 	-Wl,-Bstatic
DYNAMIC =	-Wl,-Bdynamic
LDLIBS = 	-lreadline 				\
		$(STATIC) -lkinetic 			\
		$(DYNAMIC) -lpthread -lssl -lcrypto
SANITY =	Sanity.kctl.out

all:		$(KCTL)

$(KCTL):	$(OBJS) $(KCTLDEPS)
		$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install: all
	/bin/mkdir -p $(BUILDDIR)/bin
	/usr/bin/install -c -m 755 $(KCTL) $(BUILDDIR)/bin

clean:
	rm -rf a.out $(KCTL) *.[od] Sanity.kctl.out

clobber: clean
	rm -rf *.d

sanity:
	stdbuf -oL -eL ./Sanity.kctl  > ./Sanity.kctl.out  2>&1
	cmp ./Sanity.kctl.out ./Sanity.kctl.verified

$(OBJS) $(DEPS):

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(SRCS:.c=.d) 
