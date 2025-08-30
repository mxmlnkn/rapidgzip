#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include <core/common.hpp>
#include <core/TestHelpers.hpp>
#include <filereader/BitReader.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/Standard.hpp>


using namespace rapidgzip;


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


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
void
testSequentialReading( const size_t nBitsToReadPerCall )
{
    const auto bufferSize = 128_Ki;
    const size_t fileSize = 4 * bufferSize + 1;
    const std::vector<char> fileContents( fileSize, 0 );
    BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer> bitReader(
        std::make_unique<BufferedFileReader>( fileContents ), bufferSize );

    for ( size_t i = 0; i + nBitsToReadPerCall <= fileSize * CHAR_BIT; i += nBitsToReadPerCall ) {
        REQUIRE_EQUAL( bitReader.tell(), i );
        REQUIRE( !bitReader.eof() );
        REQUIRE_EQUAL( bitReader.read( nBitsToReadPerCall ), uint64_t( 0 ) );
    }

    if ( const auto remainingBits = ( fileSize * CHAR_BIT ) % nBitsToReadPerCall; remainingBits > 0 ) {
        REQUIRE_EQUAL( bitReader.read( remainingBits ), uint64_t( 0 ) );
    }

    REQUIRE_EQUAL( bitReader.tell(), fileSize * CHAR_BIT );
    REQUIRE( bitReader.eof() );
}


enum class FastPath
{
    BIT_BUFFER_SEEK,
    ALIGNED_BYTE_BUFFER_SEEK,
    NON_ALIGNED_BYTE_BUFFER_SEEK,
    NON_ALIGNED_BYTE_BUFFER_SEEK_CLOSE_TO_BUFFER_END,
};


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer,
         FastPath FAST_PATH>
void
testBufferSeekingFastPaths()
{
    const auto bufferSize = 1_Ki;
    const size_t fileSize = 2 * bufferSize;
    const std::vector<char> fileContents( fileSize, 0 );

    BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer> bitReader(
        std::make_unique<BufferedFileReader>( fileContents ), bufferSize );

    /* This will trigger the first bit and byte buffer refill, after which we can test the optimized fast paths. */
    REQUIRE_EQUAL( bitReader.read( 1 ), 0U );
    REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 1U );
    REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );

    switch ( FAST_PATH )
    {
    case FastPath::BIT_BUFFER_SEEK:
    {
        /* Seek forward inside the bit buffer. */
        REQUIRE_EQUAL( bitReader.seek( 2 ), 2U );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );

        REQUIRE_EQUAL( bitReader.seek( 0 ), 0U );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );

        break;
    }
    /* A seek inside the byte buffer clears the bit buffer and if and only if bit-alignment is necessary,
     * the seek will internally delegate to reading bits, which may also refill the byte buffer! */
    case FastPath::ALIGNED_BYTE_BUFFER_SEEK:
    {
        /* Byte-aligned offset inside the byte buffer will not refill the bit buffer on seek and therefore
         * will also not refill the byte buffer. */
        const auto byteAlignedOffset = bufferSize / 2 * CHAR_BIT;
        bitReader.seek( byteAlignedOffset );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.tell(), byteAlignedOffset );

        /* Because the bit buffer has not been refilled, this read will also refill the bit buffer. */
        REQUIRE_EQUAL( bitReader.read( 1 ), 0U );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.tell(), byteAlignedOffset + 1 );

        break;
    }
    case FastPath::NON_ALIGNED_BYTE_BUFFER_SEEK:
    {
        /* Non-byte-aligned offset inside the byte buffer will refill the bit buffer, but if we are far enough
         * (as much as the full bit buffer can hold) from the byte buffer end, will not refill the byte buffer. */
        const auto nonByteAlignedOffset = ( bufferSize - sizeof( BitBuffer ) ) * CHAR_BIT - 1;
        bitReader.seek( nonByteAlignedOffset );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.tell(), nonByteAlignedOffset );

        /* Because the bit buffer has been refilled, this read will have no side effects nothing. */
        REQUIRE_EQUAL( bitReader.read( 1 ), 0U );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 1U );
        REQUIRE_EQUAL( bitReader.tell(), nonByteAlignedOffset + 1 );

        break;
    }
    case FastPath::NON_ALIGNED_BYTE_BUFFER_SEEK_CLOSE_TO_BUFFER_END:
    {
        /* Non-byte-aligned offset inside the byte buffer will refill the bit buffer, and if there are not enough
         * bytes to refill the bit buffer wholly, will trigger a byte buffer refill! */
        const auto nonByteAlignedOffset = ( bufferSize - sizeof( BitBuffer ) + 1 ) * CHAR_BIT + 1;
        bitReader.seek( nonByteAlignedOffset );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.tell(), nonByteAlignedOffset );

        /* Because the bit buffer has been refilled, this read will have no side effects nothing. */
        REQUIRE_EQUAL( bitReader.read( 1 ), 0U );
        REQUIRE_EQUAL( bitReader.statistics().bitBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.statistics().byteBufferRefillCount, 2U );
        REQUIRE_EQUAL( bitReader.tell(), nonByteAlignedOffset + 1 );

        break;
    }
    }
}


