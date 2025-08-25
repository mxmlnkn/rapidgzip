/**
 * @file Evolution of SingleCompressedLUT. The idea is to add branches for precode count <= 15 and possibly <= 7
 *       knowing that in these cases the histogram bins cannot overflow 4 bits or 3 bits respectively.
 * - [ ] refactor out the two-staged LUT creation
 * - [ ] Test SingleLUT with uint8_t based LUT instead of uint64_t. Might improve cache line misses?
 * - [ ] Test SingleLUT with uint8_t non-bit-packed!
 * - [ ] Test Duff's device like loop unrolling for histogram computation because next4Bits is often (12 out of 16 = 75%)
 *       quite small (<=15) and might not require testing for overflows!!!
 *       -> Then we would be able to skip the POWER_OF_TWO_SPECIAL_CASES check AND shave off further bits from
 *          code lengths 5 and 6, giving us a total of 22 bits for the LUT (4 Mi elements, 512 KiB bit-packed)
 *       -> For next4Bits in [0,1,2,3] (25%) we could shave YET ANOTHER bit from code lengths >= 3, i.e.,
 *          18 bits in total (256 Ki elements -> 32 KiB).
 *          -> If this check is added, maybe we could reduce the LUT for <=15 by returning false for code lengths <=7?
 *       -> For <= 7 special case, it makes sense to do a linear search in all valid histograms (only 22)!
 *          See analysis in testPrecodeCheck.cpp printValidHistogramsByPrecodeCount
 */

#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <core/BitManipulation.hpp>

#include "SingleLUT.hpp"
#include "SingleCompressedLUT.hpp"
#include "WalkTreeLUT.hpp"



