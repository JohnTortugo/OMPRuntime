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

void dumpDependenceGraphToDot(kmp_uint16 newTaskId=0, kmp_uint64 newDepPattern=0) {
	kmp_uint16 indegree[MAX_TASKS+1];
	std::queue<kmp_uint16> nexts;
	kmp_uint64 taskGraphCopy[MAX_TASKS];

	const unsigned long long int one = 1;
	static unsigned long long int time = 1;

	bool occupied[MAX_TASKS+1];
	char fileName[100];

	/// We are debugging just the range of 0...1000
	if (time < 0 || time > 1000) return ;

	/// Number of nodes in the current task graph
	printf("TaskGraphSize = %d\n", MAX_TASKS - freeSlots[0] + 1);

	/// every position is already occupied
	/// everybody has indegree of zero at the beginning
	for (int i=0; i<=MAX_TASKS; i++) {
		occupied[i] = true;
		indegree[i] = 0;
		taskGraphCopy[i-1] = taskGraph[i-1];
	}

	for (int i=1; i<=freeSlots[0]; i++) {
		occupied[ freeSlots[i] ] = false;
	}

	/// Calculate in-degree
	for (int i=0; i<MAX_TASKS; i++) {
		/// If that position is not occupied ignore it
		if (!occupied[i]) continue;

		for (int j=0; j<MAX_TASKS; j++) {
			bool hasEdge = (taskGraph[i] & (one << j));

			if (hasEdge) indegree[i]++;
		}
	}

	/// The first guys who has indegree zero
	for (int i=0; i<MAX_TASKS; i++) {
		/// If there is really a task at position i and it has indegree zero
		if (occupied[i] && indegree[i] == 0)
			nexts.push(i);
	}

	/// The "dot" file where we are going to write the graph
	sprintf(fileName, "graph_at_%04llu.dot", time);

	FILE* fp = fopen(fileName, "w+");

	if (!fp) {
		fprintf(stderr, "It was impossible to write the dependenceGraph to a dot file [%s].\n", fileName);
		return ;
	}

	fprintf(fp, "digraph G {\n");
	fprintf(fp, "rank=min;\n");
	fprintf(fp, "//NewTaskGraph[%d] = %llu\n", newTaskId, newDepPattern);

	/// Now we actually do the topological sort
	while (!nexts.empty()) {
		kmp_uint16 src = nexts.front(); nexts.pop();
		kmp_uint64 mask = ~((kmp_uint64)1 << src);

		fprintf(fp, "Node_%03d [shape=circle];\n", src);

		/// See how depends on the task that finished
		for (int slotId=0; slotId<MAX_TASKS; slotId++) {
			bool hasEdge = (taskGraphCopy[slotId] & (one << src));
			taskGraphCopy[slotId] &= mask;

			if (hasEdge) {
				fprintf(fp, "Node_%03d -> Node_%03d;\n", src, slotId);

				if (taskGraphCopy[slotId] == 0) {
					nexts.push(slotId);
				}
			}
		}
	}

	fprintf(fp, "}\n");
	fclose(fp);
	time++;
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

void removeFromTaskGraph(kmp_uint16 idOfFinishedTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_TaskGraph_Del);

	kmp_uint64 mask = ~((kmp_uint64)1 << idOfFinishedTask);

	/// Decrement the number of tasks in the system currently
	ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// Release the taskmetadata slot used
	if (tasks[idOfFinishedTask]->part_id >= 0) {
		__mtsp_taskMetadataStatus[tasks[idOfFinishedTask]->part_id] = false;
		//printf("Releasing position %d\n", tasks[idOfFinishedTask]->part_id);
	}

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

			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Enqueue);
			/// Check if there is any request for new thread creation
			ACQUIRE(&lock_readySlots);
			readySlots[0]++;
			readySlots[readySlots[0]] = slotId;
			RELEASE(&lock_readySlots);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	//dumpDependenceGraphToDot();
	//printf("newReleases = %llu\n", newReleases);

	__itt_task_end(__itt_mtsp_domain);
}

void addToTaskGraph(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_TaskGraph_Add);
	ACQUIRE(&lock_dependenceTable);

	/// Obtain id for the new task
	kmp_uint16 newTaskId = freeSlots[ freeSlots[0]-- ];

	/// depPattern stores a bit pattern representing the dependences of the new task
	kmp_uint64 depPattern = checkAndUpdateDependencies(newTaskId, ndeps, depList);

	/// stores the new task dependence pattern
	taskGraph[newTaskId] = depPattern;
//	printf("TaskGraphSize = %d\n", MAX_TASKS - freeSlots[0] + 1);

	//dumpDependenceGraphToDot(newTaskId, depPattern);
	RELEASE(&lock_dependenceTable);

	/// stores the pointer to the new task
	tasks[newTaskId] 				= newTask;
	tasksDeps[newTaskId]			= ndeps;
	tasksDepsPointers[newTaskId]	= depList;

	/// if the task has depPattern == 0 then it may already be dispatched.
	if (depPattern == 0) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Enqueue);
		ACQUIRE(&lock_readySlots);
		readySlots[0]++;
		readySlots[readySlots[0]] = newTaskId;
		RELEASE(&lock_readySlots);
		__itt_task_end(__itt_mtsp_domain);
	}

	__itt_task_end(__itt_mtsp_domain);
}
