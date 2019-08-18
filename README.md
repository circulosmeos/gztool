gztool
======

GZIP files indexer and data retriever.
Create small indexes for gzipped files and use them for quick and random data extraction.
No more waiting when the end of a 10 GiB gzip is needed!

See the [Release page](https://github.com/circulosmeos/gztool/releases) for executables for your platform, and [`Compilation` paragraph](https://github.com/circulosmeos/gztool#compilation) in case you want to compile the tool.

Considerations
==============

* Please, note that the initial complete index creation still consumes as much time as a complete file decompression.   
Once created the index will reduce this time.

Nonetheless, note that `gztool` **creates index interleaved with extraction of data**, so in the practice there's no waste of time. Note that if extraction of data or just index creation are stopped at any moment, `gztool` will reuse the remaining index on the next run over the same data, so time consumption is always minimized.

Also **`gztool` can monitor a growing gzip file** (for example, a log created by rsyslog directly in gzip format) and generate the index on-the-fly, thus reducing in the practice to zero the time of index creation. See the `-S` (*Supervise*) option.

* Index size is approximately 1% or less of compressed gzip file. The bigger the gzip usually the better the proportion.

Note that the size of the index depends on the span between index points on the uncompressed stream: by default it is 10 MiB, this means that when retrieving randomly situated data only 10/2 = 5 MiB of uncompressed data must be decompressed (on average) no matter the size of the gzip file - which is a fairly low value!    
The span between index points can be adjusted with `-s` (*span*) option.

Background
==========

By default gzip-compressed files cannot be accessed in random mode: any byte required at position N requires the complete gzip file to be decompressed from the beginning to the N byte.   
Nonetheless Mark Adler, the author of [zlib](https://github.com/madler/zlib), provided years ago a cryptic file named [zran.c](https://github.com/madler/zlib/blob/master/examples/zran.c) that creates an "index" of "windows" filled with 64 kiB of uncompressed data at different positions along the un/compressed file, which can be used to initialize the zlib library and make it behave as if data begin there.   

`gztool` builds upon zran.c to provide a useful command line tool.    
Also, some optimizations has been made:

* **`gztool` can *Supervise* an still-growing gzip file** (for example, a log created by rsyslog directly in gzip format) and generate the index on-the-fly, thus reducing in the practice to zero the time of index creation. See `-S`.
* extraction of data and index creation are interleaved, so there's no waste of time for the index creation.
* **index files are reusable**, so they can be stopped at any time and reused and/or completed later.
* an *ex novo* index file format has been created to store the index
* span between index points is raised by default from 1 to 10 MiB, and can be adjusted with `-s` (*span*).
* windows **are compressed** in file
* windows are not loaded in memory unless they're needed, so the application memory footprint is fairly low (< 1 MiB)
* **Compatible with [`bgzip` files](http://www.htslib.org/doc/bgzip.html)**
* **Compatible with complete `gzip` concatenated files**
* data can be provided from/to stdin/stdout

More functionality is planned.

Compilation
===========

*zlib.a* archive library is needed in order to compile `gztool`: the package providing it is actually `zlib1g-dev` (this may vary on your system):

    $ sudo apt install zlib1g-dev

    $ gcc -O3 -o gztool gztool.c -lz -lm

Compilation in Windows
======================

Compilation in Windows is possible using *gcc* for Windows and compiling the original *zlib* code to obtain the needed archive library `libz.a`.    
Please, note that **executables for different platforms are provided** on the [Release page](https://github.com/circulosmeos/gztool/releases).    

* download gcc for Windows: [mingw-w64](https://sourceforge.net/projects/mingw-w64/files/latest/download)
* Install it and add the path for gcc.exe [to your Windows PATH](https://www.computerhope.com/issues/ch000549.htm)
* Download zlib code and compile it with your new gcc: [zlib](https://github.com/madler/zlib/archive/master.zip)
* The previous step generates the file **zlib.a** that you need to compile *gztool*:
Copy gztool.c to the directory where you compiled zlib, and do:
    
    `gcc -static -O3 -I. -o gztool gztool.c libz.a -lm`


Usage
=====

      gztool (v0.6.28)
      GZIP files indexer and data retriever.
      Create small indexes for gzipped files and use them
      for quick and random positioned data extraction.
      No more waiting when the end of a 10 GiB gzip is needed!
      //github.com/circulosmeos/gztool (by Roberto S. Galende)

      $ gztool [-b #] [-s #] [-v #] [-cdefFhilStTW] [-I <INDEX>] <FILE>...

      Note that actions `-bStT` proceed to an index file creation (if
      none exists) INTERLEAVED with data extraction. As extraction and
      index creation occur at the same time there's no waste of time.
      Also you can interrupt actions at any moment and the remaining
      index file will be reused (and completed if necessary) on the
      next gztool run over the same data.

     -b #: extract data from indicated uncompressed byte position of
         gzip file (creating or reusing an index file) to STDOUT.
         Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).
     -c: utility: raw-gzip-compress indicated file to STDOUT
     -d: utility: raw-gzip-decompress indicated file to STDOUT
     -e: if multiple files are indicated, continue on error (if any)
     -f: force index overwriting from scratch, if one exists
     -F: force index creation/completion first, and then action: if
         `-F` is not used, index is created interleaved with actions.
     -h: print this help
     -i: create index for indicated gzip file (For 'file.gz'
         the default index file name will be 'file.gzi').
     -I INDEX: index file name will be 'INDEX'
     -l: check and list info contained in indicated index file
     -s #: span in uncompressed MiB between index points when
         creating the index. By default is `10`.
     -S: Supervise indicated file: create a growing index,
         for a still-growing gzip file. (`-i` is implicit).
     -t: tail (extract last bytes) to STDOUT on indicated gzip file
     -T: tail (extract last bytes) to STDOUT on indicated still-growing
         gzip file, and continue Supervising & extracting to STDOUT.
     -v #: output verbosity: from `0` (none) to `3` (maniac)
         Default is `1` (normal).
     -W: do not Write index to disk. But if one is already available
         read and use it. Useful if the index is still under a `-S` run.

      Example: Extract data from 1 GiB byte (byte 2^30) on,
      from `myfile.gz` to the file `myfile.txt`. Also gztool will
      create (or reuse, or complete) an index file named `myfile.gzi`:
      $ gztool -b 1G myfile.gz > myfile.txt

Please, **note that STDOUT is used for data extraction** with `-bcdtT` modifiers.

When using `S` (*Supervise*), the gzipped file may not yet exist when the command is executed, but it will wait patiently for its creation.

Examples of use
===============

Make an index for `test.gz`. The index will be named `test.gzi`:

    $ gztool -i test.gz

Make an index for `test.gz` with name `test.index`:

    $ gztool -I test.index test.gz

Retrieve data from uncompressed byte position 1000000 inside test.gz. Index file will be created **at the same time** (named `test.gzi`):

    $ gztool -b 1m test.gz

**Supervise an still-growing gzip file and generate the index for it on-the-fly**. The index file name will be `openldap.log.gzi` in this case. `gztool` will execute until the gzip-file data is terminated.

    $ gztool -S openldap.log.gz

The previous command can be sent to background and with no verbosity, so we can forget about it:

    $ gztool -v0 -S openldap.log.gz &

Creating and index for all "\*gz" files in a directory:

    $ gztool -i *gz

    ACTION: Create index

    Index file 'data.gzi' already exists and will be used.
    (Use `-f` to force overwriting.)
    Processing 'data.gz' ...
    Index already complete. Nothing to do.

    Processing 'data_project.2.tar.gz' ...
    Built index with 73 access points.
    Index written to 'data_project.2.tar.gzi'.

    Processing 'project_2.gz' ...
    Built index with 3 access points.
    Index written to 'project_2.gzi'.

Extract data from `project.gz` byte at 1 GiB to STDOUT, and use `grep` on this output. Index file name will be `project.gzi`:

    $ gztool -b 1G project.gz | grep -i "balance = "

Please, note that STDOUT is used for data extraction with `-bcdtT` modifiers, so an explicit command line redirection is needed if output is to be stored in a file:

    $ gztool -b 99m project.gz > uncompressed.data

Show internals of all index files in this directory. `-e` is used not to stop the process on the first error, if a `*.gzi` file is not a valid gzip index file. The `-v2` verbosity option will show data about each index point:

    $ gztool -v2 -el *.gzi

    Checking index file 'accounting.gz.gzi' ...
            Number of index points:    73
        Size of uncompressed file: 81285120
        List of points:
           @ compressed/uncompressed byte (index data size in Bytes), ...
        @ 10 / 0 ( 22870 ), @ 1034606 / 1069037 ( 32784 ), @ 2085195 / 2120817 ( 32784 ), @ 3136550 / 3180475 ( 32784 ), ...
    ...

Extract data from a gzipped file which index is still growing with a `gztool -S` process that is monitoring the (still-growing) gzip file: in this case the use of `-W` will not try to update the index on disk so the other process is not disturb! (Note that since version 0.3, `gztool` always tries to update the index used if it thinks it's necessary).

    $ gztool -Wb 100k still-growing-gzip-file.gz > mytext

To tail to stdout, *like a* `tail -f`, an still-growing gzip file (an index file will be created with name `still-growing-gzip-file.gzi` in this case):

    $ gztool -T still-growing-gzip-file.gz

More on files still being "Supervised" (`-S`) by another `gztool` instance: they can also be tailed *à la* `tail -f` without updating the index on disk using `-W`:

    $ gztool -WT still-growing-gzip-file.gz

Index file format
=================

Index files are created by default with extension '.gzi' appended to the original file name of the gzipped file:

    filename.gz     ->     filename.gzi

If the original file doesn't have ".gz" extension, ".gzi" will be appended - for example:

    filename.tgz     ->     filename.tgz.gzi

There's a special header to mark ".gzi" files as index files usable for this app:

    +-----------------+-----------------+
    |   0x0 64 bits   |    "gzipindx"   |     ~     16 bytes = 128 bits
    +-----------------+-----------------+

Note that this header has been built so that this format will be "compatible" with index files generated for *bgzip*-compressed files. **[bgzip](http://www.htslib.org/doc/bgzip.html)** files are totally compatible with gzip: they've just been made so every 64 kiB of uncompressed data the zlib library is restart, so they are composed of independent gzipped blocks one after another. The *bgzip* command can create index files for bgzipped files in less time and with much less space than with this tool as they're already almost random-access-capable. The first 64-bit-long of bgzip files is the count of index pairs that are next, so with this 0x0 header gztool-index-files can be ignored by *bgzip* command and so bgzipped and gzipped files and their indexes could live in the same folder without collision.

All numbers are stored in big-endian byte order (platform independently). Big-endian numbers are easier to interpret than little-endian ones when inspecting them with an hex editor (like `od` for example).

Next, and almost one-to-one pass of *struct access* is serialize to the file. *access->have* and *access->size* are both written even though they'd always be equal. If the index file is generated with `-S` or `-T` on a still-growing gzip file (or somehow the index hasn't been completed because the gzip data was still incomplete), the values on disk for *access->have* and *access->size* will be respectively 0x0..0 and "number of actual index points written" (both uint64_t) to mark this fact. *access->size* MAY be UINT64_MAX to avoid the need to write this value as the number of index points are added to the file. As the index is incremental the number of points can be determined by reading the index until EOF.

After that, comes all the *struct point* data. As previously said, windows are compressed so a previous register (32 bits) with their length is needed. Note that an index point with a window of size zero is possible in principle (and will be ignored).

After all the *struct point* structures data, the original uncompressed data size of the gzipped file is stored (64 bits).

Please note that not all stored numbers are 64-bit long. This is because some counters will always fit in less length. Refer to code.

With 64 bit long numbers, the index could potentially manage files up to 2^64 = 16 EiB (16 777 216 TiB).

Other tools which try to provide random access to gzipped files
===============================================================

* also based on [zlib's `zran.c`](https://github.com/madler/zlib/blob/master/examples/zran.c):

  * Perl module [Gzip::RandomAccess](https://metacpan.org/pod/Gzip::RandomAccess). It seems to create the index only in memory, after decompressing the whole gzip file.
  * Go package [gzran](https://github.com/coreos/gzran). It seems to create the index only in memory, after decompressing the whole gzip file.

* [*bgzip*](https://github.com/samtools/htslib/blob/develop/bgzip.c) command, available in linux with package *tabix* (used for chromosome handling). This discussion about the implementation is very interesting: [random-access-to-zlib-compressed-files](https://lh3.github.io/2014/07/05/random-access-to-zlib-compressed-files). I've developed also a [`bgztail` command tool](https://github.com/circulosmeos/bgztail) to tail bgzipped files, even as they grow.

* [indexed_gzip](https://github.com/pauldmccarthy/indexed_gzip) Fast random access of gzip files in Python: it also creates file indexes, but they are not as small and they cannot be reused as easily as with `gztool`.

* [zindex](https://github.com/mattgodbolt/zindex) creates SQLite indexes for text files based on regular expressions

* interesting article on parallel gzip decompression: [`Parallel decompression of gzip-compressed files and random access to DNA sequences`](https://arxiv.org/pdf/1905.07224.pdf)

Version
=======

This version is **v0.6.28**.

Please, read the *Disclaimer*. This is still a beta release. In case of any errors, please open an *Issue*.

License
=======

    A work by Roberto S. Galende
    distributed under the same License terms covering
    zlib from Mark Adler (aka Zlib license):
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

Disclaimer
==========

**This software is provided "as is", without warranty of any kind, express or implied. In no event will the authors be held liable for any damages arising from the use of this software.**

Author
======

by [Roberto S. Galende](loopidle@gmail.com)   

on code by Mark Adler's [zlib](https://github.com/madler/zlib).
