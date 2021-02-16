PROJROOTDIR  =	../../..
TESTROOTDIR  =	../..

BUILDDIR     =	$(PROJROOTDIR)/build
GTESTDIR     =	$(PROJROOTDIR)/vendor/googletest
BINDIR       =	$(TESTROOTDIR)/bin

TEST_MAIN    =	$(BINDIR)/test_keyval
OBJS         =	requests.o helper.o ../kfixtures.o ../hashtable.o
CFLAGS       =	-g -I$(BUILDDIR)/include -I$(GTESTDIR)/googletest/include
CPPFLAGS     =	$(CFLAGS)
LDFLAGS      =	-L$(BUILDDIR)/lib
LDLIBS       =	-lkinetic -lpthread -lssl -lcrypto -lgtest -lstdc++

all: $(TEST_MAIN)

$(TEST_MAIN): $(OBJS)
	/bin/mkdir -p $(BINDIR)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

install:
	/bin/mkdir -p $(BUILDDIR)/bin
	/usr/bin/install -c -m 755 $(TEST_MAIN) $(BUILDDIR)/bin

clean:
	rm -rf a.out $(TEST_MAIN) *.o

$(OBJS): ../kfixtures.hpp
