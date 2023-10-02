#pragma once
static const char *program_version = "SREP 3.93a beta",
                  *program_date = "October 11, 2014";
static const char *program_description =
    "huge-dictionary LZ77 preprocessor   (c) Bulat.Ziganshin@gmail.com";
static const char *program_homepage = "http://freearc.org/research/SREP39.aspx";

#include <algorithm>
#include <malloc.h>
#include <math.h>
#include <set>
#include <stack>
#include <stdarg.h>
#include <stdio.h>
#include <vector>

#include "Common.h"
#include "Compression.h"
#include "MultiThreading.h"

// Constants defining compressed file format
const uint SREP_SIGNATURE = 0x50455253;
const uint SREP_FORMAT_VERSION1 = 1;
const uint SREP_FORMAT_VERSION2 = 2;
const uint SREP_FORMAT_VERSION3 = 3;
const uint SREP_FORMAT_VERSION4 = 4;
const uint SREP_FOOTER_VERSION1 = 1;
enum SREP_METHOD {
  SREP_METHOD0 = 0,
  SREP_METHOD1,
  SREP_METHOD2,
  SREP_METHOD3,
  SREP_METHOD4,
  SREP_METHOD5,
  SREP_METHOD_FIRST = SREP_METHOD0,
  SREP_METHOD_LAST = SREP_METHOD5
};
typedef uint32 STAT;
const int STAT_BITS = sizeof(STAT) * CHAR_BIT, ARCHIVE_HEADER_SIZE = 4,
          BLOCK_HEADER_SIZE = 3, MAX_HEADER_SIZE = 4, MAX_HASH_SIZE = 256;
enum COMMAND_MODE { COMPRESSION, DECOMPRESSION, INFORMATION };
static const char *SREP_EXT = ".srep";

// Compression algorithms constants and defaults
const int MINIMAL_MIN_MATCH = 16; // minimum match length that sometimes allows
                                  // to reduce file using the match
const int DEFAULT_MIN_MATCH =
    32; // minimum match length that usually produces smallest compressed file
        // (don't taking into account further compression)

// Program exit codes
enum {
  NO_ERRORS = 0,
  WARNINGS = 1,
  ERROR_CMDLINE = 2,
  ERROR_IO = 3,
  ERROR_COMPRESSION = 4,
  ERROR_MEMORY = 5
};

typedef uint64 Offset; // Filesize or position inside file

// Performance counters printed by -pc option - useful for further program
// optimization
static struct {
  Offset max_offset, find_match, find_match_memaccess, check_hasharr,
      hash_found, check_len, record_match, total_match_len;
} pc;

void error(int ExitCode, char *ErrmsgFormat...); // Exit on error

#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#define _32_or_64(_32, _64) (_64)
#define _32_only(_32) (void(0))
typedef size_t
    NUMBER; // best choice for loop index variables on most 64-bit compilers
#else
#define _32_or_64(_32, _64) (_32)
#define _32_only(_32) (_32)
typedef int
    NUMBER; // best choice for loop index variables on most 32-bit compilers
#endif

#define INDEX_LZ_FOOTER_SIZE (sizeof(STAT) * 6)

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Match handling
// ***********************************************************************************************************************************
// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Structure storing LZ match info
struct LZ_MATCH {
  LZ_MATCH() : src(Offset(-1)), dest(Offset(-1)), len(STAT(-1)) {}
  Offset src, dest; // LZ match source & destination absolute positions in file
  STAT len;         // Match length
};

// Compare LZ matches by source position (for lz_matches[])
bool order_by_LZ_match_src(const LZ_MATCH &left, const LZ_MATCH &right);

// Compare LZ matches by destination position (for lz_matches_by_dest[])
bool order_by_LZ_match_dest(const LZ_MATCH &left, const LZ_MATCH &right);

// Maximum number of STAT values per compressed block. For every L input bytes,
// we can write up to 4 STAT values to statbuf
#define MAX_STATS_PER_BLOCK(block_size, L) (((block_size) / (L) + 1) * 4)

// FUTURE_LZ needs more space because matches are moved to their source blocks
// So we just alloc block_size bytes
#define FUTURELZ_MAX_STATS_PER_BLOCK(block_size) ((block_size) / sizeof(STAT))

