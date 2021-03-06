/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_has_priv --
 *	Return if the process has special privileges.
 */
int
__wt_has_priv(void)
{
	return (getuid() == 0 ? 1 : 0);
}
