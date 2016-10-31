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

#include "schema.h"

using namespace std;

struct cvmcache_context *ctx;
RAMCloud::RamCloud *ramcloud;
uint64_t table_parts;
uint64_t table_transactions;
uint64_t table_objects;

uint64_t part_size;

struct TxnTransient {
  TxnTransient() { memset(&id, 0, sizeof(id)); }
  explicit TxnTransient(const struct cvmcache_hash &hash) : id(hash) { }

  struct cvmcache_hash id;
  ObjectData object;
};

map<ObjectKey, Nonce> open_files;
map<uint64_t, TxnTransient> transactions;


static int rc_chrefcnt(struct cvmcache_hash *id, int32_t change_by) {
  //printf("Refcnt %s (%d)... ", cvmcache_hash_print(id), change_by);
  ObjectKey key(*id);
  RAMCloud::Buffer buffer;
  ObjectData object;
  bool success = false;
  do {
    uint64_t version;
    try {
      ramcloud->read(table_objects, &key, sizeof(key), &buffer,
                     NULL, &version);
    } catch (RAMCloud::ClientException &e) {
      //printf("not available\n");
      return CVMCACHE_STATUS_NOENTRY;
    }
    assert(buffer.size() == sizeof(ObjectData));
    buffer.copy(0, sizeof(object), &object);
    object.last_updated = time(NULL);
    object.refcnt += change_by;
    if (object.refcnt < 0)
      return CVMCACHE_STATUS_BADCOUNT;
    RAMCloud::RejectRules rules;
    memset(&rules, 0, sizeof(rules));
    rules.givenVersion = version;
    rules.versionNeGiven = 1;
    try {
      ramcloud->write(table_objects, &key, sizeof(key),
                      &object, sizeof(object), &rules);
      success = true;
    } catch (RAMCloud::ClientException &e) { }
  } while (!success);
  if (change_by > 0) {
    open_files[ObjectKey(*id)] = object.nonce;
  }
  printf("%s file %s\n",
         (change_by == 1) ? "opening" : "closing", object.description);
  return CVMCACHE_STATUS_OK;
}


int rc_obj_info(struct cvmcache_hash *id,
                struct cvmcache_object_info *info)
{
  //printf("Info %s... ", cvmcache_hash_print(id));
  ObjectKey key(*id);
  RAMCloud::Buffer buffer;
  try {
    ramcloud->read(table_objects, &key, sizeof(key), &buffer);
    assert(buffer.size() == sizeof(ObjectData));
    ObjectData object;
    buffer.copy(0, sizeof(object), &object);
    // Currently only size is needed
    info->size = object.size;
    return CVMCACHE_STATUS_OK;
  } catch (RAMCloud::ClientException &e) {
    //printf("not available\n");
    return CVMCACHE_STATUS_NOENTRY;
  }
}


static int rc_pread(struct cvmcache_hash *id,
                    uint64_t offset,
                    uint32_t *size,
                    unsigned char *buffer)
{
  Nonce nonce = open_files[ObjectKey(*id)];
  PartKey key(nonce, offset / part_size);
  uint32_t nbytes = 0;
  while (nbytes < *size) {
    RAMCloud::Buffer rc_buffer;
    try {
      //printf("  reading %s, %" PRIu64 "\n",
      //   cvmcache_hash_print(&block_key.hash), block_key.part_nr);
      //printf("before read\n");
      ramcloud->read(table_parts, &key, sizeof(key), &rc_buffer);
      //printf("after read\n");
      uint64_t in_block_offset = 0;
      if (nbytes == 0) {
        in_block_offset =
          offset - (key.part_nr * part_size);
      }
      uint32_t remaining =
        std::min((uint32_t)(rc_buffer.size() - in_block_offset),
                            *size - nbytes);
      rc_buffer.copy(in_block_offset, remaining, buffer + nbytes);
      nbytes += remaining;
    } catch (RAMCloud::ClientException &e) {
      //printf("  ...pread failed with %s\n", e.what());
      break;
    }
    key.part_nr++;
  }
  *size = nbytes;
  //printf("  ... ok (%u)\n", nbytes);
  // TODO: out of bounds
  return CVMCACHE_STATUS_OK;
}


