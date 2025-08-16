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

// TODO: remove duplication
enum VERBOSITY_LEVEL verbosity_level = VERBOSITY_NORMAL;

// values returned by decompress_in_advance() in ret.error value:
enum GZIP_MARK_FOUND_TYPE {
    GZIP_MARK_ERROR // GZIP_MARK_ERROR informs that recovery is not possible, but
                    // also that process will try to continue without recovery.
                    = 8, // All values greater than Z_* zlib's return codes, because
                         // a ret.error is used at decompress_in_advance() for both
                         // zlib's outputs and the function output (GZIP_MARK_FOUND_TYPE).
    GZIP_MARK_FATAL_ERROR, // This value aborts process, 'cause a compulsory fseeko() failed.
    GZIP_MARK_NONE,        // Decompress_in_advance() didn't find an error in gzip stream (all ok!).
    GZIP_MARK_BEGINNING,   // An error was found, and a new complete gzip stream was found
                           // to reinitiate process at ret.value byte.
    GZIP_MARK_FULL_FLUSH   // An error was found, and a new gzip-Z_FULL_FLUSH was found
                           // to reinitiate process at ret.value byte.
};

// values used to initialize or continue processing with decompress_in_advance() function:
enum DECOMPRESS_IN_ADVANCE_INITIALIZERS {
    DECOMPRESS_IN_ADVANCE_RESET,     // initial total reset
    DECOMPRESS_IN_ADVANCE_CONTINUE,  // no reset, just continue processing
    DECOMPRESS_IN_ADVANCE_SOFT_RESET // reset all but last_correct_reentry_point_returned
                                     // in order to continue processing the same gzip stream.
};

// `fprintf` substitute for printing with VERBOSITY_LEVEL
void print_to_stderr ( enum VERBOSITY_LEVEL verbosity, const char * format, ... ) {

    // if verbosity of message is above general verbosity_level, ignore message
    if ( verbosity <= verbosity_level ) {
        va_list args;
        va_start (args, format);
        vfprintf ( stderr, format, args );
        va_end (args);
    }

}

// Convert an integer to a short string (2 decimal places) using SI units.
// Please, note that this function uses always "number_output" global variable as output.
// INPUT:
// uint64_t input_number        : number to convert to binary SI units
// int binary                   : 0: decimal suffixes, 1: binary suffixes
// OUTPUT:
// pointer to buffer where string will be written:
// It is ALWAYS the "number_output" global variable.
char *give_me_si_units ( uint64_t input_number, int binary ) {
    // TODO: Remove deliberate memory leak
    char* number_output = malloc(sizeof(char) * MAX_GIVEMESIUNITS_RETURNED_LENGTH);

    int i;
    double number = (double)input_number;
    double BASE = ( (binary==1)? 1024.0: 1000.0 );

    char *SI_UNITS[7] =
        { "", "k", "M", "G", "T", "P", "E" };
    char *SI_BINARY_UNITS[7] =
        { "Bytes", "kiB", "MiB", "GiB", "TiB", "PiB", "EiB" };

    // totally empty string so that
    // end-of-string char is always correctly set no matter the number
    for ( i = 0; i < MAX_GIVEMESIUNITS_RETURNED_LENGTH; i++ )
        number_output[i] = '\0';

    for ( i = 0; number >= BASE; i++ )
        number /= BASE;

    snprintf( number_output, MAX_GIVEMESIUNITS_RETURNED_LENGTH,
        "%.2f %s", number, ( (binary==1)? SI_BINARY_UNITS[i]: SI_UNITS[i] ) );

    return number_output;

}


/**************
 * Endianness *
 **************/
