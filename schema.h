#define __STDC_FORMAT_MACROS

#include <stdint.h>

#include <cstring>

#include "libcvmfs_cache.h"

const unsigned kMaxDescriptionLength = 1024;


struct Nonce {
  Nonce() { memset(bytes, 0, sizeof(bytes)); }
  void Generate();
  unsigned char bytes[16];
}  __attribute__((__packed__));


struct TxnKey {
  explicit TxnKey(const Nonce &n) : nonce(n) { }
  Nonce nonce;
} __attribute__((__packed__));

struct TxnData {
  explicit TxnData(uint64_t t) : last_updated(t) { }
  uint64_t last_updated;
} __attribute__((__packed__));


struct PartKey {
  PartKey(const Nonce &n, uint64_t p) : nonce(n), part_nr(p) { }

  Nonce nonce;
  uint64_t part_nr;
} __attribute__((__packed__));


struct ObjectKey {
  ObjectKey() { memset(&id, 0, sizeof(id)); }
  explicit ObjectKey(const struct cvmcache_hash &id) : id(id) { }
  bool operator <(const ObjectKey &other) const {
    return cvmcache_hash_cmp(const_cast<cvmcache_hash *>(&(this->id)),
                             const_cast<cvmcache_hash *>(&(other.id))) < 0;
  }

  struct cvmcache_hash id;
} __attribute__((__packed__));

struct ObjectData {
  ObjectData()
    : last_updated(0)
    , nonce()
    , refcnt(0)
    , size(0)
    , type(CVMCACHE_OBJECT_REGULAR)
  {
    description[0] = '\0';
  }

  void SetDescription(const char *s);

  uint64_t last_updated;
  Nonce nonce;
  uint64_t refcnt;
  uint64_t size;
  cvmcache_object_type type;
  char description[kMaxDescriptionLength];
} __attribute__((__packed__));
