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
#include "zlib.h"

#define local static

#define SPAN 1048576L       /* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */
#define UNCOMPRESSED_WINDOW UINT32_MAX // window is an uncompressed WINSIZE size window
#define GZIP_INDEX_IDENTIFIER_STRING "gzipindx"
#define GZIP_INDEX_HEADER_SIZE 16
#define INDEX_WINDOWS_ON_DISK

/* access point entry */
struct point {
    off_t out;             /* corresponding offset in uncompressed data */
    off_t in;              /* offset in input file of first full byte */
    uint32_t bits;         /* number of bits (1-7) from byte at in - 1, or 0 */
    off_t window_beginning;/* offset at index file where this compressed window is stored (used #ifdef INDEX_WINDOWS_ON_DISK)*/
    uint32_t window_size;  /* size of window */
    unsigned char *window; /* preceding 32K of uncompressed data, compressed */
};

/* access point list */
struct access {
    uint64_t have;      /* number of list entries filled in */
    uint64_t size;      /* number of list entries allocated */
    uint64_t file_size; /* size of uncompressed file (useful for bgzip files) */
    struct point *list; /* allocated list */
    unsigned char *file_name; /* path to index file (used #ifdef INDEX_WINDOWS_ON_DISK) */
};


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

// fread substitute for endianness management of 4 and 8 bytes words
size_t fread_endian( void * ptr, size_t size, size_t count, FILE * stream ) {

    size_t output = fread(ptr, size, count, stream);

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

// fread substitute for endianness management of 4 and 8 bytes words
size_t fwrite_endian( void * ptr, size_t size, size_t count, FILE * stream ) {

    size_t output;
    void *endian_ptr;

    if (endiannes_is_big()) {
        ;
    } else {
        // machine is little endian: flip bytes
        endian_ptr = malloc(size);
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

    output = fwrite(endian_ptr, size, count, stream);

    free(endian_ptr);
    return output;

}
/*******************
 * end: Endianness *
 *******************/


// compression function, based on def() from examples/zpipe.c
/* Compress from file source to file dest until EOF on source.
   def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_STREAM_ERROR if an invalid compression
   level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
   version of the library linked do not match, or Z_ERRNO if there is
   an error reading or writing the files. */
unsigned char *compress_chunk(unsigned char *source, uint32_t *size, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    int i = 0;
    uint32_t output_size = 0;
    uint32_t input_size = *size;
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
        fprintf(stderr, "strm.avail_in = %d (i=%d) (input_size=%d)\n", strm.avail_in, i, input_size);
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
            fprintf(stderr, "strm.avail_out = %d (have=%d)\n", strm.avail_out, have);
            if ( have != 0 && (
                 NULL == (out_complete = realloc(out_complete, output_size + have)) ||
                 NULL == memcpy(out_complete + output_size, out, have)
                 ) ) {
                (void)deflateEnd(&strm);
                goto compress_chunk_error;
            }
            output_size += have;
        } while (strm.avail_out == 0);
        fprintf(stderr, "output_size = %d\n", output_size);
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


// decompression function, based on inf() from examples/zpipe.c
/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
unsigned char *decompress_chunk(unsigned char *source, int *size)
{
    int ret;
    unsigned have;
    z_stream strm;
    int i = 0;
    int output_size = 0;
    int input_size = *size;
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
        fprintf(stderr, "strm.avail_in = %d (i=%d) (input_size=%d)\n", strm.avail_in, i, input_size);
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
            fprintf(stderr, "strm.avail_out = %d (have=%d)\n", strm.avail_out, have);
            if ( have != 0 && (
                 NULL == (out_complete = realloc(out_complete, output_size + have)) ||
                 NULL == memcpy(out_complete + output_size, out, have)
                 ) ) {
                (void)inflateEnd(&strm);
                goto decompress_chunk_error;
            }
            output_size += have;
        } while (strm.avail_out == 0);
        fprintf(stderr, "output_size = %d\n", output_size);

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


/* Deallocate an index built by build_index() */
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


/* Add an entry to the access point list.  If out of memory, deallocate the
   existing list and return NULL. */
local struct access *addpoint(struct access *index, uint32_t bits,
    off_t in, off_t out, unsigned left, unsigned char *window)
{
    struct point *next;
    uint32_t size = WINSIZE;
    unsigned char *compressed_chunk;

    /* if list is empty, create it (start with eight points) */
    if (index == NULL) {
        index = malloc(sizeof(struct access));
        if (index == NULL) return NULL;
        index->list = malloc(sizeof(struct point) << 3);
        if (index->list == NULL) {
            free(index);
            return NULL;
        }
        index->size = 8;
        index->have = 0;
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
    }

    /* fill in entry and increment how many we have */
    next = index->list + index->have;
    next->bits = bits;
    next->in = in;
    next->out = out;
    next->window_size = UNCOMPRESSED_WINDOW; // uncompressed WINSIZE next->window
    next->window = malloc(WINSIZE);
    if (left)
        memcpy(next->window, window + WINSIZE - left, left);
    if (left < WINSIZE)
        memcpy(next->window + left, window, WINSIZE - left);
    index->have++;

    // compress window
    compressed_chunk = compress_chunk(next->window, &size, Z_DEFAULT_COMPRESSION);
    if (compressed_chunk == NULL) {
        fprintf(stderr, "Error whilst compressing index chunk\nProcess aborted\n.");
        return NULL;
    } 
    free(next->window);
    next->window = compressed_chunk;
    next->window_size = size;

    /* return list, possibly reallocated */
    return index;
}


/* Make one entire pass through the compressed stream and build an index, with
   access points about every span bytes of uncompressed output -- span is
   chosen to balance the speed of random access against the memory requirements
   of the list, about 32K bytes per access point.  Note that data after the end
   of the first zlib or gzip stream in the file is ignored.  build_index()
   returns the number of access points on success (>= 1), Z_MEM_ERROR for out
   of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
   file read error.  On success, *built points to the resulting index. */
local uint64_t build_index(FILE *in, off_t span, struct access **built)
{
    uint64_t ret;
    off_t totin, totout;        /* our own total counters to avoid 4GB limit */
    off_t last;                 /* totout value of last access point */
    struct access *index;       /* access points being generated */
    z_stream strm;
    unsigned char input[CHUNK];
    unsigned char window[WINSIZE];

    /* initialize inflate */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding */
    if (ret != Z_OK)
        return ret;

    /* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */
    totin = totout = last = 0;
    index = NULL;               /* will be allocated by first addpoint() */
    strm.avail_out = 0;
    do {
        /* get some compressed data from input file */
        strm.avail_in = fread(input, 1, CHUNK, in);
        if (ferror(in)) {
            ret = Z_ERRNO;
            goto build_index_error;
        }
        if (strm.avail_in == 0) {
            ret = Z_DATA_ERROR;
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
            ret = inflate(&strm, Z_BLOCK);      /* return at end of block */
            totin -= strm.avail_in;
            totout -= strm.avail_out;
            if (ret == Z_NEED_DICT)
                ret = Z_DATA_ERROR;
            if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR)
                goto build_index_error;
            if (ret == Z_STREAM_END)
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
                                 totout, strm.avail_out, window);
                if (index == NULL) {
                    ret = Z_MEM_ERROR;
                    goto build_index_error;
                }
                last = totout;
            }
        } while (strm.avail_in != 0);
    } while (ret != Z_STREAM_END);

    /* clean up and return index (release unused entries in list) */
    (void)inflateEnd(&strm);
    index->list = realloc(index->list, sizeof(struct point) * index->have);
    index->size = index->have;
    index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */
