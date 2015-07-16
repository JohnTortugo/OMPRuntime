#include "dep_tracker.h"

std::map<kmp_intptr, std::pair<kmp_uint32, kmp_uint64>> dependenceTable;
unsigned char lock_dependenceTable;


void __mtsp_initializeDepChecker() {
	lock_dependenceTable = false;
}

void releaseDependencies(kmp_uint16 idOfFinishedTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Releasing_Dependences);
	ACQUIRE(&lock_dependenceTable);

	/// This represents the bit-index corresponding to the ID of the finished task
	kmp_uint64 mask = ~((kmp_uint64)1 << idOfFinishedTask);

	/// Iterate over each dependence and check if the task ID of the last task accessing that address
	/// is the ID of the finished task. If it is remove that entry, otherwise do nothing.
	for (kmp_uint32 depIdx=0; depIdx<ndeps; depIdx++) {
		auto baseAddr 	= depList[depIdx].base_addr;
		auto hashEntry	= dependenceTable.find(baseAddr);

		/// Is someone already accessing this address?
		if (hashEntry != dependenceTable.end()) {
			auto hashValue = hashEntry->second;

			/// If the finished task was a writer we check to see if it is still the last writer
			///		if yes we clear the first field. If not we do nothing yet.
			/// If the finished task was a reader we disable that bit in the second field.
			if (depList[depIdx].flags.out) {
				if ((hashValue.first >> 16) == idOfFinishedTask) {
					hashValue.first = 0;
				}
			}
			else {
				hashValue.second &= mask;
			}

			/// If that address does not have more producers/writers we remove it from the hash
			/// otherwise we just save the updated information
			if (hashValue.first == 0 && hashValue.second == 0)
				dependenceTable.erase(hashEntry);
			else
				dependenceTable[baseAddr] = hashValue;
		}
	}

	RELEASE(&lock_dependenceTable);
	__itt_task_end(__itt_mtsp_domain);
}

kmp_uint64 checkAndUpdateDependencies(kmp_uint16 newTaskId, kmp_uint32 ndeps, kmp_depend_info* depList, kmp_uint64& corPattern) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Checking_Dependences);

	kmp_uint64 depPattern = 0;
	corPattern = 1;

	printf("Numero de deps = %d\n", ndeps);

	/// Iterate over each dependence
	for (kmp_uint32 depIdx=0; depIdx<ndeps; depIdx++) {
		auto baseAddr 	= depList[depIdx].base_addr;
		auto hashEntry 	= dependenceTable.find(baseAddr);
		bool isInput	= depList[depIdx].flags.in;
		bool isOutput	= depList[depIdx].flags.out;

		/// <ID_OF_LAST_WRITER, IDS_OF_CURRENT_READERS>
		std::pair<kmp_uint32, kmp_uint64> hashValue;

		printf("Checking base addr %x  --> %d\n", baseAddr, hashEntry != dependenceTable.end());

		/// Is someone already accessing this address?
		if (hashEntry != dependenceTable.end()) {
			hashValue = hashEntry->second;

			/// If the new task is writing it must:
			///		if there are previous readers
			///			become dependent of all previous readers
			///		if there is no previous reader
			///			become dependent of the last writer
			///		become the new "producer", i.e., the last task writing
			///		reset the readers from the last writing
			if (isOutput) {
				if (hashValue.second != 0) {
					depPattern = depPattern | hashValue.second;

					/// iterate over all readers
					kmp_uint64 readers = hashValue.second;
					kmp_uint16 readIdx = 0;

					/// while readers are different of zero then there is at least
					/// one bit set.
					while (readers) {
						/// If the current rightmost bit is set then "readIdx" contains
						/// the ID of a reader
						if (readers & 0x00000001) {
							/// If the "readIdx" is actually in the newest window...
							if ((__mtsp_NodeColor[readIdx] & 0x40)  == __mtsp_ColorVectorIdx)
								corPattern = corPattern | ((kmp_uint64)1 << __mtsp_NodeColor[readIdx]);

							/// We also add one more children to the status of the reader
							__mtsp_NodeStatus[readIdx]++;
						}

						readIdx++;
						readers = readers >> 1;
					}
				}
				else {
					kmp_uint16 lastWriterId = hashValue.first >> 16;
					depPattern = depPattern | ((kmp_uint64)1 << lastWriterId);

					/// If the "lastWriterId" is actually in the newest window...
					if ((__mtsp_NodeColor[lastWriterId] & 0x40)  == __mtsp_ColorVectorIdx)
						corPattern = corPattern | ((kmp_uint64)1 << __mtsp_NodeColor[lastWriterId]);

					/// We also add one more children to the status of the reader
					__mtsp_NodeStatus[lastWriterId]++;
				}

				hashValue.first  = ((kmp_uint32)newTaskId << 16) | ((kmp_uint16)isInput << 1) | isOutput;
				hashValue.second = 0;
			}
			else {
				/// If we are reading:
				///		- if there was a previous producer/writer the new task become dependent
				///		-		if not, the new task does not have dependences
				///		- is always added to the set of last readers
				if (hashValue.first != 0) {
					kmp_uint16 lastWriterId = hashValue.first >> 16;
					depPattern = depPattern | ((kmp_uint64)1 << lastWriterId);

					/// If the "lastWriterId" is actually in the newest window...
					if ((__mtsp_NodeColor[lastWriterId] & 0x40)  == __mtsp_ColorVectorIdx)
						corPattern = corPattern | ((kmp_uint64)1 << __mtsp_NodeColor[lastWriterId]);

					/// We also add one more children to the status of the reader
					__mtsp_NodeStatus[lastWriterId]++;
				}

				hashValue.second = hashValue.second | ((kmp_uint64)1 << newTaskId);
			}
		}
		else {/// Since there were no previous task acessing the addr
			  /// the newTask does not have any dependence regarding this param

			/// New task is writing to this address
			if (isOutput) {
				hashValue.first  = ((kmp_uint32)newTaskId << 16) | ((kmp_uint16)isInput << 1) | isOutput;
				hashValue.second = 0;
			}
			else {/// The new task is just reading from this address
				hashValue.first  = 0;
				hashValue.second = ((kmp_uint64)1 << newTaskId);
			}
		}

		dependenceTable[baseAddr] = hashValue;
	}

	printf("The dependence pattern is 0x%x\n", depPattern);

	__itt_task_end(__itt_mtsp_domain);
	return depPattern;
}
