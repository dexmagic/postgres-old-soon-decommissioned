/*-------------------------------------------------------------------------
 *
 * functions.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 */
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "nodes/parsenodes.h"
#include "utils/syscache.h"

extern Datum postquel_function(FunctionCallInfo fcinfo,
							   FunctionCachePtr fcache,
							   bool *isDone);

#endif	 /* FUNCTIONS_H */
