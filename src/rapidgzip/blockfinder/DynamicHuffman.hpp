#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <BitReader.hpp>
#include <huffman/HuffmanCodingCheckOnly.hpp>

#include "deflate.hpp"
#include "Error.hpp"
#include "precodecheck/WalkTreeLUT.hpp"


namespace rapidgzip::blockfinder
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
 * Next would be the 3-bit precode code lengths. One or two alone does not allow any filtering at all.
 * I think starting from three, it might become possible, e.g., if any two are 1, then all others must
 * be of 0-length because the tree is filled already.
 */
template<uint8_t bitCount>
constexpr bool
isDeflateCandidate( uint32_t bits )
{
    if constexpr ( bitCount == 0 ) {
        return false;
    } else {
        /* Bit 0: final block flag */
        const auto isLastBlock = ( bits & 1U ) != 0;
        bits >>= 1U;
        bool matches = !isLastBlock;
        if constexpr ( bitCount <= 1U ) {
            return matches;
        }

        /* Bits 1-2: compression type */
        const auto compressionType = bits & nLowestBitsSet<uint32_t, 2U>();
        bits >>= 2U;
        matches &= ( compressionType & 1U ) == 0;
        if constexpr ( bitCount <= 2U ) {
            return matches;
        }
        matches &= compressionType == 0b10;

        /* Bits 3-7: code count */
        if constexpr ( bitCount < 1U + 2U + 5U ) {
            return matches;
        }
        const auto codeCount = bits & nLowestBitsSet<uint32_t, 5U>();
        bits >>= 5U;
        matches &= codeCount <= 29;

        /* Bits 8-12: distance count */
        if constexpr ( bitCount < 1U + 2U + 5U + 5U ) {
            return matches;
        }
        const auto distanceCodeCount = bits & nLowestBitsSet<uint32_t, 5U>();
        matches &= distanceCodeCount <= 29;
        return matches;
    }
}


constexpr uint32_t MAX_EVALUATED_BITS = 13;


template<uint8_t bitCount>
constexpr uint8_t
nextDeflateCandidate( uint32_t bits )
{
    if ( isDeflateCandidate<bitCount>( bits ) ) {
        return 0;
    }

    if constexpr ( bitCount == 0 ) {
        return 0;
    } else {
        return 1U + nextDeflateCandidate<bitCount - 1U>( bits >> 1U );
    }
}


template<uint8_t CACHED_BIT_COUNT,
         uint8_t MAX_CACHED_BIT_COUNT = CACHED_BIT_COUNT>
constexpr void
initializeMergedNextDeflateLUTs( std::array<uint8_t, ( 1U << MAX_CACHED_BIT_COUNT ) * 2U>& lut )
{
    constexpr auto size = 1U << CACHED_BIT_COUNT;
    constexpr auto offset = size;
    for ( uint32_t i = 0; i < size; ++i ) {
        lut[offset + i] = nextDeflateCandidate<CACHED_BIT_COUNT>( i );
    }

    if constexpr ( CACHED_BIT_COUNT > 1 ) {
        initializeMergedNextDeflateLUTs<CACHED_BIT_COUNT - 1, MAX_CACHED_BIT_COUNT>( lut );
    }
}


/**
 * Contains LUTs for all CACHED_BIT_COUNT in [1,13].
 * They are packed into the array like this: [+ ++ ++++ ++++++++], i.e.,
 * - at offset 2 is a the 2-element LUT for CACHED_BIT_COUNT == 1
 * - at offset 4 is a the 4-element LUT for CACHED_BIT_COUNT == 2
 * - at offset 8 is a the 8-element LUT for CACHED_BIT_COUNT == 3
 * - ...
 * - at offset 2^CACHED_BIT_COUNT is a the 2^CACHED_BIT_COUNT-element LUT for CACHED_BIT_COUNT
 */
static constexpr auto NEXT_DEFLATE_CANDIDATE_LUTS_UP_TO_13_BITS =
    [] ()
    {
        std::array<uint8_t, ( 1U << MAX_EVALUATED_BITS ) * 2U> result{};
        initializeMergedNextDeflateLUTs<MAX_EVALUATED_BITS>( result );
        return result;
    }();


