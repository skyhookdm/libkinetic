BUILDDIR  =	../../build
TEST_MAIN =	test_getlog
OBJS      =	test_getlog_requests.o ../test_kinetic.o
CFLAGS    =	-g -I$(BUILDDIR)/include
LDFLAGS   =	-L$(BUILDDIR)/lib
LDLIBS    =	-lkinetic -lpthread -lssl -lcrypto

all: $(TEST_MAIN)

$(TEST_MAIN): $(OBJS) 
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install: all
	/bin/mkdir -p $(BUILDDIR)/bin
	/usr/bin/install -c -m 755 $(TEST_MAIN) $(BUILDDIR)/bin

clean:
	rm -rf a.out $(TEST_MAIN) *.o

$(OBJS): ../test_kinetic.h
