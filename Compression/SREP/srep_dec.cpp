#include "srep.h"
#include "util.hpp"


inline int decompress_main(
    bool &finished, int &errcode, Readable &fin, Writable &fout,
    MEMORY_MANAGER &mm, STAT header[MAX_HEADER_SIZE + MAX_HASH_SIZE],
    LZ_MATCH_HEAP &lz_matches, VIRTUAL_MEMORY_MANAGER &vm, Writable &ftemp,
    const Offset filesize, Offset &compsize, const char *tempfile,
    const unsigned maximum_save, const unsigned hash_size, void *hash_obj,
    const struct hash_descriptor *selected_hash, const bool ROUND_MATCHES,
    Offset &ram, const bool IO_LZ, const bool INDEX_LZ, Offset &max_ram,
    const bool is_info_mode, STAT *statptr, const unsigned BASE_LEN,
    const char *index_file, char *out, STAT *statsize_end, char *buf,
    STAT *statsize_ptr, Readable &fstat, const unsigned compbufsize,
    Offset &origsize, Offset &statsize, const unsigned bufsize,
    const int header_size) {
  {
    // Read block header
    int len = fin.read(header, header_size);
    // If there is no more data or EOF header (two zero 32-bit words) detected
    if ((len == 0 ||
         len >= 2 * sizeof(STAT) && header[0] == 0 && header[1] == 0) &&
        lz_matches.size() == 1) {
      finished = true;
      return 0;
    }
    if (len != header_size)
      error(ERROR_COMPRESSION,
            "Decompression problem: unexpected end of file or I/O error");

    {
      unsigned datasize1 = header[0], origsize1 = header[1],
               statsize1 = header[2], compsize1 = datasize1 + statsize1;
      if (origsize1 > bufsize)
        error(ERROR_COMPRESSION,
              "Decompression problem: uncompressed block size is %u bytes, "
              "while maximum supported size is %u bytes",
              origsize1, bufsize);
      if (compsize1 > compbufsize || header[0] > compbufsize ||
          header[2] > compbufsize)
        error(ERROR_COMPRESSION,
              "Decompression problem: compressed block size is %u bytes, "
              "while maximum supported size is %u bytes",
              compsize1, compbufsize);

      // Update statistics
      Offset block_start = origsize; // first byte in the block
      statsize += statsize1;
      compsize += header_size + compsize1;
      origsize += origsize1;
      Offset block_end = origsize; // last byte in the block

      // Read compressed data, part I: the match list
      len = fstat.read(buf, statsize1);
      if (len != statsize1)
        error(ERROR_COMPRESSION,
              "Decompression problem: unexpected end of file or I/O error");

      STAT *statendptr =
          (STAT *)(buf + statsize1); // Should point AFTER the last match
                                     // belonging to the block
      if (INDEX_LZ) {
        statendptr = statptr + (*statsize_ptr++) / sizeof(STAT);
        finished = (statsize_ptr == statsize_end);
      }

      if (is_info_mode) {
        // Skip literal data since we only need to compute uncompressed file
        // size and decompression RAM
        fin.seek(datasize1, SEEK_CUR);
        if (IO_LZ)
          return 0;

        // Calculate how much RAM will be required for decompression.  Part I:
        // remove matches with destination in the current block
        for (LZ_MATCH_ITERATOR lz_match = lz_matches.begin();
             lz_match->dest < block_end; lz_match = lz_matches.begin()) {
          ram -= MEMORY_MANAGER::needmem(lz_match->len);
          lz_matches.erase(lz_match);
        }

        // Calculate decompression RAM.  Part II: add matches with source in
        // the current block
        Offset block_pos =
            block_start; // current position in the decompressed file
        for (STAT *stat = statptr; stat < statendptr;) {
          DECODE_LZ_MATCH(stat, true, ROUND_MATCHES, BASE_LEN, block_pos,
                          lit_len, FUTURE_LZ_MATCH, lz_match);
          block_pos = lz_match.src;
          if (lz_match.dest >= block_end && lz_match.len < maximum_save) {
            ram += MEMORY_MANAGER::needmem(lz_match.len);
            lz_matches.insert(lz_match);
          }
        }
        max_ram = mymax(ram, max_ram);

        return 0;
      }

      // Read compressed data, part II: literals
      len = fin.read(buf + statsize1, datasize1);
      if (len != datasize1)
        error(ERROR_COMPRESSION,
              "Decompression problem: unexpected end of file or I/O error");

      // Perform decompression
      bool ok = IO_LZ
                    ? decompress(ROUND_MATCHES, BASE_LEN, &ftemp,
                                 block_start, statptr, buf + statsize1,
                                 buf + compsize1, out, out + origsize1)
                    : decompress_FUTURE_LZ(ROUND_MATCHES, BASE_LEN, &ftemp,
                                           block_start, statptr, statendptr,
                                           buf + statsize1, buf + compsize1,
                                           out, out + origsize1, mm, vm,
                                           lz_matches, maximum_save);
      if (!ok)
        error(ERROR_COMPRESSION,
              "Decompression problem: broken compressed data");

      if (INDEX_LZ)
        statptr = statendptr;

      // Check hashsum of decompressed data
      if (selected_hash->hash_func) {
        char checksum[MAX_HASH_SIZE];
        selected_hash->hash_func(hash_obj, out, origsize1, checksum);
        if (memcmp(checksum, header + 3, hash_size) != EQUAL)
          error(ERROR_COMPRESSION,
                "Decompression problem: checksum of decompressed data is not "
                "the same as checksum of original data");
      }

      // Write decompressed data to output file, plus to temporary file if
      // it's different
      ftemp.seek(block_start, SEEK_SET);
      checked_filepp_write(ftemp, out, origsize1);
      if (tempfile && *tempfile)
        checked_filepp_write(fout, out, origsize1);
    }
  }
  return 0;
cleanup:
  return errcode;
}

const size_t decompress_or_info_intrnl(Readable &fin, Writable &fout, const DecompressCallback callback,
                        void *opaque, const struct hash_descriptor *selected_hash,
                        const int64 vm_mem, const Offset filesize,
                        const unsigned maximum_save, const bool is_out_seekable,
                        const bool is_info_mode, const unsigned bufsize,
                        const unsigned vm_block, int& errcode) {
  STAT header[MAX_HEADER_SIZE + MAX_HASH_SIZE];
  zeroArray(header);
  char temp1[100];
  int warnings = 0, verbosity = 2, io_accelerator = 1;
  Offset origsize = 0, compsize = 0, ram = 0, max_ram = 0;
  bool print_pc = false;
  char *index_file = "", *tempfile = NULL, *DEFAULT_TEMPFILE = "srep-data.tmp",
       *vmfile_name = "srep-virtual-memory.tmp", *option_s = "+";
  const char *newline = strequ(option_s, "") ? "\n" : "    \b\b\b\b";

  File ftemp_(NULL);
  Writable *ftemp = &ftemp_;
  double GlobalTime0 = GetGlobalTime();
  double LastGlobalTime = 0;
  Offset last_origsize = Offset(-1);
  // Miscellaneous
  void *hash_obj = NULL;
  double TimeInterval = strequ(option_s, "")    ? 1e-30
                        : strequ(option_s, "-") ? 1e30
                        : strequ(option_s, "+") ? 0.2
                                                : atof(option_s);

  unsigned compbufsize =
      bufsize + sizeof(STAT) * (MAX_HEADER_SIZE + MAX_HASH_SIZE +
                                FUTURELZ_MAX_STATS_PER_BLOCK(bufsize));
  char *buf = new char[compbufsize];
  char *out = new char[bufsize];
  STAT *statbuf = (STAT *)buf, *statptr = (STAT *)buf;
  STAT *statsize_buf = NULL, *statsize_ptr = NULL, *statsize_end = NULL;

  Offset statsize = 0;
  double OperationStartGlobalTime;

  File fstat_(NULL);
  Readable &fstat = (*index_file) ? fstat_ : fin;
  if (*index_file) {
    fstat_ = File(index_file, "rb");
  }

  int io_mem = vm_block + bufsize + compbufsize + 8 * mb;
  MEMORY_MANAGER mm(vm_mem >= io_mem + vm_block * 4 ? vm_mem - io_mem
                                                    : vm_block * 4);
  VIRTUAL_MEMORY_MANAGER vm(vmfile_name, vm_block);
  LZ_MATCH_HEAP lz_matches;
  FUTURE_LZ_MATCH barrier;
  barrier.dest = Offset(-1);
  lz_matches.insert(barrier);

  // Check header of compressed file
  int len = fin.read(header, sizeof(STAT) * ARCHIVE_HEADER_SIZE);
  if (len != sizeof(STAT) * ARCHIVE_HEADER_SIZE ||
      header[0] != BULAT_ZIGANSHIN_SIGNATURE || header[1] != SREP_SIGNATURE)
    error(ERROR_COMPRESSION, "Not an SREP compressed file: ");
  int format_version = header[2] & 255;
  if (format_version < SREP_FORMAT_VERSION1 ||
      format_version > SREP_FORMAT_VERSION4)
    error(ERROR_COMPRESSION,
          "Incompatible compressed data format: v%d (%s supports only "
          "v%d..v%d) in file",
          format_version, program_version, SREP_FORMAT_VERSION1,
          SREP_FORMAT_VERSION4);

  // Get compression params from the header
  unsigned BASE_LEN = header[3];
  int hash_num = (header[2] >> 8) & 255;
  int hash_seed_size = (header[2] >> 16) & 255;
  int hash_size = ((header[2] >> 24) + 16) & 255;
  if (selected_hash->hash_func !=
      NULL) // unless hash checking was disabled by -hash- option
  {
    selected_hash = hash_by_num(hash_num);
    if (selected_hash == NULL) {
      fprintf(stderr,
              "Block checksums can't be checked since they are using unknown "
              "hash #%d-%d\n",
              hash_num, hash_size * CHAR_BIT);
      selected_hash = hash_by_name("", errcode);
    } else if (selected_hash->hash_func == NULL) {
      fprintf(stderr, "Block checksums can't be checked since they aren't "
                      "saved in the compressed data\n");
    } else if (hash_seed_size > selected_hash->hash_seed_size ||
               hash_size > selected_hash->hash_size) {
      char temp[100];
      fprintf(stderr,
              "Block checksums can't be checked since they are using "
              "unsupported hashsize %s-%s%d",
              selected_hash->hash_name,
              hash_seed_size ? show3(hash_seed_size * CHAR_BIT, temp, "-") : "",
              hash_size * CHAR_BIT);
      selected_hash = hash_by_name("", errcode);
    }
  }

  // For keyed hashes like VMAC, we should read the key (seed) and create a hash
  // using this key
  len = fin.read(header, hash_seed_size);
  if (len != hash_seed_size)
    error(ERROR_COMPRESSION,
          "Decompression problem: unexpected end of file  or I/O error");
  if (selected_hash->new_hash) {
    hash_obj = selected_hash->new_hash(header, hash_seed_size);
  }
  unsigned full_archive_header_size =
      sizeof(STAT) * ARCHIVE_HEADER_SIZE + hash_seed_size;
  compsize = full_archive_header_size;

  const int header_size = sizeof(STAT) * BLOCK_HEADER_SIZE +
                          hash_size; // compressed block header size
  const bool ROUND_MATCHES = (format_version == SREP_FORMAT_VERSION1);
  const bool IO_LZ = (format_version <= SREP_FORMAT_VERSION2);
  const bool FUTURE_LZ = (format_version == SREP_FORMAT_VERSION3);
  const bool INDEX_LZ = (format_version == SREP_FORMAT_VERSION4);
  sprintf(temp1, (BASE_LEN ? " -l%d" : ""), BASE_LEN);
  if (is_info_mode)
    fprintf(stderr, "%s:%s -hash=%s%s",
            FUTURE_LZ  ? "Future-LZ"
            : INDEX_LZ ? "Index-LZ"
                       : "I/O LZ",
            temp1, selected_hash->hash_name, INDEX_LZ ? "" : "\n");

  if (INDEX_LZ) {
    fin.seek(filesize - INDEX_LZ_FOOTER_SIZE, SEEK_SET);
    checked_filepp_read(fin, header, INDEX_LZ_FOOTER_SIZE);

    unsigned footer_version = (header[3] & 255);
    unsigned footer_size = header[2];
    Offset stat_size = header[0] + (Offset(header[1]) << 32),
           stats_count = stat_size / sizeof(STAT),
           lz_matches_count = stats_count / STATS_PER_MATCH(ROUND_MATCHES);
    compsize += footer_size + stat_size;

    if (header[5] != ~BULAT_ZIGANSHIN_SIGNATURE || header[4] != ~SREP_SIGNATURE)
      error(ERROR_COMPRESSION,
            "Not found SREP compressed file footer in file ");
    if (footer_version != SREP_FOOTER_VERSION1)
      error(ERROR_COMPRESSION,
            "Incompatible compressed file footer format: v%d (%s supports "
            "only v%d) in file ",
            footer_version, program_version, SREP_FOOTER_VERSION1);
    if (compsize > filesize)
      error(ERROR_COMPRESSION,
            "Broken SREP compressed file footer: %0.lf bytes footer + %0.lf "
            "bytes index in file ",
            double(footer_size), double(stat_size));

    // Read match list
    statbuf = statptr = new STAT[stats_count];
    fin.seek(filesize - footer_size - stat_size, SEEK_SET);
    checked_filepp_read(fin, statbuf, stat_size);

    // Read block list (count of matches for every block)
    unsigned total_blocks = (footer_size - INDEX_LZ_FOOTER_SIZE) / sizeof(STAT);
    statsize_buf = statsize_ptr = new STAT[total_blocks];
    statsize_end = statsize_buf + total_blocks;
    checked_filepp_read(fin, statsize_buf, total_blocks * sizeof(STAT));

    fin.seek(full_archive_header_size, SEEK_SET);

    if (is_info_mode) {
      // Original file size = literal bytes + match bytes
      origsize = filesize - footer_size - stat_size - full_archive_header_size -
                 total_blocks * header_size; // compute literal bytes

      // Read first block header in order to determine block size
      int len = fin.read(header, header_size);
      if (len != header_size)
        error(ERROR_COMPRESSION,
              "Decompression problem: unexpected end of file  or I/O error");
      unsigned block_size = header[1];

      // Calculate how much RAM will be required for decompression
      STAT *stat = statbuf;
      for (int i = 0; i < total_blocks; ++i) {
        Offset block_end = Offset(i + 1) * block_size;
        for (LZ_MATCH_ITERATOR lz_match = lz_matches.begin();
             lz_match->dest < block_end; lz_match = lz_matches.begin()) {
          ram -= MEMORY_MANAGER::needmem(lz_match->len);
          lz_matches.erase(lz_match);
        }

        Offset block_pos =
            Offset(i) * block_size; // current position in the decompressed file
        for (STAT *statend = stat + statsize_buf[i] / sizeof(STAT);
             stat < statend;) {
          DECODE_LZ_MATCH(stat, true, ROUND_MATCHES, BASE_LEN, block_pos,
                          lit_len, FUTURE_LZ_MATCH, lz_match);
          origsize += lz_match.len; // add match bytes to the origsize
          block_pos = lz_match.src;
          if (lz_match.dest >= block_end && lz_match.len < maximum_save) {
            ram += MEMORY_MANAGER::needmem(lz_match.len);
            lz_matches.insert(lz_match);
          }
        }
        max_ram = mymax(ram, max_ram);
      }

      char temp1[100], temp2[100], temp3[100];
      fprintf(stderr, ".  %s -> %s: %.2lf%%\n", show3(origsize, temp1),
              show3(filesize, temp2), double(filesize) * 100 / origsize);
      print_info("", max_ram, maximum_save, stat_size, ROUND_MATCHES, filesize);

      return -1;
    }
  }

  // If we will need to reread data from the stdout, it will be wise to
  // duplicate them to tempfile
  if ((IO_LZ || maximum_save != unsigned(-1)) && !is_out_seekable) {
    if (!tempfile)
      tempfile = DEFAULT_TEMPFILE;
    else if (*tempfile == 0)
      error(ERROR_IO, "Writing decompressed data to stdout without tempfile "
                      "isn't supported for this file and settings");
  }

  if (tempfile && *tempfile) {
    ftemp_ = File(tempfile, "w+b");
    if (ftemp_.get() == NULL)
      error(ERROR_IO, "Can't open tempfile %s for write", tempfile);
  } else {
    ftemp = &fout;
  printf("Cur\n");
  }

  OperationStartGlobalTime = (LastGlobalTime = GetGlobalTime() - GlobalTime0);

  // Decompress data by blocks until EOF
  for (bool finished = false; !finished;) {
    if (decompress_main(finished, errcode, fin, fout, mm, header, lz_matches,
                        vm, *ftemp, filesize, compsize, tempfile, maximum_save,
                        hash_size, hash_obj, selected_hash, ROUND_MATCHES, ram,
                        IO_LZ, INDEX_LZ, max_ram, is_info_mode, statptr,
                        BASE_LEN, index_file, out, statsize_end, buf,
                        statsize_ptr, fstat, compbufsize, origsize, statsize,
                        bufsize, header_size) < 0)
      return -1;
    if (callback)
      callback(out, opaque);
  } // for
  if (is_info_mode)
    print_info("\n", max_ram, maximum_save, statsize, ROUND_MATCHES, filesize);
  return origsize;
cleanup:
  return 0;
}



