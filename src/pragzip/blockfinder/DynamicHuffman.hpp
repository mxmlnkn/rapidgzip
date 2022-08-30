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
 * Next would be the 3-bit precode code lengths. One or two alone does not allow any filtering at all.
 * I think starting from three, it might become possible, e.g., if any two are 1, then all others must
 * be of 0-length because the tree is filled already.
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


/**
 * @note Using larger result types has no measurable difference, e.g., explained by easier access on 64-bit systems.
 *       But, it increases cache usage, so keep using the 8-bit result type.
 * @verbatim
 * 8-bit   [findDeflateBlocksPragzipLUT] ( 8.63 <= 8.7 +- 0.04 <= 8.75 ) MB/s
 * 16-bit  [findDeflateBlocksPragzipLUT] ( 8.31 <= 8.42 +- 0.12 <= 8.59 ) MB/s
 * 32-bit  [findDeflateBlocksPragzipLUT] ( 8.39 <= 8.53 +- 0.09 <= 8.71 ) MB/s
 * 64-bit  [findDeflateBlocksPragzipLUT] ( 8.618 <= 8.65 +- 0.02 <= 8.691 ) MB/s
 * @endverbatim
 */
template<uint16_t CACHED_BIT_COUNT>
CONSTEXPR_EXCEPT_MSVC std::array<uint8_t, 1U << CACHED_BIT_COUNT>
createNextDeflateCandidateLUT()
{
    std::array<uint8_t, 1U << CACHED_BIT_COUNT> result{};
    for ( uint32_t i = 0; i < result.size(); ++i ) {
        result[i] = nextDeflateCandidate<CACHED_BIT_COUNT>( i );
    }
    return result;
}


/**
 * Requires at least 7 * 5 = 35 bits and 40 bits when also including the redundant zero-counts.
 * @note Theoretically, it could be even smaller but then we would have to check that it is a valid histogram
 *       before we have even created it, a typical bootstrapping problem.
 *       We would have to somehow introduce a checked addition, maybe with a different LUT for addition that sets
 *       a bit on overflows.
 *       We should be able to check for overflow on the least significant bit per frequency count by comparing with
 *       a "carryless bit-wise addition", i.e, 0+0 -> 1, 0+1 -> 1, 1+1 -> 0, i.e., a xor.
 *       Therefore, there would be an invalid overflow iff ( A + B ) & MASK_ALL_BUT_LEAST_SIGNIFICANT_BITS !=
 *       ( A ^ B ) & MASK_ALL_BUT_LEAST_SIGNIFICANT_BITS, which is equivalent to:
 *       [ ( A + B ) & ( A ^ B ) ] == MASK_ALL_BUT_LEAST_SIGNIFICANT_BITS
 *       With this, we could shrink CompressedHistogram by 6 bits (only 2 out of bits necessary for 1-counts, etc.)
 *       down to 34 or 26 bits at the cost of more complexity because the contents of CompressedHistogram are variable
 *       length.
 */
using CompressedHistogram = uint64_t;


/**
 * @param depth A depth of 1 means that we should iterate over 1-bit codes, which can only be 0,1,2.
 * @param freeBits This can be calculated from the histogram but it saves constexpr instructions when
 *        the caller updates this value outside.
 */
template<uint8_t FREQUENCY_BITS,
         uint8_t FREQUENCY_COUNT,
         uint8_t DEPTH = 1,
         typename LUT = std::array<uint64_t, ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) / 64U> >
