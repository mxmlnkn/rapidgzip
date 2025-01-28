#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <blockfinder/PigzParallel.hpp>
#include <blockfinder/PigzNaive.hpp>
#include <blockfinder/PigzStringView.hpp>
#include <DataGenerators.hpp>
#include <filereader/Buffered.hpp>
#include <FileUtils.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


struct BenchmarkResults
{
    double duration{ 0 };
    std::size_t blockCount{ 0 };
};


size_t
findZeroBytesBitset( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4096;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};
    std::bitset<BUFFER_SIZE> zeroBytes;

    size_t count = 0;

    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        for ( size_t i = 0; i < nBytesRead; ++i ) {
            zeroBytes[i] = buffer[i] == 0;
        }
        count += zeroBytes.count();
    }

    return count;
}


size_t
findZeroBytesVector( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4096;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};
    std::vector<bool> zeroBytes( BUFFER_SIZE );

    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        for ( size_t i = 0; i < nBytesRead; ++i ) {
            zeroBytes[i] = buffer[i] == 0;
        }
    }

    return zeroBytes[BUFFER_SIZE / 2] ? 1 : 0;
}


size_t
testZeroBytesToChar( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4096;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};
    alignas( 64 ) std::array<char, BUFFER_SIZE> zeroBytes{};

    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        for ( size_t i = 0; i < nBytesRead; ++i ) {
            zeroBytes[i] = buffer[i] == 0 ? 1 : 0;
        }
    }

    return zeroBytes[BUFFER_SIZE / 2];
}


size_t
findZeroBytesBuffers( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4_Ki;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};
    alignas( 64 ) std::array<uint8_t, BUFFER_SIZE> zeroBytes{};
    alignas( 64 ) std::array<uint8_t, BUFFER_SIZE> ffBytes{};

    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );

        for ( size_t i = 0; i < nBytesRead; ++i ) {
            zeroBytes[i] = buffer[i] == 0 ? 1U : 0U;
            ffBytes[i] = buffer[i] == (char)0xFFU ? 1 : 0;
        }

        for ( size_t i = 4; i < nBytesRead; ++i ) {
            ffBytes[i] &= static_cast<uint8_t>( static_cast<uint32_t>( ffBytes  [i - 1] ) &
                                                static_cast<uint32_t>( zeroBytes[i - 2] ) &
                                                static_cast<uint32_t>( zeroBytes[i - 3] ) );
        }
    }

    return zeroBytes[BUFFER_SIZE / 2] + ffBytes[BUFFER_SIZE / 2];
}


size_t
findZeroBytes64Bit( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4_Ki;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};

    auto const* const buffer32 = reinterpret_cast<std::uint32_t*>( buffer.data() );

    /* Check endianness */
    for ( size_t i = 0; i < sizeof( std::uint32_t ); ++i ) {
        buffer[i] = static_cast<char>( i );
    }
    if ( buffer32[0] != 0x03'02'01'00UL ) {
        throw std::logic_error( "Not little endian as assumed!" );
    }

    /* Looking for 0x00 0x00 0xFF 0xFF in memory. Using little endian uint64_t, reverses the apparent byte order. */
    uint64_t constexpr TEST_STRING = 0xFF'FF'00'00ULL;
    uint64_t constexpr TEST_MASK   = 0xFF'FF'FF'FFULL;

    size_t count{ 0 };
    uint64_t bitBuffer = static_cast<uint8_t>( buffer[0] );
    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        for ( size_t i = 0; i < nBytesRead / sizeof( std::uint32_t ); ++i ) {
            bitBuffer >>= 32U;
            bitBuffer |= static_cast<uint64_t>( buffer32[i] ) << 32U;
            for ( size_t j = 0; j < sizeof( std::uint32_t ); ++j ) {
                const auto testMask = TEST_MASK << ( 8U * j );
                const auto testString = TEST_STRING << ( 8U * j );
                auto const doesMatch = ( bitBuffer & testMask ) == testString;

                if ( doesMatch ) {
                    ++count;
                }
            }
        }
    }

    return count;
}


