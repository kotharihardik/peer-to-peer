CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
COMMON   := ../../common
CPPFLAGS += -I$(COMMON)

SRC := main.cpp Tracker.cpp TrackerState.cpp TrackerSynchronizer.cpp \
       $(COMMON)/Socket.cpp $(COMMON)/TcpServer.cpp $(COMMON)/Utils.cpp
# Shared sources are compiled into THIS directory as common_*.o, so each program
# keeps its own copies and two parallel builds never write the same object file.
OBJ := $(patsubst %.cpp,%.o,$(filter-out $(COMMON)/%,$(SRC))) \
       $(patsubst $(COMMON)/%.cpp,common_%.o,$(filter $(COMMON)/%,$(SRC)))

tracker: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

common_%.o: $(COMMON)/%.cpp $(wildcard $(COMMON)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

%.o: %.cpp $(wildcard *.h) $(wildcard $(COMMON)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f *.o tracker

.PHONY: clean
