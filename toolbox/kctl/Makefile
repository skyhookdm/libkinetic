
OBJS = 	kctl.o util.o
#get.o put.o del.o info.o \
#	ping.o cluster.o lock.o acl.o range.o  \
#	histogram.o

CFLAGS=-g -I../../src -I../../vendor/protobuf-c
LDLIBS = -lkinetic
all: kctl

kctl: $(OBJS) $(KLIBS) 
	$(CC) -o $@ $(OBJS) $(KLIBS) $(LDLIBS)

clean:
	rm -rf a.out kctl *.o


$(OBJS): kctl.h