size_t
findZeroBytes64BitLUT( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 128_Ki;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};

    auto const* const buffer32 = reinterpret_cast<std::uint32_t*>( buffer.data() );

    /* Check endianness */
    for ( size_t i = 0; i < sizeof( std::uint32_t ); ++i ) {
        buffer[i] = static_cast<char>( i );
    }
    if ( buffer32[0] != 0x03'02'01'00UL ) {
        throw std::logic_error( "Not little endian as assumed!" );
    }

    /* Looking for 0x00 0x00 0xFF 0xFF in memory. Using little endian uint64_t, reverses the apparent byte order. */
    uint64_t constexpr TEST_STRING = 0xFF'FF'00'00ULL;
    uint64_t constexpr TEST_MASK   = 0xFF'FF'FF'FFULL;
    /* Alternate test string for gzip stream header. */
    //uint64_t constexpr TEST_STRING = 0x61'D3'87'C8'08'08'8B'1FULL;
    //uint64_t constexpr TEST_MASK   = 0xFF'FF'FF'FF'FF'FF'FF'FFULL;

    std::array<uint64_t, 2ULL * 4ULL> testStrings = {
        TEST_STRING       , TEST_MASK,
        TEST_STRING << 8U , TEST_MASK << 8U,
        TEST_STRING << 16U, TEST_MASK << 16U,
        TEST_STRING << 24U, TEST_MASK << 24U
    };

    size_t count{ 0 };
    uint64_t bitBuffer = 0;
    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        for ( size_t i = 0; i < nBytesRead / sizeof( std::uint32_t ); ++i ) {
            /* Append new data to the left because the byte order is from lowest bits (right) to highest. */
            bitBuffer >>= 32U;
            bitBuffer |= static_cast<uint64_t>( buffer32[i] ) << 32U;

            for ( size_t j = 0; j < sizeof( std::uint32_t ); ++j ) {
                const auto testString = testStrings[2U * j + 0U];
                const auto testMask = testStrings[2U * j + 1U];
                auto const doesMatch = ( bitBuffer & testMask ) == testString;

                /* We want branching here because it happens rarely and when executing it always,
                 * it would introduce a data dependency to the last loop. */
                if ( doesMatch ) {
                    ++count;
                }
            }
        }
    }

    return count;
}


size_t
findStringView( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4096;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};
    constexpr std::string_view TEST_STRING{ "\0\0\xFF\xFF", 4 /* required or else strlen is used resulting in zero */ };

    std::size_t count{ 0 };
    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        std::string_view stringView( buffer.data(), nBytesRead );

        for ( auto position = stringView.find( TEST_STRING.data(), 0, TEST_STRING.size() );
              position != std::string_view::npos;
              position = stringView.find( TEST_STRING.data(), position + 1, TEST_STRING.size() ) )
        {
            ++count;
        }
    }

    return count;
}


#if 0
size_t
findZeroBytes128Bit( const UniqueFileReader& file )
{
    /**
     * extern __inline __m128i __attribute__((__gnu_inline__, __always_inline__, __artificial__))
     * _mm_cmpestrm (__m128i __X, int __LX, __m128i __Y, int __LY, const int __M)
     * {
     *   return (__m128i) __builtin_ia32_pcmpestrm128 ((__v16qi)__X, __LX,
     *                                                 (__v16qi)__Y, __LY,
     *                                                 __M);
     * }
     */
    return 0;
}
#endif


size_t
countZeroBytes( const UniqueFileReader& file )
{
    static constexpr size_t BUFFER_SIZE = 4096;
    alignas( 64 ) std::array<char, BUFFER_SIZE> buffer{};

    size_t count = 0;

    while ( !file->eof() ) {
        const auto nBytesRead = file->read( buffer.data(), buffer.size() );
        for ( size_t i = 0; i < nBytesRead; ++i ) {
            count += buffer[i] == 0 ? 1 : 0;
        }
    }

    return count;
}


