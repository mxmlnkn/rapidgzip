#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <common.hpp>
#include <filereader/BufferView.hpp>
#include <Statistics.hpp>


using namespace rapidgzip;


template<bool MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReader( const std::vector<char>& data,
                    const uint8_t            nBits )
{
    using CustomBitReader = BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>;
    CustomBitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        while ( true ) {
            sum += bitReader.read( nBits );
        }
    } catch ( const typename CustomBitReader::EndOfFileReached& ) {
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
    using CustomBitReader = BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>;
    CustomBitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        while ( true ) {
            sum += bitReader.template read<nBits>();
        }
    } catch ( const typename CustomBitReader::EndOfFileReached& ) {
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
    // *INDENT-OFF*
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
    case 17: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 17>( data );
    case 18: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 18>( data );
    case 19: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 19>( data );
    case 20: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 20>( data );
    case 21: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 21>( data );
    case 22: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 22>( data );
    case 23: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 23>( data );
    case 24: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 24>( data );
    case 25: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 25>( data );
    case 26: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 26>( data );
    case 27: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 27>( data );
    case 28: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 28>( data );
    case 29: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 29>( data );
    case 30: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 30>( data );
    case 31: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 31>( data );
    default: break;
    }

    if constexpr ( sizeof( BitBuffer ) >= 64 ) {
        switch ( nBits )
        {
        /* Reading the full buffer not allowed because it complicates things and would require another branch! */
        case 32: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 32>( data );
        case 33: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 33>( data );
        case 34: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 34>( data );
        case 35: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 35>( data );
        case 36: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 36>( data );
        case 37: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 37>( data );
        case 38: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 38>( data );
        case 39: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 39>( data );
        case 40: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 40>( data );
        case 41: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 41>( data );
        case 42: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 42>( data );
        case 43: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 43>( data );
        case 44: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 44>( data );
        case 45: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 45>( data );
        case 46: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 46>( data );
        case 47: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 47>( data );
        case 48: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 48>( data );
        case 49: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 49>( data );
        case 50: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 50>( data );
        case 51: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 51>( data );
        case 52: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 52>( data );
        case 53: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 53>( data );
        case 54: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 54>( data );
        case 55: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 55>( data );
        case 56: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 56>( data );
        case 57: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 57>( data );
        case 58: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 58>( data );
        case 59: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 59>( data );
        case 60: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 60>( data );
        case 61: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 61>( data );
        case 62: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 62>( data );
        case 63: return benchmarkBitReaderTemplatedReadBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 63>( data );
        default: break;
        }
    }
    // *INDENT-ON*

    throw std::logic_error( "Template redirction for specified bits not implemented!" );
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer,
         uint8_t  nBits>
