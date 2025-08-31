#include <array>
#include <fstream>

#define LIBRAPIDARCHIVE_SKIP_LOAD_FROM_CSV

#include <rapidgzip/rapidgzip.hpp>

using namespace rapidgzip;


void
writeDataAsCSV( const std::vector<uint8_t>& data,
                const std::string&          path )
{
    std::ofstream file{ path, std::ios_base::out | std::ios_base::binary };
    for ( uint32_t i = 0; i < data.size(); ++i ) {
        file << std::setw( 4 ) << static_cast<uint64_t>( data[i] ) << ',';
        if ( ( i + 1 ) % 16 == 0 ) {
            file << '\n';
        }
    }
}


void
writeNextDeflateLUT()
{
    using namespace blockfinder;
    constexpr auto CACHED_BIT_COUNT = OPTIMAL_NEXT_DEFLATE_LUT_SIZE;
    const std::string path{ "NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT_" + std::to_string( CACHED_BIT_COUNT ) + ".csv" };
    std::ofstream file{ path, std::ios_base::out | std::ios_base::binary };
    constexpr auto lutSize = 1U << CACHED_BIT_COUNT;
    for ( uint32_t i = 0; i < lutSize; ++i ) {
        int8_t value = nextDeflateCandidate<CACHED_BIT_COUNT>( i );
        if ( value == 0 ) {
            value = -static_cast<uint8_t>( 1U + nextDeflateCandidate<CACHED_BIT_COUNT - 1U>( i >> 1U ) );
        }
        file << std::setw( 3 ) << static_cast<int>( value ) << ',';
        if ( ( i + 1 ) % 32 == 0 ) {
            file << '\n';
        }
    }
    std::cout << "Wrote " << path << " sized: " << lutSize << " B\n";
}


int
main()
{
    writeNextDeflateLUT();

    return 0;
}
