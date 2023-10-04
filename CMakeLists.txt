cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(srep)
set(CMAKE_CXX_STANDARD, 14)
set(CMAKE_C_STANDARD, 14)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-fverbose-asm  -mtune=generic -funroll-all-loops -msse2 -Wno-write-strings -Wno-unused-result")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")


add_definitions(-DFREEARC_UNIX -DFREEARC_INTEL_BYTE_ORDER -D_FILE_OFFSET_BITS=64 )
add_definitions(-D_NO_EXCEPTIONS)


include_directories(Compression Compression/_Encryption Compression/_Encryption/headers Compression/_Encryption/hashes )

add_library(decomp SHARED Compression/Common.cpp Compression/SREP/decompress.cpp Compression/SREP/srep_dec.cpp Compression/SREP/srep_util.cpp Compression/SREP/util.cpp Compression/SREP/hashes.cpp  Compression/SREP/error.cpp )

add_executable(srep
	#Compression/SREP/hashes.cpp #Compression/SREP/hash_table.cpp #Compression/SREP/compress.cpp Compression/SREP/compress_inmem.cpp  Compression/SREP/compress_cdc.cpp Compression/SREP/io.cpp Compression/MultiThreading.cpp 
	Compression/Common.cpp Compression/SREP/srep.cpp Compression/SREP/srep_util.cpp Compression/SREP/srep_dec.cpp Compression/SREP/util.cpp Compression/SREP/error.cpp
	# Compression/LZMA2/C/ThreadsUnix.c Compression/LZMA2/C/Threads.c Compression/_Encryption/ciphers/aes/aes.c Compression/_Encryption/ciphers/aes/aes_tab.c Compression/_Encryption/hashes/siphash/siphash.c Compression/_Encryption/hashes/sha1.c Compression/_Encryption/hashes/vmac/vmac.c Compression/_Encryption/hashes/sha2/sha512.c Compression/_Encryption/hashes/md5.c Compression/_Encryption/crypt/crypt_argchk.c Compression/_Encryption/prngs/fortuna.c Compression/_Encryption/misc/zeromem.c 
)
target_link_libraries(srep decomp)
