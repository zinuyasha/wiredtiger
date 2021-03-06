/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

static int  __inmem_col_fix(WT_SESSION_IMPL *, WT_PAGE *, size_t *);
static int  __inmem_col_int(WT_SESSION_IMPL *, WT_PAGE *, size_t *);
static int  __inmem_col_var(WT_SESSION_IMPL *, WT_PAGE *, size_t *);
static int  __inmem_row_int(WT_SESSION_IMPL *, WT_PAGE *, size_t *);
static int  __inmem_row_leaf(WT_SESSION_IMPL *, WT_PAGE *, size_t *);

/*
 * __wt_page_in --
 *	Acquire a hazard reference to a page; if the page is not in-memory,
 *	read it from the disk and build an in-memory version.
 */
int
__wt_page_in_func(
    WT_SESSION_IMPL *session, WT_PAGE *parent, WT_REF *ref
#ifdef HAVE_DIAGNOSTIC
    , const char *file, int line
#endif
    )
{
	int wake, read_lockout;

	/*
	 * Only wake the eviction server the first time through here (if the
	 * cache is too full), or after we fail to evict a page.  Otherwise, we
	 * are just wasting effort and making a busy mutex busier.
	 */
	wake = 1;

	for (;;) {
		switch (ref->state) {
		case WT_REF_DISK:
			/*
			 * The page isn't in memory, attempt to set the state
			 * to WT_REF_READING.  If successful, read it.
			 */
			__wt_eviction_check(session, &read_lockout, wake);
			wake = 0;

			if (read_lockout || !WT_ATOMIC_CAS(ref->state,
			    WT_REF_DISK, WT_REF_READING))
				break;

			WT_RET(__wt_cache_read(session, parent, ref));
			continue;
		case WT_REF_EVICTING:
		case WT_REF_LOCKED:
		case WT_REF_READING:
			/*
			 * The page is being read or considered for eviction --
			 * wait for that to be resolved.
			 */
			break;
		case WT_REF_EVICT_WALK:
		case WT_REF_MEM:
			/*
			 * The page is in memory: get a hazard reference, update
			 * the page's LRU and return.  The expected reason we
			 * can't get a hazard reference is because the page is
			 * being evicted; yield and try again.
			 */
			if (__wt_hazard_set(session, ref
#ifdef HAVE_DIAGNOSTIC
			    , file, line
#endif
			    ) == 0) {
				WT_ASSERT(session, !WT_PAGE_IS_ROOT(ref->page));
				ref->page->read_gen =
				    __wt_cache_read_gen(session);
				return (0);
			}
			break;
		WT_ILLEGAL_VALUE(session);
		}

		/*
		 * Find a page to evict -- if that fails, we don't care why,
		 * but we may need to wake the eviction server again if the
		 * cache is still full.
		 */
		if (__wt_evict_lru_page(session, 1) != 0)
			wake = 1;
	}
}

/*
 * __wt_page_inmem --
 *	Build in-memory page information.
 */
int
__wt_page_inmem(WT_SESSION_IMPL *session,
    WT_PAGE *parent, WT_REF *parent_ref, WT_PAGE_HEADER *dsk,
    size_t *inmem_sizep, WT_PAGE **pagep)
{
	WT_PAGE *page;
	int ret;

	WT_ASSERT_RET(session, dsk->u.entries > 0);

	*pagep = NULL;

	/*
	 * Allocate and initialize the WT_PAGE.
	 * Set the LRU so the page is not immediately selected for eviction.
	 */
	WT_RET(__wt_calloc_def(session, 1, &page));
	if (inmem_sizep != NULL)
		*inmem_sizep = sizeof(*page) + dsk->size;
	page->type = dsk->type;
	page->parent = parent;
	page->ref = parent_ref;
	page->dsk = dsk;
	/*
	 * Set the write generation to 1 (which can't match a search where the
	 * write generation wasn't set, that is, remained 0).
	 */
	page->read_gen = __wt_cache_read_gen(session);

	switch (page->type) {
	case WT_PAGE_COL_FIX:
		page->u.col_fix.recno = dsk->recno;
		WT_ERR(__inmem_col_fix(session, page, inmem_sizep));
		break;
	case WT_PAGE_COL_INT:
		page->u.intl.recno = dsk->recno;
		WT_ERR(__inmem_col_int(session, page, inmem_sizep));
		break;
	case WT_PAGE_COL_VAR:
		page->u.col_var.recno = dsk->recno;
		WT_ERR(__inmem_col_var(session, page, inmem_sizep));
		break;
	case WT_PAGE_ROW_INT:
		WT_ERR(__inmem_row_int(session, page, inmem_sizep));
		break;
	case WT_PAGE_ROW_LEAF:
		WT_ERR(__inmem_row_leaf(session, page, inmem_sizep));
		break;
	WT_ILLEGAL_VALUE(session);
	}

	*pagep = page;
	return (0);

err:	__wt_free(session, page);
	return (ret);
}

