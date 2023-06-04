/**
 * @file This is an alternative lookup table to check the precode histogram for validity.
 *       It crams all necessary counts into 24 bits in order to not only have a partial LUT but a complete one,
 *       to save a branch for possibly valid cases.
 *       The bits were shaved off by specially accounting for overflows when adding up partial histograms.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <BitManipulation.hpp>
#include <precode.hpp>


namespace rapidgzip::PrecodeCheck::SingleLUT
{
/**
 * Shrink Precode histogram to reduce LUT sizes to allow more input values being stored.
 * Precode: 19x 3-bit codes = 57 bits.
 * Histogram over the values of those 3-bit codes (0-7).
 *  - 0-counts can be omitted because they can be deduced from the total number and all others.
 *  - Because of 19 being the maximum total count, the individual counts also are less or equal -> 5 bits suffice.
 *  - Because of tree invalidity rules, there may only be <= 2 1-bit length counts and so on.
 *  - Because of the non-bloating tree rule, larger value counts can also be reduced.
 *    The most lopsided tree has 3 smaller elements and all other 16 elements will have the longest codes.
 *    This can be produced with length counts: 1: 1, 2: 1, 3: 1, 4: 0, 5: 0, 6: 0, 7: 16.
 *    This means that we can also shave off 1 bit for the 7 counts because there is only one case it can be
 *    produced with, which we can check with the power of 2 LUT.
 *    Similarly, exhaustive tests show that the maximum value of length 5 and 6 is also 16 but there is more
 *    than one case for which 16 counts can be reached, which makes integration into the power 2 LUT harder.
 *    Exhaustive search showing all histograms with any count >= 16:
 *    @verbatim
 *    1:0 2:0 3:0 4:16 5:0 6:0 7:0
 *    1:0 2:1 3:2 4:0 5:16 6:0 7:0
 *    1:0 2:2 3:0 4:0 5:16 6:0 7:0
 *    1:0 2:3 3:0 4:0 5:0 6:16 7:0
 *    1:1 2:0 3:0 4:0 5:16 6:0 7:0
 *    1:1 2:0 3:2 4:0 5:0 6:16 7:0
 *    1:1 2:1 3:0 4:0 5:0 6:16 7:0
 *    1:1 2:1 3:1 4:0 5:0 6:0 7:16
 *    @endverbatim
 *
 * @verbatim
 * Counted Value :   7     6     5    3    3  2  1   non-0
 *                 +----+-----+-----+----+---+--+-+ +-----+
 * Storage Bits  : | 4  |  5  |  5  | 4  | 3 |2 |1| |  5  |
 *                 +----+-----+-----+----+---+--+-+ +-----+
 * @verbatim
 * So, in total 24 + 5 bits.
 * The non-zero-counts are necessary for looking up the special cases but are not required for the LUT check itself.
 *
 * 1. LUT Precode 3-bit sequence -> Histogram
 *  - We can work in chunks of arbitrary length and simply add the Histogram for those chunks together.
 *    - Need to map 19*3 bits. Possible chunk sizes, amount of lookups, and LUT table sizes
 *      assuming 32-bit (4 B) histogram:
 *      3 per chunk,  9-bits key,   2 KiB LUT, 7 lookups, 6 additions
 *      4 per chunk, 12-bits key,  16 KiB LUT, 5 lookups, 4 additions  <- probably the sweet spot
 *      5 per chunk, 15-bits key, 128 KiB LUT, 4 lookups, 3 additions  <- might be worth trying out at least
 *      6 per chunk, 18-bits key,   2 MiB LUT, 4 lookups, 3 additions  <- absolutely inferior to the previous one
 *    - Note that padding is easily done with zeros because 0-counts are ignored anyway.
 *  - We can use some of the free higher bits to account for overflow during LUT creation by setting the lowest of them
 *    if no valid histogram could be created. We can simply check if any higher bits are set after taking the sum.
 *  - Account for overflows over the storage boundaries during addition.
 *    - Addition in lowest bits: 0+0 -> 0, 0+1 -> 1, 1+0 -> 1, 1+1 -> 0 (+ carry bit)
 *                               <=> bitwise xor ^ (also sometimes called carryless addition)
 *    - If there is a carry-over (overflow) from a lower bit, then these results will be inverted.
 *      We can check for that with another xor, wich also acts as a bit-wise inequality comparison,
 *      setting the resulting bit only to 1 if both source bits are different.
 *      This result needs to be masked to the bits of interest but that can be done last to reduce instructions.
 *
 * 2. 24-bit Compressed Histogram -> valid bool
 *  - 2^21 keys and value is 1 B with 8=2^3 bit values -> 2 MiB
 */