[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReaderTemplatedPeekBits( const std::vector<char>& data )
{
    using CustomBitReader = BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>;
    CustomBitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        while ( true ) {
            sum += bitReader.template peek<nBits>();
            bitReader.seekAfterPeek( nBits );
        }
    } catch ( const typename CustomBitReader::EndOfFileReached& ) {
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
    // *INDENT-OFF*
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
    case 17: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 17>( data );
    case 18: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 18>( data );
    case 19: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 19>( data );
    case 20: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 20>( data );
    case 21: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 21>( data );
    case 22: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 22>( data );
    case 23: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 23>( data );
    case 24: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 24>( data );
    case 25: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 25>( data );
    case 26: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 26>( data );
    case 27: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 27>( data );
    case 28: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 28>( data );
    case 29: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 29>( data );
    case 30: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 30>( data );
    case 31: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 31>( data );
    case 32: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 32>( data );
    default: break;
    }

    if constexpr ( sizeof( BitBuffer ) >= 64 ) {
        switch ( nBits )
        {
        case 33: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 33>( data );
        case 34: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 34>( data );
        case 35: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 35>( data );
        case 36: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 36>( data );
        case 37: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 37>( data );
        case 38: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 38>( data );
        case 39: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 39>( data );
        case 40: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 40>( data );
        case 41: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 41>( data );
        case 42: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 42>( data );
        case 43: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 43>( data );
        case 44: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 44>( data );
        case 45: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 45>( data );
        case 46: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 46>( data );
        case 47: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 47>( data );
        case 48: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 48>( data );
        case 49: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 49>( data );
        case 50: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 50>( data );
        case 51: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 51>( data );
        case 52: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 52>( data );
        case 53: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 53>( data );
        case 54: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 54>( data );
        case 55: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 55>( data );
        case 56: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 56>( data );
        case 57: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 57>( data );
        case 58: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 58>( data );
        case 59: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 59>( data );
        case 60: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 60>( data );
        case 61: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 61>( data );
        case 62: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 62>( data );
        case 63: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 63>( data );
        case 64: return benchmarkBitReaderTemplatedPeekBits<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, 64>( data );
        default: break;
        }
    }
    // *INDENT-ON*

    throw std::logic_error( "Template redirction for specified bits not implemented!" );
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
                bitBuffer <<= static_cast<unsigned int>( CHAR_BIT );
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
            BitBuffer result{ 0 };
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

            std::cout << "[" << std::setw( LABEL_WIDTH ) << toString( benchmarkType ) << "] ";
            std::cout << "Decoded with " << formatBandwidth( times ) << "\n";
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
    /* This is nice for testing. Probably should add this to the tests or maybe run this benchmark also as a test? */
    //const std::vector<uint8_t> nBitsToTest = { 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    const std::vector<uint8_t> nBitsToTest = { 1, 2, 8, 15, 16 };

    AllResults results;

    try {
        std::vector<char> dataToRead( 16_Mi );
        for ( auto& x : dataToRead ) {
            x = static_cast<char>( rand() );
        }

        std::cout << "= MSB (bzip2) =\n";

        for ( const auto nBits : nBitsToTest ) {
            std::cout << "\n== Benchmarking by reading " << static_cast<int>( nBits ) << " bits ==\n";

            std::cout << "\n=== 32-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<true, uint32_t>( dataToRead, nBits ) );
            std::cout << "\n=== 64-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<true, uint64_t>( dataToRead, nBits ) );
        }

        std::cout << "\n= LSB (gzip) =\n";

        for ( const auto nBits : nBitsToTest ) {
            std::cout << "\n== Benchmarking by reading " << static_cast<int>( nBits ) << " bits ==\n";

            std::cout << "\n=== 32-bit Buffer ===\n";
            results.merge( benchmarkBitReaders<false, uint32_t>( dataToRead, nBits ) );
            std::cout << "\n=== 64-bit Buffer ===\n";
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
    std::cout << "\n";
    for ( const auto msb : { true, false } ) {
        std::cout << "\n= " << ( msb ? "MSB (bzip2)" : "LSB (gzip)" ) << " =\n";
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
cmake --build . -- benchmarkBitReader && taskset 0x08 src/benchmarks/benchmarkBitReader

= MSB (bzip2) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 226.9 +- 1.4, max: 228.776 ) MB/s
[           BitReader read] Decoded with ( 159.7 +- 0.3, max: 160.019 ) MB/s
[  BitReader template read] Decoded with ( 224.5 +- 1.6, max: 226.585 ) MB/s
[  BitReader template peek] Decoded with ( 214.07 +- 0.28, max: 214.298 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 223.5 +- 2, max: 225.452 ) MB/s
[           BitReader read] Decoded with ( 155.1 +- 0.8, max: 156.04 ) MB/s
[  BitReader template read] Decoded with ( 234.39 +- 0.26, max: 234.694 ) MB/s
[  BitReader template peek] Decoded with ( 234.2 +- 0.7, max: 234.773 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 426 +- 7, max: 431.208 ) MB/s
[           BitReader read] Decoded with ( 293 +- 3, max: 295.596 ) MB/s
[  BitReader template read] Decoded with ( 367 +- 3, max: 369.676 ) MB/s
[  BitReader template peek] Decoded with ( 346.5 +- 3, max: 349.144 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 404.4 +- 0.9, max: 405.502 ) MB/s
[           BitReader read] Decoded with ( 286.8 +- 0.9, max: 288.072 ) MB/s
[  BitReader template read] Decoded with ( 368.2 +- 0.9, max: 369.237 ) MB/s
[  BitReader template peek] Decoded with ( 337 +- 3, max: 340.36 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1000 +- 8, max: 1007.78 ) MB/s
[           BitReader read] Decoded with ( 809 +- 6, max: 814.311 ) MB/s
[  BitReader template read] Decoded with ( 990 +- 12, max: 1006.89 ) MB/s
[  BitReader template peek] Decoded with ( 766 +- 5, max: 771.006 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1077 +- 3, max: 1081.58 ) MB/s
[           BitReader read] Decoded with ( 782 +- 7, max: 789.948 ) MB/s
[  BitReader template read] Decoded with ( 976 +- 10, max: 987.075 ) MB/s
[  BitReader template peek] Decoded with ( 779 +- 5, max: 784.374 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 949 +- 10, max: 959.642 ) MB/s
[           BitReader read] Decoded with ( 985.1 +- 2.1, max: 987.022 ) MB/s
[  BitReader template read] Decoded with ( 1203 +- 7, max: 1213.54 ) MB/s
[  BitReader template peek] Decoded with ( 674 +- 6, max: 679.533 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1210 +- 50, max: 1242.45 ) MB/s
[           BitReader read] Decoded with ( 1033 +- 17, max: 1057.49 ) MB/s
[  BitReader template read] Decoded with ( 1294 +- 13, max: 1308.76 ) MB/s
[  BitReader template peek] Decoded with ( 834 +- 2.6, max: 837.383 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1127 +- 14, max: 1143.57 ) MB/s
[           BitReader read] Decoded with ( 1094 +- 7, max: 1103.46 ) MB/s
[  BitReader template read] Decoded with ( 1322 +- 11, max: 1336.08 ) MB/s
[  BitReader template peek] Decoded with ( 858 +- 6, max: 863.784 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1347 +- 10, max: 1359.38 ) MB/s
[           BitReader read] Decoded with ( 1138 +- 11, max: 1149.12 ) MB/s
[  BitReader template read] Decoded with ( 1407 +- 26, max: 1423.37 ) MB/s
[  BitReader template peek] Decoded with ( 977 +- 10, max: 987.16 ) MB/s

= LSB (gzip) =

== Benchmarking by reading 1 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 253.4 +- 3, max: 257.485 ) MB/s
[           BitReader read] Decoded with ( 164.3 +- 0.8, max: 165.319 ) MB/s
[  BitReader template read] Decoded with ( 232.1 +- 1.1, max: 233.402 ) MB/s
[  BitReader template peek] Decoded with ( 227.4 +- 0.4, max: 227.804 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 250.8 +- 2, max: 252.473 ) MB/s
[           BitReader read] Decoded with ( 163.5 +- 0.7, max: 164.1 ) MB/s
[  BitReader template read] Decoded with ( 235.8 +- 1.4, max: 237.857 ) MB/s
[  BitReader template peek] Decoded with ( 216.14 +- 0.24, max: 216.379 ) MB/s

== Benchmarking by reading 2 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 475.7 +- 3, max: 477.625 ) MB/s
[           BitReader read] Decoded with ( 317.8 +- 2.2, max: 320.141 ) MB/s
[  BitReader template read] Decoded with ( 433.4 +- 0.9, max: 434.521 ) MB/s
[  BitReader template peek] Decoded with ( 436 +- 4, max: 441.604 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 458 +- 4, max: 462.121 ) MB/s
[           BitReader read] Decoded with ( 309 +- 3, max: 313.2 ) MB/s
[  BitReader template read] Decoded with ( 454.6 +- 0.4, max: 455.11 ) MB/s
[  BitReader template peek] Decoded with ( 437.2 +- 1.2, max: 438.594 ) MB/s

== Benchmarking by reading 8 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1215 +- 14, max: 1233.16 ) MB/s
[           BitReader read] Decoded with ( 1104 +- 7, max: 1112.88 ) MB/s
[  BitReader template read] Decoded with ( 1370 +- 8, max: 1378.24 ) MB/s
[  BitReader template peek] Decoded with ( 1442 +- 12, max: 1456.54 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1135 +- 3, max: 1139.05 ) MB/s
[           BitReader read] Decoded with ( 1091 +- 12, max: 1104.94 ) MB/s
[  BitReader template read] Decoded with ( 1418 +- 1.7, max: 1419.79 ) MB/s
[  BitReader template peek] Decoded with ( 1501 +- 16, max: 1516.25 ) MB/s