constexpr void
createPrecodeFrequenciesValidLUTHelper( LUT&                      result,
                                        uint32_t const            remainingCount,
                                        CompressedHistogram const histogram = 0,
                                        uint32_t const            freeBits = 2 )
{
    static_assert( DEPTH <= FREQUENCY_COUNT, "Cannot descend deeper than the frequency counts!" );
    if ( ( histogram & nLowestBitsSet<uint64_t, ( DEPTH - 1 ) * FREQUENCY_BITS>() ) != histogram ) {
        throw std::invalid_argument( "Only frequency of bit-lengths less than the depth may be set!" );
    }

    const auto setValid =
        [&result] ( CompressedHistogram histogramToSetValid ) constexpr {
            result[histogramToSetValid / 64U] |= CompressedHistogram( 1 ) << ( histogramToSetValid % 64U );
        };

    const auto histogramWithCount =
        [histogram] ( uint32_t count ) constexpr {
            return histogram | ( count << ( ( DEPTH - 1 ) * FREQUENCY_BITS ) );
        };

    /* The for loop maximum is given by the invalid Huffman code check, i.e.,
     * when there are more code lengths on a tree level than there are nodes. */
    for ( uint32_t count = 0; count <= std::min( remainingCount, freeBits ); ++count ) {
        const auto newFreeBits = ( freeBits - count ) * 2;
        const auto newRemainingCount = remainingCount - count;

        if constexpr ( DEPTH == FREQUENCY_COUNT ) {
            /* This filters out bloating Huffman codes, i.e., when the number of free nodes in the tree
             * is larger than the maximum possible remaining (precode) symbols to fit into the tree. */
            if ( newFreeBits <= newRemainingCount ) {
                setValid( histogramWithCount( count ) );
            }
        } else {
            if ( count == freeBits ) {
                setValid( histogramWithCount( freeBits ) );
            } else {
                createPrecodeFrequenciesValidLUTHelper<FREQUENCY_BITS, FREQUENCY_COUNT, DEPTH + 1>(
                    result, newRemainingCount, histogramWithCount( count ), newFreeBits );
            }
        }
    }
}


/**
 * This alternative version tries to reduce the number of instructions required for creation so that it can be
 * used with MSVC, which is the worst in evaluating @ref createPrecodeFrequenciesValidLUT constexpr and runs into
 * internal compiler errors or out-of-heap-space errors.
 * This alternative implementation uses the fact only very few of the LUT entries are actually valid, meaning
 * we can reduce instructions by initializing to invalid and then iterating only over the valid possibilities.
 */
template<uint8_t FREQUENCY_BITS,
         uint8_t FREQUENCY_COUNT>
[[nodiscard]] constexpr auto
createPrecodeFrequenciesValidLUT()
{
    static_assert( ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) % 64U == 0,
                   "LUT size must be a multiple of 64-bit for the implemented bit-packing!" );
    std::array<uint64_t, ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) / 64U> result{};

    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    createPrecodeFrequenciesValidLUTHelper<FREQUENCY_BITS, FREQUENCY_COUNT>( result, MAX_CL_SYMBOL_COUNT );

    /* Set the special cases for zero counts and only one 1-bit lengths to valid even though they are not based
     * on the generic algorithm test because the tree is not full but there is no other valid way to represent
     * a one-symbol tree. */
    result[0] |= 0b11ULL;

    return result;
}


template<uint8_t FREQUENCY_BITS,
         uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr CompressedHistogram
calculateCompressedHistogram( uint64_t values )
{
    static_assert( VALUE_BITS * VALUE_COUNT < std::numeric_limits<decltype( values ) >::digits,
                   "Values type does not fit the requested amount of values and bits per value!" );
    static_assert( VALUE_COUNT < ( 1U << FREQUENCY_BITS ),
                   "The number of values might overflow the frequency type!" );
    static_assert( ( 1U << VALUE_BITS ) * FREQUENCY_BITS <= std::numeric_limits<CompressedHistogram>::digits,
                   "The maximum possible value might overflow the histogram bins!" );

    CompressedHistogram frequencies{ 0 };
    for ( size_t i = 0; i < static_cast<size_t>( VALUE_COUNT ); ++i ) {
        const auto value = ( values >> ( i * VALUE_BITS ) ) & nLowestBitsSet<CompressedHistogram, VALUE_BITS>();
        /* The frequencies are calculated in a SIMD like fashion assuming that there are no overflows! */
        frequencies += CompressedHistogram( 1 ) << ( value * FREQUENCY_BITS );
    }
    return frequencies;
}


