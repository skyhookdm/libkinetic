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

PROJROOTDIR  =	../../..
TESTROOTDIR  =	../..

BUILDDIR     =	$(PROJROOTDIR)/build
GTESTDIR     =	$(PROJROOTDIR)/vendor/googletest
BINDIR       =	$(TESTROOTDIR)/bin

GOOGLETEST   =	$(GTESTDIR)/bazel-bin/libgtest.so
TEST_MAIN    =	$(BINDIR)/test_getlog
OBJS         =	requests.o helper.o ../kfixtures.o
CFLAGS       =	-g -I$(BUILDDIR)/include -I$(GTESTDIR)/googletest/include
CPPFLAGS     =	$(CFLAGS)
LDFLAGS      =	-L$(BUILDDIR)/lib
STATIC	     = 	-Wl,-Bstatic
DYNAMIC      =	-Wl,-Bdynamic
LDLIBS       =	$(STATIC) -lkinetic 					\
		$(DYNAMIC) -lpthread -lssl -lcrypto -lgtest -lstdc++

all: $(TEST_MAIN)

$(TEST_MAIN): $(OBJS)
	/bin/mkdir -p $(BINDIR)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install:
	/bin/mkdir -p $(BUILDDIR)/bin
	/usr/bin/install -c -m 755 $(TEST_MAIN) $(BUILDDIR)/bin

clean:
	rm -rf a.out $(TEST_MAIN) *.o

$(OBJS): ../kfixtures.hpp