== Benchmarking by reading 15 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 995 +- 0.8, max: 996.087 ) MB/s
[           BitReader read] Decoded with ( 1395 +- 7, max: 1402.89 ) MB/s
[  BitReader template read] Decoded with ( 1687 +- 11, max: 1701.32 ) MB/s
[  BitReader template peek] Decoded with ( 1167 +- 6, max: 1174.04 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1422 +- 10, max: 1429.87 ) MB/s
[           BitReader read] Decoded with ( 1559 +- 19, max: 1576.6 ) MB/s
[  BitReader template read] Decoded with ( 2160 +- 50, max: 2199.54 ) MB/s
[  BitReader template peek] Decoded with ( 1916 +- 11, max: 1931.2 ) MB/s

== Benchmarking by reading 16 bits ==

=== 32-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1444 +- 5, max: 1450.4 ) MB/s
[           BitReader read] Decoded with ( 1687 +- 14, max: 1700.84 ) MB/s
[  BitReader template read] Decoded with ( 2101 +- 14, max: 2117.45 ) MB/s
[  BitReader template peek] Decoded with ( 1782 +- 10, max: 1793.49 ) MB/s

=== 64-bit Buffer ===
[  Simple bit reading loop] Decoded with ( 1608 +- 16, max: 1618.47 ) MB/s
[           BitReader read] Decoded with ( 1871 +- 16, max: 1891.96 ) MB/s
[  BitReader template read] Decoded with ( 2589 +- 25, max: 2625.94 ) MB/s
[  BitReader template peek] Decoded with ( 2584 +- 16, max: 2598.02 ) MB/s


= MSB (bzip2) =
== Simple bit reading loop ==
64-bit is faster 3, slower 2, and approximately equal 0 out of 5 times.
== BitReader read ==
64-bit is faster 2, slower 3, and approximately equal 0 out of 5 times.
== BitReader template read ==
64-bit is faster 3, slower 0, and approximately equal 2 out of 5 times.
== BitReader template peek ==
64-bit is faster 4, slower 1, and approximately equal 0 out of 5 times.

= LSB (gzip) =
== Simple bit reading loop ==
64-bit is faster 2, slower 2, and approximately equal 1 out of 5 times.
== BitReader read ==
64-bit is faster 2, slower 1, and approximately equal 2 out of 5 times.
== BitReader template read ==
64-bit is faster 5, slower 0, and approximately equal 0 out of 5 times.
== BitReader template peek ==
64-bit is faster 3, slower 1, and approximately equal 1 out of 5 times.

    -> These results don't seem very stable over different benchmark runs but there are always
       multiple instances where the 64-bit buffer slows things down?!
*/
