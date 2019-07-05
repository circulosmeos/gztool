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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"

#define local static

#define SPAN 1048576L       /* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */


// compression function, based on def() from examples/zpipe.c
/* Compress from file source to file dest until EOF on source.
   def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_STREAM_ERROR if an invalid compression
   level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
   version of the library linked do not match, or Z_ERRNO if there is
   an error reading or writing the files. */
unsigned char *compress_chunk(unsigned char *source, int *size, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    int i = 0;
    int output_size = 0;
    int input_size = *size;
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
        strm.avail_in = (i + CHUNK < input_size) ? CHUNK : (input_size - i);
        fprintf(stderr, "strm.avail_in = %d (i=%d) (input_size=%d)\n", strm.avail_in, i, input_size);
        if ( memcpy(in, source + i, strm.avail_in) == NULL ) {
            (void)deflateEnd(&strm);
            goto compress_chunk_error;
        }
        flush = (i + CHUNK >= input_size) ? Z_FINISH : Z_NO_FLUSH;
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
    if (ret != Z_STREAM_END) {
        fprintf(stderr, "Decompression of index' chunk terminated with error.\n");
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


/* Demonstrate the use of compress_chunk() and decompress_chunk() */
int main(int argc, char **argv)
{
    int len;
    off_t offset;
    FILE *in;
    unsigned char *buf = malloc(CHUNK*10);
    unsigned char *output;
    //unsigned char *input;
    int i, size;
    // output:
    FILE *gzip_file;
    int MAX_PATH_LENGTH = 265;
    char output_file[MAX_PATH_LENGTH];

    // initialize
    size = CHUNK*9.31;
    for(i=0; i<size; i++) {
        buf[i] = rand() % 255;
    }
    fprintf(stderr, "chunk size = %d\n\n", CHUNK);

    // compress
    fprintf(stderr, "compressing ...\n");
    output = compress_chunk(buf, &size, Z_DEFAULT_COMPRESSION);
    fprintf(stderr, "compressed size = %d (%p)\n", size, output);
    gzip_file = fopen( "output_file.gz", "wb" );
    if (gzip_file == NULL) {
        fprintf(stderr, "compress_test: could not open %s for writing\n", output_file);
        return 1;
    }
    /* write compressed data to file */
    if ( ! fwrite(output, size, 1, gzip_file) ) {
        fprintf(stderr, "compress_test: could not write data to file %s\n", output_file);
        return 1;
    }
    fclose(gzip_file);
    free(buf);
    free(output);

    // .................................................

    gzip_file = fopen( "output_file.gz", "rb" );
    buf = malloc(size);
    fread(buf, size, 1, gzip_file);
    fclose(gzip_file);

    // decompress
    fprintf(stderr, "decompressing ...\n");
    output = decompress_chunk(buf, &size);
    fprintf(stderr, "decompressed size = %d (%p)\n", size, output);
    gzip_file = fopen( "output_file", "wb" );
    if (gzip_file == NULL) {
        fprintf(stderr, "compress_test: could not open %s for writing\n", output_file);
        return 1;
    }
    /* write compressed data to file */
    if ( ! fwrite(output, size, 1, gzip_file) ) {
        fprintf(stderr, "compress_test: could not write data to file %s\n", output_file);
        return 1;
    }
    fclose(gzip_file);

    // .................................................

    //free(input);
    free(output);
    free(buf);

    return 0;
}
