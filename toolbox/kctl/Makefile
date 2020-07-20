BUILDDIR =	../../build
OBJS = 		kctl.o util.o
#get.o put.o del.o info.o \
#	ping.o cluster.o lock.o acl.o range.o  \
#	histogram.o

KCTL=		kctl
CFLAGS =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib
LDLIBS = 	-lkinetic -lpthread

all:		$(KCTL)

$(KCTL):	$(OBJS) 
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install: all
	/bin/mkdir -p $(BUILDDIR)/bin
	/usr/bin/install -c -m 755 $(KCTL) $(BUILDDIR)/bin

clean:
	rm -rf a.out $(KCTL) *.o

$(OBJS): kctl.h
