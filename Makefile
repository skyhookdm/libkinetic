TLD :=		$(shell pwd)
BUILDDIR =	$(TLD)/build
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib

LISTDIR =	$(TLD)/vendor/list
PROTOBUFDIR =	$(TLD)/vendor/protobuf-c
SRCDIR =	$(TLD)/src
TBDIR =		$(TLD)/toolbox

LPROTOBUF = 	$(BUILDLIB)/libprotobuf-c.so
LLIST =		$(BUILDLIB)/liblist.a
LKINETIC =	$(BUILDLIB)/libkinetic.a

CC =		gcc
CFLAGS =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib

all: $(BUILDDIR) $(LPROTOBUF) $(LLIST) $(LKINETIC) $(TBDIR)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

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

clean:	protobufclean listclean kineticclean toolboxclean
	rm -rf $(BUILDDIR)

protobufclean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make clean; true)

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
	rm -rf $(BUILDDIR)

.PHONY: FORCE
FORCE:

