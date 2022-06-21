
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <Statistics.hpp>


template<bool MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReader( const std::vector<char>& data,
                    const uint8_t            nBits )
{
    BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer> bitReader( std::make_unique<BufferedFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        while ( true ) {
            sum += bitReader.read( nBits );
        }
    } catch ( const std::exception& ) {
        /* Ignore EOF exception. Checking for it in each loop is expensive! */
    }

    return { duration( t0 ), sum };
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer,
         uint8_t  nBits>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReaderTemplatedReadBits( const std::vector<char>& data )
{
    BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer> bitReader( std::make_unique<BufferedFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        while ( true ) {
            sum += bitReader.template read<nBits>();
        }
    } catch ( const std::exception& ) {
        /* Ignore EOF exception. Checking for it in each loop is expensive! */
    }

    return { duration( t0 ), sum };
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReaderTemplatedRead( const std::vector<char>& data,
                                 const uint8_t            nBits )
{
    switch ( nBits )
    {
        case  1: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  1>( data );
        case  2: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  2>( data );
        case  3: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  3>( data );
        case  4: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  4>( data );
        case  5: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  5>( data );
        case  6: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  6>( data );
        case  7: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  7>( data );
        case  8: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  8>( data );
        case  9: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  9>( data );
        case 10: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 10>( data );
        case 11: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 11>( data );
        case 12: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 12>( data );
        case 13: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 13>( data );
        case 14: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 14>( data );
        case 15: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 15>( data );
        case 16: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 16>( data );
        default: break;
    }
    return { 0, 0 };
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer,
         uint8_t  nBits>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReaderTemplatedPeekBits( const std::vector<char>& data )
{
    BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer> bitReader( std::make_unique<BufferedFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        while ( true ) {
            sum += bitReader.template peek<nBits>();
            bitReader.seekAfterPeek( nBits );
        }
    } catch ( const std::exception& ) {
        /* Ignore EOF exception. Checking for it in each loop is expensive! */
    }

    return { duration( t0 ), sum };
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReaderTemplatedPeek( const std::vector<char>& data,
                                 const uint8_t            nBits )
{
    switch ( nBits )
    {
        case  1: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  1>( data );
        case  2: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  2>( data );
        case  3: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  3>( data );
        case  4: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  4>( data );
        case  5: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  5>( data );
        case  6: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  6>( data );
        case  7: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  7>( data );
        case  8: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  8>( data );
        case  9: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,  9>( data );
        case 10: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 10>( data );
        case 11: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 11>( data );
        case 12: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 12>( data );
        case 13: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 13>( data );
        case 14: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 14>( data );
        case 15: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 15>( data );
        case 16: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 16>( data );
        default: break;
    }
    return { 0, 0 };
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReading( const std::vector<char>& data,
                     const uint8_t            nBits )
{
    const auto t0 = now();

    static constexpr auto MAX_BIT_BUFFER_SIZE = std::numeric_limits<BitBuffer>::digits;

    BitBuffer bitBuffer{ 0 };
    uint32_t bitBufferSize{ 0 };
    uint64_t sum = 0;

    if ( nBits == 0 ) {
        throw std::invalid_argument( "Must read more than zero bits!" );
    }
    assert( nBits <= sizeof( bitBuffer ) * CHAR_BIT );
    constexpr auto BIT_BUFFER_CAPACITY = sizeof( bitBuffer ) * CHAR_BIT;

    size_t i = 0;
    while ( i < data.size() ) {
        /* Fill bit buffer. */
        if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
            bitBuffer &= nLowestBitsSet<decltype( bitBuffer )>( bitBufferSize );
        } else {
            bitBuffer &= nHighestBitsSet<decltype( bitBuffer )>( bitBufferSize );
        }

        if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST ) {
            if ( bitBufferSize > 0 ) {
                bitBuffer >>= MAX_BIT_BUFFER_SIZE - bitBufferSize;
            }
        }

        while ( ( bitBufferSize + CHAR_BIT <= BIT_BUFFER_CAPACITY ) && ( i < data.size() ) ) {
            if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
                bitBuffer <<= static_cast<unsigned>( CHAR_BIT );
                bitBuffer |= static_cast<uint8_t>( data[i] );
            } else {
                bitBuffer |= ( static_cast<BitBuffer>( static_cast<uint8_t>( data[i] ) ) << bitBufferSize );
            }
            bitBufferSize += CHAR_BIT;
            ++i;
        }

        /* Move LSB bits (which are filled left-to-right) to the left if so necessary
         * so that the format is the same as for MSB bits! */
        if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST ) {
            if ( bitBufferSize > 0 ) {
                bitBuffer <<= MAX_BIT_BUFFER_SIZE - bitBufferSize;
            }
        }

        /* Use up bit buffer. */
        while ( bitBufferSize >= nBits ) {
            BitBuffer result;
            if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
                result = ( bitBuffer >> ( bitBufferSize - nBits ) ) & nLowestBitsSet<BitBuffer>( nBits );
            } else {
                result = ( bitBuffer >> ( MAX_BIT_BUFFER_SIZE - bitBufferSize ) ) & nLowestBitsSet<BitBuffer>( nBits );
            }
            bitBufferSize -= nBits;

            /* Caller doing something with the requested bits. */
            sum += result;
        }
    }

    return { duration( t0 ), sum };
}


