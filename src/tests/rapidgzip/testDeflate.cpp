#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <common.hpp>
#include <definitions.hpp>
#include <deflate.hpp>
#include <filereader/Buffered.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


void
testCalculateDistance()
{
    using namespace deflate;

    REQUIRE( calculateDistance( 4, 1, 0b0 ) == 5 );
    REQUIRE( calculateDistance( 4, 1, 0b1 ) == 6 );
    REQUIRE( calculateDistance( 5, 1, 0b0 ) == 7 );
    REQUIRE( calculateDistance( 5, 1, 0b1 ) == 8 );
    REQUIRE( calculateDistance( 6, 2, 0b00 ) == 9 );
    REQUIRE( calculateDistance( 6, 2, 0b01 ) == 10 );
    REQUIRE( calculateDistance( 6, 2, 0b10 ) == 11 );
    REQUIRE( calculateDistance( 6, 2, 0b11 ) == 12 );

    REQUIRE( calculateDistance( 7, 2, 0b00 ) == 13 );
    REQUIRE( calculateDistance( 7, 2, 0b01 ) == 14 );
    REQUIRE( calculateDistance( 7, 2, 0b10 ) == 15 );
    REQUIRE( calculateDistance( 7, 2, 0b11 ) == 16 );
    REQUIRE( calculateDistance( 8, 3, 0b000 ) == 17 );
    REQUIRE( calculateDistance( 8, 3, 0b001 ) == 18 );
    REQUIRE( calculateDistance( 8, 3, 0b010 ) == 19 );
    REQUIRE( calculateDistance( 8, 3, 0b011 ) == 20 );

    REQUIRE( calculateDistance( 19, 8, 0xFA ) == 769 + 0xFA );

    /* These first 4 do not require extra bits and therefore are treated specially anyway. */
    //REQUIRE( calculateDistance( 0  ) == 1 );
    //REQUIRE( calculateDistance( 1  ) == 2 );  // returns 1 instead of 2! But unused anyway ...
    //REQUIRE( calculateDistance( 2  ) == 3 );
    //REQUIRE( calculateDistance( 3  ) == 4 );
    REQUIRE( calculateDistance( 4 ) == 5 );
    REQUIRE( calculateDistance( 5 ) == 7 );
    REQUIRE( calculateDistance( 6 ) == 9 );
    REQUIRE( calculateDistance( 7 ) == 13 );
    REQUIRE( calculateDistance( 8 ) == 17 );
    REQUIRE( calculateDistance( 9 ) == 25 );
    REQUIRE( calculateDistance( 10 ) == 33 );
    REQUIRE( calculateDistance( 11 ) == 49 );

    REQUIRE( calculateDistance( 19 ) == 769 );
    REQUIRE( calculateDistance( 25 ) == 6145 );
    REQUIRE( calculateDistance( 29 ) == 24577 );
}


void
testCalculateLength()
{
    using namespace deflate;

    /* These 4 do not require special bits and therefore are specially treated anyway. */
    // *INDENT-OFF*
    REQUIRE( calculateLength( 261 - 261 ) == 7   );
    REQUIRE( calculateLength( 262 - 261 ) == 8   );
    REQUIRE( calculateLength( 263 - 261 ) == 9   );
    REQUIRE( calculateLength( 264 - 261 ) == 10  );

    REQUIRE( calculateLength( 265 - 261 ) == 11  );
    REQUIRE( calculateLength( 266 - 261 ) == 13  );
    REQUIRE( calculateLength( 267 - 261 ) == 15  );
    REQUIRE( calculateLength( 268 - 261 ) == 17  );

    REQUIRE( calculateLength( 269 - 261 ) == 19  );
    REQUIRE( calculateLength( 270 - 261 ) == 23  );
    REQUIRE( calculateLength( 271 - 261 ) == 27  );
    REQUIRE( calculateLength( 272 - 261 ) == 31  );

    REQUIRE( calculateLength( 273 - 261 ) == 35  );
    REQUIRE( calculateLength( 274 - 261 ) == 43  );
    REQUIRE( calculateLength( 275 - 261 ) == 51  );
    REQUIRE( calculateLength( 276 - 261 ) == 59  );

    REQUIRE( calculateLength( 277 - 261 ) == 67  );
    REQUIRE( calculateLength( 278 - 261 ) == 83  );
    REQUIRE( calculateLength( 279 - 261 ) == 99  );
    REQUIRE( calculateLength( 280 - 261 ) == 115 );

    REQUIRE( calculateLength( 281 - 261 ) == 131 );
    REQUIRE( calculateLength( 282 - 261 ) == 163 );
    REQUIRE( calculateLength( 283 - 261 ) == 195 );
    REQUIRE( calculateLength( 284 - 261 ) == 227 );
    // *INDENT-ON*
}


