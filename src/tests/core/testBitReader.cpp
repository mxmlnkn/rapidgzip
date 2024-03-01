#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include <common.hpp>
#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <filereader/Buffered.hpp>
#include <TestHelpers.hpp>


void
testMSBBitReader()
{
    const std::vector<char> fileContents = {
        /*       0x5A                0xAA               0x0F               0x0F               0x0F */
        (char)0b0101'1010, (char)0b1010'1010, (char)0b0000'1111, (char)0b0000'1111, (char)0b0000'1111
    };
    BitReader<true, uint64_t> bitReader( std::make_unique<BufferedFileReader>( fileContents ) );

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

    REQUIRE( bitReader.seek( 0, SEEK_END ) == 40 );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );
}


void
testLSBBitReader()
{
    const std::vector<char> fileContents = {
        /*       0x5A                0xAA               0x0F               0x0F               0x0F    */
        (char)0b0101'1010, (char)0b1010'1010, (char)0b0000'1111, (char)0b0000'1111, (char)0b0000'1111
    };
    BitReader<false, uint64_t> bitReader( std::make_unique<BufferedFileReader>( fileContents ) );

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

    REQUIRE( bitReader.seek( 0, SEEK_END ) == 40 );
    REQUIRE( bitReader.tell() == 40 );
    REQUIRE( bitReader.eof() );
}


void
testMSBBitReaderPeek()
{
    const std::vector<char> fileContents = {
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
    const std::vector<char> fileContents = {
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


int
main()
{
    testMSBBitReader();
    testLSBBitReader();
    testMSBBitReaderPeek();
    testLSBBitReaderPeek();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