/*
 * __inmem_col_fix --
 *	Build in-memory index for fixed-length column-store leaf pages.
 */
static int
__inmem_col_fix(WT_SESSION_IMPL *session, WT_PAGE *page, size_t *inmem_sizep)
{
	WT_BTREE *btree;
	WT_PAGE_HEADER *dsk;

	WT_UNUSED(inmem_sizep);

	btree = session->btree;
	dsk = page->dsk;

	page->u.col_fix.bitf = WT_PAGE_HEADER_BYTE(btree, dsk);
	page->entries = dsk->u.entries;
	return (0);
}

/*
 * __inmem_col_int --
 *	Build in-memory index for column-store internal pages.
 */
static int
__inmem_col_int(WT_SESSION_IMPL *session, WT_PAGE *page, size_t *inmem_sizep)
{
	WT_BTREE *btree;
	WT_CELL *cell;
	WT_CELL_UNPACK *unpack, _unpack;
	WT_PAGE_HEADER *dsk;
	WT_REF *ref;
	uint32_t i;

	btree = session->btree;
	dsk = page->dsk;
	unpack = &_unpack;

	/*
	 * Column-store page entries map one-to-one to the number of physical
	 * entries on the page (each physical entry is a offset object).
	 */
	WT_RET(__wt_calloc_def(
	    session, (size_t)dsk->u.entries, &page->u.intl.t));
	if (inmem_sizep != NULL)
		*inmem_sizep += dsk->u.entries * sizeof(*page->u.intl.t);

	/*
	 * Walk the page, building references: the page contains value items.
	 * The value items are on-page items (WT_CELL_VALUE).
	 */
	ref = page->u.intl.t;
	WT_CELL_FOREACH(btree, dsk, cell, unpack, i) {
		__wt_cell_unpack(cell, unpack);
		ref->addr = cell;
		ref->u.recno = unpack->v;
		++ref;
	}

	page->entries = dsk->u.entries;
	return (0);
}

/*
 * __inmem_col_var --
 *	Build in-memory index for variable-length, data-only leaf pages in
 *	column-store trees.
 */