// Number of STAT values used to encode one LZ match
#define STATS_PER_MATCH(ROUND_MATCHES) (ROUND_MATCHES ? 3 : 4)

// Encode one LZ record to stat[]
#define ENCODE_LZ_MATCH(stat, ROUND_MATCHES, L, lit_len, lz_match_offset,      \
                        lz_match_len)                                          \
  unsigned L1 =                                                                \
      (ROUND_MATCHES                                                           \
           ? L                                                                 \
           : 1); /* lz_match_len should be divisible by L in -m3 mode */       \
  *stat++ = (lit_len);                                                         \
  *stat++ = (lz_match_offset) / L1;                                            \
  if (!ROUND_MATCHES)                                                          \
    *stat++ = ((lz_match_offset) / L1) >> STAT_BITS;                           \
  if ((lz_match_len) < L)                                                      \
    error(ERROR_COMPRESSION, "ENCODE_LZ_MATCH: match len too small: %d < %d",  \
          (lz_match_len), L);                                                  \
  *stat++ = ((lz_match_len)-L) / L1;

// Decode one LZ record from stat[]
#define DECODE_LZ_MATCH(stat, FUTURE_LZ, ROUND_MATCHES, L, basic_pos, lit_len, \
                        LZ_MATCH_TYPE, lz_match)                               \
  unsigned L1 =                                                                \
      (ROUND_MATCHES                                                           \
           ? L                                                                 \
           : 1); /* lz_match_len should be divisible by L in -m3 mode */       \
  unsigned lit_len = *stat++; /* length of literal (copied from in[]) */       \
  Offset lz_match_offset =                                                     \
      *stat++; /* LZ.dest-LZ.src (divided by L when ROUND_MATCHES==true) */    \
  if (!ROUND_MATCHES)                                                          \
    lz_match_offset += Offset(*stat++)                                         \
                       << STAT_BITS; /* High word of lz_match_offset */        \
  lz_match_offset *= L1;                                                       \
  LZ_MATCH_TYPE lz_match;                                                      \
  lz_match.len = (*stat++) * L1 + L;                                           \
  if (!FUTURE_LZ) {                                                            \
    lz_match.dest = (basic_pos) + lit_len;                                     \
    lz_match.src = lz_match.dest / L1 * L1 - lz_match_offset;                  \
  } else {                                                                     \
    lz_match.src = (basic_pos) + lit_len;                                      \
    lz_match.dest = lz_match.src + lz_match_offset;                            \
  }

void memcpy_lz_match(void *_dest, void *_src, unsigned len);
// Structure describing one compressed block
struct COMPRESSED_BLOCK {
  COMPRESSED_BLOCK *next;  // Next block in chain
  Offset start, end;       // First and next-after-last byte of the block
  unsigned size;           // Bytes in uncompressed block
  STAT *header;            // Block header data
  STAT *statbuf, *statend; // LZ matches in the block
};

void print_info(const char *prefix_str, Offset max_ram, unsigned maximum_save,
                Offset stat_size, bool ROUND_MATCHES, Offset filesize);
static void clear_window_title();
void signal_handler(int);

#if defined(_MSC_VER)
    //  Microsoft 
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
    //  GCC
    #define EXPORT __attribute__((visibility("default")))
    #define IMPORT
#else
    //  do nothing and hope for the best?
    #define EXPORT
    #define IMPORT
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#ifdef __cplusplus
extern "C"{
#endif
EXPORT int decompress_or_info(FILE *fin, FILE *fout,
                       struct hash_descriptor *selected_hash,
                       const int64 vm_mem, const Offset filesize,
                       const unsigned maximum_save, const FILENAME& finame,
                       const FILENAME &foutname, const COMMAND_MODE cmdmode,
                       const unsigned bufsize = 8 * mb,
                       const unsigned vm_block = 8 * mb);
EXPORT int decompress_or_info_mem(void* bufin, const size_t szin, const char* fout,
                       struct hash_descriptor *selected_hash,
                       const int64 vm_mem, 
                       const unsigned maximum_save, const bool is_out_seekable, const bool is_info_mode,
                       const unsigned bufsize, const unsigned vm_block);
#ifdef __cplusplus
}//extern "C"
#endif
#include "hashes.h"
#include "hash_table.h"
#include "decompress.h"
#include "io.h"
