#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <definitions.hpp>
#include <BitManipulation.hpp>
#include <Error.hpp>


namespace rapidgzip::PrecodeCheck::WalkTreeLUT
{
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


template<uint8_t FREQUENCY_BITS>
[[nodiscard]] constexpr CompressedHistogram
packHistogramWithNonZeroCount( const std::array<uint8_t, 7>& histogram )
{
    CompressedHistogram result{ 0 };
    for ( size_t i = 0; i < histogram.size(); ++i ) {
        result += static_cast<uint64_t>( histogram[i] ) << ( ( i + 1 ) * FREQUENCY_BITS );
        result += histogram[i];
    }
    return result;
}


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

    const auto processValidHistogram =
        [&result] ( CompressedHistogram histogramToSetValid ) constexpr {
            result[histogramToSetValid / 64U] |= CompressedHistogram( 1 ) << ( histogramToSetValid % 64U );
        };

    const auto histogramWithCount =
        [histogram] ( auto count ) constexpr {
            return histogram | ( static_cast<uint64_t>( count ) << ( ( DEPTH - 1 ) * FREQUENCY_BITS ) );
        };

    /* The for loop maximum is given by the invalid Huffman code check, i.e.,
     * when there are more code lengths on a tree level than there are nodes. */
    for ( uint32_t count = 0; count <= std::min( remainingCount, freeBits ); ++count ) {
        const auto newFreeBits = ( freeBits - count ) * 2;
        const auto newRemainingCount = remainingCount - count;

        /* The first layer may not be fully filled or even empty. This does not fit any of the general tests. */
        if constexpr ( DEPTH == 1 ) {
            if ( count == 1 ) {
                processValidHistogram( histogramWithCount( count ) );
            }
        }

        if constexpr ( DEPTH == FREQUENCY_COUNT ) {
            if constexpr ( DEPTH == 7 ) {
                if ( newFreeBits == 0 ) {
                    processValidHistogram( histogramWithCount( count ) );
                }
            } else {
                /* This filters out bloating Huffman codes, i.e., when the number of free nodes in the tree
                 * is larger than the maximum possible remaining (precode) symbols to fit into the tree. */
                if ( newFreeBits <= newRemainingCount ) {
                    processValidHistogram( histogramWithCount( count ) );
                }
            }
        } else {
            if ( count == freeBits ) {
                processValidHistogram( histogramWithCount( count ) );
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
    createPrecodeFrequenciesValidLUTHelper<FREQUENCY_BITS, FREQUENCY_COUNT>( result, deflate::MAX_PRECODE_COUNT );
    return result;
}


template<uint8_t FREQUENCY_BITS,
         uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr CompressedHistogram
calculateCompressedHistogram( uint64_t values )
{
    static_assert( VALUE_BITS * VALUE_COUNT <= std::numeric_limits<decltype( values )>::digits,
                   "Values type does not fit the requested amount of values and bits per value!" );
    static_assert( VALUE_COUNT < ( 1U << FREQUENCY_BITS ),
                   "The number of values might overflow the frequency type!" );
    static_assert( ( 1U << VALUE_BITS ) * FREQUENCY_BITS <= std::numeric_limits<CompressedHistogram>::digits,
                   "The maximum possible value might overflow the histogram bins!" );

    CompressedHistogram frequencies{ 0 };
    for ( size_t i = 0; i < static_cast<size_t>( VALUE_COUNT ); ++i ) {
        const auto value = ( values >> ( i * VALUE_BITS ) ) & nLowestBitsSet<CompressedHistogram, VALUE_BITS>();
        if ( value == 0 ) {
            continue;
        }
        /* The frequencies are calculated in a SIMD like fashion assuming that there are no overflows! */
        frequencies += CompressedHistogram( 1 ) << ( value * FREQUENCY_BITS );
        frequencies++;  /* increment non-zero count in lowest FREQUENCY_BITS */
    }
    return frequencies;
}


template<uint8_t FREQUENCY_BITS,
         uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr auto
createCompressedHistogramLUT()
{
    std::array<CompressedHistogram, 1ULL << ( VALUE_COUNT * VALUE_BITS )> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = calculateCompressedHistogram<FREQUENCY_BITS, VALUE_BITS, VALUE_COUNT>( i );
    }
    return result;
}


/* Maximum number of code lengths / values is 19 -> 5 bit (up to 31 count) is sufficient.
 * Note that how we create our LUT can lead to larger counts for 0 because of padding!
 * Here we cache 4 values at a time, i.e., we have to do 5 LUT lookups, which requires
 * padding the input by one value, i.e., the maximum count can be 20 for the value 0! */
constexpr auto UNIFORM_FREQUENCY_BITS = 5U;
constexpr auto PRECODE_BITS = rapidgzip::deflate::PRECODE_BITS;

/* Max values to cache in LUT (4 * 3 bits = 12 bits LUT key -> 2^12 * 8B = 32 KiB LUT size) */
template<uint8_t PRECODE_CHUNK_SIZE>
static constexpr auto PRECODE_TO_FREQUENCIES_LUT =
    createCompressedHistogramLUT<UNIFORM_FREQUENCY_BITS, PRECODE_BITS, PRECODE_CHUNK_SIZE>();


template<uint8_t PRECODE_CHUNK_SIZE>
[[nodiscard]] constexpr CompressedHistogram
precodesToHistogram( uint64_t precodeBits )
{
    constexpr auto& LUT = PRECODE_TO_FREQUENCIES_LUT<PRECODE_CHUNK_SIZE>;
    constexpr auto CACHED_BITS = PRECODE_BITS * PRECODE_CHUNK_SIZE;  // 12
    return LUT[precodeBits & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           + LUT[( precodeBits >> ( 1U * CACHED_BITS ) ) & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           + LUT[( precodeBits >> ( 2U * CACHED_BITS ) ) & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           + LUT[( precodeBits >> ( 3U * CACHED_BITS ) ) & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           /* The last requires no bit masking because BitReader::read already has masked to the lowest 57 bits
            * and this shifts 48 bits to the right, leaving only 9 (<12) bits set anyways. */
           + LUT[( precodeBits >> ( 4U * CACHED_BITS ) )];
}


/**
 * @todo Correctly parameterize everything so that it works with other values than 5.
 * 4 * 5 = 20 bits LUT map to bool, i.e., 2^17 B = 512 KiB! -> segfault
 * 5 * 5 = 25 bits LUT map to bool, i.e., 2^22 B =   4 MiB!
 * 6 * 5 = 30 bits LUT map to bool, i.e., 2^27 B =  32 MiB! -> testPrecodeCheck fails
 */
static constexpr auto PRECODE_FREQUENCIES_LUT_COUNT = 5U;
static constexpr auto PRECODE_FREQUENCIES_1_TO_5_VALID_LUT =
    createPrecodeFrequenciesValidLUT<UNIFORM_FREQUENCY_BITS, PRECODE_FREQUENCIES_LUT_COUNT>();


/**
 * @note Requires 4 (precode count) + 57 (maximum precode count * 3) bits to check for validity.
 *       Get all 57 bits at once to avoid a data dependency on the precode count. Note that this is only
 *       possible assuming a 64-bit gzip footer, else, this could be a wrong transformation because it wouldn't
 *       be able to find very small deflate blocks close to the end of the file. because they trigger an EOF.
 *       Note that such very small blocks would normally be Fixed Huffman decoding anyway.
 */
[[nodiscard]] constexpr rapidgzip::Error
checkPrecode( const uint64_t             next4Bits,
              const uint64_t             next57Bits,
              CompressedHistogram* const histogram = nullptr )
{
    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );
    const auto bitLengthFrequencies = precodesToHistogram<4>( precodeBits );

    /* Lookup in LUT and subtable (64 values in uint64_t) */
    const auto valueToLookUp = bitLengthFrequencies >> UNIFORM_FREQUENCY_BITS;  // ignore non-zero-counts;
    {
        const auto bitToLookUp = 1ULL << ( valueToLookUp % 64 );
        constexpr auto INDEX_BIT_COUNT = UNIFORM_FREQUENCY_BITS * PRECODE_FREQUENCIES_LUT_COUNT - 6 /* log2 64 = 6 */;
        const auto elementIndex = ( valueToLookUp / 64 ) & nLowestBitsSet<CompressedHistogram, INDEX_BIT_COUNT>();
        if ( ( PRECODE_FREQUENCIES_1_TO_5_VALID_LUT[elementIndex] & bitToLookUp ) == 0 ) {
            /* Might also be bloating not only invalid. */
            return rapidgzip::Error::INVALID_CODE_LENGTHS;
        }
    }

    const auto nonZeroCount = bitLengthFrequencies & nLowestBitsSet<CompressedHistogram, UNIFORM_FREQUENCY_BITS>();

    /* Note that bitLengthFrequencies[0] must not be checked because multiple symbols may have code length
     * 0 simply when they do not appear in the text at all! And this may very well happen because the
     * order for the code lengths per symbol in the bit stream is fixed. */
    bool invalidCodeLength{ false };
    uint32_t unusedSymbolCount{ 2 };
    constexpr auto MAX_LENGTH = ( 1U << PRECODE_BITS );
    for ( size_t bitLength = 1; bitLength < MAX_LENGTH; ++bitLength ) {
        const auto frequency = ( bitLengthFrequencies >> ( bitLength * UNIFORM_FREQUENCY_BITS ) )
                               & nLowestBitsSet<CompressedHistogram, UNIFORM_FREQUENCY_BITS>();
        invalidCodeLength |= frequency > unusedSymbolCount;
        unusedSymbolCount -= static_cast<uint32_t>( frequency );
        unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
    }
    if ( invalidCodeLength ) {
        return rapidgzip::Error::INVALID_CODE_LENGTHS;
    }

    /* Using bit-wise 'and' and 'or' to avoid expensive branching does not improve performance measurably.
     * It is likely that GCC 11 already does the same optimization because it can deduce that the branched
     * comparison have no side-effects. Therefore, keep using logical operations because they are more
     * readable. Note that the standard defines bool to int conversion as true->1, false->0. */
    if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount != ( 1U << ( MAX_LENGTH - 1U ) ) ) ) ||
         ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
        return rapidgzip::Error::BLOATING_HUFFMAN_CODING;
    }

    if ( nonZeroCount == 0 ) {
        return rapidgzip::Error::EMPTY_ALPHABET;
    }

    if ( histogram != nullptr ) {
        *histogram = bitLengthFrequencies;
    }
    return rapidgzip::Error::NONE;
}
}  // namespace rapidgzip::PrecodeCheck::WalkTreeLUT
