#ifndef _UNUSED_H_
#define _UNUSED_H_

#ifndef UNUSED
	#define UNUSED(expr) do { (void)(expr); } while (0)
#endif

#endif /* _UNUSED_H_ */
