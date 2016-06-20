/******************************************************************************
 *        LG Electronics             Copyright (c) 2016                       *
 *                                   LG Electronics, Inc.                     *
 *                                   All rights reserved.                     *
 *                                                                            *
 * This work may not be copied, modified, re-published, uploaded, executed,   *
 * or distributed in any way, in any medium, whether in whole or in part,     *
 * without prior written permission from LG Electronics, Inc.                 *
 ******************************************************************************/
#include "mem_interface.h"

#define GPIO_DATA_OFFSET     0
#define MAP_SIZE 8192UL//4096UL
#define MAP_MASK (MAP_SIZE - 1)
#define TLB_SIZE 24576 //6 pages of 4KB

#define MAP_SIZE_DDR 24576UL
#define MAP_MASK_DDR (MAP_SIZE_DDR - 1)

//#define DB_EVENT  //If defined, doorbell is triggered via CPU event

//HWS_REGISTER_DEFINITION
#define HWS_CONTROL	0x0
#define HWS_STATUS	0x1
#define DOORBELL_L	0x4
#define DOORBELL_H	0x5
#define LASTMSG_L	0x6
#define LASTMSG_H	0x7
#define UINTR		0x8
#define KINTR		0x9
#define UIEN		0xa
#define KIEN		0xb
#define VER_L		0xe
#define VER_H		0xf
#define FINTASK_L	0x10
#define FINTASK_H	0x11
#define FSM_NOT_IDLE	0x1c
#define HWERR		0x1e
#define AW_MST_PROP	0x618
#define AR_MST_PROP	0x619
#define ACE_MST_CONT	0x612
#define AX_SUBM_Q	0x614
#define AX_RUN_Q	0x615
#define RUN_Q_0_L	0x600
#define RUN_Q_0_H	0x601
#define RUN_Q_1_L	0x602
#define RUN_Q_1_H	0x603
#define RUN_Q_2_L	0x604
#define RUN_Q_2_H	0x605
#define RUN_Q_3_L	0x606
#define RUN_Q_3_H	0x607
#define SUB_Q_L		0x608
#define SUB_Q_H		0x609
#define TLB_0_ENTRY_L	0X620
#define TLB_0_ENTRY_H	0X621
#define TLB_1_ENTRY_L	0X622
#define TLB_1_ENTRY_H	0x623
#define TLB_2_ENTRY_L	0x624
#define TLB_2_ENTRY_H	0x625
#define TLB_3_ENTRY_L	0x626
#define TLB_3_ENTRY_H	0x627
#define TLB_4_ENTRY_L	0x628
#define TLB_4_ENTRY_H	0x629
#define TLB_5_ENTRY_L	0x62a
#define TLB_5_ENTRY_H	0x62b
#define TLB_0_MAP_L	0x630
#define TLB_0_MAP_H	0x631
#define TLB_1_MAP_L	0x632
#define TLB_1_MAP_H	0x633
#define TLB_2_MAP_L	0x634
#define TLB_2_MAP_H	0x635
#define TLB_3_MAP_L	0x636
#define TLB_3_MAP_H	0x637
#define TLB_4_MAP_L	0x638
#define TLB_4_MAP_H	0x639
#define TLB_5_MAP_L	0x63a
#define TLB_5_MAP_H	0x63b
#define CYCLE_CNT_L	0x24e
#define CYCLE_CNT_H	0x24f
#define STAT_CTL        0x25e
#define ACE_MST_CON	0x612

//This is architecture dependent, currently set page sizes to 4096 (page shift = 12)
#define PAGE_SHIFT 12
#define PAGEMAP_LENGTH 8

//Extracting task ID from task
#define taskID(x) ((x>>50)&0x0fff)

//Max number of tasks before queue index reset
#define MTASK 0x3B9ACA00

#define device "/dev/nexusIO"
#define device_ddr "/dev/nexusIO_ddr"


void *mapped_dev_base;
void *mapped_dev_base_ddr;
int memfd;
int memfd_ddr;
uintptr_t vm_address;  //Virtual memory address


bool runq_critical_reg_set = false;
std::mutex runq_mtx;
std::mutex subq_mtx;
std::mutex retq_mtx;

static unsigned long long subq_task_counter = 0;
static unsigned long long runq_task_counter = 0;
static unsigned long long retq_task_counter = 0;

static unsigned long long local_subq_write_index = 0;
static unsigned long long local_subq_read_index = 0;
volatile static unsigned long long local_runq_read_index = 0;

static bool runq_read_dirty = false;
static bool subq_write_dirty = false;

void* create_buffer(void);
unsigned long get_page_frame_number_of_address(void *addr);

static inline uint32_t readl(const volatile void *addr)
{ 
	uint32_t ret;
	asm volatile("ldr  %[result],%[value]"
		      :[result] "=r" (ret) 
		      :[value] "m" (*(volatile uint32_t *)addr) 
		      :"memory");
	return ret;
}

