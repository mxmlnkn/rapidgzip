#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <BitManipulation.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <blockfinder/Uncompressed.hpp>
#include <common.hpp>
#include <deflate.hpp>
#include <filereader/Standard.hpp>
#include <TestHelpers.hpp>


using namespace pragzip;


/** Requires at least 13 valid bits inside the lowest bits of @ref bits! */
[[nodiscard]] bool
isValidDynamicHuffmanBlock( uint32_t bits )
{
    using namespace pragzip::deflate;

    const auto finalBlock = bits & nLowestBitsSet<uint32_t, 1U>();
    if ( finalBlock == 0b1 ) {
        return false;
    }
    bits >>= 1U;

    const auto compressionType = bits & nLowestBitsSet<uint32_t, 2U>();
    if ( compressionType != 0b10 ) {
        return false;
    }
    bits >>= 2U;

    const auto codeCount = bits & nLowestBitsSet<uint32_t, 5U>();
    if ( 257U + codeCount > MAX_LITERAL_OR_LENGTH_SYMBOLS ) {
        return false;
    }
    bits >>= 5U;

    const auto distanceCodeCount = bits & nLowestBitsSet<uint32_t, 5U>();
    return 1U + distanceCodeCount <= MAX_DISTANCE_SYMBOL_COUNT;
}


void
testDynamicHuffmanBlockFinder()
{
    REQUIRE( blockfinder::nextDeflateCandidate< 8>( 0x7CU ) == 0 );
    REQUIRE( blockfinder::nextDeflateCandidate<10>( 0x7CU ) == 0 );
    REQUIRE( blockfinder::nextDeflateCandidate<14>( 0x7CU ) == 0 );
    static constexpr auto NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT = blockfinder::createNextDeflateCandidateLUT<14>();
    for ( uint32_t bits = 0; bits < NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT.size(); ++bits ) {
        REQUIRE( isValidDynamicHuffmanBlock( bits ) == ( NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT[bits] == 0 ) );
        if ( isValidDynamicHuffmanBlock( bits ) != ( NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT[bits] == 0 ) ) {
            std::cerr << "Results differ for bits: 0x" << std::hex << bits << std::dec
                      << ", isValidDynamicHuffmanBlock: " << ( isValidDynamicHuffmanBlock( bits ) ? "true" : "false" )
                      << "\n";
        }
    }
}


void
testUncompressedBlockFinder( std::string const&                             path,
                             std::vector<std::pair<size_t, size_t> > const& expected )
{
    pragzip::BitReader bitReader( std::make_unique<StandardFileReader>( path ) );

    std::vector<std::pair<size_t, size_t> > foundRanges;
    while ( true ) {
        const auto foundRange = blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader );
        if ( !foundRange ) {
            break;
        }

        /* Test that we do not enter an infinite loop. */
        if ( !foundRanges.empty() && ( foundRanges.back() == *foundRange ) ) {
            REQUIRE( foundRanges.back() != *foundRange );
            break;
        }

        std::cerr << "Found range: " << foundRange->first << ", " << foundRange->second << "\n";

        foundRanges.emplace_back( *foundRange );
        bitReader.seek( static_cast<long long int>( foundRange->second ) + 1 );
    }

    REQUIRE_EQUAL( foundRanges.size(), expected.size() );
    REQUIRE( foundRanges == expected );
}


int
main( int    argc,
      char** argv )
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    const std::string binaryFilePath( argv[0] );
    std::string binaryFolder = ".";
    if ( const auto lastSlash = binaryFilePath.find_last_of( '/' ); lastSlash != std::string::npos ) {
        binaryFolder = std::string( binaryFilePath.begin(),
                                    binaryFilePath.begin() + static_cast<std::string::difference_type>( lastSlash ) );
    }
    const auto testsFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/random-128KiB.bgz" )
        ) / "src" / "tests" / "data";

    /* Because the whole file consists of compressed blocks, the +5 can be easily explained.
     * After a compressed block, the next one will begin at byte-boundary but the latest it might begin is at
     * the next byte boundary minus 3 0-bits (non-final block + block type 0b00). */
    const std::vector<std::pair<size_t, size_t> > expectedOffsetRanges = {
        { 24ULL    * BYTE_SIZE, 24ULL    * BYTE_SIZE + 5ULL },
        { 32806ULL * BYTE_SIZE, 32806ULL * BYTE_SIZE + 5ULL },
        { 65604ULL * BYTE_SIZE, 65604ULL * BYTE_SIZE + 5ULL },
        /* The Uncompressed block finder only looks for non-final blocks! */
        /* { 98386ULL * BYTE_SIZE, 98386ULL * BYTE_SIZE + 5ULL }, */
    };
    testUncompressedBlockFinder( testsFolder / "random-128KiB.gz", expectedOffsetRanges );

    testDynamicHuffmanBlockFinder();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
