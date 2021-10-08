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

SRCS = 		bkv.c get.c put.c del.c exists.c util.c limits.c
HDRS =		bkv.h
OBJS =		$(SRCS:.c=.o)
DEPS = 		$(SRCS:.c=.d)

LIBDEPS =	$(BUILDLIB)/libkinetic.a

BKV=		bkv
CPPFLAGS = 	-I$(BUILDINC)
CFLAGS =	-g -Wall
LDFLAGS =	-L$(BUILDLIB)
STATIC = 	-Wl,-Bstatic
DYNAMIC =	-Wl,-Bdynamic

# By default this make statically links against libkinetic
# removing the STATIC var below permits dynamic linking, but
# the shared libs will need to be installed or LD_LIBRARY_PATH
# used to locate the shared libs
LDLIBS = 	$(STATIC) -lreadline 			\
		-lkinetic	 			\
		$(DYNAMIC) -lcurses -lpthread -lssl -lcrypto

SANITY =	Sanity.bkv
SANITYO =	Sanity.bkv.out
SANITYV =	Sanity.bkv.verified

all:		$(BKV)

$(BKV):		$(OBJS) $(LIBDEPS)
		$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install: $(BKV) $(SRCS) $(HDRS) Makefile
	mkdir -p $(BUILDBIN)
	mkdir -p $(BUILDSRC)/bkv
	/usr/bin/install -c -m 755 $(BKV) $(BUILDBIN)/bkv
	for f in $(SRCS) $(HDRS); do					\
		/usr/bin/install -c -m 644 $${f} $(BUILDSRC)/bkv;	\
	done
	sed 's?$(IBUILDDIR)?../..?g' Makefile > $(BUILDSRC)/bkv/Makefile
	touch install

clean:
	rm -rf a.out $(BKV) *.[od] $(SANITYO) install

clobber: clean
	rm -rf *.d

# The sanity target only works if you have a kineticd server
# running locally. The stdbuf utility is used to interleave
# stdout and stderr to get a consistent comparable output file.
sanity:
	stdbuf -oL -eL ./$(SANITY) > $(SANITYO) 2>&1
	cmp $(SANITYO) $(SANITYV)
	@echo "*** BKV Sanity Success"

$(OBJS) $(DEPS):

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(SRCS:.c=.d) 
