
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
            const auto value = bitReader.template peek<nBits>();
            if ( !value ) {
                break;
            }
            sum += *value;
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


/*= MSB (bzip2) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 226.4 +- 0.3, max: 226.853 ) MB/s
[           BitReader read] Decoded with ( 60.52 +- 0.19, max: 60.7146 ) MB/s
[  BitReader template read] Decoded with ( 61.73 +- 0.22, max: 62.0117 ) MB/s
[  BitReader template peek] Decoded with ( 133.5 +- 0.25, max: 133.724 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 213.4 +- 1.4, max: 214.892 ) MB/s
[           BitReader read] Decoded with ( 60.07 +- 0.29, max: 60.403 ) MB/s
[  BitReader template read] Decoded with ( 60.4 +- 0.4, max: 60.6949 ) MB/s
[  BitReader template peek] Decoded with ( 139.8 +- 2.4, max: 142.989 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 428.9 +- 0.8, max: 429.891 ) MB/s
[           BitReader read] Decoded with ( 117.5 +- 1.3, max: 119.216 ) MB/s
[  BitReader template read] Decoded with ( 117.6 +- 0.6, max: 118.211 ) MB/s
[  BitReader template peek] Decoded with ( 184 +- 8, max: 191.586 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 362 +- 8, max: 368.305 ) MB/s
[           BitReader read] Decoded with ( 115.6 +- 0.5, max: 116.17 ) MB/s
[  BitReader template read] Decoded with ( 261.3 +- 0.7, max: 262.337 ) MB/s
[  BitReader template peek] Decoded with ( 207.4 +- 2.7, max: 210.264 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 980 +- 40, max: 1011.63 ) MB/s
[           BitReader read] Decoded with ( 359.8 +- 0.7, max: 360.781 ) MB/s
[  BitReader template read] Decoded with ( 400 +- 4, max: 402.503 ) MB/s
[  BitReader template peek] Decoded with ( 300.9 +- 2.6, max: 303.225 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 922 +- 6, max: 929.927 ) MB/s
[           BitReader read] Decoded with ( 365 +- 3, max: 369.626 ) MB/s
[  BitReader template read] Decoded with ( 464.01 +- 0.08, max: 464.051 ) MB/s
[  BitReader template peek] Decoded with ( 328 +- 3, max: 330.4 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 785 +- 10, max: 794.96 ) MB/s
[           BitReader read] Decoded with ( 420.5 +- 2.8, max: 422.456 ) MB/s
[  BitReader template read] Decoded with ( 425.6 +- 0.9, max: 426.632 ) MB/s
[  BitReader template peek] Decoded with ( 239.8 +- 1.4, max: 241.64 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1175 +- 6, max: 1182.77 ) MB/s
[           BitReader read] Decoded with ( 463 +- 5, max: 470.528 ) MB/s
[  BitReader template read] Decoded with ( 477.2 +- 0.4, max: 477.651 ) MB/s
[  BitReader template peek] Decoded with ( 324.6 +- 0.3, max: 324.835 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1006 +- 13, max: 1021.23 ) MB/s
[           BitReader read] Decoded with ( 425.4 +- 0.8, max: 426.089 ) MB/s
[  BitReader template read] Decoded with ( 425.31 +- 0.2, max: 425.548 ) MB/s
[  BitReader template peek] Decoded with ( 320.28 +- 0.17, max: 320.422 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1177 +- 10, max: 1186.15 ) MB/s
[           BitReader read] Decoded with ( 488.1 +- 0.8, max: 489.083 ) MB/s
[  BitReader template read] Decoded with ( 489.3 +- 1.1, max: 490.429 ) MB/s
[  BitReader template peek] Decoded with ( 343.9 +- 0.5, max: 344.36 ) MB/s

= LSB (gzip) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 247.6 +- 0.3, max: 247.95 ) MB/s
[           BitReader read] Decoded with ( 58.6 +- 0.3, max: 58.8261 ) MB/s
[  BitReader template read] Decoded with ( 151.18 +- 0.23, max: 151.386 ) MB/s
[  BitReader template peek] Decoded with ( 141.54 +- 0.25, max: 141.73 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 248.89 +- 0.21, max: 249.051 ) MB/s
[           BitReader read] Decoded with ( 58.64 +- 0.18, max: 58.8766 ) MB/s
[  BitReader template read] Decoded with ( 57.56 +- 0.27, max: 57.9159 ) MB/s
[  BitReader template peek] Decoded with ( 131.2 +- 0.4, max: 131.789 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 442 +- 30, max: 460.125 ) MB/s
[           BitReader read] Decoded with ( 115.4 +- 0.28, max: 115.81 ) MB/s
[  BitReader template read] Decoded with ( 116.1 +- 1.9, max: 117.719 ) MB/s
[  BitReader template peek] Decoded with ( 237.2 +- 2.1, max: 239.573 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 454 +- 0.9, max: 454.574 ) MB/s
[           BitReader read] Decoded with ( 112.8 +- 0.8, max: 113.872 ) MB/s
[  BitReader template read] Decoded with ( 273.8 +- 0.8, max: 274.767 ) MB/s
[  BitReader template peek] Decoded with ( 189.9 +- 0.6, max: 190.633 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1197.3 +- 1, max: 1198.29 ) MB/s
[           BitReader read] Decoded with ( 358.8 +- 0.4, max: 359.403 ) MB/s
[  BitReader template read] Decoded with ( 353.3 +- 0.6, max: 353.869 ) MB/s
[  BitReader template peek] Decoded with ( 489.4 +- 1.3, max: 490.429 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1141.1 +- 1.5, max: 1142.53 ) MB/s
[           BitReader read] Decoded with ( 347.13 +- 0.11, max: 347.233 ) MB/s
[  BitReader template read] Decoded with ( 359 +- 5, max: 365.29 ) MB/s
[  BitReader template peek] Decoded with ( 322.9 +- 1.1, max: 323.933 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 985.1 +- 1.2, max: 986.282 ) MB/s
[           BitReader read] Decoded with ( 405.53 +- 0.27, max: 405.721 ) MB/s
[  BitReader template read] Decoded with ( 403.5 +- 0.7, max: 404.449 ) MB/s
[  BitReader template peek] Decoded with ( 488 +- 8, max: 499.203 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1394.7 +- 1.7, max: 1397.2 ) MB/s
[           BitReader read] Decoded with ( 488.4 +- 2.6, max: 491.986 ) MB/s
[  BitReader template read] Decoded with ( 482.9 +- 0.5, max: 483.42 ) MB/s
[  BitReader template peek] Decoded with ( 319.36 +- 0.14, max: 319.528 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1612 +- 4, max: 1616.49 ) MB/s
[           BitReader read] Decoded with ( 401.9 +- 2.6, max: 405.378 ) MB/s
[  BitReader template read] Decoded with ( 406.9 +- 0.3, max: 407.371 ) MB/s
[  BitReader template peek] Decoded with ( 588.1 +- 2.8, max: 590.622 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1561.6 +- 1.1, max: 1562.56 ) MB/s
[           BitReader read] Decoded with ( 546.4 +- 2.1, max: 547.582 ) MB/s
[  BitReader template read] Decoded with ( 534.5 +- 0.8, max: 535.645 ) MB/s
[  BitReader template peek] Decoded with ( 331.2 +- 1, max: 332.345 ) MB/s

    -> Atrocious performance compared to the simple loop! And this case is frequent because the
       cached Huffman decoders peek CACHED_BIT_COUNT == 15!


= MSB (bzip2) =
== Simple bit reading loop ==
64-bit is faster 2, slower 3, and approximately equal 0 out of 5 times.
== BitReader read ==
64-bit is faster 3, slower 1, and approximately equal 1 out of 5 times.
== BitReader template read ==
64-bit is faster 4, slower 1, and approximately equal 0 out of 5 times.
== BitReader template peek ==
64-bit is faster 5, slower 0, and approximately equal 0 out of 5 times.

= LSB (gzip) =
== Simple bit reading loop ==
64-bit is faster 2, slower 2, and approximately equal 1 out of 5 times.
== BitReader read ==
64-bit is faster 2, slower 2, and approximately equal 1 out of 5 times.
== BitReader template read ==
64-bit is faster 4, slower 1, and approximately equal 0 out of 5 times.
== BitReader template peek ==
64-bit is faster 0, slower 5, and approximately equal 0 out of 5 times.

    -> These results don't seem very stable over different benchmark runs but there are always
       multiple instances where the 64-bit buffer slows things down?!
*/
