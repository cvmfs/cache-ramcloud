LIBCVMFS_CACHE_DIR = .
RAMCLOUD_DIR = RAMCloud

CXX = g++
CXXFLAGS = -std=c++11 -I$(LIBCVMFS_CACHE_DIR) -I$(RAMCLOUD_DIR)/src -I$(RAMCLOUD_DIR)/obj.master -g -Wall -pthread
LDFLAGS = $(LIBCVMFS_CACHE_DIR)/libcvmfs_cache.a $(RAMCLOUD_DIR)/obj.master/libramcloud.a -lrt -lcrypto
LDFLAGS_RAMCLOUD = -lpcrecpp -lprotobuf

all: cvmfs_cache_ramcloud

cvmfs_cache_ramcloud: cvmfs_cache_ramcloud.cc
	$(CXX) $(CXXFLAGS) -o cvmfs_cache_ramcloud cvmfs_cache_ramcloud.cc $(LDFLAGS) $(LDFLAGS_RAMCLOUD)

clean:
	rm -f cvmfs_cache_ramcloud


