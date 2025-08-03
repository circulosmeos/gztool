/*
 * gztool - shared header for gzip stream indexing and random access
 *
 * Based on parts from zlib/examples/zran.c & zlib/examples/zpipe.c by Mark Adler
 * and gztool by Roberto S. Galende
 *
 * Contains common data structures and functions for gzip stream processing
 * that are shared between gztool.c and nbdkit indexed-gzip filter.
 */

#ifndef GZTOOL_PRIVATE_H
#define GZTOOL_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <zlib.h>

#include <gztool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define local static

/* Constants */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */
#define UNCOMPRESSED_WINDOW UINT32_MAX // window is an uncompressed WINSIZE size window
#define GZIP_INDEX_IDENTIFIER_STRING    "gzipindx"  // default index version (v0)
#define GZIP_INDEX_IDENTIFIER_STRING_V1 "gzipindX"  // index version with line number info
#define GZIP_INDEX_HEADER_SIZE   16  // header size in bytes of gztool's .gzi files
#define GZIP_HEADER_SIZE_BY_ZLIB 10  // header size in bytes of gzip files created by zlib:
        //github.com/madler/zlib/blob/master/zlib.h
        //     If deflateSetHeader is not used, the default gzip header has text false,
        //   the time set to zero, and os set to 255, with no extra, name, or comment
        //   fields.
// default waiting time in seconds when supervising a growing gzip file:
#define WAITING_TIME 4
// how many CHUNKs will be decompressed in advance if it is needed (parameter gzip_stream_may_be_damaged, `-p`)
#define CHUNKS_TO_DECOMPRESS_IN_ADVANCE 3
// how many CHUNKs will be look to backwards for a new good gzip reentry point after an error is found (with `-p`)
#define CHUNKS_TO_LOOK_BACKWARDS 3

enum EXIT_RETURNED_VALUES {
    // used for app exit values:
    EXIT_OK = 0,
    EXIT_GENERIC_ERROR = 1,
    EXIT_INVALID_OPTION = 2,

    // used with returned_output.error, not in app exit values:
    // +100 not to crush with Z_* values (zlib): //www.zlib.net/manual.html
    // (and also not to crush with GZIP_MARK_FOUND_TYPE values... though
    // they are used only for decompress_in_advance()'s ret.error value)
    EXIT_FILE_OVERWRITTEN = 100,
    };


enum ACTION
{ ACT_NOT_SET, ACT_EXTRACT_FROM_BYTE, ACT_COMPRESS_CHUNK, ACT_DECOMPRESS_CHUNK,
  ACT_CREATE_INDEX, ACT_LIST_INFO, ACT_HELP, ACT_SUPERVISE, ACT_EXTRACT_TAIL,
  ACT_EXTRACT_TAIL_AND_CONTINUE, ACT_COMPRESS_AND_CREATE_INDEX, ACT_DECOMPRESS,
  ACT_EXTRACT_FROM_LINE };

enum VERBOSITY_LEVEL { VERBOSITY_NONE = 0, VERBOSITY_NORMAL = 1, VERBOSITY_EXCESSIVE = 2,
                       VERBOSITY_MANIAC = 3, VERBOSITY_CRAZY = 4, VERBOSITY_NUTS = 5 };

size_t fread_endian( void * ptr, size_t size, FILE * stream );
size_t fwrite_endian( void * ptr, size_t size, FILE * stream );

void print_to_stderr ( enum VERBOSITY_LEVEL verbosity, const char * format, ... );

// Always %.2f : "1234.67 Bytes\0" = 14 chars maximum
#define MAX_GIVEMESIUNITS_RETURNED_LENGTH 14
char *give_me_si_units ( uint64_t input_number, int binary );

