BUILDDIR =	../../build
DIRS     =	getlog keyval
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
