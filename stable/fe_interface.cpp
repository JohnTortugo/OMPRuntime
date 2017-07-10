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

SPSCQueue<kmp_task*, SUBMISSION_QUEUE_SIZE, SUBMISSION_QUEUE_BATCH_SIZE, SUBMISSION_QUEUE_CF> submissionQueue;

void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...) {
  int i       = 0;
  int tid     = 0;
    void** argv   = (void **) malloc(sizeof(void *) * argc);
    void** argvcp   = argv;
    va_list ap;

    /// Check whether the runtime library is initialized
    ACQUIRE(&__mtsp_lock_initialized);
    if (__mtsp_initialized == false) {
      /// Init random number generator. We use this on the work stealing part.
      srandom( time(NULL) );

      __mtsp_initialized = true;
      __mtsp_Single = false;

      //===-------- The thread that initialize the runtime is the Control Thread ----------===//
      __mtsp_initialize();
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

kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
    kmp_uint32 shareds_offset  = sizeof(kmp_taskdata) + sizeof_kmp_task_t;
    kmp_int32 sizeOfMetadata = sizeof(mtsp_task_metadata);
    kmp_int16 memorySlotId = -1;

    kmp_taskdata* taskdata = (kmp_taskdata*) malloc(shareds_offset + sizeof_shareds + sizeOfMetadata);

    kmp_task* task = KMP_TASKDATA_TO_TASK(taskdata);

    task->shareds  = (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
    task->routine  = task_entry;
    task->metadata = (_mtsp_task_metadata*) &((char *) taskdata)[shareds_offset + sizeof_shareds];
    task->metadata->globalTaskId = -1;
    task->metadata->metadata_slot_id = memorySlotId;

    return task;
}

kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list) {
  __mtsp_addNewTask(new_task, ndeps, dep_list);

  return 0;
}

kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
    /// Flush the current state of the submission queues..
    submissionQueue.fsh();

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

  return 0;
}

void __kmpc_barrier(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("***************** Executando uma barreira.\n");
#endif
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
  /// Flush the current state of the submission queues..
  submissionQueue.fsh();

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
