#pragma once
#include "srep.h"

#define checked_file_read(f, buf, size)                                        \
  {                                                                            \
    if (file_read(f, (buf), (size)) != (size)) {                               \
      fprintf(stderr, "\n  ERROR! Can't read from input file");                \
      errcode = ERROR_IO;                                                      \
      goto cleanup;                                                            \
    }                                                                          \
  }

#define checked_filepp_read(f, buf, size)                                      \
  {                                                                            \
    if (f.read((buf), (size)) != (size)) {                                     \
      fprintf(stderr, "\n  ERROR! Can't read from input file");                \
      errcode = ERROR_IO;                                                      \
      goto cleanup;                                                            \
    }                                                                          \
  }

#define checked_file_write(f, buf, size)                                       \
  {                                                                            \
    if (file_write(f, (buf), (size)) != (size)) {                              \
      fprintf(stderr, "\n  ERROR! Can't write to output file (disk full?)");   \
      errcode = ERROR_IO;                                                      \
      goto cleanup;                                                            \
    }                                                                          \
  }

#define checked_filepp_write(f, buf, size)                                     \
  {                                                                            \
    if (f.write((buf), (size)) != (size)) {                                    \
      fprintf(stderr, "\n  ERROR! Can't write to output file (disk full?)");   \
      errcode = ERROR_IO;                                                      \
      goto cleanup;                                                            \
    }                                                                          \
  }
