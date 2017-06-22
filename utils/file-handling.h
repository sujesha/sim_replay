#ifndef _FILE_HANDLING_H_
#define _FILE_HANDLING_H_

#include <stdio.h>
#include <fcntl.h>
#include "debug.h"

#ifndef open_fd
#define open_fd(fd, filename, flags) \
		    if (fd = open(filename, flags) < 0) \
        RET_ERR("file open failed for %s\n", filename);
#endif

#ifndef open_FILE
#define open_FILE(fp, filename, flags)  \
		    if ((fp = fopen(filename, flags)) == NULL)  \
        RET_ERR("fopen failed for %s\n", filename);
#endif

#ifndef generic_dump
#define generic_dump(data, type, numelts, fd) \
		    if (write(fd, (type *) data, numelts) != numelts)   \
        RET_ERR("generic_dump failed for %d\n", numelts);
#endif

#ifndef generic_dumpFILE
#define generic_dumpFILE(data, type, numelts, fp) \
		    if ((fwrite((type *) data, sizeof(type), numelts, fp)) != numelts) \
        RET_ERR("generic_dumpFILE failed for %d\n", numelts);
#endif

#ifndef generic_readup
#define generic_readup(data, type, numelts, fp) \
		    if (bytes = read(fp, (type *)data, numelts) == 0)   \
        RET_ERR("file over %d\n", numelts); \
    if (bytes < 0)                                  \
        RET_ERR("generic_readup failed %d\n", numelts);
#endif

#ifndef generic_readupFILE
#define generic_readupFILE(data, type, numelts, fp) \
		    if ((bytes = fread((type *)data, sizeof(type), numelts, fp)) == 0) \
        RET_ERR("file over %d\n", numelts);   \
    if (bytes < 0)                                  \
        RET_ERR("generic_readup failed %d\n", numelts);
#endif

#endif /* _FILE_HANDLING_H_ */
