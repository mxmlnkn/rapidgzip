CPP=clang++
CFLAGS=-I ../indexed_bzip2 -std=c++17 -Wall -Wextra -Wshadow -Wsuggest-override -O3 -g
LFLAGS=-pthread

all: testBitStringFinder testThreadPool

testBitStringFinder: testBitStringFinder.o
	$(CPP) $(LFLAGS) $^ -o $@

testBitStringFinder.o: testBitStringFinder.cpp ../indexed_bzip2/BitStringFinder.hpp
	$(CPP) $(CFLAGS) -c $< -o $@

testThreadPool: testThreadPool.o
	$(CPP) $(LFLAGS) $^ -o $@

testThreadPool.o: testThreadPool.cpp ../indexed_bzip2/ThreadPool.hpp ../indexed_bzip2/JoiningThread.hpp
	$(CPP) $(CFLAGS) -c $< -o $@