static inline void writel(uint32_t val, volatile void *addr) 
{
#if DBG(10)
	printf("[writel]: Address we are going to write to: %p\n", addr);
#endif

	asm volatile("str %0,%1"
		      : 
		      :"r" (val), "m" (*(volatile uint32_t *)addr) 
		      :"memory");
}

static inline void double_writel(uint64_t val, volatile void *addr)
{
	register uint32_t packet_low asm ("r5");
	register uint32_t packet_high asm ("r6");

	packet_low = val & (0xffffffff);
	packet_high = val & (0xffffffff00000000);

    uint32_t * base_addr = (uint32_t *) addr;

#if DBG(7)
	printf("[double_writel]: Value of mapped_dev_base: %p\n", ((volatile uint32_t *) mapped_dev_base)); 
	printf("[double_writel]: Base address for the Store Multiple instruction: %p\n", base_addr); 
	printf("[double_writel]: (packet_low, packet_high) = (%x, %x)\n", packet_low, packet_high);
#endif
    
	asm volatile("stm %[ret_addr], {r5, r6}"
              :
              : [ret_addr] "r" (base_addr) 
		      : "memory");
}

static inline void instruction_barrier(void)
{
	asm volatile("dsb");
}

static inline void flush_pipeline(void)
{
	asm volatile("isb");	
}

static inline void memory_sync_barrier(void)
{
	asm volatile("dmb");
}

void init_zynq()
{
    memfd = open(device, O_RDWR | O_SYNC);
    if (memfd == -1) 
    {
#if DBG(1)
        printf("Can't open %s.\n",device);
#endif
        exit(0);
    }
#if DBG(1)
    printf("%s opened.\n",device);
#endif
    
    mapped_dev_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    
    if (mapped_dev_base == (void *) -1) 
    {
#if DBG(1)
        printf("Can't map the memory to user space.\n");
#endif
        exit(0);
    }
#if DBG(1)
    printf("Memory mapped at address %p.\n", mapped_dev_base);
#endif

	memfd_ddr = open(device_ddr, O_RDWR | O_SYNC);
        
    if (memfd_ddr == -1) 
    {
#if DBG(1)
        printf("Can't open %s.\n",device_ddr);
#endif
        exit(0);
    }
#if DBG(1)
    printf("%s opened.\n",device_ddr);
#endif
    
	mapped_dev_base_ddr = mmap(0, MAP_SIZE_DDR, PROT_READ | PROT_WRITE, MAP_SHARED, memfd_ddr, 0);
        
    if (mapped_dev_base_ddr == (void *) -1) 
    {
#if DBG(1)
        printf("Can't map the memory to user space.\n");
#endif
        exit(0);
    }
    
#if DBG(1)
    printf("Memory mapped at address %p.\n", mapped_dev_base_ddr);
#endif   
	vm_address = (uintptr_t)mapped_dev_base_ddr;
  
}

void close_zynq()
{
    if (munmap(mapped_dev_base, MAP_SIZE) == -1) 
    {
#if DBG(1)
        printf("Can't unmap memory from user space.\n");
#endif
        exit(0);
    }
 
    close(memfd);
#if DBG(1)
    printf("%s closed.\n",device);
#endif

	if (munmap(mapped_dev_base_ddr, MAP_SIZE_DDR) == -1) 
    {
#if DBG(1)
        printf("Can't unmap memory from user space.\n");
#endif
        exit(0);
    }   
    close(memfd_ddr);
#if DBG(1)
    printf("%s closed.\n",device_ddr);
#endif
}


static ssize_t zynq_read(void *buf, size_t offset, size_t count)
{
    //assert(count==4);
#if DBG(10)
	printf("mapped_dev_base: %x\n", (uint32_t)mapped_dev_base);
#endif 
 
	((uint32_t*)buf)[0] = readl((volatile unsigned *) ((char*)mapped_dev_base + offset*count)); 
#if DBG(10)
	printf("read  addr=x'%-4X, data=x'%8X,\n", offset, ((uint32_t*)buf)[0]);
#endif  
	return count;
}


static  ssize_t zynq_write(const void *buf, size_t offset, size_t count)
{
	size_t ncount = count;
	uint32_t* buffer = (uint32_t*) &(((uint32_t*) buf)[0]);
	uint32_t i= 0;
	writel(((uint32_t*)buf)[i],(volatile uint32_t *) ((char*)mapped_dev_base + offset*count));
#if DBG(10)
	printf("wrote REG addr=x'%-4X, data=x'%8x,\n", offset, buffer[i]);
#endif
	return count;
}

static ssize_t zynq_read_ddr(void *buf, size_t offset, size_t count)
{
#if DBG(10)
	assert(count%4==0);
	printf("mapped_dev_base_ddr: %x\n", (uint32_t)mapped_dev_base_ddr);
#endif 

	((uint32_t*)buf)[0] = readl((volatile unsigned *) ((char*)mapped_dev_base_ddr + offset*count));
	return count;
}

