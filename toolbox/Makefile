BUILDDIR =	../../build
DIRS = 		kctl
BINDIR =	$(BUILDDIR)/bin
CFLAGS =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib
LDLIBS = 	-lkinetic -lpthread

all: 
	@for i in $(DIRS); do \
		(cd $$i; make all install) \
	done

clean:
	@for i in $(DIRS); do \
		(cd $$i; make clean) \
	done

.PHONY: install
install:

# Move into toolbox/tests?
test_read: read_request.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BIN_DIR)/test_read $(READ_UTIL) $(LIB_SRC_FILES) $(DEP_SRC_FILES)

test_write: write_request.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BIN_DIR)/test_write $(WRITE_UTIL) $(LIB_SRC_FILES) $(DEP_SRC_FILES)
