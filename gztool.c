//
// gztool
//
// Create small indexes for gzipped files and use them
// for quick and random data extraction.
// No more waiting when the end of a 10 GiB gzip is needed!
//
// based on parts from
// zlib/examples/zran.c & zlib/examples/zpipe.c by Mark Adler
// //github.com/madler/zlib
//
//.................................................
//
// LICENSE:
//
// v0.1, v0.2 by Roberto S. Galende, 2019-06
// //github.com/circulosmeos/gztool
// A work by Roberto S. Galende 
// distributed under the same License terms covering
// zlib from Mark Adler (aka Zlib license):
//   This software is provided 'as-is', without any express or implied
//   warranty.  In no event will the authors be held liable for any damages
//   arising from the use of this software.
//   Permission is granted to anyone to use this software for any purpose,
//   including commercial applications, and to alter it and redistribute it
//   freely, subject to the following restrictions:
//   1. The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software
//      in a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//   3. This notice may not be removed or altered from any source distribution.
//
// License contained in zlib.h by Mark Adler is copied here:
//
/* zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.2.11, January 15th, 2017
  Copyright (C) 1995-2017 Jean-loup Gailly and Mark Adler
  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.
  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:
  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
  The data format used by the zlib library is described by RFCs (Request for
  Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950
  (zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
*/
//.................................................
//
// Original zran.c notice is copied here:
//
    /* zran.c -- example of zlib/gzip stream indexing and random access
     * Copyright (C) 2005, 2012 Mark Adler
     * For conditions of distribution and use, see copyright notice in zlib.h
       Version 1.1  29 Sep 2012  Mark Adler */

    /* Version History:
     1.0  29 May 2005  First version
     1.1  29 Sep 2012  Fix memory reallocation error
     */

    /* Illustrate the use of Z_BLOCK, inflatePrime(), and inflateSetDictionary()
       for random access of a compressed file.  A file containing a zlib or gzip
       stream is provided on the command line.  The compressed stream is decoded in
       its entirety, and an index built with access points about every SPAN bytes
       in the uncompressed output.  The compressed file is left open, and can then
       be read randomly, having to decompress on the average SPAN/2 uncompressed
       bytes before getting to the desired block of data.

       An access point can be created at the start of any deflate block, by saving
       the starting file offset and bit of that block, and the 32K bytes of
       uncompressed data that precede that block.  Also the uncompressed offset of
       that block is saved to provide a referece for locating a desired starting
       point in the uncompressed stream.  build_index() works by decompressing the
       input zlib or gzip stream a block at a time, and at the end of each block
       deciding if enough uncompressed data has gone by to justify the creation of
       a new access point.  If so, that point is saved in a data structure that
       grows as needed to accommodate the points.

       To use the index, an offset in the uncompressed data is provided, for which
       the latest access point at or preceding that offset is located in the index.
       The input file is positioned to the specified location in the index, and if
       necessary the first few bits of the compressed data is read from the file.
       inflate is initialized with those bits and the 32K of uncompressed data, and
       the decompression then proceeds until the desired offset in the file is
       reached.  Then the decompression continues to read the desired uncompressed
       data from the file.

       Another approach would be to generate the index on demand.  In that case,
       requests for random access reads from the compressed data would try to use
       the index, but if a read far enough past the end of the index is required,
       then further index entries would be generated and added.

       There is some fair bit of overhead to starting inflation for the random
       access, mainly copying the 32K byte dictionary.  So if small pieces of the
       file are being accessed, it would make sense to implement a cache to hold
       some lookahead and avoid many calls to extract() for small lengths.

       Another way to build an index would be to use inflateCopy().  That would
       not be constrained to have access points at block boundaries, but requires
       more memory per access point, and also cannot be saved to file due to the
       use of pointers in the state.  The approach here allows for storage of the
       index in a file.
     */
//

// .................................................
// large file support (LFS) (files with size >2^31 (2 GiB) in linux, and >4 GiB in Windows)
#define _FILE_OFFSET_BITS 64    // defining _FILE_OFFSET_BITS with the value 64 (before including
                                // any header files) will turn off_t into a 64-bit type
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE     // off64_t for fseek64
// .................................................

#include <stdint.h> // uint32_t, uint64_t, UINT32_MAX
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include <unistd.h> // getopt(), access(), sleep()
#include <ctype.h>  // isprint()
#include <sys/stat.h> // stat()

// sets binary mode for stdin in Windows
#define STDIN 0
#define STDOUT 1
#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

#define local static

#define SPAN 10485760L      /* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */
#define UNCOMPRESSED_WINDOW UINT32_MAX // window is an uncompressed WINSIZE size window
#define GZIP_INDEX_IDENTIFIER_STRING "gzipindx"
#define GZIP_INDEX_HEADER_SIZE 16
// waiting time in seconds when supervising a growing gzip file:
#define WAITING_TIME 4

/* access point entry */
struct point {
    off_t out;             /* corresponding offset in uncompressed data */
    off_t in;              /* offset in input file of first full byte */
    uint32_t bits;         /* number of bits (1-7) from byte at in - 1, or 0 */
    off_t window_beginning;/* offset at index file where this compressed window is stored */
    uint32_t window_size;  /* size of (compressed) window */
    unsigned char *window; /* preceding 32K of uncompressed data, compressed */
};
// NOTE: window_beginning is not stored on disk, is an on-memory-only value

/* access point list */
struct access {
    uint64_t have;      /* number of list entries filled in */
    uint64_t size;      /* number of list entries allocated */
    uint64_t file_size; /* size of uncompressed file (useful for bgzip files) */
    struct point *list; /* allocated list */
    unsigned char *file_name; /* path to index file */
};
// NOTE: file_name is not stored on disk, is an on-memory-only value

/* generic struct to return a function error code and a value */
struct returned_output {
    uint64_t value;
    int error;
};

enum EXIT_APP_VALUES { EXIT_OK = 0, EXIT_GENERIC_ERROR = 1, EXIT_INVALID_OPTION = 2 };

enum SUPERVISE_OPTIONS { SUPERVISE_DONT = 0, SUPERVISE_DO = 1 };

/**************
 * Endianness *
 **************/
static inline int endiannes_is_big(void)
{
    long one= 1;
    return !(*((char *)(&one)));
}
static inline uint32_t endiannes_swap_4(uint32_t v)
{
    v = ((v & 0x0000FFFFU) << 16) | (v >> 16);
    return ((v & 0x00FF00FFU) << 8) | ((v & 0xFF00FF00U) >> 8);
}
static inline void *endiannes_swap_4p(void *x)
{
    *(uint32_t*)x = endiannes_swap_4(*(uint32_t*)x);
    return x;
}
static inline uint64_t endiannes_swap_8(uint64_t v)
{
    v = ((v & 0x00000000FFFFFFFFLLU) << 32) | (v >> 32);
    v = ((v & 0x0000FFFF0000FFFFLLU) << 16) | ((v & 0xFFFF0000FFFF0000LLU) >> 16);
    return ((v & 0x00FF00FF00FF00FFLLU) << 8) | ((v & 0xFF00FF00FF00FF00LLU) >> 8);
}
static inline void *endiannes_swap_8p(void *x)
{
    *(uint64_t*)x = endiannes_swap_8(*(uint64_t*)x);
    return x;
}

