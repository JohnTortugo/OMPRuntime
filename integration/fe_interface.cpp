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


void __mtsp_bridge_init() {

	//1a: This initializes the auxiliary TGA library
	tga_init();	

	/*
	QDescriptor* QSBase = (struct QDescriptor*) malloc(3 * sizeof(QDescriptor));

	__mtsp_SubmissionQueueDesc 	= &QSBase[0];
	__mtsp_SubmissionQueueDesc->QT = SINGLE;
	__mtsp_SubmissionQueueDesc->QF = AGENT_DISPATCH;
	__mtsp_SubmissionQueueDesc->base_address = (long long int) malloc(SUBMISSION_QUEUE_SIZE);
	__mtsp_SubmissionQueueDesc->QSize = SUBMISSION_QUEUE_SIZE;
	__mtsp_SubmissionQueueDesc->reserved = 0;
	__mtsp_SubmissionQueueDesc->QId = 0;
	__mtsp_SubmissionQueueDesc->QHead = 0;
	__mtsp_SubmissionQueueDesc->QTail = 0;



	__mtsp_RunQueueDesc = &QSBase[1];
	__mtsp_RunQueueDesc->QT = SINGLE;
	__mtsp_RunQueueDesc->QF = AGENT_DISPATCH;
	__mtsp_RunQueueDesc->base_address = (long long int) malloc(RUN_QUEUE_SIZE);
	__mtsp_RunQueueDesc->QSize = RUN_QUEUE_SIZE;
	__mtsp_RunQueueDesc->reserved = 0;
	__mtsp_RunQueueDesc->QId = 0;
	__mtsp_RunQueueDesc->QHead = 0;
	__mtsp_RunQueueDesc->QTail = 0;


	__mtsp_RetirementQueueDesc 	= &QSBase[2];
	__mtsp_RetirementQueueDesc->QT = SINGLE;
	__mtsp_RetirementQueueDesc->QF = AGENT_DISPATCH;
	__mtsp_RetirementQueueDesc->base_address = (long long int) malloc(RUN_QUEUE_SIZE);
	__mtsp_RetirementQueueDesc->QSize = RUN_QUEUE_SIZE;
	__mtsp_RetirementQueueDesc->reserved = 0;
	__mtsp_RetirementQueueDesc->QId = 0;
	__mtsp_RetirementQueueDesc->QHead = 0;
	__mtsp_RetirementQueueDesc->QTail = 0;




	printf("MTSP: -----------------------\n");
	printf("QT = %d\n", __mtsp_SubmissionQueueDesc->QT);
	printf("QF = %d\n", __mtsp_SubmissionQueueDesc->QF);
	printf("BA = %p\n", (void *)__mtsp_SubmissionQueueDesc->base_address);
	printf("QS = %d\n", __mtsp_SubmissionQueueDesc->QSize);
	printf("RV = %d\n", __mtsp_SubmissionQueueDesc->reserved);
	printf("QI = %ld\n", __mtsp_SubmissionQueueDesc->QId);
	printf("QH = %ld\n", __mtsp_SubmissionQueueDesc->QHead);
	printf("QT = %ld\n", __mtsp_SubmissionQueueDesc->QTail);

	printf("MTSP: -----------------------\n");
	printf("QT = %d\n", __mtsp_RunQueueDesc->QT);
	printf("QF = %d\n", __mtsp_RunQueueDesc->QF);
	printf("BA = %p\n", (void *)__mtsp_RunQueueDesc->base_address);
	printf("QS = %d\n", __mtsp_RunQueueDesc->QSize);
	printf("RV = %d\n", __mtsp_RunQueueDesc->reserved);
	printf("QI = %ld\n", __mtsp_RunQueueDesc->QId);
	printf("QH = %ld\n", __mtsp_RunQueueDesc->QHead);
	printf("QT = %ld\n", __mtsp_RunQueueDesc->QTail);


	printf("MTSP: -----------------------\n");
	printf("QT = %d\n", __mtsp_RetirementQueueDesc->QT);
	printf("QF = %d\n", __mtsp_RetirementQueueDesc->QF);
	printf("BA = %p\n", (void *)__mtsp_RetirementQueueDesc->base_address);
	printf("QS = %d\n", __mtsp_RetirementQueueDesc->QSize);
	printf("RV = %d\n", __mtsp_RetirementQueueDesc->reserved);
	printf("QI = %ld\n", __mtsp_RetirementQueueDesc->QId);
	printf("QH = %ld\n", __mtsp_RetirementQueueDesc->QHead);
	printf("QT = %ld\n", __mtsp_RetirementQueueDesc->QTail);

	*/

	/// Initialize structures to store task metadata
	for (int i=0; i<MAX_TASKS; i++) {
		tasks[i] = nullptr;
		freeSlots.enq(i);
	}

	/*


	/// Create the thread where the systemc (HWS) module will execute
	pthread_create(&hwsThread, NULL, __hws_init, (void *)QSBase);




	*/
	/// Create worker threads
	__mtsp_initScheduler();
}



void __mtsp_enqueue_into_submission_queue(unsigned long long packet) {
	while(!tga_subq_can_enq());

	tga_subq_enq(packet);

	/*
	while (((__mtsp_SubmissionQueueDesc->QTail + (int)sizeof(struct SQPacket)) % __mtsp_SubmissionQueueDesc->QSize) == __mtsp_SubmissionQueueDesc->QHead);

	struct SQPacket* pos = (struct SQPacket*) (__mtsp_SubmissionQueueDesc->base_address + __mtsp_SubmissionQueueDesc->QTail);
	pos->payload 		 = packet;

	if (DEBUG_MODE) printf(ANSI_COLOR_RED "[MTSP - SUBQ] Packet with payload [%llx] going to index %02ld of subq, address %p\n" ANSI_COLOR_RESET, packet, __mtsp_SubmissionQueueDesc->QTail, pos);

	__mtsp_SubmissionQueueDesc->QTail = (__mtsp_SubmissionQueueDesc->QTail + sizeof(struct SQPacket)) % __mtsp_SubmissionQueueDesc->QSize;
	*/
}




