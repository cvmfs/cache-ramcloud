#define __STDC_FORMAT_MACROS


#include <libcvmfs_cache.h>
#include <ClientException.h>
#include <RamCloud.h>

#include <alloca.h>
#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

using namespace std;

struct cvmcache_context *ctx;
RAMCloud::RamCloud *ramcloud;
uint64_t table_blocks;
uint64_t table_metadata;

uint64_t size_stored = 0;

struct TxnInfo {
  struct cvmcache_hash id;
  uint64_t size;
};

map<uint64_t, TxnInfo> transactions;

struct ObjectMetadata {
  uint64_t size;
  int64_t refcnt;
  uint64_t last_refresh;
} __attribute__((__packed__));

struct BlockKey {
  struct cvmcache_hash hash;
  uint64_t block_nr;
} __attribute__((__packed__));


// TODO(jblomer): remember open files and periodically refresh the lease
static int rc_chrefcnt(struct cvmcache_hash *id, int32_t change_by) {
  //printf("Refcnt %s (%d)... ", cvmcache_hash_print(id), change_by);
  RAMCloud::Buffer buffer;
  bool success = false;
  do {
    uint64_t version;
    try {
      ramcloud->read(table_metadata, id, sizeof(*id), &buffer,
                     NULL, &version);
    } catch (RAMCloud::ClientException &e) {
      //printf("not available\n");
      return STATUS_NOENTRY;
    }
    assert(buffer.size() == sizeof(ObjectMetadata));
    ObjectMetadata md;
    buffer.copy(0, sizeof(md), &md);
    md.last_refresh = time(NULL);
    md.refcnt += change_by;
    if (md.refcnt < 0)
      return STATUS_BADCOUNT;
    RAMCloud::RejectRules rules;
    memset(&rules, 0, sizeof(rules));
    rules.givenVersion = version;
    rules.versionNeGiven = 1;
    try {
      ramcloud->write(table_metadata, id, sizeof(*id),
                      &md, sizeof(md), &rules);
      success = true;
    } catch (RAMCloud::ClientException &e) { }
  } while (!success);
  char *hash = cvmcache_hash_print(id);
  if (change_by == 1)
    printf("opening file %s\n", hash);
  else
    printf("closing file %s\n", hash);
  free(hash);
  return STATUS_OK;
}


int rc_obj_info(struct cvmcache_hash *id,
                struct cvmcache_object_info *info)
{
  //printf("Info %s... ", cvmcache_hash_print(id));
  RAMCloud::Buffer buffer;
  try {
    ramcloud->read(table_metadata, id, sizeof(*id), &buffer);
    assert(buffer.size() == sizeof(ObjectMetadata));
    ObjectMetadata md;
    buffer.copy(0, sizeof(md), &md);
    info->size = md.size;
    info->type = OBJECT_REGULAR;
    info->description = NULL;
    //printf("ok, size: %" PRIu64 "\n", info->size);
    return STATUS_OK;
  } catch (RAMCloud::ClientException &e) {
    //printf("not available\n");
    return STATUS_NOENTRY;
  }
}


static int rc_pread(struct cvmcache_hash *id,
                    uint64_t offset,
                    uint32_t *size,
                    unsigned char *buffer)
{
  char *hash = cvmcache_hash_print(id);
  printf("Pread %s, offset %" PRIu64 ", size %u\n", hash, offset, *size);
  free(hash);
  BlockKey block_key;
  block_key.hash = *id;
  block_key.block_nr = offset / cvmcache_max_object_size(ctx);
  uint32_t nbytes = 0;
  while (nbytes < *size) {
    RAMCloud::Buffer rc_buffer;
    try {
      //printf("  reading %s, %" PRIu64 "\n",
      //   cvmcache_hash_print(&block_key.hash), block_key.block_nr);
      //printf("before read\n");
      ramcloud->read(table_blocks, &block_key, sizeof(block_key), &rc_buffer);
      //printf("after read\n");
      uint64_t in_block_offset = 0;
      if (nbytes == 0) {
        in_block_offset =
          offset - (block_key.block_nr * cvmcache_max_object_size(ctx));
      }
      uint32_t remaining = std::min((uint32_t)(rc_buffer.size() - in_block_offset),
                                    *size - nbytes);
      rc_buffer.copy(in_block_offset, remaining, buffer + nbytes);
      nbytes += remaining;
    } catch (RAMCloud::ClientException &e) {
      //printf("  ...pread failed with %s\n", e.what());
      break;
    }
    block_key.block_nr++;
  }
  *size = nbytes;
  //printf("  ... ok (%u)\n", nbytes);
  // TODO: out of bounds
  return STATUS_OK;
}


