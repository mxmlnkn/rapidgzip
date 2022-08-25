#pragma once

#include <atomic>
#include <limits>
#include <vector>

#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <common.hpp>
#include <definitions.hpp>


namespace pragzip::blockfinder
{
/**
 * This function searches for uncompressed blocks. It assumes a zero byte-padding between the uncompressed deflate
 * block header and the byte-aligned file size.
 * @return An inclusive range of possible offsets. Because of the byte-padding there might be multiple valid deflate
 *         block start points. Returns std::numeric_limits<size_t>::max if nothing was found.
 */
[[nodiscard]] std::pair<size_t, size_t>
seekToNonFinalUncompressedDeflateBlock( BitReader&   bitReader,
                                        size_t const untilOffset = std::numeric_limits<size_t>::max() )
{
    static constexpr auto DEFLATE_MAGIC_BIT_COUNT = 3U;

    try
    {
        const auto startOffset = bitReader.tell();
        for ( size_t offset = std::max( static_cast<size_t>( BYTE_SIZE ),
                                        ceilDiv( startOffset + DEFLATE_MAGIC_BIT_COUNT, BYTE_SIZE ) * BYTE_SIZE );
              offset < untilOffset; offset += BYTE_SIZE )
        {
            assert( offset % BYTE_SIZE == 0 );
            bitReader.seek( static_cast<long long int>( offset ) );

            /* We should be at a byte-boundary, so try reading the size. */
            const auto size = bitReader.peek<32>();
            if ( LIKELY( ( ( size ^ ( size >> 16U ) ) & nLowestBitsSet<uint32_t>( 16 ) ) != 0xFFFFU ) ) [[likely]] {
                continue;
            }

            /* This should happen rather rarely, at least for false positives. So, we can be a bit indulgent
             * and seek back possibly expensively to check the block header. Beware the bit order! They are
             * read and numbered from the lowest bits first, i.e., the three bits right before the size are
             * the three HIGHEST bits and the padding are the lower bits!
             * Beware that the starting offset might be up to 7 + 3 bits before the size offset!
             * 8 + 3 would be invalid because then the byte-aligned size would be 1 byte prior. */
            static constexpr auto MAX_PRECEDING_BITS = DEFLATE_MAGIC_BIT_COUNT + ( BYTE_SIZE - 1U );
            bitReader.seek( static_cast<long long int>( offset - MAX_PRECEDING_BITS ) );
            const auto previousBits = bitReader.peek<MAX_PRECEDING_BITS>();

            static constexpr auto MAGIC_BITS_MASK = 0b111ULL << ( MAX_PRECEDING_BITS - DEFLATE_MAGIC_BIT_COUNT );
            if ( LIKELY( ( previousBits & MAGIC_BITS_MASK ) != 0 ) ) [[likely]] {
                continue;
            }

            size_t trailingZeros = DEFLATE_MAGIC_BIT_COUNT;
            for ( size_t j = trailingZeros + 1; j <= MAX_PRECEDING_BITS; ++j ) {
                if ( ( previousBits & ( 1U << ( MAX_PRECEDING_BITS - j ) ) ) != 0 ) {
                    break;
                }
                trailingZeros = j;
            }

            if ( offset - DEFLATE_MAGIC_BIT_COUNT >= startOffset ) {
                return std::make_pair( offset - trailingZeros, offset - DEFLATE_MAGIC_BIT_COUNT );
            }
        }
    } catch ( const BitReader::EndOfFileReached& ) {
        /* This might happen when trying to read the 32 bits! */
    }

    return std::make_pair( std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() );
}
}  // namespace pragzip::blockfinder