/**
 * @note Using larger result types has no measurable difference, e.g., explained by easier access on 64-bit systems.
 *       But, it increases cache usage, so keep using the 8-bit result type.
 * @verbatim
 * 8-bit   [findDeflateBlocksRapidgzipLUT] ( 8.63 <= 8.7 +- 0.04 <= 8.75 ) MB/s
 * 16-bit  [findDeflateBlocksRapidgzipLUT] ( 8.31 <= 8.42 +- 0.12 <= 8.59 ) MB/s
 * 32-bit  [findDeflateBlocksRapidgzipLUT] ( 8.39 <= 8.53 +- 0.09 <= 8.71 ) MB/s
 * 64-bit  [findDeflateBlocksRapidgzipLUT] ( 8.618 <= 8.65 +- 0.02 <= 8.691 ) MB/s
 * @endverbatim
 */
template<uint8_t CACHED_BIT_COUNT>
static constexpr auto NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT =
    [] ()
    {
        std::array<uint8_t, 1U << CACHED_BIT_COUNT> result{};

    /* The recursive-like version that uses previous LUTs to calculate larger ones saves a tad of compile-time
     * but it yields to out-of-memory errors on the CI. Therefore, disable it for now. */
    #if 1
        for ( uint32_t i = 0; i < result.size(); ++i ) {
            result[i] = nextDeflateCandidate<CACHED_BIT_COUNT>( i );
        }
    #else
        const auto& SMALL_LUT = NEXT_DEFLATE_CANDIDATE_LUTS_UP_TO_13_BITS;

        /* nextDeflateCandidate only actually checks the first 13 bits, we can composite anything longer by looking
         * up 13-bits successively in the partial LUT to reduce constexpr instructions! */
        if constexpr ( CACHED_BIT_COUNT <= MAX_EVALUATED_BITS ) {
            for ( uint32_t i = 0; i < result.size(); ++i ) {
                result[i] = SMALL_LUT[result.size() + i];
            }
        } else {
            for ( size_t i = 0; i < result.size(); ++i ) {
                auto nextPosition = 0;
                for ( uint32_t bitsSkipped = 0; bitsSkipped < CACHED_BIT_COUNT - MAX_EVALUATED_BITS; ) {
                    const auto bitsToLookUpCount = std::min( MAX_EVALUATED_BITS, CACHED_BIT_COUNT - bitsSkipped );
                    const auto bitsToLookUp = ( i >> bitsSkipped ) & nLowestBitsSet<uint32_t>( bitsToLookUpCount );
                    auto possibleNextPosition = SMALL_LUT[( 1U << bitsToLookUpCount ) + bitsToLookUp];

                    nextPosition = possibleNextPosition + bitsSkipped;

                    if ( possibleNextPosition == 0 ) {
                        break;
                    }
                    bitsSkipped += nextPosition;
                }

                result[i] = nextPosition;
            }
        }
    #endif

        return result;
    }();


/**
 * @see benchmarkLUTSize
 * This highly depends on the implementation of the for loop over the bitReader.
 * - Earliest versions without checkPrecode showed best results for 18 bits for this LUT.
 * - Versions with checkPrecode showed best results for 16 bits.
 * - The version that keeps two bit buffers to avoid back-seeks was optimal with 13 bits probably
 *   because that saves an additional shift when moving bits from one bit buffer to the other while avoiding
 *   duplicated bits because there is no duplication for 13 bits.
 * - The version with manual bit buffers and HuffmanCodingReversedCodesPerLength for the precode
 *   is fastest with 15 bits.
 * - The version with manual bit buffers and no call to Block::readDynamicHuffmanCoding, which uses
 *   HuffmanCodingCheckOnly is fastest with 14 bits.
 */
static constexpr uint8_t OPTIMAL_NEXT_DEFLATE_LUT_SIZE = 14;


/**
 * Same as findDeflateBlocksRapidgzip but prefilters calling rapidgzip using a lookup table and even skips multiple bits.
 * Also, does not find uncompressed blocks nor fixed huffman blocks and as the others no final blocks!
 * The idea is that fixed huffman blocks should be very rare and uncompressed blocks can be found very fast in a
 * separate run over the data (to be implemented).
 *
 * @param untilOffset All returned matches will be smaller than this offset or `std::numeric_limits<size_t>::max()`.
 */