namespace VariableLengthPackedHistogram
{
using Histogram = uint32_t;

static constexpr std::array<uint8_t, 8> MEMBER_BIT_WIDTHS = { 5, 1, 2, 3, 4, 5, 5, 4 };
static constexpr std::array<uint8_t, 8> MEMBER_OFFSETS = [] () constexpr {
    std::array<uint8_t, 8> result{};
    uint8_t sum{ 0 };
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = sum;
        sum += MEMBER_BIT_WIDTHS[i];
    }
    return result;
}();

static constexpr uint8_t OVERFLOW_MEMBER_OFFSET = MEMBER_OFFSETS.back() + MEMBER_BIT_WIDTHS.back();
/* 7 = 2^3 - 1 is the maximum number we can simply add histograms up without having to check the overflow counter. */
static_assert( OVERFLOW_MEMBER_OFFSET + 3 <= std::numeric_limits<Histogram>::digits,
               "Data type is not wide enough to allow for up to 7 overflows." );

/** This is for the histogram version during the summing, i.e., WITH zero an overflow bits! */
static constexpr auto LOWEST_MEMBER_BITS_MASK = [] () constexpr {
    Histogram result{ 0 };
    using namespace VariableLengthPackedHistogram;
    for ( const auto offset : MEMBER_OFFSETS ) {
        result |= Histogram( 1 ) << static_cast<uint8_t>( offset );
    }
    return result;
}();
static_assert( LOWEST_MEMBER_BITS_MASK == 0b0001'00001'00001'0001'001'01'1'00001ULL );

static constexpr auto OVERFLOW_BITS_MASK =
    LOWEST_MEMBER_BITS_MASK | ( ~Histogram( 0 ) << VariableLengthPackedHistogram::OVERFLOW_MEMBER_OFFSET );
static_assert( OVERFLOW_BITS_MASK == 0b111'0001'00001'00001'0001'001'01'1'00001ULL );


[[nodiscard]] constexpr uint8_t
getCount( const Histogram histogram,
          const uint8_t   value )
{
    return ( histogram >> MEMBER_OFFSETS.at( value ) ) & nLowestBitsSet<Histogram>( MEMBER_BIT_WIDTHS.at( value ) );
}


[[nodiscard]] constexpr Histogram
setCount( const Histogram histogram,
          const uint8_t   value,
          const uint8_t   count )
{
    const auto bitWidth = MEMBER_BIT_WIDTHS.at( value );
    if ( count >= ( 1ULL << bitWidth ) ) {
        throw std::invalid_argument( "Overflow detected. Cannot set count to given value!" );
    }
    return ( histogram & ~( nLowestBitsSet<Histogram>( bitWidth ) << MEMBER_OFFSETS.at( value ) ) )
           | ( static_cast<Histogram>( count ) << MEMBER_OFFSETS.at( value ) );
}


[[nodiscard]] constexpr Histogram
incrementCount( const Histogram histogram,
                const uint8_t   value )
{
    /* Cast to 32-bit because addition with uint8_t automatically casts to signed int. */
    const auto oldCount = static_cast<uint32_t>( getCount( histogram, value ) );

    /* Always do a simple addition no matter the overflow. This is important to keep associativity property,
     * else we might get different results for the same values just because they appear at different positions
     * in the value vector and that would be hard to look up.
     * The overflow bits are actually already non-associative because we simply set it to true here no matter how
     * many overflows but we add the bits up when adding different partial histograms from the LUT.
     * However, this is not important because the overflow bits are stripped off for the validity lookup! */
    const auto newHistogram = histogram + ( Histogram( 1 ) << MEMBER_OFFSETS.at( value ) );

    if ( oldCount + 1 < ( 1ULL << MEMBER_BIT_WIDTHS.at( value ) ) ) {
        return newHistogram;
    }
    return newHistogram | ( Histogram( 1 ) << OVERFLOW_MEMBER_OFFSET );
}


template<size_t VALUE_BITS,
         size_t VALUE_COUNT>
[[nodiscard]] constexpr Histogram
calculateHistogram( uint64_t values )
{
    static_assert( VALUE_BITS * VALUE_COUNT <= std::numeric_limits<decltype( values )>::digits,
                   "Values type does not fit the requested amount of values and bits per value!" );

    Histogram histogram{ 0 };
    for ( size_t i = 0; i < static_cast<size_t>( VALUE_COUNT ); ++i ) {
        const auto value = ( values >> ( i * VALUE_BITS ) ) & nLowestBitsSet<Histogram, VALUE_BITS>();
        if ( value > 0 ) {
            histogram = incrementCount( histogram, static_cast<uint8_t>( value ) );
            /* There should never be an overflow for the non-zero counts because they have 5-bits and we
             * only add up up to 19 non-zero values. */
            ++histogram;  // = incrementCount( histogram, 0 );
        }
    }
    return histogram;
}


/**
 * Creates a lookup table (LUT) mapping @ref VALUE_COUNT values each encoded in @ref VALUE_COUNT bits to
 * a variable-length bit-packed histogram that contains counts for each value and an overflow counter.
 */
template<uint8_t VALUE_BITS,
         uint8_t VALUE_COUNT>
[[nodiscard]] constexpr auto
createHistogramLUT()
{
    std::array<Histogram, 1ULL << ( VALUE_COUNT * VALUE_BITS )> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        result[i] = calculateHistogram<VALUE_BITS, VALUE_COUNT>( i );
    }
    return result;
}


