#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>

#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include "ThreadedQueue.h"
#include "fe_interface.h"

bool 				volatile 	__mtsp_initialized 	= false;
pthread_t 						__mtsp_RuntimeThread;
bool 				volatile 	__mtsp_Single 		= false;

kmp_uint32 						__mtsp_globalTaskCounter = 0;


/// Initialization of locks
unsigned char volatile __mtsp_lock_initialized 	 = 0;
unsigned char volatile __mtsp_lock_newTasksQueue = 0;

/// Variables/constants related to the the taskMetadata buffer
bool volatile * __mtsp_taskMetadataStatus;
char ** __mtsp_taskMetadataBuffer;
//bool volatile * __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
//char ** __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];

#ifdef MEASURE_TASK_SIZE
  std::vector<mtsp_task_metadata> taskMetadata(MAXIMUM_EXPECTED_TASKS);
  kmp_uint32 lastPrintedTaskId = 0;
#endif

extern void steal_from_single_run_queue(bool just_a_bit);
extern void steal_from_multiple_run_queue(bool just_a_bit);

void debug() {
  std::cout << std::endl;
  std::cout << "------------------ DEBUG ---------- DEBUG ------- DEBUG ----------" << std::endl;
  std::cout << "__mtsp_numThreads;              => " << __mtsp_numThreads << std::endl;
  std::cout << "__mtsp_numWorkerThreads;        => " << __mtsp_numWorkerThreads << std::endl;
  std::cout << "__mtsp_inFlightTasks            => " << __mtsp_inFlightTasks << std::endl;
  std::cout << "__mtsp_threadWait               => " << __mtsp_threadWait << std::endl;
  std::cout << "__mtsp_threadWaitCounter        => " << __mtsp_threadWaitCounter<< std::endl;
  std::cout << "freeSlots[0]                    => " << freeSlots[0] << std::endl;
  std::cout << "submissionQueue.cur_load        => " << submissionQueue.cur_load() << std::endl;
  std::cout << "RunQueue.cur_load               => " << RunQueue.cur_load() << std::endl;
  std::cout << "RetirementQueue.cur_load        => " << RetirementQueue.cur_load() << std::endl;
  std::cout << "------------------------------------------------------------------" << std::endl;
}

void __mtsp_initialize() {
    __mtsp_taskMetadataStatus = (bool volatile *) malloc(MAX_TASKMETADATA_SLOTS * sizeof(bool volatile));
    __mtsp_taskMetadataBuffer = (char **) malloc(MAX_TASKMETADATA_SLOTS * sizeof(char *));

      for (int i = 0; i < MAX_TASKMETADATA_SLOTS; i++) {
        __mtsp_taskMetadataBuffer[i] = (char *) malloc(TASK_METADATA_MAX_SIZE * sizeof(char));
      }

      //===-------- This slot is free for use by any thread ----------===//
      for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
        __mtsp_taskMetadataStatus[i] = false;
      }

      //===-------- Initialize the task graph manager ----------===//
      __mtsp_initializeTaskGraph();
}

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
    /// Increment the number of tasks in the system currently
    ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

    /// TODO: Can we improve this?
    {
        newTask->metadata->dep_list = (kmp_depend_info*) malloc(sizeof(kmp_depend_info) * ndeps);
        for (kmp_uint32 i=0; i<ndeps; i++) {
            newTask->metadata->dep_list[i] = depList[i];
        }

        newTask->metadata->ndeps = ndeps;
    }

    submissionQueue.enq(newTask);
}

void* __mtsp_RuntimeThreadCode(void* params) {
    kmp_task* task = nullptr;

    while (true) {
        /// Check if the execution of a task has been completed
        if (RetirementQueue.try_deq(&task)) {
            removeFromTaskGraph(task);
        }

        /// Check if there is any request for new thread creation
        if (freeSlots[0] > 0 && submissionQueue.try_deq(&task)) {
            addToTaskGraph(task);
        }
    }

    return 0;
}

