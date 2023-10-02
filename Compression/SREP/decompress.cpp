// Copyright (C) 2009-2014 Bulat Ziganshin. All rights reserved.
// Mail Bulat.Ziganshin@gmail.com if you have any questions or want to buy a
// commercial license for the source code.

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Single block decompressor for IO-LZ
// **************************************************************************************************************
// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "decompress.h"

// Decompress data using stat[] and in[] and return original data in outbuf[].
// Returns TRUE on successful decompression
bool decompress(bool ROUND_MATCHES, unsigned L, Readable* fout, Offset block_start,
                STAT *stat, char *in, char *inend, char *outbuf, char *outend) {
  STAT *statend = (STAT *)in;
  char *out = outbuf;

  while (statend - stat >= STATS_PER_MATCH(ROUND_MATCHES)) {
    // Like in original LZ77, LZ matches and literals are strictly interleaved
    DECODE_LZ_MATCH(stat, false, ROUND_MATCHES, L, block_start + (out - outbuf),
                    lit_len, LZ_MATCH, lz_match);
    if (lit_len > inend - in || lit_len + lz_match.len > outend - out ||
        lz_match.src >= lz_match.dest)
      return false; // Bad compressed data: in/out buffer overflow or src>=dest

    // First, copy literal data
    memcpy(out, in, lit_len);
    in += lit_len;
    out += lit_len;

    // Second, copy LZ match data from previous blocks
    if (lz_match.src < block_start) {
      unsigned bytes = mymin(lz_match.len, block_start - lz_match.src);
      fout->seek(lz_match.src,SEEK_SET);
      fout->read(out, bytes);
      out += bytes;
      lz_match.src += bytes;
      lz_match.len -= bytes;
    }

    // Third, copy LZ match data from the current block
    memcpy_lz_match(out, outbuf + (lz_match.src - block_start), lz_match.len);
    out += lz_match.len;
  }

  // Copy literal data up to the block end
  if (inend - in != outend - out)
    return false; // Bad compressed data
  memcpy(out, in, inend - in);
  return true;
}

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory manager
// ***********************************************************************************************************************************
// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MEMORY_MANAGER::INDEX
MEMORY_MANAGER::save(char *ptr, int len) { // Save contents of memory area
  MEMORY_MANAGER::INDEX index = INVALID_INDEX, first_index = INVALID_INDEX,
                        prev_index = INVALID_INDEX;
  while (len > 0) {
    index = allocate();
    if (prev_index != INVALID_INDEX)
      set_next_index(prev_index, index);
    else
      first_index = index;
    prev_index = index;
    int bytes = mymin(len, USEFUL_CHUNK_SPACE);
    memcpy(data_ptr(index), ptr, bytes);
    ptr += bytes;
    len -= bytes;
  }
  set_next_index(index, INVALID_INDEX);
  return first_index;
}
void MEMORY_MANAGER::restore(MEMORY_MANAGER::INDEX index, char *ptr,
                             int len) { // Restore saved contents
  while (len > 0) {
    int bytes = mymin(len, USEFUL_CHUNK_SPACE);
    memcpy(ptr, data_ptr(index), bytes);
    ptr += bytes;
    len -= bytes;
    index = next_index(index);
  }
}
void MEMORY_MANAGER::free(MEMORY_MANAGER::INDEX index) { // Free saved contents
  while (index != INVALID_INDEX) {
    MEMORY_MANAGER::INDEX next = next_index(index);
    mark_as_free(index);
    index = next;
  }
}

MEMORY_MANAGER::INDEX
MEMORY_MANAGER::allocate() { // Allocate one chunk and return its index
  if (first_free == INVALID_INDEX)
    allocate_block();
  MEMORY_MANAGER::INDEX free_chunk = first_free;
  first_free = next_index(first_free);
  used_chunks++;
  return free_chunk;
}
void MEMORY_MANAGER::mark_as_free(INDEX index) { // Mark chunk as free
  set_next_index(index, first_free);
  first_free = index;
  used_chunks--;
}
MEMORY_MANAGER::INDEX MEMORY_MANAGER::next_index(
    MEMORY_MANAGER::INDEX
        index) { // Returns contents of "next chunk" field for given chunk
  return *(MEMORY_MANAGER::INDEX *)chunk_ptr(index);
}
void MEMORY_MANAGER::set_next_index(
    MEMORY_MANAGER::INDEX index,
    MEMORY_MANAGER::INDEX next_index) { // Sets "next chunk" field of given
                                        // chunk to the value of next_index
  *(INDEX *)chunk_ptr(index) = next_index;
}
char *MEMORY_MANAGER::data_ptr(INDEX index) { // Data part of given chunk
  return chunk_ptr(index) + sizeof(INDEX);
}

