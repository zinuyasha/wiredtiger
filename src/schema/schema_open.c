/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_schema_colgroup_name --
 *	Get the URI for a column group.  This is used for schema table lookups.
 *	The only complexity here is that simple tables (with a single column
 *	group) use a simpler naming scheme.
 */
int
__wt_schema_colgroup_name(WT_SESSION_IMPL *session,
    WT_TABLE *table, const char *cgname, size_t len, char **namebufp)
{
	const char *tablename;
	char *namebuf;
	size_t namesize;

	namebuf = *namebufp;
	tablename = table->name;
	(void)WT_PREFIX_SKIP(tablename, "table:");

	/* The primary filename is in the table config. */
	if (table->ncolgroups == 0) {
		namesize = strlen("colgroup:") + strlen(tablename) + 1;
		WT_RET(__wt_realloc(session, NULL, namesize, &namebuf));
		snprintf(namebuf, namesize, "colgroup:%s", tablename);
	} else {
		namesize = strlen("colgroup::") + strlen(tablename) + len + 1;
		WT_RET(__wt_realloc(session, NULL, namesize, &namebuf));
		snprintf(namebuf, namesize, "colgroup:%s:%.*s",
		    tablename, (int)len, cgname);
	}

	*namebufp = namebuf;
	return (0);
}

/*
 * __wt_schema_get_btree --
 *	Get the btree (into session->btree) for the named schema object
 *	(either a column group or an index).
 */
int
__wt_schema_get_btree(WT_SESSION_IMPL *session,
    const char *objname, size_t len, const char *cfg[], uint32_t flags)
{
	WT_ITEM uribuf;
	WT_CONFIG_ITEM cval;
	WT_CURSOR *cursor;
	const char *fileuri, *name, *objconf;
	int ret;

	cursor = NULL;
	WT_CLEAR(uribuf);

	name = objname;
	if (len != strlen(objname))
		WT_ERR(__wt_strndup(session, objname, len, &name));

	WT_ERR(__wt_schema_table_cursor(session, NULL, &cursor));
	cursor->set_key(cursor, name);
	WT_ERR(cursor->search(cursor));
	WT_ERR(cursor->get_value(cursor, &objconf));

	/* Get the filename from the schema table. */
	WT_ERR(__wt_config_getones(session, objconf, "filename", &cval));
	WT_ERR(__wt_buf_fmt(
	    session, &uribuf, "file:%.*s", (int)cval.len, cval.str));
	fileuri = uribuf.data;

	/* !!! Close the schema cursor first, this overwrites session->btree. */
	ret = cursor->close(cursor);
	cursor = NULL;
	if (ret != 0)
		goto err;

	ret = __wt_session_get_btree(session, name, fileuri, NULL, cfg, flags);
	if (ret == ENOENT)
		__wt_errx(session,
		    "%s created but '%s' is missing", objname, fileuri);

err:	__wt_buf_free(session, &uribuf);
	if (name != objname)
		__wt_free(session, name);
	if (cursor != NULL)
		WT_TRET(cursor->close(cursor));
	return (ret);
}

/*
 * __wt_schema_open_colgroups --
 *	Open the column groups for a table.
 */
int
__wt_schema_open_colgroups(WT_SESSION_IMPL *session, WT_TABLE *table)
{
	WT_CONFIG cparser;
	WT_CONFIG_ITEM ckey, cval;
	WT_ITEM plan;
	char *cgname;
	const char *fileconf;
	int i, ret;

	if (table->cg_complete)
		return (0);

	fileconf = NULL;
	cgname = NULL;
	ret = 0;

	WT_RET(__wt_config_subinit(session, &cparser, &table->cgconf));

	/* Open each column group. */
	for (i = 0; i < WT_COLGROUPS(table); i++) {
		if (table->ncolgroups > 0)
			WT_ERR(__wt_config_next(&cparser, &ckey, &cval));
		else
			WT_CLEAR(ckey);
		if (table->colgroup[i] != NULL)
			continue;

		WT_ERR(__wt_schema_colgroup_name(session, table,
		    ckey.str, ckey.len, &cgname));
		ret = __wt_schema_get_btree(session,
		    cgname, strlen(cgname), NULL, WT_BTREE_NO_LOCK);
		if (ret != 0) {
			/* It is okay if the table is not yet complete. */
			if (ret == WT_NOTFOUND)
				ret = 0;
			goto err;
		}
		table->colgroup[i] = session->btree;
	}

	if (!table->is_simple) {
		WT_ERR(__wt_table_check(session, table));

		WT_CLEAR(plan);
		WT_ERR(__wt_struct_plan(session,
		    table, table->colconf.str, table->colconf.len, 1, &plan));
		table->plan = __wt_buf_steal(session, &plan, NULL);
	}

	table->cg_complete = 1;

err:	__wt_free(session, cgname);
	__wt_free(session, fileconf);
	return (ret);
}

