
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
                            [size = data.size()] ( double time ) { return static_cast<double>( size ) / time / 1e6; } );
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
            x = static_cast<char>( rand() );
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
[  Simple bit reading loop] Decoded with ( 230.1 +- 2.9, max: 231.861 ) MB/s
[           BitReader read] Decoded with ( 162.1 +- 2, max: 163.732 ) MB/s
[  BitReader template read] Decoded with ( 195 +- 9, max: 206.677 ) MB/s
[  BitReader template peek] Decoded with ( 217 +- 10, max: 225.264 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 227 +- 0.7, max: 227.758 ) MB/s
[           BitReader read] Decoded with ( 153 +- 1.8, max: 154.097 ) MB/s
[  BitReader template read] Decoded with ( 188 +- 5, max: 190.886 ) MB/s
[  BitReader template peek] Decoded with ( 219.7 +- 2, max: 222.18 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 415.2 +- 2.4, max: 418.042 ) MB/s
[           BitReader read] Decoded with ( 280 +- 0.5, max: 280.418 ) MB/s
[  BitReader template read] Decoded with ( 341 +- 1.4, max: 342.356 ) MB/s
[  BitReader template peek] Decoded with ( 324 +- 4, max: 327.874 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 407.6 +- 0.5, max: 408.046 ) MB/s
[           BitReader read] Decoded with ( 273.6 +- 2.5, max: 275.975 ) MB/s
[  BitReader template read] Decoded with ( 273.5 +- 1.9, max: 276.256 ) MB/s
[  BitReader template peek] Decoded with ( 348.9 +- 1.3, max: 349.805 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1050 +- 100, max: 1101.68 ) MB/s
[           BitReader read] Decoded with ( 520 +- 70, max: 604.628 ) MB/s
[  BitReader template read] Decoded with ( 673 +- 27, max: 689.042 ) MB/s
[  BitReader template peek] Decoded with ( 604 +- 5, max: 607.177 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 980 +- 90, max: 1034.65 ) MB/s
[           BitReader read] Decoded with ( 669 +- 4, max: 672.538 ) MB/s
[  BitReader template read] Decoded with ( 693 +- 8, max: 701.251 ) MB/s
[  BitReader template peek] Decoded with ( 625 +- 15, max: 636.502 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 890 +- 60, max: 921.511 ) MB/s
[           BitReader read] Decoded with ( 694.2 +- 1.3, max: 695.381 ) MB/s
[  BitReader template read] Decoded with ( 776 +- 6, max: 782.315 ) MB/s
[  BitReader template peek] Decoded with ( 594 +- 16, max: 611.964 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1062 +- 12, max: 1069.96 ) MB/s
[           BitReader read] Decoded with ( 756 +- 8, max: 766.969 ) MB/s
[  BitReader template read] Decoded with ( 792.9 +- 2, max: 795.802 ) MB/s
[  BitReader template peek] Decoded with ( 676 +- 5, max: 680.475 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1239 +- 4, max: 1243.71 ) MB/s
[           BitReader read] Decoded with ( 758.8 +- 1.9, max: 760.537 ) MB/s
[  BitReader template read] Decoded with ( 768 +- 7, max: 777.922 ) MB/s
[  BitReader template peek] Decoded with ( 657 +- 4, max: 660.942 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1150 +- 40, max: 1192.12 ) MB/s
[           BitReader read] Decoded with ( 829 +- 5, max: 832.061 ) MB/s
[  BitReader template read] Decoded with ( 828.5 +- 1.6, max: 830.172 ) MB/s
[  BitReader template peek] Decoded with ( 688 +- 7, max: 692.378 ) MB/s

