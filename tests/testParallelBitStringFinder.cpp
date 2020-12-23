#include <cassert>
#include <cstdio>
#include <iostream>
#include <thread>

#include <unistd.h>

#include "ParallelBitStringFinder.hpp"


namespace {
int gnTests = 0;  // NOLINT
int gnTestErrors = 0;  // NOLINT
}


template<class TemplatedBitStringFinder>
bool
testBitStringFinder( TemplatedBitStringFinder&& bitStringFinder,
                     const std::vector<size_t>& stringPositions )
{
    /* Gather all strings (time out at 1k strings because tests are written manually
     * and never will require that many matches, so somethings must have gone wrong */
    std::vector<size_t> matches;
    for ( int i = 0; i < 16; ++i ) {
        matches.push_back( bitStringFinder.find() );
        if ( matches.back() == std::numeric_limits<size_t>::max() ) {
            matches.pop_back();
            break;
        }
    }

    ++gnTests;
    if ( matches != stringPositions ) {
        ++gnTestErrors;
        std::cerr << "[FAIL] Matches: " << matches << " != " << stringPositions << "\n";
        return false;
    }

    return true;
}


template<uint8_t bitStringSize>
void
testBitStringFinder( uint64_t                          bitStringToFind,
                     const std::vector<unsigned char>& buffer,
                     const std::vector<size_t>&        stringPositions )
{
    std::cerr << "Test finding bit string 0x" << std::hex << bitStringToFind << std::dec
              << " of size " << static_cast<int>( bitStringSize ) << " in buffer of size " << buffer.size() << " B\n";

    auto const * const rawBuffer = reinterpret_cast<const char*>( buffer.data() );

    for ( size_t parallelization = 1; parallelization <= std::thread::hardware_concurrency(); parallelization *= 2 )
    {
        /* test the version working on an input buffer */
        ParallelBitStringFinder<bitStringSize> bitStringFinder( rawBuffer, buffer.size(), bitStringToFind );
        if ( !testBitStringFinder( std::move( bitStringFinder ), stringPositions ) ) {
            std::cerr << "Version working on input buffer failed!\n";
        }
    }

    for ( size_t parallelization = 1; parallelization <= std::thread::hardware_concurrency(); parallelization *= 2 )
    {
        /* test the version working on an input file by writing the buffer to a temporary file */
        auto const file = make_unique_file_ptr( std::tmpfile() );  // NOLINT
        std::fwrite( buffer.data(), sizeof( buffer[0] ), buffer.size(), file.get() );
        /**
         * Flush the file so that BitReader sees the written data when accessing the file through the file descriptor.
         * Don't close file because:
         * > On some implementations (e.g. Linux), this function actually creates, opens, and immediately deletes
         * > the file from the file system
         * @see https://en.cppreference.com/w/cpp/io/c/tmpfile
         * Also, use smallest sane value for fileBufferSizeBytes = sizeof( uint64_t ) in order to check that
         * recognizing bit strings accross file buffer borders works correctly.
         */
        std::fflush( file.get() );
        ParallelBitStringFinder<bitStringSize> bitStringFinder(
            fileno( file.get() ), bitStringToFind, sizeof( uint64_t )
        );
        if ( !testBitStringFinder( std::move( bitStringFinder ), stringPositions ) ) {
            std::cerr << "Version working on input file failed!\n";
        }
    }
}


