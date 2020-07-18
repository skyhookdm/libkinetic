
KINETIC =	libkinetic.a
BUILDDIR =	../build
PROTOBUF_C =	kinetic.pb-c.c
PROTOBUF =	kinetic.pb-c.o
PROTODIR =	protocol
KOBJ = 		kinetic.o
OBJS =		ktli.o ktli_socket.o ktli_session.o	\
		getlog.o open.o protocol_interface.o	\
		$(PROTOBUF)

INC_PUB=	kinetic.h getlog.h protocol_types.h	\
		protocol/kinetic.pb-c.h

INC_PRIV=	kio.h ktli.h ktli_session.h kinetic.h	\
		kinetic_internal.h message.h session.h

CFLAGS =	-g -I$(BUILDDIR)/include -I.
LDFLAGS =	-L$(BUILDDIR)/lib
LDLIBS =	-llist -lprotobuf-c 

all:	$(KINETIC)

install: all
	/bin/mkdir -p $(BUILDDIR)/include/kinetic $(BUILDDIR)/lib
	/usr/bin/install -c -m 644 $(INC_PUB) $(BUILDDIR)/include/kinetic
	/usr/bin/install -c -m 755 $(KINETIC) $(BUILDDIR)/lib

$(KINETIC):	$(OBJS)
	ld -r -o $(KOBJ) $(OBJS) $(LDFLAGS) $(LDLIBS)
	ar -crs $@ $(KOBJ)

clean:
	rm -rf $(KINETIC) a.out *.o


$(OBJS): $(INC_PUB) $(INC_PRIV)

$(PROTOBUF): $(PROTODIR)/$(PROTOBUF_C)
	$(CC) $(CFLAGS)   -c -o $@ $<
