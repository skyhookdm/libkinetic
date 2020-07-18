TLD :=		$(shell pwd)
BUILDDIR =	$(TLD)/build
BUILDINC =	$(BUILDDIR)/include
BUILDLIB =	$(BUILDDIR)/lib

LISTDIR =	$(TLD)/vendor/list
PROTOBUFDIR =	$(TLD)/vendor/protobuf-c
SRCDIR =	$(TLD)/src

LPROTOBUF = 	$(BUILDLIB)/libprotobuf-c.so
LLIST =		$(BUILDLIB)/liblist.a
LKINETIC =	$(BUILDLIB)/libkinetic.a

CC =		gcc
CFLAGS =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib

all: $(LPROTOBUF) $(LLIST) $(LKINETIC) 

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(LPROTOBUF):	$(BUILDDIR)
	(cd $(PROTOBUFDIR); [ ! -f ./configure ] && ./autogen.sh; true)
	(cd $(PROTOBUFDIR); [ ! -f ./Makefile  ] && ./configure --prefix=$(BUILDDIR); true)
	(cd $(PROTOBUFDIR); make install)

$(LLIST):	$(BUILDDIR)
	(cd $(LISTDIR); INCDIR=$(BUILDINC) LIBDIR=$(BUILDLIB) make -e)
	(cd $(LISTDIR); INCDIR=$(BUILDINC) LIBDIR=$(BUILDLIB) make -e install)

$(LKINETIC):	$(BUILDDIR)
	(cd $(SRCDIR); BUILDDIR=$(BUILDDIR) make -e all install)

clean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make clean; true)
	(cd $(LISTDIR); make clean)
	(cd $(SRCDIR); make clean)
	rm -rf $(BUILDDIR)


distclean:
	(cd $(PROTOBUFDIR);  [ -f ./Makefile ] && make distclean; true)
	(cd $(LISTDIR); make clean)
	(cd $(SRCDIR); make clean)
	rm -rf $(BUILDDIR)


# directories
BIN_DIR=bin
TOOLBOX_DIR:=toolbox

# source files containing main functions (entrypoints)
READ_UTIL=$(TOOLBOX_DIR)/read_request.c
WRITE_UTIL=$(TOOLBOX_DIR)/write_request.c

test_read:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BIN_DIR)/test_read $(READ_UTIL) $(LIB_SRC_FILES) $(DEP_SRC_FILES)

test_write:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BIN_DIR)/test_write $(WRITE_UTIL) $(LIB_SRC_FILES) $(DEP_SRC_FILES)
