#include <libcvmfs_cache.h>
#include <RamCloud.h>

#include <unistd.h>

#include <cassert>
#include <cstring>
#include <string>

using namespace std;

static int rc_chrefcnt(const cvmcache_hash *id, int32_t change_by) {
  return STATUS_UNKNOWN;
}

int rc_obj_info(const cvmcache_hash *id,
                struct cvmcache_object_info *info)
{
  return STATUS_UNKNOWN;
}


static int rc_pread(const cvmcache_hash *id,
                    uint64_t offset,
                    uint32_t *size,
                    unsigned char *buffer)
{
  return STATUS_UNKNOWN;
}


static int rc_start_txn(const cvmcache_hash *id,
                        uint64_t txn_id,
                        struct cvmcache_object_info *info)
{
  return STATUS_UNKNOWN;
}

int rc_write_txn(uint64_t txn_id,
                 unsigned char *buffer,
                 uint32_t size)
{
  return STATUS_UNKNOWN;
}

int rc_commit_txn(uint64_t txn_id) {
  return STATUS_UNKNOWN;
}

int rc_abort_txn(uint64_t txn_id) {
  return STATUS_UNKNOWN;
}


int main() {
  struct cvmcache_callbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.cvmcache_chrefcnt = rc_chrefcnt;
  callbacks.cvmcache_obj_info = rc_obj_info;
  callbacks.cvmcache_pread = rc_pread;
  callbacks.cvmcache_start_txn = rc_start_txn;
  callbacks.cvmcache_write_txn = rc_write_txn;
  callbacks.cvmcache_commit_txn = rc_commit_txn;
  callbacks.cvmcache_abort_txn = rc_abort_txn;

  struct cvmcache_context *ctx = cvmcache_init(&callbacks);
  int retval = cvmcache_listen(ctx, strdup("/tmp/first_plugin"));
  assert(retval);
  cvmcache_process_requests(ctx);
  while (true) {
    sleep(1);
  }
  return 0;
}