template<bool     MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer = uint64_t>
void
testBitReader()
{
    for ( const auto nBitsToReadPerCall : { 1, 2, 3, 15, 16, 31, 32, 48, 63 } ) {
        testSequentialReading<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>( nBitsToReadPerCall );
    }

    testBufferSeekingFastPaths<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, FastPath::BIT_BUFFER_SEEK>();
    testBufferSeekingFastPaths<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, FastPath::ALIGNED_BYTE_BUFFER_SEEK>();
    testBufferSeekingFastPaths<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer, FastPath::NON_ALIGNED_BYTE_BUFFER_SEEK>();
    testBufferSeekingFastPaths<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer,
                               FastPath::NON_ALIGNED_BYTE_BUFFER_SEEK_CLOSE_TO_BUFFER_END>();
}


void
testDirectFileReadingBitReaderBug( const std::filesystem::path& path )
{
    /* This test is intended to work with random-128KiB.gz, but it should work with any file that is
     * greater than 128 KiB (BitReader byte buffer size) and that has mostly different bytes so that
     * the check that compares the read bytes at position 128_Ki - 1 after seeking forward and after
     * seeking back fails. */
    BitReader<false, uint64_t> bitReader( std::make_unique<StandardFileReader>( path.string() ),
                                          /* byte buffer size */ 128_Ki );

    static constexpr size_t GZIP_HEADER_SIZE = 0;
    bitReader.seek( GZIP_HEADER_SIZE * CHAR_BIT );
    REQUIRE_EQUAL( bitReader.tell(), GZIP_HEADER_SIZE * CHAR_BIT );
    /* The byte buffer should already have been refilled in the seek above but this may change in the future. */
    bitReader.read<8>();
    REQUIRE_EQUAL( bitReader.tell(), GZIP_HEADER_SIZE * CHAR_BIT + 8 );

    bitReader.seek( bitReader.bufferRefillSize() * CHAR_BIT - 16, SEEK_CUR );
    const auto oneByteBeforeByteBufferEnd = ( GZIP_HEADER_SIZE + bitReader.bufferRefillSize() - 1 ) * CHAR_BIT;
    REQUIRE_EQUAL( bitReader.tell(), oneByteBeforeByteBufferEnd );

    /* Read bytes until the end of the byte buffer. */
    char firstDummy{ 0 };
    REQUIRE_EQUAL( bitReader.read( &firstDummy, 1 ), 1U );
    REQUIRE_EQUAL( bitReader.tell(), ( GZIP_HEADER_SIZE + bitReader.bufferRefillSize() ) * CHAR_BIT );

    /* Read bytes and especially trigger the byte reading DIRECTLY from the file!
     * This only gets triggered when reading more than 1 KiB, or at least requesting that much at once. */
    std::array<char, 4_Ki> buffer{};
    REQUIRE( bitReader.read( buffer.data(), buffer.size() ) > 0 );
    REQUIRE_EQUAL( bitReader.tell(), bitReader.size().value() );

    /* The problem here was that the byte buffer did not get cleared. This resulted in a bug because the assumption /
     * assumed invariant was that the byte buffer offset in the file matches file offset - byte buffer size.
     * But, the direct reading of bytes forwarded the file offset without clearing the byte buffer, therefore
     * seeking back inside the byte buffer is now WRONG! */
    bitReader.seek( oneByteBeforeByteBufferEnd );
    REQUIRE_EQUAL( bitReader.tell(), oneByteBeforeByteBufferEnd );
    REQUIRE_EQUAL( bitReader.read<8>(), static_cast<uint8_t>( firstDummy ) );
}


int
main( int    argc,
      char** argv )
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    testMSBBitReader();
    testLSBBitReader();
    testMSBBitReaderPeek();
    testLSBBitReaderPeek();

    testBitReader<true>();
    testBitReader<false>();

    const std::string binaryFilePath( argv[0] );
    std::string binaryFolder = ".";
    if ( const auto lastSlash = binaryFilePath.find_last_of( '/' ); lastSlash != std::string::npos ) {
        binaryFolder = std::string( binaryFilePath.begin(),
                                    binaryFilePath.begin() + static_cast<std::string::difference_type>( lastSlash ) );
    }
    const auto testFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/random-128KiB.gz" )
        ) / "src" / "tests" / "data";

    testDirectFileReadingBitReaderBug( testFolder / "random-128KiB.gz" );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
