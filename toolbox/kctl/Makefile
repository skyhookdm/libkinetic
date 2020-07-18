
BUILDDIR =	../../build
OBJS = 		kctl.o util.o
#get.o put.o del.o info.o \
#	ping.o cluster.o lock.o acl.o range.o  \
#	histogram.o

CFLAGS =	-g -I$(BUILDDIR)/include
LDFLAGS =	-L$(BUILDDIR)/lib
LDLIBS = 	-lkinetic -lpthread
all: kctl

kctl: $(OBJS) 
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf a.out kctl *.o


$(OBJS): kctl.h
