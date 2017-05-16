
all: splitmastream

CC=gcc
LD=gcc
CFLAGS=-O3 -Wall -Wstrict-prototypes

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o *~ *.bak core

distclean: clean
	rm -f splitmastream

splitmastream: splitmastream.o
	$(LD) $(LDFLAGS) -o splitmastream splitmastream.o