template<typename BlockFinder>
BenchmarkResults
measureByteComparison( const UniqueFileReader& file,
                       BlockFinder             blockFinder )
{
    file->seekTo( 0 );
    BenchmarkResults result;

    const auto t0 = now();

    result.blockCount = blockFinder( file );

    result.duration = duration( t0, now() );
    return result;
}


template<typename BlockFinder>
void
measureByteComparison( const std::string& fileName,
                       BlockFinder        blockFinder )
{
    /* Load everything to memory to ignore disk I/O speeds. */
    BufferedFileReader::AlignedBuffer contents( fileSize( fileName ) );
    const auto nBytesRead = std::fread( contents.data(), 1, contents.size(), throwingOpen( fileName, "rb" ).get() );
    if ( nBytesRead != contents.size() ) {
        throw std::runtime_error( "Failed to read full file!" );
    }
    UniqueFileReader const fileReader = std::make_unique<BufferedFileReader>( std::move( contents ) );

    double minTime = std::numeric_limits<double>::infinity();
    BenchmarkResults result;
    for ( int iRepetition = 0; iRepetition < 5; ++iRepetition ) {
        result = measureByteComparison( fileReader, blockFinder );
        minTime = std::min( minTime, result.duration );
    }

    std::cout << "Searched " << nBytesRead << " B in " << minTime << " s -> "
              << static_cast<double>( nBytesRead ) / 1e6 / minTime << " MB/s and found " << result.blockCount
              << " blocks.\n";
}


template<typename BlockFinder>
BenchmarkResults
measureBlockFinderTime( const std::string& fileName )
{
    /* Load everything to memory to ignore disk I/O speeds. */
    BufferedFileReader::AlignedBuffer contents( fileSize( fileName ) );
    const auto nBytesRead = std::fread( contents.data(), 1, contents.size(), throwingOpen( fileName, "rb" ).get() );
    if ( nBytesRead != contents.size() ) {
        throw std::runtime_error( "Failed to read full file!" );
    }

    BlockFinder blockFinder( std::make_unique<BufferedFileReader>( std::move( contents ) ) );

    const auto t0 = now();

    size_t blockCount = 0;
    size_t blockOffset = 0;
    while ( ( blockOffset = blockFinder.find() ) != std::numeric_limits<size_t>::max() ) {
        ++blockCount;
    }

    const auto t1 = now();
    return BenchmarkResults{ duration( t0, t1 ), blockCount };
}


template<typename BlockFinder>
void
benchmarkBlockFinder( const std::string& fileName )
{
    const auto nBytesRead = fileSize( fileName );

    /* I love minimum time for micro-benchmarks because it returns the time without any context switches
     * and other noise in the limit of a large number of measurements. Note that it can never take less
     * time than physically possible. One caveat might be that it also "filters" out random noise caused
     * by the algorithm itself, e.g., if there is a performance race condition in multi-threaded code. */
    double minTime = std::numeric_limits<double>::infinity();
    BenchmarkResults result;
    for ( int iRepetition = 0; iRepetition < 5; ++iRepetition ) {
        result = measureBlockFinderTime<BlockFinder>( fileName );
        minTime = std::min( minTime, result.duration );
    }

    std::cout << "Searched " << nBytesRead << " B in " << minTime << " s -> "
              << static_cast<double>( nBytesRead ) / 1e6 / minTime << " MB/s and found " << result.blockCount
              << " blocks.\n";
}


