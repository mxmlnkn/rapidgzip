/**
 * @file Started out as WalkTreeCompressedLUT with FREQUENCY_COUNT 7U, i.e., a single LUT for the whole histogram.
 *       This file throws away support for the FREQUENCY_COUNT parameter and related code and tries to optimize it.
 * The main idea is to reduce the LUT even further from 1.8 MB by bit-packing again.
 * Because the non-zero counts are not used, we can adjust the precodesToHistogram to not include them to save
 * a single bit shift operation.
 */

#pragma once

#include <array>
#include <bitset>
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


namespace rapidgzip::PrecodeCheck::WalkTreeCompressedSingleLUT
{
constexpr auto UNIFORM_FREQUENCY_BITS = WalkTreeLUT::UNIFORM_FREQUENCY_BITS;  // 5 (bits)
using CompressedHistogram = WalkTreeLUT::CompressedHistogram;  // uint64_t
constexpr auto PRECODE_BITS = rapidgzip::deflate::PRECODE_BITS;  // 19
constexpr auto HISTOGRAM_BITS = uint16_t( UNIFORM_FREQUENCY_BITS * deflate::MAX_PRECODE_LENGTH - 5U );
static_assert( HISTOGRAM_BITS == 30 );


[[nodiscard]] constexpr CompressedHistogram
removeTwoBits( const CompressedHistogram histogram )
{
    const auto twoBitsRemoved = ( ( histogram >> 2U ) & ~nLowestBitsSet<uint64_t, 3>() )
                                | ( histogram & nLowestBitsSet<uint64_t, 3>() );
    const auto restored = ( ( twoBitsRemoved & ~nLowestBitsSet<uint64_t, 3>() ) << 2U )
                          | ( twoBitsRemoved & nLowestBitsSet<uint64_t, 3>() );
    if ( restored != histogram ) {
        std::cerr << "Tried to compress: " << std::bitset<64>( histogram ) << "\n";
        std::cerr << "Got compressed   : " << std::bitset<64>( twoBitsRemoved ) << "\n";
        std::cerr << "Restored         : " << std::bitset<64>( restored ) << "\n";
        throw std::logic_error( "Transformation not reversible!" );
    }
    return twoBitsRemoved;
}


[[nodiscard]] constexpr CompressedHistogram
rearrangeHistogram(  CompressedHistogram histogram )
{
#if 1
    /* This is much more benign on the number of compile-time expressions than the version below! */
    const auto counts1 = histogram & nLowestBitsSet<uint64_t, UNIFORM_FREQUENCY_BITS>();
    const auto result = ( histogram >> UNIFORM_FREQUENCY_BITS ) | ( counts1 << ( 6 * UNIFORM_FREQUENCY_BITS ) );
    return result;
#else
    std::array<uint8_t, deflate::MAX_PRECODE_LENGTH> bins{};
    for ( size_t i = 0; i < bins.size(); ++i ) {
        bins[i] = ( histogram >> ( i * UNIFORM_FREQUENCY_BITS ) ) & nLowestBitsSet<uint64_t, UNIFORM_FREQUENCY_BITS>();
    }

    auto newBins = bins;
    /* The lowest bin must be that for code-length 2 because it has up to 3 bits, perfect for bit lookup in a byte. */
    newBins[0] = bins[1];

    /**
     * We can rearrange the order of these bins freely to find the order that produces the most chunk duplicates
     * and therefore the smallest total LUT size! Let's specify the order in terms of accessing the original
     * indexes, e.g., the first one, which is simply bit-shifted: 23456 (->12345)
     * @verbatim
     * Order 23456:
     *   32 B per subtable: 512 KiB +   16896 B ( 66 subtables) -> 541184 B
     *   64 B per subtable: 256 KiB +   37888 B ( 74 subtables) -> 300032 B
     *  128 B per subtable: 128 KiB +   75776 B ( 74 subtables) -> 206848 B <-
     *  256 B per subtable: 64  KiB +  256000 B (125 subtables) -> 321536 B
     *  512 B per subtable: 32  KiB +  487424 B (119 subtables) -> 520192 B
     * 1024 B per subtable: 16  KiB +  778240 B ( 95 subtables) -> 794624 B
     * 2048 B per subtable: 8   KiB + 1294336 B ( 79 subtables) -> 1302528 B
     *
     * Order 32456:
     *
     *   32 B per subtable: 512 KiB +   18944 B ( 74) -> 543232 B
     *   64 B per subtable: 256 KiB +   37888 B ( 74) -> 300032 B
     *  128 B per subtable: 128 KiB +   75776 B ( 74) -> 206848 B <-
     *  256 B per subtable: 64  KiB +  256000 B (125) -> 321536 B
     *  512 B per subtable: 32  KiB +  487424 B (119) -> 520192 B
     * 1024 B per subtable: 16  KiB +  778240 B ( 95) -> 794624 B
     * 2048 B per subtable: 8   KiB + 1294336 B ( 79) -> 1302528 B
     *
     * Order 65432:
     *   32 B per subtable: 512 KiB +   23296 B ( 91 subtables) -> 547584 B
     *   64 B per subtable: 256 KiB +   60416 B (118 subtables) -> 322560 B
     *  128 B per subtable: 128 KiB +  121856 B (119 subtables) -> 252928 B
     *  256 B per subtable: 64  KiB +  155648 B ( 76 subtables) -> 221184 B <-
     *  512 B per subtable: 32  KiB +  360448 B ( 88 subtables) -> 393216 B
     * 1024 B per subtable: 16  KiB +  827392 B (101 subtables) -> 843776 B
     * 2048 B per subtable: 8   KiB + 1425408 B ( 87 subtables) -> 1433600 B
     *
     * Order 62345:
     *   32 B per subtable: 512 KiB +   28160 B (110 subtables) -> 552448 B
     *   64 B per subtable: 256 KiB +   56320 B (110 subtables) -> 318464 B
     *  128 B per subtable: 128 KiB +  112640 B (110 subtables) -> 243712 B <-
     *  256 B per subtable: 64  KiB +  382976 B (187 subtables) -> 448512 B
     *  512 B per subtable: 32  KiB +  860160 B (210 subtables) -> 892928 B
     * 1024 B per subtable: 16  KiB + 1613824 B (197 subtables) -> 1630208 B
     * 2048 B per subtable: 8   KiB + 2867200 B (175 subtables) -> 2875392 B
     *
     * Order 65234:
     *   32 B per subtable: 512 KiB +   23296 B ( 91 subtables) ->  547584 B
     *   64 B per subtable: 256 KiB +   60416 B (118 subtables) ->  322560 B
     *  128 B per subtable: 128 KiB +  121856 B (119 subtables) ->  252928 B <-
     *  256 B per subtable: 64  KiB +  382976 B (187 subtables) ->  448512 B
     *  512 B per subtable: 32  KiB +  688128 B (168 subtables) ->  720896 B
     * 1024 B per subtable: 16  KiB + 1286144 B (157 subtables) -> 1302528 B
     * 2048 B per subtable: 8   KiB + 2572288 B (157 subtables) -> 2580480 B
     * @endverbatim
     *  -> Well, seems like the first order, is already the most optimal. Too bad we cannot reduce it further.
     */
    newBins[1] = bins[2];
    newBins[2] = bins[3];
    newBins[3] = bins[4];
    newBins[4] = bins[5];
    newBins[5] = bins[6];

    /* The highest-order bin must be that for code-length 1 because it can only have up to 2 bits, making
     * easy truncation of the 3 higher bits in this 5-bit-width bin easily possible before look-up in the table! */
    newBins[6] = bins[0];

    uint64_t reconstructed{ 0 };
    for ( size_t i = 0; i < newBins.size(); ++i ) {
        reconstructed |= static_cast<uint64_t>( newBins[i] ) << ( i * UNIFORM_FREQUENCY_BITS );
    }
    return reconstructed;
#endif
}


/* Not constexpr because it would kill the compiler. Probably should simply be precomputed. */
template<uint16_t CHUNK_COUNT>
static const auto PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES =
    [] ()
    {
        /* The histogram uses 5 bits per bin, but if we do some checking, which we do, then that histogram would
         * have some higher bits always zero: 0b11111'11111'11111'11111'01111'00111'00011
         * Without proper fast PEXT instruction support, it can get quite expensive to bit-compress the histogram
         * further. However, especially the last bits are juicy, even more so because we are doing shifts already
         * anyway to access the bit mask! Namely, the lowest 3 bits are used to access the uint8_t.
         * Only the lowest 2 bits can be something else then 0, but oh well, at least we can get rid of bit 4 and 5
         * for free!
         *                      2 bits we can remove for free (no additional bitwise instructions)
         *                                            ++
         *      0b11111'11111'11111'11111'01111'00111'00011
         *        |                                ||   | |
         *        |                                ||   +-+ access bits in uint8_t
         *        |                                ||     |
         *        |                                |+-----+
         *        |                     Access inside one chunk (CHUNK_COUNT == 1 -> 6 bits for 64-bit chunk)
         *        +--------------------------------+
         *        Used for access into compressedLUT
         * Another idea would be to switch it up! Use the first three bits to the end / use them for
         * bit access, because the last bits may filter out much more. See rearrangeHistogram.
         */
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
                validHistograms.push_back( removeTwoBits( rearrangeHistogram( histogramToSetValid ) ) );
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


#if ! defined( LIBRAPIDARCHIVE_SKIP_LOAD_FROM_CSV )

/**
 * Load precomputed LUTs from run-length encoded CSV at compile-time for optimal case of template parameters.
 */
template<>
const auto PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<128U> =
    [] ()
    {
        constexpr std::array<uint8_t, 882> histogramLUT = {
            #include "PRECODE_FREQUENCIES_VALID_FULL_LUT_TWO_STAGES_128_HISTOGRAM_TO_INDEX.csv"
        };
        constexpr std::array<uint8_t, 2921> validLUT = {
            #include "PRECODE_FREQUENCIES_VALID_FULL_LUT_TWO_STAGES_128_INDEX_TO_VALID.csv"
        };

        return std::make_tuple(
            SimpleRunLengthEncoding::simpleRunLengthDecode<std::array<uint8_t, 128_Ki> >( histogramLUT, 128_Ki ),
            SimpleRunLengthEncoding::simpleRunLengthDecode<std::array<uint8_t, 74_Ki> >( validLUT, 74_Ki ) );
    }();

#endif


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
 * cmake --build . -- benchmarkGzipBlockFinder && taskset 1 src/benchmarks/benchmarkGzipBlockFinder
 * @verbatim
 * [13 bits, Walk Tree Compressed Single LUT (optimized)] ( 68.3 <= 71.9 +- 1.7 <= 73.6 ) MB/s
 * [14 bits, Walk Tree Compressed Single LUT (optimized)] ( 63.8 <= 71.3 +- 2.9 <= 73.6 ) MB/s
 * [15 bits, Walk Tree Compressed Single LUT (optimized)] ( 70.8 <= 71.8 +- 0.6 <= 72.4 ) MB/s
 * [16 bits, Walk Tree Compressed Single LUT (optimized)] ( 70.2 <= 71.4 +- 1.1 <= 73.1 ) MB/s
 * [17 bits, Walk Tree Compressed Single LUT (optimized)] ( 71.5 <= 72.8 +- 0.7 <= 73.5 ) MB/s
 * [18 bits, Walk Tree Compressed Single LUT (optimized)] ( 70.6 <= 71.4 +- 0.4 <= 71.7 ) MB/s
 *
 * [13 bits, this with chunk count    1] ( 65.5 <= 68.3 +- 1.2 <= 69.3 ) MB/s
 * [14 bits, this with chunk count    1] ( 65.7 <= 67.2 +- 1.1 <= 68.7 ) MB/s
 * [15 bits, this with chunk count    1] ( 64.0 <= 65.9 +- 1.2 <= 67.8 ) MB/s
 * [16 bits, this with chunk count    1] ( 65.7 <= 67.2 +- 0.8 <= 68.0 ) MB/s
 * [17 bits, this with chunk count    1] ( 43   <= 61   +- 10  <= 67   ) MB/s
 * [18 bits, this with chunk count    1] ( 43   <= 60   +- 6   <= 63   ) MB/s
 *
 * [13 bits, this with chunk count    8] ( 68.8 <= 70.8 +- 1.1 <= 71.7 ) MB/s
 * [14 bits, this with chunk count    8] ( 69.4 <= 71.6 +- 0.8 <= 72.1 ) MB/s
 * [15 bits, this with chunk count    8] ( 66.3 <= 69.7 +- 1.5 <= 71.1 ) MB/s
 * [16 bits, this with chunk count    8] ( 68.7 <= 70.5 +- 1.1 <= 71.9 ) MB/s
 * [17 bits, this with chunk count    8] ( 70.9 <= 72.0 +- 0.4 <= 72.3 ) MB/s
 * [18 bits, this with chunk count    8] ( 53   <= 62   +- 7   <= 70   ) MB/s
 *
 * [13 bits, this with chunk count  128] ( 71.4 <= 73.5 +- 1.0 <= 74.5 ) MB/s
 * [14 bits, this with chunk count  128] ( 72.5 <= 73.0 +- 0.5 <= 73.6 ) MB/s
 * [15 bits, this with chunk count  128] ( 68.9 <= 71.8 +- 1.1 <= 72.6 ) MB/s
 * [16 bits, this with chunk count  128] ( 69.8 <= 70.9 +- 1.1 <= 73.1 ) MB/s
 * [17 bits, this with chunk count  128] ( 66.7 <= 71.0 +- 2.2 <= 74.3 ) MB/s
 * [18 bits, this with chunk count  128] ( 68.6 <= 71.4 +- 1.0 <= 72.0 ) MB/s
 *
 *  -> size-optimal with 202 KiB.
 *     Still not quite nothing, but better than 2 MiB in the original WalkTreeLUT with a full LUT for all bins.
 *
 * [13 bits, this with chunk count  256] ( 69.3  <= 72.0  +- 1.5  <= 73.1  ) MB/s
 * [14 bits, this with chunk count  256] ( 71.6  <= 72.6  +- 0.5  <= 73.1  ) MB/s
 * [15 bits, this with chunk count  256] ( 69.6  <= 71.2  +- 0.8  <= 71.9  ) MB/s
 * [16 bits, this with chunk count  256] ( 69.0  <= 70.4  +- 1.4  <= 72.6  ) MB/s
 * [17 bits, this with chunk count  256] ( 72.75 <= 73.23 +- 0.30 <= 73.71 ) MB/s
 * [18 bits, this with chunk count  256] ( 70.9  <= 71.3  +- 0.3  <= 72.0  ) MB/s
 *
 * [13 bits, this with chunk count  512] ( 66.6 <= 70.7 +- 2.4 <= 72.8 ) MB/s
 * [14 bits, this with chunk count  512] ( 68.0 <= 71.8 +- 2.1 <= 73.7 ) MB/s
 * [15 bits, this with chunk count  512] ( 68.7 <= 71.1 +- 1.2 <= 72.2 ) MB/s
 * [16 bits, this with chunk count  512] ( 70.7 <= 72.3 +- 0.9 <= 73.3 ) MB/s
 * [17 bits, this with chunk count  512] ( 70.6 <= 72.7 +- 1.1 <= 73.7 ) MB/s
 * [18 bits, this with chunk count  512] ( 68.9 <= 71.0 +- 1.0 <= 72.0 ) MB/s
 *
 * [13 bits, this with chunk count 1024] ( 72.9 <= 74.3 +- 0.6 <= 74.7 ) MB/s
 * [14 bits, this with chunk count 1024] ( 65.5 <= 73.1 +- 2.7 <= 74.1 ) MB/s
 * [15 bits, this with chunk count 1024] ( 69.3 <= 71.7 +- 1.4 <= 72.9 ) MB/s
 * [16 bits, this with chunk count 1024] ( 70.7 <= 72.5 +- 0.8 <= 73.1 ) MB/s
 * [17 bits, this with chunk count 1024] ( 71.9 <= 73.1 +- 0.5 <= 73.7 ) MB/s
 * [18 bits, this with chunk count 1024] ( 66.9 <= 70.1 +- 1.3 <= 71.1 ) MB/s
 *
 * @endverbatim
 * -> It is insanely stable over the SUBTABLE_CHUNK_COUNT parameter. It made me question the correctness, but
 *    for CHUNK_COUNT < 8, there finally are some measurable slowdowns.
 * @endverbatim
 */
template<uint16_t SUBTABLE_CHUNK_COUNT = 128U>
[[nodiscard]] inline rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    constexpr auto INDEX_BITS = requiredBits( SUBTABLE_CHUNK_COUNT * sizeof( uint64_t ) * CHAR_BIT );
    constexpr auto HIGH_BITS_TO_BE_ZERO = 0b11100'00000'00000'00000'00000'10000'11000ULL;

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );
    const auto histogram = precodesToHistogram<4>( precodeBits );
    const auto valueToLookUp = histogram & ~HIGH_BITS_TO_BE_ZERO;

    /* Lookup in LUT and subtable */
    const auto& [histogramLUT, validLUT] = PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<SUBTABLE_CHUNK_COUNT>;
    const auto subIndex = histogramLUT[valueToLookUp >> ( INDEX_BITS + 2U )];
    const auto bitMaskToTest = 1ULL << ( valueToLookUp & 0b111U );
    const auto validIndex = ( subIndex << ( INDEX_BITS - 3U ) )
                            | ( ( valueToLookUp >> 5U ) & nLowestBitsSet<uint64_t>( INDEX_BITS - 3U ) );
    const auto valid = validLUT[validIndex] & bitMaskToTest;

    /* It seems that the branching || is fine as long as we compute everything beforehand.
     * This probably lets the compiler prefetch, for example, validLUT. */
    return ( ( histogram & HIGH_BITS_TO_BE_ZERO ) != 0 ) || ( valid == 0 )
           ? rapidgzip::Error::INVALID_CODE_LENGTHS
           : rapidgzip::Error::NONE;
}
}  // namespace rapidgzip::PrecodeCheck::WalkTreeCompressedSingleLUT
