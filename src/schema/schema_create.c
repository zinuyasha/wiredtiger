/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

int
__wt_create_file(WT_SESSION_IMPL *session,
    const char *name, const char *fileuri, int exclusive, const char *config)
{
	WT_ITEM *val;
	const char *cfg[] = API_CONF_DEFAULTS(session, create, config);
	const char *filecfg[4] = API_CONF_DEFAULTS(file, meta, config);
	const char *filename, *treeconf;
	int is_schema, vmajor, vminor, vpatch, ret;

	val = NULL;
	treeconf = NULL;
	ret = 0;

	filename = fileuri;
	if (!WT_PREFIX_SKIP(filename, "file:"))
		WT_RET_MSG(
		    session, EINVAL, "Expecting a 'file:' URI: %s", fileuri);

	/*
	 * Opening the schema table is a special case, use the config
	 * string we were passed to open the file.
	 */
	is_schema = (strcmp(filename, WT_SCHEMA_FILENAME) == 0);

	/* If the file exists, don't try to recreate it. */
	if ((ret = __wt_session_get_btree(session, name, fileuri,
	    is_schema ? config : NULL,
	    NULL, WT_BTREE_NO_LOCK)) != WT_NOTFOUND) {
		if (ret == 0 && exclusive)
			ret = EEXIST;
		return (ret);
	}

	WT_RET(__wt_btree_create(session, filename));
	WT_ERR(__wt_schema_table_track_fileop(session, NULL, filename));

	/* Insert WiredTiger version numbers into the schema file. */
	WT_ERR(__wt_scr_alloc(session, 0, &val));
	if (is_schema) {
		WT_ERR(__wt_schema_table_insert(
		    session, WT_SCHEMA_VERSION_STR,
		    wiredtiger_version(&vmajor, &vminor, &vpatch)));
		WT_ERR(__wt_buf_fmt(session, val,
		    "major=%d,minor=%d,patch=%d", vmajor, vminor, vpatch));
		WT_ERR(__wt_schema_table_insert(
		    session, WT_SCHEMA_VERSION, val->data));
	}

	/*
	 * Insert Btree version numbers into the schema file (including for
	 * the schema file itself, although the schema file version numbers
	 * can never be trusted, we have to get them from the turtle file).
	 */
	WT_ERR(__wt_buf_fmt(session, val, "version=(major=%d,minor=%d)",
	    WT_BTREE_MAJOR_VERSION, WT_BTREE_MINOR_VERSION));
	filecfg[2] = val->data;

	if (is_schema)
		WT_ERR(__wt_strdup(session, config, &treeconf));
	else
		WT_ERR(__wt_config_collapse(session, filecfg, &treeconf));
	WT_ERR(__wt_schema_table_insert(session, fileuri, treeconf));

	/*
	 * Call the underlying connection function to allocate a WT_BTREE handle
	 * and open the underlying file (note we no longer own the configuration
	 * string after that call).
	 */
	ret = __wt_conn_btree_open(session, name, filename, treeconf, cfg, 0);
	treeconf = NULL;
	WT_ERR(ret);
	WT_ERR(__wt_session_add_btree(session, NULL));

	/* If something goes wrong, throw away anything we created. */
err:	__wt_scr_free(&val);
	__wt_free(session, treeconf);
	return (ret);
}

