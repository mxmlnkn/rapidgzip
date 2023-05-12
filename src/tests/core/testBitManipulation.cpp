#include <cstdint>
#include <cstdio>
#include <iostream>

#include <BitManipulation.hpp>
#include <TestHelpers.hpp>


void
testBitReversing()
{
    REQUIRE_EQUAL( REVERSED_BITS_LUT<uint8_t>.size(), 256U );

    REQUIRE_EQUAL( REVERSED_BITS_LUT<uint8_t>[0b1111'0000U], 0b0000'1111U );
    REQUIRE_EQUAL( REVERSED_BITS_LUT<uint8_t>[0b1010'1010U], 0b0101'0101U );
    REQUIRE_EQUAL( reverseBitsWithoutLUT( uint8_t( 0b1111'0000 ) ), 0b0000'1111U );
    REQUIRE_EQUAL( reverseBitsWithoutLUT( uint8_t( 0b1010'1010 ) ), 0b0101'0101U );
    REQUIRE_EQUAL( reverseBits( uint8_t( 0b1111'0000 ) ), 0b0000'1111U );
    REQUIRE_EQUAL( reverseBits( uint8_t( 0b1010'1010 ) ), 0b0101'0101U );

    REQUIRE_EQUAL( REVERSED_BITS_LUT<uint16_t>[0b0000'0000'0000'0001U], 0b1000'0000'0000'0000U );
    REQUIRE_EQUAL( REVERSED_BITS_LUT<uint16_t>[0b1111'0000'1111'0000U], 0b0000'1111'0000'1111U );
    REQUIRE_EQUAL( REVERSED_BITS_LUT<uint16_t>[0b1010'1010'1010'1010U], 0b0101'0101'0101'0101U );
    REQUIRE_EQUAL( reverseBitsWithoutLUT( uint16_t( 0b1111'0000'1111'0000 ) ), 0b0000'1111'0000'1111U );
    REQUIRE_EQUAL( reverseBitsWithoutLUT( uint16_t( 0b1010'1010'1010'1010 ) ), 0b0101'0101'0101'0101U );
    REQUIRE_EQUAL( reverseBits( uint16_t( 0b1111'0000'1111'0000 ) ), 0b0000'1111'0000'1111U );
    REQUIRE_EQUAL( reverseBits( uint16_t( 0b1010'1010'1010'1010 ) ), 0b0101'0101'0101'0101U );

    /* Exhaustive test for the 16-bit table, which is used for the Huffman decoder. */
    for ( size_t i = 0; i < ( 1ULL << 16U ); ++i ) {
        const auto toReverse = static_cast<uint16_t>( i );
        REQUIRE_EQUAL( REVERSED_BITS_LUT<uint16_t>[toReverse], reverseBitsWithoutLUT( toReverse ) );
    }
}


int
main()
{
    testBitReversing();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