static int
__inmem_col_var(WT_SESSION_IMPL *session, WT_PAGE *page, size_t *inmem_sizep)
{
	WT_BTREE *btree;
	WT_COL *cip;
	WT_COL_RLE *repeats;
	WT_CELL *cell;
	WT_CELL_UNPACK *unpack, _unpack;
	WT_PAGE_HEADER *dsk;
	uint64_t recno, rle;
	size_t bytes_allocated;
	uint32_t i, indx, max_repeats, nrepeats;

	btree = session->btree;
	dsk = page->dsk;
	unpack = &_unpack;
	repeats = NULL;
	bytes_allocated = max_repeats = nrepeats = 0;
	recno = page->u.col_var.recno;

	/*
	 * Column-store page entries map one-to-one to the number of physical
	 * entries on the page (each physical entry is a data item).
	 */
	WT_RET(__wt_calloc_def(
	    session, (size_t)dsk->u.entries, &page->u.col_var.d));
	if (inmem_sizep != NULL)
		*inmem_sizep += dsk->u.entries * sizeof(*page->u.col_var.d);

	/*
	 * Walk the page, building references: the page contains unsorted value
	 * items.  The value items are on-page (WT_CELL_VALUE), overflow items
	 * (WT_CELL_VALUE_OVFL) or deleted items (WT_CELL_DEL).
	 */
	cip = page->u.col_var.d;
	indx = 0;
	WT_CELL_FOREACH(btree, dsk, cell, unpack, i) {
		__wt_cell_unpack(cell, unpack);
		(cip++)->__value = WT_PAGE_DISK_OFFSET(page, cell);

		/*
		 * Add records with repeat counts greater than 1 to an array we
		 * use for fast lookups.
		 */
		rle = __wt_cell_rle(unpack);
		if (rle > 1) {
			if (nrepeats == max_repeats) {
				max_repeats = (max_repeats == 0) ?
				    10 : 2 * max_repeats;
				WT_RET(__wt_realloc(session, &bytes_allocated,
				    max_repeats * sizeof(WT_COL_RLE),
				    &repeats));
			}
			repeats[nrepeats].indx = indx;
			repeats[nrepeats].recno = recno;
			repeats[nrepeats++].rle = rle;
		}
		indx++;
		recno += rle;
	}

	page->u.col_var.repeats = repeats;
	page->u.col_var.nrepeats = nrepeats;
	page->entries = dsk->u.entries;
	if (inmem_sizep != NULL)
		*inmem_sizep += bytes_allocated;
	return (0);
}

/*
 * __inmem_row_int --
 *	Build in-memory index for row-store internal pages.
 */
static int
__inmem_row_int(WT_SESSION_IMPL *session, WT_PAGE *page, size_t *inmem_sizep)
{
	WT_BTREE *btree;
	WT_ITEM *current, *last, *tmp;
	WT_CELL *cell;
	WT_CELL_UNPACK *unpack, _unpack;
	WT_PAGE_HEADER *dsk;
	WT_REF *ref;
	uint32_t i, nindx, prefix;
	int ret;
	void *huffman;

	btree = session->btree;
	current = last = NULL;
	unpack = &_unpack;
	dsk = page->dsk;
	ret = 0;
	huffman = btree->huffman_key;

	WT_ERR(__wt_scr_alloc(session, 0, &current));
	WT_ERR(__wt_scr_alloc(session, 0, &last));

	/*
	 * Internal row-store page entries map one-to-two to the number of
	 * physical entries on the page (each in-memory entry is a key item
	 * and location cookie).
	 */
	nindx = dsk->u.entries / 2;
	WT_RET((__wt_calloc_def(session, (size_t)nindx, &page->u.intl.t)));
	if (inmem_sizep != NULL)
		*inmem_sizep += nindx * sizeof(*page->u.intl.t);

	/*
	 * Set the number of elements now -- we're about to allocate memory,
	 * and if we fail in the middle of the page, we want to discard that
	 * memory properly.
	 */
	page->entries = nindx;

	/*
	 * Walk the page, instantiating keys: the page contains sorted key and
	 * location cookie pairs.  Keys are on-page/overflow items and location
	 * cookies are WT_CELL_ADDR items.
	 */
	ref = page->u.intl.t;
	WT_CELL_FOREACH(btree, dsk, cell, unpack, i) {
		__wt_cell_unpack(cell, unpack);
		switch (unpack->type) {
		case WT_CELL_KEY:
		case WT_CELL_KEY_OVFL:
			break;
		case WT_CELL_ADDR:
			ref->addr = cell;
			++ref;
			continue;
		WT_ILLEGAL_VALUE(session);
		}

		/*
		 * If Huffman decoding is required or it's an overflow record,
		 * use the heavy-weight __wt_cell_unpack_copy() call to build
		 * the key.  Else, we can do it faster internally as we don't
		 * have to shuffle memory around as much.
		 */
		prefix = unpack->prefix;
		if (huffman != NULL || unpack->ovfl) {
			WT_RET(__wt_cell_unpack_copy(session, unpack, current));

			/*
			 * If there's a prefix, make sure there's enough buffer
			 * space, then shift the decoded data past the prefix
			 * and copy the prefix into place.
			 */
			if (prefix != 0) {
				WT_ERR(__wt_buf_grow(
				    session, current, prefix + current->size));
				memmove((uint8_t *)current->data +
				    prefix, current->data, current->size);
				memcpy(
				    (void *)current->data, last->data, prefix);
				current->size += prefix;
			}
		} else {
			/*
			 * Get the cell's data/length and make sure we have
			 * enough buffer space.
			 */
			WT_ERR(__wt_buf_grow(
			    session, current, prefix + unpack->size));

			/* Copy the prefix then the data into place. */
			if (prefix != 0)
				memcpy((void *)
				    current->data, last->data, prefix);
			memcpy((uint8_t *)
			    current->data + prefix, unpack->data, unpack->size);
			current->size = prefix + unpack->size;
		}

		/*
		 * Allocate and initialize the instantiated key.
		 */
		WT_ERR(__wt_row_ikey_alloc(session,
		    WT_PAGE_DISK_OFFSET(page, cell),
		    current->data, current->size, (WT_IKEY **)&ref->u.key));
		if (inmem_sizep != NULL)
			*inmem_sizep += sizeof(WT_IKEY) + current->size;

		/*
		 * Swap buffers if it's not an overflow key, we have a new
		 * prefix-compressed key.
		 */
		if (!unpack->ovfl) {
			tmp = last;
			last = current;
			current = tmp;
		}
	}

err:	__wt_scr_free(&current);
	__wt_scr_free(&last);
	return (ret);
}

