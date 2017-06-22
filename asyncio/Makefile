CC  = gcc
CFLAGS  = -Wall -W -O2 -g
INCS    = -I.
#OCFLAGS = -UCOUNT_IOS -UDEBUG -DNDEBUG
#OCFLAGS = -DDEBUG_SS
XCFLAGS = -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
override CFLAGS += $(INCS) $(XCFLAGS) $(OCFLAGS)

PROGS   = asyncio
LIBS    = -laio -lrt -lpthread

default:
	    $(CC) $(CFLAGS) $(filter %.o,$^) -o sreplay debugg.c pddversion.c cpu-routines.c per-input-file.c iocbs.c reclaim-generic.c replay-generic.c main_async_replay.c $(LIBS)
#	    $(CC) $(CFLAGS) $(filter %.o,$^) $(LIBS) -o sreplay debugg.o pddversion.o cpu-routines.o per-input-file.o iocbs.o reclaim-generic.o replay-generic.o main_async_replay.o
