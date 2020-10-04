CPP=g++
CFLAGS=-I ../indexed_bzip2 -std=c++11 -Wall -Wextra -Wshadow -Wsuggest-override -O3 -DNDEBUG

bzcat: bzcat.o
	$(CPP) $^ -o $@

bzcat.o: bzcat.cpp ../indexed_bzip2/bzip2.hpp ../indexed_bzip2/BZ2Reader.hpp ../indexed_bzip2/BitReader.hpp ../indexed_bzip2/FileReader.hpp
	$(CPP) $(CFLAGS) -c $< -o $@
