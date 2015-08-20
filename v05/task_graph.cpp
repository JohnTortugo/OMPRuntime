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
kmp_uint64 			volatile	taskGraph[MAX_TASKS];
kmp_uint16 			volatile	freeSlots[MAX_TASKS + 1];
kmp_uint16 			volatile 	finishedIDS[MAX_TASKS + 1];
kmp_uint8 						idNextWorkerThread;

std::vector<int>				taskGraphSize;
std::vector<int>				runQueueSize;

const char colors[][20] = {	"blue", "maroon1", "yellow", "orange", "dodgerblue4", "dimgrey", "chartreuse4", "darkturquoise", "darkorange4", "gray19",
							"khaki4", "indigo", "limegreen", "plum4", "wheat4", "tomato4", "grey63", "grey100", "gray0", "antiquewhite4"};

void __mtsp_initializeTaskGraph() {
	for (int i=0; i<MAX_TASKS; i++) {
		tasks[i]		= nullptr;
		taskGraph[i] 	= 0;
		freeSlots[i+1] 	= i;
		__mtsp_NodeStatus[i] = 0;
		__mtsp_NodeColor[i] = 1;
	}

	freeSlots[0] 		= MAX_TASKS;
	idNextWorkerThread	= 0;

	/// also initialize the scheduler and dependence tracker
	__mtsp_initScheduler();
}

const char* getNodeColor(int nodeId) {
	int cor = __mtsp_NodeColor[nodeId] % 64;

	if (cor > 19)
		return "red";
	else
		return colors[cor];
}

void dumpDependenceGraphToDot(kmp_uint16 newTaskId, kmp_uint64 newDepPattern) {
	kmp_uint16 indegree[MAX_TASKS+1];
	std::queue<kmp_uint16> nexts;
	kmp_uint64 taskGraphCopy[MAX_TASKS];

	const unsigned long long int one = 1;
	static unsigned long long int time = 1;

	bool occupied[MAX_TASKS+1];
	char fileName[100];

//	/// We are debugging just the range of 0...1000
//	if (time < 0 || time > 1000) return ;
//
//	/// Number of nodes in the current task graph
//	printf("TaskGraphSize = %d\n", MAX_TASKS - freeSlots[0] + 1);

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

	/// Now we actually do the topological sort
	while (!nexts.empty()) {
		kmp_uint16 src = nexts.front(); nexts.pop();
		kmp_uint64 mask = ~((kmp_uint64)1 << src);

		fprintf(fp, "Node_%03d [shape=circle, style=filled, fillcolor=\"%s\"];\n", src, getNodeColor(src));

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

/// Returns the id of the worker thread that currently has the smaller number
/// of tasks in its run queue
kmp_uint16 threadWithSmallerRunQueue() {
	kmp_uint16 minSize = RunQueues[0].cur_load();
	kmp_uint16 minID = 0;

	for (kmp_uint16 i=1; i<__mtsp_numWorkerThreads; i++) {
		int sz = RunQueues[i].cur_load();

		if (sz < minSize) {
			minSize = sz;
			minID = i;
		}
	}

	return minID;
}

/// Implement a policy of round robin choosing a worker thread
kmp_uint16 roundRobinWorkerThread() {
	/// If we could assume that __mtsp_numWorkerThreads were a power of two we could evict the %
	idNextWorkerThread = (idNextWorkerThread + 1) % __mtsp_numWorkerThreads;
	return idNextWorkerThread;
}

/// Returns the next worker thread ID from the finishedIDS
kmp_uint16 threadNextFinishedToken() {
	kmp_uint16 threadID = finishedIDS[ finishedIDS[0] ];
	finishedIDS[0]--;

	return threadID;
}

/// Returns the ID of the next thread to receive a task
kmp_uint16 nextWorkerThread() {
#ifdef MTSP_WORK_DISTRIBUTION_RR
	return roundRobinWorkerThread();
#elif MTSP_WORK_DISTRIBUTION_FT
	return threadNextFinishedToken();
#elif MTSP_WORK_DISTRIBUTION_QS
	return threadWithSmallerRunQueue();
#else
	fprintf(stderr, "\x1B[31m" "***** NO POLICY FOR WORK DISTRIBUTION HAS BEEN SET [%s:%d]*****\n" "\x1B[0m", __FILE__, __LINE__);
	return -1;
#endif
}

void removeFromTaskGraph(kmp_task* finishedTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_TaskGraph_Del);

	kmp_uint16 idOfFinishedTask = finishedTask->metadata->taskgraph_slot_id;
	kmp_uint64 mask = ~((kmp_uint64)1 << idOfFinishedTask);

	/// Decrement the number of tasks in the system currently
	ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// Remove from the dependence checker the positions that this task owns
	releaseDependencies(idOfFinishedTask, finishedTask->metadata->ndeps, finishedTask->metadata->dep_list);

	/// Release the taskmetadata slot used
	if (finishedTask->metadata->metadata_slot_id >= 0) {
		__mtsp_taskMetadataStatus[finishedTask->metadata->metadata_slot_id] = false;
	}

	/// This slot is empty
	tasks[idOfFinishedTask] = nullptr;
	__mtsp_NodeColor[idOfFinishedTask] = 0;
	__mtsp_NodeStatus[idOfFinishedTask] = 0;

	freeSlots[0]++;
	freeSlots[freeSlots[0]] = idOfFinishedTask;

	/// Free the dependent tasks
	for (int slotId=0; slotId<MAX_TASKS; slotId++) {
		kmp_uint64 prev = taskGraph[slotId];
		taskGraph[slotId] &= mask;

		/// If the task now has 0 dependences.
		if (prev != 0 && taskGraph[slotId] == 0) {
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Enqueue);
#ifdef MTSP_MULTIPLE_RUN_QUEUES
	#ifndef MTSP_CRITICAL_PATH_PREDICTION
				RunQueues[ nextWorkerThread() ].enq( tasks[slotId] );
	#else
		int load = __mtsp_ColorVector[__mtsp_NodeColor[slotId]];

		if (load >= 16)
			PrioritizedRunQueues[0].enq( tasks[slotId] );
		else if (load >= 12)
			PrioritizedRunQueues[1].enq( tasks[slotId] );
		else if (load >= 8)
			PrioritizedRunQueues[2].enq( tasks[slotId] );
		else
			PrioritizedRunQueues[3].enq( tasks[slotId] );
	#endif
#else
			RunQueue.enq( tasks[slotId] );
#endif

#ifdef MTSP_DUMP_STATS
			runQueueSize.push_back(RunQueue.cur_load());
#endif

			__itt_task_end(__itt_mtsp_domain);
		}
	}

#ifdef MTSP_DUMP_STATS
	taskGraphSize.push_back(MAX_TASKS - freeSlots[0]);
#endif

	__itt_task_end(__itt_mtsp_domain);
}