// fread() substitute for endianness management of 4 and 8 bytes words
// Data in *ptr will be received as this target system waits them
// independently of how they were stored (always as big endian).
// INPUT:
// void * ptr   : pointer to buffer where data will be received
// size_t size  : size of data in bytes
// FILE * stream: stream to read
// OUTPUT:
// number of bytes read
local size_t fread_endian( void * ptr, size_t size, FILE * stream ) {

    size_t output = fread(ptr, size, 1, stream);

    if (endiannes_is_big()) {
        ;
    } else {
        // machine is little endian: flip bytes
        switch (size) {
            case 4:
                endiannes_swap_4p(ptr);
                break;
            case 8:
                endiannes_swap_8p(ptr);
                break;
            default:
                assert(0);
        }
    }

    return output;

}

// fwrite() substitute for endianness management of 4 and 8 bytes words
// Data in *ptr will be stored as big endian
// independently of how they were stored in this system.
// INPUT:
// void * ptr   : pointer to buffer where data is stored
// size_t size  : size of data in bytes
// FILE * stream: stream to write
// OUTPUT:
// number of bytes written
local size_t fwrite_endian( void * ptr, size_t size, FILE * stream ) {

    size_t output;
    void *endian_ptr;

    endian_ptr = malloc(size);

    if (endiannes_is_big()) {
        memcpy(endian_ptr, ptr, size);
    } else {
        // machine is little endian: flip bytes
        switch (size) {
            case 4:
                *(uint32_t *)endian_ptr = *(uint32_t *)ptr;
                endiannes_swap_4p(endian_ptr);
                break;
            case 8:
                *(uint64_t *)endian_ptr = *(uint64_t *)ptr;
                endiannes_swap_8p(endian_ptr);
                break;
            default:
                assert(0);
        }
    }

    output = fwrite(endian_ptr, size, 1, stream);

    free(endian_ptr);
    return output;

}
/*******************
 * end: Endianness *
 *******************/


// compression function
// based on def() from zlib/examples/zpipe.c by Mark Adler :
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// unsigned char *source: pointer to data
// uint64_t *size       : size of data in bytes
// int level            : level of compression
// OUTPUT:
// pointer to compressed data (NULL on error)
local unsigned char *compress_chunk(unsigned char *source, uint64_t *size, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    uint64_t i = 0;
    uint64_t output_size = 0;
    uint64_t input_size = *size;
    unsigned char *in, *out, *out_complete;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return NULL;

    in = malloc(CHUNK);
    out = malloc(CHUNK);
    out_complete = malloc(CHUNK);

    /* compress until end of input */
    do {
        strm.avail_in = ((i + CHUNK) < input_size) ? CHUNK : (input_size - i);
        if ( memcpy(in, source + i, strm.avail_in) == NULL ) {
            (void)deflateEnd(&strm);
            goto compress_chunk_error;
        }
        flush = ((i + CHUNK) >= input_size) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;
        i += strm.avail_in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if ( have != 0 && (
                 NULL == (out_complete = realloc(out_complete, output_size + have)) ||
                 NULL == memcpy(out_complete + output_size, out, have)
                 ) ) {
                (void)deflateEnd(&strm);
                goto compress_chunk_error;
            }
            output_size += have;
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in source processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    free(in);
    free(out);
    // return size of returned char array in size pointer parameter
    *size = output_size;
    return out_complete;

  compress_chunk_error:
    free(in);
    free(out);
    free(out_complete);
    return NULL;

}


// decompression function,
// based on inf() from zlib/examples/zpipe.c by Mark Adler
    /* Decompress from file source to file dest until stream ends or EOF.
       inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_DATA_ERROR if the deflate data is
       invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
       the version of the library linked do not match, or Z_ERRNO if there
       is an error reading or writing the files. */
// INPUT:
// unsigned char *source: pointer to data
// uint64_t *size       : size of data in bytes
// OUTPUT:
// pointer to decompressed data (NULL on error)
local unsigned char *decompress_chunk(unsigned char *source, uint64_t *size)
{
    int ret;
    unsigned have;
    z_stream strm;
    uint64_t i = 0;
    uint64_t output_size = 0;
    uint64_t input_size = *size;
    unsigned char *in, *out, *out_complete;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return NULL;

    in = malloc(CHUNK);
    out = malloc(CHUNK);
    out_complete = malloc(CHUNK);

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = (i + CHUNK < input_size) ? CHUNK : (input_size - i);
        if ( memcpy(in, source + i, strm.avail_in) == NULL ) {
            (void)inflateEnd(&strm);
            goto decompress_chunk_error;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;
        i += strm.avail_in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                goto decompress_chunk_error;
            }
            have = CHUNK - strm.avail_out;
            if ( have != 0 && (
                 NULL == (out_complete = realloc(out_complete, output_size + have)) ||
                 NULL == memcpy(out_complete + output_size, out, have)
                 ) ) {
                (void)inflateEnd(&strm);
                goto decompress_chunk_error;
            }
            output_size += have;
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    free(in);
    free(out);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        fprintf(stderr, "Decompression of index' chunk terminated with error (%d).\n", ret);
    }
    // return size of returned char array in size pointer parameter
    *size = output_size;
    return out_complete;

  decompress_chunk_error:
    free(in);
    free(out);
    free(out_complete);
    return NULL;
}


// Deallocate from memory
// an index built by build_index()
// INPUT:
// struct access *index: pointer to index
local void free_index(struct access *index)
{
    uint64_t i;
    if (index != NULL) {
        for(i=0; i < index->have; i++) {
            free(index->list[i].window);
        }
        free(index->list);
        free(index->file_name);
        free(index);
    }
}


// fill pointers of and index->list with NULL windows
// so that free_index() can proceed on any contingency
// INPUT:
// struct access *index : pointer to index
// uint64_t from        : initial point to fill
// uint64_t to          : last point to fill
local void empty_index_list(struct access *index, uint64_t from, uint64_t to)
{
    uint64_t i;
    for (i=from; i<to; i++) {
        index->list[i].window = NULL;
        index->list[i].window_size = 0;
    }
    return;
}


// Allocate an index structure
// and fill it with empty values
// OUTPUT:
// pointer to index
local struct access *create_empty_index()
{
    struct access *index;
    if ( NULL == ( index = malloc(sizeof(struct access)) ) ) {
        return NULL;
    }
    index->file_size = 0;
    index->list = NULL;
    index->file_name = NULL;

    index->list = malloc(sizeof(struct point) << 3);
    empty_index_list( index, 0, 8 );
    if (index->list == NULL) {
        free(index);
        return NULL;
    }
    index->size = 8;
    index->have = 0;

    return index;
}


/* Add an entry to the access point list.  If out of memory, deallocate the
   existing list and return NULL. */
