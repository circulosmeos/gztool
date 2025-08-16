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
// v0.1 to v1.8* by Roberto S. Galende, 2019, 2020, 2021, 2022, 2023, 2024
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
       that block is saved to provide a reference for locating a desired starting
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

//#define COMPILING_DEBIAN_PACKAGE
#ifdef COMPILING_DEBIAN_PACKAGE
    #include <config.h>
#else
    #define PACKAGE_NAME "gztool"
    #define PACKAGE_VERSION "1.8.0"
#endif

#define _XOPEN_SOURCE 500 // expose <unistd.h>'s pread()
                          // and <stdio.h>'s fileno()

#include <stdint.h> // uint32_t, uint64_t, UINT32_MAX
#include <stdio.h>
#include <stdarg.h> // va_start, va_list, va_end
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include <unistd.h> // getopt(), access(), sleep(), pread()
#include <getopt.h> // getopt(): optarg, optopt, optind
#include <sys/stat.h> // stat()
#include <stdbool.h>// bool, false, true

#include "gztool.h"
#include "gztool_internal.h"

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

// Deallocate from memory
// an index built by decompress_and_build_index()
// INPUT:
// struct access *index: pointer to index
void free_index(struct access *index)
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
void empty_index_list(struct access *index, uint64_t from, uint64_t to)
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
struct access *create_empty_index()
{
    struct access *index;
    if ( NULL == ( index = malloc(sizeof(struct access)) ) ) {
        return NULL;
    }
    index->index_version = 0;
    index->line_number_format = 0;
    index->uncompressed_file_size = 0;
    index->number_of_lines = 0;
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
// uint64_t in          : (new entry data)
// uint64_t out         : (new entry data)
// unsigned left        : (new entry data)
// unsigned char *window: window data, already compressed with size window_size,
//                        or uncompressed with size WINSIZE,
//                        or store an empty window (NULL) because it resides on file.
// uint32_t window_size : size of passed *window (may be already compressed o not)
// uint64_t line_number : actual line number if index v1, 0 otherwise
// int compress_window  : 0: store window of size window_size, as it is, in point structure
//                        1: compress passed window of size window_size
// OUTPUT:
// pointer to (new) index (NULL on error)
struct access *add_point( struct access *index, uint32_t bits,
    uint64_t in, uint64_t out, unsigned left, unsigned char *window, uint32_t window_size,
    uint64_t line_number, int compress_window )
{
    struct point *next;
    uint64_t size = window_size;
    unsigned char *compressed_chunk;

    /* if list is empty, create it (starts with eight points) */
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
    next->line_number = line_number;

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
                print_to_stderr( VERBOSITY_NORMAL, "Error whilst compressing index chunk\nProcess aborted\n." );
                return NULL;
            }
            free(next->window);
            next->window = compressed_chunk;
            /* uint64_t size and uint32_t window_size, but windows are small, so this will always fit */
            next->window_size = size;
        }
        print_to_stderr( VERBOSITY_EXCESSIVE, "\t[%llu/%llu] window_size = %d\n", index->have+1, index->size, next->window_size);
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

    /* proceed only if there's something reasonable to do */
    if (NULL == output_file || NULL == index) {
        return 0;
    }

    /*if (index->have <= 0)
        return 0;*/
    /* writing and empty index is allowed: writes the header (of size 4*8 = 32 bytes) */

    if ( index_last_written_point == 0 ) {

        /* write header */
        fseeko( output_file, 0, SEEK_SET);
        /* 0x0 8 bytes (to be compatible with .gzi for bgzip format: */
        /* the initial uint32_t is the number of bgzip-idx registers) */
        temp = 0;
        fwrite_endian(&temp, sizeof(temp), output_file);
        /* a 64 bits readable identifier */
        if ( index->index_version == 0 )
            fprintf(output_file, GZIP_INDEX_IDENTIFIER_STRING);
        else {
            fprintf(output_file, GZIP_INDEX_IDENTIFIER_STRING_V1);
            // there's an additional register on v1: line_number_format
            fwrite_endian(&(index->line_number_format), sizeof(index->line_number_format), output_file);
        }

        /* and now the raw data: the access struct "index" */

        // as this may be a growing index
        // values will be filled with special, not actual, values:
        // 0x0..0 , 0xf..f
        // Last write operation will overwrite these with correct values, that is, when
        // serialize_index_to_file() be called with index_last_written_point = index->have
        temp = 0;
        fwrite_endian(&temp, sizeof(temp), output_file); // have
        temp = UINT64_MAX;
        fwrite_endian(&temp, sizeof(temp), output_file); // size

    }

    offset = GZIP_INDEX_HEADER_SIZE + sizeof(temp) +
            ( (index->index_version == 1)? sizeof(index->line_number_format): 0 );

    // update index->size on disk with index->have data
    // ( if index->have == 0, no index points still, maintain UINT64_MAX )
    if ( index->have > 0 ) {
        // seek to index->have position
        fseeko( output_file, offset, SEEK_SET );
        // write index->have value; (when the index be closed, index->size on disk will be >0)
        fwrite_endian( &(index->have), sizeof(index->have), output_file );
    }

    // fseek to index position of index_last_written_point
    offset += sizeof(temp);
    for (i = 0; i < index_last_written_point; i++) {
        here = &(index->list[i]);
        offset += sizeof(here->out) + sizeof(here->in) +
                  sizeof(here->bits) + sizeof(here->window_size) +
                  ((here->window_size==UNCOMPRESSED_WINDOW)? WINSIZE: (here->window_size)) +
                  ( (index->index_version == 1)? sizeof(here->line_number): 0 );
    }
    fseeko( output_file, offset, SEEK_SET);
    print_to_stderr( VERBOSITY_MANIAC, "index_last_written_point = %llu\n", index_last_written_point );
    if (NULL!=here) {
        print_to_stderr( VERBOSITY_MANIAC, "%llu->window_size = %d\n", i, here->window_size );
    }
    print_to_stderr( VERBOSITY_MANIAC, "have = %llu, offset = %llu\n", index->have, offset );

    if ( index_last_written_point != index->have ) {
        for (i = index_last_written_point; i < index->have; i++) {
            here = &(index->list[i]);
            print_to_stderr( VERBOSITY_EXCESSIVE, "writing new point #%llu (%llu, %llu)\n", i+1, here->in, here->out );
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
                print_to_stderr( VERBOSITY_NORMAL, "Index incomplete! - index writing aborted.\n" );
                return 0;
            } else {
                if ( here->window_size > 0 )
                    fwrite(here->window, here->window_size, 1, output_file);
            }
            // once written, point's window CAN (and will now) BE DELETED from memory
            free(here->window);
            here->window = NULL;
            if ( index->index_version == 1 ) {
                // there's an additional register on v1: line_number
                fwrite_endian(&(here->line_number), sizeof(here->line_number), output_file);
            }
        }
    } else {
        // Last write operation:
        // tail must be written:
        /* write size of uncompressed file (useful for bgzip files) */
        fwrite_endian(&(index->uncompressed_file_size), sizeof(index->uncompressed_file_size), output_file);
        if ( index->index_version == 1 ) {
            // there's an additional register on v1: number_of_lines
            fwrite_endian(&(index->number_of_lines), sizeof(index->number_of_lines), output_file);
        }
        // and header must be updated:
        // for this, move position to header:
        fseeko( output_file,
            GZIP_INDEX_HEADER_SIZE + ( (1 == index->index_version)? sizeof(index->line_number_format): 0 ),
            SEEK_SET );
        fwrite_endian(&index->have, sizeof(index->have), output_file);
        /* index->size is not written as only filled entries are usable */
        fwrite_endian(&index->have, sizeof(index->have), output_file);
        index->index_complete = 1; /* index is now complete */
    }

    // flush written content to disk
    fflush( output_file );

    return 1;
}


