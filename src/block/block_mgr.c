/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

static int
__bm_invalid(WT_SESSION_IMPL *session)
{
	WT_RET_MSG(session, EINVAL, "invalid block manager handle");
}

/*
 * __wt_bm_addr_valid --
 *	Return if an address cookie is valid.
 */
int
__wt_bm_addr_valid(
    WT_SESSION_IMPL *session, const uint8_t *addr, uint32_t addr_size)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_addr_valid(session, block, addr, addr_size));
}

#ifdef HAVE_DIAGNOSTIC
/*
 * __wt_bm_addr_stderr --
 *	Print an address on stderr.
 */
int
__wt_bm_addr_stderr(
    WT_SESSION_IMPL *session, const uint8_t *addr, uint32_t addr_size)
{
	WT_BLOCK *block;
	WT_ITEM *buf;
	int ret;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	WT_RET(__wt_scr_alloc(session, 0, &buf));
	ret = __wt_block_addr_string(session, block, buf, addr, addr_size);
	if (ret == 0)
		fprintf(stderr, "%s\n", (char *)buf->data);
	__wt_scr_free(&buf);
	return (ret);
}
#endif

/*
 * __wt_bm_addr_string
 *	Return a printable string representation of an address cookie.
 */
int
__wt_bm_addr_string(WT_SESSION_IMPL *session,
    WT_ITEM *buf, const uint8_t *addr, uint32_t addr_size)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (
	    __wt_block_addr_string(session, block, buf, addr, addr_size));
}

/*
 * __wt_bm_create --
 *	Create a new file.
 */
int
__wt_bm_create(WT_SESSION_IMPL *session, const char *filename)
{
	return (__wt_block_create(session, filename));
}

/*
 * __wt_bm_open --
 *	Open a file.
 */
int
__wt_bm_open(WT_SESSION_IMPL *session,
    const char *filename, const char *config, const char *cfg[], int salvage)
{
	/*
	 * !!!
	 * As part of block-manager configuration, we need to return the maximum
	 * sized address cookie that a block manager will ever return.  There's
	 * a limit of WT_BM_MAX_ADDR_COOKIE, but at 255B, WT_BM_MAX_ADDR_COOKIE
	 * is too large for a Btree with 512B internal pages.  The default block
	 * manager packs an off_t and 2 uint32_t's into its cookie, so there's
	 * no problem now, but when we create a block manager extension API,
	 * we need some way to consider the block manager's maximum cookie size
	 * vs. the minimum Btree internal node size.
	 */
	return (__wt_block_open(
	    session, filename, config, cfg, salvage, &session->btree->block));
}

/*
 * __wt_bm_close --
 *	Close a file.
 */
int
__wt_bm_close(WT_SESSION_IMPL *session)
{
	WT_BLOCK *block;
	int ret;

	if ((block = session->btree->block) == NULL)
		return (0);

	ret = __wt_block_close(session, block);
	session->btree->block = NULL;

	return (ret);
}

/*
 * __wt_bm_truncate --
 *	Truncate a file.
 */
int
__wt_bm_truncate(WT_SESSION_IMPL *session, const char *filename)
{
	return (__wt_block_truncate(session, filename));
}

/*
 * __wt_bm_free --
 *	Free a block of space to the underlying file.
 */
int
__wt_bm_free(WT_SESSION_IMPL *session, const uint8_t *addr, uint32_t addr_size)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_free_buf(session, block, addr, addr_size));
}

/*
 * __wt_bm_read --
 *	Read a address cookie-referenced block into a buffer.
 */
int
__wt_bm_read(WT_SESSION_IMPL *session,
    WT_ITEM *buf, const uint8_t *addr, uint32_t addr_size)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_read_buf(session, block, buf, addr, addr_size));
}

/*
 * __wt_bm_block_header --
 *	Return the size of the block manager's header.
 */
int
__wt_bm_block_header(WT_SESSION_IMPL *session, uint32_t *headerp)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_header(session, block, headerp));
}

/*
 * __wt_bm_write_size --
 *	Return the buffer size required to write a block.
 */
int
__wt_bm_write_size(WT_SESSION_IMPL *session, uint32_t *sizep)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_write_size(session, block, sizep));
}

/*
 * __wt_bm_write --
 *	Write a buffer into a block, returning the block's address cookie.
 */
int
__wt_bm_write(
    WT_SESSION_IMPL *session, WT_ITEM *buf, uint8_t *addr, uint32_t *addr_size)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_write_buf(session, block, buf, addr, addr_size));
}

/*
 * __wt_bm_stat --
 *	Block-manager statistics.
 */
int
__wt_bm_stat(WT_SESSION_IMPL *session)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	__wt_block_stat(session, block);
	return (0);
}

/*
 * __wt_bm_salvage_start --
 *	Start a block manager salvage.
 */
int
__wt_bm_salvage_start(WT_SESSION_IMPL *session)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_salvage_start(session, block));
}

/*
 * __wt_bm_salvage_next --
 *	Return the next block from the file.
 */
int
__wt_bm_salvage_next(WT_SESSION_IMPL *session, WT_ITEM *buf,
    uint8_t *addr, uint32_t *addr_sizep, uint64_t *write_genp, int *eofp)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_salvage_next(
	    session, block, buf, addr, addr_sizep, write_genp, eofp));
}

/*
 * __wt_bm_salvage_end --
 *	End a block manager salvage.
 */
int
__wt_bm_salvage_end(WT_SESSION_IMPL *session, int success)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_salvage_end(session, block, success));
}

/*
 * __wt_bm_verify_start --
 *	Start a block manager salvage.
 */
int
__wt_bm_verify_start(WT_SESSION_IMPL *session, int *emptyp)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_verify_start(session, block, emptyp));
}

/*
 * __wt_bm_verify_end --
 *	End a block manager salvage.
 */
int
__wt_bm_verify_end(WT_SESSION_IMPL *session)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_verify_end(session, block));
}

/*
 * __wt_bm_verify_addr --
 *	Verify an address.
 */
int
__wt_bm_verify_addr(WT_SESSION_IMPL *session,
     const uint8_t *addr, uint32_t addr_size)
{
	WT_BLOCK *block;

	if ((block = session->btree->block) == NULL)
		return (__bm_invalid(session));

	return (__wt_block_verify_addr(session, block, addr, addr_size));
}
