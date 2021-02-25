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
KINETIC =	libkinetic.a
BUILDDIR =	../build
PROTOBUF_C =	kinetic.pb-c.c
PROTOBUF =	kinetic.pb-c.o
PROTODIR =	protocol
KOBJ = 		kinetic.o
OBJS =		ktli.o ktli_socket.o ktli_session.o protocol_interface.o\
		open.o getlog.o get.o put.o del.o range.o batch.o iter.o\
		aio.o util.o validate.o labels.o error.o ktb.o version.o\
		$(PROTOBUF)
GITHASH	=	githash.h
GITHASHMSG =	"/* This file is GENERATED. Do not edit. Must only be included in version.c. */"

INC_KPUB =	kinetic.h kinetic_types.h protocol_types.h

INC_PPUB =	protocol/kinetic.pb-c.h

INC_PRIV =	kio.h ktli.h ktli_session.h  \
		kinetic.h kinetic_internal.h \
		session.h

CFLAGS =	-g -I$(BUILDDIR)/include -I. -Wall
LDFLAGS =	-L$(BUILDDIR)/lib
LDLIBS =	-llist -lprotobuf-c

MKDIR =		/bin/mkdir
INSTALL = 	/usr/bin/install

all:	$(KINETIC)

test:	test/src/test_hmac.c $(OBJS)
	$(MKDIR) -p test/bin
	$(CC) $(CFLAGS) -o test/bin/test_hmac test/src/test_hmac.c $(OBJS) $(LDFLAGS) $(LDLIBS) -lssl -lcrypto -lpthread

install: all
	$(MKDIR) -p $(BUILDDIR)/include/kinetic
	$(MKDIR) -p $(BUILDDIR)/include/kinetic/protocol
	$(MKDIR) -p $(BUILDDIR)/lib
	$(INSTALL) -D -m 644 $(INC_KPUB) $(BUILDDIR)/include/kinetic
	$(INSTALL) -D -m 644 $(INC_PPUB) $(BUILDDIR)/include/kinetic/protocol
	$(INSTALL) -D -m 755 $(KINETIC)  $(BUILDDIR)/lib

$(KINETIC):	$(GITHASH) $(OBJS)
	ld -r -o $(KOBJ) $(OBJS) $(LDFLAGS) $(LDLIBS)
	ar -crs $@ $(KOBJ)

$(GITHASH): FORCE
	@( hash=`git log | head -n 1 | awk '{print $$2}'`;		\
	grep $$hash githash.h >/dev/null 2>&1;				\
	[ $$? \!= 0 ] && 						\
		printf "/* This file is GENERATED. Do not edit. Must only be included in version.c. */\nstatic const char ki_githash[] = \"%s\";\n" $$hash > githash.h;									\
	exit 0;								\
	)

clean:
	rm -rf $(KINETIC) a.out *.o


$(OBJS): $(INC_KPUB) $(INC_PPUB) $(INC_PRIV)

version.o: $(GITHASH)

$(PROTOBUF): $(PROTODIR)/$(PROTOBUF_C)
	$(CC) $(CFLAGS)   -c -o $@ $<

FORCE:
