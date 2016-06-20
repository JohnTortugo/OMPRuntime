#ifndef __MEM_INTERFACE_H__
#define __MEM_INTERFACE_H__


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string>
#include "../iotypes.h"
#include <mutex>

#define DBG(v) (DBG_VAL >= v)

typedef enum { MULTI = 0, SINGLE = 1} HSA_QType;
typedef enum { KERNEL_DISPATCH = 1, AGENT_DISPATCH = 2, DATA_MODE = 0 } HSA_QFeature;

typedef struct
{
	HSA_QType QT;
	HSA_QFeature QF;
	uint64_t* base;
	uint64_t* doorbell;
	int32_t	Qsize;
	int32_t reserved;
	int64_t QId;
	int64_t write_index;
	int64_t read_index;
} QDescriptor;

//static inline void writel(uint32_t val, volatile void *addr);
void low_init();
void init_zynq();
void close_zynq();
static ssize_t zynq_read(void *buf, size_t offset, size_t count);
static  ssize_t zynq_write(const void *buf, size_t offset, size_t count);
static ssize_t zynq_read_ddr(void *buf, size_t offset, size_t count);
static  ssize_t zynq_write_ddr(const void *buf, size_t offset, size_t count);
int mem_write(const unsigned long address, const void *const source, const size_t bytes);
int mem_read(const unsigned long address, void *const destination, const size_t bytes);
void write_desc(QDescriptor* qd, unsigned long mem);
void read_desc(unsigned long mem);
void ring_db( void );
void ring_db( void );
void task_proc( void );
void subq_push(unsigned long long * packet_addr);
unsigned long long runq0_pop(void);
void retq_push(unsigned long long task);
bool subq_can_enq(void);
bool runq0_can_deq(void);
void fill_subq( void );
void phy_mem_io( void );
void print_tickets( void );
void keymode (unsigned int key, char *m);
void print_taskpool( void ) ;
int _main( void );
void old_main( void );

extern "C" {
void pretty_dump();
}

#endif
