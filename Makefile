LIBCVMFS_CACHE_DIR = .
RAMCLOUD_DIR = RAMCloud

CXX = g++
CXXFLAGS = -std=c++11 -I$(LIBCVMFS_CACHE_DIR) -I$(RAMCLOUD_DIR)/src -I$(RAMCLOUD_DIR)/obj.master -g -Wall -pthread
LDFLAGS = $(LIBCVMFS_CACHE_DIR)/libcvmfs_cache.a $(RAMCLOUD_DIR)/obj.master/libramcloud.a -lrt -lcrypto
LDFLAGS_RAMCLOUD = -lpcrecpp -lprotobuf -lboost_system

all: cvmfs_cache_ramcloud

cvmfs_cache_ramcloud: cvmfs_cache_ramcloud.cc
	$(CXX) $(CXXFLAGS) -o cvmfs_cache_ramcloud cvmfs_cache_ramcloud.cc $(LDFLAGS) $(LDFLAGS_RAMCLOUD)

startrc:
	rm -f /tmp/coordinator.log /tmp/master*.log
	screen -dmS coordinator $(RAMCLOUD_DIR)/obj.master/coordinator -n --logFile /tmp/coordinator.log -C tcp:host=127.0.0.1,port=11211 -d 5000 --timeout 5000
	screen -dms master1 $(RAMCLOUD_DIR)/obj.master/server -r 0 --logFile /tmp/master1.log -C tcp:host=127.0.0.1,port=11211 -L tcp:host=127.0.0.1,port=11242 --timeout 5000 -m

stoprc:
	killall screen

clean:
	rm -f cvmfs_cache_ramcloud


