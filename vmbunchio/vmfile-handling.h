#ifndef _VMFILE_HANDLING_H_
#define _VMFILE_HANDLING_H_

#include "vmbunching_structs.h"

//void vm_add_input_file(char *ifile);
void vm_add_input_file(char *ifile, char *ifull_name, struct ifile_info **iipp);
//void vm_rem_input_file(void);
void vm_rem_input_file(struct ifile_info *iip);
//int next_io(struct vmreq_spec *spec);
int next_io(struct vmreq_spec *spec, struct ifile_info *iipp);

#endif /* _VMFILE_HANDLING_H_ */
