CC := gcc
CXX := g++
LD := $(CXX)
AR := ar
OBJCOPY := objcopy

CFLAGS := -ffunction-sections -O0 -std=c++1y -Wall -pthread
CXXFLAGS := -ffunction-sections -O0 -std=c++1y -Wall -pthread

all: example-01

example-01: example-01.cpp
	$(CXX) $(CFLAGS) $(CXXFLAGS) $< -o $@

clean:
	rm -f example-01
