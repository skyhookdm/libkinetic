KINETIC =	libkinetic.a
BUILDDIR =	../build
PROTOBUF_C =	kinetic.pb-c.c
PROTOBUF =	kinetic.pb-c.o
PROTODIR =	protocol
KOBJ = 		kinetic.o
OBJS =		ktli.o ktli_socket.o ktli_session.o	\
		getlog.o open.o labels.o protocol_interface.o	\
		$(PROTOBUF)

INC_KPUB =	kinetic.h getlog.h protocol_types.h

INC_PPUB =	protocol/kinetic.pb-c.h

INC_PRIV =	kio.h ktli.h ktli_session.h kinetic.h	\
		kinetic_internal.h message.h session.h

CFLAGS =	-g -I$(BUILDDIR)/include -I.
LDFLAGS =	-L$(BUILDDIR)/lib
LDLIBS =	-llist -lprotobuf-c 

MKDIR =		/bin/mkdir
INSTALL = 	/usr/bin/install

all:	$(KINETIC)

test:	test/src/test_hmac.c $(OBJS)
	$(MKDIR) -p test/bin
	$(CC) $(CFLAGS) -o test/bin/test_hmac test/src/test_hmac.c $(OBJS) $(LDFLAGS) $(LDLIBS) -lssl -lcrypto -lpthread

install: all
	$(MKDIR) -p $(BUILDDIR)/include/kinetic
	$(MKDIR) -p $(BUILDDIR)/include/kinetic/protocol
	$(MKDIR) -p $(BUILDDIR)/lib
	$(INSTALL) -D -m 644 $(INC_KPUB) $(BUILDDIR)/include/kinetic
	$(INSTALL) -D -m 644 $(INC_PPUB) $(BUILDDIR)/include/kinetic/protocol
	$(INSTALL) -D -m 755 $(KINETIC)  $(BUILDDIR)/lib

$(KINETIC):	$(OBJS)
	ld -r -o $(KOBJ) $(OBJS) $(LDFLAGS) $(LDLIBS)
	ar -crs $@ $(KOBJ)

clean:
	rm -rf $(KINETIC) a.out *.o


$(OBJS): $(INC_KPUB) $(INC_PPUB) $(INC_PRIV)

$(PROTOBUF): $(PROTODIR)/$(PROTOBUF_C)
	$(CC) $(CFLAGS)   -c -o $@ $<
