#include "zpcre.c"  // Just use a while.

// Get total number of children recursively.
// This is a tail-recursive function, so should never burst stack.
void
zget_children_summary(const zNodeInfo *zpNodeIf, _i *zpResOUT) {
// TEST: pass!
	if (NULL != zpNodeIf) {
		*zpResOUT += zpNodeIf->total;
	
		zNodeInfo *zp0NodeIf;
		for (_i i = 0; i < zpNodeIf->total; i++) {
			zp0NodeIf = zpNodeIf->pp_children[i];
			zget_children_summary(zp0NodeIf, zpResOUT);
		}
	}
}

/*
 * Generate inbound information from 'struct zNodeInfo'.
 * This is a tail-recursive function, so should never burst stack.
 * DO NOT forget to free memory.
 */
void
zgen_inbound_info(const zNodeInfo *zpNodeIf, zInboundInfo **zpCurInIf, const _i zVOffSet, const _i zEndMark) {
// TEST: pass!
	if (NULL != zpNodeIf) {
		_i zRes = 0;
		_i zMark = 0;
	
		// Self completed.
		zMem_C_Alloc((*zpCurInIf)->p_data, char, 1 + 4 + strlen(zpNodeIf->p_PkgName) + strlen(zpNodeIf->p_FuncName) + strlen(zpNodeIf->p_FuncDef));
	
		strcpy((*zpCurInIf)->p_data, zpNodeIf->p_PkgName);
		strcat((*zpCurInIf)->p_data, ".");
		strcat((*zpCurInIf)->p_data, zpNodeIf->p_FuncName);
		strcat((*zpCurInIf)->p_data, " => ");
		strcat((*zpCurInIf)->p_data, zpNodeIf->p_FuncDef);

		zget_children_summary(zpNodeIf, &zRes);
		(*zpCurInIf)->MarkEnd = zEndMark;
		(*zpCurInIf)->summary = zRes;
		(*zpCurInIf)->total = zpNodeIf->total;
		(*zpCurInIf)->vOffSet = 1 + zVOffSet;
	
		// Enter recursion.
		zMem_Alloc((*zpCurInIf)->pp_children, zInboundInfo *, zpNodeIf->total);
		for(_i i = 0;i < zpNodeIf->total; i++) {
			zMem_Alloc((*zpCurInIf)->pp_children[i], zInboundInfo, 1);
			zMark = (i == (zpNodeIf->total - 1)) ? 1 : 0;
			zgen_inbound_info(zpNodeIf->pp_children[i], &((*zpCurInIf)->pp_children[i]), (*zpCurInIf)->vOffSet, zMark);
		}
	}
}

// This is a tail-recursive function, so should never burst stack.
void
znew_get_final_res(zInboundInfo *zpPrevInboundIf, zInboundInfo ***zpppFinalResIfOUT) {
// TEST: pass!
	printf("LineNum: %d\n", zpPrevInboundIf->LineNum);
	(*zpppFinalResIfOUT)[zpPrevInboundIf->LineNum] = zpPrevInboundIf;

	_i zOffSet = 0;
	for (_i i = 0; i < zpPrevInboundIf->total; i++) {
		printf("%d\n", zOffSet);
		zpPrevInboundIf->pp_children[i]->LineNum = 1 + zpPrevInboundIf->LineNum + zOffSet;
		zOffSet += (1 + zpPrevInboundIf->pp_children[i]->summary);

		zpPrevInboundIf->pp_children[i]->EndMarkAllPrev = zpPrevInboundIf->EndMarkAllPrev;
		if (1 == zpPrevInboundIf->MarkEnd) {
			zSet_Bit(zpPrevInboundIf->pp_children[i]->EndMarkAllPrev, zpPrevInboundIf->pp_children[i]->vOffSet);
		}

		znew_get_final_res(zpPrevInboundIf->pp_children[i], zpppFinalResIfOUT);
	}
}

// Print out.
void
zprint_out(zInboundInfo **zppInboundIf, const _i zLen) {
// TEST: pass!
	for (_i i = 0; i < zLen; i++) {
		for (_ui j = 2; j <= zppInboundIf[i]->vOffSet; j++) {
			if (0 < zCheck_Bit(zppInboundIf[i]->EndMarkAllPrev, j)) {
				printf("    ");
			}
			else {
				printf("\342\224\202   "); // print: '│'
			}
		}

		if (0 < i) {
			if (1 == zppInboundIf[i]->MarkEnd) {
				printf("\342\224\224\342\224\200\342\224\200 "); // print: '└── '
			}
			else {
				printf("\342\224\234\342\224\200\342\224\200 "); // print: '├──  '
			}
		}
		printf("%s\n", zppInboundIf[i]->p_data);
	}
}