namespace rapidgzip::PrecodeCheck
{
namespace SingleLUT::VariableLengthPackedHistogram
{
[[nodiscard]] constexpr Histogram
setCount2( const Histogram histogram,
          const uint8_t   value,
          const uint8_t   count )
{
    const auto bitWidth = MEMBER_BIT_WIDTHS.at( value );
    //if ( count >= ( 1ULL << bitWidth ) ) {
    //    throw std::invalid_argument( "Overflow detected. Cannot set count to given value!" );
    //}
    return ( histogram & ~( nLowestBitsSet<Histogram>( bitWidth ) << MEMBER_OFFSETS.at( value ) ) )
           | ( static_cast<Histogram>( count ) << MEMBER_OFFSETS.at( value ) );
}


[[nodiscard]] constexpr Histogram
packHistogram2( const rapidgzip::deflate::precode::Histogram& histogram )
{
    Histogram packedHistogram{ 0 };
    uint8_t nonZeroCount{ 0 };
    for ( size_t i = 0; i < histogram.size(); ++i ) {
        const auto depth = i + 1;
        const auto count = histogram[i];
        nonZeroCount += count;
        packedHistogram = setCount2( packedHistogram, depth, count );
    }

    if ( nonZeroCount >= ( uint32_t( 1 ) << MEMBER_BIT_WIDTHS.at( 0 ) ) ) {
        throw std::invalid_argument( "More total non-zero counts than permitted!" );
    }
    return setCount2( packedHistogram, 0, nonZeroCount );
}


static const auto VALID_HISTOGRAMS_ARRAY =
    [] ()
    {
        const auto& VALID_HISTOGRAMS = deflate::precode::VALID_HISTOGRAMS;
        std::array<uint32_t, VALID_HISTOGRAMS.size()> result{};
        for ( size_t i = 0; i < VALID_HISTOGRAMS.size(); ++i ) {
            result[i] = packHistogram2( VALID_HISTOGRAMS[i] );
        }
        return result;
    } ();

static const std::unordered_set<uint32_t> VALID_HISTOGRAMS{
    VALID_HISTOGRAMS_ARRAY.begin(), VALID_HISTOGRAMS_ARRAY.end()
};
}  // namespace SingleLUT::VariableLengthPackedHistogram


namespace SingleCompressedLUTOptimized
{
namespace ShortVariableLengthPackedHistogram
{
using CompressedHistogram = uint16_t;  // See considerations in testPrecodeCheck.cpp
constexpr std::array<uint8_t, deflate::MAX_PRECODE_LENGTH> MEMBER_BIT_WIDTHS = { 1, 2, 3, 3, 3, 2, 2 };
constexpr std::array<uint8_t, deflate::MAX_PRECODE_LENGTH> MEMBER_BIT_OFFSETS = { 0, 1, 3, 6, 9, 12, 14 };


[[nodiscard]] constexpr uint16_t
countValidHistograms( uint8_t codeLengthCount )
{
    size_t count{ 0 };
    for ( const auto histogram : deflate::precode::VALID_HISTOGRAMS ) {
        size_t sum{ 0 };
        for ( auto bin : histogram ) {
            sum += bin;
        }
        if ( sum <= codeLengthCount ) {
            ++count;
        }
    }
    return count;
}


[[nodiscard]] constexpr uint16_t
packHistogram( const std::array<uint8_t, deflate::MAX_PRECODE_LENGTH>& histogram )
{
    if ( histogram.size() != MEMBER_BIT_WIDTHS.size() ) {
        throw std::logic_error( "" );
    }

    size_t width{ 0 };
    uint16_t result{ 0 };
    for ( size_t i = 0; i < MEMBER_BIT_WIDTHS.size(); ++i ) {
        result |= histogram[i] << width;
        width += MEMBER_BIT_WIDTHS[i];
    }
    if ( width > sizeof( result ) * CHAR_BIT ) {
        throw std::logic_error( "Histogram bit widths do not fit the result type!" );
    }
    return result;
}


template<uint8_t MAX_PRECODE_COUNT>
constexpr auto
createValidHistogramsList()
{
    /* Align and pad to 256-bit SIMD register length. In the future maybe to 512 bits. */
    /** @todo store directly in 256-bit registers instead of a reinterpret cast? */
    //constexpr auto validCount = ceilDiv( countValidHistograms( MAX_PRECODE_COUNT ), 256 / 8 / sizeof( uint16_t ) );
    alignas( 64 ) std::array<uint16_t, countValidHistograms( MAX_PRECODE_COUNT )> result{};
    size_t i{ 0 };
    uint16_t lastHistogram{ 0 };
    for ( const auto histogram : deflate::precode::VALID_HISTOGRAMS ) {
        size_t sum{ 0 };
        for ( auto bin : histogram ) {
            sum += bin;
        }
        if ( sum <= MAX_PRECODE_COUNT ) {
            lastHistogram = packHistogram( histogram );
            result.at( i ) = lastHistogram;
            ++i;
        }
    }
    /* Repeat the last so that we can apply SIMD assuming even multiples of 256-bit. */
    for ( ; i < result.size(); ++i ) {
        result[i] = lastHistogram;
    }
    return result;
}


template<size_t MAX_PRECODE_COUNT>
alignas( 64 ) constexpr auto VALID_HISTOGRAMS = createValidHistogramsList<MAX_PRECODE_COUNT>();


#include <immintrin.h>


template<size_t COUNT>
inline bool
containsU16AVX2( const uint16_t* const p,
                 const uint16_t        key ) noexcept
{
    const auto broadcastedKey = _mm256_set1_epi16( key );

    constexpr auto ELEMENTS_PER_REGISTER = sizeof( __m256i ) / sizeof( uint16_t );
    static_assert( ELEMENTS_PER_REGISTER == 16 );
    constexpr auto UNROLL_SIZE = 4;  // read 4 256-bit registers manually per loop iteration. */
    constexpr auto ELEMENTS_PER_UNROLL = UNROLL_SIZE * ELEMENTS_PER_REGISTER;

    if constexpr ( COUNT % ELEMENTS_PER_UNROLL == 0 ) {
        std::array<__m256i, COUNT / ELEMENTS_PER_UNROLL> anyEqual;
        size_t j{ 0 };
        for ( size_t i = 0; i < COUNT; i += ELEMENTS_PER_UNROLL ) {
            const auto v1 = _mm256_loadu_si256( reinterpret_cast<const __m256i*>( p + i ) );
            const auto v2 = _mm256_loadu_si256( reinterpret_cast<const __m256i*>( p + i + ELEMENTS_PER_REGISTER ) );
            const auto v3 = _mm256_loadu_si256( reinterpret_cast<const __m256i*>( p + i + ELEMENTS_PER_REGISTER * 2 ) );
            const auto v4 = _mm256_loadu_si256( reinterpret_cast<const __m256i*>( p + i + ELEMENTS_PER_REGISTER * 3 ) );

            const auto v5 = v1 | v2;
            const auto v6 = v3 | v4;
            anyEqual[j++] = v5 | v6;
        }
        auto result = anyEqual[0];
        for ( size_t i = 1; i < anyEqual.size(); ++i ) {
            result |= anyEqual[i];
        }
        return _mm256_movemask_epi8( result );
    } else {
        for ( size_t i = 0; i < COUNT; i += ELEMENTS_PER_REGISTER ) {
            const auto value = _mm256_loadu_si256( reinterpret_cast<const __m256i*>( p + i ) );
            const auto equal = _mm256_cmpeq_epi16( value, broadcastedKey );
            if ( _mm256_movemask_epi8( equal ) ) {
                return true;   // any 16-bit lane matched
            }
        }
    }
    return false;
}


template<uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr CompressedHistogram
calculateCompressedHistogram( uint64_t values )
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


template<uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr auto
createCompressedHistogramLUT()
{
    std::array<CompressedHistogram, 1ULL << uint16_t( VALUE_COUNT * VALUE_BITS )> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = calculateCompressedHistogram<VALUE_BITS, VALUE_COUNT>( i );
    }
    return result;
}


/* Max values to cache in LUT (4 * 3 bits = 12 bits LUT key -> 2^12 * 2B = 4 KiB LUT size) */
template<uint8_t PRECODE_CHUNK_SIZE>
static constexpr auto PRECODE_TO_FREQUENCIES_LUT =
    createCompressedHistogramLUT<deflate::PRECODE_BITS, PRECODE_CHUNK_SIZE>();


template<uint8_t PRECODE_CHUNK_SIZE,
         uint8_t MAX_PRECODE_COUNT = deflate::MAX_PRECODE_COUNT>
[[nodiscard]] constexpr forceinline CompressedHistogram
precodesToHistogram( uint64_t    precodeBits )
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
}  // namespace ShortVariableLengthPackedHistogram


namespace ShortUniformLengthPackedHistogram
{
constexpr auto MAX_PRECODE_COUNT = 15U;  // Explicitly smaller than deflate::MAX_PRECODE_COUNT (19)!
constexpr auto UNIFORM_FREQUENCY_BITS = 4U;
using CompressedHistogram = uint32_t;  // suffices for 8 * 4 bit per bin


/**
 * This code is copy-paste of WalkTreeCompressedSingleLUT with very minutiae corrections to work with
 * 4 bits per bin. This includes small fixes to only remove 1 bit instead of 2 from the lowest bin!
 */
constexpr auto HISTOGRAM_BITS = uint16_t( UNIFORM_FREQUENCY_BITS * deflate::MAX_PRECODE_LENGTH - 3U );
static_assert( HISTOGRAM_BITS == 25 );

/* The histogram uses 4 instead of 5 bits per bin, but if we do some checking, which we do, then that histogram
 * would still have some higher bits always zero: 0b1111'1111'1111'1111'1111'0111'0011
 * Without proper fast PEXT instruction support, it can get quite expensive to bit-compress the histogram
 * further. However, especially the last bits are juicy, even more so because we are doing shifts already
 * anyway to access the bit mask! Namely, the lowest 3 bits are used to access the uint8_t.
 * Only the lowest 2 bits can be something else than 0, but oh well, at least we can get rid of bit 4 and 5
 * for free!
 *      0b1111'1111'1111'1111'1111'0111'0011
 *   ^                                    |
 *   +------------------------------------+
 *   Move to high bits so that it can be easily truncated!
 *
 *      0b0011'1111'1111'1111'1111'1111'0111
 *        |                           |  | |
 *        |                           |  +-+ access bits in uint8_t
 *        |                           |
 *        |                           |
 *        +---------------------------+
 *        22 bits used for access into compressedLUT
 */

[[nodiscard]] constexpr CompressedHistogram
removeOneBit( const CompressedHistogram histogram )
{
    const auto bitsRemoved = ( ( histogram >> 1U ) & ~nLowestBitsSet<uint64_t, 3>() )
                             | ( histogram & nLowestBitsSet<uint64_t, 3>() );
    const auto restored = ( ( bitsRemoved & ~nLowestBitsSet<uint64_t, 3>() ) << 1U )
                          | ( bitsRemoved & nLowestBitsSet<uint64_t, 3>() );
    if ( restored != histogram ) {
        std::cerr << "Tried to compress: " << std::bitset<64>( histogram ) << "\n";
        std::cerr << "Got compressed   : " << std::bitset<64>( bitsRemoved ) << "\n";
        std::cerr << "Restored         : " << std::bitset<64>( restored ) << "\n";
        throw std::logic_error( "Transformation not reversible!" );
    }
    return bitsRemoved;
}


[[nodiscard]] constexpr CompressedHistogram
rearrangeHistogram(  CompressedHistogram histogram )
{
    const auto counts1 = histogram & nLowestBitsSet<uint64_t, UNIFORM_FREQUENCY_BITS>();
    const auto result = ( histogram >> UNIFORM_FREQUENCY_BITS ) | ( counts1 << ( 6 * UNIFORM_FREQUENCY_BITS ) );
    return result;
}


/**
 * Not constexpr because it would kill the compiler. Probably should simply be precomputed.
 * @verbatim
 * CHUNK_COUNT =   1: 512 KiB +   184 ( 2 subtables) -> 524472 B
 * CHUNK_COUNT =   4: 128 KiB +  1312 ( 5 subtables) -> 132384 B
 * CHUNK_COUNT =   8:  64 KiB +  3392 ( 6 subtables) ->  68928 B
 * CHUNK_COUNT =  16:  32 KiB +  6784 ( 6 subtables) ->  39552 B
 * CHUNK_COUNT =  32:  16 KiB + 13312 ( 6 subtables) ->  29696 B <-
 * CHUNK_COUNT =  64:   8 KiB + 42496 (10 subtables) ->  50688 B
 * CHUNK_COUNT = 128:   4 KiB + 72704 ( 8 subtables) ->  76800 B
 * @endverbatim
 */
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
                validHistograms.push_back( removeOneBit( rearrangeHistogram( histogramToSetValid ) ) );
            };
        WalkTreeLUT::walkValidPrecodeCodeLengthFrequencies<UNIFORM_FREQUENCY_BITS, deflate::MAX_PRECODE_LENGTH>(
            processValidHistogram, MAX_PRECODE_COUNT /* 15, not 19 here! rest is copy-paste of WalkTreeLUT */ );
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
        result[i] = rearrangeHistogram(
                        WalkTreeLUT::calculateCompressedHistogram<FREQUENCY_BITS, VALUE_BITS, VALUE_COUNT>( i )
                        >> FREQUENCY_BITS );  // Remove unused non-zero counts.
    }
    return result;
}


