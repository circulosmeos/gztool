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
// v0.1 to v0.8* by Roberto S. Galende, 2019
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
#include <stdarg.h> // va_start, va_list, va_end
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include <unistd.h> // getopt(), access(), sleep()
#include <ctype.h>  // isprint()
#include <sys/stat.h> // stat()
#include <math.h>   // pow()

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

#define GZTOOL_VERSION "0.8.1"

#define SPAN 10485760L      /* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */
#define UNCOMPRESSED_WINDOW UINT32_MAX // window is an uncompressed WINSIZE size window
#define GZIP_INDEX_IDENTIFIER_STRING "gzipindx"
#define GZIP_INDEX_HEADER_SIZE 16
// default waiting time in seconds when supervising a growing gzip file:
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
// NOTE: window_beginning is not stored on disk, it's an on-memory-only value

/* access point list */
struct access {
    uint64_t have;      /* number of list entries filled in */
    uint64_t size;      /* number of list entries allocated */
    uint64_t file_size; /* size of uncompressed file (useful for bgzip files) */
    struct point *list; /* allocated list */
    unsigned char *file_name; /* path to index file */
    int index_complete;     /* 1: index is complete; 0: index is (still) incomplete */
};
// NOTE: file_name and index_complete are not stored on disk (on-memory-only values)

/* generic struct to return a function error code and a value */
struct returned_output {
    uint64_t value;
    int error;
};

enum EXIT_APP_VALUES { EXIT_OK = 0, EXIT_GENERIC_ERROR = 1, EXIT_INVALID_OPTION = 2 };

enum INDEX_AND_EXTRACTION_OPTIONS { JUST_CREATE_INDEX, SUPERVISE_DO, SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, EXTRACT_FROM_BYTE, EXTRACT_TAIL };

enum ACTION
    { ACT_NOT_SET, ACT_EXTRACT_FROM_BYTE, ACT_COMPRESS_CHUNK, ACT_DECOMPRESS_CHUNK,
      ACT_CREATE_INDEX, ACT_LIST_INFO, ACT_HELP, ACT_SUPERVISE, ACT_EXTRACT_TAIL,
      ACT_EXTRACT_TAIL_AND_CONTINUE };

enum VERBOSITY_LEVEL { VERBOSITY_NONE = 0, VERBOSITY_NORMAL = 1, VERBOSITY_EXCESSIVE = 2,
                       VERBOSITY_MANIAC = 3, VERBOSITY_CRAZY = 4, VERBOSITY_NUTS = 5 };

enum VERBOSITY_LEVEL verbosity_level = VERBOSITY_NORMAL;


