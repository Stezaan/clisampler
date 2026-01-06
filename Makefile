CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2
LDFLAGS = -lavcodec -lavformat -lavutil -lswresample
TARGET = clisampler

# Main version with FFmpeg API
all: $(TARGET)

$(TARGET): clisampler.cpp
	$(CXX) $(CXXFLAGS) clisampler.cpp -o $(TARGET) $(LDFLAGS)

# Simple version using system calls
simple: clisampler_simple.cpp
	$(CXX) $(CXXFLAGS) clisampler_simple.cpp -o $(TARGET)_simple

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

clean:
	rm -f $(TARGET) $(TARGET)_simple *.o

.PHONY: all simple install clean
