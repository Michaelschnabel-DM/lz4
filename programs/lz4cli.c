/*
  LZ4cli - LZ4 Command Line Interface
  Copyright (C) Yann Collet 2011-2015

  GPL v2 License

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

  You can contact the author at :
  - LZ4 source repository : https://github.com/lz4/lz4
  - LZ4 public forum : https://groups.google.com/forum/#!forum/lz4c
*/
/*
  Note : this is stand-alone program.
  It is not part of LZ4 compression library, it is a user program of the LZ4 library.
  The license of LZ4 library is BSD.
  The license of xxHash library is BSD.
  The license of this compression CLI program is GPLv2.
*/

/**************************************
*  Tuning parameters
***************************************/
/* ENABLE_LZ4C_LEGACY_OPTIONS :
   Control the availability of -c0, -c1 and -hc legacy arguments
   Default : Legacy options are disabled */
/* #define ENABLE_LZ4C_LEGACY_OPTIONS */


/**************************************
*  Compiler Options
***************************************/
/* cf. http://man7.org/linux/man-pages/man7/feature_test_macros.7.html */
#define _XOPEN_VERSION 600 /* POSIX.2001, for fileno() within <stdio.h> on unix */


/****************************
*  Includes
*****************************/
#include <stdint.h>

#include "util.h"     /* Compiler options, UTIL_HAS_CREATEFILELIST */
#include <stdio.h>    /* fprintf, getchar */
#include <stdlib.h>   /* exit, calloc, free */
#include <string.h>   /* strcmp, strlen */
#include "bench.h"    /* BMK_benchFile, BMK_SetNbIterations, BMK_SetBlocksize, BMK_SetPause */
#include "lz4io.h"    /* LZ4IO_compressFilename, LZ4IO_decompressFilename, LZ4IO_compressMultipleFilenames */
#include "lz4hc.h"    /* LZ4HC_DEFAULT_CLEVEL */
#include "lz4.h"      /* LZ4_VERSION_STRING */


/*-************************************
*  OS-specific Includes
**************************************/
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || (defined(__APPLE__) && defined(__MACH__)) || defined(__DJGPP__)  /* https://sourceforge.net/p/predef/wiki/OperatingSystems/ */
#  include <unistd.h>   /* isatty */
#  define IS_CONSOLE(stdStream) isatty(fileno(stdStream))
#elif defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__)
#  include <io.h>       /* _isatty */
#  define IS_CONSOLE(stdStream) _isatty(_fileno(stdStream))
#else
#  define IS_CONSOLE(stdStream) 0
#endif


/*****************************
*  Constants
******************************/
#define COMPRESSOR_NAME "LZ4 command line interface"
#define AUTHOR "Yann Collet"
#define WELCOME_MESSAGE "*** %s %i-bits v%s, by %s ***\n", COMPRESSOR_NAME, (int)(sizeof(void*)*8), LZ4_VERSION_STRING, AUTHOR
#define LZ4_EXTENSION ".lz4"
#define LZ4CAT "lz4cat"
#define UNLZ4 "unlz4"

#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

#define LZ4_BLOCKSIZEID_DEFAULT 7


/*-************************************
*  Macros
***************************************/
#define DISPLAY(...)           fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...)   if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static unsigned displayLevel = 2;   /* 0 : no display ; 1: errors only ; 2 : downgradable normal ; 3 : non-downgradable normal; 4 : + information */


/*-************************************
*  Exceptions
***************************************/
#define DEBUG 0
#define DEBUGOUTPUT(...) if (DEBUG) DISPLAY(__VA_ARGS__);
#define EXM_THROW(error, ...)                                             \
{                                                                         \
    DEBUGOUTPUT("Error defined at %s, line %i : \n", __FILE__, __LINE__); \
    DISPLAYLEVEL(1, "Error %i : ", error);                                \
    DISPLAYLEVEL(1, __VA_ARGS__);                                         \
    DISPLAYLEVEL(1, "\n");                                                \
    exit(error);                                                          \
}


/*-************************************
*  Version modifiers
***************************************/
#define EXTENDED_ARGUMENTS
#define EXTENDED_HELP
#define EXTENDED_FORMAT
#define DEFAULT_COMPRESSOR   LZ4IO_compressFilename
#define DEFAULT_DECOMPRESSOR LZ4IO_decompressFilename
int LZ4IO_compressFilename_Legacy(const char* input_filename, const char* output_filename, int compressionlevel);   /* hidden function */


