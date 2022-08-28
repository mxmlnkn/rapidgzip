#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

#include <common.hpp>  // countNewlines
#include <TestHelpers.hpp>
#include <Statistics.hpp>


[[nodiscard]] std::vector<char>
createRandomBase64( const size_t size )
{
    std::vector<char> result( size );
    constexpr std::string_view BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567890+/";
    for ( size_t i = 0; i < size; ++i ) {
        result[i] = ( ( i + 1 == size ) || ( ( i + 1 ) % 77 == 0 )
                    ? '\n' : BASE64[static_cast<size_t>( rand() ) % BASE64.size()] );
    }
    return result;
}


[[nodiscard]] std::string
formatBandwidth( const std::vector<double>& times,
                 size_t                     byteCount )
{
    std::vector<double> bandwidths( times.size() );
    std::transform( times.begin(), times.end(), bandwidths.begin(),
                    [byteCount] ( double time ) { return static_cast<double>( byteCount ) / time / 1e6; } );
    Statistics<double> bandwidthStats{ bandwidths };

    /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
     * physically possible but many noisy influences can slow things down, i.e., the minimum time is
     * the value closest to be free of noise. */
    std::stringstream result;
    result << "( " + bandwidthStats.formatAverageWithUncertainty( true ) << " ) MB/s";
    return result.str();
}


int
main( int    /* argc */,
      char** /* argv */ )
{
    std::cout << "Creating random data..." << std::flush;
    const auto buffer = createRandomBase64( 1_Gi );
    std::cout << "OK" << std::endl;

    size_t newLines{ 0 };
    for ( const auto& c : buffer ) {
        if ( c == '\n' ) {
            newLines++;
        }
    }

    {
        const auto [result, durations] = benchmarkFunction<10>(
            [&buffer] () { return countNewlines( { buffer.data(), buffer.size() } ); } );
        if ( result != newLines ) {
            std::stringstream message;
            message << "Found " << result << " newlines even though the ground truth is " << newLines;
            throw std::logic_error( std::move( message ).str() );
        }

        std::cout << "[countNewlines] " << formatBandwidth( durations, buffer.size() ) << "\n"
                  << "    Newlines: " << result << " out of " << buffer.size() << " ("
                  << static_cast<double>( result ) / static_cast<double>( buffer.size() ) * 100 << " %)\n";
    }

    return 0;
}


/*
cmake --build . -- benchmarkCalculations && taskset 0x08 src/benchmarks/benchmarkCalculations

    Creating random data...OK
    [countNewlines] ( 11060 <= 11280 +- 140 <= 11480 ) MB/s
        Newlines: 13944700 out of 1073741824 (1.2987 %)

With -march=native

    Creating random data...OK
    [countNewlines] ( 10990 <= 11270 +- 150 <= 11480 ) MB/s
        Newlines: 13944700 out of 1073741824 (1.2987 %)
*/
