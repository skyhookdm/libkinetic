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
#
BUILDDIR =	../build
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib
BUILDBIN =	$(BUILDDIR)/bin

KINETIC =	libkinetic.a
KINETICSO = 	libkinetic.so
KINETICSOV = 	$(KINETICSO).1
KINETICSOVL = 	$(KINETICSOV).0.0

BUILDLIB_KINETIC = $(BUILDLIB)/$(KINETIC)

PROTODIR =	protocol
PROTOC =	protoc
PROTOCDIR = 	../vendor/protobuf-c/protoc-c
PROTOCPLUGIN =	$(PROTOCDIR)/protoc-gen-c
PROTOBUFPATH =	../vendor/kinetic.proto
PROTOBUF = 	$(PROTOBUFPATH)/kinetic.proto
PROTOBUF_H =	$(PROTODIR)/kinetic.pb-c.h
PROTOBUF_C =	$(PROTODIR)/kinetic.pb-c.c
PROTOBUF_O =	kinetic.pb-c.o
KOBJ = 		kinetic.o
OBJS =		ktli.o ktli_socket.o ktli_session.o protocol_interface.o\
		open.o getlog.o get.o put.o del.o range.o batch.o iter.o\
		aio.o util.o validate.o labels.o error.o ktb.o version.o\
		basickv.o stat.o noop.o	flush.o	exec.o pinop.o  device.o\
		security.o						\
		$(PROTOBUF_O)
GITHASH	=	githash.h

INC_KPUB =	kinetic.h kinetic_types.h protocol_types.h basickv.h

INC_PPUB =	$(PROTOBUF_H)

INC_PRIV =	kio.h ktli.h ktli_session.h  \
		kinetic.h kinetic_internal.h \
		session.h

STATS = 

# debuglevel: 0=no debug, 1=info, 2=debug; -fmax-errors=10 reduces shown errs
DEBUG =	-g -DLOGLEVEL=0 $(STATS)

CFLAGS =	$(DEBUG) -I. -I$(BUILDDIR)/include -Wall -fpic -Wl,-export-dynamic
LDFLAGS =	-L$(BUILDDIR)/lib -L/usr/lib/$(shell gcc -print-multiarch)
LDLIBS =	-llist -lprotobuf-c -lm

CP =		/bin/cp
LN =		/bin/ln
MKDIR =		/bin/mkdir
INSTALL = 	/usr/bin/install

all:	gen_githash $(KINETICSOVL)

test:	test/src/test_hmac.c $(OBJS)
	$(MKDIR) -p test/bin
	$(CC) $(CFLAGS) -o test/bin/test_hmac test/src/test_hmac.c $(OBJS) $(LDFLAGS) $(LDLIBS) -lssl -lcrypto -lpthread

install:  gen_githash $(BUILDLIB_KINETIC)

$(BUILDLIB_KINETIC): $(KINETICSOVL)
	$(MKDIR) -p $(BUILDDIR)/include/kinetic
	$(MKDIR) -p $(BUILDDIR)/include/kinetic/protocol
	$(MKDIR) -p $(BUILDDIR)/lib
	$(INSTALL) -D -m 644 $(INC_KPUB) $(BUILDDIR)/include/kinetic
	$(INSTALL) -D -m 644 $(INC_PPUB) $(BUILDDIR)/include/kinetic/protocol
	$(INSTALL) -D -m 755 $(KINETIC) $(BUILDDIR)/lib
	$(INSTALL) -D -m 755 $(KINETICSOVL) $(BUILDDIR)/lib
	$(CP) -a $(KINETICSOV) $(BUILDDIR)/lib
	$(CP) -a $(KINETICSO) $(BUILDDIR)/lib

$(KINETIC):	links $(OBJS)
	ld -r -o $(KOBJ) $(OBJS) $(LDFLAGS) $(LDLIBS)
	ar -crs $@ $(KOBJ)
	ranlib $@

$(KINETICSOVL): $(KINETIC)
	gcc -shared -Wl,-soname,$(KINETICSOV) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)
	$(LN) -fs $(KINETICSOVL) $(KINETICSOV)
	$(LN) -fs $(KINETICSOV) $(KINETICSO)

#
# This is specifically for basickv. basickv.h is the only exported include
# that includes kinetic.h.  The proper include directive is
#   #include <kinetic/kinetic.h>
# This will not work in library src directory but is required for external
# usage.  So a link is added to allow basickv to build.
links:
	rm -f ./kinetic
	ln -s . ./kinetic
	touch links

#
# This forces the recreation githash.h, if necessary.
# This file creates a char string variable that holds the current
# top git log hash value.  The var is used in version to identify
# the source from whence it came.
#
GH_COMMENT = \
/* This file is GENERATED. Do not edit. Must only be included in version.c. */
GH_VAR = static const char ki_githash[] = \"%s\";
gen_githash: FORCE
	@(								\
	hash=`git log | head -n 1 | awk '{print $$2}'`;			\
	grep $$hash githash.h >/dev/null 2>&1;				\
	if [ $$? \!= 0 ]; then 						\
		echo Generating $(GITHASH);				\
		printf "$(GH_COMMENT)\n$(GH_VAR)\n" $$hash > $(GITHASH);\
	fi;								\
	exit 0;								\
	)


clean:
	rm -rf $(KINETIC) $(KINETICSO) $(KINETICSOV) $(KINETICSOVL) 	\
		$(PROTOBUF_H) $(PROTOBUF_C) $(GITHASH) a.out links *.o


$(OBJS): $(INC_KPUB) $(INC_PPUB) $(INC_PRIV) Makefile

version.o: $(GITHASH)

$(GITHASH):

$(PROTOBUF_O): $(PROTOBUF_C)
	$(CC) $(CFLAGS)   -c -o $@ $<

$(PROTOBUF_H) $(PROTOBUF_C): $(PROTOBUF)
	$(PROTOC) --c_out=$(PROTODIR) --proto_path=$(PROTOBUFPATH)	\
		  --plugin=$(PROTOCPLUGIN) $(PROTOBUF)

.PHONY: FORCE
FORCE:
