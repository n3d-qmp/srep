#include "srep.h"

// Копирует данные из буфера в буфер, идя в порядке возрастания адресов
// (это важно, поскольку буфера могут пересекаться и в этом случае нужно
// размножить существующие данные)
void memcpy_lz_match(void *_dest, void *_src, unsigned len) {
  if (len) {
    char *dest = (char *)_dest, *src = (char *)_src;
    do {
      *dest++ = *src++;
    } while (--len);
  }
}

// Print decompression RAM and match stats
void print_info(const char *prefix_str, Offset max_ram, unsigned maximum_save,
                Offset stat_size, bool ROUND_MATCHES, Offset filesize) {
  char temp1[100], temp2[100], with_maximum_save_str[100],
      maximum_save_str[100];
  showMem(maximum_save, maximum_save_str, false);
  sprintf(with_maximum_save_str,
          (maximum_save != unsigned(-1) ? " with -m%s" : ""), maximum_save_str);
  fprintf(
      stderr,
      "%sDecompression memory%s is %d mb.  %s matches = %s bytes = %.2lf%% of "
      "file",
      prefix_str, with_maximum_save_str, int((max_ram + mb - 1) / mb),
      show3(stat_size / (sizeof(STAT) * STATS_PER_MATCH(ROUND_MATCHES)), temp1),
      show3(stat_size, temp2), double(stat_size) * 100 / filesize);
}
