/**
 * @file This is an alternative lookup table to check the precode histogram for validity.
 *       Noticing that the WalkTreeLUT is mostly zeros, it tries to do some kind of fixed run-time-length
 *       compression by chunking 16 64-bit = 1024 result bits into a subtable and then does a deduplication
 *       on those 1024-bit subtables, most of which are hopefully completely zero.
 */

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

#include <core/BitManipulation.hpp>
#if ! defined( LIBRAPIDARCHIVE_SKIP_LOAD_FROM_CSV )
    #include <core/SimpleRunLengthEncoding.hpp>
#endif

#include "WalkTreeLUT.hpp"


namespace rapidgzip::PrecodeCheck::WalkTreeCompressedLUT
{
constexpr auto UNIFORM_FREQUENCY_BITS = WalkTreeLUT::UNIFORM_FREQUENCY_BITS;  // 5 (bits)
using CompressedHistogram = WalkTreeLUT::CompressedHistogram;  // uint64_t
constexpr auto PRECODE_BITS = rapidgzip::deflate::PRECODE_BITS;  // 19


/* Not constexpr because it would kill the compiler. Probably should simply be precomputed. */
template<uint8_t  PRECODE_FREQUENCIES_LUT_COUNT,
         uint16_t CHUNK_COUNT>
static const auto PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES =
    [] ()
    {
        static_assert( PRECODE_FREQUENCIES_LUT_COUNT <= deflate::MAX_PRECODE_LENGTH,
                      "A maximum histogram frequency bin than the maximum precode code length makes no sense." );

        constexpr auto LUT_SIZE = ( 1ULL << uint16_t( UNIFORM_FREQUENCY_BITS * PRECODE_FREQUENCIES_LUT_COUNT ) ) / 64U;
        static_assert( LUT_SIZE % CHUNK_COUNT == 0, "Bit-valued-LUT size must be a multiple of the chunk size!" );

        /**
         * The full LUT would become 4 GiB for PRECODE_FREQUENCIES_LUT_COUNT = 7U!
         * Therefore, store all valid histograms (1526) in a sorted vector and then iterate over them in lock step
         * as we iterate over all histograms. This enables O(1) lookup in this list!
         * It is complicated though, and becomes unnecessary when using precomputed LUTs, or when moving
         * to variable-length frequencies for further savings, which would enabled us to shave off at least
         * 6 bits, for 64x size reduction, i.e., 64 MiB memory usage for computing that LUT.
         * Unfortunately, the first tri with variable-length in SingleLUT did not yield a faster check!
         */
        std::vector<uint64_t> validHistograms;
        const auto processValidHistogram =
            [&validHistograms] ( CompressedHistogram histogramToSetValid ) {
                // LUT.at( histogramToSetValid / 64U ) |= CompressedHistogram( 1 ) << ( histogramToSetValid % 64U );
                validHistograms.push_back( histogramToSetValid );
            };
        WalkTreeLUT::walkValidPrecodeCodeLengthFrequencies<UNIFORM_FREQUENCY_BITS, PRECODE_FREQUENCIES_LUT_COUNT>(
            processValidHistogram, deflate::MAX_PRECODE_COUNT );
        std::sort( validHistograms.begin(), validHistograms.end() );

        using ChunkedValues = std::array<uint64_t, CHUNK_COUNT>;
        static_assert( sizeof( ChunkedValues ) == sizeof( typename ChunkedValues::value_type ) * CHUNK_COUNT );

        /* Initialize with full zero values subtable at dictionary entry 0 to possible save a lookup.
         * Also, it is the most frequent case, so having it in the beginning keeps the linear search short. */
        std::map<ChunkedValues, size_t> valueToKey{ { ChunkedValues{}, 0 } };
        std::vector<uint8_t> dictionary( sizeof( ChunkedValues ) * CHAR_BIT, 0 );
        constexpr auto CHUNK_BITS = sizeof( typename ChunkedValues::value_type ) * CHAR_BIT;

        using Address = uint8_t;
        std::vector<Address> compressedLUT( LUT_SIZE / CHUNK_COUNT, 0 );  /* Stores indexes into dictionary. */

        auto nextValidHistogram = validHistograms.begin();
        for ( size_t i = 0; i < LUT_SIZE; i += CHUNK_COUNT ) {
            /* Copy the current chunk from the LUT. */
            ChunkedValues chunkedValues{};
            for ( size_t j = 0; j < CHUNK_COUNT; ++j ) {
                for ( ; ( nextValidHistogram != validHistograms.end() )
                        && ( *nextValidHistogram >= ( i + j ) * CHUNK_BITS )
                        && ( *nextValidHistogram < ( i + j + 1 ) * CHUNK_BITS );
                     ++nextValidHistogram )
                {
                    const auto k = *nextValidHistogram - ( i + j ) * CHUNK_BITS;
                    chunkedValues[j] |= 1ULL << k;
                }
            }

            /* Check whether the current chunk has already been encountered. If so, return the existing index/pointer. */
            const auto [match, wasInserted] = valueToKey.try_emplace( chunkedValues, valueToKey.size() );
            if ( wasInserted ) {
                for ( const auto bitMask : chunkedValues ) {
                    for ( size_t j = 0; j < CHUNK_BITS; ++j ) {
                        dictionary.push_back( static_cast<uint8_t>( ( bitMask >> j ) & 1ULL ) );
                    }
                }
            }
            compressedLUT[i / CHUNK_COUNT] = match->second;
        }

        if ( valueToKey.size() >= std::numeric_limits<Address>::max() ) {
            throw std::logic_error( "Address too large for int type!" );
        }

        return std::make_tuple( compressedLUT, dictionary );
    }();


#if ! defined( LIBRAPIDARCHIVE_SKIP_LOAD_FROM_CSV )

/**
 * Load precomputed LUTs from run-length encoded CSV at compile-time for optimal case of template parameters.
 */
template<>
const auto PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<7, 512> =
    [] ()
    {
        constexpr std::array<uint8_t, 1576> histogramLUT = {
            #include "PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES_7_512_HISTOGRAM_TO_INDEX.csv"
        };
        constexpr std::array<uint8_t, 534> validLUT = {
            #include "PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES_7_512_INDEX_TO_VALID.csv"
        };

        return std::make_tuple(
            SimpleRunLengthEncoding::simpleRunLengthDecode<std::array<uint8_t, 1_Mi> >( histogramLUT, 1_Mi ),
            SimpleRunLengthEncoding::simpleRunLengthDecode<std::array<uint8_t, 832_Ki> >( validLUT, 832_Ki ) );
    }();
#endif


/**
 * @see WalkTreeLUT::checkPrecode, but this one uses the two-staged compressed LUT.
 */
template<uint8_t  PRECODE_FREQUENCIES_LUT_COUNT = 7U,
         uint16_t SUBTABLE_CHUNK_COUNT = 512U>
[[nodiscard]] inline rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    constexpr auto INDEX_BITS = requiredBits( SUBTABLE_CHUNK_COUNT * sizeof( uint64_t ) * CHAR_BIT );

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );
    const auto bitLengthFrequencies = WalkTreeLUT::precodesToHistogram<4>( precodeBits );

    const auto valueToLookUp = bitLengthFrequencies >> UNIFORM_FREQUENCY_BITS;  // ignore non-zero-counts
    /* Lookup in LUT and subtable */
    {
        constexpr auto HISTOGRAM_TO_LOOK_UP_BITS = PRECODE_FREQUENCIES_LUT_COUNT * UNIFORM_FREQUENCY_BITS;
        const auto& [histogramLUT, validLUT] =
            PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<PRECODE_FREQUENCIES_LUT_COUNT, SUBTABLE_CHUNK_COUNT>;
        const auto elementIndex = ( valueToLookUp >> INDEX_BITS )
                                  & nLowestBitsSet<CompressedHistogram>( HISTOGRAM_TO_LOOK_UP_BITS - INDEX_BITS );
        const auto subIndex = histogramLUT[elementIndex];
        /* This seems to help slightly (3%). */
        if ( subIndex == 0 ) {
            return rapidgzip::Error::INVALID_CODE_LENGTHS;
        }

        /* We could do a preemptive return here for subIndex == 0 but it degrades performance by ~3%. */

        const auto validIndex = ( subIndex << INDEX_BITS ) + ( valueToLookUp & nLowestBitsSet<uint64_t>( INDEX_BITS ) );
        if ( LIKELY( ( validLUT[validIndex] ) == 0 ) ) [[likely]] {
            /* Might also be bloating not only invalid. */
            return rapidgzip::Error::INVALID_CODE_LENGTHS;
        }
    }

    if constexpr ( PRECODE_FREQUENCIES_LUT_COUNT < deflate::MAX_PRECODE_LENGTH ) {
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
    }

    return rapidgzip::Error::NONE;
}
}  // namespace rapidgzip::PrecodeCheck::WalkTreeCompressedLUT
