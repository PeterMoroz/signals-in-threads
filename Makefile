CC := gcc
CXX := g++
LD := $(CXX)
AR := ar
OBJCOPY := objcopy

CXXFLAGS := -ffunction-sections -O0 -std=c++1y -Wall -pthread

all: example-01 example-02 example-03

example-01: example-01.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

example-02: example-02.cpp
	$(CXX) $(CXXFLAGS) -I/usr/local/include $< -L/usr/local/boost_1_59_0/stage/lib -lboost_system -lboost_thread -o $@
	
example-03: example-03.cpp
	$(CXX) $(CXXFLAGS) -I/usr/local/include $< -L/usr/local/boost_1_59_0/stage/lib -lboost_system -lboost_thread -o $@
	

clean:
	rm -f example-01 example-02 example-03