static  ssize_t zynq_write_ddr(const void *buf, size_t offset, size_t count)
{
	size_t ncount = count;
	uint32_t* buffer = (uint32_t*) &(((uint32_t*) buf)[0]);
	uint32_t i= 0;

    if (ncount == 8)
    {
        uint32_t packet_low = ((uint32_t*)buf)[0];
        uint32_t packet_high = ((uint32_t*)buf)[1];

        uint64_t packet = packet_low | ((uint64_t)packet_high << 32);

#if DBG(8)
        printf("[zynq_write_ddr]: Low packet %x\n", packet_low);
        printf("[zynq_write_ddr]: High packet %x\n", packet_high);
        printf("[zynq_write_ddr]: Trying to write the 64-bit packet %llx\n", packet);
#endif

		double_writel((uint64_t) ((uint32_t*)buf)[0] | ((   (uint64_t) ((uint32_t*)buf)[1]   ) << 32),(volatile uint32_t *) ((char*)mapped_dev_base_ddr + (offset)*4));
    }

	for(i=0;ncount>=4;i++)
	{
		writel(((uint32_t*)buf)[i],(volatile uint32_t *) ((char*)mapped_dev_base_ddr + (offset +i)*4));
		ncount-=4;
#if DBG(10)
		printf("wrote MEM addr=x'%-4X, data=x'%8x,\n", (offset +i)*4, buffer[i]);
#endif      
	} 
	//assert(ncount==0);
    
	return count;
}

//!!!IMPORTANT!!! When calling mem_write or mem_read, memory address are 32-bit/4-byte aligned

int mem_write(const unsigned long address, const void *const source, const size_t bytes)
{
	int ret = zynq_write_ddr(source, (address), bytes);
	return ret;
}

int mem_read(const unsigned long address, void *const destination, const size_t bytes)
{
	uint32_t low32, high32;
	uint64_t *buffer;


	buffer = (uint64_t *)destination;
	if (bytes == 4)
	{
		return zynq_read_ddr(destination, (address), bytes);
	}
	else
	{
		//		&buffer = destination;
		zynq_read_ddr(&low32, (address), 4);
#if DBG(10)
		printf("\nlow32 = %x  @ %x", low32, address);
#endif
		zynq_read_ddr(&high32, (address + 1), 4);
#if DBG(10)
		printf("\nhigh32 = %x @ %x \n", high32, address+4);
#endif
		*buffer = (uint64_t)((uint64_t)high32<<32 | low32);
		return 8;
	}
}

//Write descriptors value to memory location
void write_desc(QDescriptor* qd, unsigned long mem)
{
    //All memory space here are 32-bit (4-byte) aligned
	mem_write( mem, &(qd->QT), 4);
	mem += 1;//4;
	mem_write( mem, &(qd->QF), 4);
	mem += 1;//4;
	mem_write( mem, &(qd->base), 8);
	mem += 2;//8;
	mem_write( mem, &(qd->doorbell), 8);
	mem += 2;//8;
	mem_write( mem, &(qd->Qsize), 4);
	mem += 1;//4;
	mem_write( mem, &(qd->reserved), 4);
	mem += 1;//4;
	mem_write( mem, &(qd->QId), 8);
	mem += 2;//8;
	mem_write( mem, &(qd->write_index), 8);
	mem += 2;//8;
	mem_write( mem, &(qd->read_index), 8);
}

//Read back queue descriptor values
void read_desc(unsigned long mem)
{
	unsigned long long task;
    unsigned int buf;	

    //All memory space here are 32-bit (4-byte) aligned
	mem_read(mem, &buf, 4);
	printf("\nQT %x \n", buf);
	mem += 1;//4;
	mem_read(mem, &buf, 4);
	printf("QF: %x \n", buf);
	mem += 1;//4;
	mem_read(mem, &task, sizeof(task));
	printf("base address:%llx\n", task);
	mem += 2;//8;
	mem_read(mem, &task, 8);
	printf("doorbell %llx \n", task);
	mem += 2;//8;
	
	mem_read(mem, &buf, 4);
	printf("size: %x \n", buf);
	mem += 2;//8;//4;
	mem_read(mem, &task, 8);
	printf("Qid %llx\n", task);
	mem += 2;//8;
	
	mem_read(mem, &task, 8);
	printf("write index %llx, mem @ %lx \n", task, mem);
	mem += 2;//8;
	mem_read(mem, &task, 8);
	printf("read index %llx, mem @  %lx \n", task, mem);
}

uint32_t read_register( unsigned int offset )
{
	uint32_t packet;
	zynq_read(&packet, offset + 0x300, 4);
	return packet;
}

inline void print_status_reg(void)
{
	uint32_t packet;
	char* rbuffer = (char*) &packet;

	zynq_read(rbuffer,HWS_STATUS, 4); printf("HWS_STATUS 0x%x\n", 0xff & packet);
	zynq_read(rbuffer, 0x640, 4);printf("## taskID outstanding:\t\t0x%x\n", 0xff & packet);
	zynq_read(rbuffer, 0x643, 4);printf("## Run packet outstanding\t0x%x\n", 0xff & packet);
}

