/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "util.h"

static int dump_prefix(int);
static int dump_suffix(void);
static int schema(WT_SESSION *, const char *);
static int schema_file(WT_CURSOR *, const char *);
static int schema_table(WT_CURSOR *, const char *);
static int usage(void);

static inline int
dump_forward(WT_CURSOR *cursor, const char *name)
{
	const char *key, *value;
	int ret;

	while ((ret = cursor->next(cursor)) == 0) {
		if ((ret = cursor->get_key(cursor, &key)) != 0)
			return (util_cerr(name, "get_key", ret));
		if ((ret = cursor->get_value(cursor, &value)) != 0)
			return (util_cerr(name, "get_value", ret));
		if (printf("%s\n%s\n", key, value) < 0)
			return (util_err(EIO, NULL));
	}
	return (ret == WT_NOTFOUND ? 0 : util_cerr(name, "next", ret));
}

static inline int
dump_reverse(WT_CURSOR *cursor, const char *name)
{
	const char *key, *value;
	int ret;

	while ((ret = cursor->prev(cursor)) == 0) {
		if ((ret = cursor->get_key(cursor, &key)) != 0)
			return (util_cerr(name, "get_key", ret));
		if ((ret = cursor->get_value(cursor, &value)) != 0)
			return (util_cerr(name, "get_value", ret));
		if (printf("%s\n%s\n", key, value) < 0)
			return (util_err(EIO, NULL));
	}
	return (ret == WT_NOTFOUND ? 0 : util_cerr(name, "prev", ret));
}

int
util_dump(WT_SESSION *session, int argc, char *argv[])
{
	WT_CURSOR *cursor;
	int ch, hex, ret, reverse;
	char *name;

	hex = reverse = 0;
	name = NULL;
	while ((ch = util_getopt(argc, argv, "f:rx")) != EOF)
		switch (ch) {
		case 'f':			/* output file */
			if (freopen(util_optarg, "w", stdout) == NULL)
				return (
				    util_err(errno, "%s: reopen", util_optarg));
			break;
		case 'r':
			reverse = 1;
			break;
		case 'x':
			hex = 1;
			break;
		case '?':
		default:
			return (usage());
		}
	argc -= util_optind;
	argv += util_optind;

	/* The remaining argument is the uri. */
	if (argc != 1)
		return (usage());
	if ((name =
	    util_name(*argv, "table", UTIL_FILE_OK | UTIL_TABLE_OK)) == NULL)
		goto err;

	if (dump_prefix(hex) != 0 ||
	    schema(session, name) != 0 ||
	    dump_suffix() != 0)
		goto err;

	if ((ret = session->open_cursor(session,
	    name, NULL, hex ? "dump=hex" : "dump=print", &cursor)) != 0) {
		fprintf(stderr, "%s: cursor open(%s) failed: %s\n",
		    progname, name, wiredtiger_strerror(ret));
		goto err;
	}

	if (reverse)
		ret = dump_reverse(cursor, name);
	else
		ret = dump_forward(cursor, name);

	if (0) {
err:		ret = 1;
	}

	if (name != NULL)
		free(name);

	return (ret);
}

/*
 * schema --
 *	Dump the schema for the uri.
 */
static int
schema(WT_SESSION *session, const char *uri)
{
	WT_CURSOR *cursor;
	int ret, tret;

	ret = 0;

	/* Open the schema file. */
	if ((ret = session->open_cursor(
	    session, WT_SCHEMA_URI, NULL, NULL, &cursor)) != 0) {
		fprintf(stderr, "%s: %s: session.open_cursor: %s\n",
		    progname, WT_SCHEMA_URI, wiredtiger_strerror(ret));
		return (1);
	}

	/* Dump the schema. */
	if (strncmp(uri, "table:", strlen("table:")) == 0)
		ret = schema_table(cursor, uri);
	else
		ret = schema_file(cursor, uri);

	if ((tret = cursor->close(cursor)) != 0 && ret == 0)
		ret = tret;

	return (ret);
}

/*
 * schema_table --
 *	Dump the schema for a table.
 */
