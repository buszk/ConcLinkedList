ROOT = ../..

include $(ROOT)/common/Makefile.common

BINS = $(BINDIR)/lc-ll
PROF = $(ROOT)/src

.PHONY:	all clean

all:	main

main: linkedlist.h main.cpp
	#$(CC) $(CFLAGS) $(BUILDIR)/linkedlist.o  $(BUILDIR)/main.o -o $(BINS) $(LDFLAGS)
	$(CXX) $(CFLAGS) -I ../../include main.cpp linkedlist.h -o $(BINS) $(LDFLAGS) -g -O0

clean:
	-rm -f $(BINS)
