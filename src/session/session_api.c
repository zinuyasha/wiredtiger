/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __session_close --
 *	WT_SESSION->close method.
 */
static int
__session_close(WT_SESSION *wt_session, const char *config)
{
	WT_BTREE_SESSION *btree_session;
	WT_CONNECTION_IMPL *conn;
	WT_CURSOR *cursor;
	WT_SESSION_IMPL *session, **tp;
	int ret;

	conn = (WT_CONNECTION_IMPL *)wt_session->connection;
	session = (WT_SESSION_IMPL *)wt_session;

	SESSION_API_CALL(session, close, config, cfg);
	WT_UNUSED(cfg);

	while ((cursor = TAILQ_FIRST(&session->cursors)) != NULL)
		WT_TRET(cursor->close(cursor));

	while ((btree_session = TAILQ_FIRST(&session->btrees)) != NULL)
		WT_TRET(__wt_session_remove_btree(session, btree_session, 0));

	WT_TRET(__wt_schema_close_tables(session));

	__wt_spin_lock(session, &conn->spinlock);

	/* Discard scratch buffers. */
	__wt_scr_discard(session);

	/* Confirm we're not holding any hazard references. */
	__wt_hazard_empty(session);

	/* Free the reconciliation information. */
	__wt_rec_destroy(session);

	/* Free the eviction exclusive-lock information. */
	__wt_free(session, session->excl);

	/* Destroy the thread's mutex. */
	if (session->cond != NULL)
		(void)__wt_cond_destroy(session, session->cond);

	/*
	 * Replace the session reference we're closing with the last entry in
	 * the table, then clear the last entry.  As far as the walk of the
	 * server threads is concerned, it's OK if the session appears twice,
	 * or if it doesn't appear at all, so these lines can race all they
	 * want.
	 */
	for (tp = conn->sessions; *tp != session; ++tp)
		;
	--conn->session_cnt;
	*tp = conn->sessions[conn->session_cnt];
	conn->sessions[conn->session_cnt] = NULL;

	/*
	 * Publish, making the session array entry available for re-use.  There
	 * must be a barrier here to ensure the cleanup above completes before
	 * the entry is re-used.
	 */
	WT_PUBLISH(session->iface.connection, NULL);

	session = &conn->default_session;
	__wt_spin_unlock(session, &conn->spinlock);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_open_cursor --
 *	WT_SESSION->open_cursor method.
 */
static int
__session_open_cursor(WT_SESSION *wt_session,
    const char *uri, WT_CURSOR *to_dup, const char *config, WT_CURSOR **cursorp)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;
	SESSION_API_CALL(session, open_cursor, config, cfg);

	if (uri != NULL && to_dup != NULL)
		WT_ERR_MSG(session, EINVAL,
		    "should be passed either a URI or a cursor, but not both");

	if (to_dup != NULL)
		ret = __wt_cursor_dup(session, to_dup, config, cursorp);
	else if (WT_PREFIX_MATCH(uri, "colgroup:"))
		ret = __wt_curfile_open(session, uri, cfg, cursorp);
	else if (WT_PREFIX_MATCH(uri, "config:"))
		ret = __wt_curconfig_open(session, uri, cfg, cursorp);
	else if (WT_PREFIX_MATCH(uri, "file:"))
		ret = __wt_curfile_open(session, uri, cfg, cursorp);
	else if (WT_PREFIX_MATCH(uri, "index:"))
		ret = __wt_curindex_open(session, uri, cfg, cursorp);
	else if (WT_PREFIX_MATCH(uri, "statistics:"))
		ret = __wt_curstat_open(session, uri, cfg, cursorp);
	else if (WT_PREFIX_MATCH(uri, "table:"))
		ret = __wt_curtable_open(session, uri, cfg, cursorp);
	else {
		__wt_err(session, EINVAL, "Unknown cursor type '%s'", uri);
		ret = EINVAL;
	}

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_create --
 *	WT_SESSION->create method.
 */
static int
__session_create(WT_SESSION *wt_session, const char *name, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;
	SESSION_API_CALL(session, create, config, cfg);
	WT_UNUSED(cfg);
	WT_ERR(__wt_schema_create(session, name, config));

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_rename --
 *	WT_SESSION->rename method.
 */
static int
__session_rename(WT_SESSION *wt_session,
    const char *uri, const char *newname, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;
	SESSION_API_CALL(session, rename, config, cfg);
	ret = __wt_schema_rename(session, uri, newname, cfg);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_drop --
 *	WT_SESSION->drop method.
 */
static int
__session_drop(WT_SESSION *wt_session, const char *name, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;
	SESSION_API_CALL(session, drop, config, cfg);
	ret = __wt_schema_drop(session, name, cfg);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_dumpfile --
 *	WT_SESSION->dumpfile method.
 */
static int
__session_dumpfile(WT_SESSION *wt_session, const char *uri, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;
	SESSION_API_CALL(session, dumpfile, config, cfg);
	ret = __wt_schema_worker(session, uri, cfg,
	    __wt_dumpfile, WT_BTREE_EXCLUSIVE | WT_BTREE_VERIFY);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_salvage --
 *	WT_SESSION->salvage method.
 */
static int
__session_salvage(WT_SESSION *wt_session, const char *uri, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;

	SESSION_API_CALL(session, salvage, config, cfg);
	ret = __wt_schema_worker(session, uri, cfg,
	    __wt_salvage, WT_BTREE_EXCLUSIVE | WT_BTREE_SALVAGE);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_sync --
 *	WT_SESSION->sync method.
 */
static int
__session_sync(WT_SESSION *wt_session, const char *uri, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;

	SESSION_API_CALL(session, sync, config, cfg);
	ret = __wt_schema_worker(session, uri, cfg, __wt_btree_sync, 0);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_truncate --
 *	WT_SESSION->truncate method.
 */
static int
__session_truncate(WT_SESSION *wt_session,
    const char *uri, WT_CURSOR *start, WT_CURSOR *stop, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;

	SESSION_API_CALL(session, truncate, config, cfg);
	/*
	 * If the URI is specified, we don't need a start/stop, if start/stop
	 * is specified, we don't need a URI.
	 *
	 * If no URI is specified, and both cursors are specified, start/stop
	 * must reference the same object.
	 *
	 * Any specified cursor must have been initialized.
	 */
	if ((uri == NULL && start == NULL && stop == NULL) ||
	    (uri != NULL && (start != NULL || stop != NULL)))
		WT_ERR_MSG(session, EINVAL,
		    "the truncate method should be passed either a URI or "
		    "start/stop cursors, but not both");
	if (start != NULL && stop != NULL && strcmp(start->uri, stop->uri) != 0)
		WT_ERR_MSG(session, EINVAL,
		    "truncate method cursors must reference the same object");
	if ((start != NULL && !F_ISSET(start, WT_CURSTD_KEY_SET)) ||
	    (stop != NULL && !F_ISSET(stop, WT_CURSTD_KEY_SET)))
		WT_ERR_MSG(session, EINVAL,
		    "the truncate method cursors must have their keys set");

	if (uri == NULL) {
		/*
		 * From a starting/stopping cursor to the begin/end of the
		 * object is easy, walk the object.
		 */
		if (start == NULL)
			for (;;) {
				WT_ERR(stop->remove(stop));
				if ((ret = stop->prev(stop)) != 0) {
					if (ret == WT_NOTFOUND)
						ret = 0;
					break;
				}
			}
		else
			for (;;) {
				WT_ERR(start->remove(start));
				if (stop != NULL &&
				    start->equals(start, stop))
					break;
				if ((ret = start->next(start)) != 0) {
					if (ret == WT_NOTFOUND)
						ret = 0;
					break;
				}
			}
	} else
		ret = __wt_schema_truncate(session, uri, cfg);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_upgrade --
 *	WT_SESSION->upgrade method.
 */
static int
__session_upgrade(WT_SESSION *wt_session, const char *uri, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;

	SESSION_API_CALL(session, upgrade, config, cfg);
	ret = __wt_schema_worker(session, uri, cfg,
	    __wt_upgrade, WT_BTREE_EXCLUSIVE | WT_BTREE_UPGRADE);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_verify --
 *	WT_SESSION->verify method.
 */
static int
__session_verify(WT_SESSION *wt_session, const char *uri, const char *config)
{
	WT_SESSION_IMPL *session;
	int ret;

	session = (WT_SESSION_IMPL *)wt_session;

	SESSION_API_CALL(session, verify, config, cfg);
	ret = __wt_schema_worker(session, uri, cfg,
	    __wt_verify, WT_BTREE_EXCLUSIVE | WT_BTREE_VERIFY);

err:	API_END_NOTFOUND_MAP(session, ret);
}

/*
 * __session_begin_transaction --
 *	WT_SESSION->begin_transaction method.
 */
static int
__session_begin_transaction(WT_SESSION *wt_session, const char *config)
{
	WT_UNUSED(wt_session);
	WT_UNUSED(config);

	return (ENOTSUP);
}

/*
 * __session_commit_transaction --
 *	WT_SESSION->commit_transaction method.
 */
static int
__session_commit_transaction(WT_SESSION *wt_session, const char *config)
{
	WT_UNUSED(wt_session);
	WT_UNUSED(config);

	return (ENOTSUP);
}

/*
 * __session_rollback_transaction --
 *	WT_SESSION->rollback_transaction method.
 */
static int
__session_rollback_transaction(WT_SESSION *wt_session, const char *config)
{
	WT_UNUSED(wt_session);
	WT_UNUSED(config);

	return (ENOTSUP);
}

/*
 * __session_checkpoint --
 *	WT_SESSION->checkpoint method.
 */
static int
__session_checkpoint(WT_SESSION *wt_session, const char *config)
{
	WT_UNUSED(wt_session);
	WT_UNUSED(config);

	return (ENOTSUP);
}

/*
 * __session_msg_printf --
 *	WT_SESSION->msg_printf method.
 */
static int
__session_msg_printf(WT_SESSION *wt_session, const char *fmt, ...)
{
	WT_SESSION_IMPL *session;
	va_list ap;

	session = (WT_SESSION_IMPL *)wt_session;

	va_start(ap, fmt);
	__wt_msgv(session, fmt, ap);
	va_end(ap);

	return (0);
}

/*
 * __wt_open_session --
 *	Allocate a session handle.  The internal parameter is used for sessions
 *	opened by WiredTiger for its own use.
 */
int
__wt_open_session(WT_CONNECTION_IMPL *conn, int internal,
    WT_EVENT_HANDLER *event_handler, const char *config,
    WT_SESSION_IMPL **sessionp)
{
	static WT_SESSION stds = {
		NULL,
		__session_close,
		__session_open_cursor,
		__session_create,
		__session_drop,
		__session_rename,
		__session_salvage,
		__session_sync,
		__session_truncate,
		__session_upgrade,
		__session_verify,
		__session_begin_transaction,
		__session_commit_transaction,
		__session_rollback_transaction,
		__session_checkpoint,
		__session_dumpfile,
		__session_msg_printf
	};
	WT_SESSION_IMPL *session, *session_ret;
	uint32_t slot;
	int ret;

	WT_UNUSED(config);
	ret = 0;
	session = &conn->default_session;
	session_ret = NULL;

	__wt_spin_lock(session, &conn->spinlock);

	/* Check to see if there's an available session slot. */
	if (conn->session_cnt == conn->session_size - 1)
		WT_ERR_MSG(session, WT_ERROR,
		    "WiredTiger only configured to support %d thread contexts",
		    conn->session_size);

	/*
	 * The session reference list is compact, the session array is not.
	 * Find the first empty session slot.
	 */
	for (slot = 0, session_ret = conn->session_array;
	    session_ret->iface.connection != NULL;
	    ++session_ret, ++slot)
		;

	/* Session entries are re-used, clear the old contents. */
	WT_CLEAR(*session_ret);

	WT_ERR(__wt_cond_alloc(session, "session", 1, &session_ret->cond));
	session_ret->iface = stds;
	session_ret->iface.connection = &conn->iface;
	WT_ASSERT(session, session->event_handler != NULL);
	session_ret->event_handler = session->event_handler;
	session_ret->hazard = conn->hazard + slot * conn->hazard_size;

	TAILQ_INIT(&session_ret->cursors);
	TAILQ_INIT(&session_ret->btrees);
	if (event_handler != NULL)
		session_ret->event_handler = event_handler;

	/*
	 * Public sessions are automatically closed during WT_CONNECTION->close.
	 * If the session handles for internal threads were to go on the public
	 * list, there would be complex ordering issues during close.  Set a
	 * flag to avoid this: internal sessions are not closed automatically.
	 */
	if (internal)
		F_SET(session_ret, WT_SESSION_INTERNAL);

	/*
	 * Publish: make the entry visible to server threads.  There must be a
	 * barrier to ensure the structure fields are set before any other
	 * thread can see the session.
	 */
	WT_PUBLISH(conn->sessions[conn->session_cnt++], session_ret);

	STATIC_ASSERT(offsetof(WT_CONNECTION_IMPL, iface) == 0);
	*sessionp = session_ret;

err:	__wt_spin_unlock(session, &conn->spinlock);
	return (ret);
}