static int
schema_table(WT_CURSOR *cursor, const char *uri)
{
	struct {
		char *key;			/* Schema key */
		char *value;			/* Schema value */
	} *list;
	int i, elem, list_elem, ret;
	const char *key, *name, *value;
	char *buf, *filename, *p, *t, *sep;

	ret = 0;

	/* Get the name. */
	if ((name = strchr(uri, ':')) == NULL) {
		fprintf(stderr, "%s: %s: corrupted uri\n", progname, uri);
		return (1);
	}
	++name;

	list = NULL;
	elem = list_elem = 0;
	for (; (ret = cursor->next(cursor)) == 0; free(buf)) {
		/* Get the key and duplicate it, we want to overwrite it. */
		if ((ret = cursor->get_key(cursor, &key)) != 0)
			return (util_cerr(uri, "get_key", ret));
		if ((buf = strdup(key)) == NULL)
			return (util_err(errno, NULL));
			
		/* Check for the dump table's column groups or indices. */
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		*p++ = '\0';
		if (strcmp(buf, "index") != 0 && strcmp(buf, "colgroup") != 0)
			continue;
		if ((t = strchr(p, ':')) == NULL)
			continue;
		*t++ = '\0';
		if (strcmp(p, name) != 0)
			continue;

		/* Found one, save it for review. */
		if ((ret = cursor->get_value(cursor, &value)) != 0)
			return (util_cerr(uri, "get_value", ret));
		if (elem == list_elem && (list = realloc(list,
		    (size_t)(list_elem += 20) * sizeof(*list))) == NULL)
			return (util_err(errno, NULL));
		if ((list[elem].key = strdup(key)) == NULL)
			return (util_err(errno, NULL));
		if ((list[elem].value = strdup(value)) == NULL)
			return (util_err(errno, NULL));
		++elem;
	}
	if (ret != WT_NOTFOUND)
		return (util_cerr(uri, "next", ret));
	ret = 0;

	/*
	 * Dump out the schema information: first, dump the uri entry itself
	 * (requires a lookup).
	 */
	cursor->set_key(cursor, uri);
	if ((ret = cursor->search(cursor)) != 0)
		return (util_cerr(uri, "search", ret));
	if ((ret = cursor->get_key(cursor, &key)) != 0)
		return (util_cerr(uri, "get_key", ret));
	if ((ret = cursor->get_value(cursor, &value)) != 0)
		return (util_cerr(uri, "get_value", ret));
	if (printf("%s\n%s\n", key, value) < 0)
		return (util_err(EIO, NULL));

	/*
	 * Second, dump the column group and index key/value pairs: for each
	 * one, look up the related file information and append it to the base
	 * record.
	 */
	for (i = 0; i < elem; ++i) {
		if ((filename = strstr(list[i].value, "filename=")) == NULL) {
			fprintf(stderr,
			    "%s: %s: has no underlying file configuration\n",
			    progname, list[i].key);
			return (1);
		}

		/*
		 * Nul-terminate the filename if necessary, create the file
		 * URI, then look it up.
		 */
		if ((sep = strchr(filename, ',')) != NULL)
			*sep = '\0';
		if ((t = strdup(filename)) == NULL)
			return (util_err(errno, NULL));
		if (sep != NULL)
			*sep = ',';
		p = t + strlen("filename=");
		p -= strlen("file:");
		memcpy(p, "file:", strlen("file:"));
		cursor->set_key(cursor, p);
		if ((ret = cursor->search(cursor)) != 0) {
			fprintf(stderr,
			    "%s: %s: unable to find schema reference for the "
			    "underlying file %s\n",
			    progname, list[i].key, p);
			return (1);
		}
		if ((ret = cursor->get_value(cursor, &value)) != 0)
			return (util_cerr(uri, "get_value", ret));

		/*
		 * The dumped configuration string is the original key plus the
		 * file's configuration.
		 */
		if (printf(
		    "%s\n%s,%s\n", list[i].key, list[i].value, value) < 0)
			return (util_err(EIO, NULL));
	}

	/* Leak the memory, I don't care. */
	return (0);
}

/*
 * schema_file --
 *	Dump the schema for a file.
 */
static int
schema_file(WT_CURSOR *cursor, const char *uri)
{
	const char *key, *value;
	int ret;

	ret = 0;

	cursor->set_key(cursor, uri);
	if ((ret = cursor->search(cursor)) != 0)
		return (util_cerr(uri, "search", ret));
	if ((ret = cursor->get_key(cursor, &key)) != 0)
		return (util_cerr(uri, "get_key", ret));
	if ((ret = cursor->get_value(cursor, &value)) != 0)
		return (util_cerr(uri, "get_value", ret));
	if (printf("%s\n%s\n", key, value) < 0)
		return (util_err(EIO, NULL));

	return (0);
}

/*
 * dump_prefix --
 *	Output the dump file header prefix.
 */
static int
dump_prefix(int hex)
{
	int vmajor, vminor, vpatch;

	(void)wiredtiger_version(&vmajor, &vminor, &vpatch);

	if (printf(
	    "WiredTiger Dump (WiredTiger Version %d.%d.%d)\n",
	    vmajor, vminor, vpatch) < 0 ||
	    printf("Format=%s\n", hex ? "hex" : "print") < 0 ||
	    printf("Header\n") < 0)
		return (util_err(EIO, NULL));
	return (0);
}

/*
 * dump_suffix --
 *	Output the dump file header suffix.
 */
static int
dump_suffix(void)
{
	if (printf("Data\n") < 0)
		return (util_err(EIO, NULL));
	return (0);
}

static int
usage(void)
{
	(void)fprintf(stderr,
	    "usage: %s %s "
	    "dump [-rx] [-f output-file] uri\n",
	    progname, usage_prefix);
	return (1);
}
