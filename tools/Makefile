CPP=g++
CFLAGS=-I ../indexed_bzip2 -std=c++11 -Wall -Wextra -Wshadow

bzcat: bzcat.o
	$(CPP) $^ -o $@

bzcat.o: bzcat.cpp ../indexed_bzip2/bzip2.h
	$(CPP) $(CFLAGS) -c $< -o $@
