#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <map>

#include "pthread.h"
#include "kmp.h"
#include "mtsp.h"
#include "task_graph.h"
#include "scheduler.h"
#include "fe_interface.h"

kmp_uint64 tasksExecutedByCT = 0;
kmp_uint64 metadataRequestsNotServiced = 0;
volatile kmp_uint64 tasksExecutedByRT = 0;

SPSCQueue<kmp_task*, SUBMISSION_QUEUE_SIZE, SUBMISSION_QUEUE_BATCH_SIZE, SUBMISSION_QUEUE_CF> submissionQueue;

void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...) {
	int i 			= 0;
	int tid 		= 0;
    void** argv 	= (void **) malloc(sizeof(void *) * argc);
    void** argvcp 	= argv;
    va_list ap;

    /// Check whether the runtime library is initialized
    ACQUIRE(&__mtsp_lock_initialized);
    if (__mtsp_initialized == false) {
    	/// Init random number generator. We use this on the work stealing part.
    	srandom( time(NULL) );

    	__mtsp_initialized = true;
    	__mtsp_Single = false;

    	//===-------- The thread that initialize the runtime is the Control Thread ----------===//
    	__itt_thread_set_name("ControlThread");

    	__mtsp_initialize();
    }
	RELEASE(&__mtsp_lock_initialized);

	/// The size of the tasks is reset every time we enter a new parallel region
   	taskSize.clear();

    /// Capture the parameters and add them to a void* array
    va_start(ap, microtask);
    for(i=0; i < argc; i++) { *argv++ = va_arg(ap, void *); }
	va_end(ap);

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Fork_Call);

	/// This is "global_tid", "local_tid" and "pointer to array of captured parameters"
    (microtask)(&tid, &tid, argvcp[0]);

    //printf("Assuming the compiler or the programmer added a #pragma taskwait at the end of parallel for.\n");
    __kmpc_omp_taskwait(nullptr, 0);

    __itt_task_end(__itt_mtsp_domain);
}

kmp_taskdata* allocateTaskData(kmp_uint32 numBytes, kmp_int16* memorySlotId) {
	if (numBytes > TASK_METADATA_MAX_SIZE) {
#ifdef DEBUG_MODE
		printf("Request for metadata slot to big: %u\n", numBytes);
#endif
		metadataRequestsNotServiced++;
		return (kmp_taskdata*) malloc(numBytes);
	}
	else {
		for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
			if (__mtsp_taskMetadataStatus[i] == false) {
				__mtsp_taskMetadataStatus[i] = true;
				*memorySlotId = i;
				return  (kmp_taskdata*) __mtsp_taskMetadataBuffer[i];
			}
		}
	}

#ifdef DEBUG_MODE
	static int counter = 0;
	fprintf(stderr, "[%s:%d] There was not sufficient task metadata slots. %d\n", __FUNCTION__, __LINE__, counter++);
#endif

	metadataRequestsNotServiced++;

	/// Lets take the "safe" side here..
	return (kmp_taskdata*) malloc(numBytes);
}

kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Task_Alloc);

	kmp_uint32 shareds_offset  = sizeof(kmp_taskdata) + sizeof_kmp_task_t;
	kmp_int32 sizeOfMetadata = sizeof(mtsp_task_metadata);
	kmp_int16 memorySlotId = -1;

    kmp_taskdata* taskdata = allocateTaskData(shareds_offset + sizeof_shareds + sizeOfMetadata, &memorySlotId);

    kmp_task* task = KMP_TASKDATA_TO_TASK(taskdata);

    task->shareds  = (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
    task->routine  = task_entry;
    task->metadata = (_mtsp_task_metadata*) &((char *) taskdata)[shareds_offset + sizeof_shareds];
	task->metadata->globalTaskId = -1;
    task->metadata->metadata_slot_id = memorySlotId;

    __itt_task_end(__itt_mtsp_domain);
    return task;
}

kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Task_With_Deps);

#ifdef SUBQUEUE_PATTERN
	static std::map<kmp_uint64, kmp_uint64> taskTypes;
	static kmp_uint64 counter=0; 
	kmp_uint64 addr = (kmp_uint64) new_task->routine;

	if (taskTypes.find(addr) == taskTypes.end())
		taskTypes[addr] = taskTypes.size();

	printf("%llu %u\n", counter++, taskTypes[addr]);
#endif

	/// Ask to add this task to the task graph
	__mtsp_addNewTask(new_task, ndeps, dep_list);

	__itt_task_end(__itt_mtsp_domain);
	return 0;
}