[[nodiscard]] constexpr std::optional<Histogram>
packHistogram( const rapidgzip::deflate::precode::Histogram& histogram )
{
    Histogram packedHistogram{ 0 };
    uint8_t nonZeroCount{ 0 };
    for ( size_t i = 0; i < histogram.size(); ++i ) {
        const auto depth = i + 1;
        const auto count = histogram[i];
        nonZeroCount += count;

        /* The rare histograms that are valid and have overflows in the highly compressed format
         * are handled differently with the POWER_OF_TWO_SPECIAL_CASES LUT. */
        if ( count >= ( uint32_t( 1 ) << MEMBER_BIT_WIDTHS.at( depth ) ) ) {
            return std::nullopt;
        }
        packedHistogram = setCount( packedHistogram, depth, count );
    }

    if ( nonZeroCount >= ( uint32_t( 1 ) << MEMBER_BIT_WIDTHS.at( 0 ) ) ) {
        throw std::invalid_argument( "More total non-zero counts than permitted!" );
    }
    return setCount( packedHistogram, 0, nonZeroCount );
}


/**
 * @param histogram It is assumed to begin with counts for 1 in the lowest @p FREQUENCY_BITS bits.
 */
template<uint8_t  FREQUENCY_BITS,
         typename T>
[[nodiscard]] constexpr Histogram
packUniformlyBitPackedHistogramUnsafe( T histogram )
{
    Histogram packedHistogram{ 0 };
    for ( size_t i = 1; i < MEMBER_BIT_WIDTHS.size(); ++i ) {
        const auto value = ( histogram >> ( ( i - 1 ) * FREQUENCY_BITS ) )
                           & nLowestBitsSet<Histogram>( MEMBER_BIT_WIDTHS[i] );
        packedHistogram = setCount( packedHistogram, i, value );
    }
    return packedHistogram >> MEMBER_BIT_WIDTHS.front();
}