char *MEMORY_MANAGER::chunk_ptr(
    MEMORY_MANAGER::INDEX index) { // Address of chunk with given index
  return block_addr[index >> lbK] + (index & K1) * CHUNK_SIZE;
}
void MEMORY_MANAGER::allocate_block() { // Allocate one more block of memory and
                                        // add its
                                        // chunks to the chain of free chunks
  char *p = new char[aBLOCK_SIZE];
  block_addr.push_back(p);
  int block = block_addr.size() - 1;
  for (MEMORY_MANAGER::INDEX i = block * K; i < (block + 1) * K - 1; i++)
    set_next_index(i, i + 1);
  set_next_index((block + 1) * K - 1, first_free);
  first_free = block * K;
  if (first_free == INVALID_INDEX)
    first_free++;
}

MEMORY_MANAGER::MEMORY_MANAGER(size_t memlimit) {
  first_free = INVALID_INDEX;
  used_chunks = 0;
  useful_memory = (memlimit / aBLOCK_SIZE * aBLOCK_SIZE / CHUNK_SIZE - 1) *
                  USEFUL_CHUNK_SPACE;
  CHECK(FREEARC_ERRCODE_INTERNAL, K == (1 << lbK),
        (s, "INTERNAL ERROR: K!=(1<<lbK)"));
}
Offset MEMORY_MANAGER::current_mem() { return used_chunks * CHUNK_SIZE; }
Offset MEMORY_MANAGER::max_mem() { return block_addr.size() * aBLOCK_SIZE; }
Offset MEMORY_MANAGER::available_space() {
  return useful_memory > used_chunks * USEFUL_CHUNK_SPACE
             ? useful_memory - used_chunks * USEFUL_CHUNK_SPACE
             : 0;
}

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Future-LZ match handling
// *************************************************************************************************************************
// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Statistics! Statistics! Statistics!
Offset total_matches = 0, cur_matches = 0, max_matches = 0, total_bytes = 0,
       cur_bytes = 0, max_bytes = 0, total_reads = 0;
void PLUS_MATCH(unsigned bytes) {
  if (bytes > 0)
    bytes += 16;
  total_matches++;
  cur_matches++;
  max_matches = mymax(cur_matches, max_matches);
  total_bytes += bytes;
  cur_bytes += bytes;
  max_bytes = mymax(cur_bytes, max_bytes);
}
void MINUS_MATCH(unsigned bytes) {
  if (bytes > 0)
    bytes += 16;
  cur_matches--;
  cur_bytes -= bytes;
}
void PLUS_READ() { total_reads++; }

FUTURE_LZ_MATCH::FUTURE_LZ_MATCH() : index(MEMORY_MANAGER::INVALID_INDEX) {}
void FUTURE_LZ_MATCH::save_match_data(MEMORY_MANAGER &mm, char *ptr) {
  index = mm.save(ptr, len);
  PLUS_MATCH(len);
}

void FUTURE_LZ_MATCH::restore_match_data(MEMORY_MANAGER &mm, char *ptr) const {
  mm.restore(index, ptr, len);
}

void FUTURE_LZ_MATCH::restore_match_data(MEMORY_MANAGER &mm, char *ptr,
                                         char *buf, Offset buf_start) const {
  if (index != MEMORY_MANAGER::INVALID_INDEX)
    mm.restore(index, ptr, len);
  else
    memcpy_lz_match(ptr, buf + (src - buf_start), len);
}

