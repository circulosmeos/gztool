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
#include <unistd.h> // getopt(), access(), sleep()
#include <ctype.h>  // isprint()

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

#define SPAN 1048576L       /* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */
#define UNCOMPRESSED_WINDOW UINT32_MAX // window is an uncompressed WINSIZE size window
#define GZIP_INDEX_IDENTIFIER_STRING "gzipindx"
#define GZIP_INDEX_HEADER_SIZE 16

/* access point entry */
struct point {
    off_t out;             /* corresponding offset in uncompressed data */
    off_t in;              /* offset in input file of first full byte */
    uint32_t bits;         /* number of bits (1-7) from byte at in - 1, or 0 */
    off_t window_beginning;/* offset at index file where this compressed window is stored */
    uint32_t window_size;  /* size of (compressed) window */
    unsigned char *window; /* preceding 32K of uncompressed data, compressed */
};

/* access point list */
struct access {
    uint64_t have;      /* number of list entries filled in */
    uint64_t size;      /* number of list entries allocated */
    uint64_t file_size; /* size of uncompressed file (useful for bgzip files) */
    struct point *list; /* allocated list */
    unsigned char *file_name; /* path to index file */
};

/* generic struct to return a function error code and a value */
struct returned_output {
    uint64_t value;
    int error;
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
local size_t fread_endian( void * ptr, size_t size, size_t count, FILE * stream ) {

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
local size_t fwrite_endian( void * ptr, size_t size, size_t count, FILE * stream ) {

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
        fprintf(stderr, "strm.avail_in = %d (i=%ld) (input_size=%ld)\n", strm.avail_in, i, input_size);
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
        fprintf(stderr, "output_size = %ld\n", output_size);
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
        fprintf(stderr, "strm.avail_in = %d (i=%ld) (input_size=%ld)\n", strm.avail_in, i, input_size);
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
        fprintf(stderr, "output_size = %ld\n", output_size);

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


/* Allocate an index and fill with empty values */
local struct access *create_empty_index()
{
    struct access *index;
    if ( NULL == ( index = malloc(sizeof(struct access)) ) ) {
        return NULL;
    }
    index->have = 0;
    index->size = 0;
    index->file_size = 0;
    index->list = NULL;
    index->file_name = NULL;
    return index;
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


/* fill pointers of and index->list with NULL
   so that free_index() can proceed on any contingency */
local void empty_index_list(struct access *index, uint64_t from, uint64_t to)
{
    uint64_t i;
    for (i=from; i++; i<to) {
        index->list[i].window = NULL;
        index->list[i].window_size = 0;
    }
    return;
}


/* Add an entry to the access point list.  If out of memory, deallocate the
   existing list and return NULL. */
local struct access *addpoint(struct access *index, uint32_t bits,
    off_t in, off_t out, unsigned left, unsigned char *window)
{
    struct point *next;
    uint64_t size = WINSIZE;
    unsigned char *compressed_chunk;

    /* if list is empty, create it (start with eight points) */
    if (index == NULL) {
        index = create_empty_index();
        if (index == NULL) return NULL;
        index->list = malloc(sizeof(struct point) << 3);
        empty_index_list( index, 0, 8 );
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
        empty_index_list( index, index->have + 1, index->size );
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
    /* uint64_t size and uint32_t window_size, but windows are small, so this will always fit */
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
local struct returned_output build_index(FILE *in, off_t span, struct access **built)
{
    struct returned_output ret;
    off_t totin, totout;        /* our own total counters to avoid 4GB limit */
    off_t last;                 /* totout value of last access point */
    struct access *index;       /* access points being generated */
    z_stream strm;
    unsigned char input[CHUNK];
    unsigned char window[WINSIZE];

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
                                 totout, strm.avail_out, window);
                if (index == NULL) {
                    ret.error = Z_MEM_ERROR;
                    goto build_index_error;
                }
                last = totout;
            }
        } while (strm.avail_in != 0);
    } while (ret.error != Z_STREAM_END);

    /* clean up and return index (release unused entries in list) */
    (void)inflateEnd(&strm);
    index->list = realloc(index->list, sizeof(struct point) * index->have);
    index->size = index->have;
    index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */
fprintf(stderr, "index->file_size = %ld\n", totout);
    *built = index;
    ret.value = index->size;
    return ret;

    /* return error */
  build_index_error:
    (void)inflateEnd(&strm);
    if (index != NULL)
        free_index(index);
    *built = NULL;
    return ret;
}


/* Use the index to read len bytes from offset into buf, return bytes read or
   negative for error (Z_DATA_ERROR or Z_MEM_ERROR).  If data is requested past
   the end of the uncompressed data, then extract() will return a value less
   than len, indicating how much as actually read into buf.  This function
   should not return a data error unless the file was modified since the index
   was generated.  extract() may also return Z_ERRNO if there is an error on
   reading or seeking the input file. */
local struct returned_output extract(FILE *in, struct access *index, off_t offset,
                  unsigned char *buf, uint64_t len)  // TODO: cambiar a uint64_t, y escribir en stdout, no buf si buf==NULL
{
    struct returned_output ret;
    int i, skip;
    int output_to_stdout = 0;
    int extract_all_input = 0;
    unsigned have;
    z_stream strm;
    struct point *here;
    unsigned char input[CHUNK];
    unsigned char discard[WINSIZE];
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
fprintf(stderr, "\n\n>>>>>>>>>%ld\n\n",here->out);
    if (ret.error != Z_OK) {
        if (output_to_stdout == 1)
            free(buf);
        return ret;
    }
    ret.error = fseeko(in, here->in - (here->bits ? 1 : 0), SEEK_SET);
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
fprintf(stderr, "\n\n<<<<<<<<<%d\n\n",here->window_size);
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
                    ret.error = Z_ERRNO;
                    goto extract_ret;
                }
                if (strm.avail_in == 0) {
                    ret.error = Z_DATA_ERROR;
                    goto extract_ret;
                }
                strm.next_in = input;
            }
            fprintf(stderr, " 3 ");
            ret.error = inflate(&strm, Z_NO_FLUSH);       /* normal inflate */
            if (ret.error == Z_NEED_DICT)
                ret.error = Z_DATA_ERROR;
            if (ret.error == Z_MEM_ERROR || ret.error == Z_DATA_ERROR)
                goto extract_ret;
            fprintf(stderr, "RET = %d ;", ret.error);
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
                fprintf(stderr, "LEN = %ld, SKIP = %d ;", len, skip);
            }
            if (ret.error == Z_STREAM_END)
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
        if (ret.error == Z_STREAM_END)
            break;

        /* do until offset reached and requested data read, or stream ends */
    } while (skip);

    fprintf(stderr, "RET FINAL= %ld ;", ret.value);
    /* compute number of uncompressed bytes read after offset */
    ret.value = skip ? 0 : len - strm.avail_out;
    if (output_to_stdout == 1) {
        ret.value = initial_len - len;
    }
    if (extract_all_input == 1) {
        ret.value = len;
    }
    // TODO: extract len bytes with output_to_stdout, and also set len at the end: 20190710 DONE

    /* clean up and return bytes read or error */
  extract_ret:
    fprintf(stderr, "!!! RET = %d ;", ret.error);
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
    fwrite_endian(&(index->file_size), sizeof(index->file_size), 1, output_file);

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

    index = create_empty_index();

    fread(header, 1, GZIP_INDEX_HEADER_SIZE, input_file);
    if (*((uint64_t *)header) != 0 ||
        strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING, GZIP_INDEX_HEADER_SIZE/2) != 0) {
        //fprintf(stderr, "File is not a valid gzip index file: %s\n", &header[8]);
        //fprintf(stderr, "File is not a valid gzip index file: %d, %d, %d, %d\n", (int)header[0], (int)header[1], (int)header[2], (int)header[3]);
        fprintf(stderr, "File is not a valid gzip index file.\n");
        free_index(index);
        return NULL;
    }

    fread_endian(&(index->have), sizeof(index->have), 1, input_file);
    if (index->have <= 0) {
        fprintf(stderr, "Index file contains no indexes.\n");
        free_index(index);
        return NULL;
    }

    // index->size should be equal to index->have, but this isn't checked
    fread_endian(&(index->size), sizeof(index->size), 1, input_file);

    // create the list of points
    index->list = malloc(sizeof(struct point) * index->size);
    empty_index_list( index, 0, index->size );

    for (i = 0; i < index->size; i++) {
        here = &(index->list[i]);
        fread_endian(&(here->out),  sizeof(here->out),  1, input_file);
        fread_endian(&(here->in),   sizeof(here->in),   1, input_file);
        fread_endian(&(here->bits), sizeof(here->bits), 1, input_file);
        fread_endian(&(here->window_size), sizeof(here->window_size), 1, input_file);
        if ( load_windows == 0 ) {
            here->window = NULL;
            here->window_beginning = ftello(input_file);
            // and position into file as if read had occur
            fseeko(input_file, here->window_size, SEEK_CUR);
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
    fread_endian(&(index->file_size), sizeof(index->file_size), 1, input_file);


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


// based on def from examples/zpipe.c
/* Compress from file source to file dest until EOF on source.
   def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_STREAM_ERROR if an invalid compression
   level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
   version of the library linked do not match, or Z_ERRNO if there is
   an error reading or writing the files. */
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
        fprintf(stderr, "strm.avail_in = %d\n", strm.avail_in);
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
            fprintf(stderr, "strm.avail_out = %d (have=%d)\n", strm.avail_out, have);
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                goto compress_file_error;
            }
        } while (strm.avail_out == 0);
        fprintf(stderr, "have = %ld\n", have);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    free(in);
    free(out);
