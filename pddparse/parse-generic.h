#ifndef _PARSE_GENERIC_H_
#define _PARSE_GENERIC_H_

#include "trace-struct.h"

#define PRINT_HEADER (256)

#define is_done()   (*(volatile int *)(&done))

void record_print(struct record_info *r);
int record_dump(struct record_info *r, int processed);
int next_record(int ifd, struct record_info **record);
void rfree(struct record_info *r);

void handle_sigint(__attribute__((__unused__)) int sig);

#endif /* _PARSE_GENERIC_H_ */

