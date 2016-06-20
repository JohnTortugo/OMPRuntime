#ifndef __TIOGLIB_H__
#define __TIOGLIB_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "iotypes.h"

typedef struct dep_s {
	uint64_t	varptr;
	uint8_t		mode;
} dep;

typedef struct task_s {			//TODO: Mudar spec
	uint64_t	WDPtr;
	int		ndeps;
	dep *		deplist;
	uint16_t	tID;
} task;

//TODO 2016/02/12: Check if we could alter the spec so as
//		   to return only actual structs, not pointers.

uint32_t*	tga_init(void);						
dep		tga_dep(void* varptr, uint8_t mode);		
task		tga_task(void* funptr, dep* deplist, int numdeps);
int		tga_subq_enq(task* task_pkg, int prior);	
int		tga_subq_enq(uint64_t enc_packet);	
bool		tga_subq_can_enq();
bool		tga_runq_can_deq(int prior);
task		tga_runq_deq(int prior);
uint64_t	tga_runq_raw_deq(int prior);
int		tga_retq_enq(task* task_pkg);
int		tga_retq_enq(uint64_t enc_packet);
void		tga_exit();

#endif
