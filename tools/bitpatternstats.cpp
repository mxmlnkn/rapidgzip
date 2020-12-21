/**
make && time ./bitpatternstats -n 16 -p enwiki-20201101-pages-articles-multistream.xml.bz2 > wiki-counts-2B.dat && ../results/plotBitPatternFrequencies.py wiki-counts-2B.dat
    real	3m13.837s
    user	66m43.648s
    sys	0m16.707s

    Most and least common patterns:
    0x0000 -> 49948860
    0x5555 -> 17513597
    0xaaaa -> 17008988
    0xffff -> 15832038
    0x2222 -> 8700862
    0x4444 -> 8617850
    0x8888 -> 8537502
    0x4924 -> 8272395
    0x1111 -> 8219291
    ...
    0x4fff -> 441493
    0x8fff -> 452075
    0x3ffe -> 478984
    0x47ff -> 486405
    0x27ff -> 498233
    0x9fff -> 505344
    0x2fff -> 511492
    0x3ffd -> 512230
    0x1ffe -> 516314
    0xfff6 -> 541427

stat enwiki-20201101-pages-articles-multistream.xml.bz2
      File: /media/e/IRC/enwiki-20201101-pages-articles-multistream.xml.bz2
      Size: 18902274829	Blocks: 36918576   IO Block: 4096   regular file
    Device: fd02h/64770d	Inode: 60817508    Links: 1
    Access: (0664/-rw-rw-r--)  Uid: ( 1000/ hypatia)   Gid: ( 1000/ hypatia)
    Access: 2020-11-26 19:11:37.823787121 +0100
    Modify: 2020-11-26 19:03:26.029598983 +0100
    Change: 2020-11-26 19:03:26.029598983 +0100
     Birth: -
 -> 100 MiB/s
    pretty ok I guess. the lst 10s or so was a straggler, but still very good for something written in half an hour.

make && time ./bitpatternstats -n 24 -p enwiki-20201101-pages-articles-multistream.xml.bz2 > counts-3B.dat
    real	20m23.492s
    user	455m53.464s
    sys	1m35.895s
 -> takes much longer, probably because the counts to not fit into the cache anymore!

../results/plotBitPatternFrequencies.py counts-3B.dat
    Most and least common patterns:
    0x000000 -> 30754288
    0x7fffff -> 10566260
    0x2aaaaa -> 8780793
    0x555555 -> 8616279
    0x000001 -> 1760288
    0x400000 -> 1760264
    0x200000 -> 1624002
    0x124924 -> 1477676
    0x500000 -> 1414673
    ...
    0x1ffff2 -> 629
    0x1ffff6 -> 678
    0x1ffffa -> 687
    0x0ffffa -> 696
    0x1ffff4 -> 709
    0x1ffff0 -> 819
    0x0ffff2 -> 848
    0x0ffff4 -> 853
    0x0ffff6 -> 871
    0x27fffa -> 906

time ./blockfinder enwiki-20201101-pages-articles-multistream.xml.bz2
    [...]
    18901493744 B 0 b -> magic bytes: 0x31415926
    18901550952 B 0 b -> magic bytes: 0x31415926
    18901605573 B 0 b -> magic bytes: 0x31415926
    18901686944 B 0 b -> magic bytes: 0x31415926
    18901748816 B 0 b -> magic bytes: 0x31415926
    18901800143 B 0 b -> magic bytes: 0x31415926
    18901885076 B 0 b -> magic bytes: 0x31415926
    18901936923 B 0 b -> magic bytes: 0x31415926
    18902000265 B 0 b -> magic bytes: 0x31415926
    18902066034 B 0 b -> magic bytes: 0x31415926
    18902166619 B 0 b -> magic bytes: 0x31415926
    18902260010 B 0 b -> magic bytes: 0x31415926
    18902274782 B 0 b -> magic bytes: 0x31415926
    Found 216637 blocks

    real	0m11.789s
    user	1m30.741s
    sys	0m6.347s
 -> anything occurings similarly often as the number of blocks might be some similarity in the block header data

time ./blockfinder enwiki-20201101-pages-articles-multistream.xml.bz2 # bitStringToFind changed to EOS
    18901748801 B 1 b -> magic bytes: 0x177245385090
    18901800128 B 6 b -> magic bytes: 0x177245385090
    18901885061 B 2 b -> magic bytes: 0x177245385090
    18901936908 B 3 b -> magic bytes: 0x177245385090
    18902000250 B 6 b -> magic bytes: 0x177245385090
    18902066019 B 7 b -> magic bytes: 0x177245385090
    18902166604 B 1 b -> magic bytes: 0x177245385090
    18902259995 B 1 b -> magic bytes: 0x177245385090
    18902274767 B 2 b -> magic bytes: 0x177245385090
    18902274818 B 2 b -> magic bytes: 0x177245385090
    Found 207105 blocks

    real	0m11.209s
    user	1m30.077s
    sys	0m5.873s
 -> multiple EOS bytes because it is a multistream bz2!


make && time ./bitpatternstats -n 0 -p /dev/shm/large.bz2
    # Bit Pattern | Frequencies
    0 7636824856

make && time ./bitpatternstats -n 1 -p /dev/shm/large.bz2 > large-counts-1b.dat && cat large-counts-1b.dat
    real	0m8.631s
    user	2m56.329s
    sys	0m0.348s
    # Bit Pattern | Frequencies
    0 3812144307
    1 3824680549

make && time ./bitpatternstats -n 2 -p /dev/shm/large.bz2 > large-counts-2b.dat && cat large-counts-2b.dat
    real	0m7.742s
    user	2m54.916s
    sys	0m0.320s
    # Bit Pattern | Frequencies
    0 1902142303
    1 1910002016
    2 1910002004
    3 1914678533

make && time ./bitpatternstats -n 8 -p /dev/shm/large.bz2 > large-counts-1B.dat && ../results/plotBitPatternFrequencies.py large-counts-1B.dat
    real	0m7.889s
    user	2m54.984s
    sys	0m0.423s
    Most and least common patterns:
    0x00 -> 32034481
    0xf7 -> 31632986
    0xef -> 31591782
    0x7d -> 31118148
    0xbd -> 31096031
    0x7b -> 31095787
    0xdb -> 31070827
    0xde -> 30979007
    0xfb -> 30957592
    ...
    0xff -> 27771074
    0xc0 -> 28725923
    0xe0 -> 28756014
    0x80 -> 29046454
    0x01 -> 29046478
    0x05 -> 29057123
    0x0b -> 29100918
    0x02 -> 29128775
    0xf0 -> 29136816
    0xa0 -> 29141527

make && time ./bitpatternstats -n 16 -p large.bz2 > large-counts-2B.dat && ../results/plotBitPatternFrequencies.py large-counts-2B.dat
    real	0m9.216s
    user	3m18.019s
    sys	0m0.695s
    Most and least common patterns:
    0x000000 -> 2040597
    0x00ffff -> 414321
    0x005555 -> 337436
    0x00aaaa -> 332126
    0x004924 -> 258493
    0x002492 -> 255534
    0x009249 -> 246512
    0x000001 -> 245355
    0x008000 -> 245331
    ...
    0x007fff -> 34342
    0x00fffe -> 34342
    0x003fff -> 44907
    0x00fffc -> 45995
    0x00fffd -> 48879
    0x00bfff -> 49967
    0x001fff -> 57546
    0x00fff8 -> 58454
    0x005fff -> 59721
    0x00fffa -> 60017

  -> zeros still hapen much more often. Because they are filler in the last 7 bits for blocks, I would expect them to appear on average 3.5x as often as other values. but not 20x more oftehn than the morst rare ...

make && time ./bitpatternstats -n 24 -p large.bz2 > large-counts-3B.dat && ../results/plotBitPatternFrequencies.py large-counts-3B.dat
    real	1m7.979s
    user	23m25.035s
    sys	0m8.163s
    Most and least common patterns:
    0x000000 -> 1320596
    0xffffff -> 386299
    0x555555 -> 125641
    0xaaaaaa -> 123513
    0x249249 -> 75896
    0x492492 -> 75461
    0x924924 -> 75279
    0x000001 -> 49742
    0x800000 -> 49718
    ...
    0x3ffffd -> 1
    0x1ffffc -> 1
    0x1ffffe -> 1
    0x8ffffe -> 1
    0x7ffffc -> 1
    0x7ffffa -> 1
    0x3ffffe -> 2
    0x4ffffe -> 2
    0x1ffffd -> 3
    0x3ffff9 -> 3

make && time ./blockfinder /dev/shm/large.bz2
    Block offsets  :
    954603097 B 0 b -> magic bytes: 0x177245385090
    Found 1 blocks

    real	0m0.491s
    user	0m4.647s
    sys	0m0.271s

make && time ./blockfinder /dev/shm/large.bz2
    [...]
    953391090 B 0 b -> magic bytes: 0x314159265359
    953764292 B 0 b -> magic bytes: 0x314159265359
    954055181 B 0 b -> magic bytes: 0x314159265359
    954263214 B 0 b -> magic bytes: 0x314159265359
    954483941 B 0 b -> magic bytes: 0x314159265359
    Found 1788 blocks

    real	0m1.232s
    user	0m4.697s
    sys	0m0.276s
*/

