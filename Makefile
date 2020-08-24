TLD :=		$(shell pwd)
BUILDDIR =	$(TLD)/build
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib

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

CC      =	gcc
CFLAGS  =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib

all: $(BUILDDIR) $(LPROTOBUF) $(LLIST) $(LKINETIC) $(TBDIR) $(TESTDIR) $(LGTEST)

test: $(TESTDIR)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

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

clean:	protobufclean listclean kineticclean toolboxclean
	rm -rf $(BUILDDIR)

protobufclean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make clean; true)

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
	(cd $(TBDIR); make clean)
	(cd $(TESTDIR); make clean)
	rm -rf $(BUILDDIR)

.PHONY: FORCE
FORCE:

