#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "pthread.h"
#include "kmp.h"
#include "mtsp.h"
#include "task_graph.h"
#include "scheduler.h"
#include "fe_interface.h"

kmp_uint64 tasksExecutedByCT = 0;

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
    	printf("Initializing MSP...\n");

    	/// Init random number generator. We use this on the work stealing part.
    	srandom( time(NULL) );

    	__mtsp_initialized = true;
    	__mtsp_test = false;

    	//===-------- The thread that initialize the runtime is the Control Thread ----------===//
    	__itt_thread_set_name("ControlThread");

    	__mtsp_initialize();

    	printf("MTSP initialization complete.\n");
    }
	RELEASE(&__mtsp_lock_initialized);

    /// Capture the parameters and add them to a void* array
    va_start(ap, microtask);
    for(i=0; i < argc; i++) { *argv++ = va_arg(ap, void *); }
	va_end(ap);

	/// This is "global_tid", "local_tid" and "pointer to array of captured parameters"
    (microtask)(&tid, &tid, argvcp[0]);

    //printf("Assuming the compiler or the programmer added a #pragma taskwait at the end of parallel for.\n");
    __kmpc_omp_taskwait(nullptr, 0);
}

kmp_taskdata* allocateTaskData(kmp_uint32 numBytes, kmp_int32* memorySlotId) {
	if (numBytes > TASK_METADATA_MAX_SIZE) {
		printf("Request for metadata slot to big: %u\n", numBytes);
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

	fprintf(stderr, "[%s:%d] There was not sufficient task metadata slots.\n", __FUNCTION__, __LINE__);

	/// Lets take the "safe" side here..
	return (kmp_taskdata*) malloc(numBytes);
}

kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Alloc);

	kmp_uint32 shareds_offset  = sizeof(kmp_taskdata) + sizeof_kmp_task_t;
	kmp_int32 sizeOfMetadata = sizeof(mtsp_task_metadata);
	kmp_int32 memorySlotId = -1;

    kmp_taskdata* taskdata = allocateTaskData(shareds_offset + sizeof_shareds + sizeOfMetadata, &memorySlotId);

    kmp_task* task = KMP_TASKDATA_TO_TASK(taskdata);

    task->shareds  = (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
    task->routine  = task_entry;
    task->metadata = (_mtsp_task_metadata*) &((char *) taskdata)[shareds_offset + sizeof_shareds];
    task->metadata->metadata_slot_id = memorySlotId;

    __itt_task_end(__itt_mtsp_domain);
    return task;
}

kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list) {
	/// TODO: needs to assert \param ndeps_noalias always zero.

	/// Ask to add this task to the task graph
	__mtsp_addNewTask(new_task, ndeps, dep_list);

	return 0;
}

void steal_from_single_run_queue(bool just_a_bit) {
	kmp_task* taskToExecute = nullptr;
	kmp_uint16 myId = __mtsp_numWorkerThreads;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);

	while (true) {
		if (just_a_bit) {
			if (RunQueuea.cur_load() < RunQueuea.cont_load()) {
				__itt_task_end(__itt_mtsp_domain);
				break;
			}
		}
		else {
			if (submissionQueue.cur_load() <= 0) {
				__itt_task_end(__itt_mtsp_domain);
				break;
			}
		}

		if (RunQueuea.try_deq(&taskToExecute)) {
#ifdef MTSP_WORK_DISTRIBUTION_FT
			finishedIDS[0]++;
			finishedIDS[finishedIDS[0]] = myId;
#endif

//			/// Start execution of the task
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);
			(*(taskToExecute->routine))(0, taskToExecute);
			__itt_task_end(__itt_mtsp_domain);

			tasksExecutedByCT++;

			/// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Finished_Tasks_Queue_Enqueue);
#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
			RetirementQueues[myId].enq(taskToExecute);
#else
			RetirementQueue.enq(taskToExecute);
#endif
			__itt_task_end(__itt_mtsp_domain);
		}
	}

#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
	RetirementQueues[myId].fsh();
#endif

	__itt_task_end(__itt_mtsp_domain);
}