static_assert( packUniformlyBitPackedHistogramUnsafe<5>( 0b01111'10001'10101'01001'00101'00011'00001ULL )
               == 0b1111'10001'10101'1001'101'11'1ULL );
}  // namespace VariableLengthPackedHistogram


using Histogram = VariableLengthPackedHistogram::Histogram;

/* Max values to cache in LUT (4 * 3 bits = 12 bits LUT key -> 2^12 * 8B = 32 KiB LUT size) */
static constexpr auto PRECODE_X4_TO_HISTOGRAM_LUT =
    VariableLengthPackedHistogram::createHistogramLUT<rapidgzip::deflate::PRECODE_BITS, 4>();

static constexpr auto HISTOGRAM_TO_LOOK_UP_BITS =
    VariableLengthPackedHistogram::MEMBER_OFFSETS.back()
    - VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.front()  /* Ignore non-zero counts. */
    + VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.back();  /* Add last member width to highest offset. */
static_assert( HISTOGRAM_TO_LOOK_UP_BITS == 24,
               "This is only to document the actual bit count. It might change when further pruning the members." );

using PrecodeHistogramValidLUT = std::array<uint64_t, ( 1ULL << HISTOGRAM_TO_LOOK_UP_BITS ) / 64U>;
static_assert( ( 1ULL << HISTOGRAM_TO_LOOK_UP_BITS ) % 64U == 0,
               "LUT size must be a multiple of 64-bit for the implemented bit-packing!" );


static constexpr auto PRECODE_HISTOGRAM_VALID_LUT =
    [] ()
    {
        PrecodeHistogramValidLUT result{};
        for ( const auto& histogram : rapidgzip::deflate::precode::VALID_HISTOGRAMS ) {
            if ( const auto packedHistogram = VariableLengthPackedHistogram::packHistogram( histogram );
                 packedHistogram.has_value() )
            {
                const auto histogramToSetValid =
                    *packedHistogram >> VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.front();
                result[histogramToSetValid / 64U] |= uint64_t( 1 ) << ( histogramToSetValid % 64U );
            }
        }
        return result;
    }();


/**
 * This maps non-zero counts to the only valid histogram corresponding to it or a value that never compares equal.
 * The histogram is the one to look up, i.e., without non-zero counts and without overflow counters.
 * Note that the results might contain values that are overflown but there is no other way to arrive at the same
 * result with the given non-zero count, I think (!). Even if not, this would VERY slightly increase the false
 * positive rate but that is no error because after checkPrecode there will be a much more involved correct check.
 * However, this must not result in false negatives!
 */
