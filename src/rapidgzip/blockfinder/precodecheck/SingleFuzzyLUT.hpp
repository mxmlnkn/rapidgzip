/**
 * @file The idea is to compress the 2 MiB LUT from SingleLUT by further not caring about overflows and
 *       shaving off bits progressively. Anything is fine as long as it does not lead to false negatives.
 *       We are basically approaching a bloom filter at this point, in which the lookup hash is the variable
 *       bit length encoding of the histogram.
 * As there are 1526 valid histograms, assuming almost no overlaps and equal chance to hit any entry in the LUT,
 * we can estimate the filter rate as 1526 / LUT entries, e.g., 2.3% for 64 Ki entries.
 * Tests in testPrecodeCheck showed that there are ~10 or so overlaps already when pressing the 1526 valid histograms
 * into 16 bit by allowing overflows.
 * @todo Try two or more orthogonal smaller LUTs instead of one large LUT, similar to bloom filters.
 * @todo Quantify filter rate to get a feel why larger LUTs always seem to be better ...
 * @todo Analyze the valid histograms to get a feel what to test for. E.g., if a histogram bin can assume any
 *       value and have a corresponding valid histogram, then it makes no sense to test only that bin!
 * @todo Can I somehow look for an optimal combination of LUTs etc. in an automated manner? Genetic algorithms? AI?
 *       Exhaustive search?
 * @todo Heck, even the histogram computation with LUTs could be generically changed to anything, as long as we
 *       can exhaustively check all valid precode combinations, e.g., by starting from the valid histograms.
 *       But, it probably should be an operation that is invariant in terms of precode permutation because
 *       permutations do not matter for the check as the order only implies the symbol.
 */

#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <core/BitManipulation.hpp>

#include "SingleLUT.hpp"
#include "SingleCompressedLUT.hpp"
#include "WalkTreeLUT.hpp"



namespace rapidgzip::PrecodeCheck::SingleFuzzyLUT
{
using Histogram = deflate::precode::Histogram;

constexpr Histogram MEMBER_BIT_WIDTHS = {
    /* PACK_BITS = false */
    //1, 2, 3, 4, 4, 4, 4  // 22 bits, ~73 MB/s -> slightly faster than Single LUT and WalkTreeCompressedLUT!
    //3, 3, 3, 3, 3, 3, 3  // 21 bits, ~ 60 MB/s
    //5, 2, 3, 3, 3, 2, 2  // 20 bits, ~52 MB/s
    //3, 2, 3, 3, 3, 2, 2  // 18 bits, ~54 MB/s
    //2, 2, 3, 3, 3, 2, 2  // 17 bits, ~52 MB/s
    // 1, 2, 3, 3, 3, 2, 2  // 16 bits, ~43 MB/s

    /* PACK_BITS = true */
    //1, 2, 3, 4, 4, 4, 4  // 22 bits, ~71 MB/s slightly slower than non-bit-packed
    //1, 2, 3, 4, 4, 4, 3  // 21 bits, ~65 MB/s damn. That last high bit seems important
    //1, 2, 3, 4, 4, 4, 5  // 23 bits, ~71 MB/s
    //3, 3, 3, 4, 4, 4, 4  // 25 bits, ~74 MB/s -> new high
    4, 4, 4, 1, 1, 4, 4  // , ~74 MB/s -> new high

    //3, 3, 3, 3, 3, 3, 3  // 21 bits, ~ MB/s
    //5, 2, 3, 3, 3, 2, 2  // 20 bits, ~ MB/s
    //3, 2, 3, 3, 3, 2, 2  // 18 bits, ~ MB/s
    //2, 2, 3, 3, 3, 2, 2  // 17 bits, ~ MB/s
    // 1, 2, 3, 3, 3, 2, 2  // 16 bits, ~ MB/s
};


/* Compute a cumulative sum to get the offsets. */
static constexpr auto MEMBER_BIT_OFFSETS = [] () constexpr {
    std::decay_t<decltype( MEMBER_BIT_WIDTHS )> result{};
    uint8_t sum{ 0 };
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = sum;
        sum += MEMBER_BIT_WIDTHS[i];
    }
    return result;
}();


static constexpr auto HISTOGRAM_BIT_WIDTH = MEMBER_BIT_OFFSETS.back() + MEMBER_BIT_WIDTHS.back();

using CompressedHistogram =
    std::conditional_t<
        HISTOGRAM_BIT_WIDTH <= 8, std::uint8_t,
        std::conditional_t<
            HISTOGRAM_BIT_WIDTH <= 16, std::uint16_t,
            std::conditional_t<
                HISTOGRAM_BIT_WIDTH <= 32, std::uint32_t,
                std::conditional_t<
                    HISTOGRAM_BIT_WIDTH <= 64, std::uint64_t,
                    void // fallback for unsupported sizes
                >
            >
        >
    >;


