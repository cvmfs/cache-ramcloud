CXX = g++
CXXFLAGS = -I. -g -Wall -pthread
LIBCVMFS_CACHE = libcvmfs_cache.a
LDFLAGS = $(LIBCVMFS_CACHE) -lrt -lcrypto


all: cvmfs_cache_ramcloud

cvmfs_cache_ramcloud: cvmfs_cache_ramcloud.cc
	$(CXX) $(CXXFLAGS) -o cvmfs_cache_ramcloud cvmfs_cache_ramcloud.cc $(LDFLAGS)

clean:
	rm -f cvmfs_cache_ramcloud


