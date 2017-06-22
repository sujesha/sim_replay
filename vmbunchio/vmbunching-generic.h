#ifndef __VMBUNCHING_GENERIC_H_
#define __VMBUNCHING_GENERIC_H_



void bunch_output_hdr(struct io_stream *stream);
void bunch_output_pkts(struct io_stream *stream);
void vm_bunch_create(struct io_stream *stream, __u64 start_time);
int bunch_done(struct io_stream *stream, struct vmreq_spec *spec);
void vm_bunch_add(struct io_stream *stream, struct vmreq_spec *spec);
void write_file_hdr(struct io_stream *stream, struct vm_file_hdr *hdr);



#endif /* __VMBUNCHING_GENERIC_H_ */