// INPUT:
// struct access *index : pointer to original index
// uint32_t bits        : (new entry data)
// off_t in             : (new entry data)
// off_t out            : (new entry data)
// unsigned left        : (new entry data)
// unsigned char *window: window data, already compressed with size window_size,
//                        or uncompressed with size WINSIZE,
//                        or store an empty window (NULL) because it resides on file.
// uint32_t window_size : 0: compress passed window of size WINSIZE
//                       >0: store window, of size window_size, as it is, in point structure
// OUTPUT:
// pointer to (new) index (NULL on error)
local struct access *addpoint(struct access *index, uint32_t bits,
    off_t in, off_t out, unsigned left, unsigned char *window, uint32_t window_size )
{
    struct point *next;
    uint64_t size = WINSIZE;
    unsigned char *compressed_chunk;

    /* if list is empty, create it (start with eight points) */
    if (index == NULL) {
        index = create_empty_index();
        if (index == NULL) return NULL;
    }

    /* if list is full, make it bigger */
    else if (index->have == index->size) {
        index->size <<= 1;
        next = realloc(index->list, sizeof(struct point) * index->size);
        if (next == NULL) {
            free_index(index);
            return NULL;
        }
        index->list = next;
        empty_index_list( index, index->have, index->size );
    }

    /* fill in entry and increment how many we have */
    next = index->list + index->have;
    next->bits = bits;
    next->in = in;
    next->out = out;

    if ( window_size == 0 ) {
        next->window_size = UNCOMPRESSED_WINDOW; // this marks an uncompressed WINSIZE next->window
        next->window = malloc(WINSIZE);
        if (left)
            memcpy(next->window, window + WINSIZE - left, left);
        if (left < WINSIZE)
            memcpy(next->window + left, window, WINSIZE - left);
        // compress window
        compressed_chunk = compress_chunk(next->window, &size, Z_DEFAULT_COMPRESSION);
        if (compressed_chunk == NULL) {
            fprintf(stderr, "Error whilst compressing index chunk\nProcess aborted\n.");
            return NULL;
        }
        free(next->window);
        next->window = compressed_chunk;
        /* uint64_t size and uint32_t window_size, but windows are small, so this will always fit */
        next->window_size = size;
    } else {
        if ( window == NULL ) {
            // create a NULL window: it resides on file,
            // and can/will later loaded on memory
            next->window_size = 0;
            next->window = NULL;
        } else {
            // passed window is already compressed: store as it is with size "window_size"
            next->window_size = window_size;
            next->window = window;
        }
    }

    index->have++;

    /* return list, possibly reallocated */
    return index;
}


// serialize index into a file
// Note that the function writes and empty header when called with index_last_written_point = 0
// and also writes next points from index_last_written_point to index->have.
// To create a valid index, this function must be called a last time (at least, a 2nd time)
// to write the correct header values (and the (optional) tail (size of uncompressed file)).
// INPUT:
// FILE *output_file    : output stream
// struct access *index : pointer to index
// uint64_t index_last_written_point : last index point already written to file
// OUTPUT:
// 0 on error, 1 on success
int serialize_index_to_file( FILE *output_file, struct access *index, uint64_t index_last_written_point ) {
    struct point *here;
    uint64_t temp;
    int i;

    ///* access point entry */
    //struct point {
    //    off_t out;          /* corresponding offset in uncompressed data */
    //    off_t in;           /* offset in input file of first full byte */
    //    int bits;           /* number of bits (1-7) from byte at in - 1, or 0 */
    //    unsigned char *window; /* preceding 32K of uncompressed data, compressed */
    //    int window_size;    /* size of window */
    //};
    //
    ///* access point list */
    //struct access {
    //    int have;           /* number of list entries filled in */
    //    int size;           /* number of list entries allocated */
    //    struct point *list; /* allocated list */
    //};

    /* proceed only if something reasonable to do */
    if (NULL == output_file || NULL == index) {
        return 0;
    }

    /*if (index->have <= 0)
        return 0;*/
    /* writing and empy index is allowed: writes the header (of size 4*8 = 32 bytes) */

    if ( index_last_written_point == 0 ) {
        /* write header */
        /* 0x0 8 bytes (to be compatible with .gzi for bgzip format: */
        /* the initial uint32_t is the number of bgzip-idx registers) */
        temp = 0;
        fwrite_endian(&temp, sizeof(temp), output_file);
        /* a 64 bits readable identifier */
        fprintf(output_file, GZIP_INDEX_IDENTIFIER_STRING);

        /* and now the raw data: the access struct "index" */

        // as this may be a growing index
        // values will be filled with special, not actual, values:
        // 0x0..0 , 0xf..f
        // Last write operation will overwrite these with correct values, that is, when
        // serialize_index_to_file() be called with index_last_written_point = index->have
        fwrite_endian(&temp, sizeof(temp), output_file); // have
        temp = UINT64_MAX;
        fwrite_endian(&temp, sizeof(temp), output_file); // size
    }

    if ( index_last_written_point != index->have ) {
        for (i = index_last_written_point; i < index->have; i++) {
            here = &(index->list[i]);
            fwrite_endian(&(here->out),  sizeof(here->out),  output_file);
            fwrite_endian(&(here->in),   sizeof(here->in),   output_file);
            fwrite_endian(&(here->bits), sizeof(here->bits), output_file);
            fwrite_endian(&(here->window_size), sizeof(here->window_size), output_file);
            here->window_beginning = ftello(output_file);
            if (NULL == here->window) {
                fprintf(stderr, "Index incomplete! - index writing aborted.\n");
                return 0;
            } else {
                fwrite(here->window, here->window_size, 1, output_file);
            }
            // once written, point's window CAN (and will now) BE DELETED from memory
            free(here->window);
            here->window = NULL;
        }
    } else {
        // Last write operation:
        // tail must be written:
        /* write size of uncompressed file (useful for bgzip files) */
        fwrite_endian(&(index->file_size), sizeof(index->file_size), output_file);
        // and header must be updated:
        // for this, move position to header:
        fseeko( output_file, sizeof(temp)*2, SEEK_SET );
        fwrite_endian(&index->have, sizeof(index->have), output_file);
        /* index->size is not written as only filled entries are usable */
        fwrite_endian(&index->have, sizeof(index->have), output_file);
    }

    // flush written content to disk
    fflush( output_file );

    return 1;
}


/* Make one entire pass through the compressed stream and build an index, with
   access points about every span bytes of uncompressed output -- span is
   chosen to balance the speed of random access against the memory requirements
   of the list, about 32K bytes per access point.  Note that data after the end
   of the first zlib or gzip stream in the file is ignored.  build_index()
   returns the number of access points on success (>= 1), Z_MEM_ERROR for out
   of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
   file read error.  On success, *built points to the resulting index. */
// INPUT:
// FILE *in             : input stream
// off_t span           : span
// struct access **built: address of index pointer, equivalent to passed by reference
// enum SUPERVISE_OPTIONS supervise = SUPERVISE_DONT: usual behaviour
//                                  = SUPERVISE_DO  : supervise a growing "in" gzip stream
// unsigned char *index_filename    : in case SUPERVISE_DO, index will be written on-the-fly
//                                    to this index file name.
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of built index (index->size)
local struct returned_output build_index(
    FILE *in, off_t span, struct access **built,
    enum SUPERVISE_OPTIONS supervise, unsigned char *index_filename )
{
    struct returned_output ret;
    off_t totin, totout;        /* our own total counters to avoid 4GB limit */
    off_t last;                 /* totout value of last access point */
    struct access *index;       /* access points being generated */
    z_stream strm;
    FILE *index_file = NULL;
    size_t index_last_written_point = 0;
    unsigned char input[CHUNK]; // TODO: convert to malloc
    unsigned char window[WINSIZE]; // TODO: convert to malloc

    ret.value = 0;
    ret.error = Z_OK;

    /* initialize inflate */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret.error = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding */
    if (ret.error != Z_OK)
        return ret;

    /* open index_filename for binary writing */
    // write index to index file:
    if ( strlen(index_filename) > 0 ) {
        index_file = fopen( index_filename, "wb" );
    } else {
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        index_file = stdout;
    }
    if ( NULL == index_file ) {
        fprintf( stderr, "Could not write index to file '%s'.\n", index_filename );
        goto build_index_error;
    }

