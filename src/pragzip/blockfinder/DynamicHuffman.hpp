#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <BitReader.hpp>

#include "deflate.hpp"
#include "Error.hpp"


namespace pragzip::blockfinder
{
/**
 * Valid signature to look for deflate block:
 * - 0b0  Final Block: We ignore uninteresting final blocks (filter 50%)
 * - 0b10 Compression Type Dynamic Huffman (filters 75%)
 * - (Anything but 0b1111) + 1 bit
 *   Code Count 257 + (5-bit) <= 286, i.e., (5-bit) <= 29 (31 is 0b11111, 30 is 0b11110)
 *   (filters out 2 /32 = 6.25%)
 *   Beware that the >highest< 4 bits may not be 1 but this that we requrie all 5-bits to
 *   determine validity because they are lower significant first!
 * - (Anything but 0b1111) + 1 bit
 *   Distance Code Count 1 + (5-bits) <= 30 <=> (5-bits) <= 29 -> filters out 6.25%
 * The returned position is only 0 if all of the above holds for a bitCount of 13
 */
template<uint8_t bitCount>
constexpr uint8_t
nextDeflateCandidate( uint32_t bits )
{
    if constexpr ( bitCount == 0 ) {
        return 0;
    } else {
        const auto nextBlock = 1U + nextDeflateCandidate<bitCount - 1U>( bits >> 1U );

        /* Bit 0: final block flag */
        const auto isLastBlock = ( bits & 1U ) != 0;
        bits >>= 1U;
        bool matches = !isLastBlock;
        if constexpr ( bitCount <= 1U ) {
            return matches ? 0 : nextBlock;
        }

        /* Bits 1-2: compression type */
        const auto compressionType = bits & nLowestBitsSet<uint32_t, 2U>();
        bits >>= 2U;
        matches &= ( compressionType & 1U ) == 0;
        if constexpr ( bitCount <= 2U ) {
            return matches ? 0 : nextBlock;
        }
        matches &= compressionType == 0b10;

        /* Bits 3-7: code count */
        if constexpr ( bitCount < 1U + 2U + 5U ) {
            return matches ? 0 : nextBlock;
        }
        const auto codeCount = bits & nLowestBitsSet<uint32_t, 5U>();
        bits >>= 5U;
        matches &= codeCount <= 29;

        /* Bits 8-12: distance count */
        if constexpr ( bitCount < 1U + 2U + 5U + 5U ) {
            return matches ? 0 : nextBlock;
        }
        const auto distanceCodeCount = bits & nLowestBitsSet<uint32_t, 5U>();
        matches &= distanceCodeCount <= 29;
        return matches ? 0 : nextBlock;
    }
}



template<uint8_t CACHED_BIT_COUNT>
constexpr std::array<uint8_t, 1U << CACHED_BIT_COUNT>
createNextDeflateCandidateLUT()
{
    std::array<uint8_t, 1U << CACHED_BIT_COUNT> result{};
    for ( uint32_t i = 0; i < result.size(); ++i ) {
        result[i] = nextDeflateCandidate<CACHED_BIT_COUNT>( i );
    }
    return result;
}


/**
 * Same as findDeflateBlocksPragzip but prefilters calling pragzip using a lookup table and even skips multiple bits.
 * Also, does not find uncompressed blocks nor fixed huffman blocks and as the others no final blocks!
 * The idea is that fixed huffman blocks should be very rare and uncompressed blocks can be found very fast in a
 * separate run over the data (to be implemented).
 */
template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] size_t
seekToNonFinalDynamicDeflateBlock( BitReader& bitReader,
                                   size_t     nBitsToTest = std::numeric_limits<size_t>::max() )
{
    static constexpr auto NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT = createNextDeflateCandidateLUT<CACHED_BIT_COUNT>();

    try
    {
        deflate::Block block;
        for ( size_t offset = bitReader.tell(); offset < nBitsToTest; ) {
            bitReader.seek( static_cast<long long int>( offset ) );

            const auto peeked = bitReader.peek<CACHED_BIT_COUNT>();
            const auto nextPosition = NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT[peeked];

            /* If we can skip forward, then that means that the new position only has been partially checked.
             * Therefore, rechecking the LUT for non-zero skips not only ensures that we aren't wasting time in
             * readHeader but it also ensures that we can avoid checking the first three bits again inside readHeader
             * and instead start reading and checking the dynamic Huffman code directly! */
            if ( nextPosition > 0 ) {
                bitReader.seekAfterPeek( nextPosition );
                offset += nextPosition;
                continue;
            }

            bitReader.seekAfterPeek( 3 /* Ignore gzip format data for final block and compression type flags. */ );
            auto error = block.readDynamicHuffmanCoding( bitReader );
            if ( error != Error::NONE ) {
                ++offset;
                continue;
            }

            /* Testing decoding is not necessary because the Huffman canonical check is already very strong!
             * Decoding up to 8 kiB like in pugz only impedes performance and it is harder to reuse that already
             * decoded data if we do decide that it is a valid block. The number of checks during reading is also
             * pretty few because there almost are no wasted / invalid symbols. */
            return offset;
        }
    } catch ( const BitReader::EndOfFileReached& ) {
        /* This might happen when calling readDynamicHuffmanCoding quite some bytes before the end! */
    }

    return std::numeric_limits<size_t>::max();
}
}  // pragzip::blockfinder