template<uint8_t FREQUENCY_BITS,
         uint8_t VALUE_BITS,
         uint8_t MAX_VALUE_COUNT>
[[nodiscard]] constexpr auto
createCompressedHistogramLUT()
{
    std::array<CompressedHistogram, 1ULL << ( MAX_VALUE_COUNT * VALUE_BITS )> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = calculateCompressedHistogram<FREQUENCY_BITS, VALUE_BITS, MAX_VALUE_COUNT>( i );
    }
    return result;
}


[[nodiscard]] pragzip::Error
checkPrecode( pragzip::BitReader& bitReaderAtPrecode )
{
    const auto codeLengthCount = 4 + bitReaderAtPrecode.read<4>();

    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    constexpr auto CL_CODE_LENGTH_BIT_COUNT = 3U;
    using HuffmanCode = uint8_t;

    static_assert( MAX_CL_SYMBOL_COUNT * CL_CODE_LENGTH_BIT_COUNT <= pragzip::BitReader::MAX_BIT_BUFFER_SIZE,
                   "This optimization requires a larger BitBuffer inside BitReader!" );
    /* Avoid data dependency on codeLengthCount by always peeking the maximum possible amount of bits.
     * Note that >without the 64-bit gzip footer< this could be a wrong transformation because it wouldn't
     * be able to found very small deflate blocks close to the end of the file. Note that such very small
     * blocks would normally be Fixed Huffman decoding anyway. */
    const auto bits = bitReaderAtPrecode.peek<MAX_CL_SYMBOL_COUNT * CL_CODE_LENGTH_BIT_COUNT /* 57 */>()
                      & nLowestBitsSet<uint64_t>( codeLengthCount * CL_CODE_LENGTH_BIT_COUNT );
    using Bits = std::decay_t<decltype( bits )>;

    /* Maximum number of code lengths / values is 19 -> 5 bit (up to 31 count) is sufficient.
     * Note that how we create our LUT can lead to larger counts for 0 because of padding!
     * Here we cache 4 values at a time, i.e., we have to do 5 LUT lookups, which requires
     * padding the input by one value, i.e., the maximum count can be 20 for the value 0! */
    constexpr auto FREQUENCY_BITS = 5U;
    /* Max values to cache in LUT (4 * 3 bits = 12 bits LUT key -> 2^12 * 8B = 32 KiB LUT size) */
    constexpr auto MAX_CACHED_PRECODE_VALUES = 4U;
    static auto PRECODE_FREQUENCIES_LUT =
        createCompressedHistogramLUT<FREQUENCY_BITS, CL_CODE_LENGTH_BIT_COUNT, MAX_CACHED_PRECODE_VALUES>();

    constexpr auto CACHED_BITS = CL_CODE_LENGTH_BIT_COUNT * MAX_CACHED_PRECODE_VALUES;  // 12
    const auto bitLengthFrequencies =
        PRECODE_FREQUENCIES_LUT[bits & nLowestBitsSet<Bits, CACHED_BITS>()]
        + PRECODE_FREQUENCIES_LUT[( bits >> ( 1U * CACHED_BITS ) ) & nLowestBitsSet<Bits, CACHED_BITS>()]
        + PRECODE_FREQUENCIES_LUT[( bits >> ( 2U * CACHED_BITS ) ) & nLowestBitsSet<Bits, CACHED_BITS>()]
        + PRECODE_FREQUENCIES_LUT[( bits >> ( 3U * CACHED_BITS ) ) & nLowestBitsSet<Bits, CACHED_BITS>()]
        /* The last requires no bit masking because BitReader::read already has masked to the lowest 57 bits
         * and this shifts 48 bits to the right, leaving only 9 (<12) bits set anyways. */
        + PRECODE_FREQUENCIES_LUT[( bits >> ( 4U * CACHED_BITS ) )];

    /* Now, let's use a LUT to get a simple true/false whether the code is valid or not.
     * We cannot separate the kind of errors (invalid/bloating) but speed matters more.
     * 1. The bitLengthFrequencies consists of 8 5-bit counts. This results in 2^40 possible values.
     * 2. The value can be stored in 1-bit, which yields a size of 2^40 b = 2^37 B large = 2^7 GiB = 128 GiB.
     * 3. The zero-count is redundant and the non-zero count can also be calculated by taking the sum of
     *    all other counts. This reduces the possible values by another **5** bits to 2^2 GiB.
     * 4. The frequencies for lower counts can be prefiltered easily with a bit mask to basically force
     *    the 5-bit counts into 4,3, or 2 bits for code lengths 3,2, or 1. This saves another 3+2+1 = **6** bits
     *    yielding a size of 2^6 MiB = 64 MiB = 2^29 b.
     * This is still too large. I see these possibilities:
     * 1. Omit 2 further frequencies, the largest, because they are most likely to be correct and then use the
     *    LUT to prefilter and after that do the full check for possibly valid candidates. This yields a LUT
     *    size of 2^19 b = 2^16 B = 2^6 KiB = 64 KiB. This is still pretty large compared to L1-cache.
     * 2. Do a two-staged lookup. The first LUT would have to save the unusedSymbolCount additionally to
     *    the fail bit. Note that the unusedSymbolCount must always be <= 19 (5-bits). And if have only used
     *    the LUT for the first 3 code lengths than it can only be 8 at maximum.
     *    Note that if it is zero, then all other code lengths must also be zero, which is easily checked.
     * 3. Try to reduce it by another 4-bit by checking the cases of the count of code length n being exactly
     *    2^n because in this case all other bits would have to be 0. But how to do it fast with bit magic?
     *    - I might be able to check it fast because I have the non-zero counts, which would have to be a power
     *      of 2. Then, bitLengthFrequencies without zero counts should be equal that power of 2 shifted by
     *      its log2, i.e., the number of zero bits in it. That would be equal to (2^n)^(log2 2^n) = 2^(2n).
     *      Is there bit magic to square a power of 2? Another LUT? Might be rather small though because it
     *      only has to go up to 4-bits and store only 16-bit values if compressed right but even with 32-bits,
     *      it would only be 2^4 * (32/8) = 64 B large? We might even encode it smaller because actually we only
     *      need a kind of popc LUT that gives us the zero-bit count and a bool whether it is a power of 2.
     *    - Could use a LUT using the 0-length counts, from which the non-zero counts, can be derived. From
     *      that map to a bit mask that defines forbidden bits such that ( bits & mask == bits ). This would
     *      require 2^5 * 2^3 B = 2^8 = 256 B. As opposed to the above, the LUT could store much more
     *      information than simple popc or square functions. It could also encode further information.
     *      E.g., for 4 non-zero values, we know that the tree only should have a depth of at maximum 4 to
     *      not be wasteful and it also would limit the frequency lengths of other entries. In general, if
     *      the total count is smaller than 16, then we can shave off 1-bit of every frequency count!
     *      CL counts can be in [4,19], so 16 possible values. By not allowing 16,17,18,19, this means
     *      that 12/16 = 75% of the cases we could shave 1 bit off the higher frequency counts that we did
     *      not already account for, i.e., for all those > 3, so **4** further bits! The problem is that it is
     *      hard to shave off those bits. Should be doable with https://www.felixcloutier.com/x86/pext !
     *    But, even with 4 bits less, this still would result in a large 4 MiB LUT.
     * 4. We could use different LUTs to check parts of the histogram independently. This also would require
     *    doing a full check thereafter to be sure but might filter enough to speed things up. We would not
     *    be able to check many together though. Checking 3 values together would result in 2^15 b = 2^2 KiB.
     *    We have 7 values to check. Note that this almost boils down to a combination of possibility 1 and 2
     *    and can be combined with 3 to check more values together by using a another LUT to shift them
     *    together first.
     */

    static constexpr auto PRECODE_FREQUENCIES_VALID_LUT = createPrecodeFrequenciesValidLUT<5, 5>();
    static constexpr auto INDEX_BIT_COUNT = 5 * 5 - 6 /* log2 64 = 6 */;
    const auto valueToLookUp = bitLengthFrequencies >> FREQUENCY_BITS;
    const auto bitIndex = valueToLookUp % 64;
    const auto elementIndex = ( valueToLookUp / 64 ) & nLowestBitsSet<CompressedHistogram, INDEX_BIT_COUNT>();
    if ( ( PRECODE_FREQUENCIES_VALID_LUT[elementIndex] & ( 1ULL << bitIndex ) ) == 0 ) {
        /* Might also be bloating not only invalid. */
        return pragzip::Error::INVALID_CODE_LENGTHS;
    }

    const auto zeroCounts = bitLengthFrequencies & nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS>();
    const auto nonZeroCount = 5U * MAX_CACHED_PRECODE_VALUES - zeroCounts;

    /* Note that bitLengthFrequencies[0] must not be checked because multiple symbols may have code length
     * 0 simply when they do not appear in the text at all! And this may very well happen because the
     * order for the code lengths per symbol in the bit stream is fixed. */

    bool invalidCodeLength{ false };
    HuffmanCode unusedSymbolCount{ 2 };
    for ( size_t bitLength = 1; bitLength < ( 1U << CL_CODE_LENGTH_BIT_COUNT ); ++bitLength ) {
        const auto frequency = ( bitLengthFrequencies >> ( bitLength * FREQUENCY_BITS ) )
                               & nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS>();
        invalidCodeLength |= frequency > unusedSymbolCount;
        unusedSymbolCount -= frequency;
        unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
    }
    if ( invalidCodeLength ) {
        return pragzip::Error::INVALID_CODE_LENGTHS;
    }

    /* Using bit-wise 'and' and 'or' to avoid expensive branching does not improve performance measurably.
     * It is likely that GCC 11 already does the same optimization because it can deduce that the branched
     * comparison have no side-effects. Therefore, keep using logical operations because they are more
     * readable. Note that the standard defines bool to int conversion as true->1, false->0. */
    if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount >  1 ) ) ||
         ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
        return pragzip::Error::BLOATING_HUFFMAN_CODING;
    }

    return pragzip::Error::NONE;
}


