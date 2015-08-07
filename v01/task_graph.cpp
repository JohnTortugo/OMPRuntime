#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/time.h>

#include "kmp.h"
#include "task_graph.h"
#include "dep_tracker.h"
#include "scheduler.h"
#include "mtsp.h"

bool 							taskGraphInitialized = false;
kmp_task*			volatile	tasks[MAX_TASKS];
kmp_uint32 			volatile 	tasksDeps[MAX_TASKS];
kmp_depend_info* 	volatile 	tasksDepsPointers[MAX_TASKS];
kmp_uint64 			volatile	taskGraph[MAX_TASKS];
kmp_uint16 			volatile	freeSlots[MAX_TASKS + 1];
kmp_uint16 			volatile	readySlots[MAX_TASKS + 1];
kmp_uint16 			volatile	finishedSlots[MAX_TASKS + 1];

unsigned char volatile lock_readySlots 	 = 0;
unsigned char volatile lock_finishedSlots = 0;

void __mtsp_initializeTaskGraph() {
	for (int i=0; i<MAX_TASKS; i++) {
		tasks[i]		= nullptr;
		taskGraph[i] 	= 0;
		freeSlots[i+1] 	= i;
	}

	freeSlots[0] 		= MAX_TASKS;
	readySlots[0] 		= 0;
	finishedSlots[0] 	= 0;

	/// also initialize the scheduler and dependence tracker
	__mtsp_initScheduler();
}

void removeFromTaskGraph(kmp_uint16 idOfFinishedTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Del_Task_From_TaskGraph);

	kmp_uint64 mask = ~((kmp_uint64)1 << idOfFinishedTask);

	/// Decrement the number of tasks in the system currently
	ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// This slot is empty
	tasks[idOfFinishedTask] = nullptr;
	freeSlots[0]++;
	freeSlots[freeSlots[0]] = idOfFinishedTask;

	releaseDependencies(idOfFinishedTask, tasksDeps[idOfFinishedTask], tasksDepsPointers[idOfFinishedTask]);

	kmp_uint64 newReleases = 0;

	for (int slotId=0; slotId<MAX_TASKS; slotId++) {
		kmp_uint64 prev = taskGraph[slotId];
		taskGraph[slotId] &= mask;

		/// If the task now has 0 dependences.
		if (prev != 0 && taskGraph[slotId] == 0) {
			newReleases++;

			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RunQueue_Enqueue);
			/// Check if there is any request for new thread creation
			ACQUIRE(&lock_readySlots);
			readySlots[0]++;
			readySlots[readySlots[0]] = slotId;
			RELEASE(&lock_readySlots);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	//printf("newReleases = %llu\n", newReleases);

	__itt_task_end(__itt_mtsp_domain);
}

void dumpDependenceMatrix() {
	const unsigned long long int one = 1;

	printf("    ");
	for (int i=0; i<MAX_TASKS; i++)
		printf("%2d ", i);
	printf("\n");

	printf("    ");
	for (int i=0; i<MAX_TASKS; i++)
		printf("---");
	printf("\n");

	for (int i=0; i<MAX_TASKS; i++) {
		printf("%2d) ", i);
		for (int j=0; j<MAX_TASKS; j++) {
			bool x = (taskGraph[i] & (one << j));
			printf("%s ", x?" 1":" -");
		}
		printf("\n");
	}
}

void addToTaskGraph(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Add_Task_To_TaskGraph);

	ACQUIRE(&lock_dependenceTable);

	/// Obtain id for the new task
	kmp_uint16 newTaskId = freeSlots[ freeSlots[0]-- ];
	//printf("TaskGraphSize = %d\n", MAX_TASKS - freeSlots[0] + 1);

	/// depPattern stores a bit pattern representing the dependences of the new task
	kmp_uint64 depPattern = checkAndUpdateDependencies(newTaskId, ndeps, depList);

	/// stores the new task dependence pattern
	taskGraph[newTaskId] = depPattern;
	RELEASE(&lock_dependenceTable);

	/// stores the pointer to the new task
	tasks[newTaskId] 				= newTask;
	tasksDeps[newTaskId]			= ndeps;
	tasksDepsPointers[newTaskId]	= depList;

	/// if the task has depPattern == 0 then it may already be dispatched.
	if (depPattern == 0) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RunQueue_Enqueue);
		ACQUIRE(&lock_readySlots);
		readySlots[0]++;
		readySlots[readySlots[0]] = newTaskId;
		RELEASE(&lock_readySlots);
		__itt_task_end(__itt_mtsp_domain);
	}

	__itt_task_end(__itt_mtsp_domain);
}
