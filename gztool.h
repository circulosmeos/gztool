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
#ifndef GZTOOL_H
#define GZTOOL_H

#include <stdint.h>
#include <stdio.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* access point entry */
struct point {
    uint64_t out;          /* corresponding offset in uncompressed data */
    uint64_t in;           /* offset in input file of first full byte */
    uint32_t bits;         /* number of bits (1-7) from byte at in - 1, or 0 */
    uint64_t window_beginning;/* offset at index file where this compressed window is stored */
    uint32_t window_size;  /* size of (compressed) window */
    unsigned char *window; /* preceding 32K of uncompressed data, compressed */
    // index v1:
    uint64_t line_number;  /* if index_version == 1 stores line number at this index point */
};
// NOTE: window_beginning is not stored on disk, it's an on-memory-only value

/* access point list */
struct access {
    uint64_t have;      /* number of list entries filled in */
    uint64_t size;      /* number of list entries allocated */
    uint64_t uncompressed_file_size; /* size of uncompressed file (useful for bgzip files) */
    struct point *list; /* allocated list */
    char *file_name;    /* path to index file */
    int index_complete; /* 1: index is complete; 0: index is (still) incomplete */
    // index v1:
    int index_version;  /* 0: default; 1: index with line numbers */
    uint32_t line_number_format; /* 0: linux \r | windows \n\r; 1: mac \n */
    uint64_t number_of_lines; /* number of lines (only used with v1 index format) */
};
// NOTE: file_name, index_complete and index_version are not stored on disk (on-memory-only values)

/* generic struct to return a function error code and a value */
struct returned_output {
    uint64_t value;
    int error;
};

/* Enum governing behavior of action_create_index() functions */
// FIXME: Refactor to remove the enum that modally modifies function's internal behavior
enum INDEX_AND_EXTRACTION_OPTIONS {
    JUST_CREATE_INDEX = 1, SUPERVISE_DO,
    SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, EXTRACT_FROM_BYTE,
    EXTRACT_TAIL, COMPRESS_AND_CREATE_INDEX, DECOMPRESS,
    EXTRACT_FROM_LINE };


/* Index management functions */
struct access *create_empty_index(void);
void free_index(struct access *index);
void empty_index_list(struct access *index, uint64_t from, uint64_t to);
struct access *add_point(struct access *index, uint32_t bits,
    uint64_t in, uint64_t out, unsigned left, unsigned char *window, uint32_t window_size,
    uint64_t line_number, int compress_window);

/* Index serialization */
int serialize_index_to_file(FILE *output_file, struct access *index, uint64_t index_last_written_point);
struct access *deserialize_index_from_file(FILE *input_file, int load_windows, char *file_name, int extend_index_with_lines);
int check_index_file(struct access *index, char *file_name, char *index_filename);

/* Window compression/decompression */
unsigned char *compress_chunk(unsigned char *source, uint64_t *size, int level);
unsigned char *decompress_chunk(unsigned char *source, uint64_t *size);

/* Create index */
// FIXME: Refactor to remove complexity of this very large function signature
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
    int compression_factor, bool continue_tailing_on_eof );
int compress_file( FILE *source, FILE *dest, int level, int raw_method );
int decompress_file( FILE *source, FILE *dest, int raw_method );


#ifdef __cplusplus
}
#endif

#endif /* GZTOOL_H */