// `fprintf` substitute for printing with VERBOSITY_LEVEL
void printToStderr ( enum VERBOSITY_LEVEL verbosity, const char * format, ... ) {

    // if verbosity of message is above general verbosity_level, ignore message
    if ( verbosity <= verbosity_level ) {
        va_list args;
        va_start (args, format);
        vfprintf ( stderr, format, args );
        va_end (args);
    }

}


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
        printToStderr( VERBOSITY_NORMAL, "Decompression of index' chunk terminated with error (%d).\n", ret);
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
    index->index_complete = 0;

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
// uint32_t window_size : size of passed *window (may be already compressed o not)
// int compress_window  : 0: store window of size window_size, as it is, in point structure
//                        1: compress passed window of size window_size
// OUTPUT:
// pointer to (new) index (NULL on error)
local struct access *addpoint(struct access *index, uint32_t bits,
    off_t in, off_t out, unsigned left, unsigned char *window, uint32_t window_size, int compress_window )
{
    struct point *next;
    uint64_t size = window_size;
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

    if ( compress_window == 1 ) {
        next->window_size = window_size;
        next->window = NULL;
        if ( window_size > 0 ) {
            // compress window if window_size > 0: if not, a zero-length window is stored.
            next->window = malloc( window_size );
            // check left value with windows of size != WINSIZE
            if ( left > window_size ||
                 window_size != WINSIZE )
                left = 0;
            if (left)
                memcpy(next->window, window + window_size - left, left);
            if (left < window_size)
                memcpy(next->window + left, window, window_size - left);
            // compress window
            compressed_chunk = compress_chunk(next->window, &size, Z_DEFAULT_COMPRESSION);
            if (compressed_chunk == NULL) {
                printToStderr( VERBOSITY_NORMAL, "Error whilst compressing index chunk\nProcess aborted\n." );
                return NULL;
            }
            free(next->window);
            next->window = compressed_chunk;
            /* uint64_t size and uint32_t window_size, but windows are small, so this will always fit */
            next->window_size = size;
        }
        printToStderr( VERBOSITY_EXCESSIVE, "\t[%ld/%ld] window_size = %d\n", index->have+1, index->size, next->window_size);
    } else {
        // if NULL == window, this creates a NULL window: it resides on file,
        // and can/will later loaded on memory with its correct ->window_size;
        // if passed window is already compressed it will be stored as it is with size "window_size"
        next->window_size = window_size;
        next->window = window;
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
// uint64_t index_last_written_point : last index point already written to file: its values
//                                     go from 1 to index->have, so that 0 has special value "None".
// OUTPUT:
// 0 on error, 1 on success
int serialize_index_to_file( FILE *output_file, struct access *index, uint64_t index_last_written_point ) {
    struct point *here = NULL;
    uint64_t temp;
    uint64_t offset;
    uint64_t i;

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
        fseeko( output_file, 0, SEEK_SET);
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

    // update index->size on disk with index->have data
    // ( if index->have == 0, no index points still, maintain UINT64_MAX )
    if ( index->have > 0 ) {
        // seek to index->have position
        fseeko( output_file, 3*sizeof(temp), SEEK_SET );
        // write index->have value; (when the index be closed, index->size on disk will be >0)
        fwrite_endian( &(index->have), sizeof(index->have), output_file );
    }

    // fseek to index position of index_last_written_point
    offset = 4*sizeof(temp);
    for (i = 0; i < index_last_written_point; i++) {
        here = &(index->list[i]);
        offset += sizeof(here->out) + sizeof(here->in) +
                  sizeof(here->bits) + sizeof(here->window_size) +
                  ((here->window_size==UNCOMPRESSED_WINDOW)? WINSIZE: (here->window_size));
    }
    fseeko( output_file, offset, SEEK_SET);
    printToStderr( VERBOSITY_MANIAC, "index_last_written_point = %ld\n", index_last_written_point );
    if (NULL!=here) {
        printToStderr( VERBOSITY_MANIAC, "%ld->window_size = %d\n", i, here->window_size );
    }
    printToStderr( VERBOSITY_MANIAC, "have = %ld, offset = %ld\n", index->have, offset );

    if ( index_last_written_point != index->have ) {
        for (i = index_last_written_point; i < index->have; i++) {
            printToStderr( VERBOSITY_EXCESSIVE, "writing new point #%ld\n", i+1 );
            here = &(index->list[i]);
            fwrite_endian(&(here->out),  sizeof(here->out),  output_file);
            fwrite_endian(&(here->in),   sizeof(here->in),   output_file);
            fwrite_endian(&(here->bits), sizeof(here->bits), output_file);
            if ( here->window_size==UNCOMPRESSED_WINDOW ) {
                temp = WINSIZE;
                fwrite_endian(&(temp), sizeof(here->window_size), output_file);
            } else {
                fwrite_endian(&(here->window_size), sizeof(here->window_size), output_file);
            }
            here->window_beginning = ftello(output_file);
            if ( NULL == here->window &&
                 here->window_size != 0 ) {
                printToStderr( VERBOSITY_NORMAL, "Index incomplete! - index writing aborted.\n" );
                return 0;
            } else {
                if ( here->window_size > 0 )
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


/* Basic checks of existing index file:
   - Checks that last index point ->in isn't greater than gzip file size
*/
// INPUT:
// struct access *index         : pointer to index. Can be NULL => no check.
// unsigned char *file_name     : gzip file name. Can be NULL or "" => no check.
// unsigned char *index_filename: index file name. Must be != NULL, but can be "". Only used to print warning.
// OUTPUT:
// 0 on error, 1 on success
int check_index_file( struct access *index, unsigned char *file_name, unsigned char *index_filename ) {

    if ( NULL != file_name &&
         strlen( file_name ) > 0 ) {
        if ( NULL != index &&
             index->have > 0 ) {
            // size of input file
            struct stat st;
            stat( file_name, &st );
            printToStderr( VERBOSITY_EXCESSIVE, "(%ld >= %ld)\n", st.st_size, ( index->list[index->have - 1].in ) );
            if ( index->have > 1 &&
                st.st_size < ( index->list[index->have - 1].in )
                ) {
                printToStderr( VERBOSITY_NORMAL, "WARNING: Index file '%s' corresponds to a file bigger than '%s'\n",
                    index_filename, file_name );
                return 0;
            }
        }
    }

    return 1;

}


// Creates index for a gzip stream (file or STDIN);
// This function is not called from action_create_index() if an index file
// already exists and it is complete.
// If an incomplete index is passed, it will be completed from the last
// available index point so the whole gzip stream is not processed again.
// Original (zran.c) comments:
    /* Make one entire pass through the compressed stream and build an index, with
       access points about every span bytes of uncompressed output -- span is
       chosen to balance the speed of random access against the memory requirements
       of the list, about 32K bytes per access point.  Note that data after the end
       of the first zlib or gzip stream in the file is ignored.  build_index()
       returns the number of access points on success (>= 1), Z_MEM_ERROR for out
       of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
       file read error.  On success, *built points to the resulting index. */
// INPUT:
// FILE *in                 : input stream
// unsigned char *file_name : name of the input file associated with FILE *in.
//                            Can be "" (no file name: stdin used), but not NULL.
//                            Used only if there's no usable index && input (FILE *in)
//                            is associated with a file (not stdin) &&
//                            indx_n_extraction_opts == *_TAIL, for the use of the file
//                            size as approximation of the size of the tail to be output.
// off_t span               : span
// struct access **built: address of index pointer, equivalent to passed by reference.
//                        Note that index may be received with some (all) points already set
//                        from caller, if an index file was already available - and so this
//                        function must use it or create new points from the last available one,
//                        if needed.
// enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts:
//                      = JUST_CREATE_INDEX: usual behaviour
//                      = SUPERVISE_DO  : supervise a growing "in" gzip stream
//                      = SUPERVISE_DO_AND_EXTRACT_FROM_TAIL: like SUPERVISE_DO but
//                          this will also extract data to stdout, starting from
//                          the last available bytes (tail) on gzip when called.
//                      = EXTRACT_FROM_BYTE: extract from indicated offset, to stdout
// off_t offset         : if indx_n_extraction_opts == EXTRACT_FROM_BYTE, this is the offset byte in
//                        in the uncompressed stream from which to extract to stdout.
//                        0 otherwise.
// unsigned char *index_filename    : index will be read/written on-the-fly
//                                    to this index file name.
// int write_index_to_disk  : 1: will write/update the index as usual;
//                            0: will just read, but do not write nor update (overwrite) the index on disk,
//                           and also, do not create/update the index in memory (**built and returned_output.value):
//                           This is done because if index is not written to disk, windows would need to be
//                           maintained in memory, increasing the memory footprint unnecessarily as the index
//                           is not (actually) used later in the app. (Windows could be also emptied, but again,
//                           index points are not used later.)
// int end_on_first_proper_gzip_eof : end file processing on first proper (at feof()) GZIP EOF
//                                    (to be used when the file contains surely only one gzip stream)
// int always_create_a_complete_index : create a 'complete' index file even in case of decompressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time             : waiting time in seconds between reads when `-[ST]`
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of built index (index->have)
local struct returned_output build_index(
    FILE *in, unsigned char *file_name, off_t span, struct access **built,
    enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts, off_t offset,
    unsigned char *index_filename, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time )
{
    struct returned_output ret;
    off_t totin  = 0;           /* our own total counters to avoid 4GB limit */
    off_t totout = 0;           /* our own total counters to avoid 4GB limit */
    off_t last   = 0;           /* totout value of last access point */
    off_t offset_in = 0;        /* offset in compressed data to reach (opposed to "offset" in uncompressed data) */
    off_t avail_in_0;           /* because strm.avail_in may not exhausts every cycle! */
    off_t avail_out_0;          /* because strm.avail_out may not exhausts every cycle! */
    struct access *index = NULL;/* access points being generated */
    struct point *here = NULL;
    uint64_t actual_index_point = 0; // only set initially to >0 if NULL != *built
    uint64_t output_data_counter = 0;// counts uncompressed bytes extracted to stdout
    unsigned char *decompressed_window = NULL;
    z_stream strm;
    FILE *index_file = NULL;
    size_t index_last_written_point = 0;
    int extraction_from_offset_in = 0;           // 1: extract data using offset_in value
    int start_extraction_on_first_depletion = 0; // 0: extract - no depletion interaction.
                                                 // 1: start extraction on first depletion.
    int decompressing_with_gzip_headers = 0;     // 1 if inflateInit2(&strm, 47);
    int gzip_eof_detected = 0;     // 1 if a GZIP file format EOF is detected, and so, feof() is not trustworthy
    unsigned char input[CHUNK];    // TODO: convert to malloc
    unsigned char window[WINSIZE]; // TODO: convert to malloc
    unsigned window_size = 0;      // restarted on every reinitiation of strm data
    unsigned char window2[WINSIZE];// TODO: convert to malloc
    unsigned window2_size = 0;     // size of data stored in window2 buffer
    uint64_t index_points_0 = 0;   // marker to detect index updates (and to not write if index isn't updated)
    uint64_t index_file_size_0 = 0;// marker to detect index updates (and to not write if index isn't updated)

    ret.value = 0;
    ret.error = Z_OK;

    // Inform index filename
    if ( NULL != (*built) &&
        // if index->have == 0 index is superfluous
        (*built)->have > 0 &&
        (*built)->index_complete == 1 ) {
        // even though index is complete, processing will occur because gzip file may have changed!
        printToStderr( VERBOSITY_NORMAL, "Using index '%s'...\n", index_filename );
    } else {
        if ( write_index_to_disk == 1 )
            printToStderr( VERBOSITY_NORMAL, "Processing index to '%s'...\n", index_filename );
        else
            printToStderr( VERBOSITY_NORMAL, "Reading index '%s'...\n", index_filename );
    }

    /* open index_filename for binary reading & writing */
    if ( strlen(index_filename) > 0 &&
        ( NULL == index || index->index_complete == 0 )
        ) {
        printToStderr( VERBOSITY_EXCESSIVE, "write_index_to_disk = %d", write_index_to_disk );
        if ( write_index_to_disk == 1 ) {
            if ( access( index_filename, F_OK ) != -1 ) {
                // index_filename already exist:
                // "r+": Open a file for update (both for input and output). The file must exist.
                // r+, because the index may be incomplete, and so build_index() will
                // append new data and complete it (->have & ->size to correct values, not 0x0..0, 0xf..f).
                index_file = fopen( index_filename, "r+b" );
                printToStderr( VERBOSITY_EXCESSIVE, " (r+b)\n" );
            } else {
                // index_filename does not exist:
                index_file = fopen( index_filename, "w+b" );
                printToStderr( VERBOSITY_EXCESSIVE, " (w+b)\n" );
            }
        } else {
            // open index file in read-only mode (if file does not exist, NULL == index_file)
            index_file = fopen( index_filename, "rb" );
            printToStderr( VERBOSITY_EXCESSIVE, " (rb)\n" );
        }
    } else {
        // restrictions to not collide index output with data output to stdout
        // MUST have been made on caller.
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        index_file = stdout;
    }
    if ( NULL == index_file && write_index_to_disk == 1 ) {
        printToStderr( VERBOSITY_NORMAL, "Could not write index to file '%s'.\n", index_filename );
        goto build_index_error;
    }

    /* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */

    // if an index is already passed, use it:
    if ( NULL != (*built) &&
        (*built)->have > 0 ) {
        // NULL != *built (there is a previous index available: use it!)
        // if index->have == 0 index is superfluous

        index = *built;
        index_points_0 = index->have;   // marker to detect index updates (and to not write if index isn't updated)
        index_file_size_0 = index->file_size;// marker to detect index updates (and to not write if index isn't updated)

        /* initialize file and inflate state to start there */
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = 0;
        strm.next_in = Z_NULL;
        ret.error = inflateInit2(&strm, -15);         /* raw inflate */
        if (ret.error != Z_OK)
            return ret;

        index_last_written_point = index->have;

        //
        // Select an index point to start, depending on indx_n_extraction_opts
        //
        if ( indx_n_extraction_opts == SUPERVISE_DO ||
             indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
             indx_n_extraction_opts == JUST_CREATE_INDEX ||
             indx_n_extraction_opts == EXTRACT_TAIL
             ) {
            // move to last available index point, and continue from it
            actual_index_point = index->have - 1;
            // this index must be completed from last point: index->list[index->have-1]
            totin  = index->list[ actual_index_point ].in;
            totout = index->list[ actual_index_point ].out;
            last = totout;
            here = &(index->list[ actual_index_point ]);
        }

        if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ) {
            // move to the point needed for positioning on offset, or
            // move to last available point if offset can't be reached
            // with actually available index
            here = index->list;
            actual_index_point = 0;
            while (
                ++actual_index_point &&
                actual_index_point < index->have &&
                here[1].out <= offset
                )
                here++;
            actual_index_point--;
            totin  = index->list[ actual_index_point ].in;
            totout = index->list[ actual_index_point ].out;
            last = totout;
            // offset value comes from caller as parameter
        }

        if ( index->index_complete == 1 ) {
            if ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
                 indx_n_extraction_opts == EXTRACT_TAIL ) {
                offset = ( index->file_size - totout ) /4*3;
                indx_n_extraction_opts = EXTRACT_FROM_BYTE;
            }
        }

        assert( NULL != here );

        printToStderr( VERBOSITY_EXCESSIVE, "Starting from index point %d (@%ld->%ld).\n", actual_index_point+1, here->in, here->out );

        // fseek in data for correct position
        // using here index data:
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
            goto build_index_error;
        if (here->bits) {
            int i;
            i = getc(in);
            if (i == -1) {
                ret.error = ferror(in) ? Z_ERRNO : Z_DATA_ERROR;
                goto build_index_error;
            }
            (void)inflatePrime(&strm, here->bits, i >> (8 - here->bits));
        }

        // obtain window and initialize with it zlib's Dictionary
        if (here->window == NULL && here->window_beginning != 0) {
            /* index' window data is not on memory,
            but we have position and size on index file, so we load it now */
            FILE *index_file;
            if ( index->file_name == NULL ||
                strlen(index->file_name) == 0 ) {
                printToStderr( VERBOSITY_NORMAL, "Error while opening index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto build_index_error;
            }
            if (NULL == (index_file = fopen(index->file_name, "rb")) ||
                0 != fseeko(index_file, here->window_beginning, SEEK_SET)
                ) {
                printToStderr( VERBOSITY_NORMAL, "Error while opening index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto build_index_error;
            }
            // here->window_beginning = 0; // this is not needed
            if ( here->window_size > 0 &&
                ( NULL == (here->window = malloc(here->window_size)) ||
                 !fread(here->window, here->window_size, 1, index_file) )
                ) {
                printToStderr( VERBOSITY_NORMAL, "Error while reading index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto build_index_error;
            }
            fclose(index_file);
        }

        // Windows are ALWAYS stored compressed, so decompress it now:
        /* decompress() use uint64_t counters, but index->list->window_size is smaller */
        // local_window_size in order to avoid deleting the on-memory here->window_size,
        // that may be needed later if index must be increased and written to disk (fseeko).
        window_size = 0;
        if ( here->window_size > 0 ) {
            uint64_t local_window_size = here->window_size;
            /* window is compressed on memory, so decompress it */
            decompressed_window = decompress_chunk(here->window, &local_window_size);
            if ( NULL == decompressed_window ) {
                ret.error = Z_ERRNO;
                goto build_index_error;
            }
            (void)inflateSetDictionary(&strm, decompressed_window, local_window_size); // (local_window_size may not be WINSIZE)
            free( decompressed_window );
            decompressed_window = NULL;
            window_size = local_window_size;
        }

    } // end if ( NULL != *built && (*built)->have > 0 ) {


    // more decisions for extracting uncompressed data
    if ( ( NULL != (*built) && stdin == in ) ||
         NULL == (*built) ||
         ( NULL != (*built) && (*built)->index_complete == 0 )
         ) {
        // index available and stdin is used as gzip data input,
        // or no index is available,
        // or index exists but it is incomplete.

        if ( stdin == in ) {
            // stdin is used as input for gzip data

            if ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) {
                start_extraction_on_first_depletion = 1;
            }
            if ( indx_n_extraction_opts == EXTRACT_TAIL ) {
                start_extraction_on_first_depletion = 1;
            }

        } else {
            // there's a gzip filename

            if ( indx_n_extraction_opts == EXTRACT_TAIL ||
                 indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) {
                // set offset_in (equivalent to offset but for ->in values)
                // to the last CHUNK of gzip data ... this can be a good tail...
                struct stat st;
                if ( strlen( file_name ) > 0 ) {
                    stat(file_name, &st);
                    if ( st.st_size > 0 ) {
                        extraction_from_offset_in = 1;
                        if ( st.st_size <= CHUNK ) {
                            // gzip file is really small:
                            // change operation mode to extract from byte 0
                            offset_in = 0;
                        } else {
                            offset_in = st.st_size - CHUNK;
                        }
                        printToStderr( VERBOSITY_EXCESSIVE, "offset_in=%ld\n", offset_in );
                    } else {
                        start_extraction_on_first_depletion = 1;
                    }
                } else {
                    start_extraction_on_first_depletion = 1;
                }
            }

        } // end if ( stdin == in )

    } // end if ( ( NULL != *built && stdin == in ) ||
      //          NULL == *built ) ||
      //          ( NULL != (*built) && (*built)->index_complete == 0 ) )


    // decrement offset_in and offset by actual position:
    if ( offset_in > 0 &&
        NULL != here ) {
        if ( here->in > offset_in )
            offset_in = 0;
        else
            offset_in -= here->in;
    }
    if ( offset > 0 &&
        NULL != here ) {
        if ( here->out > offset )
            offset = 0;
        else
            offset -= here->out;
    }


    // default zlib initialization
    // when no index entry points have been found:
    if ( NULL == (*built) ||
        NULL == here ) {
        // NULL != *built (there is no previous index available: build it from scratch)

        /* initialize inflate */
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = 0;
        strm.next_in = Z_NULL;
        ret.error = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding (15 + automatic header detection) */
        decompressing_with_gzip_headers = 1;
        printToStderr( VERBOSITY_MANIAC, "ret.error = %d\n", ret.error );
        if (ret.error != Z_OK)
            return ret;
        totin = totout = last = 0;
        index = NULL;               /* will be allocated by first addpoint() */
    }

    // initialize output window
    strm.next_out = window;
    strm.avail_out = WINSIZE;
    avail_out_0 = strm.avail_out;
    do {
        /* get some compressed data from input file */

        // bgzip-compatible-streams code:
        if ( ret.error == Z_STREAM_END &&
             strm.avail_in > 0 ) { // if strm.avail_in is casually 0, this block of code isn't needed
            // readjust input data moving it to the beginning of buffer
            // (strm.next_in floats from input to input+CHUNK)
            memmove( input, strm.next_in, strm.avail_in ); // memmove() is overlapping-safe
        }
        // .................................................
        // note that here strm.avail_in > 0 only if ret.error == Z_STREAM_END
        int strm_avail_in0 = strm.avail_in;
        if ( !feof( in )) { // on last block, strm.avail_in > 0 is possible with feof(in)==1 already!
            if ( ( indx_n_extraction_opts != SUPERVISE_DO_AND_EXTRACT_FROM_TAIL &&
                   indx_n_extraction_opts != SUPERVISE_DO ) ||
                waiting_time > 0 )
                printToStderr( VERBOSITY_MANIAC, "[reading %d B]", CHUNK - strm_avail_in0 );
            strm.avail_in = fread(input + strm_avail_in0, 1, CHUNK - strm_avail_in0, in);
            strm.avail_in += strm_avail_in0;
        }
        // .................................................
        // decompressor state MUST be reinitiated after Z_STREAM_END
        if ( ret.error == Z_STREAM_END ) {
            printToStderr( VERBOSITY_MANIAC, "Reinitializing zlib strm data...\n" );
            strm_avail_in0 = strm.avail_in;
            (void)inflateEnd(&strm);
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;
            ret.error = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding (15 + automatic header detection) */
            decompressing_with_gzip_headers = 1;
            if (ret.error != Z_OK)
                return ret;
            strm.avail_in = strm_avail_in0;
            // it is compulsory to reinitiate also output data:
            strm.avail_out = WINSIZE;
            strm.next_out = window;
            avail_out_0 = strm.avail_out;
            window_size = 0;
        }
        // end of bgzip-compatible-streams code

        avail_in_0 = strm.avail_in;

        printToStderr( VERBOSITY_CRAZY, "output_data_counter=%ld,totin=%ld,totout=%ld,ftello=%ld,avail_in=%d\n",
            output_data_counter, totin, totout, ftello(in), strm.avail_in );

        if ( (indx_n_extraction_opts == SUPERVISE_DO ||
              indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
              indx_n_extraction_opts == EXTRACT_TAIL ) &&
             strm.avail_in == 0 ) {

            // check conditions to start output of uncompressed data
            printToStderr( VERBOSITY_CRAZY, ">>> %d, %d, %d",
                extraction_from_offset_in, start_extraction_on_first_depletion, indx_n_extraction_opts );
            if ( start_extraction_on_first_depletion == 1 ) {
                start_extraction_on_first_depletion = 0;

                // output uncompressed data
                unsigned have = WINSIZE - strm.avail_out;
                if ( have == 0 ) {
                    if ( window2_size > 0 ) {
                        // if we have previous data to show, show it, because now we're out of fresh data!
                        output_data_counter += window2_size;
                        printToStderr( VERBOSITY_MANIAC, "[..>%d]", window2_size );
                        if (fwrite(window2, 1, window2_size, stdout) != window2_size || ferror(stdout)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            goto build_index_error;
                        }
                    }
                } else {
                    output_data_counter += have;
                    printToStderr( VERBOSITY_MANIAC, "[.>%d]", have );
                    if (fwrite(strm.next_out, 1, have, stdout) != have || ferror(stdout)) {
                        (void)inflateEnd(&strm);
                        ret.error = Z_ERRNO;
                        goto build_index_error;
                    }
                }
                fflush(stdout);

                // continue extracting data as usual,
                offset = 0;
                offset_in = 0;
                // though as indx_n_extraction_opts != EXTRACT_FROM_BYTE it'll
                // patiently waits if data exhausts.

            }

            if ( indx_n_extraction_opts == EXTRACT_TAIL ) {
                // the process ends here as all required data has been output
                // (index remains incomplete)
                ret.error = Z_OK;
                if ( NULL != index ) {
                    ret.value = index->have;
                }
                goto build_index_error;
            }

            // sleep and retry
            sleep( waiting_time );
            clearerr( in );
            continue;

        }

        if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
             indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
             indx_n_extraction_opts == EXTRACT_TAIL ) {
            // with not Supervising options, strm.avail_in == 8 + eof is equivalent to Correct END OF GZIP at EOF
            if ( feof( in ) && strm.avail_in == 8 ) {
                printToStderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected at EOF-8.\n" );
                gzip_eof_detected = 0; // to exit loop, as "gzip_eof_detected == 1" is one ORed condition
                                       // and now this variable is not needed anymore.
                break;
            }
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

        /* process all of strm.next_in (size strm.avail_in), or until end of stream */
        do {
            /* reset sliding window if necessary */
            if (strm.avail_out == 0) {
                strm.avail_out = WINSIZE;
                strm.next_out = window;
                avail_out_0 = strm.avail_out;
            }

            /* inflate until out of input, output, or at end of block --
               update the total input and output counters */
            totin += strm.avail_in;
            totout += strm.avail_out;
            window_size += strm.avail_out; // update window_size available size
            ret.error = inflate(&strm, Z_BLOCK);      /* return at end of block */
            totin -= strm.avail_in;
            totout -= strm.avail_out;
            { // update window_size available size
                window_size -= strm.avail_out;
                if (window_size > WINSIZE)
                    window_size = WINSIZE;
            }

            // maintain a backup window for the case of sudden Z_STREAM_END
            // and indx_n_extraction_opts == *_TAIL
            if ( output_data_counter == 0 &&
                 ( NULL == index || index->index_complete == 0 ) &&
                 ( indx_n_extraction_opts == EXTRACT_TAIL ||
                   indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) ) {
                if ( WINSIZE - strm.avail_out > 0 ) { // if have == 0, maintain previous (data and) window2_size value
                    window2_size = WINSIZE - avail_out_0;
                    printToStderr( VERBOSITY_NUTS, "(w2s=%d)", window2_size );
                    if ( window2_size <= WINSIZE )
                        memcpy( window2, window, window2_size );
                }
                // TODO: change to pointer flip instead of memcpy (possible?)
            }

            // .................................................
            // treat possible gzip tail:
            if ( ret.error == Z_STREAM_END &&
                 strm.avail_in >= 8 &&
                 decompressing_with_gzip_headers == 0 ) {
                // discard this data as it is probably the 8 gzip-tail bytes (4 CRC + 4 size%2^32)
                // and zlib is not able to consume it if inflateInit2(&strm, -15) (raw decompressing)
                strm.avail_in -= 8;
                strm.next_in += 8;
                totin += 8;         // data is discarded from strm input, but it MUST be counted in total input
                if ( offset_in > 0 )
                    offset_in -= 8; // data is discarded from strm input, but it MUST be counted in input offset
                printToStderr( VERBOSITY_EXCESSIVE,
                    "END OF GZIP passed @%ld while in raw mode (totout=%ld, avail_in=%d, ftello=%ld)\n",
                    totin, totout, strm.avail_in, ftello(in) );
                if ( end_on_first_proper_gzip_eof == 1 ||
                    ( ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                    indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                    indx_n_extraction_opts == EXTRACT_TAIL ) && feof( in ) ) )
                    gzip_eof_detected = 0;
                else
                    gzip_eof_detected = 1;
                break;
                // avail_in_0 doesn't need to be decremented as it tries to count raw stream input bytes
            }
            // end of treat possible gzip tail
            // .................................................
            if ( ret.error != Z_OK ) {
                if ( ret.error != Z_STREAM_END )
                    printToStderr( VERBOSITY_EXCESSIVE, "ERR %d: totin=%ld, totout=%ld, ftello=%ld\n", ret.error, totin, totout, ftello(in) );
                else
                    printToStderr( VERBOSITY_MANIAC, "ERR %d: totin=%ld, totout=%ld, ftello=%ld\n", ret.error, totin, totout, ftello(in) );
            }

            if (ret.error == Z_NEED_DICT)
                ret.error = Z_DATA_ERROR;
            if (ret.error == Z_MEM_ERROR || ret.error == Z_DATA_ERROR) {
                goto build_index_error;
            }
            if (ret.error == Z_STREAM_END) {
                if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                     indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                     indx_n_extraction_opts == EXTRACT_TAIL ) {
                    // with not Supervising options, a Z_STREAM_END at feof() is correct!
                    if ( feof( in ) && strm.avail_in == 0 ) {
                        printToStderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected at EOF.\n" );
                        gzip_eof_detected = 0; // to exit loop, as "gzip_eof_detected == 1" is one ORed condition
                                               // and now this variable is not needed anymore.
                        break;
                    }
                }
                if ( end_on_first_proper_gzip_eof == 0 )
                    gzip_eof_detected = 1;
                if ( gzip_eof_detected == 0 ) {
                    if ( end_on_first_proper_gzip_eof == 1 &&
                         feof( in ) ) {
                        gzip_eof_detected = 0;
                    } else {
                        gzip_eof_detected = 1;
                        printToStderr( VERBOSITY_NORMAL, "Warning: GZIP end detected in the middle of data: deactivating `-E`\n" );
                        end_on_first_proper_gzip_eof = 0;
                    }
                }
                if ( offset_in > 0 )
                    offset_in -= 8; // data is discarded from strm input, but it MUST be counted in input offset
                break;
            }

            //
            // if required by passed indx_n_extraction_opts option, extract to stdout:
            //
            // EXTRACT_FROM_BYTE: extract all:
            if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ) {
                unsigned have = avail_out_0 - strm.avail_out;
                printToStderr( VERBOSITY_NUTS, "[>1>%ld,%d,%d,%d]", offset, have, strm.avail_out, strm.avail_in );
                if ( offset > have ) {
                    offset -= have;
                } else {
                    if ( ( offset > 0 && offset <= have ) ||
                        offset == 0 ) {
                        // print offset - have bytes
                        // If offset==0 (from offset byte on) this prints always all bytes:
                        output_data_counter += have - offset;
                        printToStderr( VERBOSITY_CRAZY, "[>1>%d]", have - offset );
                        if (fwrite(window + offset + (WINSIZE - avail_out_0), 1, have - offset, stdout) != (have - offset) ||
                            ferror(stdout)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            goto build_index_error;
                        }
                        offset = 0;
                        fflush(stdout);
                    }
                }
                avail_out_0 = strm.avail_out;
            } else {
                // extraction_from_offset_in marks the use of "offset_in"
                if ( extraction_from_offset_in == 1 ) {
                    unsigned have = avail_out_0 - strm.avail_out;
                    unsigned have_in = avail_in_0 - strm.avail_in;
                    avail_in_0 = strm.avail_in;
                    printToStderr( VERBOSITY_NUTS, "[>2>%ld,%d,%d]", offset_in, have_in, strm.avail_in );
                    if ( offset_in > 0 )
                        offset_in -= have_in;
                    if ( ( offset_in > 0 && offset_in <= have_in ) ||
                        offset_in <= 0 ) {
                        offset_in = 0;
                        // print all "have" bytes as with offset_in it is not possible
                        // to know how much output discard (uncompressed != compressed)
                        output_data_counter += have;
                        printToStderr( VERBOSITY_CRAZY, "[>2>%d]", have );
                        if (fwrite(window + (WINSIZE - avail_out_0), 1, have, stdout) != have ||
                            ferror(stdout)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            goto build_index_error;
                        }
                        fflush(stdout);
                        // continue extracting data as usual
                        offset = 0;
                        // though indx_n_extraction_opts != EXTRACT_FROM_BYTE
                    }
                }
                avail_out_0 = strm.avail_out;
            }

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

                // check actual_index_point to see if we've passed
                // the end of the passed previous index, and so
                // we must addpoint() from now on :
                printToStderr( VERBOSITY_MANIAC, "actual_index_point = %ld,", actual_index_point+1 );
                printToStderr( VERBOSITY_MANIAC, "(%ld, %ld)\n", totout, last );
                if ( actual_index_point > 0 )
                    ++actual_index_point;
                if ( NULL != index &&
                    actual_index_point > (index->have - 1) ) {
                    actual_index_point = 0; // 0: actual_index_point won't be incremented or checked anymore
                }
                if ( actual_index_point == 0 &&
                    // addpoint() only if index doesn't yet exist or it is incomplete
                    ( NULL == index || index->index_complete == 0 ) &&
                    // do not add points if position is lower than the last index point available:
                    // (this can happen if -s is less now, than when the index was created!)
                    ( NULL == index || ( index->have > 0 &&
                        index->list[index->have -1].in < totin &&
                       (index->list[index->have -1].out + span) <= totout ) )
                    ) {

                    if ( write_index_to_disk == 1 ) { // if `-W`, index is not written to disk, and it will also not be created/updated (!)

                        if ( NULL != index )
                            printToStderr( VERBOSITY_EXCESSIVE, "addpoint index->have = %ld, index_last_written_point = %ld\n",
                                index->have, index_last_written_point );

                        index = addpoint(index, strm.data_type & 7, totin,
                                         totout, strm.avail_out, window, window_size, 1);
                        if (index == NULL) {
                            ret.error = Z_MEM_ERROR;
                            goto build_index_error;
                        }

                        // write added point!
                        // note that points written are automatically emptied of its window values
                        // in order to use as less memory a s possible
                        if ( ! serialize_index_to_file( index_file, index, index_last_written_point ) )
                            goto build_index_error;

                    }

                    if ( NULL != index )
                        index_last_written_point = index->have;

                }

                last = totout;

            }

        } while ( strm.avail_in != 0 );

    } while ( ret.error != Z_STREAM_END || strm.avail_in > 0 || gzip_eof_detected == 1 );


    // last opportunity to output tail data
    // before deleting strm object
    if ( output_data_counter == 0 &&
        ( indx_n_extraction_opts == EXTRACT_TAIL ||
          indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) ) {

        unsigned have = WINSIZE - strm.avail_out;

        printToStderr( VERBOSITY_EXCESSIVE, "last extraction: %d\n", have );

        if ( have > 0 ) {
            printToStderr( VERBOSITY_EXCESSIVE, "[.L>%d]", have );
            if (fwrite(strm.next_out, 1, have, stdout) != have || ferror(stdout)) {
                ret.error = Z_ERRNO;
            }
            output_data_counter += have;
        } else {
            // use backup window
            printToStderr( VERBOSITY_EXCESSIVE, "[..L>%d]", window2_size );
            if (fwrite(window2, 1, window2_size, stdout) != window2_size || ferror(stdout)) {
                ret.error = Z_ERRNO;
            }
            output_data_counter += window2_size;
        }

        fflush(stdout);

    }

    /* clean up */
    (void)inflateEnd(&strm);
    /* and return index (release unused entries in list) */
    if ( NULL != index ) {
        struct point *next = realloc(index->list, sizeof(struct point) * index->have);
        if (next == NULL) {
            ret.error = Z_MEM_ERROR;
            ret.value = 0;
            goto build_index_error;
        }
        index->list = next;
        index->size = index->have;
        index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */
    }

    // once all index values are filled, close index file: a last call must be done
    // with index_last_written_point = index->have
    if ( NULL != index &&
         index->index_complete == 0 &&
         write_index_to_disk == 1 ) {
        // use markers to detect index updates and to not write if index isn't updated
        if ( index_points_0 != index->have ||
             index_file_size_0 != index->file_size ) {
            if ( ! serialize_index_to_file( index_file, index, index->have ) )
                goto build_index_error;
        }
    }

    if ( NULL != index_file )
        fclose(index_file);

    if ( NULL != index &&
         index->index_complete == 0 &&
         write_index_to_disk == 1 ) {
        if ( strlen(index_filename) > 0 )
            printToStderr( VERBOSITY_NORMAL, "Index written to '%s'.\n", index_filename );
        else
            printToStderr( VERBOSITY_NORMAL, "Index written to stdout.\n" );
    }

    if ( NULL != index )
        index->index_complete = 1; /* index is now complete */

    // print output_data_counter info
    if ( output_data_counter > 0 )
        printToStderr( VERBOSITY_NORMAL, "%ld bytes of data extracted.\n", output_data_counter );

    *built = index;
    if ( NULL != index )
        ret.value = index->have;
    else
        ret.value = 0;
    return ret;

    /* return error */
  build_index_error:
    // print output_data_counter info
    if ( output_data_counter > 0 )
        printToStderr( VERBOSITY_NORMAL, "%ld bytes of data extracted.\n", output_data_counter );
    (void)inflateEnd(&strm);
    if ( always_create_a_complete_index == 1 ) {
        index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */
        // return index pointer and write index to index file, ignoring the decompression error
        *built = index;
        if ( ! serialize_index_to_file( index_file, index, index->have ) )
            printToStderr( VERBOSITY_NORMAL, "ERROR whilst writing index file '%s'.\n", index_file );
    } else {
        if (index != NULL)
            free_index(index);
        *built = NULL;
    }
    if (index_file != NULL)
        fclose(index_file);
    return ret;

    // there's no need to free(here),
    // because it pointed to index's values, and they
    // will be freed with free_index().
}


// read index from a file
// INPUT:
// FILE *input_file         : input stream
// int load_windows         : 0 do not yet load windows on memory; 1 to load them
// unsigned char *file_name : file path to index file, needed in case
//                            load_windows==0, so a later extract() can access the
//                            index file again to read the window data.
//                            Can be "" if using stdin, but not NULL.
// OUTPUT:
// struct access *index : pointer to index, or NULL on error
struct access *deserialize_index_from_file( FILE *input_file, int load_windows, unsigned char *file_name ) {
    struct point here;
    struct access *index = NULL;
    uint32_t index_complete = 1;
    uint64_t number_of_index_points = 0;
    uint64_t index_have, index_size, file_size;
    uint64_t position_at_file = 0;
    char header[GZIP_INDEX_HEADER_SIZE];
    struct stat st;

    // get index file size to calculate on-the-fly how many registers are
    // in order to being able to read still-growing index files (see `-S`)
    if ( strlen(file_name) > 0 ) {
        stat(file_name, &st);
        file_size = st.st_size;
    } else {
        // for stdin use max value
        file_size = UINT64_MAX;
    }

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

    // check index size == 0
    if ( file_size != 0 ) {
        if (fread(header, 1, GZIP_INDEX_HEADER_SIZE, input_file) < GZIP_INDEX_HEADER_SIZE ||
            *((uint64_t *)header) != 0 ||
            strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING, GZIP_INDEX_HEADER_SIZE/2) != 0) {
            printToStderr( VERBOSITY_NORMAL, "File is not a valid gzip index file.\n" );
            return NULL;
        }
    } else {
        // for an empty index, return a pointer with zero data
        index = create_empty_index();
        return index;
    }

    index = create_empty_index();

    fread_endian(&(index_have), sizeof(index_have), input_file);
    fread_endian(&(index_size), sizeof(index_size), input_file);

    printToStderr( VERBOSITY_MANIAC, "Number of index points declared: %ld - %ld\n", index_have, index_size );

    number_of_index_points = index_have;

    // index->size equals index->have when the index file is correctly closed,
    // and index->have on disk == 0 && index->have on disk = index->size whilst the index is growing:
    if ( index_have != index_size ) {
        printToStderr( VERBOSITY_NORMAL, "Index file is incomplete.\n" );
        index_complete = 0;
        number_of_index_points = index_size;
    }

    if ( verbosity_level == VERBOSITY_EXCESSIVE )
        printToStderr( VERBOSITY_EXCESSIVE, "Number of index points declared: %ld\n", number_of_index_points );

    position_at_file = GZIP_INDEX_HEADER_SIZE + sizeof(index_have)*2;

    // read the list of points
    do {

        fread_endian(&(here.out),  sizeof(here.out),  input_file);
        fread_endian(&(here.in),   sizeof(here.in),   input_file);
        fread_endian(&(here.bits), sizeof(here.bits), input_file);
        fread_endian(&(here.window_size), sizeof(here.window_size), input_file);
        position_at_file += sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) + sizeof(here.window_size);
        printToStderr( VERBOSITY_MANIAC, "READ window_size = %d\n", here.window_size );

        if ( here.bits > 8 ||
             here.window_size < 0 )
        {
            printToStderr( VERBOSITY_MANIAC, "\t(%p, %d, %ld, %ld, %d, %ld)\n", index, here.bits, here.in, here.out, here.window_size, file_size );
            printToStderr( VERBOSITY_EXCESSIVE, "Unexpected data found in index file '%s' @%ld.\nIgnoring data subsequent to point %ld.\n",
                    file_name, position_at_file, index_size - number_of_index_points + 1 );
            break; // exit do loop
        }

        if ( load_windows == 0 ) {
            // do not load window on here.window, but leave it on disk: this is marked with
            // a here.window_beginning to the position of the window on disk
            here.window = NULL;
            here.window_beginning = position_at_file;
            // and position into file as if read had occur
            if ( stdin == input_file ) {
                // read input until here.window_size
                uint64_t pos = 0;
                uint64_t position = here.window_size;
                unsigned char *input = malloc(CHUNK);
                if ( NULL == input ) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from stdin.\n" );
                    goto deserialize_index_from_file_error;
                }
                while ( pos < position ) {
                    if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), input_file) ) {
                        printToStderr( VERBOSITY_NORMAL, "Could not read index from stdin.\n" );
                        goto deserialize_index_from_file_error;
                    }
                    pos += CHUNK;
                }
                free(input);
            } else {
                if ( here.window_size > 0 )
                    fseeko(input_file, here.window_size, SEEK_CUR);
            }
        } else {
            // load compressed window on memory:
            if ( here.window_size > 0 ) {
                here.window = malloc(here.window_size);
                // load window on here.window: this is marked with
                // a here.window_beginning = 0 (which is impossible with gzipindx format)
                here.window_beginning = 0;
                if (here.window == NULL) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    goto deserialize_index_from_file_error;
                }
                if ( !fread(here.window, here.window_size, 1, input_file) ) {
                    printToStderr( VERBOSITY_NORMAL, "Error while reading index file.\n" );
                    goto deserialize_index_from_file_error;
                }
            } else {
                here.window = NULL;
            }
        }
        position_at_file += here.window_size;
        printToStderr( VERBOSITY_MANIAC, "(%p, %d, %ld, %ld, %d, %ld), ", index, here.bits, here.in, here.out, here.window_size, here.window_beginning);
        // increase index structure with a new point
        // (here.window can be NULL if load_windows==0)
        index = addpoint( index, here.bits, here.in, here.out, 0, here.window, here.window_size, 0 );

        // after increasing index, copy values which were not passed to addpoint():
        index->list[index->have - 1].window_beginning = here.window_beginning;
        // note that even if (here.window != NULL) it MUST NOT be free() here, because
        // the pointer has been copied in a point of the index structure.

    } while (
        ( file_size - position_at_file ) >
        // at least an empty window must enter, otherwise end loop:
        ( sizeof(here.out)+sizeof(here.in)+sizeof(here.bits)+
          sizeof(here.window_beginning)+sizeof(index->file_size) )
        &&
        --number_of_index_points > 0
        );


    index->file_size = 0;

    if ( index_complete == 1 ){
        /* read size of uncompressed file (useful for bgzip files) */
        /* this field may not exist (maybe useful for growing gzip files?) */
        fread_endian(&(index->file_size), sizeof(index->file_size), input_file);
    }

    index->file_name = malloc( strlen(file_name) + 1 );
    if ( NULL == memcpy( index->file_name, file_name, strlen(file_name) + 1 ) ) {
        printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
        goto deserialize_index_from_file_error;
    }

    if ( index_complete == 1 )
        index->index_complete = 1; /* index is now complete */
    else
        index->index_complete = 0;

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