/*
 * __inmem_row_leaf --
 *	Build in-memory index for row-store leaf pages.
 */
static int
__inmem_row_leaf(WT_SESSION_IMPL *session, WT_PAGE *page, size_t *inmem_sizep)
{
	WT_BTREE *btree;
	WT_CELL *cell;
	WT_CELL_UNPACK *unpack, _unpack;
	WT_PAGE_HEADER *dsk;
	WT_ROW *rip;
	uint32_t i, nindx;

	btree = session->btree;
	dsk = page->dsk;
	unpack = &_unpack;

	/*
	 * Leaf row-store page entries map to a maximum of two-to-one to the
	 * number of physical entries on the page (each physical entry might be
	 * a key without a subsequent data item).  To avoid over-allocation in
	 * workloads with large numbers of empty data items, first walk the page
	 * counting the number of keys, then allocate the indices.
	 *
	 * The page contains key/data pairs.  Keys are on-page (WT_CELL_KEY) or
	 * overflow (WT_CELL_KEY_OVFL) items, data are either a single on-page
	 * (WT_CELL_VALUE) or overflow (WT_CELL_VALUE_OVFL) item.
	 */
	nindx = 0;
	WT_CELL_FOREACH(btree, dsk, cell, unpack, i) {
		__wt_cell_unpack(cell, unpack);
		switch (unpack->type) {
		case WT_CELL_KEY:
		case WT_CELL_KEY_OVFL:
			++nindx;
			break;
		case WT_CELL_VALUE:
		case WT_CELL_VALUE_OVFL:
			break;
		WT_ILLEGAL_VALUE(session);
		}
	}

	WT_RET((__wt_calloc_def(session, (size_t)nindx, &page->u.row.d)));
	if (inmem_sizep != NULL)
		*inmem_sizep += nindx * sizeof(*page->u.row.d);

	/* Walk the page again, building indices. */
	rip = page->u.row.d;
	WT_CELL_FOREACH(btree, dsk, cell, unpack, i) {
		__wt_cell_unpack(cell, unpack);
		switch (unpack->type) {
		case WT_CELL_KEY:
		case WT_CELL_KEY_OVFL:
			rip->key = cell;
			++rip;
			break;
		case WT_CELL_VALUE:
		case WT_CELL_VALUE_OVFL:
			break;
		WT_ILLEGAL_VALUE(session);
		}
	}

	page->entries = nindx;

	/*
	 * If the keys are Huffman encoded, instantiate some set of them.  It
	 * doesn't matter if we are randomly searching the page or scanning a
	 * cursor through it, there isn't a fast-path to getting keys off the
	 * page.
	 */
	return (btree->huffman_key == NULL ?
	    0 : __wt_row_leaf_keys(session, page));
}