int
main( int    argc,
      char** argv )
{
    std::optional<TemporaryDirectory> tmpFolder;
    std::string fileName;
    if ( ( argc > 1 ) && ( std::filesystem::exists( argv[1] ) ) ) {
        fileName = argv[1];
    } else {
        tmpFolder.emplace( createTemporaryDirectory( "indexed_bzip2.benchmarkPigzBlockFinder" ) );
        fileName = tmpFolder->path() / "random-base64";
        createRandomBase64( fileName, 512_Mi );

        const auto command = "pigz -k " + fileName;
        const auto returnCode = std::system( command.c_str() );
        if ( returnCode != 0 ) {
            std::cerr << "Failed to create a temporary file for benchmarking\n";
            return 1;
        }

        std::filesystem::rename( fileName + ".gz", fileName + ".pigz" );
        fileName += ".pigz";
    }

    try
    {
        using namespace rapidgzip::blockfinder;

        std::cout << "[countZeroBytes] ";
        measureByteComparison( fileName, countZeroBytes );
        std::cout << "[findZeroBytesBitset] ";
        measureByteComparison( fileName, findZeroBytesBitset );
        std::cout << "[findZeroBytesVector] ";
        measureByteComparison( fileName, findZeroBytesVector );
        std::cout << "[testZeroBytesToChar] ";
        measureByteComparison( fileName, testZeroBytesToChar );
        std::cout << "[findZeroBytesBuffers] ";
        measureByteComparison( fileName, findZeroBytesBuffers );
        std::cout << "[findZeroBytes64Bit] ";
        measureByteComparison( fileName, findZeroBytes64Bit );
        std::cout << "[findZeroBytes64BitLUT] ";
        measureByteComparison( fileName, findZeroBytes64BitLUT );
        std::cout << "[findStringView] ";
        measureByteComparison( fileName, findStringView );

        std::cout << "[blockfinder::PigzNaive] ";
        benchmarkBlockFinder<PigzNaive>( fileName );
        std::cout << "[blockfinder::PigzStringView] ";
        benchmarkBlockFinder<PigzStringView>( fileName );
        std::cout << "[blockfinder::PigzParallel] ";
        benchmarkBlockFinder<PigzParallel>( fileName );
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Exception was thrown: " << e.what() << "\n";
        return 1;
    }

    return 0;
}


/*
cmake --build . -- benchmarkPigzBlockFinder && src/benchmarks/benchmarkPigzBlockFinder

[countZeroBytes]            Searched 408430549 B in 0.099846 s -> 4091  MB/s and found 1540593 blocks.
[findZeroBytesBitset]       Searched 408430549 B in 0.692752 s -> 590   MB/s and found 1540598 blocks.
[findZeroBytesVector]       Searched 408430549 B in 0.694819 s -> 588   MB/s and found 0 blocks.
[testZeroBytesToChar]       Searched 408430549 B in 0.039854 s -> 10248 MB/s and found 0 blocks.
[findZeroBytesBuffers]      Searched 408430549 B in 0.249480 s -> 1637  MB/s and found 0 blocks.
[findZeroBytes64Bit]        Searched 408430549 B in 0.218732 s -> 1867  MB/s and found 2114 blocks.
[findZeroBytes64BitLUT]     Searched 408430549 B in 0.208149 s -> 1962  MB/s and found 2114 blocks.
[findStringView]            Searched 408430549 B in 0.056172 s -> 7271  MB/s and found 2112 blocks.

[blockfinder::PigzParallel]   Searched 408430549 B in 0.250202 s -> 1632  MB/s and found 2115 blocks.
[blockfinder::PigzStringView] Searched 408430549 B in 0.050008 s -> 8167  MB/s and found 2115 blocks.
[blockfinder::PigzParallel]   Searched 408430549 B in 0.080975 s -> 5044  MB/s and found 2115 blocks.

[blockfinder::PigzParallel]
    [BlockFetcher::~BlockFetcher]
       Time spent in:
           refillBuffer                   : 0.035006 s
           time spent waiting for futures : 0.0497037 s
    [BlockFetcher::~BlockFetcher]
       Time spent in:
           refillBuffer                   : 0.0364572 s
           time spent waiting for futures : 0.0391328 s
    [BlockFetcher::~BlockFetcher]
       Time spent in:
           refillBuffer                   : 0.0354477 s
           time spent waiting for futures : 0.0488605 s
    [BlockFetcher::~BlockFetcher]
       Time spent in:
           refillBuffer                   : 0.0348133 s
           time spent waiting for futures : 0.0375034 s
    [BlockFetcher::~BlockFetcher]
       Time spent in:
           refillBuffer                   : 0.0344998 s
           time spent waiting for futures : 0.0430423 s
*/