/*
 * ___open_index --
 *	Open an index.
 */
static int
__open_index(WT_SESSION_IMPL *session, WT_TABLE *table,
    const char *uri, const char *idxconf, WT_BTREE **btreep)
{
	WT_BTREE *btree;
	WT_CONFIG colconf;
	WT_CONFIG_ITEM ckey, cval, icols;
	WT_ITEM cols, fmt, plan, uribuf;
	const char *fileuri;
	u_int cursor_key_cols;
	int i, ret;

	ret = 0;
	WT_CLEAR(uribuf);

	/* Get the filename from the index config. */
	WT_ERR(__wt_config_getones(session, idxconf, "filename", &cval));
	WT_ERR(__wt_buf_fmt(
	    session, &uribuf, "file:%.*s", (int)cval.len, cval.str));
	fileuri = uribuf.data;

	ret = __wt_session_get_btree(session, uri, fileuri,
	    NULL, NULL, WT_BTREE_NO_LOCK);
	if (ret == ENOENT)
		__wt_errx(session,
		    "Index '%s' created but '%s' is missing", uri, fileuri);
	/* Other errors will already have generated an error message. */
	if (ret != 0)
		goto err;

	btree = session->btree;

	/*
	 * The key format for an index is somewhat subtle: the application
	 * specifies a set of columns that it will use for the key, but the
	 * engine usually adds some hidden columns in order to derive the
	 * primary key.  These hidden columns are part of the file's key.
	 *
	 * The file's key_format is stored persistently, we need to calculate
	 * the index cursor key format (which will usually omit some of those
	 * keys).
	 */
	WT_ERR(__wt_config_getones(session, idxconf, "columns", &icols));

	/* Start with the declared index columns. */
	WT_ERR(__wt_config_subinit(session, &colconf, &icols));
	WT_CLEAR(cols);
	cursor_key_cols = 0;
	while ((ret = __wt_config_next(&colconf, &ckey, &cval)) == 0) {
		WT_ERR(__wt_buf_catfmt(
		    session, &cols, "%.*s,", (int)ckey.len, ckey.str));
		++cursor_key_cols;
	}
	if (ret != 0 && ret != WT_NOTFOUND)
		goto err;

	/*
	 * Now add any primary key columns from the table that are not
	 * already part of the index key.
	 */
	WT_ERR(__wt_config_subinit(session, &colconf, &table->colconf));
	for (i = 0; i < table->nkey_columns &&
	    (ret = __wt_config_next(&colconf, &ckey, &cval)) == 0;
	    i++) {
		/*
		 * If the primary key column is already in the secondary key,
		 * don't add it again.
		 */
		if (__wt_config_subgetraw(session, &icols, &ckey, &cval) == 0)
			continue;
		WT_ERR(__wt_buf_catfmt(
		    session, &cols, "%.*s,", (int)ckey.len, ckey.str));
	}
	if (ret != 0 && ret != WT_NOTFOUND)
		goto err;

	WT_CLEAR(plan);
	WT_ERR(__wt_struct_plan(session,
	    table, cols.data, cols.size, 0, &plan));
	btree->key_plan = __wt_buf_steal(session, &plan, NULL);

	/* Set up the cursor key format (the visible columns). */
	WT_CLEAR(fmt);
	WT_ERR(__wt_struct_truncate(session,
	    btree->key_format, cursor_key_cols, &fmt));
	btree->idxkey_format = __wt_buf_steal(session, &fmt, NULL);

	/* By default, index cursor values are the table value columns. */
	/* TODO Optimize to use index columns in preference to table lookups. */
	WT_ERR(__wt_struct_plan(session,
	    table, table->colconf.str, table->colconf.len, 1, &plan));
	btree->value_plan = __wt_buf_steal(session, &plan, NULL);

	*btreep = btree;

err:	__wt_buf_free(session, &cols);
	__wt_buf_free(session, &uribuf);

	return (ret);
}

/*
 * __wt_schema_open_indices --
 *	Open the indices for a table.
 */