// Creates an index for a gzip file.
// If index file already exists, it is completed if it weren't complete,
// or directly used if it is complete.
// INPUT:
// unsigned char *file_name     : file name of gzip file for which index will be calculated,
//                                If strlen(file_name) == 0 stdin is used as gzip file.
// struct access **index        : memory address of index pointer (passed by reference)
// unsigned char *index_filename: file name where index will be written
//                                If strlen(index_filename) == 0 stdout is used as output for index.
// enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts:
//                                  value passed to build_index();
//                                  in case of SUPERVISE_DO* (not *DONT), wait here until gzip file exists.
// off_t offset                 : if supervise == EXTRACT_FROM_BYTE, this is the offset byte in
//                                the uncompressed stream from which to extract to stdout.
//                                0 otherwise.
// off_t span_between_points    : span between index points in bytes
// int write_index_to_disk      : 1: will write/update the index as usual;
//                                0: will just read, but do not write nor update (overwrite) it
// int end_on_first_proper_gzip_eof: end file processing on first proper (at feof()) GZIP EOF
//                                   (to be used when the file contains surely only one gzip stream)
// int always_create_a_complete_index : create a 'complete' index file even in case of decompressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time             : waiting time in seconds between reads when `-[ST]`
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_create_index(
    unsigned char *file_name, struct access **index,
    unsigned char *index_filename, enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts,
    off_t offset, off_t span_between_points, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time )
{

    FILE *in;
    struct returned_output ret;
    uint64_t number_of_index_points = 0;
    int waiting = 0;

    // First of all, check that data output and index output do not collide:
    if ( strlen(file_name) == 0 &&
         strlen(index_filename) == 0 &&
         ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
           indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
           indx_n_extraction_opts == EXTRACT_TAIL )
        ) {
        // input is stdin, output is stdout, and no file name has been
        // indicated for index output, so action is not possible:
        printToStderr( VERBOSITY_NORMAL, "ERROR: Please, note that extracted data will be output to STDOUT\n" );
        printToStderr( VERBOSITY_NORMAL, "       so an index file name is needed (`-I`).\nAborted.\n" );
        return EXIT_GENERIC_ERROR;
    }

    // open <FILE>:
    if ( strlen(file_name) > 0 ) {
wait_for_file_creation:
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            if ( indx_n_extraction_opts == SUPERVISE_DO ||
                 indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) {
                if ( waiting == 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "Waiting for creation of file '%s'\n", file_name );
                    waiting++;
                }
                sleep( waiting_time );
                goto wait_for_file_creation;
            }
            printToStderr( VERBOSITY_NORMAL, "Could not open '%s' for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
        printToStderr( VERBOSITY_NORMAL, "Processing '%s' ...\n", file_name );
    } else {
        // stdin
        SET_BINARY_MODE(STDIN); // sets binary mode for stdin in Windows
        in = stdin;
        printToStderr( VERBOSITY_NORMAL, "Processing stdin ...\n" );
    }

    // compute index:
    // but if index_filename already exist, load it and use
    // (if it is complete, it'll be used directly, if not it'll be
    // completed from last point - all in build_index() ).
    if ( strlen( index_filename ) > 0 &&
         access( index_filename, F_OK ) != -1 ) {
        // index_filename already exist: try to load it
        FILE *index_file;
        index_file = fopen( index_filename, "rb" );
        if ( NULL != index_file ) {
            *index = deserialize_index_from_file( index_file, 0, index_filename );
            fclose( index_file );
            if ( NULL == *index ) {
                printToStderr( VERBOSITY_NORMAL, "Could not load index from file '%s'.\nAborted.\n", index_filename );
                return EXIT_GENERIC_ERROR;
            }
            // index ok, continue
            number_of_index_points = (*index)->have;
            (*index)->index_complete = 0; // every index can be updated with new points in case gzip data grows!
        } else {
            printToStderr( VERBOSITY_NORMAL, "Could not open '%s' for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
    }

    // checks on index read from file:
    if ( NULL != (*index) &&
         strlen( file_name ) > 0 ) {
        // (here, index_filename exists and index exists)
        check_index_file( (*index), file_name, index_filename );
        // return value is not used - only warn user and continue
    }

    // stdout to binary mode if needed
    if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
         indx_n_extraction_opts == EXTRACT_TAIL ||
         indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL
         ) {
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
    }

    ret = build_index( in, file_name, span_between_points, index,
            indx_n_extraction_opts, offset, index_filename, write_index_to_disk,
            end_on_first_proper_gzip_eof, always_create_a_complete_index,
            waiting_time );
    fclose(in);

    if ( ret.error < 0 ) {
        switch ( ret.error ) {
            case Z_MEM_ERROR:
                printToStderr( VERBOSITY_NORMAL, "ERROR: Out of memory.\n" );
                break;
            case Z_DATA_ERROR:
                if ( strlen(file_name) > 0 )
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Compressed data error in '%s'.\n", file_name );
                else
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Compressed data error in stdin.\n" );
                break;
            case Z_ERRNO:
                if ( strlen(file_name) > 0 )
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Read error on '%s'.\n", file_name );
                else
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Read error on stdin.\n" );
                break;
            default:
               printToStderr( VERBOSITY_NORMAL, "ERROR: Error %d while applying action.\n", ret.error );
       }
       return EXIT_GENERIC_ERROR;
    }

    if ( NULL != index && NULL != *index &&
         number_of_index_points != (*index)->have && write_index_to_disk == 1 )
        if ( number_of_index_points > 0 ) {
            printToStderr( VERBOSITY_NORMAL, "Updated index with %ld new access points.\n", ret.value - number_of_index_points);
            printToStderr( VERBOSITY_NORMAL, "Now index have %ld access points.\n", ret.value);
        } else
            printToStderr( VERBOSITY_NORMAL, "Built index with %ld access points.\n", ret.value);

    return EXIT_OK;

}


