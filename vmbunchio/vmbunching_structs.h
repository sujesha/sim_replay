#ifndef _VMBUNCHING_STRUCTS_H_
#define _VMBUNCHING_STRUCTS_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <linux/types.h>
#include "slist.h"
#include <fcntl.h>

/*
 * Header for each recorded file
 *
 * @version:    Version information
 * @genesis:    Time stamp for earliest bunch
 * @nbunches:   Number of bunches put into the file
 * @total_pkts: Number of packets to be processed
 */
struct vm_file_hdr {
    __u64 version;
    __u64 genesis;
    __u64 nbunches;
    __u64 total_pkts;
};

#define BT_MAX_PKTS 512
#ifndef TESTVMREPLAY
	#define MAP_BT_MAX_PKTS 128 /* VMPkts in a bunch to be mapped, then 
					processed. Mapping may result in a 
					greater number of io_pkt.
	                                Hence, the reduced number here. */
#endif

#define HOSTNAME_LEN	(PATH_MAX)

/*
 * Header for each bunch
 *
 * @nkts:   Number of IO packets to process
 * @time_stamp: Time stamp for this bunch of IOs
 */
struct vm_bunch_hdr {
    __u64 npkts;
    __u64 time_stamp;
};

/*
 * IO specifer
 *         
 * @vmname: VM identifier for volume 
 * @block:  VM IO sector identifier
 * @nbytes: Number of bytes to process
 * @rw:     IO direction: 0 = write, 1 = read
 */
struct vm_pkt {
	char vmname[HOSTNAME_LEN];
    __u32 block;
    __u32 nbytes;
    __u32 rw;
    __u8 *content;
};

/* Frame of previous struct, except it doesnt have the variable-size content */
struct vm_pkt_frame {
	char vmname[HOSTNAME_LEN];
    __u32 block;
    __u32 nbytes;
    __u32 rw;
};

/*
 * Shorthand notion of a bunch of IOs
 *
 * @hdr:    Header describing stall and how many IO packets follow
 * @pkts:   Individual IOs are described here
 */
struct vm_bunch {
    struct vm_bunch_hdr hdr;
    struct vm_pkt pkts[BT_MAX_PKTS];
};

/* This structure is used to represent the incoming read/write requests
 * for virtual blocks. After virtual block to physical block mapping
 * (could be preplay, sreplay or ioreplay) is applied, this will get
 * mapped to struct io_pkt for async I/O
 *  
 * @time:   Time stamp when trace was emitted
 * @block:  VM IO sector identifier
 * @bytes:  Number of bytes transferred
 * @rw:     Read (1) or write (0) 
 */
struct vmreq_spec {
	char vmname[HOSTNAME_LEN];
    __u64 time;
    __u32 block;
    __u32 bytes;
    __u8 *content;
    int rw;
};

/*
 * IO specifer for async I/O replay
 *
 * @sector:  IO sector identifier
 * @nbytes: Number of bytes to process
 * @rw:     IO direction: 0 = write, 1 = read
 */
struct io_pkt {
    __u32 ioblk;
    __u32 nbytes;
    __u32 rw;
    __u8 *content;
};


/*
 * Per input file information
 *
 * @head:   Used to link up on input_files
 * @devnm:  Device name portion of this input file
 * @file_name:  Fully qualified name for this input file
 * @cpu:    CPU that this file was collected on
 * @ifd:    Input file descriptor (when opened)
 * @tpkts:  Total number of packets processed.
 */
struct ifile_info {
    struct slist_head head;
    char *file_name;
    int ifd;
	__u64 tpkts, genesis;
#if defined(VMBUNCHDIRECT_TEST) || defined(BOOTVMBUNCH)
	FILE *ifp;
#endif
};
				

/*  
 * Per output file information
 *
 * @ofp:    Output file 
 * @vfp:    Verbose output file
 * @file_name:  Fully qualified name for this file
 * @vfn:    Fully qualified name for this file
 * @cur:    Current IO bunch being collected
 * @iip:    Input file this is associated with
 * @start_time: Start time of th ecurrent bunch
 * @last_time:  Time of last packet put in
 * @bunches:    Number of bunches processed
 * @pkts:   Number of packets stored in bunches
 */ 
struct io_stream {
    FILE *ofp, *vfp;
    char *file_name, *vfn;
    struct vm_bunch *cur;
    struct ifile_info *iip;
    __u64 start_time, last_time, bunches, pkts;
};


#endif /* _VMBUNCHING_STRUCTS_H_ */
