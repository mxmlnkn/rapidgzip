#CPP=g++
#CFLAGS=-I ../indexed_bzip2 -I ../external/cxxopts/include -std=c++17 -Wall -Wextra -Wshadow -Wsuggest-override -g
#CFLAGS=-I ../indexed_bzip2 -I ../external/cxxopts/include -std=c++17 -Wall -Wextra -Wshadow -Wsuggest-override -O3 -DNDEBUGCPP=clang++
CPP=clang++
CFLAGS=-I ../indexed_bzip2 -I ../external/cxxopts/include -std=c++17 -Wall -Wextra -Wshadow -g
CFLAGS=-I ../indexed_bzip2 -I ../external/cxxopts/include -std=c++17 -Wall -Wextra -Wshadow -O3 -DNDEBUG
LFLAGS=-pthread

all: bzcat blockfinder bitpatternstats

bzcat: bzcat.o
	$(CPP) $(LFLAGS) $^ -o $@

bzcat.o: bzcat.cpp \
		 ../indexed_bzip2/bzip2.hpp \
		 ../indexed_bzip2/BZ2Reader.hpp \
		 ../indexed_bzip2/BitReader.hpp \
		 Makefile
	$(CPP) $(CFLAGS) -c $< -o $@

blockfinder: blockfinder.o
	$(CPP) $(LFLAGS) $^ -o $@

blockfinder.o: blockfinder.cpp \
			   ../indexed_bzip2/BitStringFinder.hpp \
			   ../indexed_bzip2/BitReader.hpp \
			   ../indexed_bzip2/ParallelBitStringFinder.hpp \
			   ../indexed_bzip2/ThreadPool.hpp \
			   ../indexed_bzip2/JoiningThread.hpp \
			   ../indexed_bzip2/common.hpp \
			   Makefile
	$(CPP) $(CFLAGS) -c $< -o $@

bitpatternstats: bitpatternstats.o
	$(CPP) $(LFLAGS) $^ -o $@

bitpatternstats.o: bitpatternstats.cpp \
				   ../indexed_bzip2/BitReader.hpp \
				   ../indexed_bzip2/common.hpp \
				   ../external/cxxopts/include/cxxopts.hpp \
				   Makefile
	$(CPP) $(CFLAGS) -c $< -o $@
