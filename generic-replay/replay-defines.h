/* Defining cases for use of the sim_replay replay module */
/* Although the cases are listed here exhaustively, only a few of the cases
 * are implemented as of now, for our evaluation.
 */

/* ************************************/
#ifndef initmapfile
	extern int initmapfile;
#endif
#ifndef runtimemap
	extern int runtimemap;
#endif
#ifndef disksimflag
	extern int disksimflag;
#endif
#ifndef collectformat
	extern int collectformat;
#endif
#ifndef scanharddiskp
	extern int scanharddiskp;
#endif
#ifndef scanharddisks
	extern int scanharddisks;
#endif
#ifndef vmbunchreplay
	extern int vmbunchreplay;
#endif
#ifndef sreplayflag
	extern int sreplayflag;
#endif
/**************************************/

/* Disk simulation can be done without scanning (i.e. runtime mapping).
 * This was originally done in the simulation module, for comparison of
 * Vanilla vs IODEDUP vs CONFIDE. 
 * Trace in collectformat is typical so far, but may not be necessary.
 * Does not need recreatedisk.
 * Since no scanning, contentgen is not needed in case of collectformat.
 */
#define DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY \
	(disksimflag && runtimemap && collectformat && vmbunchreplay)
#define DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY	\
	(disksimflag && runtimemap && collectformat && !vmbunchreplay)
#define DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_VMBUNCHREPLAY
#define DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY \
	(disksimflag && runtimemap && !collectformat && !vmbunchreplay)
#define DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY \
	(disksimflag && sreplayflag && collectformat && vmbunchreplay)
#define DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY \
	(disksimflag && sreplayflag && collectformat && !vmbunchreplay)
#define DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY \
	(disksimflag && sreplayflag && !collectformat && !vmbunchreplay)

/* Disk simulation if done with scanning implies that scanning is done
 * on the pioevents trace file itself.
 * Disk simulation implies that scanning, if done, would be of the pioevents
 * trace file. This was especially introduced for doing PROVIDED evaluation,
 * but should work with Vanilla, IODEDUP and CONFIDE as well.
 * Does not need recreatedisk. 
 *
 * Trace may or may not be in collectformat, and contentgen is needed in case 
 * of collectformat, especially for PROVIDED.
 * may or may not be using vmbunch input file. 
 */
#define DISKSIM_SCANNINGTRACE_COLLECTFORMAT \
	(collectformat && disksimflag && (scanharddiskp || scanharddisks))
#define DISKSIM_SCANNINGTRACE_COLLECTFORMAT_VMBUNCHREPLAY
#define DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY \
	(collectformat && disksimflag && (scanharddiskp || scanharddisks) \
	&& !vmbunchreplay)
#define DISKSIM_SCANNINGTRACE_NOCOLLECTFORMAT_VMBUNCHREPLAY
#define DISKSIM_SCANNINGTRACE_NOCOLLECTFORMAT_PIOEVENTSREPLAY

/* Disk simulation if done with initmap implies that map has already been 
 * created and stored in files, which are read up now.
 * Not tested so far, but earlier map dumping and readup has been tested
 * 
 * Trace may or may not be in collectformat, and contentgen is needed in case 
 * of collectformat, especially for PROVIDED.
 * may or may not be using vmbunch input file. 
 */
#define DISKSIM_INITMAP_COLLECTFORMAT_VMBUNCHREPLAY
#define DISKSIM_INITMAP_COLLECTFORMAT_PIOEVENTSREPLAY
#define DISKSIM_INITMAP_NOCOLLECTFORMAT_VMBUNCHREPLAY
#define DISKSIM_INITMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
 
/* Realdisk (i.e. no disk simulation) implies that scanning, if done, would
 * be of the actual disk. This is not the original intention of this simulation
 * module, but with a bit of effort, this may be doable using the sync_.*
 * routines herein. But here, IODEDUP implementation and CONFIDE implementation
 * differences would need to be architected carefully, if possible at all.
 * Note that this kind of replay needs recreatedisk to be done first.
 *
 * Trace may or may not be in collectformat, and 
 * may or may not be using vmbunch input file.
 */
#define REALDISK_SCANNING_COLLECTFORMAT_VMBUNCHREPLAY 0
#define REALDISK_SCANNING_COLLECTFORMAT_PIOEVENTSREPLAY
#define REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY \
	(!disksimflag && !collectformat && vmbunchreplay && \
	 (scanharddisks || scanharddiskp))
#define REALDISK_SCANNING_NOCOLLECTFORMAT_PIOEVENTSREPLAY

/* Realdisk if used with runtime mapping, implies that recreatedisk 
 * has been done but scanning is not to be done.
 * Trace may or may not be in collectformat, and contentgen is needed in case 
 * of collectformat, especially for PROVIDED.
 * may or may not be using vmbunch input file. 
 */
#define REALDISK_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY
#define REALDISK_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY
#define REALDISK_RUNTIMEMAP_NOCOLLECTFORMAT_VMBUNCHREPLAY
#define REALDISK_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY

/* Real disk if used with initmap implies that map has already been 
 * created and stored in files, which are read up now.
 * Not tested so far, but earlier map dumping and readup has been tested
 * 
 * Trace may or may not be in collectformat, and contentgen is needed in case 
 * of collectformat, especially for PROVIDED.
 * may or may not be using vmbunch input file. 
 */
#define REALDISK_INITMAP_COLLECTFORMAT_VMBUNCHREPLAY
#define REALDISK_INITMAP_COLLECTFORMAT_PIOEVENTSREPLAY
#define REALDISK_INITMAP_NOCOLLECTFORMAT_VMBUNCHREPLAY
#define REALDISK_INITMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
 