[[nodiscard]] constexpr CompressedHistogram
packHistogram( const Histogram& histogram )
{
    if ( histogram.size() != MEMBER_BIT_WIDTHS.size() ) {
        throw std::logic_error( "" );
    }

    size_t width{ 0 };
    CompressedHistogram result{ 0 };
    for ( size_t i = 0; i < MEMBER_BIT_WIDTHS.size(); ++i ) {
        result |= histogram[i] << width;
        width += MEMBER_BIT_WIDTHS[i];
    }
    if ( width > sizeof( result ) * CHAR_BIT ) {
        throw std::logic_error( "Histogram bit widths do not fit the result type!" );
    }
    return result;
}


template<bool PACK_BITS>
static constexpr auto PRECODE_HISTOGRAM_VALID_LUT =
    [] ()
    {
        constexpr auto LUT_SIZE = 1ULL << ( HISTOGRAM_BIT_WIDTH - ( PACK_BITS ? 3U : 0U ) );
        std::array<uint8_t, LUT_SIZE> result{};
        for ( const auto& histogram : deflate::precode::VALID_HISTOGRAMS ) {
            const auto packedHistogram = packHistogram( histogram )
                                         & nLowestBitsSet<CompressedHistogram, HISTOGRAM_BIT_WIDTH>();
            if constexpr ( PACK_BITS ) {
                result[packedHistogram / 8U] |= uint64_t( 1 ) << ( packedHistogram % 8U );
            } else {
                result[packedHistogram] = 1;
            }
        }
        return result;
    }();


template<uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr CompressedHistogram
computeHistogram( uint64_t values )
{
    CompressedHistogram frequencies{ 0 };
    for ( size_t i = 0; i < static_cast<size_t>( VALUE_COUNT ); ++i ) {
        const auto value = ( values >> ( i * VALUE_BITS ) ) & nLowestBitsSet<CompressedHistogram, VALUE_BITS>();
        if ( value == 0 ) {
            continue;
        }
        /* The frequencies are calculated in a SIMD like fashion. Overflows may occur and are well-defined. */
        frequencies += CompressedHistogram( 1 ) << MEMBER_BIT_OFFSETS[value - 1];
    }
    return frequencies;
}


template<uint8_t PRECODE_CHUNK_SIZE>
static constexpr auto PRECODE_TO_FREQUENCIES_LUT =
    [] ()
    {
        std::array<CompressedHistogram, 1ULL << uint16_t( PRECODE_CHUNK_SIZE * deflate::PRECODE_BITS )> result{};
        for ( size_t i = 0; i < result.size(); ++i ) {
            result[i] = computeHistogram<deflate::PRECODE_BITS, PRECODE_CHUNK_SIZE>( i );
        }
        return result;
    }();


template<uint8_t PRECODE_CHUNK_SIZE,
         uint8_t MAX_PRECODE_COUNT = deflate::MAX_PRECODE_COUNT>
[[nodiscard]] constexpr forceinline CompressedHistogram
precodesToHistogram( uint64_t precodeBits )
{
    constexpr auto CACHED_BITS = deflate::PRECODE_BITS * PRECODE_CHUNK_SIZE;
    constexpr auto CHUNK_COUNT = ceilDiv( MAX_PRECODE_COUNT, PRECODE_CHUNK_SIZE );
    CompressedHistogram histogram{ 0 };
    for ( size_t chunk = 0; chunk < CHUNK_COUNT; ++chunk ) {
        auto precodeChunk = precodeBits >> ( chunk * CACHED_BITS );
        /* The last requires no bit masking because @ref next57Bits is already sufficiently masked.
         * This branch will hopefully get unrolled, else it could hinder performance. */
        if ( chunk != CHUNK_COUNT - 1 ) {
            precodeChunk &= nLowestBitsSet<uint64_t, CACHED_BITS>();
        }
        histogram = histogram + PRECODE_TO_FREQUENCIES_LUT<PRECODE_CHUNK_SIZE>[precodeChunk];
    }
    return histogram;
}


template<bool PACK_BITS = true>
[[nodiscard]] constexpr Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * deflate::PRECODE_BITS );
    const auto histogram = precodesToHistogram<4>( precodeBits )
                           & nLowestBitsSet<CompressedHistogram, HISTOGRAM_BIT_WIDTH>();

    constexpr auto& VALID_LUT = PRECODE_HISTOGRAM_VALID_LUT<PACK_BITS>;
    if constexpr ( PACK_BITS ) {
        if ( ( VALID_LUT[histogram >> 3U] & ( 1ULL << ( histogram & 0b111U ) ) ) != 0 ) {
            return Error::NONE;
        }
    } else {
        if ( VALID_LUT[histogram] ) {
            return Error::NONE;
        }
    }
    return Error::INVALID_CODE_LENGTHS;
}
}  // namespace rapidgzip::PrecodeCheck::SingleFuzzyLUT
