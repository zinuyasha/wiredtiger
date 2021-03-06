/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

#ifdef HAVE_DIAGNOSTIC
/*
 * __wt_session_dump_all --
 *	Dump information about all open sessions.
 */
void
__wt_session_dump_all(WT_SESSION_IMPL *session)
{
	WT_SESSION_IMPL **tp;

	if (session == NULL)
		return;

	for (tp = S2C(session)->sessions; *tp != NULL; ++tp)
		__wt_session_dump(*tp);
}

/*
 * __wt_session_dump --
 *	Dump information about a session.
 */
void
__wt_session_dump(WT_SESSION_IMPL *session)
{
	WT_CONNECTION_IMPL *conn;
	WT_CURSOR *cursor;
	WT_HAZARD *hp;
	int first;

	conn = S2C(session);

	__wt_msg(session, "session: %s%s%p",
	    session->name == NULL ? "" : session->name,
	    session->name == NULL ? "" : " ", session);

	first = 0;
	TAILQ_FOREACH(cursor, &session->cursors, q) {
		if (++first == 1)
			__wt_msg(session, "\tcursors:");
		__wt_msg(session, "\t\t%p", cursor);
	}

	first = 0;
	for (hp = session->hazard;
	    hp < session->hazard + conn->hazard_size; ++hp) {
		if (hp->page == NULL)
			continue;
		if (++first == 1)
			__wt_msg(session, "\thazard references:");
#ifdef HAVE_DIAGNOSTIC
		__wt_msg(session,
		    "\t\t%p (%s, line %d)", hp->page, hp->file, hp->line);
#else
		__wt_msg(session, "\t\t%p", hp->page);
#endif
	}
}
#endif