//Read back queue descriptor values and save into a structure
void save_desc(unsigned long mem, QDescriptor *d){

    //All memory space here are 32-bit (4-byte) aligned
	mem_read(mem, &(d->QT), 4);
	mem += 1;//4;
	mem_read(mem, &(d->QF), 4);
	mem += 1;//4;
	mem_read(mem, &(d->base), 8);
	mem += 2;//8;
	mem_read(mem, &(d->doorbell), 8);
	mem += 2;//8;
	
	mem_read(mem, &(d->Qsize), 4);
	mem += 2;//8;//4;
	mem_read(mem, &(d->QId), 8);
	mem += 2;//8;
	
	mem_read(mem, &(d->write_index), 8);
	mem += 2;//8;
	mem_read(mem, &(d->read_index), 8);
}

//read and do a formatted print of the queue descriptors
void pretty_dump(){
	
	unsigned long mem = 0;
	QDescriptor subQ, runQ[4];
	unsigned long long packet;
	char* rbuffer = (char*) &packet;

	save_desc(mem, &subQ);
	
	mem += 0x80;//0x200;
	save_desc(mem, &runQ[0]);
	mem += 0x80;//0x200;
	save_desc(mem, &runQ[1]);
	mem += 0x80;//0x200;
	save_desc(mem, &runQ[2]);
	mem += 0x80;//0x200;
	save_desc(mem, &runQ[3]);

	printf("-------------------------------------- QDescriptors Dump --------------------------------------------------------------\n");
	printf("  Variables  |        SubQ        |       runQ 0       |       runQ 1       |       runQ 2       |       runQ 3       |\n");
	printf("QT           | %18d | %18d | %18d | %18d | %18d |\n", subQ.QT, runQ[0].QT, runQ[1].QT, runQ[2].QT, runQ[3].QT);
	printf("QF           | %18d | %18d | %18d | %18d | %18d |\n", subQ.QF, runQ[0].QF, runQ[1].QF, runQ[2].QF, runQ[3].QF);
	printf("base address | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX |\n", (uint64_t)subQ.base, (uint64_t)runQ[0].base, (uint64_t)runQ[1].base, (uint64_t)runQ[2].base, (uint64_t)runQ[3].base);
	printf("doorbell     | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX |\n", (uint64_t)subQ.doorbell, (uint64_t)runQ[0].doorbell, (uint64_t)runQ[1].doorbell, (uint64_t)runQ[2].doorbell, (uint64_t)runQ[3].doorbell);
	printf("Qsize        |     0x%08X     |     0x%08X     |     0x%08X     |     0x%08X     |     0x%08X     |\n", subQ.Qsize, runQ[0].Qsize, runQ[1].Qsize, runQ[2].Qsize, runQ[3].Qsize);
	printf("reserved     |     0x%08X     |     0x%08X     |     0x%08X     |     0x%08X     |     0x%08X     |\n", subQ.reserved, runQ[0].reserved, runQ[1].reserved, runQ[2].reserved, runQ[3].reserved);
	printf("QId          | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX |\n", subQ.QId, runQ[0].QId, runQ[1].QId, runQ[2].QId, runQ[3].QId);
	printf("WIdx         | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX |\n", subQ.write_index, runQ[0].write_index, runQ[1].write_index, runQ[2].write_index, runQ[3].write_index);
	printf("RIdx         | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX | 0x%016llX |\n", subQ.read_index, runQ[0].read_index, runQ[1].read_index, runQ[2].read_index, runQ[3].read_index);
	printf("------------------------------------------------------ Statistics -----------------------------------------------------\n");
	zynq_read(rbuffer, 0x640, 4);printf("## taskID outstanding:\t\t0x%llx\n", packet);
	zynq_read(rbuffer, 0x643, 4);printf("## Run packet outstanding\t0x%llx\n", packet);
	
}

#ifdef DB_EVENT
void ring_db( void )
{ 
	asm volatile("sev");
}
#else
void ring_db( void )
{
	uint32_t packet = 0x10001111;
	register uint32_t packet_low asm ("r5");
	register uint32_t packet_high asm ("r6");

	packet_low = packet;
	packet_high = packet;

	volatile uint32_t * base_addr = (volatile uint32_t *) mapped_dev_base + DOORBELL_L;


#if DBG(7)
	printf("[ring_db]: Value of mapped_dev_base: %p\n", ((volatile uint32_t *) mapped_dev_base)); 
	printf("[ring_db]: Base offset: %p\n", ((volatile uint32_t *) (DOORBELL_L))); 
	printf("[ring_db]: Base addresses for the Store Multiple instruction: %p\n", base_addr); 
	printf("[ring_db]: (packet_low, packet_high) = (%x, %x)\n", packet_low, packet_high);
#endif

	asm volatile("stm %[ret_addr], {r5, r6}"
              :
              : [ret_addr] "r" (base_addr) 
		      : "memory");

	
}
#endif