/*-***************************
*  Functions
*****************************/
static int usage(const char* exeName)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [arg] [input] [output]\n", exeName);
    DISPLAY( "\n");
    DISPLAY( "input   : a filename\n");
    DISPLAY( "          with no FILE, or when FILE is - or %s, read standard input\n", stdinmark);
    DISPLAY( "Arguments :\n");
    DISPLAY( " -1     : Fast compression (default) \n");
    DISPLAY( " -9     : High compression \n");
    DISPLAY( " -d     : decompression (default for %s extension)\n", LZ4_EXTENSION);
    DISPLAY( " -z     : force compression\n");
    DISPLAY( " -f     : overwrite output without prompting \n");
    DISPLAY( "--rm    : remove source file(s) after successful de/compression \n");
    DISPLAY( " -h/-H  : display help/long help and exit\n");
    return 0;
}

static int usage_advanced(const char* exeName)
{
    DISPLAY(WELCOME_MESSAGE);
    usage(exeName);
    DISPLAY( "\n");
    DISPLAY( "Advanced arguments :\n");
    DISPLAY( " -V     : display Version number and exit\n");
    DISPLAY( " -v     : verbose mode\n");
    DISPLAY( " -q     : suppress warnings; specify twice to suppress errors too\n");
    DISPLAY( " -c     : force write to standard output, even if it is the console\n");
    DISPLAY( " -t     : test compressed file integrity\n");
    DISPLAY( " -m     : multiple input files (implies automatic output filenames)\n");
#ifdef UTIL_HAS_CREATEFILELIST
    DISPLAY( " -r     : operate recursively on directories (sets also -m)\n");
#endif
    DISPLAY( " -l     : compress using Legacy format (Linux kernel compression)\n");
    DISPLAY( " -B#    : Block size [4-7] (default : 7)\n");
    DISPLAY( " -BD    : Block dependency (improve compression ratio)\n");
    /* DISPLAY( " -BX    : enable block checksum (default:disabled)\n");   *//* Option currently inactive */
    DISPLAY( "--no-frame-crc : disable stream checksum (default:enabled)\n");
    DISPLAY( "--content-size : compressed frame includes original size (default:not present)\n");
    DISPLAY( "--[no-]sparse  : sparse mode (default:enabled on file, disabled on stdout)\n");
    DISPLAY( "Benchmark arguments :\n");
    DISPLAY( " -b#    : benchmark file(s), using # compression level (default : 1) \n");
    DISPLAY( " -e#    : test all compression levels from -bX to # (default : 1)\n");
    DISPLAY( " -i#    : minimum evaluation time in seconds (default : 3s)\n");
    DISPLAY( " -B#    : cut file into independent blocks of size # bytes [32+]\n");
    DISPLAY( "                      or predefined block size [4-7] (default: 7)\n");
#if defined(ENABLE_LZ4C_LEGACY_OPTIONS)
    DISPLAY( "Legacy arguments :\n");
    DISPLAY( " -c0    : fast compression\n");
    DISPLAY( " -c1    : high compression\n");
    DISPLAY( " -hc    : high compression\n");
    DISPLAY( " -y     : overwrite output without prompting \n");
#endif /* ENABLE_LZ4C_LEGACY_OPTIONS */
    EXTENDED_HELP;
    return 0;
}

static int usage_longhelp(const char* exeName)
{
    usage_advanced(exeName);
    DISPLAY( "\n");
    DISPLAY( "****************************\n");
    DISPLAY( "***** Advanced comment *****\n");
    DISPLAY( "****************************\n");
    DISPLAY( "\n");
    DISPLAY( "Which values can [output] have ? \n");
    DISPLAY( "---------------------------------\n");
    DISPLAY( "[output] : a filename \n");
    DISPLAY( "          '%s', or '-' for standard output (pipe mode)\n", stdoutmark);
    DISPLAY( "          '%s' to discard output (test mode) \n", NULL_OUTPUT);
    DISPLAY( "[output] can be left empty. In this case, it receives the following value :\n");
    DISPLAY( "          - if stdout is not the console, then [output] = stdout \n");
    DISPLAY( "          - if stdout is console : \n");
    DISPLAY( "               + for compression, output to filename%s \n", LZ4_EXTENSION);
    DISPLAY( "               + for decompression, output to filename without '%s'\n", LZ4_EXTENSION);
    DISPLAY( "                    > if input filename has no '%s' extension : error \n", LZ4_EXTENSION);
    DISPLAY( "\n");
    DISPLAY( "Compression levels : \n");
    DISPLAY( "---------------------\n");
    DISPLAY( "-0 ... -2  => Fast compression, all identicals\n");
    DISPLAY( "-3 ... -%d => High compression; higher number == more compression but slower\n", LZ4HC_MAX_CLEVEL);
    DISPLAY( "\n");
    DISPLAY( "stdin, stdout and the console : \n");
    DISPLAY( "--------------------------------\n");
    DISPLAY( "To protect the console from binary flooding (bad argument mistake)\n");
    DISPLAY( "%s will refuse to read from console, or write to console \n", exeName);
    DISPLAY( "except if '-c' command is specified, to force output to console \n");
    DISPLAY( "\n");
    DISPLAY( "Simple example :\n");
    DISPLAY( "----------------\n");
    DISPLAY( "1 : compress 'filename' fast, using default output name 'filename.lz4'\n");
    DISPLAY( "          %s filename\n", exeName);
    DISPLAY( "\n");
    DISPLAY( "Short arguments can be aggregated. For example :\n");
    DISPLAY( "----------------------------------\n");
    DISPLAY( "2 : compress 'filename' in high compression mode, overwrite output if exists\n");
    DISPLAY( "          %s -9 -f filename \n", exeName);
    DISPLAY( "    is equivalent to :\n");
    DISPLAY( "          %s -9f filename \n", exeName);
    DISPLAY( "\n");
    DISPLAY( "%s can be used in 'pure pipe mode'. For example :\n", exeName);
    DISPLAY( "-------------------------------------\n");
    DISPLAY( "3 : compress data stream from 'generator', send result to 'consumer'\n");
    DISPLAY( "          generator | %s | consumer \n", exeName);
#if defined(ENABLE_LZ4C_LEGACY_OPTIONS)
    DISPLAY( "\n");
    DISPLAY( "***** Warning  *****\n");
    DISPLAY( "Legacy arguments take precedence. Therefore : \n");
    DISPLAY( "---------------------------------\n");
    DISPLAY( "          %s -hc filename\n", exeName);
    DISPLAY( "means 'compress filename in high compression mode'\n");
    DISPLAY( "It is not equivalent to :\n");
    DISPLAY( "          %s -h -c filename\n", exeName);
    DISPLAY( "which would display help text and exit\n");
#endif /* ENABLE_LZ4C_LEGACY_OPTIONS */
    return 0;
}