    /* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */
    totin = totout = last = 0;
    index = NULL;               /* will be allocated by first addpoint() */
    strm.avail_out = 0;
    do {
        /* get some compressed data from input file */
        strm.avail_in = fread(input, 1, CHUNK, in);
        if ( supervise == SUPERVISE_DO &&
             strm.avail_in == 0 ) {
            // sleep and retry
            sleep( WAITING_TIME );
            continue;
        }
        if (ferror(in)) {
            ret.error = Z_ERRNO;
            goto build_index_error;
        }
        if (strm.avail_in == 0) {
            ret.error = Z_DATA_ERROR;
            goto build_index_error;
        }
        strm.next_in = input;

        /* process all of that, or until end of stream */
        do {
            /* reset sliding window if necessary */
            if (strm.avail_out == 0) {
                strm.avail_out = WINSIZE;
                strm.next_out = window;
            }

            /* inflate until out of input, output, or at end of block --
               update the total input and output counters */
            totin += strm.avail_in;
            totout += strm.avail_out;
            ret.error = inflate(&strm, Z_BLOCK);      /* return at end of block */
            totin -= strm.avail_in;
            totout -= strm.avail_out;
            if (ret.error == Z_NEED_DICT)
                ret.error = Z_DATA_ERROR;
            if (ret.error == Z_MEM_ERROR || ret.error == Z_DATA_ERROR)
                goto build_index_error;
            if (ret.error == Z_STREAM_END)
                break;

            /* if at end of block, consider adding an index entry (note that if
               data_type indicates an end-of-block, then all of the
               uncompressed data from that block has been delivered, and none
               of the compressed data after that block has been consumed,
               except for up to seven bits) -- the totout == 0 provides an
               entry point after the zlib or gzip header, and assures that the
               index always has at least one access point; we avoid creating an
               access point after the last block by checking bit 6 of data_type
             */
            if ((strm.data_type & 128) && !(strm.data_type & 64) &&
                (totout == 0 || totout - last > span)) {
                index = addpoint(index, strm.data_type & 7, totin,
                                 totout, strm.avail_out, window, 0);
                if (index == NULL) {
                    ret.error = Z_MEM_ERROR;
                    goto build_index_error;
                }
                last = totout;

                // write added point!
                // note that points written are automatically emptied of its window values
                // in order to use as less memory a s possible
                if ( ! serialize_index_to_file( index_file, index, index_last_written_point ) )
                    goto build_index_error;
                index_last_written_point = index->have;

            }
        } while (strm.avail_in != 0);
    } while (ret.error != Z_STREAM_END);

    /* clean up and return index (release unused entries in list) */
    (void)inflateEnd(&strm);
    index->list = realloc(index->list, sizeof(struct point) * index->have);
    index->size = index->have;
    index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */

    // once all index values are filled, close index file: a last call must be done
    // with index_last_written_point = index->have
    if ( ! serialize_index_to_file( index_file, index, index->have ) )
        goto build_index_error;
    fclose(index_file);

    if ( strlen(index_filename) > 0 )
        fprintf(stderr, "Index written to '%s'.\n", index_filename);
    else
        fprintf(stderr, "Index written to stdout.\n");

    *built = index;
    ret.value = index->size;
    return ret;

    /* return error */
  build_index_error:
    (void)inflateEnd(&strm);
    if (index != NULL)
        free_index(index);
    *built = NULL;
    if (index_file != NULL)
        fclose(index_file);
    return ret;
}


/* Use the index to read len bytes from offset into buf, return bytes read or
   negative for error (Z_DATA_ERROR or Z_MEM_ERROR).  If data is requested past
   the end of the uncompressed data, then extract() will return a value less
   than len, indicating how much as actually read into buf.  This function
   should not return a data error unless the file was modified since the index
   was generated.  extract() may also return Z_ERRNO if there is an error on
   reading or seeking the input file. */
// INPUT:
// FILE *in             : input stream
// struct access *index : pointer to index
// off_t offset         : offset in uncompressed bytes in source
// unsigned char *buf   : pointer to destination data buffer
//                        if buf is NULL, stdout will be used as output
// uint64_t len         : size of destination buffer `buf`
//                        If 0 && buf == NULL all input stream
//                        from `offset` will be inflated
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of data returned in `buf`
local struct returned_output extract(FILE *in, struct access *index, off_t offset,
                  unsigned char *buf, uint64_t len)
{
    struct returned_output ret;
    int i, skip;
    int output_to_stdout = 0;
    int extract_all_input = 0;
    unsigned have;
    z_stream strm;
    struct point *here;
    unsigned char input[CHUNK]; // TODO: convert to malloc
    unsigned char discard[WINSIZE]; // TODO: convert to malloc
    unsigned char *decompressed_window;
    uint64_t initial_len = len;

    ret.value = 0;
    ret.error = Z_OK;

    /* proceed only if something reasonable to do */
    if (NULL == in || NULL == index)
        return ret;
    if (len < 0)
        return ret;
    if (len == 0 && buf != NULL)
        return ret;

    /* print decompression to stdout if buf == NULL */
    if (buf == NULL) {
        // buf of size WINSIZE > CHUNK so probably
        // each CHUNK is totally deflated on each step
        if (NULL == (buf = malloc(WINSIZE))) {
            ret.error = Z_ERRNO;
            return ret;
        }
        output_to_stdout = 1;
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        if (len == 0) {
            extract_all_input = 1;
        }
    }

    /* find where in stream to start */
    here = index->list;
    i = index->have;
    while (--i && i!=0 && here[1].out <= offset)
        here++;

    /* initialize file and inflate state to start there */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret.error = inflateInit2(&strm, -15);         /* raw inflate */
    if (ret.error != Z_OK) {
        if (output_to_stdout == 1)
            free(buf);
        return ret;
    }
    if ( stdin == in ) {
        // read input until here->in - (here->bits ? 1 : 0)
        uint64_t pos = 0;
        uint64_t position = here->in - (here->bits ? 1 : 0);
        ret.error = 0;
        while ( pos < position ) {
            if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), in) ) {
                ret.error = -1;
                break;
            }
            pos += CHUNK;
        }
    } else {
        ret.error = fseeko(in, here->in - (here->bits ? 1 : 0), SEEK_SET);
    }
    if (ret.error == -1)
        goto extract_ret;
    if (here->bits) {
        i = getc(in);
        if (i == -1) {
            ret.error = ferror(in) ? Z_ERRNO : Z_DATA_ERROR;
            goto extract_ret;
        }
        (void)inflatePrime(&strm, here->bits, i >> (8 - here->bits));
    }

    if (here->window == NULL && here->window_beginning != 0) {
        /* index' window data is not on memory,
        but we have position and size on index file, so we load it now */
        FILE *index_file;
        if (NULL == (index_file = fopen(index->file_name, "rb")) ||
            0 != fseeko(index_file, here->window_beginning, SEEK_SET)
            ) {
            fprintf(stderr, "Error while opening index file. Extraction aborted.\n");
            fclose(index_file);
            ret.error = Z_ERRNO;
            goto extract_ret;
        }
        here->window_beginning = 0;
        if ( NULL == (here->window = malloc(here->window_size)) ||
            !fread(here->window, here->window_size, 1, index_file)
            ) {
            fprintf(stderr, "Error while reading index file. Extraction aborted.\n");
            fclose(index_file);
            ret.error = Z_ERRNO;
            goto extract_ret;
        }
        fclose(index_file);
    }

    if (here->window_size != UNCOMPRESSED_WINDOW) {
        /* decompress() use uint64_t counters, but index->list->window_size is smaller */
        uint64_t window_size = here->window_size;
        /* window is compressed on memory, so decompress it */
        decompressed_window = decompress_chunk(here->window, &window_size);
        free(here->window);
        here->window = decompressed_window;
        here->window_size = UNCOMPRESSED_WINDOW; // uncompressed WINSIZE next->window
    }

    (void)inflateSetDictionary(&strm, here->window, WINSIZE);

    /* skip uncompressed bytes until offset reached, then satisfy request */
    offset -= here->out;
    strm.avail_in = 0;
    skip = 1;                               /* while skipping to offset */
    do {
        /* define where to put uncompressed data, and how much */
        if (offset == 0 && skip) {          /* at offset now */
            strm.avail_out = len;
            strm.next_out = buf;
            skip = 0;                       /* only do this once */
        }
        if (offset > WINSIZE) {             /* skip WINSIZE bytes */
            strm.avail_out = WINSIZE;
            strm.next_out = discard;
            offset -= WINSIZE;
        }
        else if (offset != 0) {             /* last skip */
            strm.avail_out = (unsigned)offset;
            strm.next_out = discard;
            offset = 0;
        }

        if (offset == 0 && skip == 0) {         /* deflating */
            if (output_to_stdout == 1){
                if (extract_all_input == 0) {
                    strm.avail_out = (len <= WINSIZE) ? len : WINSIZE;
                } else {
                    strm.avail_out = WINSIZE;
                }
            }
        }

        /* uncompress until avail_out filled, or end of stream */
        do {
            if (skip == 0 && output_to_stdout == 1) {
                strm.next_out = buf;
                if (extract_all_input == 0) {
                    strm.avail_out = (len <= WINSIZE) ? len : WINSIZE;
                } else {
                    strm.avail_out = WINSIZE;
                }
            }
            if (strm.avail_in == 0) {
                strm.avail_in = fread(input, 1, CHUNK, in);
                if (ferror(in)) {
                    ret.error = Z_ERRNO;
                    goto extract_ret;
                }
                if (strm.avail_in == 0) {
                    ret.error = Z_DATA_ERROR;
                    goto extract_ret;
                }
                strm.next_in = input;
            }
            ret.error = inflate(&strm, Z_NO_FLUSH);       /* normal inflate */
            if (ret.error == Z_NEED_DICT)
                ret.error = Z_DATA_ERROR;
            if (ret.error == Z_MEM_ERROR || ret.error == Z_DATA_ERROR)
                goto extract_ret;
            if (skip == 0 && output_to_stdout == 1) {
                /* print decompression to stdout */
                have = WINSIZE - strm.avail_out;
                if (have > len && extract_all_input == 0) {
                    have = len;
                }
                if (fwrite(buf, 1, have, stdout) != have || ferror(stdout)) {
                    (void)inflateEnd(&strm);
                    ret.error = Z_ERRNO;
                    goto extract_ret;
                }
                if (skip == 0) {
                    if (extract_all_input == 0) {
                        len -= have;
                    } else {
                        len += have;
                    }
                }
            }
            if (ret.error == Z_STREAM_END)
                break;
        } while (
            // skipping output, but window not completely filled
            strm.avail_out != 0 ||
            // extracting to stdout and specified length not reached:
            (skip == 0 && output_to_stdout == 1 && len > 0) ||
            // extract the whole input (until break on Z_STREAM_END)
            (skip == 0 && extract_all_input == 1)
            );

        /* if reach end of stream, then don't keep trying to get more */
        if (ret.error == Z_STREAM_END)
            break;

        /* do until offset reached and requested data read, or stream ends */
    } while (skip);

    /* compute number of uncompressed bytes read after offset */
    ret.value = skip ? 0 : len - strm.avail_out;
    if (output_to_stdout == 1) {
        ret.value = initial_len - len;
    }
    if (extract_all_input == 1) {
        ret.value = len;
    }

    /* clean up and return bytes read or error */
  extract_ret:
    (void)inflateEnd(&strm);
    if (output_to_stdout == 1)
        free(buf);
    return ret;
}