#define RUNQ_0_CLEAR	0x00010000
#define RUNQ_1_CLEAR	0x00020000
#define RUNQ_2_CLEAR	0x00040000
#define RUNQ_3_CLEAR	0x00080000

void subq_push(unsigned long long * packet_addr)
{
	static unsigned long long last_update_on_write_index = 0;
	static bool equate_waiting = false;
	static bool lock_waiting = false;
	unsigned long dest_mem;
	bool this_is_a_last_package = false;

	if (*packet_addr & 0x4000000000000000)
	{
		++subq_task_counter;
#if DBG(3)
		printf("[subq_push]: Number of tasks submitted so far: 0x%llx\n", subq_task_counter);
#endif
	}

	if (subq_task_counter % MTASK == 0)
	{
#if DBG(3)
		printf("[subq_push]: Reached RunQ barrier.\n", subq_task_counter);
#endif

		unsigned long long widx, ridx;

		while(subq_task_counter != (retq_task_counter + 1))
		{
			if (!equate_waiting)
			{
#if DBG(3)
				printf("[subq_push]: Waiting for retq_task_counter (now %llx) to equate with %llx = subq_task_counter+1.\n", retq_task_counter, subq_task_counter + 1);
#endif
				equate_waiting = true;
			}
		}
		equate_waiting = false;

        runq_mtx.lock();
        subq_mtx.lock();

		long long unsigned zero = 0;
		
		mem_write(10, &zero, 8);
		mem_write(12, &zero, 8);
		mem_write(0x8a, &zero, 8);
		mem_write(0x8c, &zero, 8);


		runq_mtx.unlock();
		subq_mtx.unlock();
	}

	long long unsigned current_read_index;

	do
	{
		mem_read(12, &current_read_index, 8);
	}
	while (((local_subq_write_index + 1) % 0x200) == (current_read_index % 0x200));
			
	//for (int i = 0; i < 1000; i++);

	//mem_read(10, &local_write_index, 8);
	dest_mem = 0x400 + ((local_subq_write_index % 0x200) * 2);
	local_subq_write_index = local_subq_write_index + 1;

	//Actually writing packet
	mem_write(dest_mem, packet_addr, 8);
	asm volatile("dsb ish");

	if (*packet_addr & 0x4000000000000)
	{
		while (true)
		{
			//for (int i = 0; i < 5000; i++);

			mem_read(12, &current_read_index, 8);

			if (current_read_index == last_update_on_write_index)
				break;
		}

#if DBG(3)
		printf("[subq_push]: Updating write_index and ringing doorbell.\n");
#endif
		last_update_on_write_index = local_subq_write_index;
		mem_write(10, &local_subq_write_index, 8);
        asm volatile("dmb ish");
		ring_db();
	}
}

unsigned long long runq0_pop(void)
{
	static unsigned long long local_read_index = 0;
	static bool lock_waiting = false;
	unsigned long long task = 0;
	unsigned long long mem;

    runq_mtx.lock();

	mem = 0x800 + ((local_read_index % 0x200) * 2);

	mem_read(mem, &task, sizeof(task));
	
	local_read_index = (local_read_index + 1);// % 512;
	local_runq_read_index = local_read_index;

	runq_read_dirty = true;

	runq_mtx.unlock();

#if DBG(2)
	unsigned long long ridx;
	unsigned long long widx;
	mem_read(0x8a, &widx, 8);
	mem_read(0x8c, &ridx, 8);
	printf("[runq0_pop]: Number of elements on runq0: %lld, (ridx, widx) = (%lld, %lld)\n", (widx - ridx) % 512, ridx, widx);
	printf("[runq0_pop]: Number of tasks popped from RunQ so far: 0x%llx\n", runq_task_counter);
	print_status_reg();
#endif
	runq_task_counter++;

	return task;
}

bool subq_can_enq(void)
{
	unsigned long long subq_read_index;

	mem_read(12, &subq_read_index, 8);

	return ((local_subq_write_index + 1) % 0x200 != subq_read_index % 0x200);
}

bool runq0_can_deq(void)
{
	unsigned long long mem;
	unsigned long long ridx;
	unsigned long long widx;
	static bool printed = false;

	mem_read(0x8a, &widx, 8);
	mem_read(0x8c, &ridx, 8);

	if (runq_read_dirty && ((ridx % 512) == (widx + 1) % 512))
	{
		unsigned long long local_read_index = local_runq_read_index;
		mem_write(0x8c, &local_read_index, 8);
		runq_read_dirty = false;
	}

	//runq_mtx.unlock();

	return (local_runq_read_index % 512) != (widx % 512);
}