template<typename HuffmanCoding>
void
decodeHuffmanAndCompare( const HuffmanCoding&                               coding,
                         const std::vector<uint8_t>&                        encoded,
                         const std::vector<typename HuffmanCoding::Symbol>& decoded )
{
    BufferedFileReader::AlignedBuffer encodedChars( encoded.size() );
    std::transform( encoded.begin(), encoded.end(), encodedChars.begin(), [] ( const auto c ) { return c; } );
    gzip::BitReader bitReader( std::make_unique<BufferedFileReader>( std::move( encodedChars ) ) );

    for ( const auto symbol : decoded ) {
        const auto decodedSymbol = coding.decode( bitReader );
        REQUIRE( decodedSymbol.has_value() );

        if ( decodedSymbol && ( decodedSymbol != symbol ) ) {
            std::cerr << "Decoded symbol " << (int)*decodedSymbol << " mismatches expected " << (int)symbol << "\n";
        }
        REQUIRE( decodedSymbol == symbol );
    }
    REQUIRE_EQUAL( ceilDiv( bitReader.tell(), CHAR_BIT ) * CHAR_BIT, bitReader.size().value() );
}


void
testFixedHuffmanCoding()
{
    /**
     * @verbatim
     *  Lit Value  Bits    Codes
     * ---------   ----    -----
     * 0 - 143     8       00110000 through = 24 (bit length 7 count!)
     *                     10111111
     * 144 - 255   9       110010000 through
     *                     111111111
     * 256 - 279   7       0000000 through
     *                     0010111 = 23
     * 280 - 287   8       11000000 through
     *                     11000111
     * @endverbatim
     */
    const auto fixedHuffmanCoding = deflate::createFixedHC();

    std::vector<uint8_t> encoded = { 0 };
    std::vector<uint16_t> decoded = { 256 };
    decodeHuffmanAndCompare( fixedHuffmanCoding, encoded, decoded );

    /* Test 8-bit codes. */
    encoded.clear();
    decoded.clear();
    for ( size_t i = 0; i < 144; ++i ) {
        encoded.push_back( reverseBits( static_cast<uint8_t>( 0b0011'0000UL + i ) ) );
        decoded.push_back( i );
    }
    for ( size_t i = 0; i < 8; ++i ) {
        encoded.push_back( reverseBits( static_cast<uint8_t>( 0b1100'0000 + i ) ) );
        decoded.push_back( 280 + i );
    }
    decodeHuffmanAndCompare( fixedHuffmanCoding, encoded, decoded );

    /* Test 7-bit codes */
    for ( size_t i = 0; i < 279 - 256 + 1; ++i ) {
        encoded = { static_cast<uint8_t>( reverseBits( static_cast<uint8_t>( i ) ) >> 1U ) };
        decoded = { static_cast<uint16_t>( 256 + i ) };
        decodeHuffmanAndCompare( fixedHuffmanCoding, encoded, decoded );
    }

    /* Test 9-bit codes */
    for ( size_t i = 0; i < 255 - 144 + 1; ++i ) {
        const auto code = static_cast<uint32_t>(
            reverseBits( static_cast<uint16_t>( 0b1'1001'0000UL + i ) ) >> ( 16U - 9U ) );
        encoded = { static_cast<uint8_t>( code & 0xFFU ), static_cast<uint8_t>( ( code >> 8U ) & 0xFFU ) };

        BufferedFileReader::AlignedBuffer encodedChars( encoded.size() );
        std::transform( encoded.begin(), encoded.end(), encodedChars.begin(), [] ( const auto c ) { return c; } );
        gzip::BitReader bitReader( std::make_unique<BufferedFileReader>( encodedChars ) );
        REQUIRE( bitReader.read<9>() == code );

        decoded = { static_cast<uint16_t>( 144 + i ) };
        decodeHuffmanAndCompare( fixedHuffmanCoding, encoded, decoded );
    }

    /* Test 7-bit and 9-bit codes */
    encoded.clear();
    decoded.clear();
    for ( size_t i = 0; i < 255 - 144 + 1; ++i ) {
        constexpr auto literals7BitsCount = 279 - 256 + 1;
        const auto mergedCode = reverseBits( static_cast<uint16_t>(
            ( /* 7-bit */ ( i % literals7BitsCount ) << 9U ) | /* 9-bit */ ( 0b1'1001'0000UL + i )
        ) );

        encoded.push_back( mergedCode & nLowestBitsSet<uint16_t, 8>() );
        encoded.push_back( static_cast<uint8_t>( static_cast<uint32_t>( mergedCode ) >> 8U ) );
        decoded.push_back( 256 + ( i % literals7BitsCount ) );
        decoded.push_back( 144 + i );
    }
    decodeHuffmanAndCompare( fixedHuffmanCoding, encoded, decoded );
}


int
main()
{
    testCalculateDistance();
    testCalculateLength();
    testFixedHuffmanCoding();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