#include <fstream>
#include <future>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <cxxopts.hpp>

#include <BitReader.hpp>
#include <common.hpp>


bool
fileExists( const std::string& filePath )
{
    return std::ifstream( filePath ).good();
}


size_t
fileSize( const std::string& filePath )
{
    std::ifstream file( filePath );
    file.seekg( 0, std::ios_base::end );
    return file.tellg();
}


std::vector<size_t>
countBitPatterns( const std::string& filePath,
                  unsigned int       patternLength,
                  size_t             offset = 0,
                  size_t             size = std::numeric_limits<size_t>::max() )
{
    size_t mask = 0;
    if ( patternLength > sizeof( mask ) * CHAR_BIT ) {
        throw std::invalid_argument( "Pattern to search for longer than buffer data type!" );
    }
    if ( patternLength == sizeof( mask ) * CHAR_BIT ) {
        mask = std::numeric_limits<size_t>::max();
    } else {
        mask = ( 1ULL << patternLength ) - 1ULL;
    }

    std::vector<size_t> counts( mask + 1ULL, 0 );

    BitReader file( filePath );
    file.seek( offset );
    if ( file.closed() || file.eof() ) {
        throw std::invalid_argument( "Given file could not be opened!" );
    }

    size_t bitsRead = 0;
    size_t lastBits = 0;
    while ( !file.eof() && ( bitsRead < size ) ) {
        lastBits = ( ( lastBits << 1U ) | file.read( 1 ) ) & mask;
        ++counts.at( lastBits );
        ++bitsRead;
    }

    return counts;
}