void retq_push(unsigned long long task){

	unsigned long long packet;

	//Writing task ID to finished task 
	packet = taskID(task);

	register uint32_t packet_low asm ("r5");
	register uint32_t packet_high asm ("r6");

	packet_low = uint32_t (packet & 0xffffffff);
	packet_high = 0;

	volatile uint32_t * base_addr = (volatile uint32_t *) mapped_dev_base + FINTASK_L;

#if DBG(7)
	printf("[retq_push]: Value of mapped_dev_base: %p\n", ((volatile uint32_t *) mapped_dev_base)); 
	printf("[retq_push]: Base offset: %p\n", ((volatile uint32_t *) (FINTASK_L))); 
	printf("[retq_push]: Base addresses for the Store Multiple instruction: %p\n", base_addr); 
	printf("[retq_push]: (packet_low, packet_high) = (%u, %u)\n", packet_low, packet_high);
#endif

    retq_mtx.lock(); 
	asm volatile("stm %[ret_addr], {r5, r6}"
              :
              : [ret_addr] "r" (base_addr) 
		      : "memory");
    retq_mtx.unlock(); 

	retq_task_counter++;

#if DBG(3)
	printf("[retq_push]: Total of retired tasks so far: 0x%llx\n", retq_task_counter);
#endif

	return;
}

static int task_filled = 0;
static int subq_widx = 0;

//Loading task descriptors and tasks
void phy_mem_io( void )
{

	int task_count = 0;
	char command[16] = {0};

	unsigned long long task;
	unsigned long mem;

	// TODO: USING VIRTUAL ADDRESS FOR DESCRIPTOR, JH 3/4/16
	QDescriptor SubMQ = 
	{ 
		MULTI, 
		DATA_MODE, 
		(uint64_t *)(vm_address+0x1000),
		0,
		512,
		0,		
		0,
		0,
		//.reserved_2 = 0,
		0
	};

	QDescriptor RunQ0 = 
	{ 
		MULTI, 
		DATA_MODE, 
		(uint64_t *)(vm_address+0x2000),
		0,
		512,
		0,	
		0,
		0,
		//.reserved_2 = 0,
		0
	};

	QDescriptor RunQ1 = 
	{ 
		MULTI, 
		DATA_MODE, 
		(uint64_t *)(vm_address+0x3000),
		0,
		0,	
		512,
		0,
		0,
		//.reserved_2 = 0,
		0
	};

	QDescriptor RunQ2 = 
	{ 
		MULTI, 
		DATA_MODE, 
		(uint64_t *)(vm_address+0x4000),
		0,
		0,	
		512,
		0,
		0,
		//.reserved_2 = 0,
		0
	};

	QDescriptor RunQ3 = 
	{ 
		MULTI, 
		DATA_MODE, 
		(uint64_t *)(vm_address+0x5000),
		0,
		0,	
		512,
		0,
		0,
		//.reserved_2 = 0,
		0
	};


	mem = 0;
	write_desc(&SubMQ, mem);
	mem += 0x80;//0x200;
	write_desc(&RunQ0, mem);
	mem += 0x80;//0x200;
	write_desc(&RunQ1, mem);
	mem += 0x80;//0x200;
	write_desc(&RunQ2, mem);
	mem += 0x80;//0x200;
	write_desc(&RunQ3, mem);
		
}

void keymode (unsigned int key, char *m)
{
   	switch (key)
	{
     	case 0: m[0] = '-'; m[1] = '-'; break;
     	case 1: m[0] = 'R'; m[1] = ' '; break;
     	case 2: m[0] = 'W'; m[1] = ' '; break;
     	case 3: m[0] = 'R'; m[1] = 'W'; break;
   	}
}