enum class BenchmarkType
{
    SIMPLE_LOOP,
    BIT_READER_READ,
    BIT_READER_TEMPLATE_READ,
    BIT_READER_TEMPLATE_PEEK,
};


[[nodiscard]] std::string
toString( BenchmarkType benchmarkType )
{
    switch ( benchmarkType )
    {
    case BenchmarkType::SIMPLE_LOOP:
        return "Simple bit reading loop";
    case BenchmarkType::BIT_READER_READ:
        return "BitReader read";
    case BenchmarkType::BIT_READER_TEMPLATE_READ:
        return "BitReader template read";
    case BenchmarkType::BIT_READER_TEMPLATE_PEEK:
        return "BitReader template peek";
    }
    return "";
}


using AllResults = std::map<
    std::tuple<
        BenchmarkType,
        /* MOST_SIGNIFICANT_BITS_FIRST */ bool,
        /* bit buffer length in bits */ uint8_t,
        /* bits being read on each call */ uint8_t
    >,
    Statistics<double>
>;


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
[[nodiscard]] AllResults
benchmarkBitReaders( const std::vector<char>& data,
                     const uint8_t            nBits )
{
    static constexpr int LABEL_WIDTH = 25;

    const auto formatBandwidth =
        [&] ( const std::vector<double>& times )
        {
            std::vector<double> bandwidths( times.size() );
            std::transform( times.begin(), times.end(), bandwidths.begin(),
                            [size = data.size()] ( double time ) { return size / time / 1e6; } );
            Statistics<double> bandwidthStats{ bandwidths };

            /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
             * physically possible but many noisy influences can slow things down, i.e., the minimum time is
             * the value closest to be free of noise. */
            std::stringstream result;
            result << "( " + bandwidthStats.formatAverageWithUncertainty()
                   << ", max: " << bandwidthStats.max << " ) MB/s";
            return result.str();
        };

    AllResults results;
    std::optional<uint64_t> checksum;

    const auto measureTimes =
        [&] ( const BenchmarkType benchmarkType,
              const auto&         toMeasure )
    {
        std::vector<double> times( 6 );
        for ( auto& time : times ) {
            const auto [measuredTime, calculatedChecksum] = toMeasure();
            time = measuredTime;

            if ( !checksum ) {
                checksum = calculatedChecksum;
            } else if ( *checksum != calculatedChecksum ) {
                throw std::runtime_error( "Indeterministic or wrong result observed!" );
            }
        }

        /* Remove two (arbitrary) outliers. */
        times.erase( std::min_element( times.begin(), times.end() ) );
        times.erase( std::max_element( times.begin(), times.end() ) );

        results.emplace(
            std::make_tuple(
                benchmarkType,
                MOST_SIGNIFICANT_BITS_FIRST,
                static_cast<uint8_t>( sizeof( BitBuffer ) * CHAR_BIT ),
                nBits
            ),
            Statistics<double>( times )
        );

        std::cerr << "[" << std::setw( LABEL_WIDTH ) << toString( benchmarkType ) << "] ";
        std::cerr << "Decoded with " << formatBandwidth( times ) << "\n";
    };

    measureTimes( BenchmarkType::SIMPLE_LOOP, [&] () {
        return benchmarkBitReading<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>( data, nBits );
    } );
    measureTimes( BenchmarkType::BIT_READER_READ, [&] () {
        return benchmarkBitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>( data, nBits );
    } );
    measureTimes( BenchmarkType::BIT_READER_TEMPLATE_READ, [&] () {
        return benchmarkBitReaderTemplatedRead<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>( data, nBits );
    } );
    measureTimes( BenchmarkType::BIT_READER_TEMPLATE_PEEK, [&] () {
        return benchmarkBitReaderTemplatedPeek<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>( data, nBits );
    } );

    return results;
}