// read index from a file
// INPUT:
// FILE *input_file         : input stream
// int load_windows         : 0 do not yet load windows on memory; 1 to load them
// unsigned char *file_name : file path to index file, needed in case
//                            load_windows==0, so a later extract() can access the
//                            index file again to read the window data.
// OUTPUT:
// struct access *index : pointer to index, or NULL on error
struct access *deserialize_index_from_file( FILE *input_file, int load_windows, unsigned char *file_name ) {
    struct point here;
    struct access *index = NULL;
    uint32_t i, still_growing = 0;
    uint64_t index_have, index_size, file_size;
    char header[GZIP_INDEX_HEADER_SIZE];
    struct stat st;

    // get index file size to calculate on-the-fly how many registers are
    // in order to being able to read still-growing index files (see `-S`)
    stat(file_name, &st);
    file_size = st.st_size;

    ///* access point entry */
    //struct point {
    //    off_t out;          /* corresponding offset in uncompressed data */
    //    off_t in;           /* offset in input file of first full byte */
    //    int bits;           /* number of bits (1-7) from byte at in - 1, or 0 */
    //    unsigned char window[WINSIZE];  /* preceding 32K of uncompressed data */
    //    int window_size;    /* size of window */
    //};
    //
    ///* access point list */
    //struct access {
    //    int have;           /* number of list entries filled in */
    //    int size;           /* number of list entries allocated */
    //    struct point *list; /* allocated list */
    //};

    fread(header, 1, GZIP_INDEX_HEADER_SIZE, input_file);
    if (*((uint64_t *)header) != 0 ||
        strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING, GZIP_INDEX_HEADER_SIZE/2) != 0) {
        fprintf(stderr, "File is not a valid gzip index file.\n");
        return NULL;
    }

    index = create_empty_index();

    fread_endian(&(index_have), sizeof(index_have), input_file);
    fread_endian(&(index_size), sizeof(index_size), input_file);

    // index->size equals index->have when the index file is correctly closed
    // and index->have == UINT64_MAX when the index is still growing
    if (index_have == 0 && index_size == UINT64_MAX) {
        fprintf(stderr, "Index file is still growing!\n");
        still_growing = 1;
    }


    // create the list of points
    do {

        fread_endian(&(here.out),  sizeof(here.out),  input_file);
        fread_endian(&(here.in),   sizeof(here.in),   input_file);
        fread_endian(&(here.bits), sizeof(here.bits), input_file);
        fread_endian(&(here.window_size), sizeof(here.window_size), input_file);

        if ( here.window_size == 0 ) {
            fprintf(stderr, "Unexpected window of size 0 found in index file '%s' @%ld.\nIgnoring point %ld.\n",
                    file_name, ftello(input_file), index->have + 1);
            continue;
        }

        if ( load_windows == 0 ) {
            // do not load window on here.window, but leave it on disk: this is marked with
            // a here.window_beginning to the position of the window on disk
            here.window = NULL;
            here.window_beginning = ftello(input_file);
            // and position into file as if read had occur
            if ( stdin == input_file ) {
                // read input until here.window_size
                uint64_t pos = 0;
                uint64_t position = here.window_size;
                unsigned char *input = malloc(CHUNK);
                if ( NULL == input ) {
                    fprintf(stderr, "Not enough memory to load index from stdin.\n");
                    goto deserialize_index_from_file_error;
                }
                while ( pos < position ) {
                    if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), input_file) ) {
                        fprintf(stderr, "Could not read index from stdin.\n");
                        goto deserialize_index_from_file_error;
                    }
                    pos += CHUNK;
                }
                free(input);
            } else {
                fseeko(input_file, here.window_size, SEEK_CUR);
            }
        } else {
            // load compressed window on memory:
            here.window = malloc(here.window_size);
            // load window on here.window: this is marked with
            // a here.window_beginning = 0 (which is impossible with gzipindx format)
            here.window_beginning = 0;
            if (here.window == NULL) {
                fprintf(stderr, "Not enough memory to load index from file.\n");
                goto deserialize_index_from_file_error;
            }
            if ( !fread(here.window, here.window_size, 1, input_file) ) {
                fprintf(stderr, "Error while reading index file.\n");
                goto deserialize_index_from_file_error;
            }
        }

        // increase index structure with a new point
        // (here.window can be NULL if load_windows==0)
        index = addpoint( index, here.bits, here.in, here.out, 0, here.window, here.window_size );

        // after increasing index, copy values which were not passed to addpoint():
        index->list[index->have - 1].window_beginning = here.window_beginning;
        index->list[index->have - 1].window_size = here.window_size;

        // note that even if (here.window != NULL) it MUST NOT be free() here, because
        // the pointer has been copied in a point of the index structure.

    } while (
        ( file_size - ftello(input_file) ) >
        // at least an empty window must enter, otherwise end loop:
        ( sizeof(here.out)+sizeof(here.in)+sizeof(here.bits)+
          sizeof(here.window_beginning)+sizeof(index->file_size) )
        );


    index->file_size = 0;

    if ( still_growing == 0 ){
        /* read size of uncompressed file (useful for bgzip files) */
        /* this field may not exist (maybe useful for growing gzip files?) */
        fread_endian(&(index->file_size), sizeof(index->file_size), input_file);
    }

    index->file_name = malloc( strlen(file_name) + 1 );
    if ( NULL == memcpy( index->file_name, file_name, strlen(file_name) + 1 ) ) {
        fprintf(stderr, "Not enough memory to load index from file.\n");
        goto deserialize_index_from_file_error;
    }

    return index;

  deserialize_index_from_file_error:
    free_index( index );
    return NULL;

}