static int
__create_colgroup(WT_SESSION_IMPL *session,
    const char *name, int exclusive, const char *config)
{
	WT_CONFIG_ITEM cval;
	WT_ITEM fmt, namebuf, uribuf;
	WT_TABLE *table;
	const char *cfg[] = { __wt_confdfl_colgroup_meta, config, NULL, NULL };
	const char *filecfg[] = { config, NULL, NULL };
	const char **cfgp;
	const char *cgconf, *cgname, *fileconf, *filename, *fileuri;
	const char *oldconf, *tablename;
	size_t tlen;
	int ret;

	cgconf = fileconf = oldconf = NULL;
	WT_CLEAR(fmt);
	WT_CLEAR(namebuf);
	WT_CLEAR(uribuf);

	tablename = name;
	if (!WT_PREFIX_SKIP(tablename, "colgroup:"))
		return (EINVAL);
	cgname = strchr(tablename, ':');
	if (cgname != NULL) {
		tlen = (size_t)(cgname - tablename);
		++cgname;
	} else
		tlen = strlen(tablename);

	if ((ret =
	    __wt_schema_get_table(session, tablename, tlen, &table)) != 0)
		WT_RET_MSG(session, (ret == WT_NOTFOUND) ? ENOENT : ret,
		    "Can't create '%s' for non-existent table '%.*s'",
		    name, (int)tlen, tablename);

	/* Make sure the column group is referenced from the table. */
	if (cgname != NULL && (ret =
	    __wt_config_subgets(session, &table->cgconf, cgname, &cval)) != 0)
		WT_RET_MSG(session, EINVAL,
		    "Column group '%s' not found in table '%.*s'",
		    cgname, (int)tlen, tablename);

	/* Find the first NULL entry in the cfg stack. */
	for (cfgp = &cfg[1]; *cfgp; cfgp++)
		;

	/* Add the filename to the colgroup config before collapsing. */
	if (__wt_config_getones(session, config, "filename", &cval) == 0) {
		WT_ERR(__wt_buf_fmt(
		    session, &namebuf, "%.*s", (int)cval.len, cval.str));
		filename = namebuf.data;
	} else {
		if (cgname == NULL)
			WT_ERR(__wt_buf_fmt(session, &namebuf,
			    "filename=%.*s.wt", (int)tlen, tablename));
		else
			WT_ERR(__wt_buf_fmt(session, &namebuf,
			    "filename=%.*s_%s.wt", (int)tlen, tablename,
			    cgname));
		*cfgp++ = filename = namebuf.data;
		(void)WT_PREFIX_SKIP(filename, "filename=");
	}

	WT_ERR(__wt_config_collapse(session, cfg, &cgconf));

	/* Calculate the key/value formats -- these go into the file config. */
	WT_ERR(__wt_buf_fmt(session, &fmt, "key_format=%s", table->key_format));
	if (cgname == NULL)
		WT_ERR(__wt_buf_catfmt
		    (session, &fmt, ",value_format=%s", table->value_format));
	else {
		if (__wt_config_getones(session, config, "columns", &cval) != 0)
			WT_ERR_MSG(session, EINVAL,
			    "No 'columns' configuration for '%s'", name);
		WT_ERR(__wt_buf_catfmt(session, &fmt, ",value_format="));
		WT_ERR(__wt_struct_reformat(session,
		    table, cval.str, cval.len, NULL, 1, &fmt));
	}
	filecfg[1] = fmt.data;
	WT_ERR(__wt_config_concat(session, filecfg, &fileconf));

	WT_ERR(__wt_buf_fmt(session, &uribuf, "file:%s", filename));
	fileuri = uribuf.data;

	if ((ret = __wt_schema_table_insert(session, name, cgconf)) != 0) {
		/*
		 * If the entry already exists in the schema table, we're done.
		 * This is an error for exclusive creates but okay otherwise.
		 */
		if (ret == WT_DUPLICATE_KEY)
			ret = exclusive ? EEXIST : 0;
		goto err;
	}
	WT_ERR(__wt_create_file(session, name, fileuri, exclusive, fileconf));

	WT_ERR(__wt_schema_open_colgroups(session, table));

err:    __wt_free(session, cgconf);
	__wt_free(session, fileconf);
	__wt_free(session, oldconf);
	__wt_buf_free(session, &fmt);
	__wt_buf_free(session, &namebuf);
	__wt_buf_free(session, &uribuf);
	return (ret);
}