int
main()
{
    const std::vector<uint8_t> nBitsToTest = { 1, 2, 8, 15, 16 };

    AllResults results;

    try {
        std::vector<char> dataToRead( 16ULL * 1024ULL * 1024ULL );
        for ( auto& x : dataToRead ) {
            x = rand();
        }

        std::cerr << "= MSB (bzip2) =\n";

        for ( const auto nBits : nBitsToTest ) {
            std::cerr << "\n== Benchmarking by reading " << static_cast<int>( nBits ) << " bits ==\n";

            std::cerr << "\n=== 32-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<true, uint32_t>( dataToRead, nBits ) );
            std::cerr << "\n=== 64-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<true, uint64_t>( dataToRead, nBits ) );
        }

        std::cerr << "\n= LSB (gzip) =\n";

        for ( const auto nBits : nBitsToTest ) {
            std::cerr << "\n== Benchmarking by reading " << static_cast<int>( nBits ) << " bits ==\n";

            std::cerr << "\n=== 32-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<false, uint32_t>( dataToRead, nBits ) );
            std::cerr << "\n=== 64-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<false, uint64_t>( dataToRead, nBits ) );
        }
    } catch ( const std::exception& exception ) {
        std::cerr << "Caught exception " << exception.what() << "\n";
    }

    const std::vector<BenchmarkType> allBenchmarktypes = {
        BenchmarkType::SIMPLE_LOOP,
        BenchmarkType::BIT_READER_READ,
        BenchmarkType::BIT_READER_TEMPLATE_READ,
        BenchmarkType::BIT_READER_TEMPLATE_PEEK,
    };

    /* Analyze whether 32-bit or 64-bit buffer is faster. */
    std::cerr << "\n";
    for ( const auto msb : { true, false } ) {
        std::cerr << "\n= " << ( msb ? "MSB (bzip2)" : "LSB (gzip)" ) << " =\n";
        for ( const auto benchmarkType : allBenchmarktypes ) {
            std::cout << "== " << toString( benchmarkType ) << " ==\n";

            uint32_t faster64{ 0 };
            uint32_t slower64{ 0 };
            uint32_t similar64{ 0 };
            for ( const auto nBits : nBitsToTest ) {
                const auto key32 = std::make_tuple( benchmarkType, msb, 32, nBits );
                const auto key64 = std::make_tuple( benchmarkType, msb, 64, nBits );
                const auto match32 = results.find( key32 );
                const auto match64 = results.find( key64 );
                if ( ( match32 == results.end() ) || ( match64 == results.end() ) ) {
                    continue;
                }

                /* The map value contain time statistics for which smaller is better (faster). */
                if ( match64->second.max < match32->second.min ) {
                    faster64++;
                } else if ( match64->second.min > match32->second.max ) {
                    slower64++;
                } else {
                    similar64++;
                }
            }

            std::cout << "64-bit is faster " << faster64 << ", slower " << slower64 << ", and approximately equal "
                      << similar64 << " out of " << ( faster64 + slower64 + similar64 ) << " times.\n";
        }
    }

    return 0;
}