/* Max values to cache in LUT (4 * 3 bits = 12 bits LUT key -> 2^12 * 8B = 32 KiB LUT size) */
template<uint8_t PRECODE_CHUNK_SIZE>
static constexpr auto PRECODE_TO_FREQUENCIES_LUT =
    createCompressedHistogramLUT<UNIFORM_FREQUENCY_BITS, deflate::PRECODE_BITS, PRECODE_CHUNK_SIZE>();


template<uint8_t PRECODE_CHUNK_SIZE>
[[nodiscard]] constexpr CompressedHistogram
precodesToHistogram( uint64_t precodeBits )
{
    constexpr auto& LUT = PRECODE_TO_FREQUENCIES_LUT<PRECODE_CHUNK_SIZE>;
    constexpr auto CACHED_BITS = deflate::PRECODE_BITS * PRECODE_CHUNK_SIZE;  // 12
    return LUT[precodeBits & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           + LUT[( precodeBits >> ( 1U * CACHED_BITS ) ) & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           + LUT[( precodeBits >> ( 2U * CACHED_BITS ) ) & nLowestBitsSet<uint64_t, CACHED_BITS>()]
           /* The last requires no bit masking because BitReader::read already has masked to the lowest 57 bits
            * and this shifts 48 bits to the right, leaving only 9 (<12) bits set anyways. */
           + LUT[( precodeBits >> ( 3U * CACHED_BITS ) )];
}


template<uint16_t SUBTABLE_CHUNK_COUNT = 32U>
[[nodiscard]] inline rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    constexpr auto INDEX_BITS = requiredBits( SUBTABLE_CHUNK_COUNT * sizeof( uint64_t ) * CHAR_BIT );
    constexpr auto HIGH_BITS_TO_BE_ZERO = 0b1100'0000'0000'0000'0000'0000'1000ULL;

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * deflate::PRECODE_BITS );
    const auto histogram = precodesToHistogram<4>( precodeBits );
    const auto valueToLookUp = histogram & ~HIGH_BITS_TO_BE_ZERO;

    /* Lookup in LUT and subtable */
    const auto& [histogramLUT, validLUT] = PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<SUBTABLE_CHUNK_COUNT>;
    const auto subIndex = histogramLUT[valueToLookUp >> ( INDEX_BITS + 1U )];
    const auto bitMaskToTest = 1ULL << ( valueToLookUp & 0b111U );
    const auto validIndex = ( subIndex << ( INDEX_BITS - 3U ) )
                            | ( ( valueToLookUp >> 4U ) & nLowestBitsSet<uint64_t>( INDEX_BITS - 3U ) );
    const auto valid = validLUT[validIndex] & bitMaskToTest;

    /* It seems that the branching || is fine as long as we compute everything beforehand.
     * This probably lets the compiler prefetch, for example, validLUT. */
    return ( ( histogram & HIGH_BITS_TO_BE_ZERO ) != 0 ) || ( valid == 0 )
           ? rapidgzip::Error::INVALID_CODE_LENGTHS
           : rapidgzip::Error::NONE;
}
}  // namespace ShortUniformLengthPackedHistogram