constexpr std::array<Histogram, 1U << VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.front()>
POWER_OF_TWO_SPECIAL_CASES = {
    /*  0 */ ~Histogram( 0 ),  /* An empty alphabet is not legal for the precode! */
    /*  1 */ 0b0000'00000'00000'0000'000'00'1ULL,
    /*  2 */ 0b0000'00000'00000'0000'000'01'0ULL,  /* 1 (0b10) is an overflow of the 1-length bin. */
    /*  3 */ ~Histogram( 0 ),
    /*  4 */ 0b0000'00000'00000'0000'001'00'0ULL,  /* setCount( 0, 2, 4 ) >> MEMBER_BIT_WIDTHS.front() */
    /*  5 */ ~Histogram( 0 ),
    /*  6 */ ~Histogram( 0 ),
    /*  7 */ ~Histogram( 0 ),
    /*  8 */ 0b0000'00000'00000'0001'000'00'0ULL,  /* setCount( 0, 3, 8 ) >> MEMBER_BIT_WIDTHS.front() */
    /*  9 */ ~Histogram( 0 ),
    /* 10 */ ~Histogram( 0 ),
    /* 11 */ ~Histogram( 0 ),
    /* 12 */ ~Histogram( 0 ),
    /* 13 */ ~Histogram( 0 ),
    /* 14 */ ~Histogram( 0 ),
    /* 15 */ ~Histogram( 0 ),
    /* 16 */ 0b0000'00000'00001'0000'000'00'0ULL,  /* setCount( 0, 4, 16 ) >> MEMBER_BIT_WIDTHS.front() */
    /* 17 */ ~Histogram( 0 ),
    /* 18 */ ~Histogram( 0 ),
    /* 19 */ ~Histogram( 0 ),
    /* 20 */ ~Histogram( 0 ),
    /* 21 */ ~Histogram( 0 ),
    /* 22 */ ~Histogram( 0 ),
    /* 23 */ ~Histogram( 0 ),
    /* 24 */ ~Histogram( 0 ),
    /* 25 */ ~Histogram( 0 ),
    /* 26 */ ~Histogram( 0 ),
    /* 27 */ ~Histogram( 0 ),
    /* 28 */ ~Histogram( 0 ),
    /* 29 */ ~Histogram( 0 ),
    /* 30 */ ~Histogram( 0 ),
    /* 31 */ ~Histogram( 0 ),
};


/**
 * @note Requires 4 (precode count) + 57 (maximum precode count * 3) bits to check for validity.
 *       Get all 57 bits at once to avoid a data dependency on the precode count. Note that this is only
 *       possible assuming a 64-bit gzip footer, else, this could be a wrong transformation because it wouldn't
 *       be able to find very small deflate blocks close to the end of the file. because they trigger an EOF.
 *       Note that such very small blocks would normally be Fixed Huffman decoding anyway.
 */
[[nodiscard]] constexpr rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    constexpr auto PRECODE_BITS = rapidgzip::deflate::PRECODE_BITS;
    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );

    constexpr auto PRECODES_PER_CHUNK = 4U;
    constexpr auto CACHED_BITS = PRECODE_BITS * PRECODES_PER_CHUNK;
    constexpr auto CHUNK_COUNT = ceilDiv( rapidgzip::deflate::MAX_PRECODE_COUNT, PRECODES_PER_CHUNK );
    static_assert( CACHED_BITS == 12 );
    static_assert( CHUNK_COUNT == 5 );

    Histogram bitLengthFrequencies{ 0 };
    Histogram overflowsInSum{ 0 };
    Histogram overflowsInLUT{ 0 };

    for ( size_t chunk = 0; chunk < CHUNK_COUNT; ++chunk ) {
        auto precodeChunk = precodeBits >> ( chunk * CACHED_BITS );
        /* The last requires no bit masking because @ref next57Bits is already sufficiently masked.
         * This branch will hopefully get unrolled, else it could hinder performance. */
        if ( chunk != CHUNK_COUNT - 1 ) {
            precodeChunk &= nLowestBitsSet<uint64_t, CACHED_BITS>();
        }

        const auto partialHistogram = PRECODE_X4_TO_HISTOGRAM_LUT[precodeChunk];

        /**
         * Account for overflows over the storage boundaries during addition.
         *  - Addition in lowest bits: 0+0 -> 0, 0+1 -> 1, 1+0 -> 1, 1+1 -> 0 (+ carry bit)
         *                             <=> bitwise xor ^ (also sometimes called carryless addition)
         *  - If there is a carry-over (overflow) from a lower bit, then these results will be inverted.
         *    We can check for that with another xor, wich also acts as a bit-wise inequality comparison,
         *    setting the resulting bit only to 1 if both source bits are different.
         *    This result needs to be masked to the bits of interest but that can be done last to reduce instructions.
         */
        const auto carrylessSum = bitLengthFrequencies ^ partialHistogram;
        bitLengthFrequencies = bitLengthFrequencies + partialHistogram;
        overflowsInSum |= carrylessSum ^ bitLengthFrequencies;
        overflowsInLUT |= partialHistogram;
    }

    /* Ignore non-zero and overflow counts for lookup. */
    using namespace VariableLengthPackedHistogram;
    const auto histogramToLookUp = ( bitLengthFrequencies >> MEMBER_BIT_WIDTHS.front() )
                                   & nLowestBitsSet<Histogram>( HISTOGRAM_TO_LOOK_UP_BITS );
    const auto nonZeroCount = bitLengthFrequencies & nLowestBitsSet<Histogram>( MEMBER_BIT_WIDTHS.front() );
    if ( UNLIKELY( POWER_OF_TWO_SPECIAL_CASES[nonZeroCount] == histogramToLookUp ) ) [[unlikely]] {
        return rapidgzip::Error::NONE;
    }

    if ( ( ( overflowsInSum & OVERFLOW_BITS_MASK ) != 0 )
         || ( ( overflowsInLUT & ( ~Histogram( 0 ) << OVERFLOW_MEMBER_OFFSET ) ) != 0 ) ) {
        return rapidgzip::Error::INVALID_CODE_LENGTHS;
    }

    const auto bitToLookUp = 1ULL << ( histogramToLookUp % 64 );
    constexpr auto INDEX_BIT_COUNT = HISTOGRAM_TO_LOOK_UP_BITS - 6 /* log2 64 = 6 */;
    const auto elementIndex = ( histogramToLookUp / 64 ) & nLowestBitsSet<Histogram, INDEX_BIT_COUNT>();
    if ( LIKELY( ( PRECODE_HISTOGRAM_VALID_LUT[elementIndex] & bitToLookUp ) == 0 ) ) [[unlikely]] {
        /* This also handles the case of all being zero, which in the other version returns EMPTY_ALPHABET!
         * Some might also not be bloating but simply invalid, we cannot differentiate that but it can be
         * helpful for tests to have different errors. For actual usage comparison with NONE is sufficient. */
        return rapidgzip::Error::BLOATING_HUFFMAN_CODING;
    }

    return rapidgzip::Error::NONE;
}


