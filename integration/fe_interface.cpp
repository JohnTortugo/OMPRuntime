#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <sys/time.h>
#include <pthread.h>

#include "kmp.h"
#include "debug.h"
#include "fe_interface.h"
#include "scheduler.h"

volatile char hws_alive;

pthread_t hwsThread;
bool __mtsp_initialized = false;

struct QDescriptor* __mtsp_SubmissionQueueDesc = nullptr;
struct QDescriptor* __mtsp_RunQueueDesc = nullptr;
struct QDescriptor* __mtsp_RetirementQueueDesc = nullptr;

kmp_task* volatile tasks[MAX_TASKS];
SPSCQueue<kmp_uint16, MAX_TASKS*2, 4> freeSlots;
int mtsp_number_of_outstanding_task_descriptors = 0;

void __mtsp_init() {

	//1a: This initializes the auxiliary TGA library
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	tga_init();	
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Initialize structures to store task metadata
	for (int i=0; i<MAX_TASKS; i++) {
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		tasks[i] = nullptr;
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		freeSlots.enq(i);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
	}

	/// Create worker threads
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	__mtsp_initScheduler();
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
}



void __mtsp_enqueue_into_submission_queue(unsigned long long packet) {
	while(!tga_subq_can_enq())
	{
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		printf("[mtsp:__mtsp_enqueue_into_submission_queue]: Waiting for subq_enq window.\n");
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		fflush(stdout);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
	}

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	tga_subq_enq(packet);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
}

void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...) {
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	DEBUG_kmpc_fork_call(loc, argc);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	int i 		  = 0;
	int tid 	  = 0;
	void** argv   = (void **) malloc(sizeof(void *) * argc);
	void** argvcp = argv;
	va_list ap;

    if (__mtsp_initialized == false) {
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    	__mtsp_initialized = true;
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    	__mtsp_init();
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    }

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    va_start(ap, microtask);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

    for(i=0; i < argc; i++)
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
        *argv++ = va_arg(ap, void *);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	va_end(ap);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");


	/// This is "global_tid", "local_tid" and "pointer to array of captured parameters"
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    (microtask)(&tid, &tid, argvcp[0]);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

#if DBG(9)
    printf("Assuming the compiler or the programmer added a #pragma taskwait at the end of parallel for.\n");
#endif
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    __kmpc_omp_taskwait(nullptr, 0);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
}




kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
	size_t shareds_offset 	= sizeof(kmp_taskdata) + sizeof_kmp_task_t;

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    kmp_taskdata* taskdata 	= (kmp_taskdata*) malloc(shareds_offset + sizeof_shareds);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    kmp_task* task			= KMP_TASKDATA_TO_TASK(taskdata);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    task->shareds			= (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
    task->routine           = task_entry;
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

    return task;
}


static uint64_t encode_task(uint8_t prior, uint8_t ndeps, uint64_t WDPtr)
{
	uint64_t t_pkg = 0;

	t_pkg |= ((uint64_t) 1 << 62);				//Package Type identifying bits
	t_pkg |= ((uint64_t) 0 << 54);				//ASID
	t_pkg |= ((uint64_t) prior << 52);				//QOS
	t_pkg |= ((uint64_t) (ndeps == 0) << 50);
	t_pkg |= ((uint64_t) WDPtr);			//WorkDescriptor

	return t_pkg;
}


static uint64_t encode_dep(uint8_t mode, bool last, uint64_t varptr)
{
	uint64_t d_pkg = 0;

	d_pkg |= ((uint64_t) 2 << 62);				//Package Type identifying bits
	d_pkg |= ((uint64_t) 0 << 54);				//ASID
	d_pkg |= ((uint64_t) mode << 52);			//QOS
	d_pkg |= ((uint64_t) (last ? 1 : 0) << 50);
	d_pkg |= ((uint64_t) varptr & 0x3ffffffffffff);			//dependence address

	return d_pkg;
}


kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* deps, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list) {
	static int number_of_task_descriptors_sent = 0;
	static int number_of_dependence_descriptors_sent = 0;
	SQPacket subq_packet;

	/// Obtain the id of the new task
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	new_task->part_id = freeSlots.deq();
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
#if DBG(9)
	mtsp_number_of_outstanding_task_descriptors = MAX_TASKS - freeSlots.cur_load();
	printf("[mtsp:__kmpc_omp_task_with_deps]: Number of outstanding task descriptors: %d\n", mtsp_number_of_outstanding_task_descriptors);
#endif

#if DBG(9)
	printf("[mtsp:kmp_omp_task_with_deps]: Pointer to the kmp_task structure received: %p\n", new_task);
	printf("[mtsp:kmp_omp_task_with_deps]: Index of tasks where that pointer was stored: %d\n", new_task->part_id);
#endif

	/// Store the pointer to the task metadata
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	tasks[new_task->part_id] = new_task;
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Send the packet with the task descriptor
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	create_task_packet(subq_packet.payload, 0, (ndeps == 0), (new_task->part_id) * 4);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
#if DBG(1)
	printf("[mtsp:]\tSending task descriptor #%x to the submission queue.\n", number_of_task_descriptors_sent++);
#endif
#if DBG(20)
	printf("[mtsp:]\tSending task descriptor #%d to the submission queue:", number_of_task_descriptors_sent++);
	print_num_in_chars(subq_packet.payload);
	printf("\n");
#endif
	
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	__mtsp_enqueue_into_submission_queue(subq_packet.payload);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Increment the number of tasks currently in the system
	/// This was not the original place of this
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	fflush(stdout);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Send the packets for each parameter
	for (kmp_int32 i=0; i<ndeps; i++) {
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		unsigned char mode = deps[i].flags.in | (deps[i].flags.out << 1);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");

		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		subq_packet.payload = encode_dep(mode, (i == (ndeps-1)), deps[i].base_addr);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		//create_dep_packet(subq_packet.payload, mode, (i == (ndeps-1)), deps[i].base_addr);

#if DBG(1)
		printf("[mtsp:]\tSending dependence descriptor #%x to the submission queue.\n", number_of_dependence_descriptors_sent++);
#endif
#if DBG(20)
		print_num_in_chars(subq_packet.payload);
#endif

		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		__mtsp_enqueue_into_submission_queue(subq_packet.payload);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
	}

	return 0;
}




kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
	static bool barrier_wait = false;
	static bool release_wait = false;

	/// Reset the number of threads that have currently reached the barrier
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_AND(&__mtsp_threadWaitCounter, 0);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

#if DBG(9)
	assert(__mtsp_threadWaitCounter == 0);
#endif

	/// Tell threads that they should synchronize at a barrier
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_OR(&__mtsp_threadWait, 1);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

#if DBG(9)
	assert(__mtsp_threadWait == 1);
#endif

	/// Wait until all threads have reached the barrier
	while (__mtsp_threadWaitCounter != __mtsp_numWorkerThreads)
	{
		if (!barrier_wait)
		{
#if DBG(9)
			printf("[mtsp:__kmpc_omp_taskwait]: Waiting for %d threads to reach the barrier.\n", __mtsp_numWorkerThreads);
#endif
			barrier_wait = true;
		}
	}
	barrier_wait = false;

	/// OK. Now all threads have reached the barrier. We now free then to continue execution
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_AND(&__mtsp_threadWait, 0);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

#if DBG(9)
	assert(__mtsp_threadWait == 0);
#endif

#if DBG(9)
	printf("[mtsp:__kmpc_omp_taskwait]: Released threads for execution again.\n");
#endif

	/// Before we continue we need to make sure that all threads have "seen" the previous
	/// updated value of threadWait
	while (__mtsp_threadWaitCounter != 0)
	{
		if (!release_wait)
		{
#if DBG(9)
			printf("[mtsp:__kmpc_omp_taskwait]: Waiting for %d threads to get notified about the release.\n", __mtsp_threadWaitCounter);
#endif
			release_wait = true;
		}
	}
	release_wait = false;

	return 0;
}




kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid) {
#if DBG(10)
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
    return 0;
}




kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid) {
#if DBG(10)
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
    return 1;
}




void __kmpc_end_single(ident* loc, kmp_int32 gtid) {
	/// Reset the number of threads that have currently reached the barrier
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_AND(&__mtsp_threadWaitCounter, 0);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Tell threads that they should synchronize at a barrier
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_OR(&__mtsp_threadWait, 1);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Wait until all threads have reached the barrier
	while (__mtsp_threadWaitCounter != __mtsp_numWorkerThreads);

	/// OK. Now all threads have reached the barrier. We now free then to continue execution
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	ATOMIC_AND(&__mtsp_threadWait, 0);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Before we continue we need to make sure that all threads have "seen" the previous
	/// updated value of threadWait
	while (__mtsp_threadWaitCounter != 0);

	//hws_alive = false;
}




kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid) {
#if DBG(10)
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
	return 1;
}




void __kmpc_end_master(ident* loc, kmp_int32 gtid) {
#if DBG(10)
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
}



int omp_get_num_threads() {
#if DBG(10)
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
	return 0;
}
