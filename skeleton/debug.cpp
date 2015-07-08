#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "kmp.h"
#include "timing.h"
#include "debug.h"

#if DEBUG_ENABLED

void DEBUG_kmpc_fork_call(ident *loc, kmp_int32 argc) {
	MTSP_TIME_START(DEBUG_Fork_call);
	fprintf(stderr, "Fork-call {\n");
	fprintf(stderr, "\tLoc: %s\n", loc->psource);
	fprintf(stderr, "\tArgument count: %d\n", argc);
    fprintf(stderr, "} // fork-call\n");
    MTSP_TIME_ACCUMULATE_AND_RESET(DEBUG_Fork_call);
}

void DEBUG_kmpc_omp_task_alloc(ident *loc_ref, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
	fprintf(stderr, "\tTask_alloc: {\n");
	fprintf(stderr, "\t\t loc_ref: %s\n", loc_ref->psource);
	fprintf(stderr, "\t\t gtid: %d\n", gtid);
	fprintf(stderr, "\t\t flags: %X\n", pflags);
	fprintf(stderr, "\t\t sz_task: %u\n", sizeof_kmp_task);
	fprintf(stderr, "\t\t sz_shrds: %u\n", sizeof_shareds);
	fprintf(stderr, "\t\t fn_entry: %p\n", task_entry);
	fprintf(stderr, "\t}\n");
}

void DEBUG_kmpc_omp_task_with_deps(ident *loc_ref, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info *dep_list, kmp_int32 ndeps_noalias, kmp_depend_info *noalias_dep_list) {
	int i = 0;

	fprintf(stderr, "\tTask_with_deps: {\n");
	fprintf(stderr, "\t\t loc_ref: %s\n", loc_ref->psource);
	fprintf(stderr, "\t\t flags: %X\n", loc_ref->flags);
	fprintf(stderr, "\t\t gtid: %X\n", gtid);
	fprintf(stderr, "\t\t task: { \n");
	fprintf(stderr, "\t\t\t routine: %p\n", new_task->routine);
	fprintf(stderr, "\t\t\t part_id: %d\n", new_task->part_id);
	fprintf(stderr, "\t\t\t shareds: %p\n", new_task->shareds);
	fprintf(stderr, "\t\t\t destructors: %p\n", new_task->destructors);
	fprintf(stderr, "\t\t }\n");
	fprintf(stderr, "\t\t deps<%d>: { \n", ndeps);
	for (i=0; i<ndeps; i++) {
		fprintf(stderr, "\t\t\t dep[%d] {\n", i);
		fprintf(stderr, "\t\t\t\t base: %p {\n", (void *) dep_list[i].base_addr);
		fprintf(stderr, "\t\t\t\t len: %lu {\n", dep_list[i].len);
		fprintf(stderr, "\t\t\t\t in: %d {\n", dep_list[i].flags.in);
		fprintf(stderr, "\t\t\t\t out: %d {\n", dep_list[i].flags.out);
		fprintf(stderr, "\t\t\t }\n");
	}
	fprintf(stderr, "\t\t }\n");
	fprintf(stderr, "\t\t noalias<%d>: { \n", ndeps_noalias);
	for (i=0; i<ndeps_noalias; i++) {
		fprintf(stderr, "\t\t\t dep[%d] {\n", i);
		fprintf(stderr, "\t\t\t\t base: %p\n", (void *) noalias_dep_list[i].base_addr);
		fprintf(stderr, "\t\t\t\t len: %lu\n", noalias_dep_list[i].len);
		fprintf(stderr, "\t\t\t\t in: %d\n", noalias_dep_list[i].flags.in);
		fprintf(stderr, "\t\t\t\t out: %d\n", noalias_dep_list[i].flags.out);
		fprintf(stderr, "\t\t\t }\n");
	}
	fprintf(stderr, "\t\t }\n");
	fprintf(stderr, "\t}\n");
}

void DEBUG_kmpc_omp_taskwait(ident *loc, kmp_int32 gtid) {
	fprintf(stderr, "\tTask_wait: {\n");
	fprintf(stderr, "\t\t loc_ref: %s\n", loc->psource);
	fprintf(stderr, "\t\t gtid: %d\n", gtid);
	fprintf(stderr, "\t}\n");
}

#endif
