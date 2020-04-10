CPP=g++
CFLAGS=-I ../indexed_bzip2 -std=c++11 -Wall -Wextra -Wshadow -O3 -DNDEBUG

bzcat: bzcat.o
	$(CPP) $^ -o $@

bzcat.o: bzcat.cpp ../indexed_bzip2/bzip2.h ../indexed_bzip2/BitReader.hpp
	$(CPP) $(CFLAGS) -c $< -o $@