fprintf(stderr, "index->file_size = %ld\n", totout);
    *built = index;
    return index->size;

    /* return error */
  build_index_error:
    (void)inflateEnd(&strm);
    if (index != NULL)
        free_index(index);
    return ret;
}


/* Use the index to read len bytes from offset into buf, return bytes read or
   negative for error (Z_DATA_ERROR or Z_MEM_ERROR).  If data is requested past
   the end of the uncompressed data, then extract() will return a value less
   than len, indicating how much as actually read into buf.  This function
   should not return a data error unless the file was modified since the index
   was generated.  extract() may also return Z_ERRNO if there is an error on
   reading or seeking the input file. */
local uint64_t extract(FILE *in, struct access *index, off_t offset,
                  unsigned char *buf, uint64_t len)  // TODO: cambiar a uint64_t, y escribir en stdout, no buf si buf==NULL
{
    uint64_t ret;
    int skip;
    int output_to_stdout = 0;
    int extract_all_input = 0;
    unsigned have;
    z_stream strm;
    struct point *here;
    unsigned char input[CHUNK];
    unsigned char discard[WINSIZE];
    unsigned char *decompressed_window;
    uint64_t initial_len = len;

    /* proceed only if something reasonable to do */
    if (NULL == in || NULL == index)
        return 0;
    if (len < 0)
        return 0;
    if (len == 0 && buf != NULL)
        return 0;

    /* print decompression to stdout if buf == NULL */
    if (buf == NULL) {
        // buf of size WINSIZE > CHUNK so probably
        // each CHUNK is totally deflated on each step
        if (NULL == (buf = malloc(WINSIZE)))
            return Z_ERRNO;
        output_to_stdout = 1;
        if (len == 0) {
            extract_all_input = 1;
        }
    }

    /* find where in stream to start */
    here = index->list;
    ret = index->have;
    while (--ret && here[1].out <= offset)
        here++;

    /* initialize file and inflate state to start there */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, -15);         /* raw inflate */
    if (ret != Z_OK)
        goto extract_ret;
    ret = fseeko(in, here->in - (here->bits ? 1 : 0), SEEK_SET);
    if (ret == -1)
        goto extract_ret;
    if (here->bits) {
        ret = getc(in);
        if (ret == -1) {
            ret = ferror(in) ? Z_ERRNO : Z_DATA_ERROR;
            goto extract_ret;
        }
        (void)inflatePrime(&strm, here->bits, ret >> (8 - here->bits));
    }

