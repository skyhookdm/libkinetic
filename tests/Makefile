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
BUILDDIR =	../build
DIRS     =	whitebox
BINDIR   =	$(BUILDDIR)/bin
CFLAGS   =	-g -I$(BUILDDIR)/include
LDFLAGS  =	-L$(BUILDDIR)/lib
LDLIBS   =	-lkinetic -lpthread

all: 
	@for i in $(DIRS); do              \
		(cd $$i; make all install) \
	done

clean:
	@for i in $(DIRS); do        \
		(cd $$i; make clean) \
	done
	rm -rf *.o

.PHONY: install
install:
