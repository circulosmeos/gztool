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
#include <ctype.h>  // isprint()
#include <sys/stat.h> // stat()
#include <math.h>   // pow()
#include <stdbool.h>// bool, false, true

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

#include "gztool.h"
#include "gztool_internal.h"

#define local static

#define SPAN 10485760L      /* desired distance between access points */

// TODO: Remove duplication
enum VERBOSITY_LEVEL verbosity_level_HACK = VERBOSITY_NORMAL;

char number_output[MAX_GIVEMESIUNITS_RETURNED_LENGTH];

// list info for an index file, to stdout
// May be called with global verbosity_level_HACK == VERBOSITY_NONE in which case
// no info is printed to stdout but a value is returned.
// INPUT:
// char *file_name           : index file name. Maybe "" for STDIN.
// char *input_gzip_filename : gzip file associated with the index. May be NULL, but not "".
// enum VERBOSITY_LEVEL list_verbosity: level of detail of index checking:
//                                      minimum is 1 == VERBOSITY_NORMAL
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_list_info( char *file_name, char *input_gzip_filename, enum VERBOSITY_LEVEL list_verbosity ) {

    FILE *in = NULL;
    struct access *index = NULL;
    uint64_t j;
    int ret_value = EXIT_OK;
    struct stat st;
    char *gzip_filename = NULL;
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
        if ( verbosity_level_HACK > VERBOSITY_NONE )
            fprintf( stdout, "Checking index file '%s' ...\n", file_name );
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            print_to_stderr( VERBOSITY_NORMAL, "Could not open %s for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
    } else {
        // stdin
        print_to_stderr( VERBOSITY_NORMAL, "Checking index from STDIN ...\n" );
        SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
        in = stdin;
    }

    // in case in == stdin, file_name == ""
    // but this doesn't matter as windows won't be inflated from disk, but from memory with 2nd parameter == 1.
    index = deserialize_index_from_file( in, ((list_verbosity==VERBOSITY_MANIAC)?1:0), file_name, 0 );

    if ( NULL != index &&
         strlen( file_name ) > 0 ) {
        stat( file_name, &st );
        if ( verbosity_level_HACK > VERBOSITY_NONE ) {
            fprintf( stdout, "\tSize of index file (v%d",
                index->index_version );
            if ( index->index_version == 1 &&
                 index->line_number_format == 1 ) {
                fprintf( stdout, "\\r):" ); // inform a mac-style new line
            } else {
                fprintf( stdout, ")  :" );
            }
            fprintf( stdout, " %s (%llu Bytes)",
                give_me_si_units(st.st_size, 1), (long long unsigned)st.st_size );
        }

        if ( st.st_size > 0 &&
             list_verbosity > VERBOSITY_NONE ) {
            // try to obtain the name of the original gzip data file
            // to calculate the ratio uncompressed_file_size / gzip_file_size

            if ( NULL == gzip_filename ) {
                gzip_filename = malloc( strlen(file_name) + 1 );
                if ( NULL != gzip_filename ) {
                    sprintf( gzip_filename, "%s", file_name );
                    if ( strlen( file_name ) > 4 && // avoid out-of-bounds
                         (char *)strstr(gzip_filename, ".gzi") ==
                            (char *)(gzip_filename + strlen(file_name) - 4) ) {
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
                         verbosity_level_HACK > VERBOSITY_NONE) {
                        fprintf( stdout, " (%.2f%%/gzip)\n", (double)st.st_size / (double)st_gzip.st_size * 100.0 );
                        fprintf( stdout, "\tGuessed gzip file name   : '%s'", gzip_filename );
                        if ( index->uncompressed_file_size > 0 ) {
                            fprintf( stdout, " (%.2f%%)",
                                100.0 - (double)st_gzip.st_size / (double)index->uncompressed_file_size * 100.0 );
                        }
                        fprintf( stdout, " %s (%llu Bytes)",
                            give_me_si_units(st_gzip.st_size, 1), (long long unsigned)st_gzip.st_size );
                    }
                } else {
                    if ( verbosity_level_HACK > VERBOSITY_NONE )
                        fprintf( stdout, "\n\tIndicated gzip file '%s' doesn't exist", gzip_filename );
                }
            }

        }

        if ( verbosity_level_HACK > VERBOSITY_NONE )
            fprintf( stdout, "\n" );

    }

    if ( ! index ) {

        if ( strlen( file_name ) > 0 ) {
            print_to_stderr( VERBOSITY_NORMAL, "Could not read index from file '%s'.\n", file_name );
        } else {
            print_to_stderr( VERBOSITY_NORMAL, "Could not read index from STDIN.\n" );
        }
        ret_value = EXIT_GENERIC_ERROR;
        goto action_list_info_error;

    } else {

        if ( verbosity_level_HACK > VERBOSITY_NONE ) {
            fprintf( stdout, "\tNumber of index points   : %llu", (long long unsigned)index->have );
            if ( index->have > 1000 ) {
                fprintf( stdout, " (%s)\n", give_me_si_units(index->have, 0) );
            } else {
                fprintf( stdout, "\n" );
            }
        }

        if ( verbosity_level_HACK > VERBOSITY_NONE ) {

            // print uncompressed file size
            if ( index->uncompressed_file_size != 0 ) {
                fprintf( stdout, "\tSize of uncompressed file: %s (%llu Bytes)\n",
                    give_me_si_units(index->uncompressed_file_size, 1), (long long unsigned)index->uncompressed_file_size );
            } else {
                if ( index->have > 1 ) {
                    fprintf( stdout, "\tSize of uncompressed file > %s (%llu Bytes)\n",
                        give_me_si_units(index->list[index->have -1].out, 1),
                        (long long unsigned)index->list[index->have -1].out );
                }
            }

            // print number of lines in uncompressed data
            if ( 1 == index->index_version ) {
                uint64_t local_number_of_lines = 0;
                fprintf( stdout, "\tNumber of lines          " );
                if ( 1 == index->index_complete ) {
                    local_number_of_lines = index->number_of_lines;
                    fprintf( stdout, ":" );
                } else {
                    if ( index->have > 1 ) {
                        local_number_of_lines = index->list[index->have -1].line_number;
                    }
                    fprintf( stdout, ">" );
                }
                fprintf( stdout, " %llu", (long long unsigned)local_number_of_lines );
                if ( local_number_of_lines > 1000 ) {
                    fprintf( stdout, " (%s)", give_me_si_units(local_number_of_lines, 0) );
                }
                fprintf( stdout, "\n" );
            }

            // print compression factor, estimated from available data if index isn't complete
            fprintf( stdout, "\tCompression factor       : " );
            if ( 1 == index->index_complete &&
                 NULL != gzip_filename  // gzip file may be unknown: compression cannot be calculated here
                ) {
                // index is complete
                if ( st_gzip.st_size > 0 &&
                     index->uncompressed_file_size > 0 ) {
                    fprintf( stdout, "%.2f%%\n",
                        100.0 - (double)st_gzip.st_size/(double)index->uncompressed_file_size*100.0 );
                }
            } else {
                // index is incomplete (or gzip file is not available):
                // use last available index point as data for percentage
                if ( index->have > 1 && // index has at least one useful index point
                     index->list[index->have -1].in  > 0 &&
                     index->list[index->have -1].out > 0 ) {
                    fprintf( stdout, "%.2f%%\n",
                        100.0 - (double)index->list[index->have -1].in/(double)index->list[index->have -1].out*100.0 );
                } else {
                    fprintf( stdout, "Not available\n");
                }
            }

        }

        // print and check index points stored in index
        if ( list_verbosity > VERBOSITY_NORMAL ) {
            if ( verbosity_level_HACK > VERBOSITY_NONE ) {
                if ( list_verbosity < VERBOSITY_MANIAC ) {
                    if ( 0 == index->index_version ) {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte (window data size in Bytes @window's beginning at index file) !bits needed from previous byte, ...\n" );
                    } else {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte L#line_number (window data size in Bytes @window's beginning at index file) !bits needed from previous byte, ...\n" );
                    }
                } else {
                    if ( 0 == index->index_version ) {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte (compressed window size / uncompressed window size), ...\n" );
                    } else {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte L#line_number (compressed window size / uncompressed window size), ...\n" );
                    }
                }
                if ( list_verbosity == VERBOSITY_MANIAC ) {
                    fprintf( stdout, "\tChecking compressed windows...\n" );
                }
            }
            for ( j=0; j<index->have; j++ ) {
                if ( list_verbosity == VERBOSITY_EXCESSIVE &&
                     verbosity_level_HACK > VERBOSITY_NONE ) {
                    fprintf( stdout, "#%llu: @ %llu / %llu ",
                        (long long unsigned)(j +1), (long long unsigned)index->list[j].in, (long long unsigned)index->list[j].out );
                    if ( 1 == index->index_version ) // print line number information
                        fprintf( stdout, "L%llu ", (long long unsigned)index->list[j].line_number );
                    fprintf( stdout, "( %d @%llu ) !%d, ", index->list[j].window_size, (long long unsigned)index->list[j].window_beginning, index->list[j].bits );
                    if ( (j + 1) % 5 == 0 ) {
                        fprintf( stdout, "\n" );
                    }
                }
                if ( list_verbosity == VERBOSITY_MANIAC ) {
                    uint64_t local_window_size = index->list[j].window_size; // uint64_t to pass to decompress_chunk()
                    if ( local_window_size > 0 ) {
                        /* window is compressed on memory, so decompress it */
                        unsigned char *decompressed_window = NULL;
                        decompressed_window = decompress_chunk(index->list[j].window, &local_window_size);
                        if ( NULL == decompressed_window ) {
                            print_to_stderr( VERBOSITY_NORMAL,
                                "\nERROR: Could not decompress window #%llu from index file '%s'.\n",
                                j +1, file_name);
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                        free( decompressed_window );
                        decompressed_window = NULL;
                    }
                    if ( verbosity_level_HACK > VERBOSITY_NONE ) {
                        comp_win_counter   += index->list[j].window_size;
                        uncomp_win_counter += local_window_size;
                        fprintf( stdout, "#%llu: @ %llu / %llu ",
                            (long long unsigned)(j +1),
                            (long long unsigned)index->list[j].in, (long long unsigned)index->list[j].out );
                        if ( 1 == index->index_version ) { // print line number information
                            fprintf( stdout, "L%llu ", (long long unsigned)index->list[j].line_number );
                        }
                        fprintf( stdout, "( %d/%llu %.2f%% ), ", index->list[j].window_size,
                            (long long unsigned)local_window_size,
                            ((local_window_size>0)?(100.0 - (double)(index->list[j].window_size) / (double)local_window_size * 100.0):0.0) );
                        if ( (j + 1) % 5 == 0 ) {
                            fprintf( stdout, "\n" );
                        }
                    }
                }
            }
            if ( verbosity_level_HACK > VERBOSITY_NONE )
                fflush( stdout );
        }

        if ( verbosity_level_HACK > VERBOSITY_NONE &&
             list_verbosity == VERBOSITY_MANIAC &&
             uncomp_win_counter > 0 ) {
            fprintf( stdout, "\n\tEstimated gzip data compression factor: %.2f%%",
                100.0 - (double)comp_win_counter/(double)uncomp_win_counter*100.0 );
            if ( NULL != gzip_filename &&
                 st_gzip.st_size > 0 &&
                 index->uncompressed_file_size > 0 ) {
                fprintf( stdout, "\n\tReal gzip data compression factor     : %.2f%%",
                    100.0 - (double)st_gzip.st_size/(double)index->uncompressed_file_size*100.0 );
            }
        }

        if (verbosity_level_HACK > VERBOSITY_NONE )
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
// char *original_input: string containing the data (only read)
// OUTPUT:
// 64 bit long integer number
uint64_t giveMeAnInteger( const char *original_input ) {

    unsigned i = 0;
    char *PowerSuffixes = "kmgtpeKMGTPE";
    unsigned PowerSuffixesLength = strlen(PowerSuffixes)/2;
    char *input = NULL;
    uint64_t result = 1;

    if ( NULL == original_input )
        return 0LLU;

    input = malloc( strlen(original_input) +1 );
    memcpy(input, original_input, strlen(original_input) +1);

    if ( strlen(input) > 1 ) {
        // look for suffixes of size

        for ( i=0; i<strlen(PowerSuffixes); i++ ) {
            if ( (char *)strchr(input, PowerSuffixes[i]) == (char *)(input + strlen(input) -1) ) {
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
                 input[1] == '.' ) {
                // this is a float-point number
                ;
            } else {
                // hexadecimal or octal number:
                if ( strlen(input) > 2 &&
                     ( input[1] == 'x' || input[1] == 'X' ) ) {
                    // hexadecimal
                    result = strtoll( input +2, NULL, 16 ) * result;
                } else {
                    // octal
                    result = strtoll( input +1, NULL, 8 ) * result;
                    // Note: 0 is zero decimal and zero octal :-)
                }
                input[0] = '1';
                input[1] = '\0';
            }
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
    fprintf( stderr, "  %s (v%s)\n", PACKAGE_NAME, PACKAGE_VERSION );
    fprintf( stderr, "  GZIP files indexer, compressor and data retriever.\n" );
    fprintf( stderr, "  Create small indexes for gzipped files and use them\n" );
    fprintf( stderr, "  for quick and random-positioned data extraction.\n" );
    fprintf( stderr, "  //github.com/circulosmeos/gztool (by Roberto S. Galende)\n\n" );
    fprintf( stderr, "  $ gztool [-[abLnsv] #] [-[1..9]AcCdDeEfFhilpPqrRStTwWxXzZ|u[cCdD]] [-I <INDEX>] <FILE>...\n\n" );
    fprintf( stderr, "  `gztool -hh` for more help\n" );
    fprintf( stderr, "\n" );

}


// print help
local void print_help() {

    fprintf( stderr, "\n" );
    fprintf( stderr, "  %s (v%s)\n", PACKAGE_NAME, PACKAGE_VERSION );
    fprintf( stderr, "  GZIP files indexer, compressor and data retriever.\n" );
    fprintf( stderr, "  Create small indexes for gzipped files and use them\n" );
    fprintf( stderr, "  for quick and random-positioned data extraction.\n" );
    fprintf( stderr, "  No more waiting when the end of a 10 GiB gzip is needed!\n" );
    fprintf( stderr, "  //github.com/circulosmeos/gztool (by Roberto S. Galende)\n\n" );
    fprintf( stderr, "  $ gztool [-[abLnsv] #] [-[1..9]AcCdDeEfFhilpPqrRStTwWxXzZ|u[cCdD]] [-I <INDEX>] <FILE>...\n\n" );
    fprintf( stderr, "  Note that actions `-bcStT` proceed to an index file creation (if\n" );
    fprintf( stderr, "  none exists) INTERLEAVED with data flow. As data flow and\n" );
    fprintf( stderr, "  index creation occur at the same time there's no waste of time.\n" );
    fprintf( stderr, "  Also you can interrupt actions at any moment and the remaining\n" );
    fprintf( stderr, "  index file will be reused (and completed if necessary) on the\n" );
    fprintf( stderr, "  next gztool run over the same data.\n\n" );
    fprintf( stderr, " -[1..9]: compression factor to use with `-[c|u[cC]]`, from\n" );
    fprintf( stderr, "     best speed (`-1`) to best compression (`-9`). Default is `-6`.\n" );
    fprintf( stderr, " -a #: Await # seconds between reads when `-[ST]|Ec`. Default is 4 s.\n" );
    fprintf( stderr, " -A: Modifier for `-[rR]` to indicate the range of bytes/lines in\n" );
    fprintf( stderr, "     absolute values, instead of the default incremental values.\n" );
    fprintf( stderr, " -b #: extract data from indicated uncompressed byte position of\n" );
    fprintf( stderr, "     gzip file (creating or reusing an index file) to STDOUT.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -C: always create a 'Complete' index file, ignoring possible errors.\n" );
    fprintf( stderr, " -c: compress a file like with gzip, creating an index at the same time.\n" );
    fprintf( stderr, " -d: decompress a file like with gzip.\n" );
    fprintf( stderr, " -D: do not delete original file when using `-[cd]`.\n" );
    fprintf( stderr, " -e: if multiple files are indicated, continue on error (if any).\n" );
    fprintf( stderr, " -E: end processing on first GZIP end of file marker at EOF.\n" );
    fprintf( stderr, "     Nonetheless with `-c`, `-E` waits for more data even at EOF.\n" );
    fprintf( stderr, " -f: force file overwriting if index file already exists.\n" );
    fprintf( stderr, " -F: force index creation/completion first, and then action: if\n" );
    fprintf( stderr, "     `-F` is not used, index is created interleaved with actions.\n" );
    fprintf( stderr, " -h: print brief help; `-hh` prints this help.\n" );
    fprintf( stderr, " -i: create index for indicated gzip file (For 'file.gz' the default\n" );
    fprintf( stderr, "     index file name will be 'file.gzi'). This is the default action.\n" );
    fprintf( stderr, " -I string: index file name will be the indicated string.\n" );
    fprintf( stderr, " -l: check and list info contained in indicated index file.\n" );
    fprintf( stderr, "     `-ll` and `-lll` increase the level of index checking detail.\n" );
    fprintf( stderr, " -L #: extract data from indicated uncompressed line position of\n" );
    fprintf( stderr, "     gzip file (creating or reusing an index file) to STDOUT.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -n #: indicates that the first byte on compressed input is #, not 1,\n" );
    fprintf( stderr, "     and so truncated compressed inputs can be used if an index exists.\n" );
    fprintf( stderr, " -p: indicates that the gzip input stream may be composed of various\n" );
    fprintf( stderr, "     incorrectly terminated GZIP streams, and so then a careful\n" );
    fprintf( stderr, "     Patching of the input may be needed to extract correct data.\n" );
    fprintf( stderr, " -P: like `-p`, but when used with `-[ST]` implies that checking\n" );
    fprintf( stderr, "     for errors in stream is made as quick as possible as the gzip file\n" );
    fprintf( stderr, "     grows. Warning: this may lead to some errors not being patched.\n" );
    fprintf( stderr, " -q: extract data as indicated with `-[bL]` of a still-growing gzip file\n" );
    fprintf( stderr, "     and continue Supervising & extracting to STDOUT even after eof.\n" );
    fprintf( stderr, " -r #: (range): Number of bytes to extract when using `-[bL]`.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -R #: (Range): Number of lines to extract when using `-[bL]`.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -s #: span in uncompressed MiB between index points when\n" );
    fprintf( stderr, "     creating the index. By default is `10`.\n" );
    fprintf( stderr, " -S: Supervise indicated file: create a growing index,\n" );
    fprintf( stderr, "     for a still-growing gzip file. (`-i` is implicit).\n" );
    fprintf( stderr, " -t: tail (extract last bytes) to STDOUT on indicated gzip file.\n" );
    fprintf( stderr, " -T: tail (extract last bytes) to STDOUT on indicated still-growing\n" );
    fprintf( stderr, "     gzip file, and continue Supervising & extracting to STDOUT.\n" );
    fprintf( stderr, " -u [cCdD]: utility to compress (`-u c`) or decompress (`-u d`)\n" );
    fprintf( stderr, "          zlib-format files to STDOUT. Use `-u C` and `-u D`\n" );
    fprintf( stderr, "          to manage raw compressed files. No index involved.\n" );
    fprintf( stderr, " -v #: output verbosity: from `0` (none) to `5` (nuts).\n" );
    fprintf( stderr, "     Default is `1` (normal).\n" );
    fprintf( stderr, " -w: wait for creation if file doesn't exist, when using `-[cdST]`.\n" );
    fprintf( stderr, " -W: do not Write index to disk. But if one is already available\n" );
    fprintf( stderr, "     read and use it. Useful if the index is still under an `-S` run.\n" );
    fprintf( stderr, " -x: create index with line number information (win/*nix compatible).\n" );
    fprintf( stderr, "     (Index counts last line even w/o newline char (`wc` does not!)).\n" );
    fprintf( stderr, "     This is implicit unless `-X` or `-z` are indicated.\n" );
    fprintf( stderr, " -X: like `-x`, but newline character is '\\r' (old mac).\n" );
    fprintf( stderr, " -z: create index without line number information.\n" );
    fprintf( stderr, " -Z: adjust index points to a byte boundary: no previous byte needed.\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "  EXAMPLE: Extract data from 1 GiB byte (byte 2^30) on,\n" );
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
    char *file_name = NULL;
    FILE *in = NULL;
    struct access *index = NULL;

    // variables for grabbing the options:
    uint64_t extract_from_byte = 0LLU;
    uint64_t extract_from_line = 0LLU;
    uint64_t expected_first_byte = 1LLU;
    uint64_t span_between_points = SPAN;
    uint64_t range_number_of_bytes = 0LLU;
    uint64_t range_number_of_lines = 0LLU;
    char *index_filename = NULL;
    int continue_on_error = 0;
    int index_filename_indicated = 0;
    int force_action = 0;
    int force_strict_order = 0;
    int write_index_to_disk = 1;
    int end_on_first_proper_gzip_eof = 0;
    int always_create_a_complete_index = 0;
    int wait_for_file_creation = 0;
    int waiting_time = WAITING_TIME;
    int do_not_delete_original_file = 0;
    int extend_index_with_lines = 0;
    bool force_index_without_lines = false; // marks `-z` use
    int raw_method = 0; // for use with `-[cd]`: 0: zlib; `-[CD]`: 1: raw
    int gzip_stream_may_be_damaged = 0;
    int compression_factor = 0;
    bool lazy_gzip_stream_patching_at_eof = false;
    bool indicate_range_in_absolute_value = false;
    bool adjust_index_points_to_byte_boundary = false;
    bool continue_tailing_on_eof = false;
    char utility_option = ' ';
    uint64_t count_errors = 0;

    enum EXIT_RETURNED_VALUES ret_value;
    enum ACTION action;
    enum VERBOSITY_LEVEL list_verbosity = VERBOSITY_NONE;
    enum VERBOSITY_LEVEL help_verbosity = VERBOSITY_NONE;

    int opt = 0;
    uint64_t i = 0;
    int actions_set = 0;


    action = ACT_NOT_SET;
    ret_value = EXIT_OK;
    while ((opt = getopt(argc, argv, "123456789a:Ab:cCdDeEfFhiI:lL:n:pPqr:R:s:StTu:v:wWxXzZ")) != -1)
        switch (opt) {
            // help
            case 'h':
                help_verbosity++;
                if ( help_verbosity > VERBOSITY_EXCESSIVE )
                    help_verbosity = VERBOSITY_EXCESSIVE;
                break;
            // compression factor options: [1-9]
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                compression_factor = (char)opt - '0';
                break;
            // `-a #` default waiting time in seconds when supervising a growing gzip file (`-[ST]`)
            case 'a':
                waiting_time = (int)strtol( optarg, NULL, 10 );
                if ( waiting_time == 0 &&
                     ( strlen( optarg ) != 1 || '0' != optarg[0] ) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Invalid awaiting value of '%s'\n", optarg );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-A` modifies `-[rR]` to indicate the range of bytes/lines in
            // absolute values, instead of the default incremental values.
            case 'A':
                indicate_range_in_absolute_value = true;
                break;
            // `-b #` extracts data from indicated position byte in uncompressed stream of <FILE>
            case 'b':
                extract_from_byte = (uint64_t)giveMeAnInteger( optarg );
                action = ACT_EXTRACT_FROM_BYTE;
                actions_set++;
                break;
            // `-c` compress <FILE> (or stdin if none) to FILE.gz and create index for it
            case 'c':
                action = ACT_COMPRESS_AND_CREATE_INDEX;
                actions_set++;
                break;
            // `-C` generates always a complete index file, ignoring possible decompression errors
            case 'C':
                always_create_a_complete_index = 1;
                break;
            // `-d` decompress <FILE> (or stdin if none) to stdout
            case 'd':
                action = ACT_DECOMPRESS;
                actions_set++;
                break;
            // `-D` no not delete original file with `-[cd]`
            case 'D':
                do_not_delete_original_file = 1;
                break;
            // `-e` continues on error if multiple input files indicated
            case 'e':
                continue_on_error = 1;
                break;
            // `-E` ends gzip processing on first proper (at feof()) GZIP EOF,
            // or with `-c` continue waiting for data even at eof()
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
            // `-i` create index for <FILE>
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
            // `-L #` extracts data from indicated line in uncompressed stream of <FILE>
            case 'L':
                extract_from_line = (uint64_t)giveMeAnInteger( optarg );
                action = ACT_EXTRACT_FROM_LINE;
                actions_set++;
                if ( extend_index_with_lines == 0 ) {
                    // if `-[xX]` is not indicated, newline format is Unix
                    extend_index_with_lines = 1;
                }
                break;
            // `-n #` indicates that the first byte on compressed input is #, not 1
            // and so truncated compressed inputs can be used. This is subject to checks later on.
            case 'n':
                expected_first_byte = (uint64_t)giveMeAnInteger( optarg );
                if ( expected_first_byte == 0 ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Option `-n 0` invalid: must be >= 1.\n" );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-p` indicates that the gzip input stream may be composed of various
            // incorrectly terminated GZIP streams, and so then a careful
            // Patching of the input may be needed to extract correct data.
            case 'p':
                gzip_stream_may_be_damaged = CHUNKS_TO_DECOMPRESS_IN_ADVANCE;
                break;
            // `-P` is like `-p`, but when used with `-[ST]` implies that checking
            // for errors in stream is made as quick as possible as the gzip file
            // grows, not respecting then at EOF the CHUNKS_TO_DECOMPRESS_IN_ADVANCE value.
            case 'P':
                gzip_stream_may_be_damaged = CHUNKS_TO_DECOMPRESS_IN_ADVANCE;
                lazy_gzip_stream_patching_at_eof = true;
                break;
            // `-q` indicates that `-[bL]` options are processed and tailing and supervising
            // will continue once end-of-file is reached.
            case 'q':
                continue_tailing_on_eof = true;
                break;
            // `-r` is used to indicate how many bytes to extract when using `-[bL]`
            // Accepts SI suffixes.
            case 'r':
                range_number_of_bytes = (uint64_t)giveMeAnInteger( optarg );
                if ( range_number_of_bytes == 0LLU ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Option `-r 0` invalid: must be >= 1.\n" );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-R` is used to indicate how many lines to extract when using `-[bL]`
            // Accepts SI suffixes.
            case 'R':
                range_number_of_lines = (uint64_t)giveMeAnInteger( optarg );
                if ( range_number_of_lines == 0LLU ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Option `-R 0` invalid: must be >= 1.\n" );
                    return EXIT_INVALID_OPTION;
                }
                if ( extend_index_with_lines == 0 ) {
                    // if `-[xX]` is not indicated, newline format is Unix
                    extend_index_with_lines = 1;
                }
                break;
            // `-s #` span between index points, in MiB
            case 's':
                // span is converted from MiB to bytes for internal use
                span_between_points = strtoll( optarg, NULL, 10 );
                if ( span_between_points > UINT64_MAX / 1024 / 1024 ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Option `-s %llu` MiB value too high!\n", span_between_points );
                    return EXIT_INVALID_OPTION;
                }
                span_between_points = span_between_points * 1024 * 1024;
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
            // `-u [cd]`: utility to compress (`-u c`) or decompress (`-u d`) zlib-format files
            case 'u':
                // span is converted to from MiB to bytes for internal use
                if ( strlen(optarg) == 1 ) {
                    switch ( optarg[0] ) {
                        case 'c':
                            action = ACT_COMPRESS_CHUNK;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        case 'd':
                            action = ACT_DECOMPRESS_CHUNK;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        case 'C':
                            action = ACT_COMPRESS_CHUNK;
                            raw_method = 1;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        case 'D':
                            action = ACT_DECOMPRESS_CHUNK;
                            raw_method = 1;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        default:
                            print_to_stderr( VERBOSITY_NORMAL, "ERROR: Invalid option `-u %s` (`-u [cCdD]`).\n", optarg );
                            return EXIT_INVALID_OPTION;
                    }
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Invalid option `-u %s` (`-u [cCdD]`).\n", optarg );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-v` verbosity
            case 'v':
                verbosity_level_HACK = (int)strtol( optarg, NULL, 10 );
                if ( ( optarg[0] != '0' && verbosity_level_HACK == 0 ) ||
                     strlen( optarg ) > 1 ||
                     verbosity_level_HACK > VERBOSITY_NUTS ) {
                    verbosity_level_HACK = VERBOSITY_NORMAL; // without this an erroneous `-v` may not be shown
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Invalid option `-v %s` (`-v [0..5]`).\n", optarg );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-w` wait for file creation with `-[STcd]`
            case 'w':
                wait_for_file_creation = 1;
                break;
            // `-W` do not write nor update index on disk
            case 'W':
                write_index_to_disk = 0;
                break;
            // `-x` create extended index with line number information
            // using Unix newline format ('\n')  (compatible with Windows \r\n)
            case 'x':
                extend_index_with_lines = 1;
                break;
            // `-X` create extended index with line number information
            // using old mac newline format ('\r')
            case 'X':
                extend_index_with_lines = 2;
                break;
            // `-z` create index without line number information
            case 'z':
                force_index_without_lines = true;
                break;
            // `-Z` adjust index points to byte boundary
            case 'Z':
                adjust_index_points_to_byte_boundary = true;
                break;
            case '?':
                if ( isprint (optopt) ) {
                    // print warning only if char option is unknown
                    if ( NULL == strchr("123456789aAbcCdDeEfFhiIlLnpPrRSstTuvwWxXzZ", optopt) ) {
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Unknown option `-%c'.\n", optopt);
                        print_help();
                    }
                } else
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Unknown option character `\\x%x'.\n", optopt);
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
                return EXIT_INVALID_OPTION;
            default:
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
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


    /*
     * Checking parameter merging and absence:
     */

    if ( compression_factor > 0 ) {
        if ( action != ACT_COMPRESS_AND_CREATE_INDEX &&
             action != ACT_COMPRESS_CHUNK ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: compression factor (`-[1..9]`) must be used with `-[c|u[cC]]`.\n" );
        return EXIT_INVALID_OPTION;
        }
    } else {
        compression_factor = Z_DEFAULT_COMPRESSION; // (btw, it's -1, which means 6 for zlib)
    }

    if ( actions_set > 1 ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: do not merge parameters `-[bcdilLStTu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( write_index_to_disk == 0 &&
           ( force_action == 1 || force_strict_order == 1)
        ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: do not merge contradictory parameters `-W` and `-[fF]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action != ACT_SUPERVISE && action != ACT_EXTRACT_TAIL_AND_CONTINUE &&
           action != ACT_COMPRESS_AND_CREATE_INDEX && action != ACT_DECOMPRESS ) &&
        wait_for_file_creation == 1 ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-w` only apply to `-[cdST]`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK ) &&
        ( force_action == 1 || force_strict_order == 1 || write_index_to_disk == 0 ||
            span_between_points != SPAN || index_filename_indicated == 1 ||
            end_on_first_proper_gzip_eof == 1 || always_create_a_complete_index == 1 ||
            waiting_time != WAITING_TIME )
        ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-[aCEfFIsW]` does not apply to `-u`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action == ACT_LIST_INFO ) &&
        ( force_action == 1 || force_strict_order == 1 || write_index_to_disk == 0 ||
            span_between_points != SPAN ||
            end_on_first_proper_gzip_eof == 1 || always_create_a_complete_index == 1 ||
            waiting_time != WAITING_TIME )
        ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-[aCEfFsW]` does not apply to `-l`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( (unsigned int) waiting_time >= UINT32_MAX || waiting_time < 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Invalid awaiting value of '%d'\n", waiting_time );
        return EXIT_INVALID_OPTION;
    }

    if ( do_not_delete_original_file == 1 &&
         ( action != ACT_COMPRESS_AND_CREATE_INDEX && action != ACT_DECOMPRESS ) ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-D` option invalid when not using `-[cd]`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( span_between_points <= 0 ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Invalid `-s` parameter value: '%llu'\n", span_between_points );
        return EXIT_INVALID_OPTION;
    }
    if ( span_between_points != SPAN &&
        ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK || action == ACT_LIST_INFO )
        ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-s` parameter does not apply to `-[lu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( extend_index_with_lines > 0 || true == force_index_without_lines || true == adjust_index_points_to_byte_boundary ) &&
        ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK || action == ACT_LIST_INFO )
        ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-[xXzZ]` parameters do not apply to `-[lu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( 0 == actions_set ) {
        // `-I <FILE>` is equivalent to `-i -I <FILE>`
        if ( action == ACT_NOT_SET && index_filename_indicated  == 1 ) {
            action = ACT_CREATE_INDEX;
            if ( (optind + 1) < argc ) {
                // too much files indicated to use `-I`
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-I` is incompatible with multiple input files.\nAborted.\n\n" );
                return EXIT_INVALID_OPTION;
            }
        } else {
            // default action is `-i`
            action = ACT_CREATE_INDEX;
            actions_set = 1;
        }
    }

    if ( // these are the actions that can create an index
         action == ACT_EXTRACT_FROM_BYTE ||
         action == ACT_CREATE_INDEX ||
         action == ACT_SUPERVISE ||
         action == ACT_EXTRACT_TAIL ||
         action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
         action == ACT_COMPRESS_AND_CREATE_INDEX ||
         action == ACT_EXTRACT_FROM_LINE ||
         action == ACT_DECOMPRESS
         // it's stated that "DECOMPRESS does not require index" @ action_create_index() BUT
         // also that "`gztool -d` is just an alias for `gztool -b0` when using STDIN" @ main()
    ) {
        // if `-z`, honor it, if not, make `-x` implicit unless, in both cases, `-[xX]` was used:
        if ( true == force_index_without_lines ) {
            if ( extend_index_with_lines > 0 ) { // unless `-[xX]` was used
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-[xX]` and `-z` cannot be mixed.\n" );
                return EXIT_INVALID_OPTION;
            }
            extend_index_with_lines = 0;
        } else {
            // make `-x` implicit
            if ( extend_index_with_lines == 0 ) { // only if `-[xX]` was not used
                extend_index_with_lines = 3; // by default use '\n' as newline char
            }
        }
    } else {
        if ( true == force_index_without_lines || true == adjust_index_points_to_byte_boundary ) {
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-[zZ]` is not applicable to these options.\n" );
            return EXIT_INVALID_OPTION;
        }
    }

    if ( expected_first_byte != 1 &&
         ( action != ACT_EXTRACT_FROM_BYTE && action != ACT_EXTRACT_FROM_LINE &&
            action != ACT_CREATE_INDEX ) ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-n` parameter can only be used with `-[biL]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( range_number_of_bytes > 0LLU || range_number_of_lines > 0LLU ) &&
         ( action != ACT_EXTRACT_FROM_BYTE && action != ACT_EXTRACT_FROM_LINE )
    ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-[rR]` parameter can only be used with `-[bL]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( true == indicate_range_in_absolute_value ) {
        if ( range_number_of_bytes == 0LLU &&
             range_number_of_lines == 0LLU ) {
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-A` parameter can only be used with `-[rR]`.\n" );
            return EXIT_INVALID_OPTION;
        }
        // ( yes, `-r` AND `-R` can be simultaneosuly indicated: the 1st reached is the one applied. )
        if ( range_number_of_bytes > 0LLU ) {
            if ( action == ACT_EXTRACT_FROM_BYTE &&
                 extract_from_byte > 0LLU
            ) {
                if ( range_number_of_bytes > extract_from_byte ) {
                    range_number_of_bytes -= extract_from_byte;
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-Ar` implies a value greater than `-b`.\n" );
                    return EXIT_INVALID_OPTION;
                }
            } else {
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-Ar` (bytes) cannot be used with `-L` (lines).\n" );
                return EXIT_INVALID_OPTION;
            }
        }
        if ( range_number_of_lines > 0LLU ) {
            if ( action == ACT_EXTRACT_FROM_LINE &&
                 extract_from_line > 0LLU
            ) {
                if ( range_number_of_lines > extract_from_line ) {
                    range_number_of_lines -= extract_from_line;
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-AR` implies a value greater than `-L`.\n" );
                    return EXIT_INVALID_OPTION;
                }
            } else {
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-AR` (lines) cannot be used with `-b` (bytes).\n" );
                return EXIT_INVALID_OPTION;
            }
        }
    }

    if ( 1 == force_strict_order &&
        ( action == ACT_SUPERVISE ||
          action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
          action == ACT_LIST_INFO ||
          action == ACT_COMPRESS_CHUNK ||
          action == ACT_DECOMPRESS_CHUNK ||
          action == ACT_DECOMPRESS ) ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Cannot use `-F` with `-[dlSTu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( 1 == force_strict_order && action == ACT_COMPRESS_AND_CREATE_INDEX ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-F` not implemented with `-c`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( true == lazy_gzip_stream_patching_at_eof &&
        ( action != ACT_SUPERVISE &&
          action != ACT_EXTRACT_TAIL_AND_CONTINUE ) &&
        false == continue_tailing_on_eof ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: without `-[ST]`, use `-p` instead of `-P`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( gzip_stream_may_be_damaged > 0 &&
        !( action == ACT_EXTRACT_FROM_BYTE ||
           action == ACT_EXTRACT_FROM_LINE ||
           action == ACT_CREATE_INDEX ||
           action == ACT_SUPERVISE ||
           action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
           action == ACT_DECOMPRESS ) ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Cannot use `-p` without `-[bdiLST]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( true == continue_tailing_on_eof &&
        action != ACT_EXTRACT_FROM_LINE &&
        action != ACT_EXTRACT_FROM_BYTE ) {
        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Cannot use `-q` without `-[bL]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    /*
     * end of "Checking parameter merging and absence"
     */


    {   // inform action on stderr:
        char *action_string = NULL;
        switch ( action ) {
            case ACT_EXTRACT_FROM_BYTE:
                action_string = "Extract from byte = ";
                break;
            case ACT_COMPRESS_CHUNK:
                if ( raw_method == 1 )
                    action_string = "zlib raw compress";
                else
                    action_string = "zlib compress";
                break;
            case ACT_DECOMPRESS_CHUNK:
                if ( raw_method == 1 )
                    action_string = "zlib raw decompress";
                else
                    action_string = "zlib decompress";
                break;
            case ACT_CREATE_INDEX:
                action_string = "Create index for a gzip file";
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
            case ACT_COMPRESS_AND_CREATE_INDEX:
                action_string = "Compress file and create index";
                break;
            case ACT_DECOMPRESS:
                action_string = "Decompress file";
                break;
            case ACT_EXTRACT_FROM_LINE:
                action_string = "Extract from line = ";
                break;
            default:
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: Unexpected error! No action specified.\n" );
                return EXIT_GENERIC_ERROR;
        }
        switch( action ) {
            case ACT_EXTRACT_FROM_BYTE:
                print_to_stderr( VERBOSITY_NORMAL, "ACTION: %s%llu", action_string, extract_from_byte );
                if ( expected_first_byte > 1 ) {
                    print_to_stderr( VERBOSITY_NORMAL, " (input - %llu bytes)", expected_first_byte - 1 );
                }
                print_to_stderr( VERBOSITY_NORMAL, "\n\n" );
                break;
            case ACT_EXTRACT_FROM_LINE:
                print_to_stderr( VERBOSITY_NORMAL, "ACTION: %s%llu\n\n", action_string, extract_from_line );
                break;
            default:
                print_to_stderr( VERBOSITY_NORMAL, "ACTION: %s\n\n", action_string );
        }
    }


    // inform parameters with verbosity_level_HACK > VERBOSITY_NORMAL
    if ( verbosity_level_HACK > VERBOSITY_NORMAL ) {
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -a: %d, \t-A: %d, \t-b: %llu, \t-c: %d\n",
            waiting_time, indicate_range_in_absolute_value,
            extract_from_byte, ( (action==ACT_COMPRESS_AND_CREATE_INDEX)? 1: 0 ) );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -C: %d, \t-d: %d, \t-D: %d, \t-e: %d\n",
            always_create_a_complete_index, ( (action==ACT_DECOMPRESS)? 1: 0 ),
            do_not_delete_original_file, continue_on_error );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -E: %d, \t-f: %d, \t-F: %d\n",
            end_on_first_proper_gzip_eof, force_action, force_strict_order );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -i: %d, \t-I: %s\n",
            ( (action==ACT_CREATE_INDEX)? 1: 0 ),
            ( (index_filename_indicated>0)? index_filename: NULL ));
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -l: %d, \t-L: %llu, \t-n: %llu\n",
            ( (action==ACT_LIST_INFO)? list_verbosity: 0 ),
            extract_from_line, expected_first_byte );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -p: %d, \t-P: %d, \t-q: %d\n",
            gzip_stream_may_be_damaged, lazy_gzip_stream_patching_at_eof, continue_tailing_on_eof );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -r: %d, \t-R: %d\n",
            range_number_of_bytes, range_number_of_lines );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -s: %llu, \t-S: %d, \t-t: %d\n",
            span_between_points, ( (action==ACT_SUPERVISE)? 1: 0 ), ( (action==ACT_EXTRACT_TAIL)? 1: 0 ) );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -T: %d, \t-u: %c, \t-v: %d, \t-w: %d\n",
            ( (action==ACT_EXTRACT_TAIL_AND_CONTINUE)? 1: 0 ), utility_option,
              verbosity_level_HACK, wait_for_file_creation );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -W: %d, \t-x: %s, \t-X: %d\n",
            ( (write_index_to_disk==0)? 1: 0 ),
            ( (3 == extend_index_with_lines)? "[1]": ( (1 == extend_index_with_lines)? "1": "0" ) ),
            ( (2 == extend_index_with_lines)? 1: 0) );
        print_to_stderr( VERBOSITY_EXCESSIVE, "  -z: %d, \t-Z: %d, \t-[0-9]: %d\n\n",
            force_index_without_lines, adjust_index_points_to_byte_boundary, compression_factor  );
    }


    if (optind == argc || argc == 1) {
        // file input is stdin

        // ACT_COMPRESS_AND_CREATE_INDEX requires an index file name:
        if ( action == ACT_COMPRESS_AND_CREATE_INDEX &&
             0 == index_filename_indicated
            ) {
            // if no index filename is set (`-I`), compression cannot proceed
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: Index filename must be provided with `-I` when compressing STDIN.\n" );
            return EXIT_GENERIC_ERROR;
        }

        // `-p` cannot operate with STDIN input
        if ( gzip_stream_may_be_damaged > 0 ) {
            print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-p` cannot proceed with STDIN.\n" );
            return EXIT_GENERIC_ERROR;
        }

        // check `-f` and execute delete if index file exists
        if ( ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ||
               action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
               action == ACT_EXTRACT_FROM_BYTE || action == ACT_EXTRACT_FROM_LINE ||
               action == ACT_EXTRACT_TAIL ||
               action == ACT_COMPRESS_AND_CREATE_INDEX ) &&
             1 == index_filename_indicated &&
             access( index_filename, F_OK ) != -1 ) {
            // index file already exists

            /*if ( ( extend_index_with_lines > 0 && action != ACT_EXTRACT_FROM_LINE ) &&
                 ( force_action == 0 || ( force_action == 1 && write_index_to_disk == 0 ) ) )
                    print_to_stderr( VERBOSITY_NORMAL, "WARNING: `-[Xx]` will be ignored because index already exists.\n" );*/

            if ( force_action == 0 ) {
                if ( action == ACT_COMPRESS_AND_CREATE_INDEX ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Index file '%s' already exists.\n\n", index_filename );
                    return EXIT_GENERIC_ERROR;
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "Index file '%s' already exists and will be used.\n", index_filename );
                    if ( write_index_to_disk == 0 ) {
                        print_to_stderr( VERBOSITY_NORMAL, "Index file will NOT be modified.\n" );
                    } else {
                        ; //print_to_stderr( VERBOSITY_NORMAL, "(Use `-f` to force overwriting.)\n" );
                    }
                }
            } else {
                if ( write_index_to_disk == 1 ) {
                    // force_action == 1 => delete index file
                    print_to_stderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", index_filename );
                    // delete it
                    if ( remove( index_filename ) != 0 ) {
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\n\n", index_filename );
                        return EXIT_GENERIC_ERROR;
                    }
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "Ignoring `-f` force option with `W` on '%s' ...\n", index_filename );
                }
            }

        }

        // `-F` has no sense with stdin
        if ( force_strict_order == 1 ) {
            print_to_stderr( VERBOSITY_NORMAL, "WARNING: There is no sense in using `-F` with STDIN input: ignoring `F`.\n" );
            force_strict_order = 0;
        }

        if ( action == ACT_DECOMPRESS ) {
            // `gztool -d` is just an alias for `gztool -b0` when using STDIN
            extract_from_byte = 0;
            action = ACT_EXTRACT_FROM_BYTE;
        }

        // file input is stdin
        switch ( action ) {

            case ACT_EXTRACT_FROM_BYTE:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_FROM_BYTE, extract_from_byte, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    print_to_stderr( VERBOSITY_NORMAL, "\n" );
                    break;
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-I INDEX` must be used when extracting from STDIN.\nAborted.\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }

            case ACT_EXTRACT_FROM_LINE:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_FROM_LINE, 0, extract_from_line, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    print_to_stderr( VERBOSITY_NORMAL, "\n" );
                    break;
                } else {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: `-I INDEX` must be used when extracting from STDIN.\nAborted.\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }

            case ACT_COMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != compress_file( stdin, stdout, compression_factor, raw_method ) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR while compressing STDIN.\nAborted.\n\n" );
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
                if ( Z_OK != decompress_file( stdin, stdout, raw_method ) ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR while decompressing STDIN.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                ret_value = EXIT_OK;
                break;

            case ACT_CREATE_INDEX:
                // stdin is a gzip file that must be indexed
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        JUST_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        JUST_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                }
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_LIST_INFO:
                if ( index_filename_indicated == 1 ) {
                    // Admit `-I` as a way to indicate an input index file name:
                    ret_value = action_list_info( index_filename, NULL, list_verbosity );
                } else {
                    // stdin is an index file that must be checked
                    ret_value = action_list_info( "", NULL, list_verbosity );
                }
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_SUPERVISE:
                // stdin is a gzip file for which an index file must be created on-the-fly
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        SUPERVISE_DO, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        SUPERVISE_DO, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                }
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_EXTRACT_TAIL:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                } else {
                    // if an index filename is not indicated, index will not be output
                    // as stdout is already used for data extraction
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Index filename is needed if STDIN is used as gzip input.\nAborted.\n" );
                    ret_value = EXIT_INVALID_OPTION;
                }
                break;

            case ACT_EXTRACT_TAIL_AND_CONTINUE:
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                }
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_COMPRESS_AND_CREATE_INDEX:
                // if code reaches here, and index_filename exists
                // TODO: implement `-F` for COMPRESS_AND_CREATE_INDEX ?
                ret_value = action_create_index( "", &index, index_filename,
                        COMPRESS_AND_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                break;

            default:
                print_to_stderr( VERBOSITY_NORMAL, "ERROR: action not specified.\n" );
                ret_value = EXIT_GENERIC_ERROR;

        }

    } else {

        if ( action == ACT_SUPERVISE &&
             ( argc - optind > 1 ) ) {
            // supervise only accepts one input gz file
            print_to_stderr( VERBOSITY_NORMAL, "`-S` option only accepts one gzip file parameter: %d indicated.\n", argc - optind );
            print_to_stderr( VERBOSITY_NORMAL, "Aborted.\n" );
            return EXIT_GENERIC_ERROR;
        }

        for ( i = optind; i < (uint64_t)argc; i++ ) {

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

                if ( action == ACT_COMPRESS_AND_CREATE_INDEX ) {
                    sprintf(index_filename, "%s.gzi", argv[i]);
                } else {
                    if ( strlen( argv[i] ) > 3 && // avoid out-of-bounds
                         (char *)strstr(index_filename, ".gz") ==
                         (char *)(index_filename + strlen(argv[i]) - 3)
                        )
                        // if gzip-file name is 'FILE.gz', index file name will be 'FILE.gzi'
                        sprintf(index_filename, "%si", argv[i]);
                    else
                        // otherwise, the complete extension '.gzi' is appended
                        sprintf(index_filename, "%s.gzi", argv[i]);
                }

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

            if ( ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ||
                   action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
                   action == ACT_EXTRACT_FROM_BYTE || action == ACT_EXTRACT_FROM_LINE ||
                   action == ACT_EXTRACT_TAIL ||
                   action == ACT_COMPRESS_AND_CREATE_INDEX ) &&
                 access( index_filename, F_OK ) != -1 ) {
                // index file already exists

                /*if ( ( extend_index_with_lines > 0 && action != ACT_EXTRACT_FROM_LINE ) &&
                     ( force_action == 0 || ( force_action == 1 && write_index_to_disk == 0 ) ) )
                        print_to_stderr( VERBOSITY_NORMAL, "WARNING: `-[Xx]` will be ignored because index already exists.\n" );*/

                if ( force_action == 0 ) {
                    print_to_stderr( VERBOSITY_NORMAL, "Index file '%s' already exists and will be used.\n", index_filename );
                    if ( write_index_to_disk == 0 ) {
                        print_to_stderr( VERBOSITY_NORMAL, "Index file will NOT be modified.\n" );
                    } else {
                        ; //print_to_stderr( VERBOSITY_NORMAL, "(Use `-f` to force overwriting.)\n" );
                    }
                } else {
                    // force_action == 1
                    if ( write_index_to_disk == 1 ) {
                        // delete index file
                        print_to_stderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", index_filename );
                        if ( remove( index_filename ) != 0 ) {
                            print_to_stderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\nAborted.\n", index_filename );
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                    } else {
                        print_to_stderr( VERBOSITY_NORMAL, "Ignoring `-f` force option with `W` on '%s' ...\n", index_filename );
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
                    JUST_CREATE_INDEX, 0, 0, span_between_points,
                    write_index_to_disk, end_on_first_proper_gzip_eof,
                    always_create_a_complete_index, waiting_time, force_action,
                    wait_for_file_creation, extend_index_with_lines,
                    expected_first_byte, gzip_stream_may_be_damaged,
                    lazy_gzip_stream_patching_at_eof,
                    range_number_of_bytes, range_number_of_lines,
                    adjust_index_points_to_byte_boundary,
                    compression_factor, continue_tailing_on_eof );
                if ( ret_value != EXIT_OK ) {
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: Could not create index '%s'.\nAborted.\n", index_filename );
                    break; // breaks for() loop
                }
            }


            // "-bil" options can accept multiple files
            switch ( action ) {

                case ACT_EXTRACT_FROM_BYTE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_FROM_BYTE, extract_from_byte, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    break;

                case ACT_EXTRACT_FROM_LINE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_FROM_LINE, 0, extract_from_line, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    break;

                case ACT_COMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != compress_file( in, stdout, compression_factor, raw_method ) ) {
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR while compressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_DECOMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != decompress_file( in, stdout, raw_method ) ) {
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR while decompressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_CREATE_INDEX:
                    if ( force_strict_order == 0 )
                        // if force_strict_order == 1 action has already been done!
                        ret_value = action_create_index( file_name, &index, index_filename,
                            JUST_CREATE_INDEX, 0, 0, span_between_points,
                            write_index_to_disk, end_on_first_proper_gzip_eof,
                            always_create_a_complete_index, waiting_time, force_action,
                            wait_for_file_creation, extend_index_with_lines,
                            expected_first_byte, gzip_stream_may_be_damaged,
                            lazy_gzip_stream_patching_at_eof,
                            range_number_of_bytes, range_number_of_lines,
                            adjust_index_points_to_byte_boundary,
                            compression_factor, continue_tailing_on_eof );
                    break;

                case ACT_LIST_INFO:
                    if ( index_filename_indicated == 1 ) {
                        // if index filename has been indicated with `-I`
                        // then the actual parameter is the gzip filename
                        ret_value = action_list_info( index_filename, file_name, list_verbosity );
                    } else {
                        // if indicated file_name is not an index, but an index exists corresponding
                        // to it, use it for ease of use,
                        // BUT only if passed file exists, to avoid adding a spurious extension to
                        // imaginary FILE, obtaining FILE.gzi, the index for existent FILE.gz (!).
                        if ( access( file_name, F_OK ) != -1 &&
                            ( ( strlen( file_name ) > 4 && // avoid out-of-bounds
                                (char *)strstr(file_name, ".gzi") !=
                                (char *)(file_name + strlen(argv[i]) - 4) )
                             ||
                            strlen( file_name ) <= 4 )
                        ) {
                            print_to_stderr( VERBOSITY_EXCESSIVE,
                                "Provided file '%s' has not '.gzi' extension ...\n", file_name );
                            // file_name is not named "*.gzi"
                            // so let's see if there's a filename with the corresponding index name:
                            // fortunately, this index name has already been calculated: index_filename
                            if ( access( index_filename, F_OK ) != -1 ) {
                                // index_filename exists, so let's use it:
                                print_to_stderr( VERBOSITY_EXCESSIVE,
                                    "Detected '%s': using it as index file ...\n", index_filename );
                                ret_value = action_list_info( index_filename, file_name, list_verbosity );
                            } else {
                                // provided file_name must be used, as there's no '.gzi' companion
                                print_to_stderr( VERBOSITY_EXCESSIVE,
                                    "Not detected '%s' file ...\n", file_name );
                                ret_value = action_list_info( file_name, NULL, list_verbosity );
                            }
                        } else {
                            // this is the default case: the passed parameter file_name
                            // is the supossed index filename
                            ret_value = action_list_info( file_name, NULL, list_verbosity );
                        }
                    }
                    break;

                case ACT_SUPERVISE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        SUPERVISE_DO, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    print_to_stderr( VERBOSITY_NORMAL, "\n" );
                    break;

                case ACT_EXTRACT_TAIL:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    break;

                case ACT_EXTRACT_TAIL_AND_CONTINUE:
                    do {
                        ret_value = action_create_index( file_name, &index, index_filename,
                            SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, 0, span_between_points,
                            write_index_to_disk, end_on_first_proper_gzip_eof,
                            always_create_a_complete_index, waiting_time, force_action,
                            wait_for_file_creation, extend_index_with_lines,
                            expected_first_byte, gzip_stream_may_be_damaged,
                            lazy_gzip_stream_patching_at_eof,
                            range_number_of_bytes, range_number_of_lines,
                            adjust_index_points_to_byte_boundary,
                            compression_factor, continue_tailing_on_eof );
                            if ( EXIT_FILE_OVERWRITTEN == ret_value &&
                                 ( 0 == write_index_to_disk ||
                                   ( 1 == force_action &&
                                     1 == write_index_to_disk ) )
                            ) {
                                print_to_stderr( VERBOSITY_NORMAL, "File overwriting detected and restarting decompression...\n" );
                                // delete index file
                                if ( NULL != index ) {
                                    free_index( index );
                                    index = NULL;
                                }
                                if ( 0 == write_index_to_disk ) {

                                    expected_first_byte = 1LLU;

                                } else { // ( 1 == force_action && 1 == write_index_to_disk )

                                    print_to_stderr( VERBOSITY_NORMAL, "Using `-f` force option: Overwriting '%s' ...\n", index_filename );
                                    if ( remove( index_filename ) != 0 ) {
                                        print_to_stderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\nAborted.\n", index_filename );
                                        ret_value = EXIT_GENERIC_ERROR;
                                        break;
                                    }

                                }
                            }
                    } while ( EXIT_FILE_OVERWRITTEN == ret_value &&
                              ( 0 == write_index_to_disk ||
                                ( 1 == force_action && 1 == write_index_to_disk ) )
                            );
                            // this do-while loop mimics `tail -F`:
                            // this has the side efect that `-[fW]T` may get stuck here forever
                            // unless end_on_first_proper_gzip_eof == 1
                    print_to_stderr( VERBOSITY_NORMAL, "\n" );
                    break;

                case ACT_COMPRESS_AND_CREATE_INDEX:
                    // if code reaches here, and index_filename exists
                    ret_value = action_create_index( file_name, &index, index_filename,
                        COMPRESS_AND_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    if ( ret_value == EXIT_OK &&
                         do_not_delete_original_file == 0 ) {
                        print_to_stderr( VERBOSITY_EXCESSIVE, "Deleting file '%s'\n", file_name );
                        // a file_name + ".gz" has been created, but now the original file must be deleted
                        if ( remove( file_name ) != 0 ) {
                            print_to_stderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\n", file_name );
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                    }
                    break;

                case ACT_DECOMPRESS:
                    // `gztool -d` is just an alias for `gztool -b0` > file_name without extension
                    // and deletion of file_name.
                    ret_value = action_create_index( file_name, &index, index_filename,
                        DECOMPRESS, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte, gzip_stream_may_be_damaged,
                        lazy_gzip_stream_patching_at_eof,
                        range_number_of_bytes, range_number_of_lines,
                        adjust_index_points_to_byte_boundary,
                        compression_factor, continue_tailing_on_eof );
                    if ( ret_value == EXIT_OK ) {
                        // delete original file, as with gzip
                        if ( strlen( file_name ) > 3 && // avoid out-of-bounds
                             (char *)strstr(file_name, ".gz") ==
                             (char *)(file_name + strlen(file_name) - 3)
                            ) {
                            // if gzip-file name is 'FILE.gz', output file has been 'FILE'
                            char *output_filename = malloc( strlen(file_name) + 1 );
                            sprintf(output_filename, "%s", file_name);
                            output_filename[strlen(file_name) - 3] = '\0';
                            if ( do_not_delete_original_file == 0 ) {
                                print_to_stderr( VERBOSITY_EXCESSIVE, "Deleting file '%s'\n", file_name );
                                // delete the original file
                                if ( remove( file_name ) != 0 ) {
                                    print_to_stderr( VERBOSITY_NORMAL,
                                        "WARNING: Decompression finished, but could not delete '%s'.\n", file_name );
                                    ret_value = EXIT_GENERIC_ERROR;
                                }
                            }
                            free( output_filename );
                        }
                    } else {
                        // decompression wasn't successful
                        print_to_stderr( VERBOSITY_NORMAL, "ERROR: decompressing '%s' file.\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                default:
                    print_to_stderr( VERBOSITY_NORMAL, "ERROR: action not specified.\n" );
                    ret_value = EXIT_GENERIC_ERROR;

            }

            if ( action == ACT_LIST_INFO ) {
                // as ACT_LIST_INFO prints to stdout,
                // file input separator must be printed also to stdout
                if ( verbosity_level_HACK >= VERBOSITY_NORMAL )
                    fprintf( stdout, "\n" );
            } else {
                print_to_stderr( VERBOSITY_NORMAL, "\n" );
            }

            print_to_stderr( VERBOSITY_MANIAC, "ERROR code = %d\n", ret_value );

            if ( ret_value != EXIT_OK )
                count_errors++;

            if ( continue_on_error == 0 &&
                 ret_value != EXIT_OK ) {
                print_to_stderr( VERBOSITY_NORMAL, "Aborted.\n" );
                // break the for loop
                break;
            }

        }

    }

    if ( i > (uint64_t)optind )
        print_to_stderr( VERBOSITY_NORMAL, "%llu files processed\n",
            ( i -optind + ( (count_errors>0 && continue_on_error == 0)?1:0 ) ) );
    if ( count_errors > 0 )
        print_to_stderr( VERBOSITY_NORMAL, "%llu files processed with errors!\n", count_errors );

    print_to_stderr( VERBOSITY_NORMAL, "\n" );

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

    if ( i > (uint64_t)( 1 + optind - ( (count_errors>0 && continue_on_error == 0)?1:0 ) ) ) {
        // if processing multiple files, return EXIT_GENERIC_ERROR if any one failed:
        if ( count_errors > 0 )
            return EXIT_GENERIC_ERROR;
        else
            return EXIT_OK;
    } else
        return ret_value;

}
