#pragma once
#include <stdint.h>
#define MD5_SIZE    16
#define SHA1_SIZE   20
#define SHA512_SIZE 64
typedef unsigned char Digest[SHA1_SIZE];

#define LTC_NO_HASHES
#define   LTC_MD5
#define   LTC_SHA1
#define   LTC_SHA512
#define LTC_NO_CIPHERS
#define   LTC_RIJNDAEL
#define     ENCRYPT_ONLY

#define VMAC_TAG_LEN     128  /* Requesting VMAC-128 algorithm (instead of VMAC-64) */
#define VMAC_KEY_LEN     256  /* Must be 128, 192 or 256 (AES key size)        */
#define VMAC_NHBYTES     4096 /* Must 2^i for any 3 < i < 13. Standard = 128   */
#define VMAC_USE_LIB_TOM_CRYPT 1
#define VMAC_ALIGNMENT 16   /* SSE-compatible memory alignment */
#define VMAC_TAG_LEN_BYTES (VMAC_TAG_LEN/CHAR_BIT)
#define VMAC_KEY_LEN_BYTES (VMAC_KEY_LEN/CHAR_BIT)
#include "vmac/vmac.h"

// Возведение в степень
template <class T>
T power (T base, unsigned n);

struct VHash
{
  bool initialized;
  /*ALIGN(VMAC_ALIGNMENT)*/ vmac_ctx_t ctx;

  VHash() ;

  // Initialize ctx
  void init (void *seed = NULL);

  // Return hash value for the buffer
  void compute (const void *ptr, size_t size, void *result);
};
void* new_vhash (void *seed, int size);
void compute_vhash (void *hash, void *buf, int size, void *result);
// Using VHash instead of SHA-1 for digest
struct VDigest
{
  VHash vhash1, vhash2;
  void init() ;
  void compute (const void *ptr, size_t size, void *result);
};

// Function returning hash object initialized by the provided seed
typedef void* (*new_hash_t) (void *seed, int size);

// Hash function processing the (buf,size) and storing computed hash value to the result
typedef void (*hash_func_t) (void *hash, void *buf, int size, void *result);

// Description for various hash algorithms
struct hash_descriptor {
  char*        hash_name;           // name used in the -hash=... option
  unsigned     hash_num;            // numeric tag stored in the archive header
  unsigned     hash_seed_size;      // additional bytes stored in the archive header (seed value for randomized hashes)
  unsigned     hash_size;           // bytes stored in the each block (hash value)
  new_hash_t   new_hash;            // create hash object
  hash_func_t  hash_func;           // hash function
};
struct hash_descriptor *hash_by_name (const char *hash_name, int &errcode);
struct hash_descriptor *hash_by_num (int hash_num);

template <class ValueT>
struct FakeRollingHash;
template <class ValueT>
struct PolynomialHash;
typedef uint8_t BYTE;
template <class ValueT>
struct PolynomialRollingHash
{
  operator ValueT(){return value;}
  ValueT value, PRIME, PRIME2, PRIME3, PRIME4, PRIME5, PRIME6, PRIME7, PRIME8, PRIME_L, PRIME_L1, PRIME_L2, PRIME_L3;
  int L;

  PolynomialRollingHash (int _L, ValueT seed);

  PolynomialRollingHash (void *buf, int _L, ValueT seed);

  void update (BYTE sub, BYTE add);

  // Roll hash by N==power(2,x) bytes
  template <int N>
  void update (void *_ptr);

  void moveto (void *_buf);
};

template <class ValueT>
struct CrcRollingHash;

static const char *DEFAULT_HASH = "vmac",  *HASH_LIST = "vmac(default)/siphash/md5/sha1/sha512";