void print_taskpool( void ) 
{  // 512 entries x 104 bits - pages 32 .. 39 = 8 pages
   // just display the contents of the first 4 entries

	unsigned long rkey01, rkey12, rkey23;
	unsigned int rkey[4];
	unsigned int key0, key1, key2, key3;
	char m0[3]={0}, m1[3]={0}, m2[3]={0}, m3[3]={0};

	unsigned int link;
	unsigned int page = 32; // all 4 entries are located in this page
	uint32_t packet;
	int i, j;
	uint64_t workdescriptor;

	volatile unsigned int loop = 0;

   	//register[6e0] = 32;
	packet = 32;
	zynq_write(&packet, 0x6e0, 4);


	loop = 0;
	while (loop < 100){ loop++; }

   	//
   	// first print a header then
   	// loop around all 4 entries
   	//
#if DBG(1)
   	printf("TaskID :NextID :WD        :[0]:Mode :ResID :[1]:Mode :ResID :[2]:Mode :ResID :[3]:Mode :ResID\n");
#endif
   	//
   	for (i=0; i<64; i++) 
	{
#if DBG(1)
	printf("\nfoo 1\n");
#endif
      	for (j=0; j<4; j++) 
		{ // get all 4 entries that cover 128 bits
	    	rkey[j] = read_register( (i<<2) + j);
      	} // for j
     //
      // extract keys  &  split into key and mode field + assign mode value
      //
#if DBG(1)
      	printf("\nfoo 2\n");
#endif
		key0 = (rkey[0] >> 10) & 0x1ff;
      	keymode( ((rkey[0] >> 19) & 3), m0);

      	key1 = (rkey[0] >> 21) & 0x1ff;
      	keymode( ((rkey[0] >> 30) & 3), m1);

      	key2 = (rkey[1]) & 0x1ff;
      	keymode( ((rkey[1] >> 9) & 3), m2);

      	key3 = (rkey[1] >> 11) & 0x1ff;
      	keymode( ((rkey[1] >> 20) & 3), m3);
      //
      //	qos = (rkey[1] >> 22) & 3;
      //
      //               1 2 3 4              4      3         2     1   12            2         1 
      //
      	//workdescriptor = ( ( ( (reg[3] & 0x0ff) << 32) | reg[2]) << 8) | ((reg[1] >> 24)  & 0x0ff);
      	workdescriptor = ( ( ( (uint64_t)(rkey[3] & 0x0ff) << 32) | rkey[2]) << 8) | ((rkey[1] >> 24)  & 0x0ff);
      
		// link to next
      	//link = (reg[0] & 0x200) ? (reg[0] & 0x1ff) : (0xfff);
		link = (rkey[0] & 0x200) ? (rkey[0] & 0x1ff) : (0xfff);

#if DBG(1)
printf("\nfoo 3\n");
#endif
      //
      // now print all of this
      //
#if DBG(1)
      	printf(" %03x   : %03x   : %016llx      : --> %2s  : %03x  : --> %2s  : %03x  : --> %2s  : %03x  : --> %2s  : %03x \n",i,link,workdescriptor,m0,key0,m1,key1,m2,key2,m3,key3);
#endif

   	} // for i
} // end of taskpool



