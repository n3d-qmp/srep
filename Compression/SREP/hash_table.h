#pragma once
#include "hashes.h"
#include "srep.h"

typedef size_t HashValue;            // Hash of L-byte block, used as first step to find the match
typedef uint32 StoredHashValue;      // Hash value stored in hasharr[]
typedef uint64 BigHash;              // We need 64 bits for storing index+value in the chunkarr+hasharr
typedef uint32 Chunk;                // Uncompressed file are splitted into L-byte chunks, it's the number of chunk in the file
static const Chunk  MAX_CHUNK = Chunk(-1),  NOT_FOUND = 0;
const int MAX_HASH_CHAIN = 12;

#define min_hash_size(n)   (((n)/4+1)*5)     /* Minimum size of hash for storing n elements */


// Improves EXHAUSTIVE_SEARCH by filtering out ~90% of false matches
struct SliceHash {
  typedef uint32 entry;
  static const NUMBER BITS = 4,
                      ONES = (1 << BITS) -
                             1; // how much bits used from every calculated hash

  entry *h; // Array holding all slice hashes, one `entry` per L bytes
  Offset memreq;
  int check_slices, errcode;
  NUMBER L, slices_in_block, slice_size;

  SliceHash(Offset filesize, unsigned _L, unsigned MIN_MATCH,
            int io_accelerator);

  // Actual memory allocation (should be performed after allocation of more
  // frequently accessed arrays of the HashTable)
  void alloc(LPType LargePageMode);
  ~SliceHash();

  // Hash of provided buffer
  entry hash(void *ptr, int size);

  // Fill h[] with hashes of slices of each chunk in the buf
  void prepare_buffer(Offset offset, char *buf, int size);

  // Return TRUE if match MAY BE large enough, FALSE - if that's absolutely
  // impossible
  bool check(Chunk chunk, void *p, int i, int block_size);
};

// Match search engine
struct HashTable {
  bool ROUND_MATCHES;
  bool COMPARE_DIGESTS;
  bool PRECOMPUTE_DIGESTS;
  bool CONTENT_DEFINED_CHUNKING;
  int _errcode;
  size_t L;
  MMAP_FILE &mmap_infile;
  Offset filesize;
  Offset total_chunks;
  Chunk curchunk, chunknum_mask, hash_mask;
  Offset hs;
  size_t hashsize, hashsize1, hash_shift;
  Chunk *chunkarr;
  StoredHashValue *hasharr;
  Offset *startarr;
  Digest *digestarr;
  SliceHash slicehash;
  VDigest MainDigest, PrepDigest;

  // bitarr[] used for fast probing of hash values - it helps to detect whether
  // we ever seen such hash value before
  size_t bitarrsize;
  size_t bitshift;
  BYTE *bitarr;

  HashTable(bool _ROUND_MATCHES, bool _COMPARE_DIGESTS,
            bool _PRECOMPUTE_DIGESTS, bool INMEM_COMPRESSION,
            bool _CONTENT_DEFINED_CHUNKING, unsigned _L, unsigned MIN_MATCH,
            int io_accelerator, unsigned BITARR_ACCELERATOR,
            MMAP_FILE &_mmap_infile, Offset _filesize, LPType LargePageMode);
  ~HashTable() ;

  // Return errcode if any
  int errcode();

  // How much memory required for hash tables with given file and compression
  // method settings
  Offset memreq() ;

  // Performed once for each block read
  void prepare_buffer(Offset offset, char *buf, int size) ;

  // A quick first probe using bitarr[]
  template <unsigned ACCELERATOR>
  void prefetch_check_match_possibility(HashValue hash);
  template <unsigned ACCELERATOR> bool check_match_possibility(HashValue hash) ;
  template <unsigned ACCELERATOR> void mark_match_possibility(HashValue hash) ;

#define stored_hash(hash2)                                                     \
  ((hash2) >>                                                                  \
   (CHAR_BIT * (sizeof(BigHash) -                                              \
                sizeof(StoredHashValue)))) /* value saved in hasharr[] */
#define index_hash(hash2) (hash2)          /* value used to index chunkarr[] */

  // Run add_hash0/prefetch_match0/find_match0 with index/stored values deduced
  // in the SAME way from hash2
  Chunk add_hash(void *p, int i, int block_size, Chunk curchunk, BigHash hash2,
                 Offset new_offset);
  void prefetch_match(BigHash hash2) ;
  Chunk find_match(void *p, int i, int block_size, BigHash hash2,
                   Offset new_offset) ;

#define first_hash_slot(index) (index) /* the first chunkarr[] slot */
#define next_hash_slot(index, h)                                               \
  ((h)*123456791 + ((h) >> 16) +                                               \
   462782923) /* jump to the next chunkarr[] slot */
#define hash_index(h)                                                          \
  ((h)&hashsize1) /* compute chunkarr[] index for given hash value h;  we      \
                     prefer to use lower bits since higher ones may be shared  \
                     with stored_hash value */

#define chunkarr_value(hash, chunk)                                            \
  ((Chunk(hash) & hash_mask) +                                                 \
   (chunk)) /* combine hash and number of chunk into the one Chunk value for   \
               storing in the chunkarr[] */
#define get_hash(value)                                                        \
  ((value)&hash_mask) /* get hash from the combined value */
#define get_chunk(value)                                                       \
  ((value)&chunknum_mask) /* get chunk number from the combined value */

#define speed_opt true /* true: don't use slicehash to try >1 match in -m5 */

  // Add chunk pointed by p to hash, returning equivalent previous chunk
  template <bool CDC>
  Chunk add_hash0(void *p, int i, int block_size, Chunk curchunk, BigHash index,
                  StoredHashValue stored_value, Offset new_offset) ;

  // Prefetch chunkarr[] element for the find_match0()
  void prefetch_match0(BigHash index) ;

  // Find previous L-byte chunk with the same contents
  Chunk find_match0(void *p, int i, int block_size, BigHash index,
                    StoredHashValue stored_value, Offset new_offset) ;

  // Length of match, in bytes
  unsigned match_len(Chunk start_chunk, char *min_p, char *start_p,
                     char *last_p, Offset offset, char *buf,
                     unsigned *add_len) ;

  // Chunk start position
  Offset start(Chunk chunk) ;

  // Chunk size in -m1/-m2 mode
  Offset chunksize_CDC(Chunk chunk) ;

  // Индексировать новый блок и вернуть смещение до эквивалентного ему старого
  // (или 0)
  Offset find_match_CDC(Offset offset, void *p, int size, BYTE *vhashes);
};