void executeStealed() {
	kmp_uint16 myId = __mtsp_numWorkerThreads;
	kmp_task* taskToExecute = nullptr;

	while (StealQueues[myId].try_deq(&taskToExecute)) {
#ifdef MTSP_WORK_DISTRIBUTION_FT
		finishedIDS[0]++;
		finishedIDS[finishedIDS[0]] = myId;
#endif

		/// Start execution of the task
		 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);
		(*(taskToExecute->routine))(0, taskToExecute);
		__itt_task_end(__itt_mtsp_domain);

		tasksExecutedByCT++;

		/// Inform that this task has finished execution
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Finished_Tasks_Queue_Enqueue);
#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
		RetirementQueues[myId].enq(taskToExecute);
#else
		RetirementQueue.enq(taskToExecute);
#endif
		__itt_task_end(__itt_mtsp_domain);
	}
}

void steal_from_multiple_run_queue(bool just_a_bit) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);

	kmp_uint16 myId = __mtsp_numWorkerThreads;

	if (just_a_bit) {
		kmp_uint16 victim = random() % __mtsp_numWorkerThreads;
		//printf("Trying steal. Victim [%d], Stealer [%d]\n", victim, myId);
		CAS(&StealStatus[victim].second, -1, myId);

		/// Wait until the status of the requisition change to see if we got work
		while (StealStatus[victim].second == myId);

		executeStealed();
	}
	else {
		while (submissionQueue.cur_load() > 0) {
			kmp_uint16 victim = random() % __mtsp_numWorkerThreads;
			CAS(&StealStatus[victim].second, -1, myId);

			/// Wait until the status of the requisition change to see if we got work
			while (StealStatus[victim].second == myId);

			executeStealed();
		}
	}

#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
	RetirementQueues[myId].fsh();
#endif

	__itt_task_end(__itt_mtsp_domain);
}

void dumpVector(char* fileName, std::vector<int> vals) {
	FILE* fp = fopen(fileName, "w+");

	if (!fp) {
		fprintf(stderr, "It was impossible to write the stats file [%s].\n", fileName);
		return ;
	}

	for (auto val : taskGraphSize)
		fprintf(fp, "%d, ", val);

	fclose(fp);
}

void dumpStats() {
	static int counter=0;
	char fileNameA[100], fileNameB[100];

	sprintf(fileNameA, "taskGraphSize_%04d.csv", counter);
	sprintf(fileNameB, "runQueueSize_%04d.csv", counter);

	dumpVector(fileNameA, taskGraphSize);
	dumpVector(fileNameB, runQueueSize);

	taskGraphSize.clear();
	runQueueSize.clear();

	counter++;
}

kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
	ACQUIRE(&__mtsp_test);

	/// Flush the current state of the submission queues..
	submissionQueue.fsh();

#ifdef MTSP_WORKSTEALING_CT
	#ifdef MTSP_MULTIPLE_RUN_QUEUES
		steal_from_multiple_run_queue(false);
	#else
		steal_from_single_run_queue(false);
	#endif
#endif

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Control_Thread_Barrier_Wait);

	/// TODO: have to check if we aren't already at a barrier. This may happen
	/// if we let several threads to call taskwait();

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

	__itt_task_end(__itt_mtsp_domain);

	RELEASE(&__mtsp_test);

	return 0;
}

kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid) {
	//printf("Got here %s:%d\n", __FILE__, __LINE__);
    return 0;
}

kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid) {
	//printf("Got here %s:%d\n", __FILE__, __LINE__);
    return 1;
}

void __kmpc_end_single(ident* loc, kmp_int32 gtid) {
	//printf("Got here %s:%d\n", __FILE__, __LINE__);
	return ;
}

kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid) {
	//printf("Got here %s:%d\n", __FILE__, __LINE__);
	return 1;
}

void __kmpc_end_master(ident* loc, kmp_int32 gtid) {
	//printf("Got here %s:%d\n", __FILE__, __LINE__);
}

int omp_get_num_threads() {
	return __mtsp_numWorkerThreads + 2;
}
