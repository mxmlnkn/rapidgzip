/**
 * @file This is an alternative lookup table to check the precode histogram for validity.
 *       It crams all necessary counts into 24 bits in order to not only have a partial LUT but a complete one,
 *       to save a branch for possibly valid cases.
 *       The bits were shaved off by specially accounting for overflows when adding up partial histograms.
 */

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

#include <BitManipulation.hpp>

#include "WalkTreeLUT.hpp"


namespace rapidgzip::PrecodeCheck::WalkTreeCompressedLUT
{
constexpr auto COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_CHUNK_COUNT = 16U;
constexpr auto COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_INDEX_BITS =
    requiredBits( COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_CHUNK_COUNT * sizeof( uint64_t ) * CHAR_BIT );

static const auto COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_VALID_LUT_DICT =
    [] ()
    {
        constexpr auto& LUT = PrecodeCheck::WalkTreeLUT::PRECODE_FREQUENCIES_1_TO_5_VALID_LUT;
        constexpr auto CHUNK_COUNT = COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_CHUNK_COUNT;
        constexpr auto LUT_SIZE = LUT.size();
        using ChunkedValues = std::array<uint64_t, CHUNK_COUNT>;
        static_assert( sizeof( ChunkedValues ) == sizeof( ChunkedValues::value_type ) * CHUNK_COUNT );

        /* Initialize with full zero values subtable at dictionary entry 0 to possible save a lookup.
         * Also, it is the most frequent case, so having it in the beginning keeps the linear search short. */
        std::map<ChunkedValues, size_t> valueToKey{ { ChunkedValues{}, 0 } };
        std::vector<uint8_t> dictionary( sizeof( ChunkedValues ) * CHAR_BIT, 0 );

        using Address = uint8_t;
        using CompressedLUT = std::array<Address, LUT_SIZE / CHUNK_COUNT>;
        CompressedLUT compressedLUT{};

        for ( size_t i = 0; i < LUT_SIZE; i += CHUNK_COUNT ) {
            ChunkedValues chunkedValues{};
            for ( size_t j = 0; j < CHUNK_COUNT; ++j ) {
                chunkedValues[j] = LUT[i + j];
            }

            const auto [match, wasInserted] = valueToKey.try_emplace( chunkedValues, valueToKey.size() );
            if ( wasInserted ) {
                for ( const auto bitMask : chunkedValues ) {
                    for ( size_t j = 0; j < sizeof( bitMask ) * CHAR_BIT; ++j ) {
                        dictionary.push_back( static_cast<uint8_t>( ( bitMask >> j ) & 1ULL ) );
                    }
                }
            }
            compressedLUT[i / CHUNK_COUNT] = match->second;
        }

        assert( valueToKey.size() < std::numeric_limits<CompressedLUT::value_type>::max() );

        return std::make_tuple( compressedLUT, dictionary );
    }();


/**
 * @note Requires 4 (precode count) + 57 (maximum precode count * 3) bits to check for validity.
 *       Get all 57 bits at once to avoid a data dependency on the precode count. Note that this is only
 *       possible assuming a 64-bit gzip footer, else, this could be a wrong transformation because it wouldn't
 *       be able to find very small deflate blocks close to the end of the file. because they trigger an EOF.
 *       Note that such very small blocks would normally be Fixed Huffman decoding anyway.
 */
[[nodiscard]] inline rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    using namespace rapidgzip::PrecodeCheck::WalkTreeLUT;

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );
    const auto bitLengthFrequencies = precodesToHistogram<4>( precodeBits );

    const auto valueToLookUp = bitLengthFrequencies >> UNIFORM_FREQUENCY_BITS;  // ignore non-zero-counts
    /* Lookup in LUT and subtable */
    {
        constexpr auto HISTOGRAM_TO_LOOK_UP_BITS = 5U * UNIFORM_FREQUENCY_BITS;
        const auto& [histogramLUT, validLUT] = COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_VALID_LUT_DICT;
        constexpr auto INDEX_BITS = COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_INDEX_BITS;
        const auto elementIndex = ( valueToLookUp >> INDEX_BITS )
                                  & nLowestBitsSet<CompressedHistogram>( HISTOGRAM_TO_LOOK_UP_BITS - INDEX_BITS );
        const auto subIndex = histogramLUT[elementIndex];

        /* We could do a preemptive return here for subIndex == 0 but it degrades performance by ~3%. */

        const auto validIndex = ( subIndex << INDEX_BITS ) + ( valueToLookUp & nLowestBitsSet<uint64_t>( INDEX_BITS ) );
        if ( LIKELY( ( validLUT[validIndex] ) == 0 ) ) [[unlikely]] {
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
        unusedSymbolCount -= frequency;
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

    return rapidgzip::Error::NONE;
}
}  // namespace rapidgzip::PrecodeCheck::WalkTreeCompressedLUT