// based on def from zlib/examples/zpipe.c by Mark Adler
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// FILE *source : source stream
// FILE *dest   : destination stream
// int level    : level of compression
// OUTPUT:
// Z_* error code or Z_OK on success
local int compress_file( FILE *source, FILE *dest, int level )
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char *in;
    unsigned char *out;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return ret;

    in = malloc(CHUNK);
    out = malloc(CHUNK);

    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)deflateEnd(&strm);
            goto compress_file_error;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                goto compress_file_error;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    free(in);
    free(out);
    return Z_OK;

  compress_file_error:
    free(in);
    free(out);
    return Z_ERRNO;

}


// based on inf from zlib/examples/zpipe.c by Mark Adler
    /* Decompress from file source to file dest until stream ends or EOF.
       inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_DATA_ERROR if the deflate data is
       invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
       the version of the library linked do not match, or Z_ERRNO if there
       is an error reading or writing the files. */
// INPUT:
// FILE *source : source stream
// FILE *dest   : destination stream
// OUTPUT:
// Z_* error code or Z_OK on success
local int decompress_file(FILE *source, FILE *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char *in;
    unsigned char *out;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    in = malloc(CHUNK);
    out = malloc(CHUNK);

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            ret = Z_ERRNO;
            goto decompress_file_error;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                goto decompress_file_error;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                ret = Z_ERRNO;
                goto decompress_file_error;
            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    free(in);
    free(out);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;

  decompress_file_error:
    free(in);
    free(out);
    return ret;

}