/*
= MSB (bzip2) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 230.1 +- 2.7, max: 232.073 ) MB/s
[           BitReader read] Decoded with ( 167.9 +- 1.6, max: 170.2 ) MB/s
[  BitReader template read] Decoded with ( 254.5 +- 0.4, max: 254.888 ) MB/s
[  BitReader template peek] Decoded with ( 221.9 +- 1.5, max: 223.728 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 230.7 +- 1.5, max: 231.746 ) MB/s
[           BitReader read] Decoded with ( 150.2 +- 0.5, max: 150.706 ) MB/s
[  BitReader template read] Decoded with ( 240.7 +- 1.7, max: 242.482 ) MB/s
[  BitReader template peek] Decoded with ( 208.4 +- 2.1, max: 210.639 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 429.1 +- 0.5, max: 429.573 ) MB/s
[           BitReader read] Decoded with ( 311.3 +- 0.9, max: 312.134 ) MB/s
[  BitReader template read] Decoded with ( 372.4 +- 2.9, max: 376.526 ) MB/s
[  BitReader template peek] Decoded with ( 399.7 +- 0.5, max: 400.529 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 416.6 +- 2.9, max: 419.988 ) MB/s
[           BitReader read] Decoded with ( 277 +- 4, max: 281.569 ) MB/s
[  BitReader template read] Decoded with ( 376 +- 4, max: 381.526 ) MB/s
[  BitReader template peek] Decoded with ( 392.6 +- 0.2, max: 392.882 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 990 +- 90, max: 1066.78 ) MB/s
[           BitReader read] Decoded with ( 830 +- 4, max: 834.197 ) MB/s
[  BitReader template read] Decoded with ( 1037 +- 10, max: 1046.63 ) MB/s
[  BitReader template peek] Decoded with ( 1063 +- 5, max: 1070 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 892 +- 5, max: 897.132 ) MB/s
[           BitReader read] Decoded with ( 820.1 +- 2, max: 822.338 ) MB/s
[  BitReader template read] Decoded with ( 1088 +- 4, max: 1091.71 ) MB/s
[  BitReader template peek] Decoded with ( 1090 +- 9, max: 1096.57 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 929 +- 28, max: 949.778 ) MB/s
[           BitReader read] Decoded with ( 1009 +- 7, max: 1014.65 ) MB/s
[  BitReader template read] Decoded with ( 1248 +- 8, max: 1255.21 ) MB/s
[  BitReader template peek] Decoded with ( 950 +- 5, max: 957.703 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 979 +- 5, max: 982.57 ) MB/s
[           BitReader read] Decoded with ( 1087 +- 3, max: 1090.67 ) MB/s
[  BitReader template read] Decoded with ( 1362 +- 11, max: 1377.61 ) MB/s
[  BitReader template peek] Decoded with ( 1303 +- 19, max: 1322.43 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1170 +- 80, max: 1279.44 ) MB/s
[           BitReader read] Decoded with ( 1120 +- 80, max: 1167.36 ) MB/s
[  BitReader template read] Decoded with ( 1310 +- 100, max: 1366.1 ) MB/s
[  BitReader template peek] Decoded with ( 1360 +- 100, max: 1414.47 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1020 +- 40, max: 1043.82 ) MB/s
[           BitReader read] Decoded with ( 1192 +- 7, max: 1198.86 ) MB/s
[  BitReader template read] Decoded with ( 1455 +- 18, max: 1474.78 ) MB/s
[  BitReader template peek] Decoded with ( 1429 +- 17, max: 1455.03 ) MB/s

= LSB (gzip) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 262.8 +- 0.6, max: 263.223 ) MB/s
[           BitReader read] Decoded with ( 155.5 +- 2.2, max: 158.327 ) MB/s
[  BitReader template read] Decoded with ( 218.3 +- 1.4, max: 219.231 ) MB/s
[  BitReader template peek] Decoded with ( 226 +- 1.5, max: 227.281 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 256 +- 3, max: 259.625 ) MB/s
[           BitReader read] Decoded with ( 153.5 +- 1.2, max: 154.163 ) MB/s
[  BitReader template read] Decoded with ( 210.5 +- 0.8, max: 211.311 ) MB/s
[  BitReader template peek] Decoded with ( 225.4 +- 1.9, max: 227.542 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 475 +- 4, max: 478.4 ) MB/s
[           BitReader read] Decoded with ( 290 +- 1.2, max: 290.765 ) MB/s
[  BitReader template read] Decoded with ( 362 +- 3, max: 366.267 ) MB/s
[  BitReader template peek] Decoded with ( 250.8 +- 0.8, max: 251.849 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 464.1 +- 2.2, max: 465.842 ) MB/s
[           BitReader read] Decoded with ( 288.8 +- 1.8, max: 291.104 ) MB/s
[  BitReader template read] Decoded with ( 352.3 +- 2.6, max: 354.538 ) MB/s
[  BitReader template peek] Decoded with ( 361.4 +- 0.6, max: 362.289 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1232 +- 9, max: 1238.25 ) MB/s
[           BitReader read] Decoded with ( 768 +- 4, max: 770.705 ) MB/s
[  BitReader template read] Decoded with ( 1030 +- 14, max: 1042.51 ) MB/s
[  BitReader template peek] Decoded with ( 964 +- 10, max: 972.195 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1134 +- 11, max: 1143.01 ) MB/s
[           BitReader read] Decoded with ( 831 +- 9, max: 837.487 ) MB/s
[  BitReader template read] Decoded with ( 1072 +- 4, max: 1076.04 ) MB/s
[  BitReader template peek] Decoded with ( 910 +- 5, max: 915.332 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 989 +- 3, max: 991.775 ) MB/s
[           BitReader read] Decoded with ( 1073 +- 5, max: 1077.67 ) MB/s
[  BitReader template read] Decoded with ( 1305 +- 5, max: 1309.69 ) MB/s
[  BitReader template peek] Decoded with ( 968.3 +- 2, max: 970.302 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1440 +- 8, max: 1446.74 ) MB/s
[           BitReader read] Decoded with ( 1192 +- 8, max: 1200.35 ) MB/s
[  BitReader template read] Decoded with ( 1402 +- 14, max: 1415.49 ) MB/s
[  BitReader template peek] Decoded with ( 1201 +- 12, max: 1211.54 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1473 +- 5, max: 1477.78 ) MB/s
[           BitReader read] Decoded with ( 1091 +- 4, max: 1095.11 ) MB/s
[  BitReader template read] Decoded with ( 1378 +- 3, max: 1381.31 ) MB/s
[  BitReader template peek] Decoded with ( 1224 +- 3, max: 1227.96 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1610.6 +- 2.7, max: 1613.77 ) MB/s
[           BitReader read] Decoded with ( 1270 +- 5, max: 1274.75 ) MB/s
[  BitReader template read] Decoded with ( 1516 +- 8, max: 1525 ) MB/s
[  BitReader template peek] Decoded with ( 1317 +- 11, max: 1327.02 ) MB/s


= MSB (bzip2) =
== Simple bit reading loop ==
64-bit is faster 1, slower 2, and approximately equal 2 out of 5 times.
== BitReader read ==
64-bit is faster 2, slower 3, and approximately equal 0 out of 5 times.
== BitReader template read ==
64-bit is faster 3, slower 1, and approximately equal 1 out of 5 times.
== BitReader template peek ==
64-bit is faster 3, slower 2, and approximately equal 0 out of 5 times.

= LSB (gzip) =
== Simple bit reading loop ==
64-bit is faster 2, slower 3, and approximately equal 0 out of 5 times.
== BitReader read ==
64-bit is faster 3, slower 0, and approximately equal 2 out of 5 times.
== BitReader template read ==
64-bit is faster 3, slower 2, and approximately equal 0 out of 5 times.
== BitReader template peek ==
64-bit is faster 3, slower 1, and approximately equal 1 out of 5 times.

    -> These results don't seem very stable over different benchmark runs but there are always
       multiple instances where the 64-bit buffer slows things down?!
*/