/* #ifdef INDEX_WINDOWS_ON_DISK */
fprintf(stderr, "\n\n>>>>>>>>>%s,%p,%ld\n\n",index->file_name,here->window,here->window_beginning);
    if (here->window == NULL && here->window_beginning != 0) {
        /* index' window data is not on memory, 
        but we have position and size on index file, so we load it now */
        FILE *index_file;
        if (NULL == (index_file = fopen(index->file_name, "rb")) ||
            0 != fseeko(index_file, here->window_beginning, SEEK_SET)
            ) {
            fprintf(stderr, "Error while opening index file. Extraction aborted.\n");
            fclose(index_file);
            ret = Z_ERRNO;
            goto extract_ret;
        }
        here->window_beginning = 0;
        if ( NULL == (here->window = malloc(here->window_size)) ||
            !fread(here->window, here->window_size, 1, index_file) 
            ) {
            fprintf(stderr, "Error while reading index file. Extraction aborted.\n");
            fclose(index_file);
            ret = Z_ERRNO;
            goto extract_ret;
        }
        fclose(index_file);
    }
/* #endif */
fprintf(stderr, "\n\n<<<<<<<<<%d\n\n",here->window_size);
    if (here->window_size != UNCOMPRESSED_WINDOW) {
        /* window is compressed on memory, so decompress it */
        decompressed_window = decompress_chunk(here->window, &(here->window_size));
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
            fprintf(stderr, " > ");
            if (skip == 0 && output_to_stdout == 1) {
                strm.next_out = buf;
                if (extract_all_input == 0) {
                    strm.avail_out = (len <= WINSIZE) ? len : WINSIZE;
                } else {
                    strm.avail_out = WINSIZE;
                }
            }
            fprintf(stderr, " 2 ");
            if (strm.avail_in == 0) {
                strm.avail_in = fread(input, 1, CHUNK, in);
                if (ferror(in)) {
                    ret = Z_ERRNO;
                    goto extract_ret;
                }
                if (strm.avail_in == 0) {
                    ret = Z_DATA_ERROR;
                    goto extract_ret;
                }
                strm.next_in = input;
            }
            fprintf(stderr, " 3 ");
            ret = inflate(&strm, Z_NO_FLUSH);       /* normal inflate */
            if (ret == Z_NEED_DICT)
                ret = Z_DATA_ERROR;
            if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR)
                goto extract_ret;
            fprintf(stderr, "RET = %ld ;", ret);
            if (skip == 0 && output_to_stdout == 1) {
                /* print decompression to stdout */
                have = WINSIZE - strm.avail_out;
                if (have > len && extract_all_input == 0) {
                    have = len;
                }
                fprintf(stderr, "\noutput_to_stdout = %d, extract_all_input = %d, len = %ld; ", output_to_stdout, extract_all_input, len);
                fprintf(stderr, "HAVE = %d ;", have);
                if (fwrite(buf, 1, have, stdout) != have || ferror(stdout)) {
                    (void)inflateEnd(&strm);
                    ret = Z_ERRNO;
                    goto extract_ret;
                }
                if (skip == 0) {
                    if (extract_all_input == 0) {
                        len -= have;
                    } else {
                        len += have;
                    }
                }
                fprintf(stderr, "LEN = %ld, SKIP = %d ;", len, skip);
            }
            if (ret == Z_STREAM_END)
                break;
        } while (
            // skipping output, but window not completely filled
            (fprintf(stderr, "...while..."),strm.avail_out != 0) ||
            // extracting to stdout and specified length not reached:
            (skip == 0 && output_to_stdout == 1 && len > 0) ||
            // extract the whole input (until break on Z_STREAM_END)
            (skip == 0 && extract_all_input == 1)
            );

        /* if reach end of stream, then don't keep trying to get more */
        if (ret == Z_STREAM_END)
            break;

        /* do until offset reached and requested data read, or stream ends */
    } while (skip);

    fprintf(stderr, "RET FINAL= %ld ;", ret);
    /* compute number of uncompressed bytes read after offset */
    ret = skip ? 0 : len - strm.avail_out;
    if (output_to_stdout == 1) {
        ret = initial_len - len;
    }
    if (extract_all_input == 1) {
        ret = len;
    }
    // TODO: extract len bytes with output_to_stdout, and also set len at the end: 20190710 DONE

    /* clean up and return bytes read or error */
  extract_ret:
    fprintf(stderr, "!!! RET = %ld ;", ret);
    (void)inflateEnd(&strm);
    if (output_to_stdout == 1)
        free(buf);
    return ret;
}


