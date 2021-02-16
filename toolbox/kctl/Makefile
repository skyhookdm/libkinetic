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
OBJS = 		kctl.o util.o info.o get.o put.o del.o \
		range.o batch.o
#	ping.o cluster.o lock.o acl.o  \
#	histogram.o

KCTL=		kctl
CFLAGS =	-g -I$(BUILDDIR)/include -Wall
LDFLAGS =	-L$(BUILDDIR)/lib
LIBRL = 	-Bstatic -lreadline -Bdynamic
LDLIBS = 	$(LIBRL) -lkinetic -lpthread -lssl -lcrypto

all:		$(KCTL)

$(KCTL):	$(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install: all
	/bin/mkdir -p $(BUILDDIR)/bin
	/usr/bin/install -c -m 755 $(KCTL) $(BUILDDIR)/bin

clean:
	rm -rf a.out $(KCTL) *.o

$(OBJS): kctl.h
