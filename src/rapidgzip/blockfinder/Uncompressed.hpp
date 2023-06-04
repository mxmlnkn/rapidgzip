#pragma once

#include <algorithm>
#include <limits>
#include <utility>

#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <common.hpp>
#include <definitions.hpp>


namespace rapidgzip::blockfinder
{
/**
 * This function searches for uncompressed blocks. It assumes a zero byte-padding between the uncompressed deflate
 * block header and the byte-aligned file size.
 * @return An inclusive range of possible offsets. Because of the byte-padding there might be multiple valid deflate
 *         block start points. Returns std::numeric_limits<size_t>::max if nothing was found.
 */
[[nodiscard]] inline std::pair<size_t, size_t>
seekToNonFinalUncompressedDeflateBlock( BitReader&   bitReader,
                                        size_t const untilOffset = std::numeric_limits<size_t>::max() )
{
    static constexpr auto DEFLATE_MAGIC_BIT_COUNT = 3U;

    /* Beware that the starting offset might be up to 7 + 3 bits before the size offset!
     * 8 + 3 would be invalid because then the byte-aligned size would be 1 byte prior. */
    static constexpr uint32_t MAX_PRECEDING_BITS = DEFLATE_MAGIC_BIT_COUNT + ( BYTE_SIZE - 1U );
    static constexpr uint32_t MAX_PRECEDING_BYTES = ceilDiv( MAX_PRECEDING_BITS, BYTE_SIZE ) * BYTE_SIZE;

    try
    {
        const auto untilOffsetSizeMember = std::min(
            bitReader.size(),
            untilOffset >= std::numeric_limits<size_t>::max() - MAX_PRECEDING_BYTES
            ? std::numeric_limits<size_t>::max()
            : untilOffset + MAX_PRECEDING_BYTES
        );

        const auto startOffset = bitReader.tell();
        /* Align to byte because we begin checking there instead of the deflate magic bits. */
        const auto startOffsetByte = std::max(
            static_cast<size_t>( BYTE_SIZE ),
            ceilDiv( startOffset + DEFLATE_MAGIC_BIT_COUNT, BYTE_SIZE ) * BYTE_SIZE );
        if ( startOffsetByte < untilOffsetSizeMember ) {
            bitReader.seek( static_cast<long long int>( startOffsetByte ) );
        }

        auto size = bitReader.read<3U * BYTE_SIZE>() << BYTE_SIZE;
        for ( size_t offset = startOffsetByte; offset < untilOffsetSizeMember; offset += BYTE_SIZE ) {
            /* We should be at a byte-boundary, so try reading the size. */
            size = ( size >> BYTE_SIZE ) | ( bitReader.read<BYTE_SIZE>() << 3U * BYTE_SIZE );
            if ( LIKELY( ( ( size ^ ( size >> 16U ) ) & nLowestBitsSet<uint32_t>( 16 ) ) != 0xFFFFU ) ) [[likely]] {
                continue;
            }

            const auto oldOffset = offset + 4U * BYTE_SIZE;
            assert( oldOffset == bitReader.tell() );

            /* This should happen rather rarely, at least for false positives. So, we can be a bit indulgent
             * and seek back possibly expensively to check the block header. Beware the bit order! They are
             * read and numbered from the lowest bits first, i.e., the three bits right before the size are
             * the three HIGHEST bits and the padding are the lower bits! */
            bitReader.seek( static_cast<long long int>( offset - MAX_PRECEDING_BITS ) );
            const auto previousBits = bitReader.peek<MAX_PRECEDING_BITS>();

            static constexpr auto MAGIC_BITS_MASK = 0b111ULL << ( MAX_PRECEDING_BITS - DEFLATE_MAGIC_BIT_COUNT );
            if ( LIKELY( ( previousBits & MAGIC_BITS_MASK ) != 0 ) ) [[likely]] {
                bitReader.seek( oldOffset );
                continue;
            }

            size_t trailingZeros = DEFLATE_MAGIC_BIT_COUNT;
            for ( size_t j = trailingZeros + 1; j <= MAX_PRECEDING_BITS; ++j ) {
                if ( ( previousBits & ( 1U << ( MAX_PRECEDING_BITS - j ) ) ) != 0 ) {
                    break;
                }
                trailingZeros = j;
            }

            if ( ( offset - DEFLATE_MAGIC_BIT_COUNT >= startOffset ) && ( offset - trailingZeros < untilOffset ) ) {
                return std::make_pair( offset - trailingZeros, offset - DEFLATE_MAGIC_BIT_COUNT );
            }

            bitReader.seek( oldOffset );
        }
    } catch ( const BitReader::EndOfFileReached& ) {
        /* This might happen when trying to read the 32 bits! */
    }

    return std::make_pair( std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() );
}
}  // namespace rapidgzip::blockfinder
