/*-------------------------------------------------------------------------
 *
 * catalog_utils.h--
 *
 *
 *
 * Copyright (c) 1994, Regents of the University of California
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSER_FUNC_H
#define PARSER_FUNC_H

#include <nodes/nodes.h>
#include <nodes/pg_list.h>
#include <nodes/parsenodes.h>
#include <nodes/primnodes.h>
#include <parser/parse_func.h>
#include <parser/parse_node.h>

/*
 *	This structure is used to explore the inheritance hierarchy above
 *	nodes in the type tree in order to disambiguate among polymorphic
 *	functions.
 */
typedef struct _InhPaths
{
	int			nsupers;		/* number of superclasses */
	Oid			self;			/* this class */
	Oid		   *supervec;		/* vector of superclasses */
} InhPaths;

/*
 *	This structure holds a list of possible functions or operators that
 *	agree with the known name and argument types of the function/operator.
 */
typedef struct _CandidateList
{
	Oid		   *args;
	struct _CandidateList *next;
}		   *CandidateList;

extern Node *ParseFunc(ParseState *pstate, char *funcname, List *fargs,
	int *curr_resno);
extern Oid funcid_get_rettype(Oid funcid);
extern CandidateList func_get_candidates(char *funcname, int nargs);
extern bool can_coerce(int nargs, Oid *input_typeids, Oid *func_typeids);
extern int match_argtypes(int nargs,
				   Oid *input_typeids,
				   CandidateList function_typeids,
				   CandidateList *candidates);
extern Oid * func_select_candidate(int nargs,
						  Oid *input_typeids,
						  CandidateList candidates);
extern bool func_get_detail(char *funcname,
					int nargs,
					Oid *oid_array,
					Oid *funcid,	/* return value */
					Oid *rettype,	/* return value */
					bool *retset,	/* return value */
					Oid **true_typeids);
extern Oid ** argtype_inherit(int nargs, Oid *oid_array);
extern int findsupers(Oid relid, Oid **supervec);
extern Oid **genxprod(InhPaths *arginh, int nargs);
extern void make_arguments(int nargs,
				   List *fargs,
				   Oid *input_typeids,
				   Oid *function_typeids);

extern List *setup_tlist(char *attname, Oid relid);
extern List *setup_base_tlist(Oid typeid);
extern Node *ParseComplexProjection(ParseState *pstate,
						   char *funcname,
						   Node *first_arg,
						   bool *attisset);
extern void func_error(char *caller, char *funcname, int nargs, Oid *argtypes);

#endif							/* PARSE_FUNC_H */

