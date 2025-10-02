CXX = g++
CXXFLAGS = -std=c++11 -pthread -Wall
OPENSSL_INC = -I/opt/homebrew/opt/openssl@3/include
OPENSSL_LIB = -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto

SRC = command.cpp tracker.cpp upload.cpp download.cpp globals.cpp sync.cpp
OBJ = $(SRC:.cpp=.o)

TARGET = tracker

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OPENSSL_INC) $(OPENSSL_LIB)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPENSSL_INC) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
