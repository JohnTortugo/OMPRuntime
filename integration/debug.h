#ifndef __MTSP_DEBUG_HEADER
	#define __MTSP_DEBUG_HEADER 1

	#include "kmp.h"

	void DEBUG_kmpc_fork_call(ident *loc, kmp_int32 argc);

	void DEBUG_kmpc_omp_task_alloc(ident *loc_ref, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry);

	void DEBUG_kmpc_omp_task_with_deps(ident *loc_ref, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info *dep_list, kmp_int32 ndeps_noalias, kmp_depend_info *noalias_dep_list);

	void DEBUG_kmpc_omp_taskwait(ident *loc, kmp_int32 gtid);
#endif