// TODO: functions description
int serialize_index_to_file( FILE *output_file, struct access *index ) {
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
    /* writing and empy index is allowed (of size 4*8 = 32 bytes) */

    /* write header */
    /* 0x0 8 bytes (to be compatible with .gzi for bgzip format: */
    /* the initial uint32_t is the number of bgzip-idx registers) */
    temp = 0;
    fwrite_endian(&temp, sizeof(temp), 1, output_file);
    /* a 64 bits readable identifier */
    fprintf(output_file, GZIP_INDEX_IDENTIFIER_STRING);

    /* and now the raw data: the access struct "index" */
    fwrite_endian(&index->have, sizeof(index->have), 1, output_file);
    /* index->size is not written as only filled entries are usable */
    fwrite_endian(&index->have, sizeof(index->have), 1, output_file);

    for (i = 0; i < index->have; i++) {
        here = &(index->list[i]);
        fwrite_endian(&(here->out),  sizeof(here->out),  1, output_file);
        fwrite_endian(&(here->in),   sizeof(here->in),   1, output_file);
        fwrite_endian(&(here->bits), sizeof(here->bits), 1, output_file);
        fwrite_endian(&(here->window_size), sizeof(here->window_size), 1, output_file);
        if (NULL == here->window) {
            fprintf(stderr, "Index incomplete! - index writing aborted.\n");
            return 0;
        } else {
            fwrite(here->window, here->window_size, 1, output_file);
        }

        /*fprintf(stderr, "%ld >\n", here->out);
        fprintf(stderr, "%ld >\n", here->in);
        fprintf(stderr, "%d >\n", here->bits);
        fprintf(stderr, "%d >\n", here->window_size);*/
    }

    /* write size of uncompressed file (useful for bgzip files) */
    fwrite_endian(&index->file_size, sizeof(index->file_size), 1, output_file);

    return 1;
}


struct access *deserialize_index_from_file( FILE *input_file, int load_windows, unsigned char *file_name ) {
    struct point *here;
    struct access *index;
    uint32_t i;
    char header[GZIP_INDEX_HEADER_SIZE];

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

    index = malloc(sizeof(struct access));

