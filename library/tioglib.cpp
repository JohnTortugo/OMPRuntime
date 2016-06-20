/********************************************************
 * Author: Lucas H.                                     *
 * File:   tioglib.c                                    *
 *                                                      *
 * This file implements the functions defined in        *
 * tioglib.h.                                           *
 *******************************************************/
#include "tioglib.h"
#include "reg_mapping.h"
#include "mem_interface.h"
#include <sys/mman.h>
#include <malloc.h>

#define USER_NPAGES 	1
#define SUBQ_NENTRIES 	128
#define RUNQ_NENTRIES 	16
#define DEVF_LOC	"/dev/tioga"
#define DEVICE_LEN 	8192UL			//length (in bytes) of device memory space
#define MEMORY_LEN      24576UL			//length (in bytes) of allocated memory

int PAGE_BYTESIZE = -1;	

void * user_page;
void * subq_ptr;
void * runq0_ptr;
void * runq1_ptr;
void * runq2_ptr;
void * runq3_ptr;


int tioga_fd;
uint32_t *device_mem = NULL;    //pointer to device memory region
uint32_t *user_mem   = NULL;    //pointer to user-space allocated memory


/// Function for allocating all data structures used by the library,
/// configuring TGA registers accordingly and asking the system to
/// boot up.
uint32_t* tga_init(void)
{
	low_init();

	//_main();
	//exit(0);
}

/// Function for creating high-level dependence descriptors.
///
/// @varptr:   dependence pointer
/// @mode:     dependence mode
dep tga_dep(void* varptr, uint8_t mode)
{
	dep r_dep = {.varptr = (uint64_t) varptr, .mode = mode};
	return r_dep;
}

/// Function for creating high-level task descriptors.
///
/// @funptr:   function pointer
/// @dlist:    pointer to a linked list holding the task
///            dependencies
/// @numdeps:  number of the dependences of the task
task tga_task(void* funptr, dep* dlist, int numdeps)
{
	task r_task = {.WDPtr = (uint64_t) funptr, .ndeps = numdeps, .deplist = dlist, .tID = (uint16_t) -1};

	return r_task;
}

/// Function for encoding task packets according to the
/// format specified for Tioga's submission queue.
///
/// Here it is assumed that the function pointer contained
/// in task_pkg->WDPtr has the last 2 null padding bits.
///
/// @task_pkg:		pointer to a high-level task descriptor
/// @prior:		QOS information about the task to encode
static uint64_t encode_task(task* task_pkg, int prior)
{
	uint64_t t_pkg = 0;

	t_pkg |= ((uint64_t) 1 << 62);				//Package Type identifying bits
	t_pkg |= ((uint64_t) 0 << 54);				//ASID
	t_pkg |= ((uint64_t) prior << 52);				//QOS
#if DBG(2)
	printf("task_pkg->ndeps: %d, calculated last field: %d\n", task_pkg->ndeps, (task_pkg->ndeps == 0));
#endif
	t_pkg |= ((uint64_t) (task_pkg->ndeps == 0) << 50);
	t_pkg |= ((uint64_t) task_pkg->WDPtr);			//WorkDescriptor

#if DBG(2)
	printf("Encoded task packet: %x\n", t_pkg);
#endif

	return t_pkg;
}

/// Function for encoding dependence packets according
/// to bit layout defined on Tioga's specification.
///
/// @dep_pkg:		high-level dependence descriptor to encode
static uint64_t encode_dep(dep* dep_pkg, bool last)
{
	uint64_t d_pkg = 0;

	d_pkg |= ((uint64_t) 2 << 62);				//Package Type identifying bits
	d_pkg |= ((uint64_t) 0 << 54);				//ASID
	d_pkg |= ((uint64_t) dep_pkg->mode << 52);			//QOS
	
#if DBG(3)
	printf("dep_pkg->mode: %d\n", dep_pkg->mode);
#endif
	d_pkg |= ((uint64_t) (last ? 1 : 0) << 50);
	d_pkg |= ((uint64_t) dep_pkg->varptr);			//dependence address

#if DBG(3)
	printf("Encoded dep packet: %Lx\n", d_pkg);
#endif

	return d_pkg;
}

static uint64_t encode_ret(task* run_pkg)
{
	uint64_t rt_pkg = 0;	

	rt_pkg |= (((uint64_t) run_pkg->tID) & 0xfff);

	return rt_pkg;
}

/// Function for reading a runnable task information
/// from an uint64_t variable.
///
/// @enc_run:   variable to be decoded
static task decode_run(uint64_t enc_run)
{
	task dec_run;

	dec_run.WDPtr = enc_run & 0x3ffffffffffff;
	dec_run.tID =   (enc_run >> 50) & 0xfff;
	dec_run.ndeps = -1;
	dec_run.deplist = NULL;

	return dec_run;
}

