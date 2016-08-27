/*-
 * Copyright (c) 2014-2015 MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"


#ifdef SSDM_OP4
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h> //for ioctl
#include <stdint.h> //for uint64_t
#include <inttypes.h> //for PRI64
#include <sys/types.h>
#include <linux/fs.h> //for FIBMAP
extern FILE* my_fp5;
#ifdef SSDM_OP4_2
extern int my_coll_streamid;
#endif
extern int my_journal_streamid;

#if defined(SSDM_OP4_3) || defined(SSDM_OP4_4)
extern int my_coll_left_streamid;
extern int my_coll_right_streamid;
extern off_t my_b;
extern uint64_t count1;
extern uint64_t count2;
#endif

#endif 

/*
 * __wt_read --
 *	Read a chunk.
 */
int
__wt_read(
    WT_SESSION_IMPL *session, WT_FH *fh, wt_off_t offset, size_t len, void *buf)
{
	size_t chunk;
	ssize_t nr;
	uint8_t *addr;

	WT_STAT_FAST_CONN_INCR(session, read_io);

	WT_RET(__wt_verbose(session, WT_VERB_FILEOPS,
	    "%s: read %" WT_SIZET_FMT " bytes at offset %" PRIuMAX,
	    fh->name, len, (uintmax_t)offset));

	/* Assert direct I/O is aligned and a multiple of the alignment. */
	WT_ASSERT(session,
	    !fh->direct_io ||
	    S2C(session)->buffer_alignment == 0 ||
	    (!((uintptr_t)buf &
	    (uintptr_t)(S2C(session)->buffer_alignment - 1)) &&
	    len >= S2C(session)->buffer_alignment &&
	    len % S2C(session)->buffer_alignment == 0));

	/* Break reads larger than 1GB into 1GB chunks. */
	for (addr = buf; len > 0; addr += nr, len -= (size_t)nr, offset += nr) {
		chunk = WT_MIN(len, WT_GIGABYTE);
		if ((nr = pread(fh->fd, addr, chunk, offset)) <= 0)
			WT_RET_MSG(session, nr == 0 ? WT_ERROR : __wt_errno(),
			    "%s read error: failed to read %" WT_SIZET_FMT
			    " bytes at offset %" PRIuMAX,
			    fh->name, chunk, (uintmax_t)offset);
	}
	return (0);
}

/*
 * __wt_write --
 *	Write a chunk.
 */