    fread(header, 1, GZIP_INDEX_HEADER_SIZE, input_file);
    if (*((uint64_t *)header) != 0 ||
        strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING, GZIP_INDEX_HEADER_SIZE/2) != 0) {
        fprintf(stderr, "File is not a valid gzip index file: %s\n", &header[8]);
        fprintf(stderr, "File is not a valid gzip index file: %d, %d, %d, %d\n", (int)header[0], (int)header[1], (int)header[2], (int)header[3]);
        fprintf(stderr, "File is not a valid gzip index file.\n");
        return NULL;
    }

    fread_endian(&index->have, sizeof(index->have), 1, input_file);
    if (index->have <= 0) {
        fprintf(stderr, "Index file contains no indexes.\n");
        return NULL;
    }

    // index->size should be equal to index->have, but this isn't checked
    fread_endian(&index->size, sizeof(index->size), 1, input_file);

    // create the list of points
    index->list = malloc(sizeof(struct point) * index->size);

    for (i = 0; i < index->size; i++) {
        here = &(index->list[i]);
        fread_endian(&(here->out),  sizeof(here->out),  1, input_file);
        fread_endian(&(here->in),   sizeof(here->in),   1, input_file);
        fread_endian(&(here->bits), sizeof(here->bits), 1, input_file);
        fread_endian(&(here->window_size), sizeof(here->window_size), 1, input_file);
        if ( load_windows == 0 ) {
            here->window = NULL;
            here->window_beginning = ftello(input_file);
        } else {
            here->window = malloc(here->window_size);
            here->window_beginning = 0;
            if (here->window == NULL) {
                fprintf(stderr, "Not enough memory to load index from file.\n");
                goto deserialize_index_from_file_error;
            }
            if ( !fread(here->window, here->window_size, 1, input_file) ) {
                fprintf(stderr, "Error while reading index file.\n");
                goto deserialize_index_from_file_error;
            }
        }
        /*fprintf(stderr, "%ld <\n", here->out);
        fprintf(stderr, "%ld <\n", here->in);
        fprintf(stderr, "%d <\n", here->bits);
        fprintf(stderr, "%d <\n", here->window_size);*/
    }

    /* read size of uncompressed file (useful for bgzip files) */
    /* this field may not exist (maybe useful for growing gzip files?) */
    index->file_size = 0;
    fread_endian(&index->file_size, sizeof(index->file_size), 1, input_file);


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


/* Demonstrate the use of build_index() and extract() by processing the file
   provided on the command line, and the extracting 16K from about 2/3rds of
   the way through the uncompressed output, and writing that to stdout. */
int main(int argc, char **argv)
{
    uint64_t len;
    off_t offset;
    FILE *in;
    struct access *index = NULL;
    unsigned char buf[CHUNK];
    // output:
    FILE *index_file = NULL;
    int MAX_PATH_LENGTH = 265;
    char output_file[MAX_PATH_LENGTH];
    //char *output_file="TEST.gz.gzi";
    sprintf(output_file, "%s.gzi", argv[1]);

    //struct access *index2 = NULL; // temp
    int TEST, TEST_OUT;

    /* open input file */
    if (argc != 2) {
        fprintf(stderr, "usage: zran file.gz\n");
        return 1;
    }
    in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "create_index: could not open %s for reading\n", argv[1]);
        return 1;
    }

    TEST = 4+16;
    TEST_OUT = 0;
// -------------------------------------------------------------------------------
    if (TEST & 1) {
        fprintf(stderr, "\t[ 1 ]\a\n");
        /* build index */
        len = build_index(in, SPAN, &index);
        if (len < 0) {
           fclose(in);
           switch (len) {
           case Z_MEM_ERROR:
               fprintf(stderr, "create_index: out of memory\n");
               break;
           case Z_DATA_ERROR:
               fprintf(stderr, "create_index: compressed data error in %s\n", argv[1]);
               break;
           case Z_ERRNO:
               fprintf(stderr, "create_index: read error on %s\n", argv[1]);
               break;
           default:
               fprintf(stderr, "create_index: error %ld while building index\n", len);
           }
           return 1;
        }
        fprintf(stderr, "create_index: built index with %ld access points\n", len);

        if (TEST_OUT & 1) {
            fclose(in);
            free_index(index);
            return 0;
        }
    }

// -------------------------------------------------------------------------------
    if (TEST & 2) {
        fprintf(stderr, "\t[ 2 ]\a\a\n");
        /* write index to appropriate index-file */
        if (strlen(argv[1]) > (MAX_PATH_LENGTH - 5)) {
           fprintf(stderr, "create_index: cannot write %s.gzi: PATH too large\n", argv[1]);
           return 1;
        }
        index_file = fopen( output_file, "wb" );
        if (index_file == NULL) {
           fprintf(stderr, "create_index: could not open %s for writing\n", output_file);
           return 1;
        }
        /* write index to index-file */
        if ( ! serialize_index_to_file(index_file, index) ) {
           fprintf(stderr, "create_index: could not write index to file %s\n", output_file);
           return 1;
        }
        //fclose(index_file);

        if (TEST_OUT & 2) {
            fclose(in);
            fclose(index_file);
            free_index(index);
            return 0;
        }
    }

