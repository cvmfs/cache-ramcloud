#include "schema.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstring>

using namespace std;


void Nonce::Generate() {
  int fd = open("/dev/urandom", O_RDONLY);
  assert(fd >= 0);
  size_t nbytes = read(fd, bytes, sizeof(bytes));
  assert(nbytes == sizeof(bytes));
  close(fd);
}


void ObjectData::SetDescription(const char *s) {
  if (s == NULL) {
    description[0] = '\0';
    return;
  }
  size_t l = std::min(static_cast<unsigned>(strlen(s)),
                      kMaxDescriptionLength - 1);
  memcpy(description, s, l);
  description[l + 1] = '\0';
}
