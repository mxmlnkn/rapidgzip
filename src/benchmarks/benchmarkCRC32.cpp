/**
 * @file While the other benchmarks test varying situations and parameters for single components,
 *       this file is a collection of benchmarks for selected (best) versions for each component
 *       to get an overview of the current state of rapidgzip.
 */

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/**
 * See:
 *  - clang -march=native -dM -E - < /dev/null | grep -E "SSE|AVX" | sort
 *  - gcc -march=native mavx2 -dM -E - < /dev/null | grep -E "SSE|AVX" | sort
 */
#ifdef __SSE4_2__
    #include <nmmintrin.h>
#endif

#include <common.hpp>
#include <crc32.hpp>
#include <Statistics.hpp>


constexpr size_t REPEAT_COUNT{ 10 };


[[nodiscard]] std::string
formatBandwidth( const std::vector<double>& times,
                 const size_t               nBytes )
{
    std::vector<double> bandwidths( times.size() );
    std::transform( times.begin(), times.end(), bandwidths.begin(),
                    [nBytes] ( double time ) { return static_cast<double>( nBytes ) / time / 1e6; } );
    Statistics<double> bandwidthStats{ bandwidths };

    /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
     * physically possible but many noisy influences can slow things down, i.e., the minimum time is
     * the value closest to be free of noise. */
    std::stringstream result;
    result << "( min: " << bandwidthStats.min << ", " << bandwidthStats.formatAverageWithUncertainty()
           << ", max: " << bandwidthStats.max << " ) MB/s";
    return result.str();
}


using BenchmarkFunction = std::function<std::pair</* duration */ double, /* checksum */ uint64_t>()>;


[[nodiscard]] std::vector<double>
repeatBenchmarks( const BenchmarkFunction& toMeasure,
                  const size_t             repeatCount = REPEAT_COUNT )
{
    std::cout << "Repeating benchmarks " << repeatCount << " times ... " << std::flush;
    const auto tStart = now();

    std::optional<uint64_t> checksum;
    std::vector<double> times( repeatCount );
    for ( auto& time : times ) {
        const auto [measuredTime, calculatedChecksum] = toMeasure();
        time = measuredTime;

        if ( !checksum ) {
            checksum = calculatedChecksum;
        } else if ( *checksum != calculatedChecksum ) {
            throw std::runtime_error( "Indeterministic or wrong result observed!" );
        }
    }

    std::cout << "Done (" << duration( tStart ) << " s)\n";
    return times;
}


template<unsigned int SLICE_SIZE>
[[nodiscard]] uint32_t
computeCRC32SliceByN( const std::vector<char>& buffer )
{
    return ~rapidgzip::updateCRC32<SLICE_SIZE>( ~uint32_t( 0 ), buffer.data(), buffer.size() );
}


#ifdef __SSE4_2__
[[nodiscard]] uint32_t
crc32SSE4( uint32_t    crc,
           const char* bytes,
           size_t      size )
{
    crc = ~crc;

    const auto* const chunks = reinterpret_cast<const __m128i*>( bytes );
    for ( size_t i = 0; i < size / sizeof( __m128i ); ++i ) {
        /* https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_crc32&ig_expand=1563 */
        const __m128i data = _mm_loadu_si128( chunks + i );
        crc = _mm_crc32_u32( crc, _mm_cvtsi128_si32( data ) );
        crc = _mm_crc32_u32( crc, _mm_extract_epi32( data, 1 ) );
        crc = _mm_crc32_u32( crc, _mm_extract_epi32( data, 2 ) );
        crc = _mm_crc32_u32( crc, _mm_extract_epi32( data, 3 ) );
    }

    for ( size_t i = size - ( size % sizeof( __m128i ) ); i < size; ++i ) {
        crc = _mm_crc32_u8( crc, bytes[i] );
    }

    return ~crc;
}


[[nodiscard]] uint32_t
computeCRC32SSE4u32( const std::vector<char>& buffer ) noexcept
{
    return crc32SSE4( 0, buffer.data(), buffer.size() );
}


[[nodiscard]] uint32_t
crc32SSE4u64( uint32_t    crc,
              const char* bytes,
              size_t      size )
{
    crc = ~crc;

    const auto* const chunks = reinterpret_cast<const __m128i*>( bytes );
    for ( size_t i = 0; i < size / sizeof( __m128i ); ++i ) {
        /* https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_crc32&ig_expand=1563 */
        const __m128i data = _mm_loadu_si128( chunks + i );
        crc = _mm_crc32_u64( crc, _mm_cvtsi128_si64( data ) );
        crc = _mm_crc32_u64( crc, _mm_extract_epi64( data, 1 ) );
    }

    for ( size_t i = size - ( size % sizeof( __m128i ) ); i < size; ++i ) {
        crc = _mm_crc32_u8( crc, bytes[i] );
    }

    return ~crc;
}


[[nodiscard]] uint32_t
computeCRC32SSE4u64( const std::vector<char>& buffer ) noexcept
{
    return crc32SSE4u64( 0, buffer.data(), buffer.size() );
}
#endif  // ifdef __SSE4_2__

[[nodiscard]] uint32_t
computeCRC32LUT( const std::vector<char>& buffer ) noexcept
{
    auto crc = ~uint32_t( 0 );
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        crc = ( crc >> 8U ) ^ rapidgzip::CRC32_TABLE[( crc ^ buffer[i] ) & 0xFFU];
    }
    return ~crc;
}


