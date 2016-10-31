LIBCVMFS_CACHE_DIR = .
RAMCLOUD_DIR = RAMCloud

CXX = g++
CXXFLAGS_GENERIC = -std=c++11 -g -Wall -pthread
CXXFLAGS_CVMFS = -I$(LIBCVMFS_CACHE_DIR)
CXXFLAGS_RAMCLOUD = -I$(RAMCLOUD_DIR)/src -I$(RAMCLOUD_DIR)/obj.master
CXXFLAGS = $(CXXFLAGS_GENERIC) $(CXXFLAGS_CVMFS) $(CXXFLAGS_RAMCLOUD)
LDFLAGS_GENERIC = -lrt
LDFLAGS_CVMFS = $(LIBCVMFS_CACHE_DIR)/libcvmfs_cache.a -lcrypto
LDFLAGS_RAMCLOUD = $(RAMCLOUD_DIR)/obj.master/libramcloud.a -lpcrecpp -lprotobuf -lboost_system
LDFLAGS = $(LDFLAGS_GENERIC) $(LDFLAGS_CVMFS) $(LDFLAGS_RAMCLOUD)

all: cvmfs_cache_ramcloud

cvmfs_cache_ramcloud: cvmfs_cache_ramcloud.cc
	$(CXX) $(CXXFLAGS) -o cvmfs_cache_ramcloud cvmfs_cache_ramcloud.cc $(LDFLAGS)

startrc:
	rm -f /tmp/coordinator.log /tmp/master*.log
	screen -dmS coordinator $(RAMCLOUD_DIR)/obj.master/coordinator -n --logFile /tmp/coordinator.log -C basic+udp:host=127.0.0.1,port=11211 -d 5000 --timeout 5000
	screen -dms master1 $(RAMCLOUD_DIR)/obj.master/server -t 4000 -r 0 --logFile /tmp/master1.log -C basic+udp:host=127.0.0.1,port=11211 -L basic+udp:host=127.0.0.1,port=11242 --timeout 5000 -m

stoprc:
	killall screen

clean:
	rm -f cvmfs_cache_ramcloud