static int
__create_index(WT_SESSION_IMPL *session,
    const char *name, int exclusive, const char *config)
{
	WT_CONFIG pkcols;
	WT_CONFIG_ITEM ckey, cval, icols;
	WT_ITEM extra_cols, fmt, namebuf, uribuf;
	WT_TABLE *table;
	const char *cfg[] = { __wt_confdfl_index_meta, config, NULL, NULL };
	const char *filecfg[] = { config, NULL, NULL };
	const char *fileconf, *filename, *fileuri, *idxconf, *idxname;
	const char *tablename;
	size_t tlen;
	int i, ret;

	idxconf = fileconf = NULL;
	WT_CLEAR(fmt);
	WT_CLEAR(extra_cols);
	WT_CLEAR(namebuf);
	WT_CLEAR(uribuf);

	tablename = name;
	if (!WT_PREFIX_SKIP(tablename, "index:"))
		return (EINVAL);
	idxname = strchr(tablename, ':');
	if (idxname == NULL)
		WT_RET_MSG(session, EINVAL, "Invalid index name, "
		     "should be <table name>:<index name>: %s", name);

	tlen = (size_t)(idxname++ - tablename);
	if ((ret =
	    __wt_schema_get_table(session, tablename, tlen, &table)) != 0)
		WT_RET_MSG(session, ret,
		    "Can't create an index for a non-existent table: %.*s",
		    (int)tlen, tablename);

	/* Add the filename to the index config before collapsing. */
	if (__wt_config_getones(session, config, "filename", &cval) == 0) {
		WT_ERR(__wt_buf_fmt(session,
		    &namebuf, "%.*s", (int)cval.len, cval.str));
		filename = namebuf.data;
	} else {
		WT_ERR(__wt_buf_fmt(session, &namebuf,
		    "filename=%.*s_%s.wti", (int)tlen, tablename, idxname));
		cfg[2] = filename = namebuf.data;
		(void)WT_PREFIX_SKIP(filename, "filename=");
	}
	WT_ERR(__wt_config_collapse(session, cfg, &idxconf));

	/* Calculate the key/value formats -- these go into the file config. */
	if (__wt_config_getones(session, config, "columns", &icols) != 0)
		WT_ERR_MSG(session, EINVAL,
		    "No 'columns' configuration for '%s'", name);

	/*
	 * The key format for an index is somewhat subtle: the application
	 * specifies a set of columns that it will use for the key, but the
	 * engine usually adds some hidden columns in order to derive the
	 * primary key.  These hidden columns are part of the file's
	 * key_format, which we are calculating now, but not part of an index
	 * cursor's key_format.
	 */
	WT_ERR(__wt_config_subinit(session, &pkcols, &table->colconf));
	for (i = 0; i < table->nkey_columns &&
	    (ret = __wt_config_next(&pkcols, &ckey, &cval)) == 0;
	    i++) {
		/*
		 * If the primary key column is already in the secondary key,
		 * don't add it again.
		 */
		if (__wt_config_subgetraw(session, &icols, &ckey, &cval) == 0)
			continue;
		WT_ERR(__wt_buf_catfmt(
		    session, &extra_cols, "%.*s,", (int)ckey.len, ckey.str));
	}
	if (ret != 0 && ret != WT_NOTFOUND)
		goto err;
	/* Index values are empty: all columns are packed into the index key. */
	WT_ERR(__wt_buf_fmt(session, &fmt, "value_format=,key_format="));
	WT_ERR(__wt_struct_reformat(session, table,
	     icols.str, icols.len, (const char *)extra_cols.data, 0, &fmt));
	filecfg[1] = fmt.data;
	WT_ERR(__wt_config_concat(session, filecfg, &fileconf));

	WT_ERR(__wt_buf_fmt(session, &uribuf, "file:%s", filename));
	fileuri = uribuf.data;

	if ((ret = __wt_schema_table_insert(session, name, idxconf)) != 0) {
		/*
		 * If the entry already exists in the schema table, we're done.
		 * This is an error for exclusive creates but okay otherwise.
		 */
		if (ret == WT_DUPLICATE_KEY)
			ret = exclusive ? EEXIST : 0;
		goto err;
	}
	WT_ERR(__wt_create_file(session, name, fileuri, exclusive, fileconf));

err:	__wt_free(session, fileconf);
	__wt_free(session, idxconf);
	__wt_buf_free(session, &fmt);
	__wt_buf_free(session, &extra_cols);
	__wt_buf_free(session, &namebuf);
	__wt_buf_free(session, &uribuf);
	return (ret);
}

