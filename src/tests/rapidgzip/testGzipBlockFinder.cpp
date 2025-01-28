#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <BitManipulation.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <blockfinder/Uncompressed.hpp>
#include <common.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


/** Requires at least 13 valid bits inside the lowest bits of @ref bits! */
[[nodiscard]] bool
isValidDynamicHuffmanBlock( uint32_t bits )
{
    using namespace rapidgzip::deflate;

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
    using namespace rapidgzip::blockfinder;

    /* Note that it non-final dynamic blocks must begin with 0b100 (bits are read from lowest to highest bit).
     * From that we can already construct some tests. */
    REQUIRE( nextDeflateCandidate<0>( 0b0 ) == 0 );
    REQUIRE( nextDeflateCandidate<1>( 0b1 ) == 1 );
    REQUIRE( nextDeflateCandidate<1>( 0b0 ) == 0 );

    REQUIRE( nextDeflateCandidate<2>( 0b01 ) == 1 );
    REQUIRE( nextDeflateCandidate<2>( 0b00 ) == 0 );
    REQUIRE( nextDeflateCandidate<2>( 0b11 ) == 2 );
    REQUIRE( nextDeflateCandidate<2>( 0b10 ) == 2 );

    REQUIRE( nextDeflateCandidate<3>( 0b001 ) == 1 );
    REQUIRE( nextDeflateCandidate<3>( 0b000 ) == 1 );
    REQUIRE( nextDeflateCandidate<3>( 0b011 ) == 2 );
    REQUIRE( nextDeflateCandidate<3>( 0b010 ) == 2 );
    REQUIRE( nextDeflateCandidate<3>( 0b101 ) == 3 );
    REQUIRE( nextDeflateCandidate<3>( 0b100 ) == 0 );
    REQUIRE( nextDeflateCandidate<3>( 0b111 ) == 3 );
    REQUIRE( nextDeflateCandidate<3>( 0b110 ) == 3 );

    REQUIRE( nextDeflateCandidate< 8>( 0x7CU ) == 0 );
    REQUIRE( nextDeflateCandidate<10>( 0x7CU ) == 0 );
    REQUIRE( nextDeflateCandidate<14>( 0x7CU ) == 0 );

    const auto& LUT = NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT<18>;
    for ( uint32_t bits = 0; bits < LUT.size(); ++bits ) {
        REQUIRE( isValidDynamicHuffmanBlock( bits ) == ( LUT[bits] == 0 ) );
        if ( isValidDynamicHuffmanBlock( bits ) != ( LUT[bits] == 0 ) ) {
            std::cerr << "Results differ for bits: 0x" << std::hex << bits << std::dec
                      << ", isValidDynamicHuffmanBlock: " << ( isValidDynamicHuffmanBlock( bits ) ? "true" : "false" )
                      << "\n";
        }
    }
}


