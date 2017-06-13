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
#include "benchmarking.hpp"

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

int stick_this_thread_to_core(const char* const pref, int core_id) {
  //	int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  //
  //	if (core_id >= num_cores)
  //		return -1;
  //
  //	cpu_set_t cpuset;
  //	CPU_ZERO(&cpuset);
  //	CPU_SET(core_id, &cpuset);
  //
  //	pthread_t current_thread = pthread_self();
  //
  //	pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
  //
  //	printf("%s executing now on core %d\n", pref, sched_getcpu());

  return 0;
}


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
      //bool volatile * __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
      //char ** __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];

      //===-------- This slot is free for use by any thread ----------===//
      for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
      __mtsp_taskMetadataStatus[i] = false;
      }

      /// This the original main thread to core-0
      stick_this_thread_to_core("ControlThread", __MTSP_MAIN_THREAD_CORE__);



      //===-------- Initialize the task graph manager ----------===//
      __mtsp_initializeTaskGraph();

      }

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {

#ifdef MTSP_WORKSTEALING_CT
  /// The CT is trying to submit work but the queue is full. The CT will then
  /// spend some time executing tasks
  if (submissionQueue.cur_load() > submissionQueue.cont_load()) {
    steal_from_single_run_queue(true);
  }
#endif

  // This is a global counter for we identify the task across the whole prog. execution
  newTask->metadata->globalTaskId = __mtsp_globalTaskCounter++;

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

void __mtsp_RuntimeWorkSteal() {
  kmp_task* taskToExecute = nullptr;

  /// Counter for the total cycles spent per task
  unsigned long long start=0, end=0;


  while (RunQueue.cur_load() > RunQueue.cont_load()) {
    if (RunQueue.try_deq(&taskToExecute)) {

#ifdef MEASURE_TASK_SIZE
      start = beg_read_mtsp();
#endif

      /// Start execution of the task
      (*(taskToExecute->routine))(0, taskToExecute);

#ifdef MEASURE_TASK_SIZE
      end = end_read_mtsp();
#endif

#ifdef MEASURE_TASK_SIZE
      taskToExecute->metadata->cycles_execution = (end - start);
#endif

#ifdef MEASURE_RETIREMENT
      start = beg_read_mtsp();
#endif
      tasksExecutedByRT++;

      /// Inform that this task has finished execution
      RetirementQueue.enq(taskToExecute);
#ifdef MEASURE_RETIREMENT
      end = end_read_mtsp();
      taskToExecute->metadata->cycles_retirement = (end - start);
#endif
    }
  }

}

void* __mtsp_RuntimeThreadCode(void* params) {
  stick_this_thread_to_core("RuntimeThread", __MTSP_RUNTIME_THREAD_CORE__);
  kmp_task* task = nullptr;
  kmp_uint64 iterations = 0;
  kmp_uint64 BatchSize = (64 - 1);	// should be 2^N - 1

#ifdef MEASURE_RETIREMENT
  kmp_uint32 retiring_task_id;
#endif

  while (true) {
    /// Check if the execution of a task has been completed
#ifdef MEASURE_RETIREMENT
    kmp_uint64 start = beg_read_mtsp();
#endif
    if (RetirementQueue.try_deq(&task)) {
#ifdef MEASURE_RETIREMENT
      retiring_task_id = task->metadata->globalTaskId;
      taskMetadata[retiring_task_id].cycles_execution = task->metadata->cycles_execution;
      taskMetadata[retiring_task_id].cycles_addition = task->metadata->cycles_addition;
      taskMetadata[retiring_task_id].cycles_allocation = task->metadata->cycles_allocation;
#endif
      removeFromTaskGraph(task);

#ifdef MEASURE_RETIREMENT
      kmp_uint64 end = end_read_mtsp();
      taskMetadata[retiring_task_id].cycles_retirement += (end - start);
#endif

#ifdef MEASURE_TASK_SIZE
      if (retiring_task_id >= MAXIMUM_EXPECTED_TASKS) {
        printf("Critical: Maximum_expected_tasks reached at %s:%d\n", __FILE__, __LINE__);
        exit(-1);
      }
#endif
    }

    /// Check if there is any request for new thread creation
#ifdef MEASURE_ADDITION
    kmp_uint64 start_add = beg_read_mtsp();
#endif
    if (freeSlots[0] > 0 && submissionQueue.try_deq(&task)) {
      addToTaskGraph(task);
#ifdef MEASURE_ADDITION
    kmp_uint64 end_add = end_read_mtsp();
    task->metadata->cycles_addition += (end_add - start_add);
#endif
    }

    iterations++;
    if ((iterations & BatchSize) == 0) {
      submissionQueue.fsh();
#ifdef MTSP_WORKSTEALING_RT
      if ((kmp_uint32) RunQueue.cur_load() > __mtsp_numWorkerThreads) {		// may be we should consider the CT also
        __mtsp_RuntimeWorkSteal();
      }
#endif
    }
  }

  return 0;
}





