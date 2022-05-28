#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <common.hpp>
#include <BitReader.hpp>
#include <BufferedFileReader.hpp>


template<uint8_t bitsSet>
uint16_t nLowestBitsSet16()
{
    return nLowestBitsSet<uint16_t, bitsSet>();
}

template<uint8_t bitsSet>
uint32_t nLowestBitsSet32()
{
    return nLowestBitsSet<uint32_t, bitsSet>();
}

template<uint8_t bitsSet>
uint64_t nLowestBitsSet64()
{
    return nLowestBitsSet<uint64_t, bitsSet>();
}

template<uint8_t bitsSet>
uint16_t nHighestBitsSet16()
{
    return nHighestBitsSet<uint16_t, bitsSet>();
}

template<uint8_t bitsSet>
uint32_t nHighestBitsSet32()
{
    return nHighestBitsSet<uint32_t, bitsSet>();
}

template<uint8_t bitsSet>
uint64_t nHighestBitsSet64()
{
    return nHighestBitsSet<uint64_t, bitsSet>();
}


void
testMSBBitReader()
{
    const std::vector<char> fileContents ={
        /*       0x5A                0xAA               0x0F               0x0F               0x0F */
        (char)0b0101'1010, (char)0b1010'1010, (char)0b0000'1111, (char)0b0000'1111, (char)0b0000'1111
    };
    BitReader<true> bitReader( std::make_unique<BufferedFileReader>( fileContents ) );

    REQUIRE( bitReader.read<0>() == 0b0UL );
    REQUIRE( bitReader.read<1>() == 0b0UL );
    REQUIRE( bitReader.tell() == 1 );
    REQUIRE( bitReader.read<1>() == 0b1UL );
    REQUIRE( bitReader.tell() == 2 );
    REQUIRE( bitReader.read<2>() == 0b01UL );
    REQUIRE( bitReader.tell() == 4 );
    REQUIRE( bitReader.read<4>() == 0b1010UL );
    REQUIRE( bitReader.tell() == 8 );
    REQUIRE( bitReader.read<8>() == 0b1010'1010UL );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.tell() == 0 );
    REQUIRE( bitReader.read<8>() == 0b0101'1010UL );
    REQUIRE( bitReader.tell() == 8 );
    REQUIRE( bitReader.read<16>() == 0b1010'1010'0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_CUR ) == 16 );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_END ) == 32 );
    REQUIRE( bitReader.read<1>() == 0b0UL );
    REQUIRE( bitReader.tell() == 33 );
    REQUIRE( bitReader.read<3>() == 0b000UL );
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( bitReader.read<4>() == 0b1111UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.read<24>() == 0x5AAA0FUL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.read<32>() == 0x5AAA'0F0FUL );
    REQUIRE( bitReader.tell() == 32 );

    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.read<13>() == 0b1010'1010'1010'0UL );
    REQUIRE( bitReader.tell() == 17 );

    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.read<32>() == 0xAAA0'F0F0UL );
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( bitReader.read<2>() == 0b11UL );
    REQUIRE( bitReader.read<2>() == 0b11UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );

    REQUIRE( bitReader.seek( -35, SEEK_END ) == 5 );
    REQUIRE( bitReader.tell() == 5 );
    REQUIRE( bitReader.read<32>() == 0b010'1010'1010'0000'1111'0000'1111'0000'1UL );
    REQUIRE( bitReader.tell() == 37 );
}