static int rc_start_txn(struct cvmcache_hash *id,
                        uint64_t txn_id,
                        struct cvmcache_object_info *info)
{
  //printf("Start transaction %" PRIu64 "\n", txn_id);
  TxnInfo txn;
  txn.id = *id;
  // TODO(jblomer): handle description string
  txn.size = 0;
  transactions[txn_id] = txn;
  return STATUS_OK;
}


int rc_write_txn(uint64_t txn_id,
                 unsigned char *buffer,
                 uint32_t size)
{
  //printf("Write transaction %" PRIu64 "\n", txn_id);
  TxnInfo txn = transactions[txn_id];
  BlockKey block_key;
  block_key.hash = txn.id;
  block_key.block_nr = txn.size / cvmcache_max_object_size(ctx);
  //printf("  writing %s, %" PRIu64 "\n",
  //       cvmcache_hash_print(&block_key.hash), block_key.block_nr);
  try {
    ramcloud->write(table_blocks, &block_key, sizeof(block_key),
                    buffer, size);
  } catch (RAMCloud::ClientException &e) {
    return STATUS_IOERR;
  }

  txn.size += size;
  transactions[txn_id] = txn;
  return STATUS_OK;
}


int rc_commit_txn(uint64_t txn_id) {
  //printf("Commit transaction %" PRIu64 "\n", txn_id);
  TxnInfo txn = transactions[txn_id];
  ObjectMetadata md;
  md.size = txn.size;
  md.refcnt = 1;
  md.last_refresh = time(NULL);
  bool success = false;
  do {
    RAMCloud::Buffer buffer;
    RAMCloud::RejectRules rules;
    memset(&rules, 0, sizeof(rules));
    uint64_t version;
    try {
      ramcloud->read(table_metadata, &txn.id, sizeof(txn.id), &buffer,
                     NULL, &version);
      assert(buffer.size() == sizeof(ObjectMetadata));
      buffer.copy(0, sizeof(md), &md);
      md.refcnt += 1;
      md.last_refresh = time(NULL);
      rules.givenVersion = version;
      rules.versionNeGiven = 1;
    } catch (RAMCloud::ClientException &e) {
      rules.exists = 1;
    }
    try {
      ramcloud->write(table_metadata, &txn.id, sizeof(txn.id),
                      &md, sizeof(md), &rules);
      success = true;
    } catch (RAMCloud::ClientException &e) { }
  } while (!success);
  transactions.erase(txn_id);
  size_stored += md.size;
  char *hash = cvmcache_hash_print(&txn.id);
  printf("committed %s, size %" PRIu64 ", total size %" PRIu64 "\n",
         hash, txn.size, size_stored);
  free(hash);
  return STATUS_OK;
}

int rc_abort_txn(uint64_t txn_id) {
  //printf("Abort transaction %" PRIu64 "\n", txn_id);
  transactions.erase(txn_id);
  // TODO(jblomer): race-free deletion of written blocks: write random
  // id into metadata block with refcount 0
  return STATUS_OK;
}

void Usage(const char *progname) {
  printf("%s <RAMCloud locator> <Cvmfs cache socket>\n", progname);
}


int main(int argc, char **argv) {
  if (argc < 3) {
    Usage(argv[0]);
    return 1;
  }
  ramcloud = new RAMCloud::RamCloud(argv[1]);
  table_blocks = ramcloud->createTable("cvmfs_blocks");
  table_metadata = ramcloud->createTable("cvmfs_metadata");
  printf("Connected to RAMCloud %s\n", argv[1]);

  struct cvmcache_callbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.cvmcache_chrefcnt = rc_chrefcnt;
  callbacks.cvmcache_obj_info = rc_obj_info;
  callbacks.cvmcache_pread = rc_pread;
  callbacks.cvmcache_start_txn = rc_start_txn;
  callbacks.cvmcache_write_txn = rc_write_txn;
  callbacks.cvmcache_commit_txn = rc_commit_txn;
  callbacks.cvmcache_abort_txn = rc_abort_txn;

  ctx = cvmcache_init(&callbacks);
  int retval = cvmcache_listen(ctx, argv[2]);
  assert(retval);
  printf("Listening for cvmfs clients on %s\n", argv[2]);
  chown(argv[2], 993, 992);
  cvmcache_process_requests(ctx);
  while (true) {
    sleep(1);
  }
  delete ramcloud;
  return 0;
}
