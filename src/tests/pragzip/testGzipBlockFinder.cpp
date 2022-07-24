#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <BitManipulation.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <common.hpp>
#include <deflate.hpp>


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


int
main( int    argc,
      char** /* argv */)
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    testDynamicHuffmanBlockFinder();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