static int
__create_table(WT_SESSION_IMPL *session,
    const char *name, int exclusive, const char *config)
{
	WT_CONFIG conf;
	WT_CONFIG_ITEM cgkey, cgval, cval;
	WT_TABLE *table;
	const char *cfg[] = { __wt_confdfl_table_meta, config, NULL, NULL };
	const char *tableconf, *tablename;
	char *cgname;
	size_t cgsize;
	int ncolgroups, ret;

	cgname = NULL;
	table = NULL;
	tableconf = NULL;

	tablename = name;
	if (!WT_PREFIX_SKIP(tablename, "table:"))
		return (EINVAL);

	if ((ret = __wt_schema_get_table(session,
	    tablename, strlen(tablename), &table)) == 0) {
		if (exclusive)
			ret = EEXIST;
		return (0);
	}
	if (ret != WT_NOTFOUND)
		return (ret);

	WT_RET(__wt_config_gets(session, cfg, "colgroups", &cval));
	WT_RET(__wt_config_subinit(session, &conf, &cval));
	for (ncolgroups = 0;
	    (ret = __wt_config_next(&conf, &cgkey, &cgval)) == 0;
	    ncolgroups++)
		;
	if (ret != WT_NOTFOUND)
		return (ret);

	WT_RET(__wt_config_collapse(session, cfg, &tableconf));
	WT_ERR(__wt_schema_table_insert(session, name, tableconf));

	/* Attempt to open the table now to catch any errors. */
	WT_ERR(__wt_schema_get_table(
	    session, tablename, strlen(tablename), &table));

	if (ncolgroups == 0) {
		cgsize = strlen("colgroup:") + strlen(tablename) + 1;
		WT_ERR(__wt_calloc_def(session, cgsize, &cgname));
		snprintf(cgname, cgsize, "colgroup:%s", tablename);
		WT_ERR(__create_colgroup(session, cgname, exclusive, config));
	}

	if (0) {
err:		if (table != NULL)
			(void)__wt_schema_remove_table(session, table);
	}
	__wt_free(session, cgname);
	__wt_free(session, tableconf);
	return (ret);
}

int
__wt_schema_create(
    WT_SESSION_IMPL *session, const char *name, const char *config)
{
	WT_CONFIG_ITEM cval;
	int exclusive, ret;

	/* Disallow objects in the WiredTiger name space. */
	WT_RET(__wt_schema_name_check(session, name));

	exclusive = (
	    __wt_config_getones(session, config, "exclusive", &cval) == 0 &&
	    cval.val != 0);

	/*
	 * We track rename operations, if we fail in the middle, we want to
	 * back it all out.
	 */
	WT_RET(__wt_schema_table_track_on(session));

	if (WT_PREFIX_MATCH(name, "colgroup:"))
		ret = __create_colgroup(session, name, exclusive, config);
	else if (WT_PREFIX_MATCH(name, "file:"))
		ret = __wt_create_file(session, name, name, exclusive, config);
	else if (WT_PREFIX_MATCH(name, "index:"))
		ret = __create_index(session, name, exclusive, config);
	else if (WT_PREFIX_MATCH(name, "table:"))
		ret = __create_table(session, name, exclusive, config);
	else
		ret = __wt_unknown_object_type(session, name);

	WT_TRET(__wt_schema_table_track_off(session, ret != 0));

	return (ret);
}