void FUTURE_LZ_MATCH::free(MEMORY_MANAGER &mm) const {
  mm.free(index);
  MINUS_MATCH(index != MEMORY_MANAGER::INVALID_INDEX ? len : 0);
}
void FUTURE_LZ_MATCH::set_marking_point() { len = 0; }
bool FUTURE_LZ_MATCH::is_marking_point() const { return len == 0; }

// Compare LZ matches by destination position (for LZ_MATCH_HEAP)
bool operator<(const FUTURE_LZ_MATCH &left, const FUTURE_LZ_MATCH &right) {
  return (left.dest < right.dest);
}

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Virtual memory manager
// ***************************************************************************************************************************
// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
VIRTUAL_MEMORY_MANAGER::VIRTUAL_MEMORY_MANAGER(char *_vmfile_name,
                                               Offset _VMBLOCK_SIZE)
    : vmfile_name(_vmfile_name), vmfile(NULL), vmbuf(NULL),
      VMBLOCK_SIZE(_VMBLOCK_SIZE), new_block(0), total_read(0), total_write(0) {
}
VIRTUAL_MEMORY_MANAGER::~VIRTUAL_MEMORY_MANAGER() {
  delete vmbuf;
  if (vmfile) {
    fclose(vmfile);
    remove(vmfile_name);
  }
}
Offset VIRTUAL_MEMORY_MANAGER::current_mem() {
  return max_mem() - free_blocks.size() * VMBLOCK_SIZE;
}
Offset VIRTUAL_MEMORY_MANAGER::max_mem() { return new_block * VMBLOCK_SIZE; }