void steal_from_single_run_queue(bool just_a_bit) {
	kmp_task* taskToExecute = nullptr;
	kmp_uint16 myId = __mtsp_numWorkerThreads;

	/// Counter for the total cycles spent per task
	unsigned long long start=0, end=0;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);

	while (true) {
		if (just_a_bit) {
			if (RunQueue.cur_load() < RunQueue.cont_load() && submissionQueue.cur_load() < submissionQueue.cont_load()) {
				__itt_task_end(__itt_mtsp_domain);
				return;
			}
		}
		else {
			if (RunQueue.cur_load() < RunQueue.cont_load() && submissionQueue.cur_load() <= 0) {
				__itt_task_end(__itt_mtsp_domain);
				return;
			}
		}

		if (RunQueue.try_deq(&taskToExecute)) {

			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);

#ifdef MEASURE_TASK_SIZE
			start = beg_read_mtsp();
#endif

			/// Start execution of the task
			(*(taskToExecute->routine))(0, taskToExecute);

#ifdef MEASURE_TASK_SIZE
			end = end_read_mtsp();
#endif

			__itt_task_end(__itt_mtsp_domain);

#ifdef MEASURE_TASK_SIZE
			taskToExecute->metadata->taskSize = (end - start);
#endif

			tasksExecutedByCT++;

			/// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
			RetirementQueue.enq(taskToExecute);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	__itt_task_end(__itt_mtsp_domain);
}

kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Barrier_Wait);

	/// Flush the current state of the submission queues..
	submissionQueue.fsh();

#ifdef MTSP_WORKSTEALING_CT
	steal_from_single_run_queue(false);
#endif

#ifdef TG_DUMP_MODE
	while (submissionQueue.cur_load() > 0);
	__mtsp_dumpTaskGraphToDot();
	__mtsp_activate_workers = true;
#endif

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

#ifdef TG_DUMP_MODE
	/// delete: this is just to debug (temporarily)
	__mtsp_activate_workers = false;
#endif

#ifdef MTSP_DUMP_STATS
	printf("%llu tasks were executed by the control thread.\n", tasksExecutedByCT);
	printf("%llu tasks were executed by the runtime thread.\n\n", tasksExecutedByRT);
	printf("Addr of coalested_task is %x\n", executeCoalesced);
	printf("Addr of runtime is %x\n", __mtsp_RuntimeThreadCode);
	printf("Addr of coalescing is %x\n", addCoalescedTask);

	for (auto& ts : taskSize) {
		auto& val = ts.second;
		std::cout << "Task " << std::hex << ts.first << " executed " << std::dec << val.first << " iteration(s) taking " << val.second << " cycles on average." << std::endl;
	}

	printf("\n\n\n");
#endif

	__itt_task_end(__itt_mtsp_domain);
	return 0;
}

kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid) {
	//printf("__kmpc_cancel_barrier %s:%d\n", __FILE__, __LINE__);
    return 0;
}

kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid) {
	//printf("__kmpc_single %s:%d\n", __FILE__, __LINE__);
	return TRY_ACQUIRE(&__mtsp_Single);
}

void __kmpc_end_single(ident* loc, kmp_int32 gtid) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Barrier_Wait);

	/// Flush the current state of the submission queues..
	submissionQueue.fsh();

#ifdef MTSP_WORKSTEALING_CT
	steal_from_single_run_queue(false);
#endif

#ifdef TG_DUMP_MODE
	/// delete: this is just to debug (temporarily)
	while (submissionQueue.cur_load() > 0);
	__mtsp_dumpTaskGraphToDot();
	__mtsp_activate_workers = true;
#endif

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

#ifdef TG_DUMP_MODE
	/// delete: this is just to debug (temporarily)
	__mtsp_activate_workers = false;
#endif

#ifdef MTSP_DUMP_STATS
	printf("%llu tasks were executed by the control thread.\n", tasksExecutedByCT);
	printf("%llu tasks were executed by the runtime thread.\n\n", tasksExecutedByRT);
#endif

#ifdef MEASURE_TASK_SIZE
//	for (; lastPrintedTaskId<__mtsp_globalTaskCounter; lastPrintedTaskId++) {
//		printf("%d %llu\n", lastPrintedTaskId, taskSizes[lastPrintedTaskId]);
//	}
#endif

	__itt_task_end(__itt_mtsp_domain);
	RELEASE(&__mtsp_Single);
	return ;
}

kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid) {
	//printf("__kmpc_master %s:%d\n", __FILE__, __LINE__);
	return 1;
}

void __kmpc_end_master(ident* loc, kmp_int32 gtid) {
	//printf("__kmpc_end_master %s:%d\n", __FILE__, __LINE__);
}

int omp_get_num_threads() {
	return __mtsp_numWorkerThreads + 2;
}
