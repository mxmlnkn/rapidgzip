/**
 * @file This was the first implementation of a LUT to check histograms with 5-bits per count for validity.
 *       It is inferior because it checks all x million possibilities even though only ~1000 are valid and the
 *       rest could be initialized as invalid with a simply memset.
 */

#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <BitManipulation.hpp>
#include <common.hpp>
#include <Error.hpp>


namespace rapidgzip::PrecodeCheck::BruteForceLUT
{
using CompressedHistogram = uint64_t;


/**
 * @note This check was pulled from HuffmanCodingBase::checkCodeLengthFrequencies
 * @param frequencies Stores FREQUENCY_COUNT FREQUENCY_BITS-bit-sized values starting with the counts for length 1.
 *                           The zero-counts are omitted in the histogram!
 */
template<uint8_t FREQUENCY_BITS,
         uint8_t FREQUENCY_COUNT>
[[nodiscard]] rapidgzip::Error
checkPrecodeFrequencies( CompressedHistogram frequencies )
{
    static_assert( FREQUENCY_COUNT <= 7, "Precode code lengths go only up to 7!" );
    static_assert( FREQUENCY_COUNT * FREQUENCY_BITS <= std::numeric_limits<CompressedHistogram>::digits,
                   "Argument type does not fit as many values as to be processed!" );

    /* If all counts are zero, then either it is a valid frequency distribution for an empty code or we are
     * missing some higher counts and cannot determine whether the code is bloating because all missing counts
     * might be zero, which is a valid special case. It is also not invalid because counts are the smallest they
     * can be.
     * Similarly, the special case of only a single symbol encoded in 1-bit is also valid because there is no
     * valid (non-bloating) way to encode it! */
    constexpr auto bitsToProcessMask = nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS * FREQUENCY_COUNT>();
    if ( UNLIKELY( ( frequencies & bitsToProcessMask ) == 1 ) ) [[unlikely]] {
        return rapidgzip::Error::NONE;
    }

    const auto getCount =
        [] ( const CompressedHistogram histogram,
             const uint8_t             bitLength )
        {
            return ( histogram >> ( ( bitLength - 1U ) * FREQUENCY_BITS ) )
                   & nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS>();
        };

    /* Because we do not know actual total count, we have to assume the most relaxed check for the bloating check. */
    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    auto remainingCount = MAX_CL_SYMBOL_COUNT;

    uint8_t unusedSymbolCount{ 2 };
    for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT; ++bitLength ) {
        const auto frequency = getCount( frequencies, bitLength );
        if ( frequency > unusedSymbolCount ) {
            return rapidgzip::Error::INVALID_CODE_LENGTHS;
        }

        unusedSymbolCount -= frequency;
        unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */

        remainingCount -= frequency;

        if ( unusedSymbolCount > remainingCount ) {
            return rapidgzip::Error::BLOATING_HUFFMAN_CODING;
        }
    }

    /* In the deepest possible layer, we can do a more rigorous check against non-optimal huffman codes. */
    if constexpr ( FREQUENCY_COUNT == 7 ) {
        uint64_t nonZeroCount{ 0 };
        for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT; ++bitLength ) {
            const auto frequency = getCount( frequencies, bitLength );
            nonZeroCount += frequency;
        }

        if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount >  1 ) ) ||
             ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
            return rapidgzip::Error::BLOATING_HUFFMAN_CODING;
        }

        if ( nonZeroCount == 0 ) {
            return rapidgzip::Error::EMPTY_ALPHABET;
        }
    }

    return rapidgzip::Error::NONE;
}


/**
 * This older, alternative precode frequency check LUT creation is thousands of times slower and requires much
 * more heap space during compilation than the newer one when made constexpr! Therefore, use the newer better
 * constexpr version and keep this test to check at test runtime whether the newer and alternative LUT creation
 * functions yield identical results.
 */
template<uint32_t FREQUENCY_BITS,
         uint32_t FREQUENCY_COUNT>
[[nodiscard]] auto
createPrecodeFrequenciesValidLUT()
{
    static_assert( ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) % 64U == 0,
                   "LUT size must be a multiple of 64-bit for the implemented bit-packing!" );
    std::array<uint64_t, ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) / 64U> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        for ( size_t j = 0; j < 64U; ++j ) {
            const auto isValid = checkPrecodeFrequencies<FREQUENCY_BITS, FREQUENCY_COUNT>( i * 64U + j )
                                 == rapidgzip::Error::NONE;
            result[i] |= static_cast<uint64_t>( isValid ) << j;
        }
    }
    return result;
}
}  // namespace rapidgzip::PrecodeCheck::BruteForceLUT
