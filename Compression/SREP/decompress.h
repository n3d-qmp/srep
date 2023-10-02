#pragma once
#include "srep.h"
#include "util.hpp"

// Manages memory in fixed-size chunks in order to prevent uncontrolled memory
// fragmentation provided by malloc()
class MEMORY_MANAGER {
public: // ******************************************************* HIGH-LEVEL
        // API: OPERATIONS ON VARIABLE-SIZED MEMORY AREAS *************
        // Index of chunk
  typedef uint32 INDEX;

  INDEX save(char *ptr, int len);
  void restore(INDEX index, char *ptr, int len);
  void free(INDEX index);

private: // ******************************************************* MID-LEVEL
         // API: OPERATIONS ON CHUNKS ***********************************
  INDEX allocate();
  void mark_as_free(INDEX index);
  INDEX next_index(INDEX index);
  void set_next_index(INDEX index, INDEX next_index);
  char *data_ptr(INDEX index);

private: // ******************************************************* LOW-LEVEL
         // API: OPERATIONS ON BLOCKS ***********************************
  char *chunk_ptr(INDEX index);
  void allocate_block();

private: // ******************************************************* VARIABLES
         // AND CONSTANTS ***********************************************
  std::vector<char *> block_addr;
  INDEX first_free;
  size_t used_chunks, useful_memory;

  static const size_t CHUNK_SIZE = 64,
                      USEFUL_CHUNK_SPACE = CHUNK_SIZE - sizeof(INDEX);
  static const size_t aBLOCK_SIZE = 1 * mb, K = aBLOCK_SIZE / CHUNK_SIZE,
                      K1 = K - 1, lbK = 14;

public:
  static const INDEX INVALID_INDEX = 0;
  MEMORY_MANAGER(size_t memlimit);
  Offset current_mem();
  Offset max_mem();
  Offset available_space();
  static size_t needmem(size_t len) {
    return ((len - 1) / USEFUL_CHUNK_SPACE + 1) * CHUNK_SIZE;
  }
};
// Structure storing Future-LZ match info
struct FUTURE_LZ_MATCH : LZ_MATCH {
  MEMORY_MANAGER::INDEX
      index; // Index of data from the match saved by MEMORY_MANAGER
  FUTURE_LZ_MATCH();

  // Save match data to buffers provided by MEMORY_MANAGER
  void save_match_data(MEMORY_MANAGER &mm, char *ptr);

  // Copy match data to ptr
  void restore_match_data(MEMORY_MANAGER &mm, char *ptr) const;

  // Copy match data to ptr
  void restore_match_data(MEMORY_MANAGER &mm, char *ptr, char *buf,
                          Offset buf_start) const;

  // Free memory allocated by match data
  void free(MEMORY_MANAGER &mm) const;

  // Pseudo-match used to mark positions where restore_from_disk() should be
  // called
  void set_marking_point();
  bool is_marking_point() const;
};
bool operator<(const FUTURE_LZ_MATCH &left, const FUTURE_LZ_MATCH &right);

typedef std::multiset<FUTURE_LZ_MATCH>
    LZ_MATCH_HEAP; // Used to store matches ordered by LZ destination
typedef LZ_MATCH_HEAP::iterator LZ_MATCH_ITERATOR;
typedef LZ_MATCH_HEAP::reverse_iterator LZ_MATCH_REVERSE_ITERATOR;

struct VIRTUAL_MEMORY_MANAGER {
  char *vmfile_name;   // File used as virtual memory
  FILE *vmfile;        // -.-
  Offset VMBLOCK_SIZE; // VM block size
  char *vmbuf;         // Buffer temporarily storing one VM block contents
  std::stack<unsigned>
      free_blocks;    // List of free blocks (that were allocated previosly)
  unsigned new_block; // Next block to alloc if free_blocks list is empty
  Offset total_read, total_write; // Bytes read/written to disk by VMM

  VIRTUAL_MEMORY_MANAGER(char *_vmfile_name, Offset _VMBLOCK_SIZE);
  ~VIRTUAL_MEMORY_MANAGER();
  Offset current_mem();
  Offset max_mem();

  // Save matches with largest LZ.dest to disk
  void save_to_disk(MEMORY_MANAGER &mm, LZ_MATCH_HEAP &lz_matches);

  // Restore matches, pointed by mark, from disk
  void restore_from_disk(MEMORY_MANAGER &mm, LZ_MATCH_HEAP &lz_matches,
                         LZ_MATCH_ITERATOR &mark);
};

bool decompress(bool ROUND_MATCHES, unsigned L, Readable *fout, Offset block_start,
                STAT *stat, char *in, char *inend, char *outbuf, char *outend);
bool decompress_FUTURE_LZ(bool ROUND_MATCHES, unsigned L, Readable *fout,
                          Offset block_start, STAT *statbuf, STAT *statend,
                          char *in, char *inend, char *outbuf, char *outend,
                          MEMORY_MANAGER &mm, VIRTUAL_MEMORY_MANAGER &vm,
                          LZ_MATCH_HEAP &lz_matches, unsigned maximum_save);