template<uint8_t CACHED_BIT_COUNT = OPTIMAL_NEXT_DEFLATE_LUT_SIZE>
[[nodiscard]] size_t
seekToNonFinalDynamicDeflateBlock( BitReader&   bitReader,
                                   size_t const untilOffset = std::numeric_limits<size_t>::max() )
{
    const auto oldOffset = bitReader.tell();

    try
    {
        using namespace rapidgzip::deflate;  /* For the definitions of deflate-specific number of bits. */

        /**
         * For LUT we need at CACHED_BIT_COUNT bits and for the precode check we would need in total
         * 13 + 4 + 57 = 74 bits. Because this does not fit into 64-bit we need to keep two sliding bit buffers.
         * The first can simply have length CACHED_BIT_COUNT and the other one can even keep duplicated bits to
         * have length of 61 bits required for the precode. Updating three different buffers would require more
         * instructions but might not be worth it.
         */
        auto bitBufferForLUT = bitReader.peek<CACHED_BIT_COUNT>();
        bitReader.seek( static_cast<long long int>( oldOffset ) + 13 );
        constexpr auto ALL_PRECODE_BITS = PRECODE_COUNT_BITS + MAX_PRECODE_COUNT * PRECODE_BITS;
        static_assert( ( ALL_PRECODE_BITS == 61 ) && ( ALL_PRECODE_BITS >= CACHED_BIT_COUNT )
                       && ( ALL_PRECODE_BITS <= std::numeric_limits<uint64_t>::digits )
                       && ( ALL_PRECODE_BITS <= rapidgzip::BitReader::MAX_BIT_BUFFER_SIZE ),
                       "It must fit into 64-bit and it also must fit the largest possible jump in the LUT." );
        auto bitBufferPrecodeBits = bitReader.read<ALL_PRECODE_BITS>();

        for ( size_t offset = oldOffset; offset < untilOffset; ) {
            auto nextPosition = NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT<CACHED_BIT_COUNT>[bitBufferForLUT];

            /* If we can skip forward, then that means that the new position only has been partially checked.
             * Therefore, rechecking the LUT for non-zero skips not only ensures that we aren't wasting time in
             * readHeader but it also ensures that we can avoid checking the first three bits again inside readHeader
             * and instead start reading and checking the dynamic Huffman code directly! */
            if ( nextPosition == 0 ) {
                nextPosition = 1;

                const auto next4Bits = bitBufferPrecodeBits & nLowestBitsSet<uint64_t, PRECODE_COUNT_BITS>();
                const auto next57Bits = ( bitBufferPrecodeBits >> PRECODE_COUNT_BITS )
                                        & nLowestBitsSet<uint64_t, MAX_PRECODE_COUNT * PRECODE_BITS>();

                using rapidgzip::PrecodeCheck::WalkTreeLUT::checkPrecode;
                const auto precodeError = checkPrecode( next4Bits, next57Bits );

                if ( UNLIKELY( precodeError == Error::NONE ) ) [[unlikely]] {
                #ifndef NDEBUG
                    const auto oldTell = bitReader.tell();
                #endif

                    const auto literalCodeCount = 257 + ( ( bitBufferForLUT >> 3U ) & nLowestBitsSet<uint64_t, 5>() );
                    const auto distanceCodeCount = 1 + ( ( bitBufferForLUT >> 8U ) & nLowestBitsSet<uint64_t, 5>() );
                    const auto codeLengthCount = 4 + next4Bits;
                    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );

                    /* Get code lengths (CL) for alphabet P. */
                    std::array<uint8_t, MAX_PRECODE_COUNT> codeLengthCL{};
                    for ( size_t i = 0; i < codeLengthCount; ++i ) {
                        const auto codeLength = ( precodeBits >> ( i * PRECODE_BITS ) )
                                                & nLowestBitsSet<uint64_t, PRECODE_BITS>();
                        codeLengthCL[PRECODE_ALPHABET[i]] = codeLength;
                    }

                    PrecodeHuffmanCoding precodeHC;
                    auto error = precodeHC.initializeFromLengths( { codeLengthCL.data(), codeLengthCL.size() } );

                    /* Note that the precode should never fail to initialize because checkPrecode
                     * already returned successful! */
                    LiteralAndDistanceCLBuffer literalCL{};
                    if ( LIKELY( error == Error::NONE ) ) [[likely]] {
                        bitReader.seek( static_cast<long long int>( offset )
                                        + 13 + 4 + ( codeLengthCount * PRECODE_BITS ) );
                        error = readDistanceAndLiteralCodeLengths(
                            literalCL, bitReader, precodeHC, literalCodeCount + distanceCodeCount );
                        /* Using this theoretically derivable position avoids a possibly costly call to tell()
                         * to save the old offset. */
                        bitReader.seek( static_cast<long long int>( offset ) + 13 + ALL_PRECODE_BITS );
                    }

                    if ( UNLIKELY( literalCL[deflate::END_OF_BLOCK_SYMBOL] == 0 ) ) [[unlikely]] {
                        error = Error::INVALID_CODE_LENGTHS;
                    }

                    /* Check distance code lengths. */
                    if ( UNLIKELY( error == Error::NONE ) ) [[unlikely]] {
                        HuffmanCodingCheckOnly<uint16_t, MAX_CODE_LENGTH,
                                               uint8_t, MAX_DISTANCE_SYMBOL_COUNT> distanceHC;
                        error = distanceHC.initializeFromLengths(
                            VectorView<uint8_t>( literalCL.data() + literalCodeCount, distanceCodeCount ) );
                    }

                    /* Check literal code lengths. */
                    if ( UNLIKELY( error == Error::NONE ) ) [[unlikely]] {
                        HuffmanCodingCheckOnly<uint16_t, MAX_CODE_LENGTH,
                                               uint16_t, MAX_LITERAL_HUFFMAN_CODE_COUNT> literalHC;
                        error = literalHC.initializeFromLengths(
                            VectorView<uint8_t>( literalCL.data(), literalCodeCount ) );
                    }

                    if ( UNLIKELY( error == Error::NONE ) ) [[unlikely]] {
                        /* Testing decoding is not necessary because the Huffman canonical check is already very strong!
                         * Decoding up to 8 KiB like in pugz only impedes performance and it is harder to reuse that
                         * already decoded data if we do decide that it is a valid block. The number of checks during
                         * reading is also pretty few because there almost are no wasted / invalid symbols. */
                        return offset;
                    }

                #ifndef NDEBUG
                    if ( oldTell != bitReader.tell() ) {
                        std::cerr << "Previous position: " << oldTell << " new position: " << bitReader.tell() << "\n";
                        throw std::logic_error( "Did not seek back correctly!" );
                    }
                #endif
                }
            }

            const auto bitsToLoad = nextPosition;

            /* Refill bit buffer for LUT using the bits from the higher precode bit buffer. */
            bitBufferForLUT >>= bitsToLoad;
            if constexpr ( CACHED_BIT_COUNT > 13 ) {
                constexpr uint8_t DUPLICATED_BITS = CACHED_BIT_COUNT - 13;
                bitBufferForLUT |= ( ( bitBufferPrecodeBits >> DUPLICATED_BITS )
                                     & nLowestBitsSet<uint64_t>( bitsToLoad ) )
                                   << static_cast<uint8_t>( CACHED_BIT_COUNT - bitsToLoad );
            } else {
                bitBufferForLUT |= ( bitBufferPrecodeBits & nLowestBitsSet<uint64_t>( bitsToLoad ) )
                                   << static_cast<uint8_t>( CACHED_BIT_COUNT - bitsToLoad );
            }

            /* Refill the precode bit buffer directly from the bit reader. */
            bitBufferPrecodeBits >>= bitsToLoad;
            bitBufferPrecodeBits |= bitReader.read( bitsToLoad )
                                    << static_cast<uint8_t>( ALL_PRECODE_BITS - bitsToLoad );

            offset += nextPosition;
        }
    } catch ( const BitReader::EndOfFileReached& ) {
        /* This might happen when calling readDynamicHuffmanCoding quite some bytes before the end! */
    }

    return std::numeric_limits<size_t>::max();
}
}  // rapidgzip::blockfinder
