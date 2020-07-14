
OBJS = 	kctl.o util.o
#get.o put.o del.o info.o \
#	ping.o cluster.o lock.o acl.o range.o  \
#	histogram.o

CFLAGS=-g -I../../src
LDLIBS = -lunwind -lssl -lcrypto -lpthread
all: kctl

kctl: $(OBJS) $(KLIBS) 
	$(C) -o $@ $(OBJS) $(KLIBS) $(LDLIBS)

clean:
	rm -rf a.out kctl *.o


$(OBJS): kctl.h