using rapidgzip::PrecodeCheck::SingleLUT::Histogram;
using rapidgzip::PrecodeCheck::SingleLUT::HISTOGRAM_TO_LOOK_UP_BITS;
using rapidgzip::PrecodeCheck::SingleLUT::PRECODE_HISTOGRAM_VALID_LUT;
using rapidgzip::PrecodeCheck::SingleLUT::POWER_OF_TWO_SPECIAL_CASES;


template<uint16_t COMPRESSED_LUT_CHUNK_COUNT = 8U>
[[nodiscard]] constexpr Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * deflate::PRECODE_BITS );

    #if 0
        /**
         * On Notebook
         * 19: ~20 MB/s
         * 15: ~40 MB/s
         * 12: ~65 MB/s
         * 10: ~75 MB/s
         *  9: ~75 MB/s
         *  8: ~80 MB/s
         *  6: ~90 MB/s
         *  0: ~95 MB/s
         * -> any of these special cases only slows things down :(
         */
        constexpr auto MAX_PRECODE_COUNT = 12;
        if ( codeLengthCount <= MAX_PRECODE_COUNT ) {
            /** Fuzzy check using SIMD and 16-bit histograms with overflows allowed! */
            using namespace ShortVariableLengthPackedHistogram;
            const auto histogram = precodesToHistogram<3>( precodeBits );
            return containsU16AVX2<VALID_HISTOGRAMS<MAX_PRECODE_COUNT>.size()>(
                VALID_HISTOGRAMS<MAX_PRECODE_COUNT>.data(), histogram )
                   ? rapidgzip::Error::NONE
                   : rapidgzip::Error::INVALID_CODE_LENGTHS;
        }
    #elif 0
        /* 12 out of 16 times (75%) of the time, we expect this to be true. */
        if ( codeLengthCount <= 15 ) {
            return ShortUniformLengthPackedHistogram::checkPrecode( next4Bits, next57Bits );
        }
    #endif

    constexpr auto PRECODES_PER_CHUNK = 4U;
    constexpr auto CACHED_BITS = deflate::PRECODE_BITS * PRECODES_PER_CHUNK;
    static_assert( CACHED_BITS == 12 );

    constexpr auto CHUNK_COUNT = ceilDiv( deflate::MAX_PRECODE_COUNT, PRECODES_PER_CHUNK );
    static_assert( CHUNK_COUNT == 5 );

    /* We don't care for overflows. They will randomly return an error or not, but this only creates benign
     * false positives, which are filtered later on. */
    Histogram bitLengthFrequencies{ 0 };
    for ( size_t chunk = 0; chunk < CHUNK_COUNT; ++chunk ) {
        auto precodeChunk = precodeBits >> ( chunk * CACHED_BITS );
        /* The last requires no bit masking because @ref next57Bits is already sufficiently masked.
         * This branch will hopefully get unrolled, else it could hinder performance. */
        if ( chunk != CHUNK_COUNT - 1 ) {
            precodeChunk &= nLowestBitsSet<uint64_t, CACHED_BITS>();
        }

        const auto partialHistogram = SingleLUT::PRECODE_X4_TO_HISTOGRAM_LUT[precodeChunk];

        bitLengthFrequencies = bitLengthFrequencies + partialHistogram;
    }