int
main( int    argc,
      char** argv )
{
    cxxopts::Options options( "bitpatternstats",
                              "Simple tool to count 0s and 1s in respect to the last n bits before it." );

    options.add_options()
        ( "h,help", "Print this help mesage." )
        ( "i,input", "Input file.",
          cxxopts::value<std::string>() )
        ( "n,pattern-bits",
          "The returned table will contain 2^n entries holding the number of occurences per pattern.",
          cxxopts::value<unsigned int>()->default_value( "16" ) )
        ( "p,parallelism",
          "The number of parallel threads to use for processing the file.",
          cxxopts::value<unsigned int>()->default_value( "1" )->implicit_value( "0" ) );

    options.parse_positional( { "input" } );

    const auto parsedArgs = options.parse( argc, argv );

    if ( parsedArgs.count( "help" ) > 0 ) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    const auto filePath = parsedArgs["input"].as<std::string>();
    const auto bitsPerPattern = parsedArgs["pattern-bits"].as<unsigned int>();
    auto parallelism = parsedArgs["parallelism"].as<unsigned int>();
    if ( parallelism == 0 ) {
        parallelism = std::thread::hardware_concurrency();
    }

    std::list<std::future<std::vector<size_t> > > results;
    const auto sizeInBits = fileSize( filePath ) * CHAR_BIT;
    const auto sizePerChunk = ceilDiv( sizeInBits, parallelism );
    size_t offset = 0;
    for ( auto i = 0U; i < parallelism; ++i, offset += sizePerChunk ) {
        results.emplace_back( std::async( &countBitPatterns, filePath, bitsPerPattern, offset, sizePerChunk ) );
    }

    std::vector<size_t> totalCounts;
    for ( auto& result : results ) {
        auto counts = result.get();
        if ( totalCounts.empty() ) {
            totalCounts = std::move( counts );
        } else if ( counts.size() == totalCounts.size() ) {
            for ( size_t i = 0; i < totalCounts.size(); ++i ) {
                totalCounts[i] += counts[i];
            }
        } else {
            throw std::runtime_error( "Mismatching count sizes! Cannot reduce." );
        }
    }

    std::cout << "# Bit Pattern | Frequencies\n";
    for ( size_t i = 0; i < totalCounts.size(); ++i ) {
        std::cout << i << " " << totalCounts[i] << "\n";
    }

    return 0;
}