// list info for an index file, to stdout
// May be called with global verbosity_level == VERBOSITY_NONE in which case
// no info is printed to stdout but a value is returned.
// INPUT:
// unsigned char *file_name           : index file name
// unsigned char *input_gzip_filename : gzip file associated with the index. May be NULL, but not "".
// enum VERBOSITY_LEVEL list_verbosity: level of detail of index checking:
//                                      minimum is 1 == VERBOSITY_NORMAL
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_list_info( unsigned char *file_name, unsigned char *input_gzip_filename, enum VERBOSITY_LEVEL list_verbosity ) {

    FILE *in = NULL;
    struct access *index = NULL;
    uint64_t j;
    int ret_value = EXIT_OK;
    struct stat st;
    unsigned char *gzip_filename = NULL;
    struct stat st_gzip;
    st_gzip.st_size = 0;
    uint64_t comp_win_counter = 0;   // just to count bytes and calculate a file estimated "hardness"
    uint64_t uncomp_win_counter = 0; // just to count bytes and calculate a file estimated "hardness"

    // check input_gzip_filename
    if ( NULL != input_gzip_filename ) {
        gzip_filename = malloc( strlen(input_gzip_filename) + 1 );
        if ( NULL == gzip_filename )
            return EXIT_GENERIC_ERROR;
        sprintf( gzip_filename, "%s", input_gzip_filename );
    }

    // open index file:
    if ( strlen( file_name ) > 0 ) {
        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "Checking index file '%s' ...\n", file_name );
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            printToStderr( VERBOSITY_NORMAL, "Could not open %s for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
    } else {
        // stdin
        printToStderr( VERBOSITY_NORMAL, "Checking index from stdin ...\n" );
        SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
        in = stdin;
    }

    // in case in == stdin, file_name == ""
    // but this doesn't matter as windows won't be inflated from disk, but from memory with 2nd parameter == 1.
    index = deserialize_index_from_file( in, ((list_verbosity==VERBOSITY_MANIAC)?1:0), file_name );

    if ( NULL != index &&
         strlen( file_name ) > 0 ) {
        stat( file_name, &st );
        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\tSize of index file:        %ld Bytes", st.st_size );

        if ( st.st_size > 0 &&
             list_verbosity > VERBOSITY_NONE ) {
            // try to obtain the name of the original gzip data file
            // to calculate the ratio index_file_size / gzip_file_size

            if ( NULL == gzip_filename ) {
                gzip_filename = malloc( strlen(file_name) + 1 );
                if ( NULL != gzip_filename ) {
                    sprintf( gzip_filename, "%s", file_name );
                    if ( (unsigned char *)strstr(gzip_filename, ".gzi") ==
                            (unsigned char *)(gzip_filename + strlen(file_name) - 4) ) {
                        // if index file name is 'FILE.gzi', gzip-file name should be 'FILE.gz'
                        gzip_filename[strlen(gzip_filename)-1]='\0';
                        // let's see if we're certain:
                        if ( access( gzip_filename, F_OK ) != -1 ) {
                            // we've found the gzip-file name !
                            ;
                        } else {
                            // we haven't found it: may be the name is of the format "FILE.another_extension.gzi"
                            gzip_filename[strlen(gzip_filename)-3]='\0';
                            if ( access( gzip_filename, F_OK ) != -1 ) {
                                // we've found the gzip-file name !
                                ;
                            } else {
                                // we must give up guessing names here
                                free( gzip_filename );
                                gzip_filename = NULL;
                            }
                        }
                    } else {
                        // if ".gzi" doesn't fit as extension, we're up guessing!
                        free( gzip_filename );
                        gzip_filename = NULL;
                    }
                }
            }

            if ( NULL != gzip_filename ) {
                if( access( gzip_filename, F_OK ) != -1 ) {
                    check_index_file( index, gzip_filename, file_name );
                    stat( gzip_filename, &st_gzip );
                    if ( st_gzip.st_size > 0 &&
                         verbosity_level > VERBOSITY_NONE) {
                        fprintf( stdout, " (%.2f%%/gzip)\n", (double)st.st_size / (double)st_gzip.st_size * 100.0 );
                        fprintf( stdout, "\tGuessed gzip file name:    '%s'", gzip_filename );
                        if ( index->file_size > 0 )
                            fprintf( stdout, " (%.2f%%)", 100.0 - (double)st_gzip.st_size / (double)index->file_size * 100.0 );
                        fprintf( stdout, " ( %ld Bytes )", st_gzip.st_size );
                    }
                } else {
                    if ( verbosity_level > VERBOSITY_NONE )
                        fprintf( stdout, "\n\tIndicated gzip file '%s' doesn't exist", gzip_filename );
                }
            }

        }

        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\n" );

    }

    if ( ! index ) {

        printToStderr( VERBOSITY_NORMAL, "Could not read index from file '%s'.\n", file_name);
        ret_value = EXIT_GENERIC_ERROR;
        goto action_list_info_error;

    } else {

        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\tNumber of index points:    %ld\n", index->have );
        if (index->file_size != 0) {
            if ( verbosity_level > VERBOSITY_NONE )
                fprintf( stdout, "\tSize of uncompressed file: %ld Bytes\n", index->file_size );
        }
        if ( list_verbosity > VERBOSITY_NORMAL ) {
            if ( verbosity_level > VERBOSITY_NONE ) {
                if ( list_verbosity < VERBOSITY_MANIAC )
                    fprintf( stdout, "\tList of points:\n\t   @ compressed/uncompressed byte (window data size in Bytes @window's beginning at index file), ...\n\t" );
                else
                    fprintf( stdout, "\tList of points:\n\t   @ compressed/uncompressed byte (compressed window size / uncompressed window size), ...\n\t" );
                if ( list_verbosity == VERBOSITY_MANIAC )
                    fprintf( stdout, "Checking compressed windows...\n\t" );
            }
            for ( j=0; j<index->have; j++ ) {
                if ( list_verbosity == VERBOSITY_EXCESSIVE &&
                     verbosity_level > VERBOSITY_NONE )
                    fprintf( stdout, "#%ld: @ %ld / %ld ( %d @%ld ), ", j +1, index->list[j].in, index->list[j].out, index->list[j].window_size, index->list[j].window_beginning );
                if ( list_verbosity == VERBOSITY_MANIAC ) {
                    uint64_t local_window_size = index->list[j].window_size;
                    if ( local_window_size > 0 ) {
                        /* window is compressed on memory, so decompress it */
                        unsigned char *decompressed_window = NULL;
                        decompressed_window = decompress_chunk(index->list[j].window, &local_window_size);
                        if ( NULL == decompressed_window ) {
                            printToStderr( VERBOSITY_NORMAL, "\nERROR: Could not decompress window %d from index file '%s'.\n", j, file_name);
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                        free( decompressed_window );
                        decompressed_window = NULL;
                    }
                    if ( verbosity_level > VERBOSITY_NONE ) {
                        comp_win_counter   += index->list[j].window_size;
                        uncomp_win_counter += local_window_size;
                        fprintf( stdout, "#%ld: @ %ld / %ld ( %d/%ld %.2f%% ), ",
                            j +1, index->list[j].in, index->list[j].out, index->list[j].window_size,
                            local_window_size, ((local_window_size>0)?(100.0 - (double)(index->list[j].window_size) / (double)local_window_size * 100.0):0.0) );
                    }
                }
            }
        }

        if ( verbosity_level > VERBOSITY_NONE &&
             list_verbosity == VERBOSITY_MANIAC &&
             uncomp_win_counter > 0 ) {
            fprintf( stdout, "\n\tEstimated gzip data compression factor: %.2f%%", 100.0 - (double)comp_win_counter/(double)uncomp_win_counter*100.0 );
            if ( NULL != gzip_filename &&
                 st_gzip.st_size > 0 &&
                 index->file_size > 0 ) {
                fprintf( stdout, "\n\tReal gzip data compression factor     : %.2f%%", 100.0 - (double)st_gzip.st_size/(double)index->file_size*100.0 );
            }
        }

        if (verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\n" );

    }

action_list_info_error:
    if ( NULL != in )
        fclose( in );
    if ( NULL != index )
        free_index( index );
    if ( NULL != gzip_filename )
        free( gzip_filename );

    return ret_value;

}


// obtain an integer from a string that may used decimal point,
// with valid suffixes: kmgtpe (powers of 10) and KMGTPE (powers of 2),
// and valid prefixes: "0" (octal), "0x" or "0X" (hexadecimal).
// Examples:
// "83m" == 83*10^6, "9G" == 9*2^30, "0xa" == 10, "010k" = 8000, "23.5k" = 23500
// INPUT:
// unsigned char *original_input: string containing the data (only read)
// OUTPUT:
// 64 bit long integer number
uint64_t giveMeAnInteger( const unsigned char *original_input ) {

    int i = 0;
    unsigned char *PowerSuffixes = "kmgtpeKMGTPE";
    int PowerSuffixesLength = strlen(PowerSuffixes)/2;
    unsigned char *input = NULL;
    uint64_t result = 1;

    if ( NULL == original_input )
        return 0LL;

    input = malloc( strlen(original_input) +1 );
    memcpy(input, original_input, strlen(original_input) +1);

    if ( strlen(input) > 1 ) {
        // look for suffixes of size

        for ( i=0; i<strlen(PowerSuffixes); i++ ) {
            if ( (unsigned char *)strchr(input, PowerSuffixes[i]) == (unsigned char *)(input + strlen(input) -1) ) {
                if ( i >= PowerSuffixesLength ) {
                    result = pow( 2.0, 10.0 + 10.0*(double)(i - PowerSuffixesLength) );
                } else {
                    result = pow( 10.0, 3.0 + 3.0*(double)i );
                }
                input[strlen(input) -1] = '\0';
                break;
            }
        }

    }

    if ( strlen(input) > 1 ) {
        // look fo prefixes of base

        if ( input[0] == '0' ) {
            if ( strlen(input) > 2 &&
                 ( input[1] == 'x' || input[1] == 'X' ) ) {
                // hexadecimal
                result = strtoll( input +2, NULL, 16 ) * result;
            } else {
                // octal
                result = strtoll( input +1, NULL, 8 ) * result;
            }
            input[0] = '1';
            input[1] = '\0';
        }

    }

    result = (uint64_t)(strtod(input, NULL) * (double)result);

    if ( NULL != input )
        free( input );

    return result;

}


// .................................................


// print brief help
local void print_brief_help() {

    fprintf( stderr, "\n" );
    fprintf( stderr, "  gztool (v%s)\n", GZTOOL_VERSION );
    fprintf( stderr, "  GZIP files indexer and data retriever.\n" );
    fprintf( stderr, "  Create small indexes for gzipped files and use them\n" );
    fprintf( stderr, "  for quick and random positioned data extraction.\n" );
    fprintf( stderr, "  //github.com/circulosmeos/gztool (by Roberto S. Galende)\n\n" );
    fprintf( stderr, "  $ gztool [-[absv] #] [-cCdeEfFhilStTW] [-I <INDEX>] <FILE>...\n\n" );
    fprintf( stderr, "  `gztool -hh` for more help\n" );
    fprintf( stderr, "\n" );

}


// print help
local void print_help() {

    fprintf( stderr, "\n" );
    fprintf( stderr, "  gztool (v%s)\n", GZTOOL_VERSION );
    fprintf( stderr, "  GZIP files indexer and data retriever.\n" );
    fprintf( stderr, "  Create small indexes for gzipped files and use them\n" );
    fprintf( stderr, "  for quick and random positioned data extraction.\n" );
    fprintf( stderr, "  No more waiting when the end of a 10 GiB gzip is needed!\n" );
    fprintf( stderr, "  //github.com/circulosmeos/gztool (by Roberto S. Galende)\n\n" );
    fprintf( stderr, "  $ gztool [-[absv] #] [-cCdeEfFhilStTW] [-I <INDEX>] <FILE>...\n\n" );
    fprintf( stderr, "  Note that actions `-bStT` proceed to an index file creation (if\n" );
    fprintf( stderr, "  none exists) INTERLEAVED with data extraction. As extraction and\n" );
    fprintf( stderr, "  index creation occur at the same time there's no waste of time.\n" );
    fprintf( stderr, "  Also you can interrupt actions at any moment and the remaining\n" );
    fprintf( stderr, "  index file will be reused (and completed if necessary) on the\n" );
    fprintf( stderr, "  next gztool run over the same data.\n\n" );
    fprintf( stderr, " -a #: Await # seconds between reads when `-[ST]`. Default is 4 s.\n" );
    fprintf( stderr, " -b #: extract data from indicated uncompressed byte position of\n" );
    fprintf( stderr, "     gzip file (creating or reusing an index file) to STDOUT.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -C: always create a 'Complete' index file, ignoring possible errors\n" );
    fprintf( stderr, " -c: utility: raw-gzip-compress indicated file to STDOUT\n" );
    fprintf( stderr, " -d: utility: raw-gzip-decompress indicated file to STDOUT\n" );
    fprintf( stderr, " -e: if multiple files are indicated, continue on error (if any)\n" );
    fprintf( stderr, " -E: end processing on first GZIP end of file marker at EOF\n" );
    fprintf( stderr, " -f: force index overwriting from scratch, if one exists\n" );
    fprintf( stderr, " -F: force index creation/completion first, and then action: if\n" );
    fprintf( stderr, "     `-F` is not used, index is created interleaved with actions.\n" );
    fprintf( stderr, " -h: print brief help; `-hh` prints this help.\n" );
    fprintf( stderr, " -i: create index for indicated gzip file (For 'file.gz'\n" );
    fprintf( stderr, "     the default index file name will be 'file.gzi').\n" );
    fprintf( stderr, " -I INDEX: index file name will be 'INDEX'\n" );
    fprintf( stderr, " -l: check and list info contained in indicated index file.\n" );
    fprintf( stderr, "     `-ll` and `-lll` increase the level of index detail checking.\n" );
    fprintf( stderr, " -s #: span in uncompressed MiB between index points when\n" );
    fprintf( stderr, "     creating the index. By default is `10`.\n" );
    fprintf( stderr, " -S: Supervise indicated file: create a growing index,\n" );
    fprintf( stderr, "     for a still-growing gzip file. (`-i` is implicit).\n" );
    fprintf( stderr, " -t: tail (extract last bytes) to STDOUT on indicated gzip file\n" );
    fprintf( stderr, " -T: tail (extract last bytes) to STDOUT on indicated still-growing\n" );
    fprintf( stderr, "     gzip file, and continue Supervising & extracting to STDOUT.\n" );
    fprintf( stderr, " -v #: output verbosity: from `0` (none) to `5` (nuts)\n" );
    fprintf( stderr, "     Default is `1` (normal).\n" );
    fprintf( stderr, " -W: do not Write index to disk. But if one is already available\n" );
    fprintf( stderr, "     read and use it. Useful if the index is still under a `-S` run.\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "  Example: Extract data from 1 GiB byte (byte 2^30) on,\n" );
    fprintf( stderr, "  from `myfile.gz` to the file `myfile.txt`. Also gztool will\n" );
    fprintf( stderr, "  create (or reuse, or complete) an index file named `myfile.gzi`:\n" );
    fprintf( stderr, "  $ gztool -b 1G myfile.gz > myfile.txt\n" );
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
    int force_strict_order = 0;
    int write_index_to_disk = 1;
    int end_on_first_proper_gzip_eof = 0;
    int always_create_a_complete_index = 0;
    int waiting_time = WAITING_TIME;
    int count_errors = 0;

    enum EXIT_APP_VALUES ret_value;
    enum ACTION action;
    enum VERBOSITY_LEVEL list_verbosity = VERBOSITY_NONE;
    enum VERBOSITY_LEVEL help_verbosity = VERBOSITY_NONE;

    int opt = 0;
    int i;
    int actions_set = 0;


    action = ACT_NOT_SET;
    ret_value = EXIT_OK;
    while ((opt = getopt(argc, argv, "a:b:cCdeEfFhiI:ls:StTv:W")) != -1)
        switch (opt) {
            // help
            case 'h':
                help_verbosity++;
                if ( help_verbosity > VERBOSITY_EXCESSIVE )
                    help_verbosity = VERBOSITY_EXCESSIVE;
                break;
            // `-a #` default waiting time in seconds when supervising a growing gzip file (`-[ST]`)
            case 'a':
                waiting_time = (int)strtol( optarg, NULL, 10 );
                if ( waiting_time == 0 &&
                     ( strlen( optarg ) != 1 || '0' != optarg[0] ) ) {
                    printToStderr( VERBOSITY_NORMAL, "WARNING: Ignoring awaiting value of '%s'\n", optarg );
                    waiting_time = WAITING_TIME;
                }
                break;
            // `-b #` extracts data from indicated position byte in uncompressed stream of <FILE>
            case 'b':
                extract_from_byte = giveMeAnInteger( optarg );
                // read from stdin and output to stdout
                action = ACT_EXTRACT_FROM_BYTE;
                actions_set++;
                break;
            // `-c` compress <FILE> (or stdin if none) to stdout
            case 'c':
                action = ACT_COMPRESS_CHUNK;
                actions_set++;
                break;
            // `-C` generates always a complete index file, ignoring possible decompression errors
            case 'C':
                always_create_a_complete_index = 1;
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
            // `-E` ends gzip processing on first proper (at feof()) GZIP EOF
            case 'E':
                end_on_first_proper_gzip_eof = 1;
                break;
            // `-f` forces index overwriting from scratch, if one exists
            case 'f':
                force_action = 1;
                break;
            // First create index, the process indicated action
            case 'F':
                force_strict_order = 1;
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
                if ( list_verbosity == VERBOSITY_NONE )
                    actions_set++;
                list_verbosity++;
                if ( list_verbosity > VERBOSITY_MANIAC )
                    list_verbosity = VERBOSITY_MANIAC;
                break;
            // `-s #` span between index points, in MiB
            case 's':
                // span is converted to from MiB to bytes for internal use
                span_between_points = atoll(optarg) * 1024 * 1024;
                break;
            // `-S` supervise a still-growing gzip <FILE> and create index for it
            case 'S':
                action = ACT_SUPERVISE;
                actions_set++;
                break;
            // `-t` tail file contents
            case 't':
                action = ACT_EXTRACT_TAIL;
                actions_set++;
                break;
            // `-T` tail file contents and continue Supervising (and extracting data from) gzip file
            case 'T':
                action = ACT_EXTRACT_TAIL_AND_CONTINUE;
                actions_set++;
                break;
            // `-v` verbosity
            case 'v':
                verbosity_level = atoi(optarg);
                if ( ( optarg[0] != '0' && verbosity_level == 0 ) ||
                     strlen( optarg ) > 1 ||
                     verbosity_level > VERBOSITY_NUTS ) {
                    printToStderr( VERBOSITY_NORMAL, "Option `-v %s` ignored (`-v [0..5]`).\n", optarg );
                    verbosity_level = VERBOSITY_NORMAL;
                }
                break;
            // `-W` do not write nor update index on disk
            case 'W':
                write_index_to_disk = 0;
                break;
            case '?':
                if ( isprint (optopt) ) {
                    // print warning only if char option is unknown
                    if ( NULL == strchr("abcCdeEfFhiIlSstTvW", optopt) ) {
                        printToStderr( VERBOSITY_NORMAL, "Unknown option `-%c'.\n", optopt);
                        print_help();
                    }
                } else
                    printToStderr( VERBOSITY_NORMAL, "Unknown option character `\\x%x'.\n", optopt);
                printToStderr( VERBOSITY_NORMAL, "\n" );
                return EXIT_INVALID_OPTION;
            default:
                printToStderr( VERBOSITY_NORMAL, "\n" );
                abort ();
        }

    // check for help action and exit
    if ( help_verbosity > VERBOSITY_NONE ) {
        if ( help_verbosity == VERBOSITY_NORMAL )
            print_brief_help();
        else
            print_help();
        return EXIT_OK;
    }

    // Checking parameter merging and absence
    if ( actions_set > 1 ) {
        printToStderr( VERBOSITY_NORMAL, "Please, do not merge parameters `-bcdilStT`.\nAborted.\n\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( write_index_to_disk == 0 &&
           ( force_action == 1 || force_strict_order == 1)
        ) {
        printToStderr( VERBOSITY_NORMAL, "Please, do not merge contradictory parameters `-W` and `-fF`.\nAborted.\n\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK ) &&
        ( force_action == 1 || force_strict_order == 1 || write_index_to_disk == 0 ||
            span_between_points != SPAN || index_filename_indicated == 1 ||
            end_on_first_proper_gzip_eof == 1 || always_create_a_complete_index == 1 ||
            waiting_time != WAITING_TIME )
        ) {
        printToStderr( VERBOSITY_NORMAL, "WARNING: Ignoring `-aCEfFIsW` with `-[cd]`\n" );
        waiting_time = WAITING_TIME;
        force_action = 0;
        force_strict_order = 0;
        write_index_to_disk = 1;
        span_between_points = SPAN;
        index_filename_indicated = 0;
        end_on_first_proper_gzip_eof = 0;
        always_create_a_complete_index = 0;
    }

    if ( waiting_time >= UINT32_MAX || waiting_time < 0 ) {
        printToStderr( VERBOSITY_NORMAL, "WARNING: Ignoring awaiting value of '%d'\n", waiting_time );
        waiting_time = WAITING_TIME;
    }

    if ( span_between_points <= 0 ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid `-s` parameter value: '%d'\n", span_between_points );
        return EXIT_INVALID_OPTION;
    }
    if ( span_between_points != SPAN &&
        ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK || action == ACT_LIST_INFO )
        ) {
        printToStderr( VERBOSITY_NORMAL, "`-s` parameter will be ignored.\n" );
    }

    if ( actions_set == 0 ) {
        // `-I <FILE>` is equivalent to `-i -I <FILE>`
        if ( action == ACT_NOT_SET && index_filename_indicated  == 1 ) {
            action = ACT_CREATE_INDEX;
            if ( (optind + 1) < argc ) {
                // too much files indicated to use `-I`
                printToStderr( VERBOSITY_NORMAL, "`-I` is incompatible with multiple input files.\nAborted.\n\n" );
                return EXIT_INVALID_OPTION;
            }
        } else {
            printToStderr( VERBOSITY_NORMAL, "Please, indicate one parameter of `-bcdilStT`, or `-h` for help.\nAborted.\n\n" );
            return EXIT_INVALID_OPTION;
        }
    }

    if ( force_strict_order == 1 &&
        ( action == ACT_SUPERVISE ||
          action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
          action == ACT_LIST_INFO ||
          action == ACT_COMPRESS_CHUNK ||
          action == ACT_DECOMPRESS_CHUNK ) ) {
        printToStderr( VERBOSITY_NORMAL, "WARNING: There's no sense in using `-F` with `-cdlST`: ignoring `-F`.\n" );
        force_strict_order = 0;
    }


    {   // inform action on stderr:
        unsigned char *action_string;
        switch ( action ) {
            case ACT_EXTRACT_FROM_BYTE:
                action_string = "Extract from byte = ";
                break;
            case ACT_COMPRESS_CHUNK:
                action_string = "Compress chunk";
                break;
            case ACT_DECOMPRESS_CHUNK:
                action_string = "Decompress chunk";
                break;
            case ACT_CREATE_INDEX:
                action_string = "Create index";
                break;
            case ACT_SUPERVISE:
                action_string = "Supervise still-growing file";
                break;
            case ACT_LIST_INFO:
                action_string = "Check & list info in index file";
                break;
            case ACT_EXTRACT_TAIL:
                action_string = "Extract tail data";
                break;
            case ACT_EXTRACT_TAIL_AND_CONTINUE:
                action_string = "Extract from tail data from a still-growing file";
                break;
        }
        if ( action == ACT_EXTRACT_FROM_BYTE )
            printToStderr( VERBOSITY_NORMAL, "ACTION: %s%ld\n\n", action_string, extract_from_byte );
        else
            printToStderr( VERBOSITY_NORMAL, "ACTION: %s\n\n", action_string );
    }


    if (optind == argc || argc == 1) {
        // file input is stdin

        // check `-f` and execute delete if index file exists
        if ( ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ||
               action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
               action == ACT_EXTRACT_FROM_BYTE || action == ACT_EXTRACT_TAIL ) &&
             index_filename_indicated == 1 &&
             access( index_filename, F_OK ) != -1 ) {
            // index file already exists

            if ( force_action == 0 ) {
                printToStderr( VERBOSITY_NORMAL, "Index file '%s' already exists and will be used.\n", index_filename );
                if ( write_index_to_disk == 0 )
                    printToStderr( VERBOSITY_NORMAL, "Index file will NOT be modified.\n" );
                else
                    ; //printToStderr( VERBOSITY_NORMAL, "(Use `-f` to force overwriting.)\n" );
            } else {
                // force_action == 1 => delete index file
                printToStderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", index_filename );
                // delete it
                if ( remove( index_filename ) != 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\nAborted.\n", index_filename );
                    ret_value = EXIT_GENERIC_ERROR;
                }
            }

        }

        // `-F` has no sense with stdin
        if ( force_strict_order == 1 ) {
            printToStderr( VERBOSITY_NORMAL, "WARNING: There is no sense in using `-F` with stdin input: ignoring `F`.\n" );
            force_strict_order = 0;
        }

        // file input is stdin
        switch ( action ) {

            case ACT_EXTRACT_FROM_BYTE:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_FROM_BYTE, extract_from_byte, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;
                } else {
                    printToStderr( VERBOSITY_NORMAL, "`-I INDEX` must be used when extracting from stdin.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }

            case ACT_COMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != compress_file( stdin, stdout, Z_DEFAULT_COMPRESSION ) ) {
                    printToStderr( VERBOSITY_NORMAL, "Error while compressing stdin.\nAborted.\n\n" );
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
                    printToStderr( VERBOSITY_NORMAL, "Error while decompressing stdin.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                ret_value = EXIT_OK;
                break;

            case ACT_CREATE_INDEX:
                // stdin is a gzip file that must be indexed
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename, JUST_CREATE_INDEX,
                        0, span_between_points, write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                } else {
                    ret_value = action_create_index( "", &index, "", JUST_CREATE_INDEX,
                        0, span_between_points, write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_LIST_INFO:
                if ( index_filename_indicated == 1 ) {
                    // Admit `-I` as a way to indicate an input index file name:
                    ret_value = action_list_info( index_filename, NULL, list_verbosity );
                } else {
                    // stdin is an index file that must be checked
                    ret_value = action_list_info( "", NULL, list_verbosity );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_SUPERVISE:
                // stdin is a gzip file for which an index file must be created on-the-fly
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename, SUPERVISE_DO, 0,
                        span_between_points, write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                } else {
                    ret_value = action_create_index( "", &index, "", SUPERVISE_DO, 0,
                        span_between_points, write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_EXTRACT_TAIL:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_TAIL, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                } else {
                    // if an index filename is not indicated, index will not be output
                    // as stdout is already used for data extraction
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Index filename is needed if stdin is used as gzip input.\nAborted.\n" );
                    ret_value = EXIT_INVALID_OPTION;
                }
                break;

            case ACT_EXTRACT_TAIL_AND_CONTINUE:
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

        }

    } else {

        if ( action == ACT_SUPERVISE &&
             ( argc - optind > 1 ) ) {
            // supervise only accepts one input gz file
            printToStderr( VERBOSITY_NORMAL, "`-S` option only accepts one gzip file parameter: %d indicated.\n", argc - optind );
            printToStderr( VERBOSITY_NORMAL, "Aborted.\n" );
            return EXIT_GENERIC_ERROR;
        }

        for (i = optind; i < argc; i++) {

            file_name = argv[i];

            ret_value = EXIT_OK;

            // if no index filename is set (`-I`), it is derived from each <FILE> parameter
            if ( 0 == index_filename_indicated ) {
                if ( NULL != index_filename ) {
                    free(index_filename);
                    index_filename = NULL;
                }
                index_filename = malloc( strlen(argv[i]) + 4 + 1 );
                sprintf( index_filename, "%s", argv[i] );
                if ( (unsigned char *)strstr(index_filename, ".gz") == (unsigned char *)(index_filename + strlen(argv[i]) - 3) )
                    // if gzip-file name is 'FILE.gz', index file name will be 'FILE.gzi'
                    sprintf(index_filename, "%si", argv[i]);
                else
                    // otherwise, the complete extension '.gzi' is appended
                    sprintf(index_filename, "%s.gzi", argv[i]);
            }

            // free previous loop's resources
            if ( NULL != in ) {
                fclose( in );
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

            if ( ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ||
                   action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
                   action == ACT_EXTRACT_FROM_BYTE || action == ACT_EXTRACT_TAIL ) &&
                 access( index_filename, F_OK ) != -1 ) {
                // index file already exists

                if ( force_action == 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "Index file '%s' already exists and will be used.\n", index_filename );
                    if ( write_index_to_disk == 0 )
                        printToStderr( VERBOSITY_NORMAL, "Index file will NOT be modified.\n" );
                    else
                        ; //printToStderr( VERBOSITY_NORMAL, "(Use `-f` to force overwriting.)\n" );
                } else {
                    // force_action == 1
                    // delete index file
                    printToStderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", index_filename );
                    if ( remove( index_filename ) != 0 ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\nAborted.\n", index_filename );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                }

            }


            // check possible errors and `-e` before proceed
            if ( ret_value != EXIT_OK ) {
                if ( continue_on_error == 1 ) {
                    continue;
                } else {
                    break; // breaks for() loop
                }
            }


            // create index first if `-F`
            // (checking of conformity between `-F` and action has been done before)
            if ( force_strict_order == 1 ) {
                ret_value = action_create_index( file_name, &index, index_filename,
                            JUST_CREATE_INDEX, 0, span_between_points, write_index_to_disk,
                            end_on_first_proper_gzip_eof, always_create_a_complete_index,
                            waiting_time );
            }


            // "-bil" options can accept multiple files
            switch ( action ) {

                case ACT_EXTRACT_FROM_BYTE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_FROM_BYTE, extract_from_byte, span_between_points, write_index_to_disk,
                        end_on_first_proper_gzip_eof, always_create_a_complete_index,
                        waiting_time );
                    break;

                case ACT_COMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        printToStderr( VERBOSITY_NORMAL, "Error while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != compress_file( in, stdout, Z_DEFAULT_COMPRESSION ) ) {
                        printToStderr( VERBOSITY_NORMAL, "Error while compressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_DECOMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        printToStderr( VERBOSITY_NORMAL, "Error while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != decompress_file( in, stdout ) ) {
                        printToStderr( VERBOSITY_NORMAL, "Error while decompressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_CREATE_INDEX:
                    if ( force_strict_order == 0 )
                        // if force_strict_order == 1 action has already been done!
                        ret_value = action_create_index( file_name, &index, index_filename,
                            JUST_CREATE_INDEX, 0, span_between_points, write_index_to_disk,
                            end_on_first_proper_gzip_eof, always_create_a_complete_index,
                            waiting_time );
                    break;

                case ACT_LIST_INFO:
                    if ( index_filename_indicated == 1 ) {
                        // if index filename has been indicated with `-I`
                        // then the actual parameter is the gzip filename
                        ret_value = action_list_info( index_filename, file_name, list_verbosity );
                    } else {
                        ret_value = action_list_info( file_name, NULL, list_verbosity );
                    }
                    break;

                case ACT_SUPERVISE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        SUPERVISE_DO, 0, span_between_points, write_index_to_disk,
                        end_on_first_proper_gzip_eof, always_create_a_complete_index,
                        waiting_time );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;

                case ACT_EXTRACT_TAIL:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_TAIL, 0, span_between_points, write_index_to_disk,
                        end_on_first_proper_gzip_eof, always_create_a_complete_index,
                        waiting_time );
                    break;

                case ACT_EXTRACT_TAIL_AND_CONTINUE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, span_between_points, write_index_to_disk,
                        end_on_first_proper_gzip_eof, always_create_a_complete_index,
                        waiting_time );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;

            }

            if ( action == ACT_LIST_INFO ) {
                // as ACT_LIST_INFO prints to stdout,
                // file input separator must be printed also to stdout
                if ( verbosity_level >= VERBOSITY_NORMAL )
                    fprintf( stdout, "\n" );
            } else {
                printToStderr( VERBOSITY_NORMAL, "\n" );
            }

            printToStderr( VERBOSITY_MANIAC, "ERROR code = %d\n", ret_value );

            if ( ret_value != EXIT_OK )
                count_errors++;

            if ( continue_on_error == 0 &&
                 ret_value != EXIT_OK ) {
                printToStderr( VERBOSITY_NORMAL, "Aborted.\n" );
                // break the for loop
                break;
            }

        }

    }

    if ( (i -optind) >= 1 )
        printToStderr( VERBOSITY_NORMAL, "%d files processed\n", 
            ( i -optind + ( (count_errors>0 && continue_on_error == 0 )?1:0 ) ) );
    if ( count_errors > 0 )
        printToStderr( VERBOSITY_NORMAL, "%d files processed with errors!\n", count_errors );

    printToStderr( VERBOSITY_NORMAL, "\n" );

    // final freeing of resources
    if ( NULL != in ) {
        fclose( in );
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

    if ( ( i -optind + ( (count_errors>0 && continue_on_error == 0 )?1:0 ) ) > 1 ) {
        // if processing multiple files, return EXIT_GENERIC_ERROR if any one failed:
        if ( count_errors > 0 )
            return EXIT_GENERIC_ERROR;
        else
            return EXIT_OK;
    } else
        return ret_value;

}
