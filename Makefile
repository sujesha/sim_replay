CXX = g++
CC = gcc
#ARCHI = -march=i686
CFLAGS  = -Werror -Wall -W -O2 -g
# -fprofile-arcs -ftest-coverage
INCS    = -I. -I.. -I./utils -I./provided -I./syncio -I./controller-utils -I./standard -I./iodedup -I./pddparse -I./vmbunchio -I./generic-replay -I./syncio -I./asyncio -I./confided -I./apriori-fixed -I./apriori-chunk -I./sector-cache -I./content-cache -I./runtime-fixed -I./runtime-chunk -I./apriori-fixed -I./apriori-chunk -I./ARC-cache -I./LRU-cache -I./contentdedup -I./blkID-hashtab -I./simdisk-runtime -I./simulated-cache-io
#OCFLAGS = -UCOUNT_IOS -UDEBUG -DNDEBUG
OCFLAGS = -DSYNCIO -DPDD_REPLAY_DEBUG_SS -DSIM_REPLAY -DSIMREPLAY_DEBUG_SS -DSIM_BENCHMARK_STATS 
OCFLAGS += -DNONSPANNING_PROVIDE
#OCFLAGS += -DSPANIMMEDIATE_PROVIDE
OCFLAGS += -DSIMULATED_DISK
#OCFLAGS += -DINCONSISTENT_TRACES 
#OCFLAGS += -DSTRICT_NO_HASH_COLLISION 
OCFLAGS += -DPRO_STATS
#OCFLAGS += -DCONTENTDEDUP
#OCFLAGS += -DMETADATAUPDATE_UPON_WRITES
OCFLAGS += -DIOMETADATAUPDATE_UPON_WRITES
#OCFLAGS += -DPDD_BENCHMARK_STATS
#OCFLAGS += -DPDDREPLAY_DEBUG_SS -DRECLAIM_DEBUG_SS 
#####dont use this!! OCFLAGS += SC_SUPPORT
XCFLAGS = -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

#LIBS     = -L./provided/rabin-tool-master/src
#INCS     += -I./provided/rabin-tool-master/src
#LDFLAGS = -lrabinpoly
#LIBS    += -laio -lrabinpoly
LIBS += -lpthread -lrt
override CFLAGS += $(INCS) $(XCFLAGS) $(OCFLAGS)

BLKSIZE := 4096
MINCHUNKSIZE := 512
MAXCHUNKSIZE := 61440
#HASHALGO := MD5
#CHUNKINGALGO := Rabin
HOSTNAME := $(shell hostname)
NOHASHMAGIC := 1
DISKNAME := $(shell ./get_testharddiskname.sh)

EXTRA_CFLAGS += -DBLKSIZE=$(BLKSIZE) -DMINCHUNKSIZE=$(MINCHUNKSIZE) -DMAXCHUNKSIZE=$(MAXCHUNKSIZE) -DHOSTNAME="$(HOSTNAME)" -DNOHASHMAGIC=$(NOHASHMAGIC) -DDISKNAME="$(DISKNAME)"
override CFLAGS += $(EXTRA_CFLAGS)

ifdef HEXPRINT12bits
	EXTRA_CFLAGS += -DHEXPRINT12bits=$(HEXPRINT12bits)
else
	EXTRA_CFLAGS += -DHEXPRINT12bits=0x008
endif

default:
#	cd provided/rabin-tool-master && make clean && ./configure && make
#	$(CXX) -c $(ARCHI) $(CFLAGS) $(LIBS) provided/rabin.cpp $(LDFLAGS)
	$(CC) -o simreplay $(ARCHI) $(CFLAGS) utils/ulist.c debugg.c pddversion.c utils/mbuffer.c utils/md5.c pddparse/endianness.c pddparse/parse-generic.c pddparse/parse_pdd_events.c pddparse/pdd_tokens.c generic-replay/cpu-routines.c generic-replay/per-input-file.c generic-replay/request-generic.c syncio/sync-replay-generic.c generic-replay/replay-generic.c asyncio/reclaim-generic.c asyncio/async-replay-generic.c asyncio/iocbs.c controller-utils/v2p-mapdump.c controller-utils/serveio-utils.c controller-utils/sync-disk-interface.c utils/utils.c utils/vector16.c utils/vector32.c controller-utils/v2p-map.c provided/v2c-map.c provided/serveio.c apriori-chunk/scandisk.c utils/uhashtab.c provided/chunktab.c provided/chunking.c provided/c2pv-map.c provided/rabin.c controller-utils/voltab.c provided/mapdump.c standard/std_serveio.c confided/fixedtab.c confided/f2pv-map.c confided/fixing.c apriori-fixed/fscandisk.c apriori-fixed/ioscandisk.c confided/fserveio.c confided/v2f-map.c confided/fmapdump.c generic-replay/replay-plugins.c runtime-fixed/fruntime.c runtime-fixed/ioruntime.c runtime-chunk/pruntime.c iodedup/deduptab.c iodedup/ioserveio.c iodedup/p2d-map.c iodedup/d2pv-map.c iodedup/iodeduping.c sector-cache/sectorcache.c content-cache/contentcache.c ARC-cache/arc.c LRU-cache/lru_cache.c simulated-cache-io/sim-replay-generic.c vmbunchio/vmfile-handling.c controller-utils/write-v2p-map.c contentdedup/contentdeduptab.c contentdedup/contentdedup-API.c pro_outputhashfn.c pro_outputtimefn.c content-gen.c content-simfile.c blkID-hashtab/blkidtab.c blkID-hashtab/blkidtab-API.c main_sim_replay.c simdisk-runtime/simdisk-API.c simulated-cache-io/simcache-file-API.c $(LIBS)
#	$(CXX) -o simreplay $(LIBS) $(ARCHI) $(CFLAGS) ulist.o debugg.o pddversion.o endianness.o parse-generic.o parse_pdd_events.o pdd_tokens.o rabin.o mbuffer.o md5.o cpu-routines.o per-input-file.o request-generic.o sync-replay-generic.o replay-generic.o reclaim-generic.o async-replay-generic.o iocbs.o v2p-mapdump.o serveio-utils.o sync-disk-interface.o utils.o vector16.o vector32.o v2p-map.o v2c-map.o serveio.o scandisk.o uhashtab.o chunktab.o chunking.o c2pv-map.o voltab.o mapdump.o std_serveio.o fixedtab.o f2pv-map.o fixing.o fscandisk.o ioscandisk.o fserveio.o v2f-map.o fmapdump.o replay-plugins.o fruntime.o ioruntime.o pruntime.o deduptab.o ioserveio.o p2d-map.o d2pv-map.o iodeduping.o sectorcache.o contentcache.o arc.o lru_cache.o sim-replay-generic.o vmfile-handling.o write-v2p-map.o pro_outputhashfn.o pro_outputtimefn.o content-gen.o content-simfile.o main_sim_replay.o contentdedup-API.o contentdeduptab.o blkidtab.o blkidtab-API.o simdisk-API.o $(LDFLAGS) $(LIBS)


clean: 
	rm -f *.o

