#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/time.h>
#include <queue>
#include <map>
#include <sstream>
#include <iomanip>


#include "kmp.h"
#include "task_graph.h"
#include "dep_tracker.h"
#include "scheduler.h"
#include "mtsp.h"

bool 					taskGraphInitialized = false;
kmp_task*	volatile	tasks[MAX_TASKS];
kmp_uint16 	volatile	depCounters[MAX_TASKS];
kmp_uint16 	volatile	dependents[MAX_TASKS][MAX_DEPENDENTS+1];
bool 		volatile	whoIDependOn[MAX_TASKS][MAX_DEPENDENTS+1];
kmp_uint16 	volatile	freeSlots[MAX_TASKS + 1];

void __mtsp_initializeTaskGraph() {
	for (int i=0; i<MAX_TASKS; i++) {
		tasks[i]			= nullptr;

		freeSlots[i+1] 		= i;

		depCounters[i]		= 0;
		dependents[i][0]	= 0;

		for (int j=1; j<=MAX_TASKS; j++) 
			whoIDependOn[i][j] = false;
	}

	freeSlots[0] 			= MAX_TASKS;

	/// also initialize the scheduler and dependence tracker
	__mtsp_initScheduler();
}

void removeFromTaskGraph(kmp_task* finishedTask) {
	kmp_uint16 idOfFinishedTask = finishedTask->metadata->taskgraph_slot_id;

	/// Decrement the number of tasks in the system currently
	ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// Release the dependent tasks
	int sz = dependents[idOfFinishedTask][0];
	for (int i=1; i<=sz; i++) {
		int depId = dependents[idOfFinishedTask][i];
		depCounters[depId]--;

		// We reset the dependent status of the dependent task
		// that is: depId no longer depends on idOfFinishedTask
		whoIDependOn[depId][idOfFinishedTask] = false;

		if (depCounters[depId] == 0) {
			RunQueue.enq( tasks[depId] );
		}
	}

	/// Remove from the dependence checker the positions that this task owns
	releaseDependencies(idOfFinishedTask, finishedTask->metadata->ndeps, finishedTask->metadata->dep_list);


	/// Release the taskmetadata slot used
	if (finishedTask->metadata->metadata_slot_id >= 0) {
		__mtsp_taskMetadataStatus[finishedTask->metadata->metadata_slot_id] = false;
	}

	/// This slot is empty
	dependents[idOfFinishedTask][0] = 0;
	depCounters[idOfFinishedTask] = 0;
	tasks[idOfFinishedTask] = nullptr;

	freeSlots[0]++;
	freeSlots[freeSlots[0]] = idOfFinishedTask;
}



void addToTaskGraph(kmp_task* newTask) {
	kmp_uint32 ndeps = newTask->metadata->ndeps;
	kmp_depend_info* depList = newTask->metadata->dep_list;

	/// Obtain id for the new task
	kmp_uint16 newTaskId = freeSlots[ freeSlots[0] ];
	freeSlots[0]--;

    /// Check the dependences of the new task and return the number of unique
    /// tasks that the new task depends upon
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
		RunQueue.enq( newTask );
	}
}

void dumpTaskGraphToDot() {
    static int TaskGraphID = 0;
    std::stringstream GraphName;
    GraphName << "TaskGraph_";
    GraphName << TaskGraphID;
    GraphName << ".dot";

    FILE* fp = fopen(GraphName.str().c_str(), "w+");
    TaskGraphID++;

    fprintf(fp, "digraph g {\n");

    for (int i=0; i<MAX_TASKS; i++) {
        bool found = false;

        for (int j=1; j<=freeSlots[0]; j++) {
            if (i == freeSlots[j]) {
                // this task id is free, so there is no task for it
                found = true;
                break;
            }
        }

        // If the task slot is free dont print the task
        if (found) continue;

        fprintf(fp, "\tN_%03d;\n", i);

        for (int j=1; j<=dependents[i][0]; j++) {
            fprintf(fp, "\tN_%03d -> N_%03d ;\n", i, dependents[i][j]);
        }
    }

    fprintf(fp, "\n}\n");

    fclose(fp);
}