fprintf(stderr, "Z_OK (%d)\n", Z_OK);
    return Z_OK;

  compress_file_error:
    free(in);
    free(out);
    return Z_ERRNO;
}



/* Demonstrate the use of build_index() and extract() by processing the file
   provided on the command line, and the extracting 16K from about 2/3rds of
   the way through the uncompressed output, and writing that to stdout. */
int main(int argc, char **argv)
{


    // variables for used for the different actions:
    struct returned_output ret; // TODO: make uint64_t again: extract() SHOULD NOT return negative values, and neither build_index() PATCH!
    off_t offset;
    FILE *in;
    struct access *index = NULL;
    int bytes, position_in_buf;
    unsigned char *compressed_chunk = NULL;
    unsigned char *file_name = NULL;
    FILE *index_file = NULL;

    // variables for grabbing the options:
    uint64_t extract_from_byte = 0;
    unsigned char *index_filename = NULL;
    int continue_on_error = 0;
    int index_filename_indicated = 0;
    int force_overwriting = 0;

    enum EXIT_APP_VALUES { EXIT_OK = 0, EXIT_GENERIC_ERROR = 1, EXIT_INVALID_OPTION = 2 }
        ret_value;
    enum ACTION
        { ACT_NOT_SET, ACT_EXTRACT_FROM_BYTE, ACT_COMPRESS_CHUNK, ACT_DECOMPRESS_CHUNK,
          ACT_CREATE_INDEX, ACT_LIST_INFO, ACT_HELP }
        action;

    int opt = 0;
    int i, j;
    int actions_set = 0;

    action = ACT_NOT_SET;
    ret_value = EXIT_OK;
    while ((opt = getopt(argc, argv, "b:cdefhiI:l")) != -1)
        switch(opt) {
            // help
            case 'h':
                // TODO
                //print_help();
                return EXIT_OK;
            // `-b` extracts data from indicated position byte in uncompressed stream of <FILE>
            case 'b':
                // TODO: pass to strtoll() checking 0 and 0x
                extract_from_byte = atoll(optarg);
                // read from stdin and output to stdout
                action = ACT_EXTRACT_FROM_BYTE;
                actions_set++;
                // TODO
                break;
            // `-c` compress <FILE> (or stdin if none) to stdout
            case 'c':
                // TODO: check that compress_chunk() can be called in CHUNK sizes and decompress_chunk() inflates that
                action = ACT_COMPRESS_CHUNK;
                actions_set++;
                // TODO
                break;
            // `-d` decompress <FILE> (or stdin if none) to stdout
            case 'd':
                // TODO: check that decompress_chunk() can be called in CHUNK sizes and complete inflation is correct
                action = ACT_DECOMPRESS_CHUNK;
                actions_set++;
                // TODO
                break;
            // `-e` continues on error if multiple input files indicated
            case 'e':
                continue_on_error = 1;
                break;
            // do not overwrite files unless `-f` is indicated
            case 'f':
                force_overwriting = 1;
                break;
            // `-i` creates index for <FILE>
            case 'i':
                action = ACT_CREATE_INDEX;
                actions_set++;
                // TODO
                break;
            // list number of bytes, but not 0 valued
            // `-I` creates index for <FILE> with name <INDEX_FILE>
            case 'I':
                // TODO: do not overwrite files unless `-f` is indicated
                index_filename_indicated = 1;
                index_filename = malloc( strlen(optarg) + 1 );
                memcpy( index_filename, optarg, strlen(optarg) + 1 );
                // action = ACT_CREATE_INDEX but only if no other option indicated
                // TODO
                break;
            // `-l` list info of index <FILE>
            case 'l':
                action = ACT_LIST_INFO;
                actions_set++;
                // TODO
                break;
            case '?':
                if (isprint (optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                    // TODO
                    //print_help();
                } else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                return EXIT_INVALID_OPTION;
            default:
                abort ();
        }

    // Checking parameter merging and absence
    if ( actions_set > 1 ) {
        fprintf(stderr, "Please, do not merge parameters `-bcdil`.\nAborted.\n" );
        return EXIT_INVALID_OPTION;
    }
    if ( actions_set == 0 ) {
        // `-I <FILE>` is equivalent to `-i -I <FILE>`
        if ( action == ACT_NOT_SET && index_filename_indicated  == 1 ) {
            action = ACT_CREATE_INDEX;
            if ( (optind + 1) < argc ) {
                // too much files indicated to use `-I`
                fprintf(stderr, "`-I` is incompatible with multiple input files.\nAborted.\n" );
                return EXIT_INVALID_OPTION;
            }
        } else {
            fprintf(stderr, "Please, indicate one parameter of `-bcdil`.\nAborted.\n" );
            return EXIT_INVALID_OPTION;
        }
    }

    if (optind == argc || argc == 1) {
        // file input is stdin
        // TODO: actions
fprintf(stderr, " > > > > > Lack of file operand > > > > >\n");
        switch ( action ) {
            case ACT_EXTRACT_FROM_BYTE:
                fprintf( stderr, "Cannot use index file with STDIN input.\nAborted.\n" );
                return EXIT_INVALID_OPTION;

            case ACT_COMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != compress_file( stdin, stdout, Z_DEFAULT_COMPRESSION ) ) {
                    fprintf( stderr, "Error while compressing STDIN.\nAborted.\n" );
                    return EXIT_GENERIC_ERROR;
                }
                return EXIT_OK;

            // TODO ...
        }

    } else {

        for (i = optind; i < argc; i++) {

            file_name = argv[i];

            // if no index filename is set (`-I`), it is derived from each <FILE> parameter
            if ( 0 == index_filename_indicated ) {
                if ( NULL != index_filename ) {
                    free(index_filename);
                }
                index_filename = malloc( strlen(argv[i]) + 4 + 1 );
                sprintf(index_filename, "%s.gzi", argv[i]);
            }

            // free previous loop's resources
            if ( NULL != index ) {
                free_index( index );
            }
            if ( NULL != index_file ) {
                fclose( index_file );
            }

            if ( force_overwriting == 0 &&
                 action == ACT_CREATE_INDEX &&
                 access( index_filename, F_OK ) != -1 ) {
                // index file already exists
                fprintf( stderr, "Index file '%s' already exists.\n", index_filename );
                if ( continue_on_error == 1 ) {
                    continue;
                } else {
                    fprintf( stderr, "Aborted.\n" );
                    return EXIT_GENERIC_ERROR;
                }
            }

            // "-bil" options can accept multiple files
            switch ( action ) {
                case ACT_EXTRACT_FROM_BYTE:
                    // TODO: if index_filename doesn't exist action will not proceed, unless `-f`
                    // in which case the index is created, and then the data is extracted.
                    // TODO: for this, it will be useful to convert the code inside these cases to functions...
fprintf(stderr, "'%s', '%s', '%ld'\n", file_name, index_filename, extract_from_byte);
                    // open <FILE>:
                    in = fopen( file_name, "rb" );
                    if ( NULL == in ) {
                        fprintf( stderr, "Could not open '%s' for reading.\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    // open index file (filename derived from <FILE> unless indicated with `-I`)
                    index_file = fopen( index_filename, "rb" );
                    if ( NULL == in ) {
                        fprintf( stderr, "Could not open '%s' for reading.\n", index_filename );
                        ret_value = EXIT_GENERIC_ERROR;
                        fclose( in );
                        break;
                    }
                    // deserialize_index_from_file
                    index = deserialize_index_from_file( index_file, 0, index_filename );
                    if ( ! index ) {
                        fprintf( stderr, "Could not read index from file '%s'\n", index_filename );
                        ret_value = EXIT_GENERIC_ERROR;
                        fclose( in );
                        fclose( index_file );
                        break;
                    }
                    ret = extract( in, index, extract_from_byte, NULL, 0 );
                    if ( ret.error < 0 ) {
                        fprintf( stderr, "Data extraction failed: %s error\n",
                                ret.error == Z_MEM_ERROR ? "out of memory" : "input corrupted" );
                        ret_value = EXIT_GENERIC_ERROR;
                    } else {
                        fprintf( stderr, "Extracted %ld bytes from '%s' to stdout.\n", ret.value, file_name );
                    }
                    fclose( in );
                    fclose( index_file );
                    break;

                case ACT_COMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        fprintf( stderr, "Error while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    if ( Z_OK != compress_file( in, stdout, Z_DEFAULT_COMPRESSION ) ) {
                        fprintf( stderr, "Error while compressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    fclose( in );
                    break;

                case ACT_CREATE_INDEX:
                    // open <FILE>:
                    fprintf( stderr, "Processing '%s' ...\n", file_name );
                    in = fopen( file_name, "rb" );
                    if ( NULL == in ) {
                        fprintf( stderr, "Could not open %s for reading.\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    // compute index:
                    ret = build_index( in, SPAN, &index );
                    fclose(in);
                    if ( ret.error < 0 ) {
                       fclose( in );
                       switch ( ret.error ) {
                       case Z_MEM_ERROR:
                           fprintf( stderr, "ERROR: Out of memory.\n" );
                           break;
                       case Z_DATA_ERROR:
                           fprintf( stderr, "ERROR: Compressed data error in %s.\n", file_name );
                           break;
                       case Z_ERRNO:
                           fprintf( stderr, "ERROR: Read error on %s.\n", file_name );
                           break;
                       default:
                           fprintf( stderr, "ERROR: Error %d while building index.\n", ret.error );
                       }
                       ret_value = EXIT_GENERIC_ERROR;
                       break;
                    }
                    fprintf(stderr, "Built index with %ld access points.\n", ret.value);
                    // write index to index file:
                    index_file = fopen( index_filename, "wb" );
                    if ( NULL == index_file || 
                         ! serialize_index_to_file( index_file, index ) ) {
                        fprintf( stderr, "Could not write index to file '%s'.\n", index_filename );
                        if ( continue_on_error == 0 )
                            ret_value = EXIT_GENERIC_ERROR;
                    }
                    fclose( index_file );
                    break;

                case ACT_LIST_INFO:
                    // open index file:
                    fprintf( stderr, "Checking index file '%s' ...\n", file_name );
                    in = fopen( file_name, "rb" );
                    if ( NULL == in ) {
                        fprintf( stderr, "Could not open %s for reading.\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    index = deserialize_index_from_file( in, 0, file_name );
                    if ( ! index ) {
                        fprintf(stderr, "Could not read index from file '%s'.\n", file_name);
                        if ( continue_on_error == 0 )
                            ret_value = EXIT_GENERIC_ERROR;
                    } else {
                        fprintf( stderr, "\tNumber of index points:    %ld\n", index->have );
                        if (index->file_size != 0)
                            fprintf( stderr, "\tSize of uncompressed file: %ld\n", index->file_size );
                        fprintf( stderr, "\tList of points:\n\t" );
                        for (j=0; j<index->have; j++) {
                            fprintf( stderr, "@%ld (%dB), ", index->list[j].out, index->list[j].window_size );
                        }
                        fprintf( stderr, "\n" );
                    }
                    fclose( in );
                    break;

                // TODO: actions
                // ACTION( argv[i] );
            }

            if ( continue_on_error = 0 &&
                 ret_value != EXIT_OK ) {
                fprintf( stderr, "Aborted.\n" );
                // break the for loop
                break;
            }

        }

        // final freeing of resources
        if ( NULL != index ) {
            free_index( index );
        }
        if ( NULL != index_filename ) {
            free( index_filename );
        }        if ( NULL != index_file ) {
            fclose( index_file );
        }

        return ret_value;

    }

}