// -------------------------------------------------------------------------------
    if (TEST & 4) {
        fprintf(stderr, "\t[ 4 ]\a\a\a\a\n");
        /* read index from index-file */
        if (NULL != index)
            free_index( index );
        fprintf(stderr, "Reading index from file %s\n", output_file);
        if (NULL != index_file)
            fclose(index_file);
        index_file = fopen( output_file, "rb" );
        //index2 = deserialize_index_from_file(index_file, 1, output_file);
        /*
        if deserialize_index_from_file(, 0,) then TEST & 8 cannot be called!
        because index is incomplete on memory. It'll be completed when used
        on calling to extract().
        */
        index = deserialize_index_from_file(index_file, 0, output_file);
        //if ( ! index2 ) {
        if ( ! index ) {
            fprintf(stderr, "create_index: could not read index from file %s\n", output_file);
            return 1;
        }
        fprintf(stderr, "Reading index finished.\n");

        if (TEST_OUT & 4) {
            fclose(in);
            fclose(index_file);
            free_index(index);
            return 0;
        }

        //fprintf(stderr, "index == index2 : %d\n", memcmp(index->list[0].window, index2->list[0].window, index->list[0].window_size));
    }

// -------------------------------------------------------------------------------
    if (TEST & 8) {
        fprintf(stderr, "\t[ 8 ]\a\a\a\a\a\a\a\a\n");
        /* test: write the read index to another file, for comparison */
        fprintf(stderr, "Writing second index to file %s2\n", output_file);
        if (NULL != index_file)
            fclose(index_file);
        sprintf(output_file, "%s.gzi2", argv[1]);
        index_file = fopen( output_file, "wb" );
        //index_file = fopen( "TEST.gz.gzi2", "wb" );
        if (index_file == NULL) {
            fprintf(stderr, "create_index: could not open %s for writing\n", output_file);
            return 1;
        }
        if ( ! serialize_index_to_file(index_file, index) ) {
            fprintf(stderr, "create_index: could not write index to file %s\n", output_file);
            return 1;
        }
        fprintf(stderr, "Writing second index finished.\n");
        fclose(index_file);

        if (TEST_OUT & 8) {
            fclose(in);
            fclose(index_file);
            free_index(index);
            return 0;
        }
    }

// -------------------------------------------------------------------------------
    if (TEST & 16) {
        fprintf(stderr, "\t[ 16 ]\a\a\a\a\a\a\a\a\a\a\a\a\a\a\a\a\n");
        /* use index by reading some bytes from an arbitrary offset */
        if (NULL != index_file)
            fclose(index_file);
        fprintf(stderr, "points : %ld\n", index->have);
        fprintf(stderr, "offset of last point : %ld\n", index->list[index->have - 1].out);
        offset = (index->list[index->have - 1].out << 1) / 3;
        fprintf(stderr, "inflating from byte: %ld\n", offset);
        len = extract(in, index, offset, buf, CHUNK); // TODO: pasar a 64
        //len = extract(in, index, offset, NULL, CHUNK*100); // TODO: pasar a 64
        fprintf(stderr, "create_index: test finished.\n");
        if (len < 0)
            fprintf(stderr, "create_index: extraction failed: %s error\n",
                    len == Z_MEM_ERROR ? "out of memory" : "input corrupted");
        else {
            //fwrite(buf, 1, len, stdout); // TODO: pasar a 64
            fprintf(stderr, "create_index: extracted %ld bytes at %ld\n", len, offset);
        }
        fprintf(stderr, "create_index: test start.\n");
        len = extract(in, index, offset, NULL, 0); // TODO: pasar a 64
        fprintf(stderr, "create_index: test finished.\n");
        if (len < 0)
            fprintf(stderr, "create_index: extraction failed: %s error\n",
                    len == Z_MEM_ERROR ? "out of memory" : "input corrupted");
        else {
            //fwrite(buf, 1, len, stdout); // TODO: pasar a 64
            fprintf(stderr, "create_index: extracted %ld bytes at %ld\n", len, offset);
        }

        /* clean up and exit */
        fclose(in);
        free_index(index);
        //free_index(index2);
        return 0;
    }

}