static inline int endianness_is_big(void)
{
    long one= 1;
    return !(*((char *)(&one)));
}
static inline uint32_t endianness_swap_4(uint32_t v)
{
    v = ((v & 0x0000FFFFU) << 16) | (v >> 16);
    return ((v & 0x00FF00FFU) << 8) | ((v & 0xFF00FF00U) >> 8);
}
static inline void *endianness_swap_4p(void *x)
{
    *(uint32_t*)x = endianness_swap_4(*(uint32_t*)x);
    return x;
}
static inline uint64_t endianness_swap_8(uint64_t v)
{
    v = ((v & 0x00000000FFFFFFFFLLU) << 32) | (v >> 32);
    v = ((v & 0x0000FFFF0000FFFFLLU) << 16) | ((v & 0xFFFF0000FFFF0000LLU) >> 16);
    return ((v & 0x00FF00FF00FF00FFLLU) << 8) | ((v & 0xFF00FF00FF00FF00LLU) >> 8);
}
static inline void *endianness_swap_8p(void *x)
{
    *(uint64_t*)x = endianness_swap_8(*(uint64_t*)x);
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
size_t fread_endian( void * ptr, size_t size, FILE * stream ) {

    size_t output = fread(ptr, size, 1, stream);

    if (endianness_is_big()) {
        ;
    } else {
        // machine is little endian: flip bytes
        switch (size) {
            case 4:
                endianness_swap_4p(ptr);
                break;
            case 8:
                endianness_swap_8p(ptr);
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
size_t fwrite_endian( void * ptr, size_t size, FILE * stream ) {

    size_t output;
    void *endian_ptr;

    endian_ptr = malloc(size);

    if (endianness_is_big()) {
        memcpy(endian_ptr, ptr, size);
    } else {
        // machine is little endian: flip bytes
        switch (size) {
            case 4:
                *(uint32_t *)endian_ptr = *(uint32_t *)ptr;
                endianness_swap_4p(endian_ptr);
                break;
            case 8:
                *(uint64_t *)endian_ptr = *(uint64_t *)ptr;
                endianness_swap_8p(endian_ptr);
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


// pread() does not exist in Windows, so in this platform
// pread() is substitued by a fseeko() + fread() operations.
// ReadFile() in Windows would also be feasible in decompress_in_advance()
// context as it will explicitly restore file pointer at the end,
// but it doesn't seem to worth the effort of using non-POSIX functions.
ssize_t PREAD(
    FILE *fildes, void *buf, size_t nbyte, off_t offset
) {

#ifdef _WIN32
    fseeko( fildes, offset, SEEK_SET );
    return fread( buf, 1, nbyte, fildes );
#else
    return pread( fileno( fildes ), buf, nbyte, offset );
#endif

}

// decompress at least CHUNKS_TO_DECOMPRESS_IN_ADVANCE chunks
// in front of actual decompression stream (decompress_and_build_index()),
// to assure that no errors will be found, and if an error is found in advance,
// try to find a reason in these CHUNKS_TO_DECOMPRESS_IN_ADVANCE chunks
// that explain it as an incorrectly terminated gzip stream and the
// beginning of a new one immediately attached after it:
// and so either a GZIP header or a full flush point are found there.
// If an error is found and a feasible restart is found, its gzip stream
// byte position is returned for decompress_and_build_index() to decompress
// ONLY until that point, and RESTART decompression just in it later.
// If errors aren't detected with decompression in advance, incorrect but
// technically viable (will not launch zlib errors) data can/will be extracted,
// and the point of error will be already inserted in another gzip stream
// than the original.
// INPUT:
// z_stream strm_original: shallow copy of strm from callecjecompress_and_build_index()
//                         only used if strm structure hasn't been initialized
// FILE *file_in  : file to be used for reading. Its position pointer will be restored
//                  to actual position when this function returns.
//                  (stdin is not valid as it doesn't allow fseeko())
// char *file_name: name of the input file associated with FILE *in, to check the size
//                  of the input file (if not stdin) to check shrinking.
//                  Can be "" (no file name: stdin used; though decompress_in_advance() MUST NOT be called then,
//                             because this function MUST be able to seek backwards in gzip stream),
//                  or NULL if initialize_function == DECOMPRESS_IN_ADVANCE_RESET.
// uint64_t totin0: total number of input gzip stream bytes already "correctly" processed:
//                  it is set on first call, with initialize_function == DECOMPRESS_IN_ADVANCE_RESET, AND ALSO in all later calls.
// int chunks_in_advance0  : indicates if a decompression in advance will be used and how many CHUNKs
//                           will be decompressed in advance then (CHUNKS_TO_DECOMPRESS_IN_ADVANCE).
//                           If decompress_in_advance is called, this value MUST be GREATER than 0
//                           AND it MUST be set ONLY on first call with initialize_function == DECOMPRESS_IN_ADVANCE_RESET.
// int chunks_to_look_backwards0  : indicates how many CHUNKs can look the function backwards in order
//                           to find a new good gzip entry point to patch an error found in the stream.
//                           If decompress_in_advance is called, this value MUST be GREATER than 0
//                           AND it MUST be set ONLY on first call with initialize_function == DECOMPRESS_IN_ADVANCE_RESET.
// enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts0: value of indx_n_extraction_opts on caller.
//                                            only set on first call, with initialize_function == DECOMPRESS_IN_ADVANCE_RESET.
// int decompressing_with_gzip_headers0: 1 if inflateInit2(&strm, 47) on actual state on first
//                                       call with initialize_function == DECOMPRESS_IN_ADVANCE_RESET. 0 otherwise.
// int end_on_first_proper_gzip_eof0   : end file processing on first proper (at feof()) GZIP EOF.
//                                       Set with initialize_function == DECOMPRESS_IN_ADVANCE_RESET. 0 otherwise.
// uint64_t output_data_counter  : used to know if caller has already output data or not: depending on this
//                                  the final warning is about data collision or about different data beginning.
// uint64_t totout               : passed on each DECOMPRESS_IN_ADVANCE_CONTINUE call to inform it on overlapping warning.
// int waiting_time0             : waiting time in seconds between reads when `-[ST]` (always >0)
// bool lazy_gzip_stream_patching_at_eof0: when used with `-[ST]` implies that checking
//                                  for errors in stream is made as quick as possible as the gzip file
//                                  grows, not respecting then at EOF the CHUNKS_TO_DECOMPRESS_IN_ADVANCE value.
//                                  Set with initialize_function == DECOMPRESS_IN_ADVANCE_RESET. 0 otherwise.
// uint64_t expected_first_byte0 : indicates that the first byte on compressed input is this, not 1,
//                                and so truncated compressed inputs can be used. Used in decompress_in_advance()
//                                only to not check "Detected gzip file overwriting, so ending process" condition:
//                                this check must not be done if using `-n` (because then st.st_size < totin in general)
// DECOMPRESS_IN_ADVANCE_INITIALIZERS initialize_function:
//                          if DECOMPRESS_IN_ADVANCE_RESET, internal value strm_initialized is set to false
//                          marking this way the beginning of a new file processing.
//                          See DECOMPRESS_IN_ADVANCE_INITIALIZERS.
// OUTPUT:
// struct returned_output: contains two values:
//      .error: one of GZIP_MARK_FOUND_TYPE:
//              GZIP_MARK_ERROR, GZIP_MARK_NONE, GZIP_MARK_BEGINNING, GZIP_MARK_FULL_FLUSH.
//      .value: gzip stream byte position of a feasible restart gzip-point.
struct returned_output decompress_in_advance(
    const z_streamp strm_original, FILE *file_in, const char *file_name,
    const uint64_t totin0,
    const int chunks_in_advance0,
    const int chunks_to_look_backwards0,
    const enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts0,
    const int decompressing_with_gzip_headers0,
    const int end_on_first_proper_gzip_eof0,
    const uint64_t output_data_counter,
    const uint64_t totout,
    const int waiting_time0,
    const bool lazy_gzip_stream_patching_at_eof0,
    const uint64_t expected_first_byte0,
    const enum DECOMPRESS_IN_ADVANCE_INITIALIZERS initialize_function
) {
    struct returned_output returned_value; // returned structure
    returned_value.value = 0LLU;
    returned_value.error = GZIP_MARK_NONE;
    off_t initial_file_position = 0; // Store actual FILE *file_in position for later restore.
                                     // This value won't be needed if pread() would be cross-platform
                                     // as it doesn't modify the file pointer, but unfortunately it
                                     // doesn't exist in Windows platforms.
    int initial_file_EOF = 0; // It is important to keep the feof(file_in) state intact after all fseek()
    uint64_t caller_last_processed_byte = 0LLU; // store parameter totin0 when initialize_function == DECOMPRESS_IN_ADVANCE_CONTINUE
    uint64_t process_until_this_byte = 0LLU; // inflate() until this input byte position is reached
    int gzip_eof_detected = 0;// 1 if a GZIP file format EOF is detected, and so, feof() is not trustworthy
    // this value made static because control could return at <#Z_STREAM_END note> if totin >= process_until_this_byte:
    static uint64_t strm_avail_in_decrement = 0LLU; /* use to mark how many bytes must be discarded in the next read when processing
                                                    a bgzip tail that is not complete in the actual input pointer block */
    static z_stream strm;
    // internal structure to store ret.error from inflate() (cloning decompress_and_build_index()'s code):
    static struct returned_output ret =
        { .value = 0LLU,    // ret.value is never used
          .error = Z_OK };  // ret.error is used, to maintain this code as an-almost-clone of decompress_and_build_index()
                            // thanks to using the same name.
        // ( note: DO NOT initialize values out of the static declaration, as they'd be initialized on every call! )
    static bool strm_initialized = false;
    static enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts = JUST_CREATE_INDEX;
    static uint64_t totin = 0LLU;
    static uint64_t last_processed_byte = 0LLU; // maintains internal ftello() between calls to this function.
    static int chunks_in_advance = 0;   // set to chunks_in_advance0 from caller on decompress_in_advance()
                                        // initialization (..., DECOMPRESS_IN_ADVANCE_*RESET).
                                        // Also internally set to 0 to mark a correct EOF found and
                                        // so that then no more processing of this function is needed.
    static int chunks_to_look_backwards = 0;
    static int decompressing_with_gzip_headers = 0;
    static int end_on_first_proper_gzip_eof = 0;
    static int waiting_time = 0;
    static bool lazy_gzip_stream_patching_at_eof = false;
    static uint64_t expected_first_byte = 1LLU;
    static unsigned char input[CHUNK];    // TODO: convert to malloc
    static unsigned char window[WINSIZE]; // TODO: convert to malloc
    static uint64_t last_correct_reentry_point_returned = 0LLU; // this function will never go backwards beyond the
                                                                // last reentry point (It doesn't seem to have sense
                                                                // as it implies to discard last extracted bytes); anyway,
                                                                // what it's clear is that this last point MUST NOT be
                                                                // used again, or possible infinite loops could appear.
    static bool first_call = true;  // to know if this is the first call to this function,
                                    // and so strm already contains previous valid data!

    // strm_initialized must be set to false in decompress_in_advance()
    // when the file processing ends, no matter what...
    // As there may be errors in the middle of the process, the only
    // way to guarantee this is to explicitly reset this function prior to each processing,
    // via decompress_in_advance( ...,     DECOMPRESS_IN_ADVANCE_*RESET ) from decompress_and_build_index()
    if ( DECOMPRESS_IN_ADVANCE_RESET == initialize_function ||
         DECOMPRESS_IN_ADVANCE_SOFT_RESET == initialize_function ) {
        if ( true == strm_initialized ) {
            inflateEnd( &strm );
            // ( input[] and window[] must not be emptied
            //   as they're filled on (next) first call. )
        }
        strm_initialized = false;
        indx_n_extraction_opts = indx_n_extraction_opts0;
        totin = totin0;
        last_processed_byte = totin0;
        //ret.value = 0LLU; // ret.value is never used:
        ret.error = Z_OK;   // ret.error is used, to maintain this code as an-almost-clone of decompress_and_build_index()
                            // thanks to using the same name.
        chunks_in_advance = chunks_in_advance0;
        chunks_to_look_backwards = chunks_to_look_backwards0;
        decompressing_with_gzip_headers = decompressing_with_gzip_headers0;
        end_on_first_proper_gzip_eof = end_on_first_proper_gzip_eof0;
        expected_first_byte = expected_first_byte0;
        first_call = true;
        strm_avail_in_decrement = 0LLU;
        if ( DECOMPRESS_IN_ADVANCE_RESET == initialize_function ) {
            waiting_time = waiting_time0;
            lazy_gzip_stream_patching_at_eof = lazy_gzip_stream_patching_at_eof0;
            last_correct_reentry_point_returned = 0LLU;
        }
        if ( chunks_in_advance0 == 0 ||
             chunks_to_look_backwards0 == 0 ) {
            // these two values cannot be zero!
            returned_value.error = GZIP_MARK_FATAL_ERROR;
        }
        return returned_value;
    } else {
        // This value tells us how far behind we can go without compromising
        // already extracted (and so, already considered "good") data.
        // It is informative, so it's not (actually) used as a non-trespass barrier.
        caller_last_processed_byte = totin0;
        // note that in general: totin0 <= ftello(file_in)
    }

    // If this function found a licit EOF on a previous call,
    // then no more decompress_in_advance() processing is needed:
    if ( 0 == chunks_in_advance ) {
        return returned_value;
    }

    // store actual FILE *file_in position for later restore
    initial_file_position = ftello( file_in );
    initial_file_EOF      = feof( file_in );

    process_until_this_byte = caller_last_processed_byte + chunks_in_advance*CHUNK;

    // move file read position to last processed byte by this function:
    if ( 0 != fseeko( file_in, last_processed_byte, SEEK_SET ) ) {
        print_to_stderr( VERBOSITY_NORMAL,
            "ERROR: Cannot seek input file to @%llu Byte.\n", totin );
        returned_value.error = GZIP_MARK_FATAL_ERROR;
        return returned_value;
    }

    // if local strm has not been initialized
    // copy *strm_original on it, using inflateCopy()
    if ( false == strm_initialized ) {
        // deep copy:
        if ( Z_OK != inflateCopy( (z_streamp)&strm, strm_original ) ) {
            // no work can be done in this function! :
            print_to_stderr( VERBOSITY_EXCESSIVE, "ERROR: internal inflateCopy() failed.\n" );
            returned_value.value = 0LLU;
            returned_value.error = GZIP_MARK_ERROR;
            return returned_value;
        }
        // now copy original buffers to local ones:
        // It is not possible to copy all *input data only knowing
        // strm.next_in and strm.avail_in, because there exists the special
        // case of strm.avail_in<CHUNK when feof() and then strm.next_in == input
        // whilst otherwise strm_next_in - CHUNK + strm.avail_in == input.
        // Even if this is possible knowing feof(file_in), it is not needed
        // to copy all original input buffer: just the strm.avail_in # bytes suffice.
        // For input data: copy necessary bytes to local input with .next_in help
        memcpy( input, strm.next_in, strm.avail_in );
        strm.next_in = input;
        // For output data: copy it all to local window with .next_out help
        memcpy( window, strm.next_out - WINSIZE + strm.avail_out, WINSIZE );
        strm.next_out = window + WINSIZE - strm.avail_out;
        // at this point, decompress_in_advance() has been correctly initialized:
        strm_initialized = true;
    }

    // Note that all code from here on, which is not involved in the
    //      if ( Z_OK != ret.error &&
    //           Z_STREAM_END != ret.error ) { ... }
    // loop to manage errors in gzip stream, is copied from
    // decompress_and_build_index(), with some lines commented out with "////"
    // and not removed, in order to maintain some one-to-one identity with
    // the original code.

    // initiate decompression "in advance":
    do {

        // bgzip-compatible-streams code:
        if ( ret.error == Z_STREAM_END &&
             strm.avail_in > 0 ) { // if strm.avail_in is casually 0, this block of code isn't needed.
            // readjust input data moving it to the beginning of buffer:
            // (strm.next_in floats from input to input+CHUNK)
            memmove( input, strm.next_in, strm.avail_in ); // memmove() is overlapping-safe
        }
        // .................................................
        // note that here strm.avail_in > 0 only if ret.error == Z_STREAM_END
        int strm_avail_in0 = strm.avail_in;
        // .................................................
        // OK... previous comment:
        //  "note that here strm.avail_in > 0 only if ret.error == Z_STREAM_END"
        // is correct on caller decompress_and_build_index(),
        // :<#but not here in decompress_in_advance()> : here, strm.avail_in can be greater
        // than zero on first call due to the fact that on caller, call to this function occurs
        // after fread() has taken place, so strm.avail_in is full of bytes not yet processed
        // (a CHUNK or till EOF) - as strm is cloned here, also this input buffer is inherited full.
        // This has the side effect that first call to fread() here in +2 lines, is empty (returns no data)
        // and so totin will be greater than ftello(file_in) during first loop processing: this
        // curious but correct behaviour is replicated on every call from decompress_and_build_index()
        // that ocurrs after a reinitiation of the gzip stream: that is, on first index entry point
        // or beginning of gzip stream, and after a decompress_in_advance() returned new-valid-entry-point
        // is executed.
        // Another side effect is that in practice one call to fread()+disk is saved, which cannot be bad.
        // Nonetheless, it must be noted that after the first CHUNK is processed with strm.avail_in > 0,
        // the loop returns here with totin > ftello(file_in), which is not correct in general, as this
        // would result in another fread() over the same data that has been just processed! so fseeko()
        // must be set to totin position in order to correctly read next input from gzip stream:
        if ( totin > (uint64_t)ftello(file_in)
            // this condition's code must run (even if ret.error==Z_STREAM_END here, (see <#Z_STREAM_END note>))
            // to adjust file_in read position TO AVOID READ DATA ALREADY IN "input" BUFFER [*] which,
            // btw, ends at ( input + strm.avail_in ) if the memmove() was executed because ret.error == Z_STREAM_END
            // or ends at ( input + CHUNK - strm.avail_in ) if ret.error != Z_STREAM_END,
            // but in both cases that is equal to ( totin + strm.avail_in ):
            // ( note "[*]":
            //    see <#but not here in decompress_in_advance()> comment about why buffer is ahead of ftello(). )
             ) {
            print_to_stderr( VERBOSITY_MANIAC,
                    "PATCHING: Adjusting 'in advance' read position from %llu to %llu.\n", ftello(file_in), totin );
            if ( 0 != fseeko( file_in, totin + strm.avail_in, SEEK_SET ) ) {
                print_to_stderr( VERBOSITY_NORMAL,
                    "ERROR: Cannot seek input file to @%llu Byte.\n", totin );
                returned_value.error = GZIP_MARK_FATAL_ERROR;
                return returned_value;
            }
        }
        // .................................................
        if ( !feof( file_in ) &&
             ( (uint64_t)ftello( file_in ) == totin || initial_file_EOF == 0 ) // as we're using the same initial buffer with ftello(file_in)>totin
                                                                               // this precaution is compulsory
             ) { // on last block, strm.avail_in > 0 is possible with feof(file_in)==1 already!
            if ( !first_call
                 // !first_call ensure that no there's no DOUBLE fread over the same contents:
                 // on entering here for the first time (first caller call after DECOMPRESS_IN_ADVANCE_*RESET!)
                 // input ALREADY contains strm_avail_in0 valid data,
                 // so we cannot enter here to read the same data (fseeko() before the "do" loop) after it (input + strm_avail_in0),
                 // which would result in AN INCORRECT input buffer.
                 // Note than on first (first_call) "do" loop iteration always feof(file_in)==0 even with small inputs,
                 // because there's an fseeko() before the "do" loop - These small inputs (<CHUNK) are the ones that would result
                 // in AN INCORRECT input buffer, otherwise if file contents are still bigger than CHUNK passed this point
                 // in each call there's no problem as strm_avail_in0 == CHUNK and so this fread() reads 0 bytes.
            ) {
                strm.avail_in = fread(input + strm_avail_in0, 1, CHUNK - strm_avail_in0, file_in); // here I do not use pread() just to maintain the same code base as in decompress_and_build_index()
                print_to_stderr( VERBOSITY_MANIAC, "[read in advance %d B]", strm.avail_in );
                strm.avail_in += strm_avail_in0;
            }
        }
        if ( feof( file_in ) ) {
            // generic check for growing files (not related to bgzip-compatible-streams code):
            // check that file hasn't shrunk, what would mean that the file
            // has been overwritten from the beginning (possible with rsyslog logs, for example)
            if ( strlen( file_name ) > 0 &&  // this check cannot be done on STDIN
                 expected_first_byte <= 1LLU // this check must not be done if using `-n` (because then st.st_size < totin in general)
            ) {
                struct stat st;
                stat(file_name, &st);
                print_to_stderr( VERBOSITY_NUTS, "(%llu<%llu?)", st.st_size + GZIP_HEADER_SIZE_BY_ZLIB, totin );
                if ( (uint64_t)( st.st_size + GZIP_HEADER_SIZE_BY_ZLIB ) < totin ) {
                    // file has shrunk! so do a correct finish of the action_create_index (whatever it is)
                    // (Note that it is not possible that this condition arises when accessing a truncated gzip
                    // file with its corresponding (not-truncated) complete index file, because in this case
                    // fseeko() previously failed. Also if gzip-file size is 0 (which is possible and allowed)
                    // this condition won't arise because (10 < 0) is false (index doesn't exist) and
                    // (10 < 10) is false (index contains always the first access point))
                    print_to_stderr( VERBOSITY_NORMAL, "\nPATCHING: Detected '%s' gzip file overwriting, so ending patching process\n", file_name );
                    break;
                }
            }
        }
        // .................................................
        //      It is not necessary to reinitiate decompress_in_advance() after Z_STREAM_END
        //      in case of returned_value.error == GZIP_MARK_ERROR && returned_value.value > 0
        //      just because that means that the process will find here at its moment
        //      an unrecoverable error and will stop :-)
        // .................................................
        // decompressor state MUST be reinitiated after Z_STREAM_END
        if ( ret.error == Z_STREAM_END ) {
            print_to_stderr( VERBOSITY_MANIAC, "Reinitializing zlib 'in advance' strm data @%llu...\n", totin );
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
                goto decompress_in_advance_ret;
            strm.avail_in = strm_avail_in0;
            // it is compulsory to reinitiate also output data:
            strm.avail_out = WINSIZE;
            strm.next_out = window;
            ////avail_out_0 = strm.avail_out;
            ////window_size = 0;
        }
        // end of bgzip-compatible-streams code

        first_call = false; // before any "continue" on this "do" loop

        if ( (indx_n_extraction_opts == SUPERVISE_DO ||
              indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
              indx_n_extraction_opts == EXTRACT_TAIL ) &&
             strm.avail_in == 0 ) {

            //// ...

            if ( indx_n_extraction_opts == EXTRACT_TAIL ) {
                // the process ends here as all required data has been output
                // (index remains incomplete)
                ////ret.error = Z_OK;
                ret.error = GZIP_MARK_NONE;
                ////if ( NULL != index ) {
                ////    ret.value = index->have;
                ////}
                goto decompress_in_advance_ret;
            }

            // sleep and retry
            if ( true == lazy_gzip_stream_patching_at_eof ) {
                // return to caller as soon as possible, ignoring
                // CHUNKS_TO_DECOMPRESS_IN_ADVANCE at EOF when `-P[ST]`,
                // to print out current caller input buffer,
                // which, as we've reached until here, is CORRECT!
                // because fread() MUST have read it also and so the
                // inflate() has occurred because this code is ONLY
                // REACHED WITH strm.avail_in == 0
                ret.error = GZIP_MARK_NONE;
                goto decompress_in_advance_ret;
            } else {
                sleep( waiting_time );
            }
            clearerr( file_in );
            continue;

        }

        if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
             indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
             indx_n_extraction_opts == EXTRACT_FROM_LINE ||
             indx_n_extraction_opts == EXTRACT_TAIL ) {
            // with not Supervising options, strm.avail_in == 8 + eof is equivalent to Correct END OF GZIP at EOF
            if ( ( feof( file_in ) || initial_file_EOF != 0 ) &&
                 strm.avail_in == 8 ) {
                print_to_stderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected 'in advance' at EOF-8.\n" );
                break;
            }
        }

        if (ferror(file_in)) {
            ret.error = Z_ERRNO;
            goto decompress_in_advance_ret;
        }
        if (strm.avail_in == 0) {
            ret.error = Z_DATA_ERROR;
            goto decompress_in_advance_ret;
        }

        // Set strm.next_in to input buffer.
        // Note that if strm_avail_in_decrement == 0, strm.avail_in and totin don't change.
        strm.next_in = input + strm_avail_in_decrement;
        strm.avail_in -= strm_avail_in_decrement;
        totin += strm_avail_in_decrement;
        // after using strm_avail_in_decrement it must be set to zero
        strm_avail_in_decrement = 0;

        do {

            /* reset sliding window if necessary */
            if (strm.avail_out == 0) {
                strm.avail_out = WINSIZE;
                strm.next_out = window;
                ////avail_out_0 = strm.avail_out;
            }

            /* inflate until out of input, output, or at end of block --
               update the total input and output counters */
            totin += strm.avail_in;
            ////totout += strm.avail_out;
            ////window_size += strm.avail_out; // update window_size available size
            ret.error = inflate(&strm, Z_BLOCK); /* return at end of block */
            totin -= strm.avail_in;
            ////totout -= strm.avail_out;

            // .................................................
            // treat possible gzip tail:
            if ( ret.error == Z_STREAM_END &&
                 // strm.avail_in >= 8 && // strm.avail_in may be less than the required 8 bytes for a tail
                                          // this case must also be treated because inflate() would raise an error
                                          // if initiated in the middle of a tail
                 decompressing_with_gzip_headers == 0 ) {
                // discard this data as it is probably the 8 gzip-tail bytes (4 CRC + 4 size%2^32)
                // and zlib is not able to consume it if inflateInit2(&strm, -15) (raw decompressing)
                if ( strm.avail_in < 8 ) {
                    strm.next_in += strm.avail_in;
                    totin += strm.avail_in; // data is discarded from strm input, but it MUST be counted in total input
                    strm_avail_in_decrement = 8 - strm.avail_in;
                    strm.avail_in = 0;
                } else {
                    strm.next_in += 8;
                    totin += 8; // data is discarded from strm input, but it MUST be counted in total input
                    strm.avail_in -= 8;
                }
                ////if ( offset_in >= 8 )
                ////    offset_in -= 8; // data is discarded from strm input, and also from offset_in
                ////else
                ////    offset_in = 0;
                print_to_stderr( VERBOSITY_MANIAC,
                    "END OF GZIP passed @%llu while in raw 'in advance' mode (avail_in=%d, ftello=%llu)\n",
                    totin, strm.avail_in, ftello(file_in) );
                if ( end_on_first_proper_gzip_eof == 1 ||
                    ( ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                    indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                    indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                    indx_n_extraction_opts == EXTRACT_TAIL ) && ( feof( file_in ) || initial_file_EOF != 0 ) ) ) {
                    gzip_eof_detected = 0; // to exit outer loop with gzip_eof_detected == 0
                                           // if the rest of loop conditions hold true.
                    chunks_in_advance = 0; // decompress_in_advance() ceases to have sense in this gzip stream
                                           // so that is marked here with static chunks_in_advance == 0.
                } else {
                    gzip_eof_detected = 1;
                }
                break;
                //// avail_in_0 doesn't need to be decremented as it tries to count raw stream input bytes
            }
            // end of treat possible gzip tail
            // .................................................

            if ( Z_OK != ret.error &&
                 Z_STREAM_END != ret.error ) { // [B]
                // This is the point of error in the gzip stream!
                // Now we must obtain current position in gzip stream to figure out
                // if we can go backwards, and then look for GZIP init stream markers.
                returned_value.error = GZIP_MARK_NONE;
                // Which position exactly has the error been found at:
                off_t file_position_of_error = totin;
                print_to_stderr( VERBOSITY_NORMAL,
                    "PATCHING: Gzip stream error %d found @ %llu Byte.\n", ret.error, file_position_of_error );
                if ( file_position_of_error == 0 ) {
                    // there's no room for more backwards searching for gzip initial markers
                    // [A]
                    returned_value.error = GZIP_MARK_ERROR;
                    returned_value.value = 0LLU;
                    goto decompress_in_advance_ret;
                } else {
                    // in order to honor chunks_to_look_backwards,
                    // the reverse search will be partitioned in chunks of size CHUNK
                    int i = 0;
                    uint64_t backwards_until_this_byte = 0LLU;
                    int MAX_HEADER_SIZE = 3;
                    int LOCAL_CHUNK = CHUNK +MAX_HEADER_SIZE; // CHECK GZIP HEADERS SPREAD BETWEEN CHUNKS' BORDERS!!!
                                                              // so read 3 bytes more than a CHUNK each time.
                    unsigned char buffer[LOCAL_CHUNK];
                    int buffer_position = 0;
                    int bytes_read = 0;

                    do { // while ( ++i <= chunks_to_look_backwards &&
                         //         backwards_until_this_byte >= CHUNK );

                        backwards_until_this_byte =
                            ( (file_position_of_error / CHUNK) - i ) * CHUNK;// integer operations

                        if ( backwards_until_this_byte <= last_correct_reentry_point_returned ) {
                            // never pass "last_correct_reentry_point_returned" byte
                            // backwards as it has been already used:
                            backwards_until_this_byte = last_correct_reentry_point_returned + 1;
                        }

                        // I use pread() to avoid multiple syscalls with fseeko()+fread()
                        if ( !( bytes_read =
                              PREAD( file_in, buffer,
                                LOCAL_CHUNK, // read CHUNK even @ i==0, to be able to search even a little
                                             // after buffer_position in that case (markers has 4 bytes in length)
                                backwards_until_this_byte ) ) ) {
                            returned_value.error = GZIP_MARK_ERROR;
                            returned_value.value = 0LLU;
                            goto decompress_in_advance_ret;
                        }

                        buffer_position = bytes_read - MAX_HEADER_SIZE; // do not read pass the buffer's limits!

                        if ( i == 0 &&
                             ( backwards_until_this_byte + buffer_position ) > totin
                        ) {
                            buffer_position = totin - backwards_until_this_byte;
                        }

                        if ( buffer_position < 0 ) {
                            buffer_position = 0; // do not read pass the buffer's limits!
                        }

                        // loop to run over all buffer long in case
                        // multiple "false" restart points, at possible gzip markers, are found:
                        do { // while ( buffer_position >= 0 );
                            // so now it's the time to search for GZIP-beggining markers:
                            // //tools.ietf.org/html/rfc1952#section-2.2
                            //  1F 8B + compressionMethod (08: deflate) + flags + 4x modificationTime + ...
                            //  or
                            //  00 00 FF FF, for a full flush point (Z_FULL_FLUSH)
                            //
                            do {
                                if ( buffer[buffer_position]    == 0x1F &&
                                     buffer[buffer_position +1] == 0x8B &&
                                     buffer[buffer_position +2] == 0x08 &&
                                     buffer[buffer_position +3] < 32 // first three bits are always reserved as 0
                                    ) {
                                    returned_value.value = backwards_until_this_byte + buffer_position;
                                    returned_value.error = GZIP_MARK_BEGINNING;
                                    break;
                                }
                                if ( buffer[buffer_position]    == 0x00 &&
                                     buffer[buffer_position +1] == 0x00 &&
                                     buffer[buffer_position +2] == 0xFF &&
                                     buffer[buffer_position +3] == 0xFF ) {
                                    returned_value.value = backwards_until_this_byte + buffer_position
                                        + 4; // hopefully this +4 position will exist even after end of "buffer"
                                    returned_value.error = GZIP_MARK_FULL_FLUSH;
                                    break;
                                }
                            } while ( --buffer_position >= 0 );

                            if ( returned_value.error == GZIP_MARK_BEGINNING ||
                                 returned_value.error == GZIP_MARK_FULL_FLUSH ) {
                                buffer_position--;
                                // now we must try to see if the point found is
                                // a valid starting point of a GZIP compressed stream!
                                /* initialize file and inflate state to start there */
                                z_stream strm;
                                struct returned_output ret2;
                                unsigned char buffer[CHUNK];  // input
                                unsigned char window[WINSIZE];// output
                                // initialize zlib's strm:
                                strm.zalloc = Z_NULL;
                                strm.zfree = Z_NULL;
                                strm.opaque = Z_NULL;
                                strm.avail_in = 0;
                                strm.next_in = Z_NULL;
                                if ( returned_value.error == GZIP_MARK_FULL_FLUSH ) {
                                    // raw inflate
                                    ret2.error = inflateInit2(&strm, -15);
                                    decompressing_with_gzip_headers = 0;
                                } else {
                                    // automatic zlib or gzip decoding (15 + automatic header detection)
                                    ret2.error = inflateInit2(&strm, 47);
                                    decompressing_with_gzip_headers = 1;
                                }
                                if ( Z_OK != ret2.error ) {
                                    returned_value.error = ret2.error;
                                    returned_value.value = 0LLU;
                                    // caller must know that an unrecoverable error occurred
                                    goto decompress_in_advance_ret;
                                }
                                // once zlib's strm is initialized, provide content to it:
                                if ( !PREAD( file_in, buffer,
                                    CHUNK,
                                    returned_value.value ) ) {
                                    (void)inflateEnd(&strm); // empty strm structure appropriately
                                    returned_value.error = GZIP_MARK_ERROR;
                                    returned_value.value = 0LLU;
                                    // caller must known that an unrecoverable error occurred
                                    goto decompress_in_advance_ret;
                                }
                                strm.next_in = buffer;
                                strm.avail_in = CHUNK;
                                strm.next_out = window;
                                strm.avail_out = WINSIZE;
                                ret2.error = inflate(&strm, Z_BLOCK);
                                (void)inflateEnd(&strm); // empty strm structure appropriately
                                if ( Z_OK != ret2.error ) {
                                    // this is not a valid restarting point for gzip:
                                    // so let's try another one!
                                    continue;
                                } else {
                                    // this is a valid restarting point for gzip:
                                    // communicate this to caller: it'll take appropriate measures
                                    goto decompress_in_advance_ret;
                                }

                            } // if ( returned_value.error == GZIP_MARK_BEGINNING ||
                              //      returned_value.error == GZIP_MARK_FULL_FLUSH )

                        } while ( buffer_position >= 0 );

                    } while ( ++i <= chunks_to_look_backwards &&
                              backwards_until_this_byte >= CHUNK );

                    // there has been no luck looking for a valid gzip restart point:
                    returned_value.error = GZIP_MARK_ERROR;
                    returned_value.value = totin;
                    goto decompress_in_advance_ret;

                } // if ( file_position_of_error <= 0 ||
                  //      file_position_of_error - CHUNK + strm.avail_in < 0 )

            } // if ( Z_OK != ret.error &&
              //      Z_STREAM_END != ret.error )


            if (ret.error == Z_STREAM_END) {
                if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                     indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                     indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                     indx_n_extraction_opts == EXTRACT_TAIL ) {
                    // with not Supervising options, a Z_STREAM_END at feof() is correct!
                    if ( ( feof( file_in ) ||
                           initial_file_EOF != 0 ) // as we're using the same initial buffer with ftello(file_in)>totin
                                                   // this precaution is compulsory
                         && strm.avail_in == 0 ) {
                        print_to_stderr( VERBOSITY_MANIAC, "Correct END OF GZIP file detected at EOF whilst 'in advance' mode.\n" );
                        gzip_eof_detected = 0; // to exit loop, as "gzip_eof_detected == 1" is one ORed condition
                                               // and now this variable is not needed anymore.
                        break;
                    }
                }
                if ( end_on_first_proper_gzip_eof == 0 )
                    gzip_eof_detected = 1;
                if ( gzip_eof_detected == 0 ) {
                    if ( end_on_first_proper_gzip_eof == 1 &&
                         ( feof( file_in ) || initial_file_EOF != 0 ) ) {
                        gzip_eof_detected = 0;
                    } else {
                        gzip_eof_detected = 1;
                        ////print_to_stderr( VERBOSITY_NORMAL, "Warning: GZIP end detected in the middle of data: deactivating `-E`\n" );
                        end_on_first_proper_gzip_eof = 0;
                    }
                }
                ////if ( offset_in >= 8 ) {
                ////    offset_in -= 8; // data is discarded from strm input, and also from offset_in
                ////} else {
                ////    offset_in = 0;
                ////}
                break;
            }

        } while ( strm.avail_in != 0 );

        // Test if loop
        // do { ... } while ( ret.error != Z_STREAM_END || strm.avail_in > 0 || gzip_eof_detected == 1 );
        // must end.
        // This condition outside the while() condition of the loop is only possible
        // if "continue;" is not used affecting this loop.
        if ( totin >= process_until_this_byte /*&&
             ret.error != Z_STREAM_END*/ ) { // :<#Z_STREAM_END note>
                                           // Z_STREAM_END conditions can be treated in the loop
                                           // before returning to caller as they are very sensible:
                                           // there's a lot of code in the beginning of the loop
                                           // waiting to correctly treat this condition. Entering
                                           // there from a new call, is possible, preserving the state
                                           //   { totin, ftello(), ret.error, strm_avail_in_decrement }
                                           // which is what I've finally decided to do. But uncommenting
                                           // the condition "&& ret.error != Z_STREAM_END" is totally fine.
            break;
        }

    } while ( ret.error != Z_STREAM_END || strm.avail_in > 0 || gzip_eof_detected == 1 );

    // ok, decompression in advance has correctly ended
    returned_value.value = 0LLU;
    returned_value.error = GZIP_MARK_NONE;


  /* return value */
  decompress_in_advance_ret:

    // store actual ftello() to fseeko() file_in on next calls
    last_processed_byte = ftello( file_in );

    // strm structure deletion is called by a decompress_in_advance(...,true)
    // from caller, when it estimates it is time to do it: that is (should be) after
    // a successful GZIP_MARK_FULL_FLUSH or GZIP_MARK_BEGINNING returned ret.error value.

    // before returning, a fseeko() is needed if native pread() (which doesn't
    // change file pointer value) hasn't been used
    // OR if fread() has been used, which is always the case (just to maintain the
    // same code base as in decompress_and_build_index()).
    if ( 0 != fseeko( file_in, initial_file_position, SEEK_SET ) ) {
        print_to_stderr( VERBOSITY_NORMAL,
            "ERROR: Cannot seek input file to @%llu Byte.\n", initial_file_position );
        returned_value.error = GZIP_MARK_FATAL_ERROR;
    }
    if ( 0 != initial_file_EOF ) {
        if ( 0 != fread( input, 1, 1, file_in ) ) {
            // If file_in has grown in the interim, it doesn't matter if feof() is
            // not maintained because then the feof() won't be "correct" anyway...
            if ( 0 != fseeko( file_in, initial_file_position, SEEK_SET ) ) {
                print_to_stderr( VERBOSITY_NORMAL,
                    "ERROR: Cannot seek input file to @%llu Byte.\n", initial_file_position );
                returned_value.error = GZIP_MARK_FATAL_ERROR;
            }
            // and feof() == 0 but as said, it shouldn't be important.
        }
    }

    // set last_correct_reentry_point_returned
    if ( returned_value.error == GZIP_MARK_BEGINNING ||
         returned_value.error == GZIP_MARK_FULL_FLUSH ) {
        last_correct_reentry_point_returned = returned_value.value;
    }

    // use caller_last_processed_byte to issue data collision warning if needed:
    if ( (returned_value.error == GZIP_MARK_BEGINNING ||
          returned_value.error == GZIP_MARK_FULL_FLUSH ) &&
         returned_value.value < caller_last_processed_byte ) {
        if ( output_data_counter > 0LLU ) {
            print_to_stderr( VERBOSITY_NORMAL,
                "PATCHING WARNING:\n"
                "    Part of data extracted after (compressed %llu / uncompressed %llu) Byte\n"
                "    overlaps with previously extracted data, after a badly-ended gzip stream\n"
                "    was found @%llu and a new starting point began @%llu.\n",
                caller_last_processed_byte, totout, totin, returned_value.value );
        } else {
            // if caller has not yet extracted data, the warning is to indicate
            // that the point of extraction will be BEFORE indicated `-[bL]` value:
            if ( SUPERVISE_DO              != indx_n_extraction_opts &&
                 JUST_CREATE_INDEX         != indx_n_extraction_opts &&
                 COMPRESS_AND_CREATE_INDEX != indx_n_extraction_opts
                 ) {
                print_to_stderr( VERBOSITY_NORMAL,
                    "PATCHING WARNING:\n"
                    "    Beginning of extracted data will begin BEFORE indicated\n"
                    "    `-[bL]` value, due to an unfinished gzip-erroneous-stream.\n"
                    );
            } else {
                // unless there was nothing to extract!
                print_to_stderr( VERBOSITY_NORMAL,
                    "PATCHING WARNING:\n"
                    "    Data extracted around the patching point may overlap.\n"
                    );
            }
        }
    }

    return returned_value;

}


// Returns the index position in char 'buffer' array in which ends
// the line number 'number_of_lines'
//  - OR -
// if number_of_lines == 0, returns the number of lines seen in
// passed 'buffer' of size 'size_of_buffer'
// INPUT:
// const unsigned char *buffer: array of chars
// int size_of_buffer   : size of buffer (at most, WINSIZE)
// int number_of_lines  : number of lines to find
// int line_number_format   : 0: '\n', 1: '\r'
// int *there_are_more_chars: totlines may need to be decremented at the end depending on last stream char;
//                            "passed by reference", may be NULL, in which case it is not treated.
// OUTPUT:
// int give_me_number_of_lines_in_chars: index position in 'buffer' at which ends line number number_of_lines. This
//                                 coincides with number of bytes in buffer before next line 'number_of_lines + 1'.
//                                 OR if number_of_lines == 0, number of lines seen in
//                                 passed 'buffer' of size 'size_of_buffer'
int give_me_number_of_lines_in_chars(
    const unsigned char *buffer,
    int size_of_buffer,
    int number_of_lines,
    int line_number_format,
    int *passed_there_are_more_chars )
{
    const unsigned char *original_buffer_beginning = buffer;
    unsigned char *pos;
    int there_are_more_chars = 0;  /* totlines may need to be decremented at the end depending on last stream char */
    int have_lines = 0;
    do {
        pos = memchr( buffer, ( (0 == line_number_format)? '\n': '\r' ), size_of_buffer );
        if ( NULL != pos ) {
            size_of_buffer -= ( pos - buffer ) + 1;
            buffer = pos + 1;
            have_lines ++;
            if ( size_of_buffer > 0 )
                there_are_more_chars = 1;
            else
                there_are_more_chars = 0;
        }
        if ( have_lines == number_of_lines ) {
            break;
        }
    } while ( NULL != pos && pos < (buffer + size_of_buffer) );

    if ( NULL != passed_there_are_more_chars ) {
        *passed_there_are_more_chars = there_are_more_chars;
    }

    if ( 0 == number_of_lines ) {
        return have_lines;
    } else {
        return (int)( buffer - original_buffer_beginning );
    }

}


// Limit buffer output based on range_number_of_bytes or range_number_of_lines,
// if necessary.
// Used in decompress_and_build_index() only @
//      if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE )
// and  if ( indx_n_extraction_opts == EXTRACT_FROM_LINE )
// INPUT:
// uint64_t *range_number_of_bytes: range_number_of_bytes "passed by reference"
// uint64_t *range_number_of_lines: range_number_of_lines "passed by reference"
// unsigned int *have   : have "passed by reference"
// uint64_t offset      : offset in buffer
// uint64_t *output_data_counter  : output_data_counter "passed by reference"
// uint64_t *output_lines_counter : output_lines_counter "passed by reference"
// uint64_t *have_lines : have_lines "passed by reference"
// const unsigned char *WINDOW: WINDOW value on caller, already adjusted;
//                              It is a const because it is not changed.
// int line_number_format: index->line_number_format on caller
// OUTPUT:
// bool limitBufferOutput: if true, process is terminated on caller because range
// (range_number_of_bytes or range_number_of_lines) has been reached
bool limitBufferOutput(
    uint64_t *range_number_of_bytes, uint64_t *range_number_of_lines,
    unsigned int *have, uint64_t offset,
    uint64_t *output_data_counter, uint64_t *output_lines_counter,
    uint64_t *have_lines, const unsigned char *WINDOW, int line_number_format )
{
    bool end_processing_because_of_range = false;

    if ( *range_number_of_bytes > 0 ) {
        if ( *range_number_of_bytes > (*have - offset) ) {
            *range_number_of_bytes -= (*have - offset);
        } else {
            // decrement counters to take *range_number_of_bytes into account:
            *output_data_counter -= ( (*have - offset) - *range_number_of_bytes );
            *have -= ( (*have - offset) - *range_number_of_bytes );
            *output_lines_counter += - *have_lines +
                                    give_me_number_of_lines_in_chars(
                                        WINDOW,
                                        *have - offset, 0,
                                        line_number_format, NULL );
            // process will end after this last fwrite!
            end_processing_because_of_range = true;
        }
    }
    if ( *range_number_of_lines > 0 ) {
        if ( *range_number_of_lines > *have_lines ) {
            *range_number_of_lines -= *have_lines;
        } else {
            // decrement counters to take *range_number_of_lines into account:
            *output_lines_counter += *range_number_of_lines - *have_lines;
            *have_lines = *range_number_of_lines;
            // give_me_number_of_lines_in_chars() won't be never greater than (*have - offset):
            int number_of_lines_in_chars = give_me_number_of_lines_in_chars(
                                            WINDOW,
                                            *have - offset, *range_number_of_lines,
                                            line_number_format, NULL );
            *output_data_counter -= ( (*have - offset) - number_of_lines_in_chars );
            *have -= ( (*have - offset) - number_of_lines_in_chars );
            // process will end after this last fwrite!
            end_processing_because_of_range = true;
        }
    }

    return end_processing_because_of_range;

}


// See gztool_internal.h
struct returned_output decompress_and_build_index(
    FILE *file_in, FILE *file_out, char *file_name, uint64_t span, struct access **built,
    enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts, uint64_t offset, uint64_t line_number_offset,
    char *index_filename, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time, int extend_index_with_lines, uint64_t expected_first_byte,
    int gzip_stream_may_be_damaged, bool lazy_gzip_stream_patching_at_eof,
    uint64_t range_number_of_bytes, uint64_t range_number_of_lines,
    bool adjust_index_points_to_byte_boundary, bool continue_tailing_on_eof )
{
    struct returned_output ret;
    uint64_t totin  = 0;           /* our own total counters to avoid 4GB limit */
    uint64_t totout = 0;           /* our own total counters to avoid 4GB limit */
    uint64_t totlines = 1;         /* counts total line numbers in uncompressed data from file_in */
    int there_are_more_chars = 0;  /* totlines may need to be decremented at the end depending on last stream char */
    uint64_t last   = 0;           /* totout value of last access point */
    uint64_t offset_in = 0;        /* offset in compressed data to reach (opposed to "offset" in uncompressed data) */
    uint64_t have_lines = 0;       /* number of lines in last chunk of uncompressed data */
    uint64_t avail_in_0;           /* because strm.avail_in may not exhausts every cycle! */
    uint64_t avail_out_0;          /* because strm.avail_out may not exhausts every cycle! */
    uint64_t strm_avail_in_decrement = 0LLU; /* use to mark how many bytes must be discarded in the next read when processing
                                             a bgzip tail that is not complete in the actual input pointer block */
    struct access *index = NULL;/* access points being generated */
    struct point *here = NULL;
    uint64_t actual_index_point = 0;  // only set initially to >0 if NULL != *built
    uint64_t output_data_counter = 0; // counts uncompressed bytes extracted to stdout
    uint64_t output_lines_counter = 0;// counts lines extracted to stdout
    uint64_t decompress_until_this_point_byte = 0LLU; // used if gzip_stream_may_be_damaged == 1 &&
                                                      // decompress_in_advance() found a valid new
                                                      // GZIP-beginning at this byte in the file_in stream.
    int decompress_until_this_point_type = GZIP_MARK_NONE; // stores type of beginning:
                                                      // ( GZIP_MARK_FULL_FLUSH or GZIP_MARK_BEGINNING )
                                                      // when decompress_until_this_point_byte > 0. Also
                                                      // GZIP_MARK_ERROR && decompress_until_this_point_byte > 0
                                                      // when an unrecoverable error was detected.
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
    // to store original indx_n_extraction_opts when continue_tailing_on_eof:
    enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts_4_tailing_on_eof = EXTRACT_FROM_BYTE;

    ret.value = 0LLU;
    ret.error = Z_OK;

    // Inform index filename
    if ( NULL != (*built) &&
        // if index->have == 0 index is superfluous
        (*built)->have > 0 &&
        // this condition is never true because index is passed always as incomplete
        (*built)->index_complete == 1 ) {
        // even though index is complete, processing will occur because gzip file may have changed!
        print_to_stderr( VERBOSITY_NORMAL, "Using index '%s'...\n", index_filename );
    } else {
        if ( write_index_to_disk == 1 ) {
            if ( strlen(index_filename) > 0 )
                print_to_stderr( VERBOSITY_NORMAL, "Processing index to '%s'...\n", index_filename );
            else
                print_to_stderr( VERBOSITY_NORMAL, "Processing index to STDOUT...\n" );
        } else {
            if ( indx_n_extraction_opts != DECOMPRESS )
                print_to_stderr( VERBOSITY_NORMAL, "Reading index '%s'...\n", index_filename );
        }
    }

    /* open index_filename for binary reading & writing */
    if ( strlen(index_filename) > 0 &&
        ( NULL == (*built) ||           // if passed index doesn't exists yet,
          // or it's an incomplete index in which case it may need to be completed
          (*built)->index_complete == 0 )
        ) {
        if ( indx_n_extraction_opts != DECOMPRESS ) {
            print_to_stderr( VERBOSITY_EXCESSIVE, "write_index_to_disk = %d", write_index_to_disk );
            if ( write_index_to_disk == 1 ) {
                if ( access( index_filename, F_OK ) != -1 ) {
                    // index_filename already exist:
                    // "r+": Open a file for update (both for input and output). The file must exist.
                    // r+, because the index may be incomplete, and so decompress_and_build_index() will
                    // append new data and complete it (->have & ->size to correct values, not 0x0..0, 0xf..f).
                    index_file = fopen( index_filename, "r+b" );
                    print_to_stderr( VERBOSITY_EXCESSIVE, " (r+b)\n" );
                } else {
                    // index_filename does not exist:
                    index_file = fopen( index_filename, "w+b" );
                    print_to_stderr( VERBOSITY_EXCESSIVE, " (w+b)\n" );
                }
            } else {
                // open index file in read-only mode (if file does not exist, NULL == index_file)
                index_file = fopen( index_filename, "rb" );
                print_to_stderr( VERBOSITY_EXCESSIVE, " (rb)\n" );
            }
        }
    } else {
        // restrictions to not collide index output with data output to stdout
        // MUST have been made on caller.
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        index_file = stdout;
    }
    if ( NULL == index_file && write_index_to_disk == 1 ) {
        print_to_stderr( VERBOSITY_NORMAL, "Could not write index to file '%s'.\n", index_filename );
        ret.error = Z_ERRNO;
        return ret;
    }

    if ( indx_n_extraction_opts == DECOMPRESS ) {
        indx_n_extraction_opts = EXTRACT_FROM_BYTE;
        offset = 0;
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
        index_file_size_0 = index->uncompressed_file_size;// marker to detect index updates (and to not write if index isn't updated)

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
            totlines = index->list[ actual_index_point ].line_number;
            last = totout;
            here = &(index->list[ actual_index_point ]);
        }

        // this block of code never executes because index is always received as incomplete
        if ( index->index_complete == 1 ) {
            if ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
                 indx_n_extraction_opts == EXTRACT_TAIL ) {
                offset = ( index->uncompressed_file_size - totout ) /4*3;
                indx_n_extraction_opts = EXTRACT_FROM_BYTE;
            }
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
            totlines = index->list[ actual_index_point ].line_number;
            last = totout;
            // offset value comes from caller as parameter
        }

        if ( indx_n_extraction_opts == EXTRACT_FROM_LINE ) {
            // move to the point needed for positioning on a line, or
            // move to last available point if offset can't be reached
            // with actually available index
            here = index->list;
            actual_index_point = 0;
            while (
                ++actual_index_point &&
                actual_index_point < index->have &&
                // if index point is on the desired line number, we must start
                // on the previous one to retrieve the line from its beginning:
                here[1].line_number < line_number_offset
                )
                here++;
            actual_index_point--;
            totin  = index->list[ actual_index_point ].in;
            totout = index->list[ actual_index_point ].out;
            totlines = index->list[ actual_index_point ].line_number;
            last = totout;
            // offset value comes from caller as parameter
        }

        assert( NULL != here );

        print_to_stderr( VERBOSITY_EXCESSIVE, "Starting from index point %llu (@%llu->%llu,L%llu).\n",
            actual_index_point+1, here->in, here->out, here->line_number );

        // fseek in data for correct position
        // using here index data:
        {
            uint64_t position = here->in - (here->bits ? 1 : 0);

            // checking expected_first_byte value:
            // (although this has already been done for `-b`, `-L` could not be checked until now)
            if ( ( expected_first_byte -1 ) > position ) {
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-n %llu` is beyond required index point @%llu byte.\n",
                    expected_first_byte, position );
                ret.error = Z_ERRNO;
                // strm and index are already filled, so a simple return isn't possible:
                goto decompress_and_build_index_error;
            } else {
                position -= ( expected_first_byte -1 );
            }

            if ( stdin == file_in ) {
                // read input until here->in - (here->bits ? 1 : 0)
                uint64_t pos = 0;
                ret.error = 0;
                while ( pos < position ) {
                    if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), file_in) ) {
                        ret.error = -1;
                        break;
                    }
                    pos += CHUNK;
                }
            } else {
                ret.error = fseeko(file_in, position, SEEK_SET);
            }

            if (ret.error == -1)
                goto decompress_and_build_index_error;

            if (here->bits) {
                int i;
                i = getc(file_in);
                if (i == -1) {
                    ret.error = ferror(file_in) ? Z_ERRNO : Z_DATA_ERROR;
                    goto decompress_and_build_index_error;
                }
                (void)inflatePrime(&strm, here->bits, i >> (8 - here->bits));
            }

        }

        // obtain window and initialize with it zlib's Dictionary
        if (here->window == NULL && here->window_beginning != 0) {
            /* index' window data is not on memory,
            but we have position and size on index file, so we load it now */
            FILE *index_file;
            if ( index->file_name == NULL ||
                strlen(index->file_name) == 0 ) {
                print_to_stderr( VERBOSITY_NORMAL, "Error while opening index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
            }
            if (NULL == (index_file = fopen(index->file_name, "rb")) ||
                0 != fseeko(index_file, here->window_beginning, SEEK_SET)
                ) {
                print_to_stderr( VERBOSITY_NORMAL, "Error while opening index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
            }
            // here->window_beginning = 0; // this is not needed
            if ( here->window_size > 0 &&
                ( NULL == (here->window = malloc(here->window_size)) ||
                 !fread(here->window, here->window_size, 1, index_file) )
                ) {
                print_to_stderr( VERBOSITY_NORMAL, "Error while reading index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
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
                goto decompress_and_build_index_error;
            }
            (void)inflateSetDictionary(&strm, decompressed_window, local_window_size); // (local_window_size may not be WINSIZE)
            free( decompressed_window );
            decompressed_window = NULL;
            window_size = local_window_size;
        }

    } // end if ( NULL != (*built) && (*built)->have > 0 ) {


    // more decisions for extracting uncompressed data
    if ( ( NULL != (*built) && stdin == file_in ) ||
         NULL == (*built) ||
         ( NULL != (*built) && (*built)->index_complete == 0 )
         ) {
        // index available and stdin is used as gzip data input,
        // or no index is available,
        // or index exists but it is incomplete.

        if ( stdin == file_in ) {
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
                        print_to_stderr( VERBOSITY_EXCESSIVE, "offset_in=%llu\n", offset_in );
                    } else {
                        extraction_from_offset_in = 1;
                    }
                } else {
                    // STDIN: size cannot be calculated in advance
                    start_extraction_on_first_depletion = 1;
                }
            }

        } // end if ( stdin == file_in )

    } // end if ( ( NULL != *built && stdin == file_in ) ||
      //          NULL == *built ) ||
      //          ( NULL != (*built) && (*built)->index_complete == 0 ) )

    // Once `-[bL]` options are set,
    // let's pretend this is an usual SUPERVISE_DO_AND_EXTRACT_FROM_TAIL
    if ( true == continue_tailing_on_eof ) {
        // original indx_n_extraction_opts must be stored for later use:
        indx_n_extraction_opts_4_tailing_on_eof = indx_n_extraction_opts;
        indx_n_extraction_opts = SUPERVISE_DO_AND_EXTRACT_FROM_TAIL;
    }

    // decrement offset_in, offset and line_number_offset by actual position:
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
    if ( line_number_offset > 0 ) {
        if ( NULL != here ) {
            // there's an index loaded
            if ( here->line_number > line_number_offset )
                line_number_offset = 0;
            else
                line_number_offset -= here->line_number;
        } else {
            // there's no index available
            line_number_offset --;
        }
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
        print_to_stderr( VERBOSITY_MANIAC, "inflateInit2 = %d\n", ret.error );
        if (ret.error != Z_OK)
            goto decompress_and_build_index_error;
        totin = totout = last = 0;
        totlines = 1;
        index = NULL;               /* will be allocated on first add_point() */

        // previously I trusted that "index" will be allocated on first add_point(),
        // but subsequently more added code depended on an allocated index
        // ( see block "// if required by passed indx_n_extraction_opts option, extract to stdout:",
        // calls to give_me_number_of_lines_in_chars() and limitBufferOutput() )
        // so the easiest patch is to create an index right now,
        // independently of extend_index_with_lines values:
        index = create_empty_index();
        if ( index == NULL ) { // Oops!?
            ret.error = Z_MEM_ERROR;
            goto decompress_and_build_index_error;
        }

        if ( extend_index_with_lines > 0 ) {
            // mark index as index_version = 1 to store line numbers when serialize();
            // in order to do this, index must be created now (empty)
            index->index_version = 1;
            // here extend_index_with_lines can be 3 (implicit `-x`)
            if ( extend_index_with_lines == 2 )
                index->line_number_format = 1;
            else {
                index->line_number_format = 0;
                if ( 3 == extend_index_with_lines ) {
                    print_to_stderr( VERBOSITY_EXCESSIVE, "implicit `-x`: 1 (decompress_and_build_index).\n" );
                }
                // end now possible extend_index_with_lines == 3 (implicit `-x`)
                extend_index_with_lines = 1;
            }
        }
    }

    // initialize decompress_in_advance() internal (static) processing variables:
    if ( gzip_stream_may_be_damaged > 0 ) {
        decompress_in_advance( NULL, NULL, NULL,
            totin, CHUNKS_TO_DECOMPRESS_IN_ADVANCE, CHUNKS_TO_LOOK_BACKWARDS,
            indx_n_extraction_opts,
            decompressing_with_gzip_headers,
            end_on_first_proper_gzip_eof,
            0LLU, 0LLU,
            waiting_time,
            lazy_gzip_stream_patching_at_eof,
            expected_first_byte,
            DECOMPRESS_IN_ADVANCE_RESET ); // returned value is always ok when called with this value
    }

    // initialize output window
    strm.next_out = window;
    strm.avail_out = WINSIZE;
    avail_out_0 = strm.avail_out;
    do {
        /* get some compressed data from input file */

        // bgzip-compatible-streams code:
        if ( ret.error == Z_STREAM_END &&
             strm.avail_in > 0 ) { // if strm.avail_in is casually 0, this block of code isn't needed.
            // readjust input data moving it to the beginning of buffer:
            // (strm.next_in floats from input to input+CHUNK)
            memmove( input, strm.next_in, strm.avail_in ); // memmove() is overlapping-safe
        }
        // .................................................
        // note that here strm.avail_in > 0 only if ret.error == Z_STREAM_END
        int strm_avail_in0 = strm.avail_in;
        if ( !feof( file_in ) ) { // on last block, strm.avail_in > 0 is possible with feof(file_in)==1 already!
            strm.avail_in = fread(input + strm_avail_in0, 1, CHUNK - strm_avail_in0, file_in);
            print_to_stderr( VERBOSITY_MANIAC, "[read %d B]", strm.avail_in );
            strm.avail_in += strm_avail_in0;
        }
        if ( feof( file_in ) ) {
            // generic check for growing files (not related to bgzip-compatible-streams code):
            // check that file hasn't shrunk, what would mean that the file
            // has been overwritten from the beginning (possible with rsyslog logs, for example)
            if ( strlen( file_name ) > 0 &&  // this check cannot be done on STDIN
                 expected_first_byte == 1LLU // this check must not be done if using `-n` (because then st.st_size < totin in general)
            ) {
                struct stat st;
                stat(file_name, &st);
                print_to_stderr( VERBOSITY_NUTS, "(%llu<%llu?)", st.st_size + GZIP_HEADER_SIZE_BY_ZLIB, totin );
                if ( (uint64_t)( st.st_size + GZIP_HEADER_SIZE_BY_ZLIB ) < totin ) {
                    // file has shrunk! so do a correct finish of the action_create_index (whatever it is)
                    // (Note that it is not possible that this condition arises when accessing a truncated gzip
                    // file with its corresponding (not-truncated) complete index file, because in this case
                    // fseeko() previously failed. Also if gzip-file size is 0 (which is possible and allowed)
                    // this condition won't arise because (10 < 0) is false (index doesn't exist) and
                    // (10 < 10) is false (index contains always the first access point))
                    print_to_stderr( VERBOSITY_EXCESSIVE, "\nDetected '%s' gzip file overwriting", file_name );
                    if ( SUPERVISE_DO_AND_EXTRACT_FROM_TAIL != indx_n_extraction_opts ||
                         0 != write_index_to_disk
                       ) {
                        print_to_stderr( VERBOSITY_EXCESSIVE, ", so ending process" );
                    }
                    print_to_stderr( VERBOSITY_EXCESSIVE, "\n" );
                    ret.error = EXIT_FILE_OVERWRITTEN;
                    break;
                }
            }
        }
        // .................................................
        // decompressor state MUST be reinitiated after Z_STREAM_END
        if ( ret.error == Z_STREAM_END ) {
            print_to_stderr( VERBOSITY_MANIAC, "Reinitializing zlib strm data @%llu...\n", totin );
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
                goto decompress_and_build_index_error;
            strm.avail_in = strm_avail_in0;
            // it is compulsory to reinitiate also output data:
            strm.avail_out = WINSIZE;
            strm.next_out = window;
            avail_out_0 = strm.avail_out;
            window_size = 0;
        }
        // end of bgzip-compatible-streams code

        avail_in_0 = strm.avail_in;

        print_to_stderr( VERBOSITY_CRAZY, "output_data_counter=%llu,totin=%llu,totout=%llu,totlines=%llu,ftello=%llu,avail_in=%d\n",
            output_data_counter, totin, totout, totlines, ftello(file_in), strm.avail_in );

        if ( (indx_n_extraction_opts == SUPERVISE_DO ||
              indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
              indx_n_extraction_opts == EXTRACT_TAIL ) &&
             strm.avail_in == 0 ) {

            // check conditions to start output of uncompressed data
            print_to_stderr( VERBOSITY_CRAZY, ">>> %d, %d, %d",
                extraction_from_offset_in, start_extraction_on_first_depletion, indx_n_extraction_opts );
            if ( start_extraction_on_first_depletion == 1 ) {
                start_extraction_on_first_depletion = 0;

                // output uncompressed data
                unsigned have = avail_out_0 - strm.avail_out;
                if ( have == 0 ) {
                    if ( window2_size > 0 ) {
                        // if we have previous data to show, show it, because now we're out of fresh data!
                        output_data_counter += window2_size;
                        print_to_stderr( VERBOSITY_MANIAC, "[..>%d]", window2_size );
                        if (fwrite(window2, 1, window2_size, file_out) != window2_size || ferror(file_out)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            fflush(file_out);
                            goto decompress_and_build_index_error;
                        }
                    } else {
                        // file has size 0 and hasn't (still) grown, so retain state
                        start_extraction_on_first_depletion = 1;
                    }
                } else {
                    output_data_counter += have;
                    print_to_stderr( VERBOSITY_MANIAC, "[.>%d]", have );
                    if (fwrite(strm.next_out, 1, have, file_out) != have || ferror(file_out)) {
                        (void)inflateEnd(&strm);
                        ret.error = Z_ERRNO;
                        fflush(file_out);
                        goto decompress_and_build_index_error;
                    }
                }
                fflush(file_out);

                // continue extracting data as usual,
                offset = 0;
                offset_in = 0;
                line_number_offset = 0;
                // though as indx_n_extraction_opts != EXTRACT_FROM_* it'll
                // patiently waits if data exhausts.

            }

            if ( indx_n_extraction_opts == EXTRACT_TAIL ) {
                // the process ends here as all required data has been output
                // (index remains incomplete)
                ret.error = Z_OK;
                if ( NULL != index ) {
                    ret.value = index->have;
                }
                goto decompress_and_build_index_error;
            }

            // sleep and retry
            sleep( waiting_time );
            clearerr( file_in );
            continue;

        }

        if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
             indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
             indx_n_extraction_opts == EXTRACT_FROM_LINE ||
             indx_n_extraction_opts == EXTRACT_TAIL ) {
            // with not Supervising options, strm.avail_in == 8 + eof is equivalent to Correct END OF GZIP at EOF
            if ( feof( file_in ) && strm.avail_in == 8 ) {
                print_to_stderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected at EOF-8.\n" );
                break;
            }
        }

        if (ferror(file_in)) {
            ret.error = Z_ERRNO;
            print_to_stderr( VERBOSITY_EXCESSIVE, "ERROR: while reading file at %llu Byte\n", totin );
            goto decompress_and_build_index_error;
        }
        if (strm.avail_in == 0) {
            ret.error = Z_DATA_ERROR;
            print_to_stderr( VERBOSITY_EXCESSIVE, "ERROR: abrupt EOF at %llu Byte\n", totin );
            goto decompress_and_build_index_error;
        }

        // Set strm.next_in to input buffer.
        // Note that if strm_avail_in_decrement == 0, strm.avail_in and totin don't change.
        if ( strm_avail_in_decrement > 0LLU ) {
            print_to_stderr( VERBOSITY_MANIAC, "Managing tail after Z_STREAM_END @%llu (%d, %llu)\n",
                totin, strm.avail_in, strm_avail_in_decrement );
        }
        if ( strm.avail_in > strm_avail_in_decrement ) {
            strm.next_in = input + strm_avail_in_decrement;
            totin += strm_avail_in_decrement;
            strm.avail_in -= strm_avail_in_decrement;
            // after using strm_avail_in_decrement it must be set to zero
            strm_avail_in_decrement = 0LLU;
        } else {
            strm.next_in = input + strm.avail_in;
            totin += strm.avail_in;
            strm_avail_in_decrement -= strm.avail_in;
            // after using strm.avail_in it must be set to zero
            strm.avail_in = 0;
        }

        //
        // INSERTION POINT FOR GZIP RECOVERY CODE (gzip_stream_may_be_damaged)
        // IS PROBABLY HERE
        //
        if ( gzip_stream_may_be_damaged > 0 &&
             decompress_until_this_point_type != GZIP_MARK_BEGINNING &&
             decompress_until_this_point_type != GZIP_MARK_FULL_FLUSH &&
             decompress_until_this_point_byte == 0LLU ) {
            struct returned_output ret;
            ret = decompress_in_advance( (z_streamp)&strm, file_in, file_name, totin, 0, 0, 0, 0, 0,
                    output_data_counter, totout, 0, false, 0LLU,
                    DECOMPRESS_IN_ADVANCE_CONTINUE );
            switch ( ret.error ) {
                case GZIP_MARK_NONE:
                    // no error was found in advance: proceed as usual
                    break;
                case GZIP_MARK_ERROR:
                    // and unrecoverable gzip error was found or
                    // a processing error raised which makes recovery impossible:
                    // progress as usual, as nothing more can be done.
                    print_to_stderr( VERBOSITY_NORMAL,
                        "PATCHING: GZIP errors cannot be fixed.\n" );
                    // set these values to avoid entering this loop again:
                    decompress_until_this_point_byte = ret.value;
                    decompress_until_this_point_type = ret.error;
                    // (optionally) free strm structure from internal state of decompress_in_advance()
                    // as it won't be called again because an error will rise eventually on caller:
                    decompress_in_advance( NULL, NULL, NULL,
                        0LLU, CHUNKS_TO_DECOMPRESS_IN_ADVANCE, CHUNKS_TO_LOOK_BACKWARDS,
                        0, 0, 0, 0LLU, 0LLU, 0, false, 0LLU, DECOMPRESS_IN_ADVANCE_RESET ); // returned value is always ok when called with this value
                    break;
                case GZIP_MARK_FATAL_ERROR:
                    // an internal and compulsory fseeko() failed, so process
                    // MUST stop here, as read won't be correct at this ftello(file_in):
                    print_to_stderr( VERBOSITY_NORMAL,
                        "PATCHING ERROR: Fatal Read error of input file obliges to stop process.\n" );
                    ret.error = Z_DATA_ERROR;
                    goto decompress_and_build_index_error;
                case GZIP_MARK_FULL_FLUSH:
                    print_to_stderr( VERBOSITY_NORMAL,
                        "PATCHING: New valid gzip full flush found @ %llu Byte.\n", ret.value );
                    // a new GZIP-stream beginning mark was found:
                    decompress_until_this_point_byte = ret.value;
                    decompress_until_this_point_type = ret.error;
                    break;
                case GZIP_MARK_BEGINNING:
                    if ( GZIP_MARK_BEGINNING == ret.error ) {
                        print_to_stderr( VERBOSITY_NORMAL,
                            "PATCHING: New valid gzip stream beginning found @ %llu Byte.\n", ret.value );
                    }
                    // a new GZIP-stream beginning mark was found:
                    decompress_until_this_point_byte = ret.value;
                    decompress_until_this_point_type = ret.error;
                    break;
            }
        }

        /* process all of strm.next_in (size strm.avail_in), or until end of stream */
        do {

            /* reset sliding window if necessary */
            if (strm.avail_out == 0) {
                strm.avail_out = WINSIZE;
                strm.next_out = window;
                avail_out_0 = strm.avail_out;
            }

            // input data MUST (also) be checked against decompress_until_this_point_byte
            // BEFORE inflate() in order to assure
            // that no invalid data is fed to the process !!!
            // ( The other point of checking is [C]. )
            if ( gzip_stream_may_be_damaged > 0 &&
                 decompress_until_this_point_byte > 0LLU &&
                 ( GZIP_MARK_FULL_FLUSH == decompress_until_this_point_type ||
                   GZIP_MARK_BEGINNING  == decompress_until_this_point_type ) &&
                 ( totin + strm.avail_in ) >= decompress_until_this_point_byte ) {
                if ( totin > decompress_until_this_point_byte ) {
                    // inflate() process cannot continue as we are well beyond the
                    // estimated good point of reentry due to a forthcoming gzip error,
                    // so we must diverge to set a new entire inflateInit():
                    // ( This goto point (which I know it'd not exist) must be inside this
                    // do { ... } while ( strm.avail_in != 0 ); cycle
                    // not to disturb (too much) the proper progression of the algorithm )
                    print_to_stderr( VERBOSITY_MANIAC,
                        "\nPATCHING: Reinitiating decompression point backwards.\n" );
                    goto decompress_and_build_index_Set_next_good_decompression_point;
                } else {
                    // We are still in the good data buffer, but we must process with
                    // caution and chop the entry before the next good reentry point, so
                    // cut input data to just the byte before decompress_until_this_point_byte
                    print_to_stderr( VERBOSITY_MANIAC,
                        "\nPATCHING: Adjusting size of decompression buffer from %d", strm.avail_in );
                    strm.avail_in -= ( totin + strm.avail_in ) - decompress_until_this_point_byte - 1;
                    print_to_stderr( VERBOSITY_MANIAC, " to %d.\n", strm.avail_in );
                }
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
            // count lines in this decompressed chunk
            if ( NULL != index && // if lines must be counted, index passed must be !=NULL
                                  // to mark index->index_version == 1
                 index->index_version == 1 ) {

                have_lines = give_me_number_of_lines_in_chars(
                    // output data is in window + an offset
                    window + (WINSIZE - avail_out_0),
                    avail_out_0 - strm.avail_out,
                    0, index->line_number_format,
                    &there_are_more_chars
                );

                totlines += have_lines;

            }

            // maintain a backup window for the case of sudden Z_STREAM_END
            // and indx_n_extraction_opts == *_TAIL
            if ( output_data_counter == 0 &&
                 ( NULL == index || index->index_complete == 0 ) &&
                 ( indx_n_extraction_opts == EXTRACT_TAIL ||
                   indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) ) {
                if ( avail_out_0 - strm.avail_out > 0 ) { // if have == 0, maintain previous (data and) window2_size value
                    window2_size = WINSIZE - strm.avail_out;
                    print_to_stderr( VERBOSITY_NUTS, "(w2s=%d)", window2_size );
                    if ( window2_size <= WINSIZE )
                        memcpy( window2, window, window2_size );
                }
                // TODO: change to pointer flip instead of memcpy (possible?)
            }

            // .................................................
            // treat possible gzip tail:
            if ( ret.error == Z_STREAM_END &&
                 // strm.avail_in >= 8 && // strm.avail_in may be less than the required 8 bytes for a tail
                                          // this case must also be treated because inflate() would raise an error
                                          // if initiated in the middle of a tail
                 decompressing_with_gzip_headers == 0 ) {
                // discard this data as it is probably the 8 gzip-tail bytes (4 CRC + 4 size%2^32)
                // and zlib is not able to consume it if inflateInit2(&strm, -15) (raw decompressing)
                if ( strm.avail_in < 8 ) {
                    strm.next_in += strm.avail_in;
                    totin += strm.avail_in; // data is discarded from strm input, but it MUST be counted in total input
                    strm_avail_in_decrement = 8 - strm.avail_in;
                    strm.avail_in = 0;
                } else {
                    strm.next_in += 8;
                    totin += 8; // data is discarded from strm input, but it MUST be counted in total input
                    strm.avail_in -= 8;
                }
                if ( offset_in >= 8 )
                    offset_in -= 8; // data is discarded from strm input, and also from offset_in
                else
                    offset_in = 0;
                print_to_stderr( VERBOSITY_EXCESSIVE,
                    "END OF GZIP passed @%llu while in raw mode (totout=%llu, avail_in=%d, ftello=%llu)\n",
                    totin, totout, strm.avail_in, ftello(file_in) );
                if ( end_on_first_proper_gzip_eof == 1 ||
                    ( ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                    indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                    indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                    indx_n_extraction_opts == EXTRACT_TAIL ) && feof( file_in ) ) ) {
                    gzip_eof_detected = 0; // to exit outer loop with gzip_eof_detected == 0
                } else {
                    gzip_eof_detected = 1;
                }
                break;
                // avail_in_0 doesn't need to be decremented as it tries to count raw stream input bytes
            }
            // end of treat possible gzip tail
            // .................................................

            if ( ret.error != Z_OK ) {
                if ( ret.error != Z_STREAM_END ) {
                    print_to_stderr( VERBOSITY_EXCESSIVE,
                        "ERR %d: totin=%llu, totout=%llu, ftello=%llu\n",
                            ret.error, totin, totout, ftello(file_in) );
                } else {
                    print_to_stderr( VERBOSITY_MANIAC,
                        "Z_STREAM_END: totin=%llu, totout=%llu, ftello=%llu\n",
                            totin, totout, ftello(file_in) );
                }
            }

            if (ret.error == Z_NEED_DICT) {
                ret.error = Z_DATA_ERROR;
            }

            if (ret.error == Z_MEM_ERROR || ret.error == Z_DATA_ERROR) {
                print_to_stderr( VERBOSITY_EXCESSIVE,
                    "ERROR: compressed data error @%llu.\n", totin );
                goto decompress_and_build_index_error;
            }

            if (ret.error == Z_STREAM_END) {
                if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                     indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                     indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                     indx_n_extraction_opts == EXTRACT_TAIL ) {
                    // with not Supervising options, a Z_STREAM_END at feof() is correct!
                    if ( feof( file_in ) && strm.avail_in == 0 ) {
                        print_to_stderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected at EOF.\n" );
                        gzip_eof_detected = 0; // to exit loop, as "gzip_eof_detected == 1" is one ORed condition
                                               // and now this variable is not needed anymore.
                        break;
                    }
                }
                if ( end_on_first_proper_gzip_eof == 0 )
                    gzip_eof_detected = 1;
                if ( gzip_eof_detected == 0 ) {
                    if ( end_on_first_proper_gzip_eof == 1 &&
                         feof( file_in ) ) {
                        gzip_eof_detected = 0;
                    } else {
                        gzip_eof_detected = 1;
                        print_to_stderr( VERBOSITY_NORMAL, "Warning: GZIP end detected in the middle of data: deactivating `-E`\n" );
                        end_on_first_proper_gzip_eof = 0;
                    }
                }
                if ( offset_in >= 8 ) {
                    offset_in -= 8; // data is discarded from strm input, and also from offset_in
                } else {
                    offset_in = 0;
                }
                break;
            }

            //
            // if required by passed indx_n_extraction_opts option, extract to stdout:
            //
            // EXTRACT_FROM_BYTE: extract all:
            if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                 indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                 continue_tailing_on_eof == true ) {

                // end processing based on range_number_of_bytes or range_number_of_lines:
                bool end_processing_because_of_range = false;

                if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                     ( continue_tailing_on_eof == true &&
                       indx_n_extraction_opts_4_tailing_on_eof == EXTRACT_FROM_BYTE )
                    ) {
                    unsigned have = avail_out_0 - strm.avail_out;
                    print_to_stderr( VERBOSITY_NUTS, "[>1>%llu,%d,%d,%d]", offset, have, strm.avail_out, strm.avail_in );
                    if ( offset > have ) {
                        offset -= have;
                    } else {
                        if ( ( offset > 0 && offset <= have ) ||
                            offset == 0 ) {
                            // print have - offset bytes
                            // If offset==0 (from offset byte on) this prints always all bytes:
                            output_data_counter += have - offset;
                            /*
                             * also count lines, if necessary:
                             */
                            if ( offset != 0 &&   // Count lines ONLY in offset block, not in all window.
                                 NULL != index && // If lines must be counted, index passed must be !=NULL
                                                  // to mark index->index_version == 1
                                                  // and use index->line_number_format
                                 index->index_version == 1 ) {
                                have_lines = give_me_number_of_lines_in_chars(
                                    // output data is in window + an offset
                                    window + offset + (WINSIZE - avail_out_0), have - offset,
                                    0, index->line_number_format,
                                    NULL
                                );
                            }
                            // if index->index_version == 0 then output_lines_counter is simply not used
                            output_lines_counter += have_lines;
                            /*
                             * end of "also count lines, if necessary"
                             */
                            // limit buffer output based on range_number_of_bytes or range_number_of_lines,
                            // if necessary:
                            end_processing_because_of_range = limitBufferOutput(
                                                                &range_number_of_bytes, &range_number_of_lines, &have, offset,
                                                                &output_data_counter, &output_lines_counter, &have_lines,
                                                                window + offset + (WINSIZE - avail_out_0),
                                                                index->line_number_format );
                            print_to_stderr( VERBOSITY_CRAZY, "[>1>%d]", have - offset );
                            if (fwrite(window + offset + (WINSIZE - avail_out_0), 1, have - offset, file_out) != (have - offset) ||
                                ferror(file_out)) {
                                (void)inflateEnd(&strm);
                                ret.error = Z_ERRNO;
                                goto decompress_and_build_index_error;
                            }
                            offset = 0;
                            fflush(file_out);
                        }
                    }
                    avail_out_0 = strm.avail_out;
                } else {
                    // indx_n_extraction_opts == EXTRACT_FROM_LINE
                    unsigned have = avail_out_0 - strm.avail_out;
                    print_to_stderr( VERBOSITY_NUTS, "[>3>%llu,%d,%d,%d]",
                        line_number_offset, have_lines, strm.avail_out, strm.avail_in );
                    if ( line_number_offset > have_lines ) {
                        line_number_offset -= have_lines;
                    } else {
                        if ( ( line_number_offset > 0 && line_number_offset <= have_lines ) ||
                            line_number_offset == 0 ) {
                            output_lines_counter += have_lines - line_number_offset;
                            // print have_lines - line_number_offset bytes
                            print_to_stderr( VERBOSITY_CRAZY, "[>3>%d]", have_lines - line_number_offset );
                            // If line_number_offset==0 (from line_number_offset byte on) this prints always all bytes:
                            if ( line_number_offset != 0 ) {
                                // calculate at which byte to start fwrite output:
                                offset = give_me_number_of_lines_in_chars(
                                    window + (WINSIZE - avail_out_0), avail_out_0 - strm.avail_out,
                                    line_number_offset, index->line_number_format,
                                    NULL
                                );
                                if ( // give_me_number_of_lines_in_chars() only if range_number_of_* > 0
                                     // just to spare some CPU cycles (as limitBufferOutput() doesn't
                                     // change anything if ! range_number_of_* > 0)
                                     range_number_of_bytes > 0 ||
                                     range_number_of_lines > 0 ) {
                                    have_lines = give_me_number_of_lines_in_chars(
                                                    window + offset + (WINSIZE - avail_out_0),
                                                    have - offset, 0,
                                                    index->line_number_format, NULL );
                                }
                            } else {
                                offset = 0;
                            }
                            output_data_counter += have - offset;
                            // limit buffer output based on range_number_of_bytes or range_number_of_lines,
                            // if necessary:
                            end_processing_because_of_range = limitBufferOutput(
                                                                &range_number_of_bytes, &range_number_of_lines, &have, offset,
                                                                &output_data_counter, &output_lines_counter, &have_lines,
                                                                window + offset + (WINSIZE - avail_out_0),
                                                                index->line_number_format );
                            if (fwrite(window + offset + (WINSIZE - avail_out_0), 1, have - offset, file_out) != (have - offset) ||
                                ferror(file_out)) {
                                (void)inflateEnd(&strm);
                                ret.error = Z_ERRNO;
                                goto decompress_and_build_index_error;
                            }
                            line_number_offset = 0;
                            offset = 0;
                            fflush(file_out);
                        }
                    }
                    avail_out_0 = strm.avail_out;
                }

                // end process if end_processing_because_of_range == true
                if ( true == end_processing_because_of_range ) {
                    goto decompress_and_build_index_error;
                }

            } else {
                // extraction_from_offset_in marks the use of "offset_in"
                if ( extraction_from_offset_in == 1 ) {
                    unsigned have = avail_out_0 - strm.avail_out;
                    unsigned have_in = avail_in_0 - strm.avail_in;
                    avail_in_0 = strm.avail_in;
                    print_to_stderr( VERBOSITY_NUTS, "[>2>%llu,%d,%d]", offset_in, have_in, strm.avail_in );
                    if ( ( offset_in > 0 && offset_in <= have_in ) ||
                        offset_in <= 0 ) {
                        // print all "have" bytes as with offset_in it is not possible
                        // to know how much output discard (uncompressed != compressed)
                        output_data_counter += have;
                        print_to_stderr( VERBOSITY_CRAZY, "[>2>%d]", have );
                        if (fwrite(window + (WINSIZE - avail_out_0), 1, have, file_out) != have ||
                            ferror(file_out)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            goto decompress_and_build_index_error;
                        }
                        offset_in = 0;
                        fflush(file_out);
                    } else {
                        offset_in -= have_in;
                    }
                }
                avail_out_0 = strm.avail_out;
            }

decompress_and_build_index_Set_next_good_decompression_point:
            // Check decompress_until_this_point_byte to see if decompression has reached
            // or surpassed the point, and restart the strm structure again in this case.
            //
            // Gzip-reinitiation check MUST be done BEFORE adding a new point to the index,
            // because the (possible) point at decompress_until_this_point_byte Byte
            // will be a zero-length window one, so actual window(s) must be emptied.
            //
            if ( ( GZIP_MARK_BEGINNING  == decompress_until_this_point_type ||
                   GZIP_MARK_FULL_FLUSH == decompress_until_this_point_type ) &&
                totin >= decompress_until_this_point_byte ) { // [C]
                // gzip-stream MUST be reinitiated at byte decompress_until_this_point_byte
                // in order to avoid an error after the actual unfinished gzip stream
                // previously detected by decompress_in_advance().
                // Reinitiation is complete, so possible strm.avail_in bytes remaining (is
                // this possible?) will be discarded.
                // All counters will remain the same as the stream will be considered "correct".
                totin = decompress_until_this_point_byte; // do not count duplicate input bytes, even if that is what occurred
                print_to_stderr( VERBOSITY_MANIAC, "Reinitializing zlib strm data @%llu...\n", totin );
                (void)inflateEnd(&strm);
                strm.zalloc = Z_NULL;
                strm.zfree = Z_NULL;
                strm.opaque = Z_NULL;
                strm.avail_in = 0;
                strm.next_in = Z_NULL;
                if ( GZIP_MARK_FULL_FLUSH == decompress_until_this_point_type ) {
                    // raw inflate
                    ret.error = inflateInit2(&strm, -15);
                    decompressing_with_gzip_headers = 0;
                } else {
                    // automatic zlib or gzip decoding (15 + automatic header detection)
                    ret.error = inflateInit2(&strm, 47);
                    decompressing_with_gzip_headers = 1;
                }
                if ( Z_OK != ret.error ) {
                    goto decompress_and_build_index_error;
                }
                strm.avail_in = 0; // == 0 in order to oblige
                                   // to begin a new cycle with fread()
                                   // at the end of this cycle
                                   // "do{...}while ( strm.avail_in != 0 );".
                                   // This will also set strm.next_in = input;
                                   // at its proper time.
                strm.next_out = window;
                strm.avail_out = WINSIZE;
                // adjacent counters:
                avail_out_0 = strm.avail_out;
                window_size = 0;
                avail_in_0 = strm.avail_in; // 0

                //
                // set ret.error to a Z_OK value, to always force a new cycle
                // in the cycle
                // "do{...} while ( ret.error != Z_STREAM_END || strm.avail_in > 0 || gzip_eof_detected == 1 );".
                // (I think here it should be Z_OK always, but who knows!)
                //
                ret.error = Z_OK;
                //
                // last step is to ensure that file_in pointer is set to next gzip valid byte:
                //
                if ( 0 != fseeko( file_in, decompress_until_this_point_byte, SEEK_SET ) ) {
                    goto decompress_and_build_index_error;
                }
                //
                // and reset decompress_until_this_point_* variables:
                //
                decompress_until_this_point_byte = 0LLU;
                decompress_until_this_point_type = GZIP_MARK_NONE;
                //
                // and reset decompress_in_advance() internal state
                //
                decompress_in_advance( NULL, NULL, NULL,
                    totin, CHUNKS_TO_DECOMPRESS_IN_ADVANCE, CHUNKS_TO_LOOK_BACKWARDS,
                    indx_n_extraction_opts,
                    decompressing_with_gzip_headers,
                    end_on_first_proper_gzip_eof,
                    0LLU, 0LLU,
                    0,
                    false,
                    0LLU,
                    DECOMPRESS_IN_ADVANCE_SOFT_RESET ); // returned value is always ok when called with this value
                //
                // all done!
                //
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
                (totout == 0 || ((totout - last) > span)) &&
                (totout == 0 || // first index point must be set always
                    ( !adjust_index_points_to_byte_boundary ||
                      (strm.data_type & 7) == 0 // only create an index point in a "good" byte boundary
                                                // (no bits cut before index point)
                    ))
                ) {

                // check actual_index_point to see if we've passed
                // the end of the passed previous index, and so
                // we must add_point() from now on :
                print_to_stderr( VERBOSITY_MANIAC, "actual_index_point = %llu,", actual_index_point+1 );
                print_to_stderr( VERBOSITY_MANIAC, "(%llu, %llu)\n", totout, last );
                if ( actual_index_point > 0 )
                    ++actual_index_point;
                if ( NULL != index &&
                    actual_index_point > (index->have - 1) ) {
                    actual_index_point = 0; // 0: actual_index_point won't be incremented or checked anymore
                }
                if ( actual_index_point == 0 &&
                    // add_point() only if index doesn't yet exist or it is incomplete
                    ( NULL == index || index->index_complete == 0 ) &&
                    // do not add points if position is lower than the last index point available:
                    // (this can happen if -s is less now, than when the index was created!)
                    ( NULL == index ||
                       index->have == 0 || // if index->index_version == 1 index has been previously created (!=NULL)
                       ( index->have > 0 &&
                        index->list[index->have -1].in < totin &&
                        (index->list[index->have -1].out + span) <= totout ) )
                    ) {

                    if ( write_index_to_disk == 1 ) { // if `-W`, index is not written to disk, and it will also not be created/updated (!)

                        if ( NULL != index )
                            print_to_stderr( VERBOSITY_EXCESSIVE, "add_point index->have = %llu, index_last_written_point = %llu\n",
                                index->have, index_last_written_point );
                        else
                            print_to_stderr( VERBOSITY_EXCESSIVE, "add_point index->have = 0, index_last_written_point = %llu\n",
                                index_last_written_point );
                        index = add_point(index, strm.data_type & 7, totin,
                                         totout, strm.avail_out, window, window_size, totlines, 1);
                        if ( NULL == index ) {
                            ret.error = Z_MEM_ERROR;
                            goto decompress_and_build_index_error;
                        }

                        // write added point!
                        // note that points written are automatically emptied of its window values
                        // in order to use as less memory as possible
                        if ( ! serialize_index_to_file( index_file, index, index_last_written_point ) )
                            goto decompress_and_build_index_error;

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

        print_to_stderr( VERBOSITY_EXCESSIVE, "last extraction: %d\n", have );

        if ( have > 0 ) {
            print_to_stderr( VERBOSITY_EXCESSIVE, "[.L>%d]", have );
            if (fwrite(strm.next_out, 1, have, file_out) != have || ferror(file_out)) {
                ret.error = Z_ERRNO;
            }
            output_data_counter += have;
        } else {
            // use backup window
            print_to_stderr( VERBOSITY_EXCESSIVE, "[..L>%d]", window2_size );
            if (fwrite(window2, 1, window2_size, file_out) != window2_size || ferror(file_out)) {
                ret.error = Z_ERRNO;
            }
            output_data_counter += window2_size;
        }

        fflush(file_out);

    }

    /* clean up */
    (void)inflateEnd(&strm);
    /* and return index (release unused entries in list) */
    if ( NULL != index &&
         index->have > 0 ) {
        struct point *next = realloc(index->list, sizeof(struct point) * index->have);
        if ( NULL == next ) {
            ret.error = Z_MEM_ERROR;
            ret.value = 0;
            print_to_stderr( VERBOSITY_EXCESSIVE, "Z_MEM_ERROR\n" );
            goto decompress_and_build_index_error;
        }
        index->list = next;
        index->size = index->have;
        index->uncompressed_file_size = totout; /* size of uncompressed file (useful for bgzip files) */
        if ( 0 == there_are_more_chars && totlines > 1 ) {
            totlines --;
        }
        index->number_of_lines = totlines; /* lines in uncompressed file */
    }

    // once all index values are filled, close index file: a last call must be done
    // with index_last_written_point = index->have
    if ( NULL != index &&
         index->index_complete == 0 &&
         write_index_to_disk == 1 ) {
        // use markers to detect index updates and to not write if index isn't updated
        if ( index_points_0 != index->have ||
             index_file_size_0 != index->uncompressed_file_size ) {
            print_to_stderr( VERBOSITY_EXCESSIVE,
                "Closing index file with %llu points and uncompressed file size of %llu bytes.\n",
                index->have, index->uncompressed_file_size );
            if ( ! serialize_index_to_file( index_file, index, index->have ) )
                goto decompress_and_build_index_error;
            if ( strlen(index_filename) > 0 )
                print_to_stderr( VERBOSITY_NORMAL, "Index written to '%s'.\n", index_filename );
            else
                print_to_stderr( VERBOSITY_NORMAL, "Index written to STDOUT.\n" );
            /* index is now complete */
        }
    }

    if ( NULL != index_file )
        fclose(index_file);

    // print output_data_counter info
    if ( output_data_counter > 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data extracted.\n",
            give_me_si_units(output_data_counter, 1), output_data_counter );
    }

    // print output_lines_counter info
    if ( ( indx_n_extraction_opts == EXTRACT_FROM_LINE ||
           ( NULL != index && index->index_version > 0) ) &&
        output_lines_counter > 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "%llu lines extracted", output_lines_counter );
        if ( output_lines_counter > 1000 ) {
            print_to_stderr( VERBOSITY_NORMAL, " (%s).\n", give_me_si_units(output_lines_counter, 0) );
        } else {
            print_to_stderr( VERBOSITY_NORMAL, ".\n" );
        }
    }

    // free strm structure from internal state of decompress_in_advance():
    decompress_in_advance( NULL, NULL, NULL,
        0LLU, 0, 0, 0, 0, 0, 0LLU, 0LLU, 0, false, 0LLU, DECOMPRESS_IN_ADVANCE_RESET ); // returned value is always ok when called with this value

    *built = index;
    if ( NULL != index ) {
        ret.value = index->have;
    } else {
        ret.value = 0;
    }
    return ret;

    /*
     * return error point,
     * or point of exit when end_processing_because_of_range == true
     */
  decompress_and_build_index_error:

    // free strm structure from internal state of decompress_in_advance():
    decompress_in_advance( NULL, NULL, NULL,
        0LLU, 0, 0, 0, 0, 0, 0LLU, 0LLU, 0, false, 0LLU, DECOMPRESS_IN_ADVANCE_RESET ); // returned value is always ok when called with this value

    // print output_data_counter info
    if ( output_data_counter > 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data extracted.\n",
            give_me_si_units(output_data_counter, 1), output_data_counter );
    }

    // print output_lines_counter info
    if ( ( indx_n_extraction_opts == EXTRACT_FROM_LINE ||
           ( NULL != index && index->index_version > 0) ) &&
        output_lines_counter > 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "%llu lines extracted", output_lines_counter );
        if ( output_lines_counter > 1000 ) {
            print_to_stderr( VERBOSITY_NORMAL, " (%s).\n", give_me_si_units(output_lines_counter, 0) );
        } else {
            print_to_stderr( VERBOSITY_NORMAL, ".\n" );
        }
    }

    (void)inflateEnd(&strm);

    if ( always_create_a_complete_index == 1 &&
         NULL != index ) {
        index->uncompressed_file_size = totout; /* size of uncompressed file (useful for bgzip files) */
        if ( 0 == there_are_more_chars && totlines > 1 ) {
            totlines --;
        }
        index->number_of_lines = totlines; /* lines in uncompressed file */
        // return index pointer and write index to index file, ignoring the decompression error
        *built = index;
        if ( ! serialize_index_to_file( index_file, index, index->have ) )
            print_to_stderr( VERBOSITY_NORMAL, "ERROR whilst writing index file '%s'.\n", index_file );
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

// See gztool_internal.h
struct returned_output compress_and_build_index(
    FILE *file_in, FILE *file_out, int level, char *file_name, uint64_t span,
    struct access **built, char *index_filename, int write_index_to_disk,
    int end_on_first_eof, int always_create_a_complete_index,
    int waiting_time, int extend_index_with_lines )
{
    struct returned_output ret;
    uint64_t last   = 0;           /* uncompressed byte position of last access point */
    struct access *index = NULL;/* access points being generated */
    uint64_t totin  = 0;          // counts uncompressed bytes read from file_in
    uint64_t totout = 0;          // counts compressed bytes deflated to file_out
    uint64_t totlines = 1;        // counts total line numbers in file_in
    int there_are_more_chars = 0; // totlines may need to be decremented at the end depending on last stream char
    int flush = Z_NO_FLUSH;
    unsigned have;
    z_stream strm;
    FILE *index_file = NULL;
    size_t index_last_written_point = 0;
    unsigned char *input;
    unsigned char *output;

    ret.value = 0;
    ret.error = Z_OK;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret.error = deflateInit2( //www.zlib.net/manual.html
                //github.com/madler/zlib/blob/master/zlib.h
                // Add 16 to windowBits to write a simple gzip header and trailer
                // around the compressed data instead of a zlib wrapper
        &strm, level, Z_DEFLATED, 15 + 16,
        8, Z_DEFAULT_STRATEGY);
    if (ret.error != Z_OK)
        return ret;

    input = malloc(CHUNK);
    output = malloc(CHUNK);

    if ( extend_index_with_lines > 0 ) {
        // mark index as index_version = 1 to store line numbers when serialize();
        // in order to do this, index must be created now (empty)
        index = create_empty_index();
        if ( index == NULL ) { // Oops!?
            ret.error = Z_MEM_ERROR;
            return ret;
        }
        index->index_version = 1;
        if ( extend_index_with_lines == 2 )
            index->line_number_format = 1;
        else
            index->line_number_format = 0;
    }

    /* open index_filename for binary writing */
    // index_filename DOES NOT previously exists: this has has been checked on caller
    if ( strlen(index_filename) > 0 ) {
        if ( write_index_to_disk == 1 ) {
            print_to_stderr( VERBOSITY_EXCESSIVE, "write_index_to_disk = %d", write_index_to_disk );
            if ( access( index_filename, F_OK ) != -1 ) {
                // index_filename already exist:
                // Abort, as we would overwrite it:
                print_to_stderr( VERBOSITY_NORMAL, "Index file '%s' already exist. Aborted.\n", index_filename );
                goto compress_and_build_index_error;
            } else {
                // index_filename does not exist:
                index_file = fopen( index_filename, "w+b" );
                print_to_stderr( VERBOSITY_EXCESSIVE, " (w+b)\n" );
            }
        }
    } else {
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        index_file = stdout;
    }
    if ( NULL == index_file && write_index_to_disk == 1 ) {
        print_to_stderr( VERBOSITY_NORMAL, "Could not write index to file '%s'.\n", index_filename );
        goto compress_and_build_index_error;
    }

    /* compress until end of file */
    do {

        // check if a new index point must be created
        if ( // create an index point at uncompressed byte 0 (just before data)
             ( totin == 0 &&
               index_last_written_point == 0 )
             ||
             // create index points after Z_FULL_FLUSH marks on compressed data
             ( totin - last >= span &&
               Z_FULL_FLUSH == flush )
            ) {

            // write index point
            if ( write_index_to_disk == 1 ) { // if `-W`, index is not written to disk, and it will also not be created/updated (!)
                if ( NULL != index )
                    print_to_stderr( VERBOSITY_EXCESSIVE, "add_point index->have = %llu, index_last_written_point = %llu\n",
                        index->have, index_last_written_point );
                else
                    print_to_stderr( VERBOSITY_EXCESSIVE, "add_point index->have = 0, index_last_written_point = %llu\n",
                        index_last_written_point );
                index = add_point(index, 0,
                                 ( ( 0==totout )? GZIP_HEADER_SIZE_BY_ZLIB: totout ), // compressed byte after header <=> uncompressed byte 0
                                 totin, 0, NULL, 0, totlines, 0);

                // write added point!
                // note that points written are automatically emptied of its window values
                // in order to use as less memory a s possible
                if ( ! serialize_index_to_file( index_file, index, index_last_written_point ) )
                    goto compress_and_build_index_error;
            }

            if ( NULL != index )
                index_last_written_point = index->have;

            last = totin;

        }

        strm.avail_in = fread(input, 1, CHUNK, file_in);
        print_to_stderr( VERBOSITY_MANIAC, "[read %d B]", strm.avail_in );
        totin += strm.avail_in;
        // count lines in this source chunk
        if ( extend_index_with_lines > 0 ) {
            totlines += give_me_number_of_lines_in_chars(
                input, strm.avail_in,
                0, extend_index_with_lines - 1,
                &there_are_more_chars
            );
        }

        if ( ferror(file_in) ) {
            (void)deflateEnd(&strm);
            goto compress_and_build_index_error;
        }

        if ( feof( file_in ) ) {
            if ( end_on_first_eof == 1 )
                flush = Z_FINISH;
            else
                flush = Z_SYNC_FLUSH; // Z_SYNC_FLUSH doesn't occupy space
        } else {
            flush = Z_NO_FLUSH;
        }
        if ( totin - last >= span ) {
            // create a FLUSH point for an index point with window of size zero
            // With gzip compression this should not affect compression performance
            flush = Z_FULL_FLUSH;
        }

        strm.next_in = input;

        /* run deflate() on input until output buffer not full, finish
           compression if all of file_in has been read in */
        do {

            strm.avail_out = CHUNK;
            strm.next_out = output;
            print_to_stderr( VERBOSITY_NUTS, "(avail_in=%d),",
                strm.avail_in );
            ret.error = deflate(&strm, flush);    /* no bad return value */
            assert(ret.error != Z_STREAM_ERROR);  /* state not clobbered */

            have = CHUNK - strm.avail_out;
            totout += have;

            print_to_stderr( VERBOSITY_NUTS, "flush=%d,have=%d,avail_out=%d,",
                flush, have, strm.avail_out );
            print_to_stderr( VERBOSITY_CRAZY, "totin=%llu,totout=%llu,ftello=%llu,avail_in=%d,",
                totin, totout, ftello(file_in), strm.avail_in );

            if (fwrite(output, 1, have, file_out) != have || ferror(file_out)) {
                (void)deflateEnd(&strm);
                goto compress_and_build_index_error;
            }

            // flush written content to disk
            //fflush( file_out );

        } while ( strm.avail_out == 0 );
        assert(strm.avail_in == 0);     /* all input will be used */

        // generic check for growing files:
        // check that file hasn't shrunk, what would mean that the file
        // has been overwritten from the beginning (possible with logs, for example)
        if ( feof( file_in ) &&
             end_on_first_eof == 0 ) { // if end_on_first_eof == 1, ret.error == Z_STREAM_END already
            if ( strlen( file_name ) > 0 ) {    // this check cannot be done on STDIN
                struct stat st;
                stat(file_name, &st);
                print_to_stderr( VERBOSITY_NUTS, "(%llu<%llu?)", st.st_size, totin );
                if ( (uint64_t) st.st_size < totin ) {
                    // file has shrunk! so do a correct finish of the action_create_index (whatever it is)
                    // (Note that it is not possible that this condition arises when accessing a file
                    // with size 0 (which is possible and allowed) because (0 < 0) is false )
                    print_to_stderr( VERBOSITY_EXCESSIVE, "\nDetected '%s' file overwriting, so ending process\n", file_name );
                    end_on_first_eof = 1;
                    // loop will continue and because feof() will activate again (because source file
                    // won't be overwritten so quick as to surpase actual total_in bytes) a Z_FINISH
                    // will be written, and a Z_STREAM_END will be returned to end the process.
                }
            }
        }

        if ( end_on_first_eof == 0 &&
             strm.avail_in == 0 &&
             feof( file_in ) ) {
            // flush written content to disk
            fflush( file_out );
            // wait for file to grow
            sleep( waiting_time );
            clearerr( file_in );
        }

        /* done when last data in file processed */

    } while ( flush != Z_FINISH );

    assert(ret.error == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);

    /* and return index (release unused entries in list) */
    if ( NULL != index &&
         index->have > 0 ) {
        struct point *next = realloc(index->list, sizeof(struct point) * index->have);
        if ( NULL == next ) {
            ret.error = Z_MEM_ERROR;
            ret.value = 0;
            print_to_stderr( VERBOSITY_EXCESSIVE, "Z_MEM_ERROR\n" );
            goto compress_and_build_index_error;
        }
        index->list = next;
        index->size = index->have;
        index->uncompressed_file_size = totin; /* size of uncompressed file */
        if ( 0 == there_are_more_chars && totlines > 1 ) {
            totlines --;
        }
        index->number_of_lines = totlines; /* lines in uncompressed file */
    }

    // once all index values are filled, close index file: a last call must be done
    // with index_last_written_point = index->have
    if ( NULL != index &&
         index->index_complete == 0 &&
         write_index_to_disk == 1 ) {
        print_to_stderr( VERBOSITY_EXCESSIVE, "Closing index file with %llu points and uncompressed file size of %llu bytes.\n",
            index->have, index->uncompressed_file_size );
        if ( ! serialize_index_to_file( index_file, index, index->have ) )
            goto compress_and_build_index_error;
        if ( strlen(index_filename) > 0 )
            print_to_stderr( VERBOSITY_NORMAL, "Index written to '%s'.\n", index_filename );
        else
            print_to_stderr( VERBOSITY_NORMAL, "Index written to STDOUT.\n" );
        /* index is now complete */
    }

    if ( NULL != index_file )
        fclose(index_file);

    // print totin info
    if ( totin > 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data compressed.\n",
            give_me_si_units(totin, 1), totin );
        if ( extend_index_with_lines > 0 &&
            totlines > 0 ) {
            print_to_stderr( VERBOSITY_NORMAL, "%llu lines compressed.\n", totlines );
            if ( totlines > 1000 ) {
                print_to_stderr( VERBOSITY_NORMAL, " (%s).\n", give_me_si_units(totlines, 0) );
            } else {
                print_to_stderr( VERBOSITY_NORMAL, ".\n" );
            }
        }
        if ( totout > 0 )
            print_to_stderr( VERBOSITY_NORMAL, "%llu bytes of gzip data (%.2f%%).\n",
                totout, 100.0 - (double)totout / (double)totin * 100.0 );
    }

    *built = index;
    if ( NULL != index )
        ret.value = index->have;
    else
        ret.value = 0;

    free(input);
    free(output);

    return ret;

  compress_and_build_index_error:
    free(input);
    free(output);

    // flush written content to disk
    fflush( file_out );

    // print totin info
    if ( totin > 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data compressed.\n",
            give_me_si_units(totin, 1), totin );
        if ( extend_index_with_lines > 0 &&
            totlines > 0 ) {
            print_to_stderr( VERBOSITY_NORMAL, "%llu lines compressed", ( (0 == there_are_more_chars)? totlines-1: totlines ) );
            if ( totlines > 1000 ) {
                print_to_stderr( VERBOSITY_NORMAL, " (%s).\n", give_me_si_units(totlines, 0) );
            } else {
                print_to_stderr( VERBOSITY_NORMAL, ".\n" );
            }
        }
        if ( totout > 0 )
            print_to_stderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of gzip data (%.2f%%).\n",
                give_me_si_units(totout, 1), totout, 100.0 - (double)totout / (double)totin * 100.0 );
    }

    (void)deflateEnd(&strm);

    if ( always_create_a_complete_index == 1 ) {
        if ( NULL != index ) {
            index->uncompressed_file_size = totin; /* size of uncompressed file */
            if ( 0 == there_are_more_chars && totlines > 1 ) {
                totlines --;
            }
            index->number_of_lines = totlines; /* lines in uncompressed file */
            // return index pointer and write index to index file, ignoring the compression error
            *built = index;
            if ( write_index_to_disk == 1 ) {
                if ( ! serialize_index_to_file( index_file, index, index->have ) )
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR whilst writing index file '%s'.\n", index_file );
            }
        }
    } else {
        if ( NULL != index )
            free_index(index);
        *built = NULL;
    }

    if ( NULL != index_file )
        fclose(index_file);
    if ( Z_OK == ret.error )
        ret.error = Z_ERRNO;
    return ret;

}
