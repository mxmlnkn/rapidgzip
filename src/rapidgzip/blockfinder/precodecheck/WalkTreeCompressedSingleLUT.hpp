/**
 * @file Started out as WalkTreeCompressedLUT with FREQUENCY_COUNT 7U, i.e., a single LUT for the whole histogram.
 *       This file throws away support for the FREQUENCY_COUNT parameter and related code and tries to optimize it.
 * The main idea is to reduce the LUT even further from 1.8 MB by bit-packing again.
 * Because the non-zero counts are not used, we can adjust the precodesToHistogram to not include them to save
 * a single bit shift operation.
 */

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

#include <core/BitManipulation.hpp>

#include "WalkTreeLUT.hpp"


namespace rapidgzip::PrecodeCheck::WalkTreeCompressedSingleLUT
{
constexpr auto UNIFORM_FREQUENCY_BITS = WalkTreeLUT::UNIFORM_FREQUENCY_BITS;  // 5 (bits)
using CompressedHistogram = WalkTreeLUT::CompressedHistogram;  // uint64_t
constexpr auto PRECODE_BITS = rapidgzip::deflate::PRECODE_BITS;  // 19
constexpr auto HISTOGRAM_BITS = uint16_t( UNIFORM_FREQUENCY_BITS * deflate::MAX_PRECODE_LENGTH );


/* Not constexpr because it would kill the compiler. Probably should simply be precomputed. */
template<uint16_t CHUNK_COUNT>
static const auto PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES =
    [] ()
    {
        constexpr auto LUT_SIZE = ( 1ULL << HISTOGRAM_BITS ) / 64U;
        static_assert( LUT_SIZE % CHUNK_COUNT == 0, "Bit-valued-LUT size must be a multiple of the chunk size!" );

        /**
         * The full bit-packed LUT would become 4 GiB on the 35-bit histogram!
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
        WalkTreeLUT::walkValidPrecodeCodeLengthFrequencies<UNIFORM_FREQUENCY_BITS, deflate::MAX_PRECODE_LENGTH>(
            processValidHistogram, deflate::MAX_PRECODE_COUNT );
        std::sort( validHistograms.begin(), validHistograms.end() );

        using ChunkedValues = std::array<uint64_t, CHUNK_COUNT>;
        static_assert( sizeof( ChunkedValues ) == sizeof( typename ChunkedValues::value_type ) * CHUNK_COUNT );

        /* Initialize with full zero values subtable at dictionary entry 0 to possible save a lookup.
         * Also, it is the most frequent case, so having it in the beginning keeps the linear search short. */
        std::map<ChunkedValues, size_t> valueToKey{ { ChunkedValues{}, 0 } };
        std::vector<uint8_t> dictionary( sizeof( ChunkedValues ), 0 );
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
                    for ( size_t j = 0; j < CHUNK_BITS; j += CHAR_BIT ) {
                        dictionary.push_back( static_cast<uint8_t>( ( bitMask >> j ) & 0xFFULL ) );
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


template<uint8_t FREQUENCY_BITS,
         uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr auto
createCompressedHistogramLUT()
{
    std::array<CompressedHistogram, 1ULL << uint16_t( VALUE_COUNT * VALUE_BITS )> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = WalkTreeLUT::calculateCompressedHistogram<FREQUENCY_BITS, VALUE_BITS, VALUE_COUNT>( i )
                    >> FREQUENCY_BITS;  // Remove unused non-zero counts.
    }
    return result;
}


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
 * @see WalkTreeLUT::checkPrecode, but this one uses the two-staged compressed LUT.
 * @verbatim
 * [13 bits, Walk Tree Compressed Single LUT] ( 70.3 <= 72.6 +- 1.3 <= 74.6 ) MB/s
 * [14 bits, Walk Tree Compressed Single LUT] ( 70.1 <= 73.1 +- 1.9 <= 75.3 ) MB/s
 * [15 bits, Walk Tree Compressed Single LUT] ( 69.7 <= 72.9 +- 1.5 <= 74.4 ) MB/s
 * [16 bits, Walk Tree Compressed Single LUT] ( 70.7 <= 72.6 +- 1.0 <= 74.4 ) MB/s
 * [17 bits, Walk Tree Compressed Single LUT] ( 70.5 <= 71.7 +- 1.7 <= 74.7 ) MB/s
 * [18 bits, Walk Tree Compressed Single LUT] ( 71.2 <= 72.7 +- 0.6 <= 73.2 ) MB/s
 *
 * [13 bits, this with chunk count 1] ( 62.9 <= 64.5 +- 1.0 <= 65.5 ) MB/s
 * [14 bits, this with chunk count 1] ( 62.4 <= 64.5 +- 0.9 <= 65.1 ) MB/s
 * [15 bits, this with chunk count 1] ( 62.3 <= 64.0 +- 0.8 <= 64.7 ) MB/s
 * [16 bits, this with chunk count 1] ( 60.6 <= 63.5 +- 1.1 <= 64.1 ) MB/s
 * [17 bits, this with chunk count 1] ( 58.8 <= 62.3 +- 1.5 <= 63.2 ) MB/s
 * [18 bits, this with chunk count 1] ( 54.3 <= 58.0 +- 1.9 <= 61.2 ) MB/s
 *
 * [13 bits, this with chunk count 2] ( 55 <= 65 +- 4 <= 68 ) MB/s
 * [14 bits, this with chunk count 2] ( 64.1 <= 65.4 +- 1.2 <= 68.1 ) MB/s
 * [15 bits, this with chunk count 2] ( 62.6 <= 64.3 +- 1.6 <= 67.7 ) MB/s
 * [16 bits, this with chunk count 2] ( 63.1 <= 63.6 +- 0.4 <= 64.3 ) MB/s
 * [17 bits, this with chunk count 2] ( 63.0 <= 65.0 +- 1.5 <= 66.4 ) MB/s
 * [18 bits, this with chunk count 2] ( 61.3 <= 63.1 +- 1.7 <= 65.3 ) MB/s
 *
 * [13 bits, this with chunk count 8] ( 64.9 <= 71.4 +- 2.6 <= 74.4 ) MB/s
 * [14 bits, this with chunk count 8] ( 65.7 <= 69.5 +- 2.7 <= 73.6 ) MB/s
 * [15 bits, this with chunk count 8] ( 68.4 <= 71.8 +- 2.4 <= 74.0 ) MB/s
 * [16 bits, this with chunk count 8] ( 62.0 <= 68.9 +- 2.7 <= 71.1 ) MB/s
 * [17 bits, this with chunk count 8] ( 69.8 <= 71.1 +- 1.0 <= 73.4 ) MB/s
 * [18 bits, this with chunk count 8] ( 64.2 <= 66.9 +- 1.9 <= 70.2 ) MB/s
 *
 * [13 bits, this with chunk count 128] ( 65.1 <= 72.1 +- 2.6 <= 74.3 ) MB/s
 * [14 bits, this with chunk count 128] ( 69.1 <= 73.2 +- 1.7 <= 74.4 ) MB/s
 * [15 bits, this with chunk count 128] ( 73.45 <= 73.69 +- 0.15 <= 73.92 ) MB/s
 * [16 bits, this with chunk count 128] ( 70.1 <= 73.4 +- 1.3 <= 74.1 ) MB/s
 * [17 bits, this with chunk count 128] ( 72.1 <= 73.6 +- 1.2 <= 74.7 ) MB/s
 * [18 bits, this with chunk count 128] ( 69.6 <= 70.6 +- 1.0 <= 72.1 ) MB/s
 *
 * [13 bits, this with chunk count 256] ( 63.7 <= 70.4 +- 2.5 <= 72.1 ) MB/s
 * [14 bits, this with chunk count 256] ( 68.3 <= 72.6 +- 2.3 <= 74.5 ) MB/s
 * [15 bits, this with chunk count 256] ( 68.8 <= 71.1 +- 1.0 <= 71.9 ) MB/s
 * [16 bits, this with chunk count 256] ( 71.9 <= 73.4 +- 1.1 <= 74.6 ) MB/s
 * [17 bits, this with chunk count 256] ( 69.4 <= 71.0 +- 1.4 <= 74.5 ) MB/s
 * [18 bits, this with chunk count 256] ( 67.4 <= 68.5 +- 0.7 <= 70.1 ) MB/s
 *
 * [13 bits, this with chunk count 512] ( 70.7 <= 74.0 +- 1.8 <= 75.4 ) MB/s
 * [14 bits, this with chunk count 512] ( 70.7 <= 72.8 +- 1.6 <= 74.8 ) MB/s
 * [15 bits, this with chunk count 512] ( 69.3 <= 71.8 +- 1.0 <= 72.4 ) MB/s
 * [16 bits, this with chunk count 512] ( 70.2 <= 71.3 +- 1.0 <= 73.6 ) MB/s
 * [17 bits, this with chunk count 512] ( 72.26 <= 72.49 +- 0.13 <= 72.64 ) MB/s
 * [18 bits, this with chunk count 512] ( 67.1 <= 71.8 +- 2.2 <= 73.7 ) MB/s
 *
 * [13 bits, this with chunk count 1024] ( 59.9 <= 67.6 +- 2.8 <= 69.6 ) MB/s
 * [14 bits, this with chunk count 1024] ( 66.8 <= 67.6 +- 0.3 <= 67.9 ) MB/s
 * [15 bits, this with chunk count 1024] ( 65.5 <= 67.2 +- 1.5 <= 70.8 ) MB/s
 * [16 bits, this with chunk count 1024] ( 66.7 <= 68.3 +- 1.2 <= 70.9 ) MB/s
 * [17 bits, this with chunk count 1024] ( 68.6 <= 69.3 +- 0.3 <= 70.0 ) MB/s
 * [18 bits, this with chunk count 1024] ( 63.7 <= 66.5 +- 1.8 <= 69.9 ) MB/s
 *
 * [13 bits, this with chunk count 2048] ( 59 <= 69 +- 4 <= 72 ) MB/s
 * [14 bits, this with chunk count 2048] ( 67.9 <= 70.7 +- 1.3 <= 71.8 ) MB/s
 * [15 bits, this with chunk count 2048] ( 65.6 <= 69.8 +- 2.2 <= 71.5 ) MB/s
 * [16 bits, this with chunk count 2048] ( 66.9 <= 68.8 +- 1.1 <= 70.7 ) MB/s
 * [17 bits, this with chunk count 2048] ( 69.7 <= 71.1 +- 1.2 <= 72.7 ) MB/s
 * [18 bits, this with chunk count 2048] ( 65.5 <= 66.7 +- 0.6 <= 67.3 ) MB/s
 * @endverbatim
 * -> It is insanely stable over the SUBTABLE_CHUNK_COUNT parameter. It made me question the correctness, but
 *    for CHUNK_COUNT < 8, there finally are some measurable slowdowns.
 * @endverbatim
 */
template<uint16_t SUBTABLE_CHUNK_COUNT = 512U>
[[nodiscard]] inline rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    constexpr auto INDEX_BITS = requiredBits( SUBTABLE_CHUNK_COUNT * sizeof( uint64_t ) * CHAR_BIT );

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );
    const auto valueToLookUp = precodesToHistogram<4>( precodeBits );

    /* Lookup in LUT and subtable */
    const auto& [histogramLUT, validLUT] = PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<SUBTABLE_CHUNK_COUNT>;
    const auto elementIndex = ( valueToLookUp >> INDEX_BITS )
                              & nLowestBitsSet<CompressedHistogram>( HISTOGRAM_BITS - INDEX_BITS );
    const auto subIndex = histogramLUT[elementIndex];
    /* This seems to help, especially as the lookup becomes ever more expensive! */
    if ( subIndex == 0 ) {
        return rapidgzip::Error::INVALID_CODE_LENGTHS;
    }

    /* We could do a preemptive return here for subIndex == 0 but it degrades performance by ~3%. */

    const auto validIndex = ( subIndex << INDEX_BITS ) + ( valueToLookUp & nLowestBitsSet<uint64_t>( INDEX_BITS ) );
    return ( validLUT[validIndex >> 3U] & ( 1ULL << ( validIndex & 0b111U ) ) ) == 0
           ? rapidgzip::Error::INVALID_CODE_LENGTHS
           : rapidgzip::Error::NONE;
}
}  // namespace rapidgzip::PrecodeCheck::WalkTreeCompressedSingleLUT