// read index from a file
// INPUT:
// FILE *input_file         : input stream
// int load_windows         : 0 do not yet load windows on memory; 1 to load them
// char *file_name : file path to index file, needed in case
//                            load_windows==0, so a later extract() can access the
//                            index file again to read the window data.
//                            Can be "" if using stdin, but not NULL.
// int extend_index_with_lines: if >0 (index v1) but index is v0, operation cannot be performed
//                              unless:
//                              * the index is an **empty file**, in which case the index
//                                pointer is correctly initialized with corresponding ->index_version
//                              * extend_index_with_lines == 3, in which case extend_index_with_lines
//                                is implicit, not compulsory, so process can transparently proceed
// OUTPUT:
// struct access *index : pointer to index, or NULL on error
struct access *deserialize_index_from_file(
    FILE *input_file, int load_windows, char *file_name,
    int extend_index_with_lines
) {
    struct point here;
    struct access *index = NULL;
    uint32_t index_complete = 1;
    uint64_t number_of_index_points = 0;
    uint64_t index_have, index_size, file_size;
    uint64_t position_at_file = 0;
    char header[GZIP_INDEX_HEADER_SIZE];
    struct stat st;
    int index_version = 0;           /* 0: default; 1: index with line numbers */

    // get index file size to calculate on-the-fly how many registers are
    // in order to being able to read still-growing index files (see `-S`)
    if ( strlen(file_name) > 0 ) {
        stat(file_name, &st);
        file_size = st.st_size;
    } else {
        // for stdin use max value
        file_size = UINT64_MAX;
    }

    // check index size == 0
    if ( file_size != 0 ) {
        if (fread(header, 1, GZIP_INDEX_HEADER_SIZE, input_file) == GZIP_INDEX_HEADER_SIZE &&
            *((uint64_t *)header) == 0 ) {
            if ( strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING, GZIP_INDEX_HEADER_SIZE/2) != 0 ) {
                if ( strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING_V1, GZIP_INDEX_HEADER_SIZE/2) != 0 ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: File is not a valid gzip index file.\n" );
                    return NULL;
                } else {
                    index_version = 1;
                }
            } else {
                index_version = 0;
            }
        } else {
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: File is not a valid gzip index file.\n" );
            return NULL;
        }
    } else {
        // for an empty index, return a pointer with zero data
        index = create_empty_index();
        if ( extend_index_with_lines > 0 ) {
            index->index_version = 1;
            if ( extend_index_with_lines == 2 )
                index->line_number_format = 1;
            else
                index->line_number_format = 0; // extend_index_with_lines = 1 || 3
        }
        return index;
    }

    if ( 0 == index_version &&
         extend_index_with_lines > 0 ) {
        if ( 3 != extend_index_with_lines ) {
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: Existing index has no line number information.\n" );
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: Aborting on `-[LRxX]` parameter(s).\n" );
            goto deserialize_index_from_file_error;
        } else {
            // transparently handle v0 files, as `-x` was implicitly tried (v>1.4.2), but it is not compulsory
            extend_index_with_lines = 0; // this value is not used - set here only for clarity
            print_to_stderr( VERBOSITY_EXCESSIVE, "implicit `-x`: 0 (deserialize_index_from_file).\n" );
        }
    }

    index = create_empty_index();
    index->index_version = index_version;

    // if index is v1, there's a previous register with line separator format info:
    if ( index_version == 1 ) {
        fread_endian(&(index->line_number_format), sizeof(index->line_number_format), input_file);
    }

    fread_endian(&(index_have), sizeof(index_have), input_file);
    fread_endian(&(index_size), sizeof(index_size), input_file);

    print_to_stderr( VERBOSITY_MANIAC, "Number of index points declared: %llu - %llu\n", index_have, index_size );

    number_of_index_points = index_have;

    // index->size equals index->have when the index file is correctly closed,
    // and index->have on disk == 0 && index->have on disk = index->size whilst the index is growing:
    if ( index_have != index_size ) {
        print_to_stderr( VERBOSITY_NORMAL, "Index file is incomplete.\n" );
        index_complete = 0;
        number_of_index_points = index_size;
    }

    print_to_stderr( VERBOSITY_EXCESSIVE, "Number of index points declared: %llu\n", number_of_index_points );

    position_at_file = GZIP_INDEX_HEADER_SIZE +
        ( (1 == index->index_version)? sizeof(index->line_number_format): 0 ) +
        sizeof(index_have)*2;

    // read the list of points
    do {

        fread_endian(&(here.out),  sizeof(here.out),  input_file);
        fread_endian(&(here.in),   sizeof(here.in),   input_file);
        fread_endian(&(here.bits), sizeof(here.bits), input_file);
        fread_endian(&(here.window_size), sizeof(here.window_size), input_file);
        position_at_file += sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) + sizeof(here.window_size);
        print_to_stderr( VERBOSITY_MANIAC, "#%llu: READ window_size=%d",
            ((NULL==index)? 1: index->have +1), here.window_size );

        if ( here.bits > 8 )
        {
            print_to_stderr( VERBOSITY_MANIAC, "\t(%p, %d, %llu, %llu, %d, %llu)\n", index, here.bits, here.in, here.out, here.window_size, file_size );
            print_to_stderr( VERBOSITY_EXCESSIVE, "Unexpected data found in index file '%s' @%llu.\nIgnoring data subsequent to point %llu.\n",
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
                    print_to_stderr( VERBOSITY_NORMAL, "Not enough memory to load index from STDIN.\n" );
                    goto deserialize_index_from_file_error;
                }
                while ( pos < position ) {
                    if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), input_file) ) {
                        print_to_stderr( VERBOSITY_NORMAL, "Could not read index from STDIN.\n" );
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
                    print_to_stderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    goto deserialize_index_from_file_error;
                }
                if ( !fread(here.window, here.window_size, 1, input_file) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Error while reading index file.\n" );
                    goto deserialize_index_from_file_error;
                }
            } else {
                here.window = NULL;
            }
        }
        position_at_file += here.window_size;

        if ( index_version == 1 ) {
            fread_endian(&(here.line_number), sizeof(here.line_number), input_file);
            position_at_file += sizeof(here.line_number);
        } else {
            here.line_number = 0;
        }

        print_to_stderr( VERBOSITY_MANIAC, "(%p, %d, %llu, %llu, %d, %llu, %llu)\n",
            index, here.bits, here.in, here.out, here.window_size, here.window_beginning, here.line_number );
        // increase index structure with a new point
        // (here.window can be NULL if load_windows==0)
        index = add_point( index, here.bits, here.in, here.out, 0, here.window, here.window_size, here.line_number, 0 );

        // after increasing index, copy values which were not passed to add_point():
        index->list[index->have - 1].window_beginning = here.window_beginning;
        // note that even if (here.window != NULL) it MUST NOT be free() here, because
        // the pointer has been copied in a point of the index structure.

        print_to_stderr( VERBOSITY_NUTS, "{%llu>=%llu && %llu>0}", ( file_size - position_at_file ),
            ( sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) +
              sizeof(here.window_size) ), number_of_index_points -1 );

    } while (
        ( file_size - position_at_file ) >=
        // at least an empty window must enter, otherwise end loop:
        ( sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) +
          sizeof(here.window_size) )
        &&
        --number_of_index_points > 0
        );


    index->uncompressed_file_size = 0;

    if ( index_complete == 1 ){
        /* read size of uncompressed file (useful for bgzip files) */
        /* this field may not exist (maybe useful for growing gzip files?) */
        fread_endian(&(index->uncompressed_file_size), sizeof(index->uncompressed_file_size), input_file);
        /* read number of lines in uncompressed file */
        /* this field does not exist with v0 of index file */
        fread_endian(&(index->number_of_lines), sizeof(index->number_of_lines), input_file);
    }

    index->file_name = malloc( strlen(file_name) + 1 );
    if ( NULL == index->file_name ||
         NULL == memcpy( index->file_name, file_name, strlen(file_name) + 1 ) ) {
        print_to_stderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
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


/* Basic checks of existing index file:
   - Checks that last index point ->in isn't greater than gzip file size
*/
// INPUT:
// struct access *index         : pointer to index. Can be NULL => no check.
// char *file_name     : gzip file name. Can be NULL or "" => no check.
// char *index_filename: index file name. Must be != NULL, but can be "". Only used to print warning.
// OUTPUT:
// 0 on error, 1 on success
int check_index_file( struct access *index, char *file_name, char *index_filename ) {

    if ( NULL != file_name &&
         strlen( file_name ) > 0 ) {
        if ( NULL != index &&
             index->have > 0 ) {
            // size of input file
            struct stat st;
            stat( file_name, &st );
            print_to_stderr( VERBOSITY_EXCESSIVE, "(%llu >= %llu)\n", st.st_size, ( index->list[index->have - 1].in ) );
            if ( index->have > 1 &&
                (uint64_t) st.st_size < ( index->list[index->have - 1].in )
                ) {
                print_to_stderr( VERBOSITY_NORMAL, "WARNING: Index file '%s' corresponds to a file bigger than '%s'\n",
                    index_filename, file_name );
                return 0;
            }
        }
    }

    return 1;

}


// Compress source stream using zlib format.
// Used internally to compress the index' windows.
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
unsigned char *compress_chunk(unsigned char *source, uint64_t *size, int level)
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


// Decompress a source stream with zlib format.
// Used internally to decompress the index' windows.
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
unsigned char *decompress_chunk(unsigned char *source, uint64_t *size)
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
                ret = Z_DATA_ERROR;
                (void)inflateEnd(&strm);
                goto decompress_chunk_error;
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
        print_to_stderr( VERBOSITY_NORMAL, "Decompression of index' chunk terminated with error (%d).\n", ret);
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


// Creates an index for a gzip file.
// If index file already exists, it is completed if it weren't complete,
// or directly used if it is complete.
// INPUT:
// char *file_name     : file name of gzip file for which index will be calculated,
//                                or that will be decompressed, depending on indx_n_extraction_opts.
//                                If strlen(file_name) == 0 stdin is used as gzip file.
// struct access **index        : memory address of index pointer (passed by reference)
// char *index_filename: file name where index will be written
//                                If strlen(index_filename) == 0 stdout is used as output for index.
// enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts:
//                                  value passed to decompress_and_build_index();
//                                  in case of SUPERVISE_DO* (not *DONT), wait here until gzip file exists.
// uint64_t offset                 : if supervise == EXTRACT_FROM_BYTE, this is the offset byte in
//                                the uncompressed stream from which to extract to stdout.
//                                0 otherwise.
// uint64_t line_number_offset  : if indx_n_extraction_opts == EXTRACT_FROM_LINE, this is the offset line
//                                in the uncompressed stream from which to extract to stdout.
//                                0 otherwise.
// uint64_t span_between_points    : span between index points in bytes
// int write_index_to_disk      : 1: will write/update the index as usual;
//                                0: will just read, but do not write nor update (overwrite) it
// int end_on_first_proper_gzip_eof: end file processing on first proper (at feof()) GZIP EOF
//                                   (to be used when the file contains surely only one gzip stream)
// int always_create_a_complete_index : create a 'complete' index file even in case of decompressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time             : waiting time in seconds between reads when `-[ST]`
// int force_action             : with `-[cd]`, force destination file overwriting if it exists
// int wait_for_file_creation   : 0: exit with error if source file doesn't exist
//                                1: wait for file creation if it doesn't yet exist, when using `-[STcd]`
// int extend_index_with_lines  : create index with (1,2) or without (0) line numbers (v1 or v0 index):
//                                decision is make at de/compress_and_build_index(), not here, unless
//                                index file already exists and extend_index_with_lines == 3, in which
//                                case if index is v0 => extend_index_with_lines = 0 and if index is
//                                v1 => extend_index_with_lines = 1, to transparently handle v0 indexes
//                                with implicit `-x` (v>1.4.2).
// uint64_t expected_first_byte  : indicates that the first byte on compressed input is this, not 1,
//                                and so truncated compressed inputs can be used. Only can be used (>1) if
//                                indx_n_extraction_opts is EXTRACT_FROM_BYTE or EXTRACT_FROM_LINE.
//                                Directly passed here to decompress_and_build_index(), w/o further processing.
// int gzip_stream_may_be_damaged: indicates if a decompression in advance will be used and how many CHUNKs
//                                will be decompressed in advance then (CHUNKS_TO_DECOMPRESS_IN_ADVANCE).
//                                If 0, no decompression in advance is made.
// bool lazy_gzip_stream_patching_at_eof: when used with `-[ST]` implies that checking
//                                for errors in stream is made as quick as possible as the gzip file
//                                grows, not respecting then at EOF the CHUNKS_TO_DECOMPRESS_IN_ADVANCE value.
// uint64_t range_number_of_bytes: indicates how many bytes to extract when using `-[bL]` (offset, line_number_offset)
// uint64_t range_number_of_lines: indicates how many lines to extract when using `-[bL]` (offset, line_number_offset)
// bool adjust_index_points_to_byte_boundary: adjust index points to a byte boundary, so no previous byte (bits) would be needed.
//                                This will result in index points spaced by more than "span_between_points" bytes between then,
//                                and so, may be, less points in the index. But this is completely safe and sound.
// int compression_factor        : a value from 1 to 9 (Z_BEST_SPEED to Z_BEST_COMPRESSION),
//                                 defaulting to Z_DEFAULT_COMPRESSION which though it's -1, means 6 internally for zlib.
//                                 Used here only when calling compress_and_build_index().
// bool continue_tailing_on_eof: indicates that `-[bL]` options are processed and tailing and supervising
//                                will continue once end-of-file is reached.
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
int action_create_index(
    char *file_name, struct access **index,
    char *index_filename, enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts,
    uint64_t offset, uint64_t line_number_offset, uint64_t span_between_points, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time, int force_action, int wait_for_file_creation,
    int extend_index_with_lines, uint64_t expected_first_byte, int gzip_stream_may_be_damaged,
    bool lazy_gzip_stream_patching_at_eof,
    uint64_t range_number_of_bytes, uint64_t range_number_of_lines,
    bool adjust_index_points_to_byte_boundary,
    int compression_factor, bool continue_tailing_on_eof )
{

    FILE *file_in  = NULL;
    FILE *file_out = NULL;
    struct returned_output ret;
    uint64_t number_of_index_points = 0;
    int waiting = 0;

    // First of all, check that data output and index output do not collide:
    if ( strlen(file_name) == 0 &&
         strlen(index_filename) == 0 &&
         ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
           indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
           indx_n_extraction_opts == EXTRACT_FROM_LINE ||
           indx_n_extraction_opts == EXTRACT_TAIL )
        ) {
        // input is stdin, output is stdout, and no file name has been
        // indicated for index output, so action is not possible:
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Please, note that extracted data will be output to STDOUT\n" );
        print_to_stderr( VERBOSITY_NORMAL, "       so an index file name is needed (`-I`).\nAborted.\n" );
        return EXIT_GENERIC_ERROR;
    }

    // open <FILE>:
    if ( strlen( file_name ) > 0 ) {
action_create_index_wait_for_file_creation:
        file_in = fopen( file_name, "rb" );
        if ( NULL == file_in ) {
            if ( wait_for_file_creation == 1 &&
                 ( indx_n_extraction_opts == SUPERVISE_DO ||
                   indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
                   indx_n_extraction_opts == COMPRESS_AND_CREATE_INDEX ||
                   indx_n_extraction_opts == DECOMPRESS ) ) {
                if ( waiting == 0 ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Waiting for creation of file '%s'\n", file_name );
                    waiting = 1;
                }
                sleep( waiting_time );
                goto action_create_index_wait_for_file_creation;
            }
            print_to_stderr( VERBOSITY_NORMAL, "Could not open '%s' for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
        print_to_stderr( VERBOSITY_NORMAL, "Processing '%s' ...\n", file_name );
    } else {
        // stdin
        SET_BINARY_MODE(STDIN); // sets binary mode for stdin in Windows
        file_in = stdin;
        print_to_stderr( VERBOSITY_NORMAL, "Processing STDIN ...\n" );
    }

    if ( indx_n_extraction_opts == COMPRESS_AND_CREATE_INDEX ) {
        // compression !

        // an output filename is necessary:
        // this block directly provides de FILE *file_out pointer
        if ( strlen(file_name) > 0 ) {
            char *out_filename;
            out_filename = malloc( strlen(file_name) + 3 + 1 );
            sprintf( out_filename, "%s.gz", file_name );
            // check that destination file doesn't exist
            if ( access( out_filename, F_OK ) != -1 ) {
                if ( 0 == force_action ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Compress destination file '%s' already exist.\nAborted\n", out_filename );
                    free( out_filename );
                    return EXIT_GENERIC_ERROR;
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", out_filename );
                }
            }
            file_out = fopen( out_filename, "w+b" );
            if ( NULL == file_out ) {
                print_to_stderr( VERBOSITY_NORMAL, "Could not open '%s' for writing.\nAborted.\n", out_filename );
                free( out_filename );
                return EXIT_GENERIC_ERROR;
            }
            print_to_stderr( VERBOSITY_NORMAL, "Compressing to '%s'\n", out_filename );
            free( out_filename );
        } else {
            // stdin
            // If using STDIN, the output will be STDOUT
            SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
            file_out = stdout;
            print_to_stderr( VERBOSITY_NORMAL, "Compressing to STDOUT ...\n" );
        }

        if ( extend_index_with_lines == 3 ) {
            extend_index_with_lines = 1; // in case of COMPRESS_AND_CREATE_INDEX, direct v1 index
                                         // as no previous index is possible, so there's no doubt.
            print_to_stderr( VERBOSITY_EXCESSIVE, "implicit `-x`: 1 (action_create_index).\n" );
        }

        ret = compress_and_build_index( file_in, file_out, compression_factor,
                file_name, span_between_points, index, index_filename,
                write_index_to_disk,
                ((end_on_first_proper_gzip_eof==0)?1:0), // `-E` behaviour is inverted with `-c` for ease of use
                always_create_a_complete_index, waiting_time,
                extend_index_with_lines );

        if ( NULL != index && NULL != *index ) {
            // it is important to set (*index)->file_name, if the index is to be reused,
            // because the window data must be read from disk to be used.
            if ( NULL == (*index)->file_name ) {
                (*index)->file_name = malloc( strlen(index_filename) + 1 );
                if ( NULL == (*index)->file_name ||
                     NULL == memcpy( (*index)->file_name, index_filename, strlen(index_filename) + 1 ) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    if ( NULL != (*index)->file_name )
                        free( (*index)->file_name );
                    return EXIT_GENERIC_ERROR;
                }
            }
        }

    } else {
        // decompression!

        // compute index:
        // if index_filename already exist, load it and use it
        // (if it is complete, it'll be used directly, if not it'll be
        // completed from last point - all in decompress_and_build_index() ).
        if ( strlen( index_filename ) > 0 &&
             access( index_filename, F_OK ) != -1 &&
             indx_n_extraction_opts != DECOMPRESS // DECOMPRESS does not require index
            ) {
            if ( NULL == index || NULL == (*index) ) {
                // index_filename already exist: try to load it
                FILE *index_file;
                index_file = fopen( index_filename, "rb" );
                if ( NULL != index_file ) {
                    *index = deserialize_index_from_file( index_file, 0, index_filename, extend_index_with_lines );
                    fclose( index_file );
                    if ( NULL == *index ) {
                        print_to_stderr( VERBOSITY_NORMAL, "Could not load index from file '%s'.\nAborted.\n", index_filename );
                        return EXIT_GENERIC_ERROR;
                    }
                    // index ok, continue
                    number_of_index_points = (*index)->have;
                    (*index)->index_complete = 0; // every index can be updated with new points in case gzip data grows!
                    // set local "extend_index_with_lines" to the curated value returned by deserialize_index_from_file()
                    // (this way a possible extend_index_with_lines == 3 is reconverted to a usable value {0,1,2})
                    if ( 0 == (*index)->index_version ) {
                        extend_index_with_lines = 0;
                        print_to_stderr( VERBOSITY_EXCESSIVE, "implicit `-x`: 0  (action_create_index).\n" );
                    } else {
                        extend_index_with_lines = (*index)->line_number_format;
                        print_to_stderr( VERBOSITY_EXCESSIVE, "implicit `-x`: 1\\%d (action_create_index).\n", extend_index_with_lines );
                    }
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "Could not open '%s' for reading.\nAborted.\n", index_filename );
                    return EXIT_GENERIC_ERROR;
                }
            } else {
                number_of_index_points = (*index)->have;
                (*index)->index_complete = 0; // every index can be updated with new points in case gzip data grows!
            }
            // there's an index now, continue:
            // it is important to set (*index)->file_name, if the index is to be reused,
            // because the window data must be read from disk to be used.
            if ( NULL == (*index)->file_name ) {
                (*index)->file_name = malloc( strlen(index_filename) + 1 );
                if ( NULL == (*index)->file_name ||
                     NULL == memcpy( (*index)->file_name, index_filename, strlen(index_filename) + 1 ) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    if ( NULL != (*index)->file_name )
                        free( (*index)->file_name );
                    return EXIT_GENERIC_ERROR;
                }
            }

        }

        // checks on index read from file:
        if ( NULL != (*index) &&
             strlen( file_name ) > 0 ) {
            // (here, index_filename exists and index exists)
            check_index_file( (*index), file_name, index_filename );
            // returned value is not used - only warn user and continue
        }

        // stdout to binary mode if needed
        if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
             indx_n_extraction_opts == EXTRACT_FROM_LINE ||
             indx_n_extraction_opts == EXTRACT_TAIL ||
             indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL
             ) {
            SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        }

        if ( indx_n_extraction_opts == DECOMPRESS ) {
            // file must have ".gz" extension so it can be decompressed
            if ( strlen( file_name ) > 3 && // avoid out-of-bounds
                 0 == strcmp( ".gz",
                    (char *)(file_name + strlen(file_name) - 3) )
                ) {
                // if gzip-file name is 'FILE.gz', output file name will be 'FILE'
                char *output_filename = malloc( strlen(file_name) + 1 );
                sprintf(output_filename, "%s", file_name);
                output_filename[strlen(file_name) - 3] = '\0';
                print_to_stderr( VERBOSITY_NORMAL, "Decompressing to '%s'\n", output_filename );
                // now, if output_filename already exist, we cannot proceed...
                if ( access( output_filename, F_OK ) != -1 ) {
                    if ( force_action == 0 ) {
                        print_to_stderr( VERBOSITY_NORMAL, "Output file name '%s' already exists.\nAborted.\n", output_filename );
                        free ( output_filename );
                        return EXIT_GENERIC_ERROR;
                    }
                }
                // destination file will be overwritten:
                file_out = fopen( output_filename, "wb" );
                if ( NULL == file_out ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Could not open '%s' for writing.\nAborted.\n", output_filename );
                    free ( output_filename );
                    return EXIT_GENERIC_ERROR;
                }
                free( output_filename );
            } else {
                // otherwise decompression cannot proceed
                print_to_stderr( VERBOSITY_NORMAL, "File '%s' does not have '.gz' extension.\nAborted.\n", file_name );
                return EXIT_GENERIC_ERROR;
            }

            ret = decompress_and_build_index( file_in, file_out, file_name, span_between_points, index,
                    DECOMPRESS, offset, line_number_offset, index_filename, 0, // write_index_to_disk = 0
                    1 , always_create_a_complete_index, // end_on_first_proper_gzip_eof = 1
                    waiting_time, extend_index_with_lines, 1, // expected_first_byte not used, so 1
                    gzip_stream_may_be_damaged, lazy_gzip_stream_patching_at_eof,
                    range_number_of_bytes, range_number_of_lines,
                    adjust_index_points_to_byte_boundary, continue_tailing_on_eof );

        } else {
            file_out = stdout;

            ret = decompress_and_build_index( file_in, file_out, file_name, span_between_points, index,
                    indx_n_extraction_opts, offset, line_number_offset, index_filename, write_index_to_disk,
                    end_on_first_proper_gzip_eof, always_create_a_complete_index,
                    waiting_time, extend_index_with_lines, expected_first_byte,
                    gzip_stream_may_be_damaged, lazy_gzip_stream_patching_at_eof,
                    range_number_of_bytes, range_number_of_lines,
                    adjust_index_points_to_byte_boundary, continue_tailing_on_eof );
        }

        if ( NULL != index && NULL != *index ) {
            // it is important to set (*index)->file_name, if the index is to be reused,
            // because the window data must be read from disk to be used.
            if ( NULL == (*index)->file_name ) {
                (*index)->file_name = malloc( strlen(index_filename) + 1 );
                if ( NULL == (*index)->file_name ||
                     NULL == memcpy( (*index)->file_name, index_filename, strlen(index_filename) + 1 ) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    if ( NULL != (*index)->file_name )
                        free( (*index)->file_name );
                    return EXIT_GENERIC_ERROR;
                }
            }
        }

    }

    if ( file_in != stdin )
        fclose(file_in);

    if ( NULL != file_out &&
         file_out != stdout )
        fclose( file_out );

    if ( ret.error < 0 ) {
        switch ( ret.error ) {
            case Z_MEM_ERROR:
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: Out of memory.\n" );
                break;
            case Z_DATA_ERROR:
                if ( strlen(file_name) > 0 )
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Compressed data error in '%s'.\n", file_name );
                else
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Compressed data error in STDIN.\n" );
                break;
            case Z_ERRNO:
                if ( strlen(file_name) > 0 )
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Read error on '%s'.\n", file_name );
                else
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Read error on STDIN.\n" );
                break;
            default:
               print_to_stderr( VERBOSITY_NORMAL, "ERROR: Error %d while applying action.\n", ret.error );
       }
       return EXIT_GENERIC_ERROR;
    }

    if ( NULL != index && NULL != *index &&
         number_of_index_points != (*index)->have && write_index_to_disk == 1 ) {
        if ( number_of_index_points > 0 ) {
            print_to_stderr( VERBOSITY_NORMAL, "Updated index with %llu new access points.\n", ret.value - number_of_index_points);
            print_to_stderr( VERBOSITY_NORMAL, "Now index has %llu access points.\n", ret.value);
        } else {
            print_to_stderr( VERBOSITY_NORMAL, "Built index with %llu access points.\n", ret.value);
        }
    }

    if ( EXIT_FILE_OVERWRITTEN <= ret.error ) {
        return ret.error;
    }

    return EXIT_OK;

}