#if 0
    // 0.49 MB/s
    const auto& valid = SingleLUT::VariableLengthPackedHistogram::VALID_HISTOGRAMS;
    return std::find( valid.begin(), valid.end(), bitLengthFrequencies ) != valid.end()
           ? Error::NONE
           : Error::INVALID_CODE_LENGTHS;
#elif 0
    // ~ 43 MB/s
    return SingleLUT::VariableLengthPackedHistogram::VALID_HISTOGRAMS.count( bitLengthFrequencies ) > 0
           ? Error::NONE
           : Error::INVALID_CODE_LENGTHS;
#else
    /* Ignore non-zero and overflow counts for lookup. */
    const auto histogramToLookUp = ( bitLengthFrequencies >> 5U )
                                   & nLowestBitsSet<Histogram>( HISTOGRAM_TO_LOOK_UP_BITS );
    const auto nonZeroCount = bitLengthFrequencies & nLowestBitsSet<Histogram>( 5 );

    /* We cannot skip this check because it would result in false negatives, which is "unrecoverable"!
     * (It can be recovered from, but at a much much higher level, namely the chunk prefetching, and therefore
     * will reduce parallel decompression speed a lot.
     * This overflow check is unnecessary because the the overflows only happen in a benign manner manner
     * not leading to false negatives. -> @todo Test this assumption exhaustively! */
    if ( UNLIKELY( POWER_OF_TWO_SPECIAL_CASES[nonZeroCount] == histogramToLookUp ) ) [[unlikely]] {
        return Error::NONE;
    }

    const auto& [histogramLUT, validLUT] =
        SingleCompressedLUT::COMPRESSED_PRECODE_HISTOGRAM_VALID_LUT_DICT<COMPRESSED_LUT_CHUNK_COUNT>;
    constexpr auto INDEX_BITS = requiredBits( COMPRESSED_LUT_CHUNK_COUNT * sizeof( uint64_t ) * CHAR_BIT );
    const auto elementIndex = ( histogramToLookUp >> INDEX_BITS )
                              & nLowestBitsSet<Histogram>( HISTOGRAM_TO_LOOK_UP_BITS - INDEX_BITS );
    const auto subIndex = histogramLUT[elementIndex];

    /* We could do a preemptive return here for subIndex == 0 but it degrades performance by ~3%. */

    const auto validIndex = ( subIndex << INDEX_BITS ) + ( histogramToLookUp & nLowestBitsSet<uint64_t>( INDEX_BITS ) );
    return validLUT[validIndex] == 0 ? Error::BLOATING_HUFFMAN_CODING : Error::NONE;
#endif
}
}  // namespace SingleCompressedLUTOptimized
}  // namespace rapidgzip::PrecodeCheck::SingleCompressedLUTOptimized
