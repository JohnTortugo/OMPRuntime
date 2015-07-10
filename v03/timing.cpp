#include "kmp.h"
#include "timing.h"

/// Timer for measuring the time spent inside the function __kmpc_fork_call
MTSP_CREATE_TIMER(Fork_call);

/// Timer for measuring the time spent inside the function __kmpc_task_alloc
MTSP_CREATE_TIMER(Task_alloc);

/// Timer for measuring the time spent inside the function __kmpc_task_with_deps
MTSP_CREATE_TIMER(Task_with_deps);

/// Timer for measuring the time spent executing user tasks
MTSP_CREATE_TIMER(Task);

/// Timer for measuring the time spent inside the function DEBUG__kmpc_Fork_call
/// Just for testing linkage
MTSP_CREATE_TIMER(DEBUG_Fork_call);
