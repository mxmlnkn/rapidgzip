#include <array>
#include <fstream>

#define LIBRAPIDARCHIVE_SKIP_LOAD_FROM_CSV

#include <core/SimpleRunLengthEncoding.hpp>
#include <rapidgzip/rapidgzip.hpp>
#include <rapidgzip/blockfinder/precodecheck/WalkTreeCompressedSingleLUT.hpp>

using namespace rapidgzip;


void
writeDataAsCSV( const std::vector<uint8_t>& data,
                const std::string&          path )
{
    std::ofstream file{ path };
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
    const std::string path{ "OPTIMAL_NEXT_DEFLATE_LUT_SIZE.csv" };
    std::ofstream file{ path };
    constexpr auto lutSize = 1U << OPTIMAL_NEXT_DEFLATE_LUT_SIZE;
    for ( uint32_t i = 0; i < lutSize; ++i ) {
        file << std::setw( 2 )
             << static_cast<unsigned int>( nextDeflateCandidate<OPTIMAL_NEXT_DEFLATE_LUT_SIZE>( i ) ) << ',';
        if ( ( i + 1 ) % 32 == 0 ) {
            file << '\n';
        }
    }
    std::cout << "Wrote " << path << " sized: " << lutSize << " B\n";
}


void
writeDataAsRLECompressedCSV( const std::vector<uint8_t>& data,
                             const std::string&          path )
{
    using namespace SimpleRunLengthEncoding;

    const auto compressed = simpleRunLengthEncode( data );
    const auto restored =
        simpleRunLengthDecode<std::vector<uint8_t> >( compressed, data.size() );
    if ( restored != data ) {
        throw std::logic_error( "RLE encoding is broking!" );
    }
    writeDataAsCSV( compressed, path );
    std::cout << "Wrote " << path << " sized " << data.size() << " B -> compressed to: " << compressed.size() << " B\n";
}


void
writeWalkTreeCompressedLUT()
{
    constexpr auto SUBTABLE_CHUNK_COUNT = 128U;
    const auto& [histogramLUT, validLUT] =
        PrecodeCheck::WalkTreeCompressedSingleLUT::PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<SUBTABLE_CHUNK_COUNT>;

    writeDataAsRLECompressedCSV(
        histogramLUT,
        "PRECODE_FREQUENCIES_VALID_FULL_LUT_TWO_STAGES_" + std::to_string( SUBTABLE_CHUNK_COUNT )
        + "_HISTOGRAM_TO_INDEX.csv" );
    writeDataAsRLECompressedCSV(
        validLUT, "PRECODE_FREQUENCIES_VALID_FULL_LUT_TWO_STAGES_" + std::to_string( SUBTABLE_CHUNK_COUNT )
        + "_INDEX_TO_VALID.csv" );
}


int
main()
{
    writeNextDeflateLUT();
    writeWalkTreeCompressedLUT();

    return 0;
}
