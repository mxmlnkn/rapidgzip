
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include <BitReader.hpp>
#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <HuffmanCodingDoubleLiteralCached.hpp>
#include <HuffmanCodingLinearSearch.hpp>
#include <HuffmanCodingReversedBitsCached.hpp>
#include <HuffmanCodingReversedCodesPerLength.hpp>
#include <HuffmanCodingSymbolsPerLength.hpp>
#include <TestHelpers.hpp>


using namespace pragzip;


template<typename HuffmanCoding>
void
decodeHuffmanAndCompare( const std::vector<uint8_t>&                        codeLengths,
                         const std::vector<uint8_t>&                        encoded,
                         const std::vector<typename HuffmanCoding::Symbol>& decoded )
{
    BufferedFileReader::AlignedBuffer encodedChars( encoded.size() );
    std::transform( encoded.begin(), encoded.end(), encodedChars.begin(), [] ( const auto c ) { return c; } );
    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( std::move( encodedChars ) ) );

    HuffmanCoding coding;
    const auto errorCode = coding.initializeFromLengths( codeLengths );
    if ( errorCode != Error::NONE ) {
        std::cerr << "Could not create HuffmanCoding from given lengths because: " << toString( errorCode ) << "\n";
        REQUIRE( false );
        return;
    }

    for ( const auto symbol : decoded ) {
        const auto decodedSymbol = coding.decode( bitReader );
        REQUIRE( decodedSymbol.has_value() );

        if ( decodedSymbol && ( decodedSymbol != symbol ) ) {
            std::cerr << "Decoded symbol " << (int)*decodedSymbol << " mismatches expected " << (int)symbol << "\n";
        }
        REQUIRE( decodedSymbol == symbol );
    }
    REQUIRE( ceilDiv( bitReader.tell(), CHAR_BIT ) * CHAR_BIT == bitReader.size() );
}


template<typename HuffmanCoding>
void
testHuffmanCoding()
{
    /* codeLengths, encoded, decoded */
    decodeHuffmanAndCompare<HuffmanCoding>( { 1, 1 }, { 0 }, { 0 } );
    decodeHuffmanAndCompare<HuffmanCoding>( { 1, 1 }, { 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 } );
    decodeHuffmanAndCompare<HuffmanCoding>( { 1, 1 }, { 0b1010 }, { 0, 1, 0, 1, 0, 0, 0, 0 } );

    decodeHuffmanAndCompare<HuffmanCoding>( { 1, 2, 2 }, { 0b11'01'0 }, { 0, 1, 2, 0, 0 } );
    decodeHuffmanAndCompare<HuffmanCoding>( { 1, 3, 3, 3, 3 }, { 0b111'001'0 }, { 0, 1, 4, 0 } );
    decodeHuffmanAndCompare<HuffmanCoding>( { 1, 3, 3, 3, 3 }, { 0b011'101'0 }, { 0, 2, 3, 0 } );

    const std::vector<typename HuffmanCoding::BitCount> equalCodeLengths( 256, 8 );
    std::vector<typename HuffmanCoding::Symbol> decoded( equalCodeLengths.size() );
    std::vector<uint8_t> encoded( equalCodeLengths.size() );
    for ( size_t i = 0; i < encoded.size(); ++i ) {
        decoded[i] = i;
        encoded[i] = reverseBits( static_cast<uint8_t>( i ) );
    }
    decodeHuffmanAndCompare<HuffmanCoding>( equalCodeLengths, encoded, decoded );
}


int main()
{
    static constexpr auto MAX_CODE_LENGTH = 15;
    static constexpr auto MAX_SYMBOL_COUNT = 512;

    std::cerr << "Testing HuffmanCodingLinearSearch...\n";
    testHuffmanCoding<HuffmanCodingLinearSearch<uint16_t, uint16_t> >();

    std::cerr << "Testing HuffmanCodingSymbolsPerLength...\n";
    testHuffmanCoding<HuffmanCodingSymbolsPerLength<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT> >();

    std::cerr << "Testing HuffmanCodingReversedBitsCached...\n";
    testHuffmanCoding<HuffmanCodingReversedBitsCached<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT> >();

    std::cerr << "Testing HuffmanCodingReversedCodesPerLength...\n";
    testHuffmanCoding<HuffmanCodingReversedCodesPerLength<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT> >();

    std::cerr << "Testing HuffmanCodingDoubleLiteralCached...\n";
    testHuffmanCoding<HuffmanCodingDoubleLiteralCached<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT> >();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