static int badusage(const char* exeName)
{
    DISPLAYLEVEL(1, "Incorrect parameters\n");
    if (displayLevel >= 1) usage(exeName);
    exit(1);
}


static void waitEnter(void)
{
    DISPLAY("Press enter to continue...\n");
    (void)getchar();
}


/*! readU32FromChar() :
    @return : unsigned integer value reach from input in `char` format
    Will also modify `*stringPtr`, advancing it to position where it stopped reading.
    Note : this function can overflow if result > MAX_UINT */
static unsigned readU32FromChar(const char** stringPtr)
{
    unsigned result = 0;
    while ((**stringPtr >='0') && (**stringPtr <='9'))
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    return result;
}

typedef enum { om_auto, om_compress, om_decompress, om_test, om_bench } operationMode_e;

const char* readFile(const char* filename, uint32_t *orig_size, uint32_t *comp_size) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    uint32_t value = 0;
    uint8_t test;

    fread(&test, sizeof(uint8_t), 1, file);
    value += test << 0;

    fread(&test, sizeof(uint8_t), 1, file);
    value += test << 8;

    fread(&test, sizeof(uint8_t), 1, file);
    value += test << 16;

    fread(&test, sizeof(uint8_t), 1, file);
    value += test << 24;

    *comp_size = value;

    uint32_t value_2 = 0;

    fread(&test, sizeof(uint8_t), 1, file);
    value_2 += test << 0;

    fread(&test, sizeof(uint8_t), 1, file);
    value_2 += test << 8;

    fread(&test, sizeof(uint8_t), 1, file);
    value_2 += test << 16;

    fread(&test, sizeof(uint8_t), 1, file);
    value_2 += test << 24;

    *orig_size = value_2;

    // Determine the size of the file
    fseek(file, 0, SEEK_END);
    long size = ftell(file) - 8;
    fseek(file, 8, SEEK_SET);

    // Allocate memory to store the file content
    char* content = malloc(size + 1); // +1 for null terminator
    if (!content) {
        perror("Error allocating memory");
        exit(EXIT_FAILURE);
    }

    // Read the file content into the allocated memory
    size_t bytesRead = fread((void*)content, 1, size, file);
    if (bytesRead != size) {
        perror("Error reading file");
        exit(EXIT_FAILURE);
    }

    // Add null terminator at the end
    content[size] = '\0';

    // Close the file
    fclose(file);

    return content;
}

int main()
{
    uint32_t orig_size, comp_size;
    const char* filename = "C:\\Users\\micha\\Desktop\\compressed_rust\\1.comp";
    const char* fileContent = readFile(filename, &orig_size, &comp_size);

    char* output_buffer = malloc(orig_size);
    LZ4_decompress_fast(fileContent, output_buffer, orig_size);

    FILE* outputFile = fopen("C:\\Users\\micha\\Desktop\\compressed_rust\\1.decomp", "w");

    if (!outputFile) {
        fprintf(stderr, "Could not open the file for writing.\n");
        return 1;
    }

    fwrite(output_buffer, sizeof(char), strlen(output_buffer), outputFile);

    // Close the file when you're done
    fclose(outputFile);

    free((void*)fileContent);

    return 0;
}
