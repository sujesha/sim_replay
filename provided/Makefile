CC  = g++
CFLAGS  = -Wall -W -O2 -g
INCS    = -I. -I.. -I../pdd_replay
OCFLAGS = -UCOUNT_IOS -UDEBUG -DNDEBUG
XCFLAGS = -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
override CFLAGS += $(INCS) $(XCFLAGS) $(OCFLAGS)

PROGS   = 
LIBS    = -laio -lrt -lpthread

all: depend $(PROGS)

$(PROGS): | depend

docs:
	$(MAKE) -C doc all

docsclean:
	$(MAKE) -C doc clean

clean: docsclean
	rm -f *.o $(PROGS) .depend

%.o: %.c
	$(CC) $(CFLAGS) -c -o $*.o $<

btrecord: glist.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^) $(LIBS)

depend:
	@$(CC) -MM $(CFLAGS) *.c 1> .depend

ifneq ($(wildcard .depend),)
include .depend
endif