void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...) {
	DEBUG_kmpc_fork_call(loc, argc);

	int i 		  = 0;
	int tid 	  = 0;
    void** argv   = (void **) malloc(sizeof(void *) * argc);
    void** argvcp = argv;
    va_list ap;

    if (__mtsp_initialized == false) {
    	__mtsp_initialized = true;
    	__mtsp_bridge_init();
    }

    va_start(ap, microtask);

    for(i=0; i < argc; i++)
        *argv++ = va_arg(ap, void *);

	va_end(ap);


	/// This is "global_tid", "local_tid" and "pointer to array of captured parameters"
    (microtask)(&tid, &tid, argvcp[0]);

    //printf("Assuming the compiler or the programmer added a #pragma taskwait at the end of parallel for.\n");
    __kmpc_omp_taskwait(nullptr, 0);
}




kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
	size_t shareds_offset 	= sizeof(kmp_taskdata) + sizeof_kmp_task_t;

    kmp_taskdata* taskdata 	= (kmp_taskdata*) malloc(shareds_offset + sizeof_shareds);

    kmp_task* task			= KMP_TASKDATA_TO_TASK(taskdata);

    task->shareds			= (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
    task->routine           = task_entry;

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
	new_task->part_id = freeSlots.deq();

	/// Store the pointer to the task metadata
	//printf("[mtsp:kmp_omp_task_with_deps]: Pointer to the kmp_task structure received: %p\n", new_task);
	//printf("[mtsp:kmp_omp_task_with_deps]: Index of tasks where that pointer was stored: %d\n", new_task->part_id);
	tasks[new_task->part_id] = new_task;

	/// Send the packet with the task descriptor
	create_task_packet(subq_packet.payload, 0, (ndeps == 0), new_task->part_id);
#ifdef LINEAR_DEBUG
	printf("[mtsp_bridge:]\tSending task descriptor #%d to the submission queue.\n", number_of_task_descriptors_sent++);
#endif
#ifdef PRINT_PACKETS
	//printf("[mtsp_bridge:]\tSending task descriptor #%d to the submission queue:", number_of_task_descriptors_sent++);
	print_num_in_chars(subq_packet.payload);
	//printf("\n");
#endif
	
	__mtsp_enqueue_into_submission_queue(subq_packet.payload);

	/// Increment the number of tasks currently in the system
	/// This was not the original place of this
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// Send the packets for each parameter
	for (kmp_int32 i=0; i<ndeps; i++) {
		unsigned char mode = deps[i].flags.in | (deps[i].flags.out << 1);

		subq_packet.payload = encode_dep(mode, (i == (ndeps-1)), deps[i].base_addr);
		//create_dep_packet(subq_packet.payload, mode, (i == (ndeps-1)), deps[i].base_addr);

#ifdef LINEAR_DEBUG
		printf("[mtsp_bridge:]\tSending dependence descriptor #%d to the submission queue.\n", number_of_dependence_descriptors_sent++);
#endif
#ifdef PRINT_PACKETS
		print_num_in_chars(subq_packet.payload);
#endif

		__mtsp_enqueue_into_submission_queue(subq_packet.payload);
	}

	return 0;
}




kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
	/// Reset the number of threads that have currently reached the barrier
	ATOMIC_AND(&__mtsp_threadWaitCounter, 0);

	/// Tell threads that they should synchronize at a barrier
	ATOMIC_OR(&__mtsp_threadWait, 1);

	/// Wait until all threads have reached the barrier
	while (__mtsp_threadWaitCounter != __mtsp_numWorkerThreads);

	/// OK. Now all threads have reached the barrier. We now free then to continue execution
	ATOMIC_AND(&__mtsp_threadWait, 0);

	/// Before we continue we need to make sure that all threads have "seen" the previous
	/// updated value of threadWait
	while (__mtsp_threadWaitCounter != 0);

	//hws_alive = false;

	return 0;
}




kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid) {
#ifdef LINEAR_DEBUG
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
    return 0;
}




kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid) {
#ifdef LINEAR_DEBUG
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
    return 1;
}




void __kmpc_end_single(ident* loc, kmp_int32 gtid) {
	/// Reset the number of threads that have currently reached the barrier
	ATOMIC_AND(&__mtsp_threadWaitCounter, 0);

	/// Tell threads that they should synchronize at a barrier
	ATOMIC_OR(&__mtsp_threadWait, 1);

	/// Wait until all threads have reached the barrier
	while (__mtsp_threadWaitCounter != __mtsp_numWorkerThreads);

	/// OK. Now all threads have reached the barrier. We now free then to continue execution
	ATOMIC_AND(&__mtsp_threadWait, 0);

	/// Before we continue we need to make sure that all threads have "seen" the previous
	/// updated value of threadWait
	while (__mtsp_threadWaitCounter != 0);

	//hws_alive = false;
}




kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid) {
#ifdef LINEAR_DEBUG
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
	return 1;
}




void __kmpc_end_master(ident* loc, kmp_int32 gtid) {
#ifdef LINEAR_DEBUG
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
}



int omp_get_num_threads() {
#ifdef LINEAR_DEBUG
	printf("Not implemented function was called. [%s, %d].\n", __FILE__, __LINE__);
#endif
	return 0;
}