#ifdef __cplusplus
extern "C"{
#endif
EXPORT int decompress_or_info(FILE *fin, FILE *fout,
                       struct hash_descriptor *selected_hash,
                       const int64 vm_mem, const Offset filesize,
                       const unsigned maximum_save, const FILENAME finame,
                       const FILENAME foutname, const COMMAND_MODE cmdmode,
                       const unsigned bufsize, const unsigned vm_block) {
  if (selected_hash==NULL){
    int errcode = 0;
    selected_hash = hash_by_name(DEFAULT_HASH, errcode);
  }
  File fin_(fin);
  File fout_(fout);
  const bool is_out_seekable = strequ(foutname, "-") != 0;

  int errcode = 0;
  size_t res = decompress_or_info_intrnl(fin_, fout_, NULL, NULL, selected_hash, vm_mem,
                             filesize, maximum_save, is_out_seekable,
                             cmdmode == INFORMATION, bufsize, vm_block, errcode);
  if (res==0) return errcode;
  return res;
}


EXPORT int decompress_or_info_mem(void* bufin, const size_t szin, const char* fout,
                       struct hash_descriptor *selected_hash,
                       const int64 vm_mem, 
                       const unsigned maximum_save, const bool is_out_seekable, const bool is_info_mode,
                       const unsigned bufsize, const unsigned vm_block) {
  if (selected_hash==NULL){
    int errcode = 0;
    selected_hash = hash_by_name(DEFAULT_HASH, errcode);
  }
  Cursor fin_(bufin, szin);
  File fout_(fout, "wb");

  int errcode = 0;
  size_t res = decompress_or_info_intrnl(fin_, fout_, NULL, NULL, selected_hash, vm_mem,
                             szin, maximum_save, is_out_seekable,
                             is_info_mode, bufsize, vm_block,errcode);
  if (res==0) return errcode;
  return res;
}

EXPORT int decompress_or_info_mem2mem(void* bufin, const size_t szin, void* bufout, const size_t capout,
                       struct hash_descriptor *selected_hash,
                       const int64 vm_mem, 
                       const unsigned maximum_save, const bool is_out_seekable, const bool is_info_mode,
                       const unsigned bufsize, const unsigned vm_block) {
  if (selected_hash==NULL){
    int errcode = 0;
    selected_hash = hash_by_name(DEFAULT_HASH, errcode);
  }
  Cursor fin_(bufin, szin);
  Cursor fout_(bufout,0,capout);

  int errcode = 0;
  size_t res = decompress_or_info_intrnl(fin_, fout_, NULL, NULL, selected_hash, vm_mem,
                             szin, maximum_save, is_out_seekable,
                             is_info_mode, bufsize, vm_block,errcode);
  if (res==0) return errcode;
  return res;
}
const size_t decompress_or_info_intrnl_p(Readable *fin, Writable *fout, const DecompressCallback callback,
                        void *opaque, struct hash_descriptor *selected_hash,
                        const int64 vm_mem, const Offset filesize,
                        const unsigned maximum_save, const bool is_out_seekable,
                        const bool is_info_mode, const unsigned bufsize,
                        const unsigned vm_block, int* errcode){
  if (selected_hash==NULL){
    selected_hash = hash_by_name(DEFAULT_HASH, *errcode);
  }
  return decompress_or_info_intrnl(*fin, *fout,callback,opaque,selected_hash,vm_mem,filesize,maximum_save, is_out_seekable,is_info_mode, bufsize,vm_block,*errcode);
}

#ifdef __cplusplus
}//extern "C"
#endif