// Creates index for a gzip stream (file or STDIN);
// action_create_index() always calls this function with an incomplete index file
// if it already exists.
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
// FILE *file_in            : input stream
// FILE *file_out           : output stream
// char *file_name : name of the input file associated with FILE *in.
//                            Can be "" (no file name: stdin used), but not NULL.
//                            Used only if there's no usable index && input (FILE *in)
//                            is associated with a file (not stdin) &&
//                            indx_n_extraction_opts == *_TAIL, for the use of the file
//                            size as approximation of the size of the tail to be output.
// uint64_t span            : span
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
//                      = EXTRACT_FROM_BYTE: extract from indicated offset byte, to stdout
//                      = EXTRACT_FROM_LINE: extract from indicated line, to stdout
// uint64_t offset      : if indx_n_extraction_opts == EXTRACT_FROM_BYTE, this is the offset byte
//                        in the uncompressed stream from which to extract to stdout.
//                        0 otherwise.
// uint64_t line_number_offset : if indx_n_extraction_opts == EXTRACT_FROM_LINE, this is the offset line
//                               in the uncompressed stream from which to extract to stdout.
//                               0 otherwise.
// char *index_filename   : index will be read/written on-the-fly
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
// int waiting_time             : waiting time in seconds between reads when `-[ST]` (always >0)
// int extend_index_with_lines  : 0: create index without line numbers (v0 index)
//                                1: create index WITH line numbers (v1 index) using Unix format (\n)
//                                2: create index WITH line numbers (v1 index) using old Mac format (\r) (compatible with Windows \n\r)
//                                Nonetheless, note that if index previously exists and is
//                                index->index_version == 0 it cannot be extended to v1, and
//                                if it is index->index_version == 1, it cannot be made v0,
//                                so extend_index_with_lines IS RESPECTED ONLY IF INDEX DOES NOT EXIST YET.
// uint64_t expected_first_byte  : indicates that the first byte on compressed input is this, not 1,
//                                and so truncated compressed inputs can be used. Only can be used (>1) if
//                                indx_n_extraction_opts is EXTRACT_FROM_BYTE or EXTRACT_FROM_LINE.
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
// bool continue_tailing_on_eof: indicates that `-[bL]` options are processed and tailing and supervising
//                                will continue once end-of-file is reached.
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of built index (index->have)
struct returned_output decompress_and_build_index(
    FILE *file_in, FILE *file_out, char *file_name, uint64_t span, struct access **built,
    enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts, uint64_t offset, uint64_t line_number_offset,
    char *index_filename, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time, int extend_index_with_lines, uint64_t expected_first_byte,
    int gzip_stream_may_be_damaged, bool lazy_gzip_stream_patching_at_eof,
    uint64_t range_number_of_bytes, uint64_t range_number_of_lines,
    bool adjust_index_points_to_byte_boundary, bool continue_tailing_on_eof );

// GZIP compress the file_in stream to file_out
// creating a .gzi index to index_filename file name,
// and returning it on *built.
// Note that the index will be of minimum size as this function
// creates Z_FULL_FLUSH marks on compression stream every span bytes,
// so the window size will be always 0 bytes. This produces indexes
// almost as small as with bgzip compression, with better compression factor.
// Compression based on def() from zlib/examples/zpipe.c by Mark Adler
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// FILE *file_in : source stream
// FILE *file_out   : destination stream
// int level    : level of compression
// char *file_name : name of the input file associated with FILE *in.
//                            Can be "" (no file name: stdin used), but not NULL.
//                            Used only if input (FILE *in)
//                            is associated with a file (not stdin) to detect a
//                            possible overwriting of the source file (and then stop).
// uint64_t span    : span
// struct access **built: address of index pointer, equivalent to passed by reference.
//                        Note that *built == NULL on call ALWAYS, because index is
//                        constructed from scratch here.
// char *index_filename  : index will be read/written on-the-fly
//                                  to this index file name.
// int write_index_to_disk  : 1: will write/update the index as usual;
//                            0: do not write the index on disk,
//                           and also, do not create/update the index in memory (**built and returned_output.value):
//                           This is done because if index is not written to disk, windows would need to be
//                           maintained in memory, increasing the memory footprint unnecessarily as the index
//                           is not (actually) used later in the app. (Windows could be also emptied, but again,
//                           index points are not used later.)
// int end_on_first_eof : end file processing on first feof():
//                        to be used when we are not compressing a growing file.
// int always_create_a_complete_index : create a 'complete' index file even in case of compressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time     : waiting time in seconds between reads when `-[S]`
// int extend_index_with_lines  : 0: create index without line numbers (v0 index)
//                                1: create index WITH line numbers (v1 index) using Unix format (\n)
//                                2: create index WITH line numbers (v1 index) using old Mac format (\r) (compatible with Windows \n\r)
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of built index (index->have)
struct returned_output compress_and_build_index(
    FILE *file_in, FILE *file_out, int level, char *file_name, uint64_t span,
    struct access **built, char *index_filename, int write_index_to_disk,
    int end_on_first_eof, int always_create_a_complete_index,
    int waiting_time, int extend_index_with_lines );

#ifdef __cplusplus
}
#endif

#endif /* GZTOOL_H */