= LSB (gzip) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 241.6 +- 2.2, max: 243.662 ) MB/s
[           BitReader read] Decoded with ( 190.8 +- 0.7, max: 191.297 ) MB/s
[  BitReader template read] Decoded with ( 175 +- 3, max: 178.7 ) MB/s
[  BitReader template peek] Decoded with ( 182.2 +- 1.3, max: 183.099 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 216.3 +- 0.6, max: 216.802 ) MB/s
[           BitReader read] Decoded with ( 212.5 +- 2.2, max: 214.712 ) MB/s
[  BitReader template read] Decoded with ( 224.5 +- 2.2, max: 226.785 ) MB/s
[  BitReader template peek] Decoded with ( 191.9 +- 2.1, max: 194.702 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 388 +- 3, max: 390.164 ) MB/s
[           BitReader read] Decoded with ( 326.3 +- 0.6, max: 327.178 ) MB/s
[  BitReader template read] Decoded with ( 290.1 +- 0.9, max: 290.789 ) MB/s
[  BitReader template peek] Decoded with ( 279.3 +- 2.6, max: 281.663 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 386 +- 0.9, max: 386.937 ) MB/s
[           BitReader read] Decoded with ( 384.8 +- 0.3, max: 385.251 ) MB/s
[  BitReader template read] Decoded with ( 326.3 +- 2.6, max: 328.408 ) MB/s
[  BitReader template peek] Decoded with ( 326.51 +- 0.21, max: 326.768 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 893 +- 3, max: 895.86 ) MB/s
[           BitReader read] Decoded with ( 680 +- 3, max: 683.871 ) MB/s
[  BitReader template read] Decoded with ( 621.6 +- 1.3, max: 623.266 ) MB/s
[  BitReader template peek] Decoded with ( 604 +- 21, max: 618.594 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 810 +- 50, max: 836.893 ) MB/s
[           BitReader read] Decoded with ( 810 +- 60, max: 844.787 ) MB/s
[  BitReader template read] Decoded with ( 800 +- 60, max: 832.863 ) MB/s
[  BitReader template peek] Decoded with ( 714 +- 22, max: 726.076 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 740 +- 40, max: 764.192 ) MB/s
[           BitReader read] Decoded with ( 740 +- 40, max: 760.927 ) MB/s
[  BitReader template read] Decoded with ( 714 +- 28, max: 732.89 ) MB/s
[  BitReader template peek] Decoded with ( 625 +- 9, max: 634.09 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 981.5 +- 2.4, max: 983.68 ) MB/s
[           BitReader read] Decoded with ( 983 +- 6, max: 989.062 ) MB/s
[  BitReader template read] Decoded with ( 1026 +- 4, max: 1029.28 ) MB/s
[  BitReader template peek] Decoded with ( 779 +- 4, max: 782.971 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1050 +- 10, max: 1058.53 ) MB/s
[           BitReader read] Decoded with ( 847.8 +- 2.2, max: 849.808 ) MB/s
[  BitReader template read] Decoded with ( 748 +- 4, max: 753.922 ) MB/s
[  BitReader template peek] Decoded with ( 728 +- 5, max: 734.149 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1069 +- 12, max: 1084.89 ) MB/s
[           BitReader read] Decoded with ( 1025 +- 4, max: 1029.37 ) MB/s
[  BitReader template read] Decoded with ( 1031 +- 5, max: 1035.14 ) MB/s
[  BitReader template peek] Decoded with ( 804 +- 4, max: 808.422 ) MB/s


= MSB (bzip2) =
== Simple bit reading loop ==
64-bit is faster 1, slower 2, and approximately equal 2 out of 5 times.
== BitReader read ==
64-bit is faster 3, slower 2, and approximately equal 0 out of 5 times.
== BitReader template read ==
64-bit is faster 2, slower 1, and approximately equal 2 out of 5 times.
== BitReader template peek ==
64-bit is faster 3, slower 0, and approximately equal 2 out of 5 times.

= LSB (gzip) =
== Simple bit reading loop ==
64-bit is faster 1, slower 2, and approximately equal 2 out of 5 times.
== BitReader read ==
64-bit is faster 5, slower 0, and approximately equal 0 out of 5 times.
== BitReader template read ==
64-bit is faster 5, slower 0, and approximately equal 0 out of 5 times.
== BitReader template peek ==
64-bit is faster 5, slower 0, and approximately equal 0 out of 5 times.

    -> These results don't seem very stable over different benchmark runs but there are always
       multiple instances where the 64-bit buffer slows things down?!
*/
