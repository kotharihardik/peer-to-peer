CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
COMMON   := ../../common

# OpenSSL (for SHA1). On macOS it comes from Homebrew; on Linux the system paths work.
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
ifneq ($(OPENSSL_PREFIX),)
  CPPFLAGS += -I$(OPENSSL_PREFIX)/include
  LDFLAGS  += -L$(OPENSSL_PREFIX)/lib
endif
CPPFLAGS += -I$(COMMON)
LDLIBS   += -lcrypto

SRC := main.cpp Client.cpp TrackerClient.cpp TrackerIndex.cpp \
       $(COMMON)/Socket.cpp $(COMMON)/TcpServer.cpp $(COMMON)/Utils.cpp \
       $(COMMON)/Sha1.cpp $(COMMON)/FileMetadata.cpp $(COMMON)/SharedFiles.cpp \
       $(COMMON)/PeerServer.cpp $(COMMON)/UploadManager.cpp \
       $(COMMON)/Download.cpp $(COMMON)/DownloadManager.cpp
# Shared sources are compiled into THIS directory as common_*.o, so each program
# keeps its own copies and two parallel builds never write the same object file.
OBJ := $(patsubst %.cpp,%.o,$(filter-out $(COMMON)/%,$(SRC))) \
       $(patsubst $(COMMON)/%.cpp,common_%.o,$(filter $(COMMON)/%,$(SRC)))

client: $(OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

common_%.o: $(COMMON)/%.cpp $(wildcard $(COMMON)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

%.o: %.cpp $(wildcard *.h) $(wildcard $(COMMON)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f *.o client

.PHONY: clean