int
__wt_write(WT_SESSION_IMPL *session,
    WT_FH *fh, wt_off_t offset, size_t len, const void *buf)
{
	size_t chunk;
	ssize_t nw;
	const uint8_t *addr;
#if defined(SSDM_OP4) || defined(SSDM) || defined(SSDM_OP2)
	int my_ret;
	uint64_t off_tem;
	int stream_id;
#endif
	WT_STAT_FAST_CONN_INCR(session, write_io);

	WT_RET(__wt_verbose(session, WT_VERB_FILEOPS,
	    "%s: write %" WT_SIZET_FMT " bytes at offset %" PRIuMAX,
	    fh->name, len, (uintmax_t)offset));

	/* Assert direct I/O is aligned and a multiple of the alignment. */
	WT_ASSERT(session,
	    !fh->direct_io ||
	    S2C(session)->buffer_alignment == 0 ||
	    (!((uintptr_t)buf &
	    (uintptr_t)(S2C(session)->buffer_alignment - 1)) &&
	    len >= S2C(session)->buffer_alignment &&
	    len % S2C(session)->buffer_alignment == 0));
#ifdef SSDM_OP4
/*Naive multi-streamed,
 * stream-id 2: index
 * stream-id 3: journal
 * stream-id 4~7: collection if SSDM_OP4_2 is active
 * stream-id 1: others
 * Except collection, other file types are already assigned
 * stream_id in __wt_open() function
 * */
	//set stream_id depended on data types

#if defined(SSDM_OP4_3) || defined(SSDM_OP4_4)
	if(strstr(fh->name, "ycsb/collection") != 0){
		//Convert from file offset to 4096b block offset 
		off_tem = offset / 4096;
		my_ret = ioctl(fh->fd, FIBMAP, &off_tem);
		if(my_ret != 0){
			perror("ioctl");
		}
//		fprintf(stderr, "offset: %jd, LBA: %"PRIu64" \n", offset, off_tem);
	//	my_coll_streamid = 0; //unused 
		if(off_tem < (uint64_t)my_b){
			stream_id = my_coll_left_streamid;
			++count1;
		}
		else {
			stream_id = my_coll_right_streamid;
			++count2;
		}	
		my_ret = posix_fadvise(fh->fd, offset, stream_id, 8); //POSIX_FADV_DONTNEED=8
		if(my_ret != 0){
			fprintf(my_fp5, "error call posix_fadvise, my_ret=%d, error is %s\n",my_ret, strerror(errno));		
			perror("posix_fadvise");	
		}
	}
#endif  //defined(SSDM_OP4_3) || defined(SSDM_OP4_4)
#if defined(SSDM_OP4_2)
	//Increase streamd id for collecton when ckpt call follow 
	//round-robin fashion
	if(strstr(fh->name, "ycsb/collection") != 0){
	//	my_coll_left_streamid = my_coll_right_streamid = 0; //unused 
		stream_id = my_coll_streamid;
		my_ret = posix_fadvise(fh->fd, offset, stream_id, 8); //POSIX_FADV_DONTNEED=8
		if(my_ret != 0){
			fprintf(my_fp5, "error call posix_fadvise, my_ret=%d, error is %s\n",my_ret, strerror(errno));		
			perror("posix_fadvise");	
		}
	}
#endif //defined(SSDM_OP4_2)
//if both SSDM_OP4_2 and SSDM_OP4_3, and SSDM_OP_4 is not defined
//we use the default setting in __wt_open
//
#endif //ifdef SSDM_OP4
#ifdef SSDM_OP2 //size range method
	size_t ori_len = len;
	size_t STOP1 = 4096;
	size_t STOP2 = 28672;
	size_t STOP3 = 32768;
	int stream_id = 0;
	//Apply multi-streamed SSD 
	if (STOP3 <= ori_len) {
		stream_id = 4;
		//posix_fadvise(fh->fd, offset, 4, 8); //POSIX_FADV_DONTNEED=8
	}
	else if (STOP2 <= ori_len && ori_len < STOP3){
		stream_id = 3;
		//posix_fadvise(fh->fd, offset, 3, 8); //POSIX_FADV_DONTNEED=8
	}
	else if (STOP1 <= ori_len && ori_len < STOP2){
		stream_id = 2;
		//posix_fadvise(fh->fd, offset, 2, 8); //POSIX_FADV_DONTNEED=8
	}
	else {
		stream_id = 1;
		//posix_fadvise(fh->fd, offset, 1, 8); //POSIX_FADV_DONTNEED=8
	}
	my_ret = posix_fadvise(fh->fd, offset, stream_id, 8); //POSIX_FADV_DONTNEED=8
	if(my_ret != 0){
		perror("posix_fadvise");
	}
#endif //SSDM_OP2 
//This loop write is use for all both optimize version and original
	/* Break writes larger than 1GB into 1GB chunks. */
	for (addr = buf; len > 0; addr += nw, len -= (size_t)nw, offset += nw) {
		chunk = WT_MIN(len, WT_GIGABYTE);
		if ((nw = pwrite(fh->fd, addr, chunk, offset)) < 0)
			WT_RET_MSG(session, __wt_errno(),
			    "%s write error: failed to write %" WT_SIZET_FMT
			    " bytes at offset %" PRIuMAX,
			    fh->name, chunk, (uintmax_t)offset);
	}

	return (0);
}
