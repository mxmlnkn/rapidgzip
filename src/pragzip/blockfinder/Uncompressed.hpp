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
 *         block start points. Returns std::numeric_limits<size_t>::max if was found.
 */
[[nodiscard]] std::pair<size_t, size_t>
seekToNonFinalUncompressedDeflateBlock( BitReader&   bitReader,
                                        size_t const untilOffset = std::numeric_limits<size_t>::max() )
{
    try
    {
        const auto startOffset = bitReader.tell();
        for ( size_t offset = std::max( static_cast<size_t>( BYTE_SIZE ),
                                        ceilDiv( startOffset + 3U, BYTE_SIZE ) * BYTE_SIZE );
              offset < untilOffset; offset += BYTE_SIZE )
        {
            bitReader.seek( static_cast<long long int>( offset ) );

            /* We should be at a byte-boundary, so try reading the size. */
            const auto size = bitReader.peek<32>();
            if ( LIKELY( ( ( size ^ ( size >> 16U ) ) & nLowestBitsSet<uint32_t>( 16 ) ) != 0xFFFFU ) ) [[likely]] {
                continue;
            }

            /* This should happen rather rarely, at least for false positives. So, we can be a bit indulgent
             * and seek backpossibly expensively to check the block header. */
            bitReader.seek( static_cast<long long int>( offset - BYTE_SIZE ) );
            const auto previousByte = bitReader.peek<BYTE_SIZE>();
            if ( LIKELY( ( previousByte & 0b111U ) != 0 ) ) [[likely]] {
                continue;
            }

            uint8_t trailingZeros = 3;
            for ( uint8_t j = trailingZeros + 1; j <= 8; ++j ) {
                if ( ( previousByte & ( 1U << static_cast<uint8_t>( j - 1U ) ) ) == 0 ) {
                    trailingZeros = j;
                } else {
                    break;
                }
            }
            assert( trailingZeros >= 3U );
            if ( offset - 3U >= startOffset ) {
                return std::make_pair( offset - trailingZeros, offset - 3U );
            }
        }
    } catch ( const BitReader::EndOfFileReached& ) {
        /* This might happen when trying to read the 32 bits! */
    }

    return std::make_pair( std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() );
}
}  // namespace pragzip::blockfinder
