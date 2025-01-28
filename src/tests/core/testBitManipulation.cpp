#include <cstdint>
#include <cstdio>
#include <iostream>

#include <BitManipulation.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


void
testByteSwap()
{
    REQUIRE_EQUAL( byteSwap( uint16_t( 0xABCD ) ), 0xCDABU );
    REQUIRE_EQUAL( byteSwap( uint32_t( 0xABCDEF01 ) ), 0x01EFCDABUL );
    REQUIRE_EQUAL( byteSwap( uint64_t( 0xABCDEF01'23456789 ) ), 0x89674523'01EFCDABULL );
}


template<uint8_t bitsSet>
uint8_t
nLowestBitsSet8()
{
    return nLowestBitsSet<uint8_t, bitsSet>();
}


template<uint8_t bitsSet>
uint16_t
nLowestBitsSet16()
{
    return nLowestBitsSet<uint16_t, bitsSet>();
}


template<uint8_t bitsSet>
uint32_t
nLowestBitsSet32()
{
    return nLowestBitsSet<uint32_t, bitsSet>();
}


template<uint8_t bitsSet>
uint64_t
nLowestBitsSet64()
{
    return nLowestBitsSet<uint64_t, bitsSet>();
}


template<uint8_t bitsSet>
uint8_t
nHighestBitsSet8()
{
    return nHighestBitsSet<uint8_t, bitsSet>();
}


template<uint8_t bitsSet>
uint16_t
nHighestBitsSet16()
{
    return nHighestBitsSet<uint16_t, bitsSet>();
}


template<uint8_t bitsSet>
uint32_t
nHighestBitsSet32()
{
    return nHighestBitsSet<uint32_t, bitsSet>();
}


template<uint8_t bitsSet>
uint64_t
nHighestBitsSet64()
{
    return nHighestBitsSet<uint64_t, bitsSet>();
}


void
testLowestBitsSet()
{
    // *INDENT-OFF*
    REQUIRE( nLowestBitsSet<uint8_t>(  0 ) == uint8_t( 0b0000U ) );
    REQUIRE( nLowestBitsSet<uint8_t>(  1 ) == uint8_t( 0b0001U ) );
    REQUIRE( nLowestBitsSet<uint8_t>(  2 ) == uint8_t( 0b0011U ) );
    REQUIRE( nLowestBitsSet<uint8_t>(  3 ) == uint8_t( 0b0111U ) );
    REQUIRE( nLowestBitsSet<uint8_t>(  8 ) == uint8_t( 0x00FFU ) );

    REQUIRE( nLowestBitsSet8<0 >() == uint8_t( 0b0000U ) );
    REQUIRE( nLowestBitsSet8<1 >() == uint8_t( 0b0001U ) );
    REQUIRE( nLowestBitsSet8<2 >() == uint8_t( 0b0011U ) );
    REQUIRE( nLowestBitsSet8<3 >() == uint8_t( 0b0111U ) );
    REQUIRE( nLowestBitsSet8<8 >() == uint8_t( 0x00FFU ) );

    REQUIRE( nLowestBitsSet<uint16_t>(  0 ) == uint16_t( 0b0000U ) );
    REQUIRE( nLowestBitsSet<uint16_t>(  1 ) == uint16_t( 0b0001U ) );
    REQUIRE( nLowestBitsSet<uint16_t>(  2 ) == uint16_t( 0b0011U ) );
    REQUIRE( nLowestBitsSet<uint16_t>(  3 ) == uint16_t( 0b0111U ) );
    REQUIRE( nLowestBitsSet<uint16_t>(  8 ) == uint16_t( 0x00FFU ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 15 ) == uint16_t( 0x7FFFU ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 16 ) == uint16_t( 0xFFFFU ) );

    REQUIRE( nLowestBitsSet16<0 >() == uint16_t( 0b0000U ) );
    REQUIRE( nLowestBitsSet16<1 >() == uint16_t( 0b0001U ) );
    REQUIRE( nLowestBitsSet16<2 >() == uint16_t( 0b0011U ) );
    REQUIRE( nLowestBitsSet16<3 >() == uint16_t( 0b0111U ) );
    REQUIRE( nLowestBitsSet16<8 >() == uint16_t( 0x00FFU ) );
    REQUIRE( nLowestBitsSet16<15>() == uint16_t( 0x7FFFU ) );
    REQUIRE( nLowestBitsSet16<16>() == uint16_t( 0xFFFFU ) );

    REQUIRE( nLowestBitsSet<uint32_t>(  0 ) == uint32_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet<uint32_t>(  1 ) == uint32_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet<uint32_t>(  2 ) == uint32_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet<uint32_t>(  3 ) == uint32_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet<uint32_t>(  8 ) == uint32_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 31 ) == uint32_t( 0x7FFF'FFFFU ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 32 ) == uint32_t( 0xFFFF'FFFFU ) );

    REQUIRE( nLowestBitsSet32<0 >() == uint32_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet32<1 >() == uint32_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet32<2 >() == uint32_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet32<3 >() == uint32_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet32<8 >() == uint32_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet32<31>() == uint32_t( 0x7FFF'FFFFU ) );
    REQUIRE( nLowestBitsSet32<32>() == uint32_t( 0xFFFF'FFFFU ) );

    REQUIRE( nLowestBitsSet<uint64_t>(  0 ) == uint64_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet<uint64_t>(  1 ) == uint64_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet<uint64_t>(  2 ) == uint64_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet<uint64_t>(  3 ) == uint64_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet<uint64_t>(  8 ) == uint64_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 63 ) == uint64_t( 0x7FFF'FFFF'FFFF'FFFFULL ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 64 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );

    REQUIRE( nLowestBitsSet64<0 >() == uint64_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet64<1 >() == uint64_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet64<2 >() == uint64_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet64<3 >() == uint64_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet64<8 >() == uint64_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet64<63>() == uint64_t( 0x7FFF'FFFF'FFFF'FFFFULL ) );
    REQUIRE( nLowestBitsSet64<64>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );
    // *INDENT-ON*
}


void
testHighestBitsSet()
{
    // *INDENT-OFF*
    REQUIRE( nHighestBitsSet<uint8_t>(  0 ) == uint8_t( 0x00UL ) );
    REQUIRE( nHighestBitsSet<uint8_t>(  1 ) == uint8_t( 0x80UL ) );
    REQUIRE( nHighestBitsSet<uint8_t>(  2 ) == uint8_t( 0xC0UL ) );
    REQUIRE( nHighestBitsSet<uint8_t>(  3 ) == uint8_t( 0xE0UL ) );
    REQUIRE( nHighestBitsSet<uint8_t>(  8 ) == uint8_t( 0xFFUL ) );

    REQUIRE( nHighestBitsSet8<0 >() == uint8_t( 0x00UL ) );
    REQUIRE( nHighestBitsSet8<1 >() == uint8_t( 0x80UL ) );
    REQUIRE( nHighestBitsSet8<2 >() == uint8_t( 0xC0UL ) );
    REQUIRE( nHighestBitsSet8<3 >() == uint8_t( 0xE0UL ) );
    REQUIRE( nHighestBitsSet8<8 >() == uint8_t( 0xFFUL ) );

    REQUIRE( nHighestBitsSet<uint16_t>(  0 ) == uint16_t( 0x0000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>(  1 ) == uint16_t( 0x8000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>(  2 ) == uint16_t( 0xC000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>(  3 ) == uint16_t( 0xE000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>(  8 ) == uint16_t( 0xFF00UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 15 ) == uint16_t( 0xFFFEUL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 16 ) == uint16_t( 0xFFFFUL ) );

    REQUIRE( nHighestBitsSet16<0 >() == uint16_t( 0x0000UL ) );
    REQUIRE( nHighestBitsSet16<1 >() == uint16_t( 0x8000UL ) );
    REQUIRE( nHighestBitsSet16<2 >() == uint16_t( 0xC000UL ) );
    REQUIRE( nHighestBitsSet16<3 >() == uint16_t( 0xE000UL ) );
    REQUIRE( nHighestBitsSet16<8 >() == uint16_t( 0xFF00UL ) );
    REQUIRE( nHighestBitsSet16<15>() == uint16_t( 0xFFFEUL ) );
    REQUIRE( nHighestBitsSet16<16>() == uint16_t( 0xFFFFUL ) );

    REQUIRE( nHighestBitsSet<uint32_t>(  0 ) == uint32_t( 0x0000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>(  1 ) == uint32_t( 0x8000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>(  2 ) == uint32_t( 0xC000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>(  3 ) == uint32_t( 0xE000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>(  8 ) == uint32_t( 0xFF00'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 31 ) == uint32_t( 0xFFFF'FFFEUL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 32 ) == uint32_t( 0xFFFF'FFFFUL ) );

    REQUIRE( nHighestBitsSet32<0 >() == uint32_t( 0x0000'0000UL ) );
    REQUIRE( nHighestBitsSet32<1 >() == uint32_t( 0x8000'0000UL ) );
    REQUIRE( nHighestBitsSet32<2 >() == uint32_t( 0xC000'0000UL ) );
    REQUIRE( nHighestBitsSet32<3 >() == uint32_t( 0xE000'0000UL ) );
    REQUIRE( nHighestBitsSet32<8 >() == uint32_t( 0xFF00'0000UL ) );
    REQUIRE( nHighestBitsSet32<31>() == uint32_t( 0xFFFF'FFFEUL ) );
    REQUIRE( nHighestBitsSet32<32>() == uint32_t( 0xFFFF'FFFFUL ) );

    REQUIRE( nHighestBitsSet<uint64_t>(  0 ) == uint64_t( 0x0000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>(  1 ) == uint64_t( 0x8000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>(  2 ) == uint64_t( 0xC000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>(  3 ) == uint64_t( 0xE000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>(  8 ) == uint64_t( 0xFF00'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 63 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFEULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 64 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );

    REQUIRE( nHighestBitsSet64<0 >() == uint64_t( 0x0000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<1 >() == uint64_t( 0x8000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<2 >() == uint64_t( 0xC000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<3 >() == uint64_t( 0xE000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<8 >() == uint64_t( 0xFF00'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<63>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFEULL ) );
    REQUIRE( nHighestBitsSet64<64>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );
    // *INDENT-ON*
}


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


void
testRequiredBits()
{
    static_assert( requiredBits( 0 ) == 0 );
    static_assert( requiredBits( 1 ) == 1 );
    static_assert( requiredBits( 2 ) == 1 );
    static_assert( requiredBits( 3 ) == 2 );
    static_assert( requiredBits( 4 ) == 2 );
    static_assert( requiredBits( 5 ) == 3 );
    static_assert( requiredBits( 6 ) == 3 );
    static_assert( requiredBits( 7 ) == 3 );
    static_assert( requiredBits( 8 ) == 3 );
    static_assert( requiredBits( 64 ) == 6 );
    static_assert( requiredBits( 256 ) == 8 );
}


int
main()
{
    REQUIRE( isLittleEndian() );
    testByteSwap();
    testBitReversing();
    testLowestBitsSet();
    testHighestBitsSet();
    testRequiredBits();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