// Compress source stream to dest stream using zlib compression format
// that is, with 2-byte header and adler-32 crc, or raw format.
// based on def from zlib/examples/zpipe.c by Mark Adler
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// FILE *source  : source stream
// FILE *dest    : destination stream
// int level     : level of compression
// int raw_method: 0: zlib compression (header+Adler-32)
//                 1: raw compression (no header nor tail)
// OUTPUT:
// Z_* error code or Z_OK on success
int compress_file( FILE *source, FILE *dest, int level, int raw_method )
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
    if ( raw_method == 0 ) {
        ret = deflateInit(&strm, level);
    } else {
        ret = deflateInit2(
        &strm, level, Z_DEFLATED, -15,
        8, Z_DEFAULT_STRATEGY);
    }
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


// Decompress source stream to dest stream using zlib compression format
// that is, with 2-byte header and adler-32 crc, or raw format.
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
// int raw_method: 0: zlib decompression (header+Adler-32)
//                 1: raw decompression (no header nor tail)
// OUTPUT:
// Z_* error code or Z_OK on success
int decompress_file( FILE *source, FILE *dest, int raw_method )
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
    if ( raw_method == 0 ) {
        ret = inflateInit(&strm);
    } else {
        ret = inflateInit2(&strm, -15);
    }
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
                ret = Z_DATA_ERROR;
                (void)inflateEnd(&strm);
                goto decompress_file_error;
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