int
main()
{
    /* 0-size bit strings to find arguably makes no sense to test for. */
    //testBitStringFinder<0>( 0b0, {}, {} );
    //testBitStringFinder<0>( 0b0, { 0x00 }, {} );
    //testBitStringFinder<0>( 0b1111'1111, {}, {} );
    //testBitStringFinder<0>( 0b1111'1111, { 0x00 }, {} );

    testBitStringFinder<1>( 0b0, { 0b0000'1111 }, { 0, 1, 2, 3 } );
    testBitStringFinder<1>( 0b0, { 0b1010'1010 }, { 1, 3, 5, 7 } );
    testBitStringFinder<1>( 0b0, { 0b1111'1111 }, {} );
    testBitStringFinder<1>( 0b0, { 0b0111'1111, 0b1111'1110 }, { 0, 15 } );
    testBitStringFinder<2>( 0b0, { 0b0000'1111 }, { 0, 1, 2 } );
    testBitStringFinder<3>( 0b0, { 0b0000'1111 }, { 0, 1 } );
    testBitStringFinder<4>( 0b0, { 0b0000'1111 }, { 0 } );
    testBitStringFinder<5>( 0b0, { 0b0000'1111 }, {} );

    testBitStringFinder<1>( 0b1111'1111, { 0b0000'1111 }, { 4, 5, 6, 7 } );
    testBitStringFinder<1>( 0b1111'1111, { 0b1010'1010 }, { 0, 2, 4, 6 } );
    testBitStringFinder<8>( 0b1111'1111, { 0b1111'1111 }, { 0 } );
    testBitStringFinder<1>( 0b1111'1111, { 0b1000'0000, 0b0000'0001 }, { 0, 15 } );
    testBitStringFinder<2>( 0b1111'1111, { 0b0000'1111 }, { 4, 5, 6 } );
    testBitStringFinder<3>( 0b1111'1111, { 0b0000'1111 }, { 4, 5 } );
    testBitStringFinder<4>( 0b1111'1111, { 0b0000'1111 }, { 4 } );
    testBitStringFinder<5>( 0b1111'1111, { 0b0000'1111 }, {} );

    testBitStringFinder<10>( 0b10'1010'1010, { 0b0101'0101, 0b0101'0101 }, { 1, 3, 5 } );
    testBitStringFinder<48>( 0x314159265359ULL, { 0x11, 0x41, 0x59, 0x26, 0x53, 0x59 }, {} );
    testBitStringFinder<48>( 0x314159265359ULL, { 0x31, 0x41, 0x59, 0x26, 0x53, 0x58 }, {} );
    testBitStringFinder<48>( 0x314159265359ULL, { 0x31, 0x41, 0x59, 0x26, 0x53, 0x59 }, { 0 } );
    testBitStringFinder<48>( 0x314159265359ULL, { 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0, 0 }, { 0 } );
    testBitStringFinder<48>( 0x314159265359ULL, { 0, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0, 0 }, { 8 } );
    testBitStringFinder<48>( 0x314159265359ULL, { 0, 0, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0, 0 }, { 16 } );
    testBitStringFinder<48>( 0x314159265359ULL, { 0, 0, 0, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0, 0 }, { 24 } );
    testBitStringFinder<48>( 0x314159265359ULL, { 0, 0, 0, 0, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0, 0 }, { 32 } );

    /* Tests with second match a lot further away and definitely over the loading chunk size. */
    {
        const std::vector<unsigned char> buffer = { 0, 0, 0, 0, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0, 0 };
        const std::vector<size_t> expectedResults = { 32 };

        const std::vector<char> secondMatchingString = { 0x31, 0x41, 0x59, 0x26, 0x53, 0x59 };
        auto const minSubChunkSize = 4096;
        /** at this offset the second sub chunk begins and it will actually become multi-threadad */
        auto const specialOffset = minSubChunkSize - buffer.size() - secondMatchingString.size();

        const std::vector<size_t> offsetsToTest = { 1, 100, 123, 1024, 28*1024, 32*1024*1024,
                                                    specialOffset - 1, specialOffset, specialOffset + 1 };

        for ( const auto offset : offsetsToTest ) {
            auto tmpResults = expectedResults;
            tmpResults.push_back( ( buffer.size() + offset ) * CHAR_BIT );

            auto tmpBuf = buffer;
            tmpBuf.resize( tmpBuf.size() + offset, 0 );
            for ( auto c : secondMatchingString ) {
                tmpBuf.push_back( c );
            }

            testBitStringFinder<48>( 0x314159265359ULL, tmpBuf, tmpResults );
        }
    }

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