namespace ValidHistogramID
{
using HistogramID = uint16_t;
static_assert( deflate::precode::VALID_HISTOGRAMS_COUNT + 1 <= ( 1ULL << std::numeric_limits<HistogramID>::digits ),
               "Must be able to store IDs for each of valid histogram and a non-valid ID." );


/**
 * @tparam SUBTABLE_INDEX_BIT_WIDTH Size is in number bits, i.e., actual size is 2^SUBTABLE_INDEX_BIT_WIDTH elements.
 */
template<uint8_t SUBTABLE_INDEX_BIT_WIDTH>
static constexpr auto REQUIRED_SUBTABLES_COUNT =
    [] ()
    {
        using rapidgzip::deflate::precode::VALID_HISTOGRAMS;
        std::array<std::pair</* truncated address */ uint32_t,
                             /* number of valid histograms in same truncated address */ uint16_t>,
                   VALID_HISTOGRAMS.size()> counts;
        for ( const auto& histogram : VALID_HISTOGRAMS ) {
            using namespace rapidgzip::PrecodeCheck::SingleLUT;
            const auto packedHistogram = VariableLengthPackedHistogram::packHistogram( histogram );
            if ( !packedHistogram ) {
                continue;
            }

            const auto truncatedAddress = *packedHistogram >> ( VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.front()
                                                                + SUBTABLE_INDEX_BIT_WIDTH );

            for ( auto& [address, count] : counts ) {
                if ( address == truncatedAddress ) {
                    ++count;
                    break;
                }
                if ( count == 0 ) {
                    address = truncatedAddress;
                    ++count;
                    break;
                }
            }
        }

        size_t uniqueAddresses{ 0 };
        for ( const auto& [address, count] : counts ) {
            ++uniqueAddresses;
            if ( count == 0 ) {
                break;
            }
        }
        return uniqueAddresses + 1 /* additional invalid address for invalid histograms */;
    }();

static_assert( REQUIRED_SUBTABLES_COUNT<9> == 184 );


constexpr auto SUBTABLES_BIT_WIDTH = 9U;
constexpr auto SUBTABLE_SIZE = 1ULL << SUBTABLES_BIT_WIDTH;
constexpr auto SUBTABLES_COUNT = REQUIRED_SUBTABLES_COUNT<SUBTABLES_BIT_WIDTH>;
/**
 * This is a two-staged LUT helper to lookup the index in VALID_HISTOGRAMS for a given histogram.
 * For invalid LUTs, it returns an out-of-range index.
 * The outer LUT contains values to a corresponding subtable.
 */
static constexpr auto HISTOGRAM_TO_ID_LUT =
    [] ()
    {
        using SubtableID = uint8_t;
        std::array<SubtableID, ( 1ULL << ( HISTOGRAM_TO_LOOK_UP_BITS - SUBTABLES_BIT_WIDTH ) )> lut{};
        std::array<HistogramID, SUBTABLE_SIZE * SUBTABLES_COUNT> subtables{};
        static_assert( SUBTABLES_COUNT < std::numeric_limits<SubtableID>::max() );

        /* Initialize subtables with invalid indexes. Using 0 as invalid index by convention would require
         * further post-processing and a branch after the lookup, so avoid it. */
        for ( auto& x : subtables ) {
            x = std::numeric_limits<HistogramID>::max();
        }
        /* Skip the first table, which shall contain only invalid values. @ref lut is already initialized with zeros,
         * i.e., it points to this table, so that is perfect to avoid further initialization of @ref lut. */
        size_t subtablesCount{ 1 };

        using rapidgzip::deflate::precode::VALID_HISTOGRAMS;
        std::array<std::pair<uint32_t, SubtableID>, VALID_HISTOGRAMS.size()> truncatedAddressToSubtable{};
        const auto getSubtableId =
            [&truncatedAddressToSubtable, &subtablesCount] ( const uint32_t truncatedAddress )
            {
                for ( auto& [address, subtableId] : truncatedAddressToSubtable ) {
                    if ( address == truncatedAddress ) {
                        return subtableId;
                    }
                    /* Subtable IDs are initialized to zero by default but as this function is only used
                     * for valid histograms and we have reserved the zeroth subtable for invalid ranges,
                     * we can simply test like this whether the map entry is unused. */
                    if ( subtableId == 0 ) {
                        address = truncatedAddress;
                        subtableId = subtablesCount++;
                        return subtableId;
                    }
                }
                throw std::logic_error( "Should only happen when we run out of subtable space, i.e., never." );
            };

        for ( size_t i = 0; i < VALID_HISTOGRAMS.size(); ++i ) {
            const auto& histogram = VALID_HISTOGRAMS[i];

            using namespace rapidgzip::PrecodeCheck::SingleLUT;
            const auto packedHistogram = VariableLengthPackedHistogram::packHistogram( histogram );
            if ( !packedHistogram ) {
                continue;
            }

            const auto histogramWithoutZero =
                *packedHistogram >> VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.front();
            const auto truncatedAddress = histogramWithoutZero >> SUBTABLES_BIT_WIDTH;
            const auto subtableId = getSubtableId( truncatedAddress );
            lut[truncatedAddress] = subtableId;

            /* It should not be possible to get colliding subtable entries because for any given @ref truncatedAddress
             * there are no duplicate @ref lowParts values because @ref truncatedAddress contains the high bits that
             * do not wrap around. */
            const auto lowParts = histogramWithoutZero & nLowestBitsSet<uint32_t>( SUBTABLES_BIT_WIDTH );
            subtables[subtableId * SUBTABLE_SIZE + lowParts] = i;
        }

        return std::make_tuple( lut, subtables );
    }();


constexpr std::array<HistogramID, 1U << VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS.front()>
POWER_OF_TWO_SPECIAL_CASES_TO_ID = {
    /*  0 */ std::numeric_limits<HistogramID>::max(),
    /*  1 */ 1031,
    /*  2 */ 1525,
    /*  3 */ std::numeric_limits<HistogramID>::max(),
    /*  4 */ 1030,
    /*  5 */ std::numeric_limits<HistogramID>::max(),
    /*  6 */ std::numeric_limits<HistogramID>::max(),
    /*  7 */ std::numeric_limits<HistogramID>::max(),
    /*  8 */ 276,
    /*  9 */ std::numeric_limits<HistogramID>::max(),
    /* 10 */ std::numeric_limits<HistogramID>::max(),
    /* 11 */ std::numeric_limits<HistogramID>::max(),
    /* 12 */ std::numeric_limits<HistogramID>::max(),
    /* 13 */ std::numeric_limits<HistogramID>::max(),
    /* 14 */ std::numeric_limits<HistogramID>::max(),
    /* 15 */ std::numeric_limits<HistogramID>::max(),
    /* 16 */ 7,
    /* 17 */ std::numeric_limits<HistogramID>::max(),
    /* 18 */ std::numeric_limits<HistogramID>::max(),
    /* 19 */ std::numeric_limits<HistogramID>::max(),
    /* 20 */ std::numeric_limits<HistogramID>::max(),
    /* 21 */ std::numeric_limits<HistogramID>::max(),
    /* 22 */ std::numeric_limits<HistogramID>::max(),
    /* 23 */ std::numeric_limits<HistogramID>::max(),
    /* 24 */ std::numeric_limits<HistogramID>::max(),
    /* 25 */ std::numeric_limits<HistogramID>::max(),
    /* 26 */ std::numeric_limits<HistogramID>::max(),
    /* 27 */ std::numeric_limits<HistogramID>::max(),
    /* 28 */ std::numeric_limits<HistogramID>::max(),
    /* 29 */ std::numeric_limits<HistogramID>::max(),
    /* 30 */ std::numeric_limits<HistogramID>::max(),
    /* 31 */ std::numeric_limits<HistogramID>::max(),
};


[[nodiscard]] constexpr size_t
getHistogramIdFromVLPHWithoutZero( const uint32_t& packedHistogramWithoutZero )
{
    const auto& [LUT, SUBTABLES] = HISTOGRAM_TO_ID_LUT;
    const auto subtableId = LUT[packedHistogramWithoutZero >> SUBTABLES_BIT_WIDTH];
    const auto lowParts = packedHistogramWithoutZero & nLowestBitsSet<Histogram>( SUBTABLES_BIT_WIDTH );
    return SUBTABLES[subtableId * SUBTABLE_SIZE + lowParts];
}


[[nodiscard]] constexpr size_t
getHistogramIdFromUniformlyPackedHistogram( const uint64_t& histogram5BitCounts )
{
    const auto nonZeroCount = histogram5BitCounts & nLowestBitsSet<Histogram>( 5 );
    const auto specialID = POWER_OF_TWO_SPECIAL_CASES_TO_ID[nonZeroCount];
    if ( specialID < rapidgzip::deflate::precode::VALID_HISTOGRAMS.size() ) {
        return specialID;
    }

    const auto histogramWithoutZero = histogram5BitCounts >> 5U;
    if ( ( histogramWithoutZero & 0b10000'00000'00000'00000'10000'11000'11100'11110ULL ) != 0 ) {
        return std::numeric_limits<size_t>::max();
    }
    const auto packedHistogramWithoutZero =
        VariableLengthPackedHistogram::packUniformlyBitPackedHistogramUnsafe<5>( histogramWithoutZero );
    return getHistogramIdFromVLPHWithoutZero( packedHistogramWithoutZero );
}
}  // namspace ValidHistogramID
}  // namespace rapidgzip::PrecodeCheck::SingleLUT