static int rc_start_txn(struct cvmcache_hash *id,
                        uint64_t txn_id,
                        struct cvmcache_object_info *info)
{
  TxnTransient txn(*id);
  txn.object.nonce.Generate();
  txn.object.type = info->type;
  txn.object.SetDescription(info->description);

  TxnKey txn_key(txn.object.nonce);
  TxnData txn_data(time(NULL));
  try {
    ramcloud->write(table_transactions, &txn_key, sizeof(txn_key),
                    &txn_data, sizeof(txn_data));
  } catch (RAMCloud::ClientException &e) {
    return CVMCACHE_STATUS_IOERR;
  }
  transactions[txn_id] = txn;
  return CVMCACHE_STATUS_OK;
}


int rc_write_txn(uint64_t txn_id,
                 unsigned char *buffer,
                 uint32_t size)
{
  //printf("Write transaction %" PRIu64 "\n", txn_id);
  TxnTransient txn(transactions[txn_id]);
  PartKey key(txn.object.nonce, txn.object.size / part_size);
  //printf("  writing %s, %" PRIu64 "\n",
  //       cvmcache_hash_print(&block_key.hash), block_key.part_nr);
  try {
    ramcloud->write(table_parts, &key, sizeof(key), buffer, size);
  } catch (RAMCloud::ClientException &e) {
    return CVMCACHE_STATUS_IOERR;
  }

  txn.object.size += size;
  transactions[txn_id] = txn;
  return CVMCACHE_STATUS_OK;
}


int rc_commit_txn(uint64_t txn_id) {
  //printf("Commit transaction %" PRIu64 "\n", txn_id);
  TxnTransient txn(transactions[txn_id]);
  txn.object.refcnt = 1;
  txn.object.last_updated = time(NULL);
  ObjectKey key(txn.id);
  bool success = false;
  do {
    ObjectData object = txn.object;
    RAMCloud::Buffer buffer;
    RAMCloud::RejectRules rules;
    memset(&rules, 0, sizeof(rules));
    uint64_t version;
    try {
      ramcloud->read(table_objects, &key, sizeof(key), &buffer, NULL, &version);
      assert(buffer.size() == sizeof(ObjectData));
      buffer.copy(0, sizeof(object), &object);
      object.refcnt += 1;
      object.last_updated = time(NULL);
      rules.givenVersion = version;
      rules.versionNeGiven = 1;
    } catch (RAMCloud::ClientException &e) {
      rules.exists = 1;
    }
    try {
      ramcloud->write(table_objects, &key, sizeof(key),
                      &object, sizeof(object), &rules);
      success = true;
    } catch (RAMCloud::ClientException &e) { }
  } while (!success);

  TxnKey txn_key(txn.object.nonce);
  ramcloud->remove(table_transactions, &txn_key, sizeof(txn_key));
  transactions.erase(txn_id);
  //char *hash = cvmcache_hash_print(&txn.id);
  //printf("committed %s, size %" PRIu64 ", total size %" PRIu64 "\n",
  //       hash, txn.size, size_stored);
  //free(hash);
  return CVMCACHE_STATUS_OK;
}

int rc_abort_txn(uint64_t txn_id) {
  //printf("Abort transaction %" PRIu64 "\n", txn_id);
  transactions.erase(txn_id);
  // TODO(jblomer): remove from ramcloud
  return CVMCACHE_STATUS_OK;
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
  table_parts = ramcloud->createTable("cvmfs_parts");
  table_objects = ramcloud->createTable("cvmfs_objects");
  table_transactions = ramcloud->createTable("cvmfs_transactions");
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
  callbacks.capabilities = CVMCACHE_CAP_REFCOUNT;

  ctx = cvmcache_init(&callbacks);
  assert(ctx != NULL);
  part_size = cvmcache_max_object_size(ctx);
  int retval = cvmcache_listen(ctx, argv[2]);
  assert(retval);
  printf("Listening for cvmfs clients on %s\n", argv[2]);
  chown(argv[2], 993, 992);
  cvmcache_process_requests(ctx, 0);
  while (true) {
    sleep(1);
  }
  delete ramcloud;
  return 0;
}