static constexpr uint8_t OPTIMAL_NEXT_DEFLATE_LUT_SIZE = 16;  /** @see benchmarkLUTSize */


/**
 * Same as findDeflateBlocksPragzip but prefilters calling pragzip using a lookup table and even skips multiple bits.
 * Also, does not find uncompressed blocks nor fixed huffman blocks and as the others no final blocks!
 * The idea is that fixed huffman blocks should be very rare and uncompressed blocks can be found very fast in a
 * separate run over the data (to be implemented).
 *
 * @param untilOffset All returned matches will be smaller than this offset or `std::numeric_limits<size_t>::max()`.
 */
template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] size_t
seekToNonFinalDynamicDeflateBlock( BitReader&   bitReader,
                                   size_t const untilOffset = std::numeric_limits<size_t>::max() )
{
    static const auto NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT = createNextDeflateCandidateLUT<CACHED_BIT_COUNT>();

    try
    {
        deflate::Block block;
        for ( size_t offset = bitReader.tell(); offset < untilOffset; ) {
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

            /* Ignore 13 bits of deflate format data and huffman coding lengths other than the precode coding.
             * Note that this is only a prefiltering. It can be removed without any effects on correctness. */
            static_assert( CACHED_BIT_COUNT >= 13, "This implementation is optimized for LUTs with at least 13 bits!" );
            bitReader.seekAfterPeek( 13 );
            if ( checkPrecode( bitReader ) != pragzip::Error::NONE ) {
                ++offset;
                continue;
            }

            /* Ignore 3 bits of deflate format data for final block and compression type flags. */
            bitReader.seek( static_cast<long long int>( offset ) + 3 );
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