int
__wt_schema_open_index(
    WT_SESSION_IMPL *session, WT_TABLE *table, const char *idxname, size_t len)
{
	WT_CURSOR *cursor;
	const char *idxconf, *name, *tablename, *uri;
	int i, match, ret, skipped;

	cursor = NULL;
	skipped = 0;
	idxconf = NULL;
	tablename = table->name;
	(void)WT_PREFIX_SKIP(tablename, "table:");

	if (len == 0 && table->idx_complete)
		return (0);

	/*
	 * XXX Do a full scan through the schema table to find all matching
	 * indices.  This scan be optimized when we have cursor search + next.
	 */
	WT_RET(__wt_schema_table_cursor(session, NULL, &cursor));

	/* Open each index. */
	for (i = 0; (ret = cursor->next(cursor)) == 0;) {
		WT_ERR(cursor->get_key(cursor, &uri));
		name = uri;
		if (!WT_PREFIX_SKIP(name, "index:") ||
		    !WT_PREFIX_SKIP(name, tablename) ||
		    !WT_PREFIX_SKIP(name, ":"))
			continue;

		/* Is this the index we are looking for? */
		match = (len > 0 &&
		   strncmp(name, idxname, len) == 0 && name[len] == '\0');

		if (i * sizeof(WT_BTREE *) >= table->index_alloc)
			WT_ERR(__wt_realloc(session, &table->index_alloc,
			    WT_MAX(10 * sizeof(WT_BTREE *),
			    2 * table->index_alloc),
			    &table->index));

		if (table->index[i] == NULL) {
			if (len == 0 || match) {
				WT_ERR(cursor->get_value(cursor, &idxconf));
				WT_ERR(__open_index(session,
				    table, uri, idxconf, &table->index[i]));
			} else
				skipped = 1;
		}

		if (match) {
			ret = cursor->close(cursor);
			cursor = NULL;
			session->btree = table->index[i];
			break;
		}
		i++;
	}

	/* Did we make it all the way through? */
	if (ret == WT_NOTFOUND) {
		ret = 0;
		if (!skipped) {
			table->nindices = i;
			table->idx_complete = 1;
		}
	}

err:	if (cursor != NULL)
		WT_TRET(cursor->close(cursor));
	return (ret);
}

/*
 * __wt_schema_open_table --
 *	Open a named table.
 */
int
__wt_schema_open_table(WT_SESSION_IMPL *session,
    const char *name, size_t namelen, WT_TABLE **tablep)
{
	WT_CONFIG cparser;
	WT_CONFIG_ITEM ckey, cval;
	WT_CURSOR *cursor;
	WT_ITEM buf;
	WT_TABLE *table;
	const char *tconfig;
	char *tablename;
	int ret;

	cursor = NULL;
	table = NULL;

	WT_CLEAR(buf);
	WT_RET(__wt_buf_fmt(session, &buf, "table:%.*s", (int)namelen, name));
	tablename = __wt_buf_steal(session, &buf, NULL);

	WT_ERR(__wt_schema_table_cursor(session, NULL, &cursor));
	cursor->set_key(cursor, tablename);
	WT_ERR(cursor->search(cursor));
	WT_ERR(cursor->get_value(cursor, &tconfig));

	WT_ERR(__wt_calloc_def(session, 1, &table));
	table->name = tablename;
	tablename = NULL;

	WT_ERR(__wt_config_getones(session, tconfig, "columns", &cval));

	WT_ERR(__wt_config_getones(session, tconfig, "key_format", &cval));
	WT_ERR(__wt_strndup(session, cval.str, cval.len, &table->key_format));
	WT_ERR(__wt_config_getones(session, tconfig, "value_format", &cval));
	WT_ERR(__wt_strndup(session, cval.str, cval.len, &table->value_format));
	WT_ERR(__wt_strdup(session, tconfig, &table->config));

	/* Point to some items in the copy to save re-parsing. */
	WT_ERR(__wt_config_getones(session, table->config,
	    "columns", &table->colconf));

	/*
	 * Count the number of columns: tables are "simple" if the columns
	 * are not named.
	 */
	WT_ERR(__wt_config_subinit(session, &cparser, &table->colconf));
	table->is_simple = 1;
	while ((ret = __wt_config_next(&cparser, &ckey, &cval)) == 0)
		table->is_simple = 0;
	if (ret != WT_NOTFOUND)
		goto err;

	/* Check that the columns match the key and value formats. */
	if (!table->is_simple)
		WT_ERR(__wt_schema_colcheck(session,
		    table->key_format, table->value_format, &table->colconf,
		    &table->nkey_columns, NULL));

	WT_ERR(__wt_config_getones(session, table->config,
	    "colgroups", &table->cgconf));

	/* Count the number of column groups. */
	WT_ERR(__wt_config_subinit(session, &cparser, &table->cgconf));
	table->ncolgroups = 0;
	while ((ret = __wt_config_next(&cparser, &ckey, &cval)) == 0)
		++table->ncolgroups;
	if (ret != WT_NOTFOUND)
		goto err;

	WT_ERR(__wt_calloc_def(session, WT_COLGROUPS(table), &table->colgroup));
	WT_ERR(__wt_schema_open_colgroups(session, table));

	*tablep = table;

	if (0) {
err:		if (table != NULL)
			__wt_schema_destroy_table(session, table);
	}
	if (cursor != NULL)
		WT_TRET(cursor->close(cursor));
	__wt_free(session, tablename);
	return (ret);
}