void
benchmarkCRC32( const std::vector<char>&                                   data,
                const std::function<uint32_t ( const std::vector<char>& )> crc32Function,
                const std::string&                                         name )
{
    const auto crc32 = crc32Function( data );
    std::stringstream message;
    message << "Result: 0x" << std::hex << std::uppercase << crc32 << "\n";

    const auto times = repeatBenchmarks(
        [&] () {
            const auto tCRC32Start = now();
            const auto result = crc32Function( data );
            return std::make_pair( duration( tCRC32Start ), static_cast<uint64_t>( result ) );
        } );

    std::ofstream dataFile( "compute-crc32.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Compute CRC32 (" << name << ")] " << formatBandwidth( times, data.size() )
              << " -> " << std::move( message ).str() << "\n";
}


int
main()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 128_Mi + 13 /* Some uneven number to test the tail handling */ );
    //std::vector<char> data( 1 );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    const auto t1 = now();
    std::cout << "Done (" << duration( t0, t1 ) << " s)\n";

    benchmarkCRC32( data, computeCRC32LUT, "LUT" );
    benchmarkCRC32( data, computeCRC32SliceByN<4>, "slice by 4" );
    benchmarkCRC32( data, computeCRC32SliceByN<8>, "slice by 8" );
    benchmarkCRC32( data, computeCRC32SliceByN<12>, "slice by 12" );
    benchmarkCRC32( data, computeCRC32SliceByN<16>, "slice by 16" );
    benchmarkCRC32( data, computeCRC32SliceByN<20>, "slice by 20" );
    benchmarkCRC32( data, computeCRC32SliceByN<24>, "slice by 24" );
    benchmarkCRC32( data, computeCRC32SliceByN<32>, "slice by 32" );
    benchmarkCRC32( data, computeCRC32SliceByN<64>, "slice by 64" );

#ifdef __SSE4_2__
    benchmarkCRC32( data, computeCRC32SSE4u32, "_mm_crc32_u32" );
    benchmarkCRC32( data, computeCRC32SSE4u64, "_mm_crc32_u64" );
#endif

    return 0;
}


/*
cmake --build . -- benchmarkCRC32 && src/benchmarks/benchmarkCRC32 2>&1 | tee benchmarkCRC32.log

Initializing random data for benchmark... Done (1.38061 s)

[Compute CRC32 (LUT)]           ( min: 516.417, 517.3 +- 1.0, max: 519.571 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 4)]    ( min: 1402.55, 1414  +-   5, max: 1419.92 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 8)]    ( min: 2553.45, 2588  +-  19, max: 2618.4  ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 12)]   ( min: 3602.26, 3760  +-  60, max: 3808.64 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 16)]   ( min: 3869.64, 3970  +-  50, max: 4038.77 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 20)]   ( min: 2586.97, 2627  +-  23, max: 2644.93 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 24)]   ( min: 2956.9 , 2988  +-  12, max: 2997.68 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 32)]   ( min: 2736.25, 2806  +-  29, max: 2828.43 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 64)]   ( min: 2104.77, 2139  +-  13, max: 2150.09 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (_mm_crc32_u32)] ( min: 5212.63, 5280  +-  50, max: 5351.18 ) MB/s -> Result: 0xAFDBD4A7
[Compute CRC32 (_mm_crc32_u64)] ( min: 9012.49, 9700  +- 400, max: 10155.2 ) MB/s -> Result: 0xAFDBD4A7

Without -march=native and with loop unrolling 8:

Initializing random data for benchmark... Done (1.36239 s)
[Compute CRC32 (LUT)]         ( min: 525.228,  535 +-  6, max: 542.199 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 4)]  ( min: 1422.35, 1464 +- 15, max: 1478.13 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 8)]  ( min: 2644.90, 2668 +-  9, max: 2673.84 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 12)] ( min: 3926.02, 3978 +- 26, max: 4009.95 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 16)] ( min: 4546.23, 4630 +- 40, max: 4670.93 ) MB/s -> Result: 0xFBA351D8 <-
[Compute CRC32 (slice by 20)] ( min: 2628.45, 2653 +- 19, max: 2676.97 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 24)] ( min: 2967.99, 3100 +- 60, max: 3152.44 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 32)] ( min: 2791.56, 2829 +- 20, max: 2845.71 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 64)] ( min: 2173.20, 2222 +- 21, max: 2238.27 ) MB/s -> Result: 0xFBA351D8

Unroll 2:
[Compute CRC32 (slice by 16)] ( min: 4236.64, 4330 +- 40, max: 4356.45 ) MB/s -> Result: 0xFBA351D8

Unroll 4:
[Compute CRC32 (slice by 16)] ( min: 4336.98, 4380 +- 30, max: 4466.21 ) MB/s -> Result: 0xFBA351D8

Unroll 8:
[Compute CRC32 (slice by 16)] ( min: 3970.89, 4470 +- 180, max: 4570.67 ) MB/s -> Result: 0xFBA351D8


Benchmarks on AMD EPYC 7702 64-Core Processor at 2.0 GHz

[Compute CRC32 (LUT)]         ( min:  402.787,  407 +- 4 , max:  413.8  ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 4)]  ( min: 1145.26 , 1155 +- 5 , max: 1161.87 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 8)]  ( min: 2171.20 , 2179 +- 7 , max: 2193.91 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 12)] ( min: 3243.96 , 3269 +- 12, max: 3285.04 ) MB/s -> Result: 0xFBA351D8 <-
[Compute CRC32 (slice by 16)] ( min: 2980.60 , 2995 +- 12, max: 3013.38 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 20)] ( min: 2101.17 , 2109 +- 6 , max: 2116.93 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 24)] ( min: 2192.88 , 2199 +- 6 , max: 2211.52 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 32)] ( min: 2185.12 , 2194 +- 6 , max: 2206.73 ) MB/s -> Result: 0xFBA351D8
[Compute CRC32 (slice by 64)] ( min: 1473.50 , 1484 +- 7 , max: 1492.82 ) MB/s -> Result: 0xFBA351D8
*/