void __mtsp_updateColors(kmp_uint16 newTaskId, kmp_uint64 corPattern) {
	/// iterate over all readers
	kmp_uint16 corIdx = 1;
	kmp_uint16 endCorIdx = 1;
	kmp_uint16 endCorVal = 1;

	kmp_uint64 prev = corPattern;

	/// while readers are different of zero then there is at least
	/// one bit set.
	while (corPattern) {
		/// If the current rightmost bit is set then "corIdx" contains
		/// the ID of a reader
		if (corPattern & 0x00000001) {
			int cor = __mtsp_ColorVectorIdx*MAX_TASKS + corIdx;

			__mtsp_ColorVector[cor]++;

			if (__mtsp_ColorVector[cor] > endCorVal) {
				endCorVal = __mtsp_ColorVector[cor];
				endCorIdx = cor;
			}
		}

		corIdx++;
		corPattern = corPattern >> 1;
	}

//	printf("The colorset of %02d is %02d, its color is %02d\n", newTaskId, prev, endCorIdx);

	__mtsp_NodeColor[newTaskId] = endCorIdx;
}

void addToTaskGraph(kmp_task* newTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_TaskGraph_Add);
	ACQUIRE(&lock_dependenceTable);

	kmp_uint32 ndeps = newTask->metadata->ndeps;
	kmp_depend_info* depList = newTask->metadata->dep_list;

	/// Obtain id for the new task
	kmp_uint16 newTaskId = freeSlots[ freeSlots[0]-- ];

	/// This will contain the ID of the colors of the new node parents
	kmp_uint64 corPattern = 1;

	/// depPattern stores a bit pattern representing the dependences of the new task
	kmp_uint64 depPattern = checkAndUpdateDependencies(newTaskId, ndeps, depList, corPattern);

	/// We also add one more children to the status of the reader
	__mtsp_NodeStatus[newTaskId] = 1;

	__mtsp_updateColors(newTaskId, corPattern);

	/// Store the task_id of this task
	newTask->metadata->taskgraph_slot_id = newTaskId;

	/// stores the new task dependence pattern
	taskGraph[newTaskId] = depPattern;

	//dumpDependenceGraphToDot(newTaskId, depPattern);
	RELEASE(&lock_dependenceTable);

	/// stores the pointer to the new task
	tasks[newTaskId] = newTask;

	/// if the task has depPattern == 0 then it may already be dispatched.
	if (depPattern == 0) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Enqueue);
#ifdef MTSP_MULTIPLE_RUN_QUEUES
	#ifndef MTSP_CRITICAL_PATH_PREDICTION
		RunQueues[ nextWorkerThread() ].enq( newTask );
	#else
		int load = __mtsp_ColorVector[__mtsp_NodeColor[newTaskId]];

		if (load >= 16)
			PrioritizedRunQueues[0].enq( newTask );
		else if (load >= 12)
			PrioritizedRunQueues[1].enq( newTask );
		else if (load >= 8)
			PrioritizedRunQueues[2].enq( newTask );
		else
			PrioritizedRunQueues[3].enq( newTask );
	#endif
#else
		RunQueue.enq( newTask );
#endif

#ifdef MTSP_DUMP_STATS
		runQueueSize.push_back(RunQueue.cur_load());
#endif
		__itt_task_end(__itt_mtsp_domain);
	}

#ifdef MTSP_DUMP_STATS
	taskGraphSize.push_back(MAX_TASKS - freeSlots[0]);
#endif


	__itt_task_end(__itt_mtsp_domain);
}