void low_init(void)
{
	uint32_t packet, mem, zero = 0;
	char* rbuffer = (char*) &packet;
	char* wbuffer = (char*) &packet;
	char* zero_p = (char*) &zero;

	init_zynq();

	//Initially Set the Mode to IDLE
	packet = 0x0000000;
	wbuffer = (char *)&packet;
#if DBG(1)
	printf("\nWriting HWS_CONTROL  ");
#endif
	zynq_write(wbuffer, HWS_CONTROL, 4);

	//Configure AW, AR and ACE master registers
//Thu May 19 14:41:36 BRT 2016
/*
 * Michael asked us to disable caching by setting 0x00000010 instead of 0x00000000
 */
	packet = 0x00000010;
#if DBG(1)
	printf("\nWriting AW_MST_PROP  ");
#endif
	zynq_write(wbuffer, AW_MST_PROP, 4);
//Thu May 19 14:41:43 BRT 2016
	packet = 0x00000010;
#if DBG(1)
	printf("\nWriting AR_MST_PROP  ");
#endif
	zynq_write(wbuffer, AR_MST_PROP, 4);
	packet = 0x00000000;
#if DBG(1)
	printf("\nWriting ACE_MST_CONT  ");
#endif
	zynq_write(wbuffer, ACE_MST_CONT, 4);

	//Enable the Interrupt Register
	packet = 0x000f0100;
#if DBG(1)
	printf("\nWriting UIEN  ");
#endif
	zynq_write(wbuffer, UIEN, 4);

	//Set the Subq and Runq Policy Registers
	packet = 0x00000300;
#if DBG(1)
	printf("\nWriting AX_SUBM_Q  ");
#endif
	zynq_write(wbuffer, AX_SUBM_Q, 4);
	packet = 0x01018002;//0x00008102;//0x03040303;
#if DBG(1)
	printf("\nWriting AX_RUN_Q  ");
#endif
	zynq_write(wbuffer, AX_RUN_Q, 4);

	//Queue Descriptors Virtual Addresses Register Configurations
	packet = (uint32_t)(vm_address) + 0x200;//0x1f400200;
#if DBG(1)
	printf("\nWriting RUN_Q_0_L  ");
#endif
	zynq_write(wbuffer, RUN_Q_0_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting RUN_Q_0_H  ");
#endif
	zynq_write(zero_p, RUN_Q_0_H, 4);
	packet += 0x200;//= 0x1f400400;
#if DBG(1)
	printf("\nWriting RUN_Q_1_L  ");
#endif
	zynq_write(wbuffer, RUN_Q_1_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting RUN_Q_1_H  ");
#endif
	zynq_write(zero_p, RUN_Q_1_H, 4);
	packet += 0x200;//= 0x1f400600;
#if DBG(1)
	printf("\nWriting RUN_Q_2_L  ");
#endif
	zynq_write(wbuffer, RUN_Q_2_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting RUN_Q_2_H  ");
#endif
	zynq_write(zero_p, RUN_Q_2_H, 4);
	packet += 0x200;//= 0x1f400800;
#if DBG(1)
	printf("\nWriting RUN_Q_3_L  ");
#endif
	zynq_write(wbuffer, RUN_Q_3_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting RUN_Q_3_H  ");
#endif
	zynq_write(zero_p, RUN_Q_3_H, 4);
	packet = (uint32_t)(vm_address);//= 0x1f400000;
#if DBG(1)
	printf("\nWriting SUB_Q_L  ");
#endif
	zynq_write(wbuffer, SUB_Q_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting SUB_Q_H  ");
#endif
	zynq_write(zero_p, SUB_Q_H, 4);


	//Queue Descriptors and Queue Buffers Virtual Addresses Configurations for TLB
	packet = (uint32_t)(vm_address);//0x1f400000;
#if DBG(1)
	printf("\nWriting TLB_0_ENTRY_L  ");
#endif
	zynq_write(wbuffer, TLB_0_ENTRY_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_0_ENTRY_H  ");
#endif
	zynq_write(zero_p, TLB_0_ENTRY_H, 4);
	packet += 0x1000;//0x1f406000;
#if DBG(1)
	printf("\nWriting TLB_1_ENTRY_L  ");
#endif
	zynq_write(wbuffer, TLB_1_ENTRY_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_1_ENTRY_H  ");
#endif
	zynq_write(zero_p, TLB_1_ENTRY_H, 4);
	packet += 0x1000;//0x1f40e000;
#if DBG(1)
	printf("\nWriting TLB_2_ENTRY_L  ");
#endif
	zynq_write(wbuffer, TLB_2_ENTRY_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_2_ENTRY_H  ");
#endif
	zynq_write(zero_p, TLB_2_ENTRY_H, 4);
	packet += 0x1000;//0x1f416000;
#if DBG(1)
	printf("\nWriting TLB_3_ENTRY_L  ");
#endif
	zynq_write(wbuffer, TLB_3_ENTRY_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_3_ENTRY_H  ");
#endif
	zynq_write(zero_p, TLB_3_ENTRY_H, 4);
	packet += 0x1000;//0x1f41e000;
#if DBG(1)
	printf("\nWriting TLB_4_ENTRY_L  ");
#endif
	zynq_write(wbuffer, TLB_4_ENTRY_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_4_ENTRY_H  ");
#endif
	zynq_write(zero_p, TLB_4_ENTRY_H, 4);
	packet += 0x1000;//0x1f426000;
#if DBG(1)
	printf("\nWriting TLB_5_ENTRY_L  ");
#endif
	zynq_write(wbuffer, TLB_5_ENTRY_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_5_ENTRY_H  ");
#endif
	zynq_write(zero_p, TLB_5_ENTRY_H, 4);

	//Queue Descriptors and Queue Buffers Physical Addresses Configurations for TLB
	packet = 0x10000000;//(uint32_t)(pm_address);//0x1f400000;
#if DBG(1)
	printf("\nWriting TLB_0_MAP_L  ");
#endif
	zynq_write(wbuffer, TLB_0_MAP_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_0_MAP_H  ");
#endif
	zynq_write(zero_p, TLB_0_MAP_H, 4);
	packet += 0x1000;//= 0x1f406000;
#if DBG(1)
	printf("\nWriting TLB_1_MAP_L  ");
#endif
	zynq_write(wbuffer, TLB_1_MAP_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_1_MAP_H  ");
#endif
	zynq_write(zero_p, TLB_1_MAP_H, 4);
	packet += 0x1000;//= 0x1f40e000;
#if DBG(1)
	printf("\nWriting TLB_2_MAP_L  ");
#endif
	zynq_write(wbuffer, TLB_2_MAP_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_2_MAP_H  ");
#endif
	zynq_write(zero_p, TLB_2_MAP_H, 4);
	packet += 0x1000;//= 0x1f416000;
#if DBG(1)
	printf("\nWriting TLB_3_MAP_L  ");
#endif
	zynq_write(wbuffer, TLB_3_MAP_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_3_MAP_H  ");
#endif
	zynq_write(zero_p, TLB_3_MAP_H, 4);
	packet += 0x1000;//= 0x1f41e000;
#if DBG(1)
	printf("\nWriting TLB_4_MAP_L  ");
#endif
	zynq_write(wbuffer, TLB_4_MAP_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_4_MAP_H  ");
#endif
	zynq_write(zero_p, TLB_4_MAP_H, 4);
	packet += 0x1000;//= 0x1f426000;
#if DBG(1)
	printf("\nWriting TLB_5_MAP_L  ");
#endif
	zynq_write(wbuffer, TLB_5_MAP_L, 4);
	//packet = 0x00000000;
#if DBG(1)
	printf("\nWriting TLB_5_MAP_H  ");
#endif
	zynq_write(zero_p, TLB_5_MAP_H, 4);

	//Enable the doorbell and set to DBG mode
#ifdef DB_EVENT
	packet = 0x010001d3;
#else	
    packet = 0x010002d3;
#endif
#if DBG(1)
	printf("\nWriting HWS_CONTROL  ");
	printf("\n");
#endif
	zynq_write(wbuffer, HWS_CONTROL, 4);
	phy_mem_io(  );
}