/// Function for enqueuing new tasks on the submission queue.
/// 
/// @task_pkg:   struct describing the task to be scheduled
/// @prior:      QOS information about the task
int tga_subq_enq(task* task_pkg, int prior)
{
	tga_subq_enq(encode_task(task_pkg, prior));

	return 0;
}

/// Function for enqueuing pre-encoded elements on the submission queue.
/// 
/// @enc_packet:   encoded packet (may be of task or dep type)
int tga_subq_enq(uint64_t enc_packet)
{
#if DBG(4) 
	printf("\nSubQ Descriptor:");
	read_desc(0);//0x10000000);//0x1f400000);

	printf("\nRunQ0 Descriptor:");
	read_desc(0x80);//200);//0x10000200);//0x1f400200);

	printf("Packet going to the SubQ: %llx\n", enc_packet);
#endif
	subq_push(&enc_packet);

	return 0;
}

/// Function for checking whether we can enqueue a new
/// element onto the subQ.
bool tga_subq_can_enq(void)
{
	return subq_can_enq();
}

/// Function for checking whether a certain runQ
/// has any elements to be dequeued.
///
/// @prior:   priority index of the runQ
bool tga_runq_can_deq(int prior)
{
	//assert(prior == 0);
	
	return runq0_can_deq();
}

/// Function for dequeuing a new runnable packet
/// from the runQ with given priority.
///
/// @prior:   priority index of the runQ
task tga_runq_deq(int prior)
{
	task my_task;

	return my_task;
}

/// Function for dequeuing a raw descriptor of a new
/// runnable packet from the runQ with given priority.
///
/// @prior:   priority index of the runQ
uint64_t tga_runq_raw_deq(int prior)
{
	//Mon Apr 25 10:45:13 BRT 2016
	//Only runq0 dequeuing is currently implemented
	//assert(prior == 0);	

	return runq0_pop();
}

/// Function for informing TGA that a new task
/// has finished its execution by enqueuing a
/// high-level descriptor of the retiring task
/// into the retQ.
///
/// @run_pkg:	run packet of the retiring task
int tga_retq_enq(task* run_pkg)
{
	tga_retq_enq(encode_ret(run_pkg));
	return 0;
}

/// Function for informing TGA that a new task
/// has finished its execution by enqueuing a
/// raw descriptor of the retiring task into
/// the retQ.
///
/// @run_pkg:	run packet of the retiring task
int tga_retq_enq(uint64_t enc_packet)
{
	retq_push(enc_packet);

	return 0;
}

void tga_exit(void)
{
	free(user_page);

	munmap(device_mem, DEVICE_LEN);
	device_mem = NULL;

	munmap(user_mem, MEMORY_LEN);
	user_mem = NULL;

	ioctl(tioga_fd, FINISH);	
	
	close(tioga_fd);
}

void test_task_encoding(void)
{
	task test_task = {123456789, 3};

	printf("Task 01:\n");
	printf("\tWDRPtr: %x\n", 123456789);
	printf("\tndeps: %d\n", 3);
	printf("\tprior: %d\n\n", 2);

	uint64_t encoded_task = encode_task(&test_task, 2);
	printf("Encoded task: %Lx\n", (long long unsigned) encoded_task);

	task test_task2 = {11109876543210, 0};

	printf("Task 02:\n");
	printf("\tWDRPtr: %Lx\n", (long long unsigned) 11109876543210);
	printf("\tndeps: %d\n", 0);
	printf("\tprior: %d\n\n", 2);

	uint64_t encoded_task2 = encode_task(&test_task2, 2);
	printf("Encoded task: %Lx\n", (long long unsigned) encoded_task2);
}

void test_dep_encoding(void)
{
	dep test_dep = {123456789, 2};

	printf("Dep 01:\n");
	printf("\tvarptr: %x\n", 123456789);
	printf("\tmode: %d\n", 2);
	printf("\tnext: %Lx\n\n", (long long unsigned) 5554443332);

	uint64_t encoded_dep = encode_dep(&test_dep, false);
	printf("Encoded dependence: %Lx\n", (long long unsigned) encoded_dep);

	dep test_dep2 = {11109876543210, 3};

	printf("Dep 02:\n");
	printf("\tvarptr: %Lx\n", (long long unsigned) 11109876543210);
	printf("\tmode: %d\n", 3);
	printf("\tnext: %x\n\n", 0);

	uint64_t encoded_dep2 = encode_dep(&test_dep2, true);
	printf("Encoded dependence: %Lx\n", (long long unsigned) encoded_dep2);
}

/*
int main (void)
{
	printf("Entering main...\n");
	testsuite2();
	//tga_init();
	//tga_exit();
	//test_queue();
	//test_task_encoding();
	//printf("-----------------------------------------\n");
	//test_dep_encoding();
	//test_spscqueue();

	return 0;
}
*/
