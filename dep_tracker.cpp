#include "dep_tracker.h"
#include "task_graph.h"


std::map<kmp_intptr, std::pair<kmp_int32, std::vector<kmp_uint32>>> dependenceTable;

void releaseDependencies(kmp_uint16 idOfFinishedTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
//	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Releasing_Dependences);

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
				if (hashValue.first == idOfFinishedTask) {
					hashValue.first = -1;
				}
			}
			else {
//				__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Releasing_Dep_Reader);

				for (int idxPos=0; idxPos<hashValue.second.size(); idxPos++) {
					if (hashValue.second[idxPos] == idOfFinishedTask) {
						hashValue.second.erase(hashValue.second.begin() + idxPos);
						break;
					}
				}

//				__itt_task_end(__itt_mtsp_domain);
			}

			/// If that address does not have more producers/writers we remove it from the hash
			/// otherwise we just save the updated information
			if (hashValue.first == -1 && hashValue.second.size() == 0)
				dependenceTable.erase(hashEntry);
			else
				dependenceTable[baseAddr] = hashValue;
		}
	}

//	__itt_task_end(__itt_mtsp_domain);
}

kmp_uint64 checkAndUpdateDependencies(kmp_uint16 newTaskId, kmp_uint32 ndeps, kmp_depend_info* depList) {
	kmp_uint64 depCounter = 0;

	/// Iterate over each dependence
	for (kmp_uint32 depIdx=0; depIdx<ndeps; depIdx++) {
		auto baseAddr 	= depList[depIdx].base_addr;
		auto hashEntry 	= dependenceTable.find(baseAddr);
		bool isInput	= depList[depIdx].flags.in;
		bool isOutput	= depList[depIdx].flags.out;

		/// <ID_OF_LAST_WRITER, IDS_OF_CURRENT_READERS>
		std::pair<kmp_int32, std::vector<kmp_uint32>> hashValue;

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
				if (hashValue.second.size() != 0) {

					/// The new task become dependent on all previous readers
					for (auto& idReader : hashValue.second) {
						/// If the new task does not already depends on the reader, it now becomes dependent
						if (whoIDependOn[newTaskId][idReader] == false) {
							whoIDependOn[newTaskId][idReader] = true;
							depCounter++;

							dependents[idReader][0]++;
							dependents[idReader][ dependents[idReader][0] ] = newTaskId;
						}
					}
				}
				else {
					kmp_uint32 lastWriterId = hashValue.first;

					/// If the new task does not already depends on the writer, it now becomes dependent
					if (whoIDependOn[newTaskId][lastWriterId] == false) {
						whoIDependOn[newTaskId][lastWriterId] = true;

						dependents[lastWriterId][0]++;
						dependents[lastWriterId][ dependents[lastWriterId][0] ] = newTaskId;
						depCounter++;
					}
				}

				hashValue.first = newTaskId;
				hashValue.second.clear();
			}
			else {
				/// If we are reading:
				///		- if there was a previous producer/writer the new task become dependent
				///		-		if not, the new task does not have dependences
				///		- is always added to the set of last readers
				if (hashValue.first != -1) {
					kmp_uint32 lastWriterId = hashValue.first;

					/// If the new task does not already depends on the writer, it now becomes dependent
					if (whoIDependOn[newTaskId][lastWriterId] == false) {
						whoIDependOn[newTaskId][lastWriterId] = true;

						dependents[lastWriterId][0]++;
						dependents[lastWriterId][ dependents[lastWriterId][0] ] = newTaskId;
						depCounter++;
					}
				}

				hashValue.second.push_back(newTaskId);
			}
		}
		else {/// Since there were no previous task acessing the addr
			  /// the newTask does not have any dependence regarding this param

			/// New task is writing to this address
			if (isOutput) {
				hashValue.first = newTaskId;
				hashValue.second.clear();
			}
			else {/// The new task is just reading from this address
				hashValue.first = -1;
				hashValue.second.push_back(newTaskId);
			}
		}

		dependenceTable[baseAddr] = hashValue;
	}

	return depCounter;
}