// write index for a gzip file
// INPUT:
// unsigned char *file_name     : file name of gzip file for which index will be calculated,
//                                If strlen(file_name) == 0 stdin is used as gzip file.
// struct access **index        : memory address of index pointer (passed by reference)
// unsigned char *index_filename: file name where index will be written
//                                If strlen(index_filename) == 0 stdout is used as output for index.
// int supervise                : value passed to build_index()
// off_t span_between_points    : span between index points in bytes
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_create_index(
    unsigned char *file_name, struct access **index,
    unsigned char *index_filename, int supervise, off_t span_between_points )
{

    FILE *in;
    struct returned_output ret;
    int waiting = 0;

    // open <FILE>:
    if ( strlen(file_name) > 0 ) {
wait_for_file_creation:
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            if (supervise == SUPERVISE_DO) {
                if ( waiting == 0 ) {
                    fprintf( stderr, "Waiting for creation of file '%s'\n", file_name );
                    waiting++;
                }
                sleep( WAITING_TIME );
                goto wait_for_file_creation;
            }
            fprintf( stderr, "Could not open %s for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
        fprintf( stderr, "Processing '%s' ...\n", file_name );
    } else {
        // stdin
        SET_BINARY_MODE(STDIN); // sets binary mode for stdin in Windows
        in = stdin;
        fprintf( stderr, "Processing stdin ...\n" );
    }

    // compute index:
    ret = build_index( in, span_between_points, index, supervise, index_filename );
    fclose(in);
    if ( ret.error < 0 ) {
        switch ( ret.error ) {
        case Z_MEM_ERROR:
            fprintf( stderr, "ERROR: Out of memory.\n" );
            break;
        case Z_DATA_ERROR:
            if ( strlen(file_name) > 0 )
                fprintf( stderr, "ERROR: Compressed data error in '%s'.\n", file_name );
            else
                fprintf( stderr, "ERROR: Compressed data error in stdin.\n" );
            break;
        case Z_ERRNO:
            if ( strlen(file_name) > 0 )
                fprintf( stderr, "ERROR: Read error on '%s'.\n", file_name );
            else
                fprintf( stderr, "ERROR: Read error on stdin.\n" );
            break;
        default:
           fprintf( stderr, "ERROR: Error %d while building index.\n", ret.error );
       }
       return EXIT_GENERIC_ERROR;
    }
    fprintf(stderr, "Built index with %ld access points.\n", ret.value);

    return EXIT_OK;

}


// extract data from a gzip file using its index file (creates it if it doesn't exist)
// INPUT:
// unsigned char *file_name     : gzip file name
// unsigned char *index_filename: index file name
// uint64_t extract_from_byte   : uncompressed offset of original data from which to extract
// int force_action             : if 1 and index file doesn't exist, create it
// off_t span_between_points    : span between index points in bytes
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_extract_from_byte(
    unsigned char *file_name, unsigned char *index_filename,
    uint64_t extract_from_byte, int force_action, off_t span_between_points )
{

    FILE *in = NULL;
    FILE *index_file = NULL;
    struct access *index = NULL;
    struct returned_output ret;
    int mark_recursion = 0;
    int ret_value;

    // open <FILE>:
    if ( strlen(file_name) > 0 ) {
        fprintf(stderr, "Extracting data from uncompressed byte @%ld in file '%s',\nusing index '%s'...\n",
            extract_from_byte, file_name, index_filename);
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            fprintf( stderr, "Could not open '%s' for reading.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
    } else {
        // stdin
        fprintf(stderr, "Extracting data from uncompressed byte @%ld on stdin,\nusing index '%s'...\n",
            extract_from_byte, index_filename);
        SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
        in = stdin;
    }
    // open index file (filename derived from <FILE> unless indicated with `-I`)
open_index_file:
    index_file = fopen( index_filename, "rb" );
    if ( NULL == index_file ) {
        if ( force_action == 1 && mark_recursion == 0 ) {
            // before extraction, create index file
            ret_value = action_create_index( file_name, &index, index_filename, SUPERVISE_DONT, span_between_points );
            if ( ret_value != EXIT_OK )
                goto action_extract_from_byte_error;
            // index file has been created, so it must now be opened
            mark_recursion = 1;
            goto open_index_file;
        } else {
            fprintf( stderr, "Index file '%s' not found.\n", index_filename );
            ret_value = EXIT_GENERIC_ERROR;
            goto action_extract_from_byte_error;
        }
    }
    // deserialize_index_from_file
    index = deserialize_index_from_file( index_file, 0, index_filename );
    if ( ! index ) {
        fprintf( stderr, "Could not read index from file '%s'\n", index_filename );
        ret_value = EXIT_GENERIC_ERROR;
        goto action_extract_from_byte_error;
    }
    ret = extract( in, index, extract_from_byte, NULL, 0 );
    if ( ret.error < 0 ) {
        fprintf( stderr, "Data extraction failed: %s error\n",
                ret.error == Z_MEM_ERROR ? "out of memory" : "input corrupted" );
        ret_value = EXIT_GENERIC_ERROR;
    } else {
        if ( strlen(file_name) > 0 )
            fprintf( stderr, "Extracted %ld bytes from '%s' to stdout.\n", ret.value, file_name );
        else
            fprintf( stderr, "Extracted %ld bytes from stdin to stdout.\n", ret.value );
        ret_value = EXIT_OK;
    }

action_extract_from_byte_error:
    if ( NULL != in )
        fclose( in );
    if ( NULL != index_file )
        fclose( index_file );
    if ( NULL != index )
        free_index( index );
    return ret_value;

}


// list info for an index file
// INPUT:
// unsigned char *file_name: index file name
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_list_info( unsigned char *file_name ) {

    FILE *in = NULL;
    struct access *index = NULL;
    uint64_t j;
    int ret_value;

    // open index file:
    if ( strlen(file_name) > 0 ) {
        fprintf( stderr, "Checking index file '%s' ...\n", file_name );
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            fprintf( stderr, "Could not open %s for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
    } else {
        // stdin
        fprintf( stderr, "Checking index from stdin ...\n" );
        SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
        in = stdin;
    }

    // in case in == stdin, file_name == "" but this doesn't matter as windows won't be deconmpressed
    index = deserialize_index_from_file( in, 0, file_name );

    if ( ! index ) {

        fprintf(stderr, "Could not read index from file '%s'.\n", file_name);
        ret_value = EXIT_GENERIC_ERROR;
        goto action_list_info_error;

    } else {

        fprintf( stderr, "\tNumber of index points:    %ld\n", index->have );
        if (index->file_size != 0)
            fprintf( stderr, "\tSize of uncompressed file: %ld\n", index->file_size );
        fprintf( stderr, "\tList of points:\n\t   @ compressed/uncompressed byte (index data size in Bytes), ...\n\t" );
        for (j=0; j<index->have; j++) {
            fprintf( stderr, "@ %ld / %ld ( %d ), ", index->list[j].in, index->list[j].out, index->list[j].window_size );
        }
        fprintf( stderr, "\n" );

    }

action_list_info_error:
    if ( NULL != in )
        fclose( in );
    if ( NULL != index )
        free_index( index );
    return ret_value;

}


// .................................................


// print help
local void print_help() {

    fprintf( stderr, " gztool (v0.2)\n GZIP files indexer and data retriever.\n");
    fprintf( stderr, " Create small indexes for gzipped files and use them\n for quick and random data extraction.\n" );
    fprintf( stderr, " No more waiting when the end of a 10 GiB gzip is needed!\n" );
    fprintf( stderr, " //github.com/circulosmeos/gztool (by Roberto S. Galende)\n" );
    fprintf( stderr, "\n  $ gztool [-b #] [-cdefhilsS] [-I <INDEX>] <FILE> ...\n\n" );
    fprintf( stderr, " -b #: extract data from indicated byte position number\n      of gzip file, using index\n" );
    fprintf( stderr, " -c: raw-gzip-compress indicated file to STDOUT\n" );
    fprintf( stderr, " -d: raw-gzip-decompress indicated file to STDOUT \n" );
    fprintf( stderr, " -e: if multiple files are indicated, continue on error\n" );
    fprintf( stderr, " -f: with `-i` force index overwriting if one exists\n" );
    fprintf( stderr, "     with `-b` force index creation if none exists\n" );
    fprintf( stderr, " -h: print this help\n" );
    fprintf( stderr, " -i: create index for indicated gzip file (For 'file.gz'\n" );
    fprintf( stderr, "     the default index file name will be 'file.gzi')\n" );
    fprintf( stderr, " -I INDEX: index file name will be 'INDEX'\n" );
    fprintf( stderr, " -l: list info contained in indicated index file\n" );
    fprintf( stderr, " -s #: span in MiB between index points. By default is 10.\n" );
    fprintf( stderr, " -S: supervise indicated file: create a growing index,\n" );
    fprintf( stderr, "     for a still-growing gzip file. (`-i` is  implicit).\n" );
    fprintf( stderr, "\n" );

}


// OUTPUT:
// EXIT_* error code or EXIT_OK on success
int main(int argc, char **argv)
{
    // variables for used for the different actions:
    struct returned_output ret;
    off_t offset;
    unsigned char *file_name = NULL;
    FILE *in = NULL;
    FILE *index_file = NULL;
    struct access *index = NULL;
    int bytes, position_in_buf;

    // variables for grabbing the options:
    uint64_t extract_from_byte = 0;
    off_t span_between_points = SPAN;
    unsigned char *index_filename = NULL;
    int continue_on_error = 0;
    int index_filename_indicated = 0;
    int force_action = 0;

    enum EXIT_APP_VALUES ret_value;
    enum ACTION
        { ACT_NOT_SET, ACT_EXTRACT_FROM_BYTE, ACT_COMPRESS_CHUNK, ACT_DECOMPRESS_CHUNK,
          ACT_CREATE_INDEX, ACT_LIST_INFO, ACT_HELP, ACT_SUPERVISE }
        action;

    int opt = 0;
    int i, j;
    int actions_set = 0;

    fprintf( stderr, "\n" );

    action = ACT_NOT_SET;
    ret_value = EXIT_OK;
    while ((opt = getopt(argc, argv, "b:cdefhiI:ls:S")) != -1)
        switch(opt) {
            // help
            case 'h':
                print_help();
                return EXIT_OK;
            // `-b #` extracts data from indicated position byte in uncompressed stream of <FILE>
            case 'b':
                // TODO: pass to strtoll() checking 0 and 0x
                extract_from_byte = atoll(optarg);
                // read from stdin and output to stdout
                action = ACT_EXTRACT_FROM_BYTE;
                actions_set++;
                break;
            // `-c` compress <FILE> (or stdin if none) to stdout
            case 'c':
                action = ACT_COMPRESS_CHUNK;
                actions_set++;
                break;
            // `-d` decompress <FILE> (or stdin if none) to stdout
            case 'd':
                action = ACT_DECOMPRESS_CHUNK;
                actions_set++;
                break;
            // `-e` continues on error if multiple input files indicated
            case 'e':
                continue_on_error = 1;
                break;
            // do not overwrite files unless `-f` is indicated
            case 'f':
                force_action = 1;
                break;
            // `-i` creates index for <FILE>
            case 'i':
                action = ACT_CREATE_INDEX;
                actions_set++;
                break;
            // list number of bytes, but not 0 valued
            // `-I INDEX_FILE` creates index for <FILE> with name <INDEX_FILE>
            case 'I':
                // action = ACT_CREATE_INDEX but only if no other option indicated
                index_filename_indicated = 1;
                index_filename = malloc( strlen(optarg) + 1 );
                memcpy( index_filename, optarg, strlen(optarg) + 1 );
                break;
            // `-l` list info of index <FILE>
            case 'l':
                action = ACT_LIST_INFO;
                actions_set++;
                break;
            // `-s #` span between index points, in MiB
            case 's':
                // TODO: pass to strtoll() checking 0 and 0x
                // span is converted to bytes for internal use
                span_between_points = atoll(optarg) * 1024 * 1024;
                break;
            // `-S` supervise a still-growing gzip <FILE> and create index for it
            case 'S':
                action = ACT_SUPERVISE;
                actions_set++;
                break;
            case '?':
                if ( isprint (optopt) ) {
                    // print warning only if char option is unknown
                    if ( NULL == strchr("bcdefhiIlS", optopt) ) {
                        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                        print_help();
                    }
                } else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                fprintf( stderr, "\n" );
                return EXIT_INVALID_OPTION;
            default:
                fprintf( stderr, "\n" );
                abort ();
        }

    // Checking parameter merging and absence
    if ( actions_set > 1 ) {
        fprintf(stderr, "Please, do not merge parameters `-bcdilS`.\nAborted.\n\n" );
        return EXIT_INVALID_OPTION;
    }
    if ( span_between_points != SPAN &&
        action != ACT_CREATE_INDEX && action != ACT_SUPERVISE ) {
        fprintf(stderr, "`-s` parameter will be ignored.\n" );
    }
    if ( actions_set == 0 ) {
        // `-I <FILE>` is equivalent to `-i -I <FILE>`
        if ( action == ACT_NOT_SET && index_filename_indicated  == 1 ) {
            action = ACT_CREATE_INDEX;
            if ( (optind + 1) < argc ) {
                // too much files indicated to use `-I`
                fprintf(stderr, "`-I` is incompatible with multiple input files.\nAborted.\n\n" );
                return EXIT_INVALID_OPTION;
            }
        } else {
            fprintf(stderr, "Please, indicate one parameter of `-bcdilS`, or `-h` for help.\nAborted.\n\n" );
            return EXIT_INVALID_OPTION;
        }
    }

    if (optind == argc || argc == 1) {

        // file input is stdin
        switch ( action ) {

            case ACT_EXTRACT_FROM_BYTE:
            if ( index_filename_indicated == 1 ) {
                ret_value = action_extract_from_byte(
                    "", index_filename, extract_from_byte, force_action, span_between_points );
                fprintf( stderr, "\n" );
                break;
            } else {
                fprintf( stderr, "`-I INDEX` must be used when extracting from stdin.\nAborted.\n\n" );
                ret_value = EXIT_GENERIC_ERROR;
                break;
            }

            case ACT_COMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != compress_file( stdin, stdout, Z_DEFAULT_COMPRESSION ) ) {
                    fprintf( stderr, "Error while compressing stdin.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                ret_value = EXIT_OK;
                break;

            case ACT_DECOMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != decompress_file( stdin, stdout ) ) {
                    fprintf( stderr, "Error while decompressing stdin.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                ret_value = EXIT_OK;
                break;

            case ACT_CREATE_INDEX:
                if ( force_action == 0 &&
                     index_filename_indicated == 1 &&
                     access( index_filename, F_OK ) != -1 ) {
                    // index file already exists
                    fprintf( stderr, "Index file '%s' already exists.\n", index_filename );
                    fprintf( stderr, "Use `-f` to force overwriting.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                // stdin is a gzip file that must be indexed
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename, SUPERVISE_DONT, span_between_points );
                } else {
                    ret_value = action_create_index( "", &index, "", SUPERVISE_DONT, span_between_points );
                }
                fprintf( stderr, "\n" );
                break;

            case ACT_LIST_INFO:
                // stdin is an index file that must be checked
                ret_value = action_list_info( "" );
                fprintf( stderr, "\n" );
                break;

            case ACT_SUPERVISE:
                // stdin is a gzip file for which an index file must be created on-the-fly
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename, SUPERVISE_DO, span_between_points );
                } else {
                    ret_value = action_create_index( "", &index, "", SUPERVISE_DO, span_between_points );
                }
                fprintf( stderr, "\n" );
                break;

        }

    } else {

        if ( action == ACT_SUPERVISE &&
             ( argc - optind > 1 ) ) {
            // supervise only accepts one input gz file
            fprintf( stderr, "`-S` option only accepts one gzip file parameter: %d indicated.\n", argc - optind );
            fprintf( stderr, "Aborted.\n" );
            return EXIT_GENERIC_ERROR;
        }

        for (i = optind; i < argc; i++) {

            file_name = argv[i];

            // if no index filename is set (`-I`), it is derived from each <FILE> parameter
            if ( 0 == index_filename_indicated ) {
                if ( NULL != index_filename ) {
                    free(index_filename);
                    index_filename = NULL;
                }
                index_filename = malloc( strlen(argv[i]) + 4 + 1 );
                if ( strstr(index_filename, ".gz") == (argv[i] + strlen(argv[i]) - 3) )
                    // if gzip-file name is 'FILE.gz', index file name will be 'FILE.gzi'
                    sprintf(index_filename, "%si", argv[i]);
                else
                    // otherwise, the complete extension '.gzi' is appended
                    sprintf(index_filename, "%s.gzi", argv[i]);
            }

            // free previous loop's resources
            if ( NULL != in ) {
                free( in );
                in = NULL;
            }
            if ( NULL != index ) {
                free_index( index );
                index = NULL;
            }
            if ( NULL != index_file ) {
                fclose( index_file );
                index_file = NULL;
            }

            if ( force_action == 0 &&
                 ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ) &&
                 access( index_filename, F_OK ) != -1 ) {
                // index file already exists
                fprintf( stderr, "Index file '%s' already exists.\n", index_filename );
                if ( continue_on_error == 1 ) {
                    continue;
                } else {
                    fprintf( stderr, "Use `-f` to force overwriting.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
            }

            // "-bil" options can accept multiple files
            switch ( action ) {

                case ACT_EXTRACT_FROM_BYTE:
                    ret_value = action_extract_from_byte(
                        file_name, index_filename, extract_from_byte, force_action, span_between_points );
                    break;

                case ACT_COMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        fprintf( stderr, "Error while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != compress_file( in, stdout, Z_DEFAULT_COMPRESSION ) ) {
                        fprintf( stderr, "Error while compressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_DECOMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        fprintf( stderr, "Error while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != decompress_file( in, stdout ) ) {
                        fprintf( stderr, "Error while decompressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_CREATE_INDEX:
                    ret_value = action_create_index( file_name, &index, index_filename, SUPERVISE_DONT, span_between_points );
                    break;

                case ACT_LIST_INFO:
                    ret_value = action_list_info( file_name );
                    break;

                case ACT_SUPERVISE:
                    ret_value = action_create_index( file_name, &index, index_filename, SUPERVISE_DO, span_between_points );
                    fprintf( stderr, "\n" );
                    break;

            }

            fprintf( stderr, "\n" );

            if ( continue_on_error = 0 &&
                 ret_value != EXIT_OK ) {
                fprintf( stderr, "Aborted.\n" );
                // break the for loop
                break;
            }

        }

    }

    // final freeing of resources
    if ( NULL != in ) {
        free( in );
    }
    if ( NULL != index ) {
        free_index( index );
    }
    if ( NULL != index_filename ) {
        free( index_filename );
    }
    if ( NULL != index_file ) {
        fclose( index_file );
    }

    return ret_value;

}