void
testLSBBitReader()
{
    const std::vector<char> fileContents ={
        /*       0x5A                0xAA               0x0F               0x0F               0x0F    */
        (char)0b0101'1010, (char)0b1010'1010, (char)0b0000'1111, (char)0b0000'1111, (char)0b0000'1111
    };
    BitReader<false> bitReader( std::make_unique<BufferedFileReader>( fileContents ) );

    REQUIRE( bitReader.read<0>() == 0b0UL );
    REQUIRE( bitReader.read<1>() == 0b0UL );
    REQUIRE( bitReader.tell() == 1 );
    REQUIRE( bitReader.read<1>() == 0b1UL );
    REQUIRE( bitReader.tell() == 2 );
    REQUIRE( bitReader.read<2>() == 0b10UL );
    REQUIRE( bitReader.tell() == 4 );
    REQUIRE( bitReader.read<4>() == 0b0101UL );
    REQUIRE( bitReader.tell() == 8 );
    REQUIRE( bitReader.read<8>() == 0b1010'1010UL );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.tell() == 0 );
    REQUIRE( bitReader.read<8>() == 0b0101'1010UL );
    REQUIRE( bitReader.tell() == 8 );
    /* Note that reading more than 8 bits, will result in the bytes being swapped!
     * This is because the byte numbering is from left to right but bit numbering from right to left,
     * but when we request more than 8 bits, all bits are numbered right to left in the resulting DWORD! */
    REQUIRE( bitReader.read<16>() == 0b0000'1111'1010'1010UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_CUR ) == 16 );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_END ) == 32 );
    REQUIRE( bitReader.read<1>() == 0b1UL );
    REQUIRE( bitReader.tell() == 33 );
    REQUIRE( bitReader.read<3>() == 0b111UL );
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( bitReader.read<4>() == 0b0000UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.read<32>() == 0x0F0F'AA5AUL );
    REQUIRE( bitReader.tell() == 32 );

    REQUIRE( bitReader.seek( 8, SEEK_SET ) == 8 );
    REQUIRE( bitReader.read<13>() == 0b00'1111'1010'1010UL );
    REQUIRE( bitReader.tell() == 21 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.read<4>() == 0xAUL );
    REQUIRE( bitReader.read<4>() == 0x5UL );
    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.read<4>() == 0x5UL );

    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    const auto result = bitReader.read<32>();
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( result == 0xF0F0'FAA5UL );
    REQUIRE( bitReader.read<2>() == 0b00UL );
    REQUIRE( bitReader.read<2>() == 0b00UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );
}


void
testMSBBitReaderPeek()
{
    const std::vector<char> fileContents ={
        /*       0x5A                0xAA               0x0F               0x0F               0x0F */
        (char)0b0101'1010, (char)0b1010'1010, (char)0b0000'1111, (char)0b0000'1111, (char)0b0000'1111
    };
    /* Bit buffer must be uint64_t or else the peek 32-bits might feel if it is not aligned to byte boundary! */
    BitReader<true, uint64_t> bitReader( std::make_unique<BufferedFileReader>( fileContents ) );

    REQUIRE( bitReader.peek<0>() == 0b0UL );
    REQUIRE( bitReader.read<0>() == 0b0UL );
    REQUIRE( bitReader.peek<1>() == 0b0UL );
    REQUIRE( bitReader.read<1>() == 0b0UL );
    REQUIRE( bitReader.tell() == 1 );
    REQUIRE( bitReader.peek<1>() == 0b1UL );
    REQUIRE( bitReader.read<1>() == 0b1UL );
    REQUIRE( bitReader.tell() == 2 );
    REQUIRE( bitReader.peek<2>() == 0b01UL );
    REQUIRE( bitReader.read<2>() == 0b01UL );
    REQUIRE( bitReader.tell() == 4 );
    REQUIRE( bitReader.peek<4>() == 0b1010UL );
    REQUIRE( bitReader.read<4>() == 0b1010UL );
    REQUIRE( bitReader.tell() == 8 );
    REQUIRE( bitReader.peek<8>() == 0b1010'1010UL );
    REQUIRE( bitReader.read<8>() == 0b1010'1010UL );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.peek<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.tell() == 0 );
    REQUIRE( bitReader.peek<8>() == 0b0101'1010UL );
    REQUIRE( bitReader.read<8>() == 0b0101'1010UL );
    REQUIRE( bitReader.tell() == 8 );
    REQUIRE( bitReader.peek<16>() == 0b1010'1010'0000'1111UL );
    REQUIRE( bitReader.read<16>() == 0b1010'1010'0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_CUR ) == 16 );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.peek<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_END ) == 32 );
    REQUIRE( bitReader.peek<1>() == 0b0UL );
    REQUIRE( bitReader.read<1>() == 0b0UL );
    REQUIRE( bitReader.tell() == 33 );
    REQUIRE( bitReader.peek<3>() == 0b000UL );
    REQUIRE( bitReader.read<3>() == 0b000UL );
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( bitReader.peek<4>() == 0b1111UL );
    REQUIRE( bitReader.read<4>() == 0b1111UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.peek<24>() == 0x5AAA0FUL );
    REQUIRE( bitReader.read<24>() == 0x5AAA0FUL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.peek<32>() == 0x5AAA'0F0FUL );
    REQUIRE( bitReader.read<32>() == 0x5AAA'0F0FUL );
    REQUIRE( bitReader.tell() == 32 );

    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.peek<13>() == 0b1010'1010'1010'0UL );
    REQUIRE( bitReader.read<13>() == 0b1010'1010'1010'0UL );
    REQUIRE( bitReader.tell() == 17 );

    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.peek<32>() == 0xAAA0'F0F0UL );
    REQUIRE( bitReader.read<32>() == 0xAAA0'F0F0UL );
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( bitReader.peek<2>() == 0b11UL );
    REQUIRE( bitReader.read<2>() == 0b11UL );
    REQUIRE( bitReader.peek<2>() == 0b11UL );
    REQUIRE( bitReader.read<2>() == 0b11UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );

    REQUIRE( bitReader.seek( -35, SEEK_END ) == 5 );
    REQUIRE( bitReader.tell() == 5 );
    REQUIRE( bitReader.peek<32>() == 0b010'1010'1010'0000'1111'0000'1111'0000'1UL );
    REQUIRE( bitReader.read<32>() == 0b010'1010'1010'0000'1111'0000'1111'0000'1UL );
    REQUIRE( bitReader.tell() == 37 );
}


void
testLSBBitReaderPeek()
{
    const std::vector<char> fileContents ={
        /*       0x5A                0xAA               0x0F               0x0F               0x0F    */
        (char)0b0101'1010, (char)0b1010'1010, (char)0b0000'1111, (char)0b0000'1111, (char)0b0000'1111
    };
    /* Bit buffer must be uint64_t or else the peek 32-bits might feel if it is not aligned to byte boundary! */
    BitReader<false, uint64_t> bitReader( std::make_unique<BufferedFileReader>( fileContents ) );

    REQUIRE( bitReader.peek<0>() == 0b0UL );
    REQUIRE( bitReader.read<0>() == 0b0UL );
    REQUIRE( bitReader.peek<1>() == 0b0UL );
    REQUIRE( bitReader.read<1>() == 0b0UL );
    REQUIRE( bitReader.tell() == 1 );
    REQUIRE( bitReader.peek<1>() == 0b1UL );
    REQUIRE( bitReader.read<1>() == 0b1UL );
    REQUIRE( bitReader.tell() == 2 );
    REQUIRE( bitReader.peek<2>() == 0b10UL );
    REQUIRE( bitReader.read<2>() == 0b10UL );
    REQUIRE( bitReader.tell() == 4 );
    REQUIRE( bitReader.peek<4>() == 0b0101UL );
    REQUIRE( bitReader.read<4>() == 0b0101UL );
    REQUIRE( bitReader.tell() == 8 );
    REQUIRE( bitReader.peek<8>() == 0b1010'1010UL );
    REQUIRE( bitReader.read<8>() == 0b1010'1010UL );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.peek<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.tell() == 0 );
    REQUIRE( bitReader.peek<8>() == 0b0101'1010UL );
    REQUIRE( bitReader.read<8>() == 0b0101'1010UL );
    REQUIRE( bitReader.tell() == 8 );
    /* Note that reading more than 8 bits, will result in the bytes being swapped!
     * This is because the byte numbering is from left to right but bit numbering from right to left,
     * but when we request more than 8 bits, all bits are numbered right to left in the resulting DWORD! */
    REQUIRE( bitReader.peek<16>() == 0b0000'1111'1010'1010UL );
    REQUIRE( bitReader.read<16>() == 0b0000'1111'1010'1010UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_CUR ) == 16 );
    REQUIRE( bitReader.tell() == 16 );
    REQUIRE( bitReader.peek<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.read<8>() == 0b0000'1111UL );
    REQUIRE( bitReader.tell() == 24 );

    REQUIRE( bitReader.seek( -8, SEEK_END ) == 32 );
    REQUIRE( bitReader.peek<1>() == 0b1UL );
    REQUIRE( bitReader.read<1>() == 0b1UL );
    REQUIRE( bitReader.tell() == 33 );
    REQUIRE( bitReader.peek<3>() == 0b111UL );
    REQUIRE( bitReader.read<3>() == 0b111UL );
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( bitReader.peek<4>() == 0b0000UL );
    REQUIRE( bitReader.read<4>() == 0b0000UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.peek<32>() == 0x0F0F'AA5AUL );
    REQUIRE( bitReader.read<32>() == 0x0F0F'AA5AUL );
    REQUIRE( bitReader.tell() == 32 );

    REQUIRE( bitReader.seek( 8, SEEK_SET ) == 8 );
    REQUIRE( bitReader.peek<13>() == 0b00'1111'1010'1010UL );
    REQUIRE( bitReader.read<13>() == 0b00'1111'1010'1010UL );
    REQUIRE( bitReader.tell() == 21 );

    REQUIRE( bitReader.seek( 0, SEEK_SET ) == 0 );
    REQUIRE( bitReader.peek<4>() == 0xAUL );
    REQUIRE( bitReader.read<4>() == 0xAUL );
    REQUIRE( bitReader.peek<4>() == 0x5UL );
    REQUIRE( bitReader.read<4>() == 0x5UL );
    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.peek<4>() == 0x5UL );
    REQUIRE( bitReader.read<4>() == 0x5UL );

    REQUIRE( bitReader.seek( 4, SEEK_SET ) == 4 );
    REQUIRE( bitReader.peek<32>() == 0xF0F0'FAA5UL );
    const auto result = bitReader.read<32>();
    REQUIRE( bitReader.tell() == 36 );
    REQUIRE( result == 0xF0F0'FAA5 );
    REQUIRE( bitReader.peek<2>() == 0b00UL );
    REQUIRE( bitReader.read<2>() == 0b00UL );
    REQUIRE( bitReader.peek<2>() == 0b00UL );
    REQUIRE( bitReader.read<2>() == 0b00UL );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );
}


void
testLowestBitsSet()
{
    REQUIRE( nLowestBitsSet<uint16_t>( 0  ) == uint16_t( 0b0000U ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 1  ) == uint16_t( 0b0001U ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 2  ) == uint16_t( 0b0011U ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 3  ) == uint16_t( 0b0111U ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 8  ) == uint16_t( 0x00FFU ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 15 ) == uint16_t( 0x7FFFU ) );
    REQUIRE( nLowestBitsSet<uint16_t>( 16 ) == uint16_t( 0xFFFFU ) );

    REQUIRE( nLowestBitsSet16<0 >() == uint16_t( 0b0000U ) );
    REQUIRE( nLowestBitsSet16<1 >() == uint16_t( 0b0001U ) );
    REQUIRE( nLowestBitsSet16<2 >() == uint16_t( 0b0011U ) );
    REQUIRE( nLowestBitsSet16<3 >() == uint16_t( 0b0111U ) );
    REQUIRE( nLowestBitsSet16<8 >() == uint16_t( 0x00FFU ) );
    REQUIRE( nLowestBitsSet16<15>() == uint16_t( 0x7FFFU ) );
    REQUIRE( nLowestBitsSet16<16>() == uint16_t( 0xFFFFU ) );

    REQUIRE( nLowestBitsSet<uint32_t>( 0  ) == uint32_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 1  ) == uint32_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 2  ) == uint32_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 3  ) == uint32_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 8  ) == uint32_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 31 ) == uint32_t( 0x7FFF'FFFFU ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 32 ) == uint32_t( 0xFFFF'FFFFU ) );

    REQUIRE( nLowestBitsSet32<0 >() == uint32_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet32<1 >() == uint32_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet32<2 >() == uint32_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet32<3 >() == uint32_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet32<8 >() == uint32_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet32<31>() == uint32_t( 0x7FFF'FFFFU ) );
    REQUIRE( nLowestBitsSet32<32>() == uint32_t( 0xFFFF'FFFFU ) );

    REQUIRE( nLowestBitsSet<uint64_t>( 0  ) == uint64_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 1  ) == uint64_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 2  ) == uint64_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 3  ) == uint64_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 8  ) == uint64_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 63 ) == uint64_t( 0x7FFF'FFFF'FFFF'FFFFULL ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 64 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );

    REQUIRE( nLowestBitsSet64<0 >() == uint64_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet64<1 >() == uint64_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet64<2 >() == uint64_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet64<3 >() == uint64_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet64<8 >() == uint64_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet64<63>() == uint64_t( 0x7FFF'FFFF'FFFF'FFFFULL ) );
    REQUIRE( nLowestBitsSet64<64>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );
}


void
testHighestBitsSet()
{
    REQUIRE( nHighestBitsSet<uint16_t>( 0  ) == uint16_t( 0x0000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 1  ) == uint16_t( 0x8000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 2  ) == uint16_t( 0xC000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 3  ) == uint16_t( 0xE000UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 8  ) == uint16_t( 0xFF00UL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 15 ) == uint16_t( 0xFFFEUL ) );
    REQUIRE( nHighestBitsSet<uint16_t>( 16 ) == uint16_t( 0xFFFFUL ) );

    REQUIRE( nHighestBitsSet16<0 >() == uint16_t( 0x0000UL ) );
    REQUIRE( nHighestBitsSet16<1 >() == uint16_t( 0x8000UL ) );
    REQUIRE( nHighestBitsSet16<2 >() == uint16_t( 0xC000UL ) );
    REQUIRE( nHighestBitsSet16<3 >() == uint16_t( 0xE000UL ) );
    REQUIRE( nHighestBitsSet16<8 >() == uint16_t( 0xFF00UL ) );
    REQUIRE( nHighestBitsSet16<15>() == uint16_t( 0xFFFEUL ) );
    REQUIRE( nHighestBitsSet16<16>() == uint16_t( 0xFFFFUL ) );

    REQUIRE( nHighestBitsSet<uint32_t>( 0  ) == uint32_t( 0x0000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 1  ) == uint32_t( 0x8000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 2  ) == uint32_t( 0xC000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 3  ) == uint32_t( 0xE000'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 8  ) == uint32_t( 0xFF00'0000UL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 31 ) == uint32_t( 0xFFFF'FFFEUL ) );
    REQUIRE( nHighestBitsSet<uint32_t>( 32 ) == uint32_t( 0xFFFF'FFFFUL ) );

    REQUIRE( nHighestBitsSet32<0 >() == uint32_t( 0x0000'0000UL ) );
    REQUIRE( nHighestBitsSet32<1 >() == uint32_t( 0x8000'0000UL ) );
    REQUIRE( nHighestBitsSet32<2 >() == uint32_t( 0xC000'0000UL ) );
    REQUIRE( nHighestBitsSet32<3 >() == uint32_t( 0xE000'0000UL ) );
    REQUIRE( nHighestBitsSet32<8 >() == uint32_t( 0xFF00'0000UL ) );
    REQUIRE( nHighestBitsSet32<31>() == uint32_t( 0xFFFF'FFFEUL ) );
    REQUIRE( nHighestBitsSet32<32>() == uint32_t( 0xFFFF'FFFFUL ) );

    REQUIRE( nHighestBitsSet<uint64_t>( 0  ) == uint64_t( 0x0000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 1  ) == uint64_t( 0x8000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 2  ) == uint64_t( 0xC000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 3  ) == uint64_t( 0xE000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 8  ) == uint64_t( 0xFF00'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 63 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFEULL ) );
    REQUIRE( nHighestBitsSet<uint64_t>( 64 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );

    REQUIRE( nHighestBitsSet64<0 >() == uint64_t( 0x0000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<1 >() == uint64_t( 0x8000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<2 >() == uint64_t( 0xC000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<3 >() == uint64_t( 0xE000'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<8 >() == uint64_t( 0xFF00'0000'0000'0000ULL ) );
    REQUIRE( nHighestBitsSet64<63>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFEULL ) );
    REQUIRE( nHighestBitsSet64<64>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );
}


int
main()
{
    testLowestBitsSet();
    testHighestBitsSet();

    testMSBBitReader();
    testLSBBitReader();
    testMSBBitReaderPeek();
    testLSBBitReaderPeek();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