void
testUncompressedBlockFinder( const std::filesystem::path&                   path,
                             const std::vector<std::pair<size_t, size_t> >& expected )
{
    gzip::BitReader bitReader( std::make_unique<StandardFileReader>( path.string() ) );

    std::vector<std::pair<size_t, size_t> > foundRanges;
    while ( true ) {
        const auto foundRange = blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader );
        if ( foundRange.first == std::numeric_limits<size_t>::max() ) {
            break;
        }

        /* Test that we do not enter an infinite loop. */
        if ( !foundRanges.empty() && ( foundRanges.back() == foundRange ) ) {
            REQUIRE( foundRanges.back() != foundRange );
            break;
        }

        foundRanges.emplace_back( foundRange );
        bitReader.seek( static_cast<long long int>( foundRange.second ) + 1 );
    }

    const auto printRanges =
        [&bitReader] ( const auto& offsetRanges ) {
            std::stringstream message;
            for ( const auto& [start, stop] : offsetRanges ) {
                bitReader.seek( stop + 3 );
                message << std::dec << "    [" << start << ", " << stop << "] -> size: 0x"
                        << std::hex << bitReader.peek<32>() << "\n";
            }
            return std::move( message ).str();
        };

    REQUIRE_EQUAL( foundRanges.size(), expected.size() );
    REQUIRE( foundRanges == expected );
    if ( foundRanges != expected ) {
        std::cerr << "Found ranges:\n" << printRanges( foundRanges );
        std::cerr << "Expected ranges:\n" << printRanges( expected );
    }

    /* Search in 1 B blocks. */
    foundRanges.clear();
    static constexpr auto BLOCK_SIZE = 8U;  /** in bits */
    for ( size_t offset = 0; offset < bitReader.size().value(); offset += BLOCK_SIZE ) {
        bitReader.seek( static_cast<long long int>( offset ) );
        const auto foundRange = blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader, offset + BLOCK_SIZE );
        if ( foundRange.first != std::numeric_limits<size_t>::max() ) {
            const auto validResult = rangesIntersect( foundRange, std::make_pair( offset, offset + BLOCK_SIZE ) );
            REQUIRE( validResult );
            if ( !validResult ) {
                std::cerr << "Found range: [" << formatBits( foundRange.first ) << ", "
                          << formatBits( foundRange.second ) << "] is outside of search range ["
                          << formatBits( offset ) << ", " << formatBits( offset + BLOCK_SIZE ) << "]\n";
            }
            foundRanges.emplace_back( foundRange );
        }
    }

    /* It is valid for there to be duplicates because the allowed start range may be 3 to 10 bits before the
     * uncompressed block size depending on how many zero bits there are. */
    foundRanges.erase( std::unique( foundRanges.begin(), foundRanges.end() ), foundRanges.end() );

    REQUIRE_EQUAL( foundRanges.size(), expected.size() );
    REQUIRE( foundRanges == expected );
    if ( foundRanges != expected ) {
        std::cerr << "Found ranges:\n" << printRanges( foundRanges );
        std::cerr << "Expected ranges:\n" << printRanges( expected );
    }
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

    /* Note that rapidgzip --analyze shows the real offset to be 199507 but depending on the preceding bits
     * the range can go all the way back to the last byte boundary. In this case it goes back 1 bit. */
    testUncompressedBlockFinder( testsFolder / "base64-64KiB.pigz", { { 199506, 199509 } } );

    /* Note that rapidgzip --analyze shows the real offset to be 24942 * BYTE_SIZE + 7 but depending on the preceding bits
     * the range can go all the way back to the last byte boundary. In this case it goes back 1 bit. */
    testUncompressedBlockFinder( testsFolder / "base64-64KiB-7b-offset-uncompressed.pigz",
                                 { { 24942 * BYTE_SIZE + 6, 24944 * BYTE_SIZE - 3 } } );

    /* Because the whole file consists of compressed blocks, the +5 can be easily explained.
     * After a compressed block, the next one will begin at byte-boundary but the latest it might begin is at
     * the next byte boundary minus 3 0-bits (non-final block + block type 0b00). */
    const std::vector<std::pair<size_t, size_t> > expectedOffsetRanges = {
        { 24ULL    * BYTE_SIZE - 2ULL, 24ULL    * BYTE_SIZE + 5ULL },
        { 32806ULL * BYTE_SIZE       , 32806ULL * BYTE_SIZE + 5ULL },
        { 65604ULL * BYTE_SIZE       , 65604ULL * BYTE_SIZE + 5ULL },
        /* The Uncompressed block finder only looks for non-final blocks. However, because of the byte-alignment
         * and the zero-padding it might give a false positive range even for a final uncompressed block!
         * In this case, the real offset is at exactly 98386 B. But this means that there 5 zero-padded bits
         * following that might get interpreted as the non-final uncompressed block signature 0b000! */
        { 98386ULL * BYTE_SIZE + 1ULL, 98386ULL * BYTE_SIZE + 5ULL },
    };
    testUncompressedBlockFinder( testsFolder / "random-128KiB.gz", expectedOffsetRanges );

    testDynamicHuffmanBlockFinder();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