void VIRTUAL_MEMORY_MANAGER::save_to_disk(MEMORY_MANAGER &mm,
                                          LZ_MATCH_HEAP &lz_matches) {
  if (!vmbuf)
    vmbuf = new char[VMBLOCK_SIZE];
  if (!vmfile)
    vmfile = fopen(vmfile_name, "w+b");

  // Encode matches to the block, while it has enough space
  Offset min_dest = Offset(-1);
  LZ_MATCH_REVERSE_ITERATOR lz = lz_matches.rbegin();
  char *p = vmbuf;
  for (;;) {
    lz++;
    if (lz == lz_matches.rend())
      break; // There are no more matches in the heap :D
    if (lz->index == MEMORY_MANAGER::INVALID_INDEX)
      continue; // Match doesn't contain match data - skip it
    if (vmbuf + VMBLOCK_SIZE - p < 24 + lz->len)
      break; // Not enough space in the block to save this match - go writing
             // block to the disk

    *(STAT *)p = lz->len;
    *(Offset *)(p + 4) = lz->src;
    *(Offset *)(p + 12) = min_dest = lz->dest;
    lz->restore_match_data(mm, p + 20);
    p += 20 + lz->len;

    lz->free(mm);
    lz_matches.erase(*lz);
  }
  *(STAT *)p = 0; // End-of-block mark

  // Save block to disk, and add to the heap pseudo-match marking the restore
  // point
  unsigned block;
  if (free_blocks.empty())
    block = new_block++;
  else
    block = free_blocks.top(),
    free_blocks.pop(); // First free block in the file
  file_seek(vmfile, block * VMBLOCK_SIZE);
  file_write(vmfile, vmbuf, VMBLOCK_SIZE);
  total_write += VMBLOCK_SIZE;
  FUTURE_LZ_MATCH mark;
  mark.src = block;
  mark.dest = min_dest;
  mark.set_marking_point();
  lz_matches.insert(mark);
}
void VIRTUAL_MEMORY_MANAGER::restore_from_disk(MEMORY_MANAGER &mm,
                                               LZ_MATCH_HEAP &lz_matches,
                                               LZ_MATCH_ITERATOR &mark) {
  // Free up enough memory to ensure that there are space to restore the block
  while (mm.available_space() < VMBLOCK_SIZE)
    save_to_disk(mm, lz_matches);

  // Read block from disk
  unsigned block = mark->src;
  file_seek(vmfile, block * VMBLOCK_SIZE);
  file_read(vmfile, vmbuf, VMBLOCK_SIZE);
  total_read += VMBLOCK_SIZE;
  free_blocks.push(block);

  // Restore matches encoded in the block
  for (char *p = vmbuf; *(STAT *)p != 0;) // Until end-of-block mark
  {
    FUTURE_LZ_MATCH lz;
    lz.len = *(STAT *)p;
    lz.src = *(Offset *)(p + 4);
    lz.dest = *(Offset *)(p + 12);
    lz.save_match_data(mm, p + 20);
    lz_matches.insert(lz);
    p += 20 + lz.len;
  }
}

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Single block Future-LZ decompressor
// **************************************************************************************************************
// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Decompress data using stat[] and in[] and return original data in outbuf[].
// Returns TRUE on successful decompression
bool decompress_FUTURE_LZ(bool ROUND_MATCHES, unsigned L, Readable *fout,
                          Offset block_start, STAT *statbuf, STAT *statend,
                          char *in, char *inend, char *outbuf, char *outend,
                          MEMORY_MANAGER &mm, VIRTUAL_MEMORY_MANAGER &vm,
                          LZ_MATCH_HEAP &lz_matches, unsigned maximum_save) {
  Offset block_end =
      block_start + (outend - outbuf); // Absolute file position corresponding
                                       // to end of the current block
  char *out = outbuf;

  // 1. Insert into lz_matches matches with LZ.dest in the current block
  Offset block_pos = block_start;
  for (STAT *stat = statbuf;
       statend - stat >= STATS_PER_MATCH(ROUND_MATCHES);) {
    DECODE_LZ_MATCH(stat, true, ROUND_MATCHES, L, block_pos, lit_len,
                    FUTURE_LZ_MATCH, lz_match);
    if (lz_match.src < block_pos || lz_match.src >= block_end ||
        lz_match.len > block_end - lz_match.src ||
        lz_match.dest <= lz_match.src)
      return false; // Bad compressed data
    if (lz_match.dest < block_end)
      lz_matches.insert(lz_match), PLUS_MATCH(0);
    block_pos = lz_match.src;
  }

  // 2. LZ decompression loop, processing all LZ matches with LZ.dest in the
  // current block
  for (LZ_MATCH_ITERATOR lz_match = lz_matches.begin();
       lz_match->dest < block_end; lz_match = lz_matches.begin()) {
    if (lz_match->is_marking_point()) {
      vm.restore_from_disk(mm, lz_matches, lz_match);
    } else {
      // Copy literal data up to match start
      int lit_len = (lz_match->dest - block_start) - (out - outbuf);
      if ((lz_match->dest < block_start + (out - outbuf)) ||
          (in + lit_len > inend) || (out + lit_len + lz_match->len > outend))
        return false; // Bad compressed data
      memcpy(out, in, lit_len);
      in += lit_len;
      out += lit_len;

      // Copy match data
      if (lz_match->len >= maximum_save && lz_match->src < block_start) {
        fout->seek(lz_match->src,SEEK_SET);
        fout->read(out, lz_match->len);
        PLUS_READ();
      } else {
        lz_match->restore_match_data(mm, out, outbuf, block_start);
      }
      out += lz_match->len;
      lz_match->free(mm);
    }
    lz_matches.erase(lz_match);
  }
  // Copy literal data up to the block end
  if (inend - in != outend - out)
    return false; // Bad compressed data
  memcpy(out, in, inend - in);

  // 3. Insert into lz_matches matches with LZ.dest in future blocks
  block_pos = block_start;
  for (STAT *stat = statbuf;
       statend - stat >= STATS_PER_MATCH(ROUND_MATCHES);) {
    DECODE_LZ_MATCH(stat, true, ROUND_MATCHES, L, block_pos, lit_len,
                    FUTURE_LZ_MATCH, lz_match);
    if (lz_match.dest >= block_end) {
      if (lz_match.len >= maximum_save)
        PLUS_MATCH(0);
      else {
        while (lz_match.len > mm.available_space())
          vm.save_to_disk(mm, lz_matches);
        lz_match.save_match_data(
            mm, outbuf + (lz_match.src -
                          block_start)); // copy match data into
                                         // dynamically-allocated buffer
      }
      lz_matches.insert(lz_match);
    }
    block_pos = lz_match.src;
  }

  return true;
}
