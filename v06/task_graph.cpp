#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/time.h>
#include <queue>

#include "kmp.h"
#include "task_graph.h"
#include "dep_tracker.h"
#include "scheduler.h"
#include "mtsp.h"

bool 							taskGraphInitialized = false;
kmp_task*			volatile	tasks[MAX_TASKS];
kmp_uint16 			volatile	depCounters[MAX_TASKS];
kmp_uint16 			volatile	dependents[MAX_TASKS][MAX_DEPENDENTS+1];
kmp_uint16 			volatile	freeSlots[MAX_TASKS + 1];


void __mtsp_dumpTaskGraphToDot() {
	/// identify which nodes are present in the TG
	bool present[MAX_TASKS];

	/// at the begining everybody is present
	for (int i=0; i<MAX_TASKS; i++) present[i] = true;

	/// the slots that are free are not present
	for (int i=1; i<=freeSlots[0]; i++) present[freeSlots[i]] = false;

	/// Store the name of the task graph and its identifier
	char fileName[100];
	static int counter = 0;

	/// The "dot" file where we are going to write the graph
	sprintf(fileName, "taskgraph_%04d.dot", counter++);

	FILE* fp = fopen(fileName, "w+");

	if (!fp) { fprintf(stderr, "It was impossible to write the dependenceGraph to a dot file [%s].\n", fileName); exit(-1); }

	fprintf(fp, "digraph TaskGraph {\n");

	/// for each present node
	for (int src=0; src<MAX_TASKS; src++) { if (present[src]) {
		fprintf(fp, "Node_%03d [shape=circle];\n", src);

		for (int j=1; j<=dependents[src][0]; j++) {
			fprintf(fp, "Node_%03d -> Node_%03d;\n", src, dependents[src][j]);
		}
	}}

	fprintf(fp, "}\n");
	fclose(fp);

	printf("Taskgraph written to file %s\n", fileName);
}

void __mtsp_initializeTaskGraph() {
	for (int i=0; i<MAX_TASKS; i++) {
		tasks[i]			= nullptr;

		freeSlots[i+1] 		= i;

		depCounters[i]		= 0;
		dependents[i][0]	= 0;
	}

	freeSlots[0] 			= MAX_TASKS;

	/// also initialize the scheduler and dependence tracker
	__mtsp_initScheduler();
}



void removeFromTaskGraph(kmp_task* finishedTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Del_Task_From_TaskGraph);

	kmp_uint16 idOfFinishedTask = finishedTask->metadata->taskgraph_slot_id;

	/// Decrement the number of tasks in the system currently
	ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// Release the dependent tasks
	int sz = dependents[idOfFinishedTask][0];
	for (int i=1; i<=sz; i++) {
		int depId = dependents[idOfFinishedTask][i];
		depCounters[depId]--;

		if (depCounters[depId] == 0) {
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RunQueue_Enqueue);
			RunQueue.enq( tasks[depId] );
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	/// Remove from the dependence checker the positions that this task owns
	releaseDependencies(idOfFinishedTask, finishedTask->metadata->ndeps, finishedTask->metadata->dep_list);



	/// This slot is empty
	dependents[idOfFinishedTask][0] = 0;
	depCounters[idOfFinishedTask] = 0;
	tasks[idOfFinishedTask] = nullptr;

	freeSlots[0]++;
	freeSlots[freeSlots[0]] = idOfFinishedTask;

	/// Release the taskmetadata slot used
	if (finishedTask->metadata->metadata_slot_id >= 0) {
		__mtsp_taskMetadataStatus[finishedTask->metadata->metadata_slot_id] = false;
	}

	__itt_task_end(__itt_mtsp_domain);
}



void addToTaskGraph(kmp_task* newTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Add_Task_To_TaskGraph);

	kmp_uint32 ndeps = newTask->metadata->ndeps;
	kmp_depend_info* depList = newTask->metadata->dep_list;

	/// Obtain id for the new task
	kmp_uint16 newTaskId = freeSlots[ freeSlots[0] ];
	freeSlots[0]--;

	/// depPattern stores a bit pattern representing the dependences of the new task
	kmp_uint64 depCounter = checkAndUpdateDependencies(newTaskId, ndeps, depList);

	/// Store the task_id of this task
	newTask->metadata->taskgraph_slot_id = newTaskId;

	/// stores the new task dependence pattern
	depCounters[newTaskId] = depCounter;
	dependents[newTaskId][0] = 0;

	/// stores the pointer to the new task
	tasks[newTaskId] = newTask;

	/// if the task has depPattern == 0 then it may already be dispatched.
	if (depCounter == 0) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RunQueue_Enqueue);
		RunQueue.enq( newTask );
		__itt_task_end(__itt_mtsp_domain);
	}

	__itt_task_end(__itt_mtsp_domain);
}




