#include <array>
#include <bitset>
#include <climits>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <core/TestHelpers.hpp>
#include <core/Error.hpp>
#include <filereader/Buffered.hpp>
#include <rapidgzip/blockfinder/precodecheck/BruteForceLUT.hpp>
#include <rapidgzip/blockfinder/precodecheck/SingleCompressedLUT.hpp>
#include <rapidgzip/blockfinder/precodecheck/SingleCompressedLUTOptimized.hpp>
#include <rapidgzip/blockfinder/precodecheck/SingleLUT.hpp>
#include <rapidgzip/blockfinder/precodecheck/WalkTreeCompressedLUT.hpp>
#include <rapidgzip/blockfinder/precodecheck/WalkTreeCompressedSingleLUT.hpp>
#include <rapidgzip/blockfinder/precodecheck/WalkTreeLUT.hpp>
#include <rapidgzip/blockfinder/precodecheck/WithoutLUT.hpp>
#include <rapidgzip/gzip/precode.hpp>


using namespace rapidgzip;


/**
 * Use like this: dummyPrintValue<uint32_t, 1234>() and then check for an unused warning:
 * @verbatim
 * testPrecodeCheck.cpp: In instantiation of ‘void dummyPrintValue() [with T = unsigned int; T <anonymous> = 1234]’:
 * testPrecodeCheck.cpp:43:36:   required from here
 * testPrecodeCheck.cpp:31:9: warning: unused variable ‘a’ [-Wunused-variable]
 * @endverbatim
 */
template<typename T, T>
void
dummyPrintValue()
{
    // NOLINTNEXTLINE(clang-diagnostic-unused-variable)
    [[maybe_unused]] int a = 0;
}


using CompressedHistogram = rapidgzip::PrecodeCheck::WalkTreeLUT::CompressedHistogram;


[[nodiscard]] rapidgzip::Error
checkPrecodeDirectly( size_t   next4Bits,
                      uint64_t precodeBits )
{
    using namespace rapidgzip::deflate;

    const auto codeLengthCount = 4 + next4Bits;

    /* Get code lengths (CL) for alphabet P. */
    std::array<uint8_t, MAX_PRECODE_COUNT> precodeCL{};
    std::memset( precodeCL.data(), 0, precodeCL.size() * sizeof( precodeCL[0] ) );
    for ( size_t i = 0; i < codeLengthCount; ++i ) {
        precodeCL[PRECODE_ALPHABET[i]] = ( precodeBits >> ( i * 3U ) ) & 0b111U;
    }

    rapidgzip::deflate::precode::PrecodeHuffmanCoding precodeHC;
    return precodeHC.initializeFromLengths( VectorView<uint8_t>( precodeCL.data(), precodeCL.size() ) );
}


void
testVLPHImplementation()
{
    using namespace rapidgzip::PrecodeCheck::SingleLUT::VariableLengthPackedHistogram;

    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 0 ) == 0b01010 );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 1 ) == 0b1     );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 2 ) == 0b10    );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 3 ) == 0b100   );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 4 ) == 0b0101  );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 5 ) == 0b10101 );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 6 ) == 0b10001 );
    static_assert( getCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 7 ) == 0b1101  );

    static_assert( setCount( 0b1101'10001'10101'0101'100'10'1'01010UL, 4, 0b1111 )
                   == 0b1101'10001'10101'1111'100'10'1'01010UL );
    static_assert( setCount( 0b1111'11111'11111'1111'111'11'1'11111UL, 4, 0b1111 )
                   == 0b1111'11111'11111'1111'111'11'1'11111UL );
    static_assert( setCount( 0b0000'00000'00000'0000'000'00'0'00000UL, 4, 0b1111 )
                   == 0b0000'00000'00000'1111'000'00'0'00000UL );

    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 0 )
                   ==              0b0'1101'10001'10101'0101'100'10'1'01011UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 1 )
                   ==              0b1'1101'10001'10101'0101'100'11'0'01010UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 2 )
                   ==              0b0'1101'10001'10101'0101'100'11'1'01010UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 3 )
                   ==              0b0'1101'10001'10101'0101'101'10'1'01010UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 4 )
                   ==              0b0'1101'10001'10101'0110'100'10'1'01010UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 5 )
                   ==              0b0'1101'10001'10110'0101'100'10'1'01010UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 6 )
                   ==              0b0'1101'10010'10101'0101'100'10'1'01010UL );
    static_assert( incrementCount( 0b0'1101'10001'10101'0101'100'10'1'01010UL, 7 )
                   ==              0b0'1110'10001'10101'0101'100'10'1'01010UL );

    constexpr auto getHistogram =
        [] ( const auto values ) { return calculateHistogram</* VALUE_BITS */ 3, /* VALUE_COUNT */ 4>( values ); };

    static_assert( getHistogram( 0b000'000'000'000 ) == 0b0'0000'00000'00000'0000'000'00'0'00000UL );
    static_assert( getHistogram( 0b111'111'111'111 ) == 0b0'0100'00000'00000'0000'000'00'0'00100UL );
    static_assert( getHistogram( 0b111'001'000'111 ) == 0b0'0010'00000'00000'0000'000'00'1'00011UL );
    static_assert( getHistogram( 0b111'001'001'111 ) == 0b1'0010'00000'00000'0000'000'01'0'00100UL );
    static_assert( getHistogram( 0b010'010'010'010 ) == 0b1'0000'00000'00000'0000'001'00'0'00100UL );
    static_assert( getHistogram( 0b001'010'001'001 ) == 0b1'0000'00000'00000'0000'000'10'1'00100UL );
    /* @note calculateHistogram allows to overflow the individual counts to keep associativity for the part
     *       without overflow bits. */

    /* In C++20 we could have used static_assert because it has constexpr == and std::equal. */
    const std::array<uint8_t, 8> EXPECTED_MEMBER_OFFSETS = { 0, 5, 6, 8, 11, 15, 20, 25 };
    REQUIRE( MEMBER_OFFSETS == EXPECTED_MEMBER_OFFSETS );
}


void
testSingleLUTImplementation4Precodes()
{
    /* With only 4 precodes, there will be no overflow issues when adding partial histograms because only the
     * first one will be non-zero. */

    using namespace rapidgzip::PrecodeCheck;

    const auto check4Precodes =
        [] ( const auto values ) { return SingleLUT::checkPrecode( 0, values ); };

    static_assert( check4Precodes( 0 ) != rapidgzip::Error::NONE );

    /* Only one non-zero value that is not 1 leads to a non-optimal tree. */
    REQUIRE( check4Precodes( 0b000'000'000'010 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'000'011 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'000'100 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'010'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'011'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'100'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'010'000'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'011'000'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'100'000'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b010'000'000'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b011'000'000'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b100'000'000'000 ) != rapidgzip::Error::NONE );

    REQUIRE( check4Precodes( 0b000'000'001'000 ) == rapidgzip::Error::NONE );

    REQUIRE_EQUAL( checkPrecodeDirectly    ( 0, 0b001'000'000'001 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( WithoutLUT::checkPrecode( 0, 0b001'000'000'001 ), rapidgzip::Error::NONE );

    REQUIRE_EQUAL( checkPrecodeDirectly    ( 0, 0b010'000'010'001 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( WithoutLUT::checkPrecode( 0, 0b010'000'010'001 ), rapidgzip::Error::NONE );

    REQUIRE_EQUAL( checkPrecodeDirectly              ( 0, 0b000'000'001'000 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( WithoutLUT::checkPrecodeUsingArray( 0, 0b000'000'001'000 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( WithoutLUT          ::checkPrecode( 0, 0b000'000'001'000 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( SingleLUT           ::checkPrecode( 0, 0b000'000'001'000 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( SingleCompressedLUT ::checkPrecode( 0, 0b000'000'001'000 ), rapidgzip::Error::NONE );
    REQUIRE_EQUAL( WalkTreeLUT         ::checkPrecode( 0, 0b000'000'001'000 ), rapidgzip::Error::NONE );

    REQUIRE_EQUAL( WithoutLUT::checkPrecodeUsingArray( 0, 0b000'000'010'000 ),
                   rapidgzip::Error::BLOATING_HUFFMAN_CODING );
    REQUIRE_EQUAL( checkPrecodeDirectly             ( 0, 0b000'000'010'000 ), rapidgzip::Error::BLOATING_HUFFMAN_CODING );
    REQUIRE_EQUAL( WithoutLUT         ::checkPrecode( 0, 0b000'000'010'000 ), rapidgzip::Error::BLOATING_HUFFMAN_CODING );
    REQUIRE_EQUAL( SingleLUT          ::checkPrecode( 0, 0b000'000'010'000 ), rapidgzip::Error::BLOATING_HUFFMAN_CODING );
    REQUIRE_EQUAL( SingleCompressedLUT::checkPrecode( 0, 0b000'000'010'000 ), rapidgzip::Error::BLOATING_HUFFMAN_CODING );
    /* Because of the usage of a LUT, the error reasong can not always be exactly deduced. In that case,
     * non-optimal Huffman codings will be reported as invalid ones! */
    REQUIRE_EQUAL( WalkTreeLUT::checkPrecode( 0, 0b000'000'010'000 ), rapidgzip::Error::INVALID_CODE_LENGTHS );

    /* A single code length with 1 bit is valid. */
    REQUIRE( check4Precodes( 0b000'000'000'001 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'001'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'000'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'000'000 ) == rapidgzip::Error::NONE );

    /* Two non-zero values are only valid if both of them are of length 1. */
    REQUIRE( check4Precodes( 0b001'001'000'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'001'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'000'001 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'001'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'000'001 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'001'001 ) == rapidgzip::Error::NONE );

    REQUIRE( WithoutLUT::checkPrecodeUsingArray ( 0, 0b000'000'001'001 ) == rapidgzip::Error::NONE );
    REQUIRE( WithoutLUT           ::checkPrecode( 0, 0b000'000'001'001 ) == rapidgzip::Error::NONE );
    REQUIRE( SingleLUT            ::checkPrecode( 0, 0b000'000'001'001 ) == rapidgzip::Error::NONE );
    REQUIRE( SingleCompressedLUT  ::checkPrecode( 0, 0b000'000'001'001 ) == rapidgzip::Error::NONE );
    REQUIRE( WalkTreeLUT          ::checkPrecode( 0, 0b000'000'001'001 ) == rapidgzip::Error::NONE );

    /* If there is a code length longer than one out of the two, then the tree will be non-optimal. */
    REQUIRE( check4Precodes( 0b001'011'000'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'011'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'000'011 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'011'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'000'011 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'011'001 ) != rapidgzip::Error::NONE );

    /* Even with 3 values, there is still only one tree that is valid: code lengths: 1, 2, 2. */
    REQUIRE( check4Precodes( 0b001'010'010'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'010'000'010 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'010'000'010 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b010'001'010'000 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'010'010 ) == rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'010'010'001 ) == rapidgzip::Error::NONE );

    REQUIRE( check4Precodes( 0b001'010'011'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b011'010'000'010 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'110'000'010 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b010'001'011'000 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'010'110 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b000'010'010'101 ) != rapidgzip::Error::NONE );

    /* And even with 4 values, there is still only one tree that is valid: code lengths: 2, 2, 2, 2. */
    REQUIRE( check4Precodes( 0b010'010'010'010 ) == rapidgzip::Error::NONE );

    /* Too many of the same value overflows the variable-length bit-packed histogram. This was detected in earlier
     * versions, but the detection is too expensive, so simply depend on the subsequent more strict checks for speed. */
    //REQUIRE( check4Precodes( 0b001'010'001'001 ) != rapidgzip::Error::NONE );
    REQUIRE( check4Precodes( 0b001'010'001'001 ) == rapidgzip::Error::NONE );

    /* Because it 19 with code length 7 will overflow the histogram, do note expect a correct result.
     * But, more importantly, check that it does not segfault when accessing the LUT with such an overflow! */
    REQUIRE( check4Precodes( ~uint64_t( 0 ) ) != rapidgzip::Error::NONE );
}


void
testSingleLUTImplementation8Precodes()
{
    /* Starting with these tests there is more than one valid tree configuration and addition of partial histograms
     * comes into play and can be tested. */

    constexpr auto check8Precodes =
        [] ( const auto values ) { return rapidgzip::PrecodeCheck::SingleLUT::checkPrecode( 4, values ); };

    /**
     * For 5 non-zero precodes, there can be multiple tree configurations:
     * @verbatim
     *    /\                /\
     *   o  \      CL 1    o  \
     *      /\                /\
     *     o  \    CL 2      /  \
     *        /\            /\  /\
     *       o  o  CL 3    o  oo  o
     * @endverbatim
     */
    static_assert( check8Precodes( 0b000'000'000'100'100'011'010'001 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b000'000'100'100'011'010'001'000 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b000'100'100'011'010'001'000'000 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b100'100'011'010'001'000'000'000 ) == rapidgzip::Error::NONE );

    using namespace rapidgzip::PrecodeCheck::SingleLUT;

    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b011'011'011'011 ) == 0b0'0000'00000'00000'0000'100'00'0'00100UL );
    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b000'000'000'001 ) == 0b0'0000'00000'00000'0000'000'00'1'00001UL );
    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b011'011'011'011 ) +
                   PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b000'000'000'001 ) == 0b0'0000'00000'00000'0000'100'00'1'00101UL );

    static_assert( check8Precodes( 0b000'000'000'001'011'011'011'011 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b000'000'000'011'011'011'011'001 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b000'000'011'011'011'011'001'000 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b000'011'011'011'011'001'000'000 ) == rapidgzip::Error::NONE );
    static_assert( check8Precodes( 0b011'011'011'011'001'000'000'000 ) == rapidgzip::Error::NONE );

    /* With 4 non-zero precodes, we can now check the overflow algorithm. */
}


template<uint32_t CHUNKED_NEIGHBORS,
         typename LUT>
[[nodiscard]] size_t
countUniqueValues( const LUT& lut )
{
    std::set<std::array<uint64_t, CHUNKED_NEIGHBORS> > uniqueValues;
    for ( size_t i = 0; i < lut.size(); i += CHUNKED_NEIGHBORS ) {
        std::array<uint64_t, CHUNKED_NEIGHBORS> valueChunk{};
        for ( size_t j = 0; j < CHUNKED_NEIGHBORS; ++j ) {
            valueChunk[j] = lut[i + j];
        }
        uniqueValues.emplace( valueChunk );
    }
    return uniqueValues.size();
}


template<uint32_t UINT64_COUNT,
         typename LUT>
[[nodiscard]] std::string
printLUTStats( const LUT& lut )
{
    const auto uniqueValues = countUniqueValues<UINT64_COUNT>( lut );

    std::stringstream result;
    result << "64-bit chunks: " << UINT64_COUNT << ", unique values: " << uniqueValues;

    /* When directly addressing uint64_t values in a generic std::array<uint64_t>, we need more addresses
     * when chunking. */
    const auto uniqueValueAddresses = uniqueValues * UINT64_COUNT;
    /* Round up to 8 bits per byte because bit-packing is arithmetically expensive. */
    const auto requiredBytes = static_cast<int>( std::ceil( std::log2( uniqueValueAddresses ) / 8 ) );
    result << ", address size: " << formatBytes( requiredBytes ) << "\n        ";

    const auto reducedLUTSize = lut.size() / UINT64_COUNT * requiredBytes;
    const auto valueLUTSize = uniqueValues * UINT64_COUNT * sizeof( uint64_t );

    result << "LUT: " << std::setw( 7 ) << formatBytes( reducedLUTSize ) << "\n"
           << "            value LUT (bits) : " << formatBytes( valueLUTSize )
           << " -> SUM: " << formatBytes( reducedLUTSize + valueLUTSize ) << "\n"
           << "            value LUT (bytes): " << formatBytes( valueLUTSize * CHAR_BIT )
           << " -> SUM: " << formatBytes( reducedLUTSize + valueLUTSize * CHAR_BIT );

    return std::move( result ).str();
}


template<typename LUT>
void
analyzeSingleLUTCompression( const LUT& precodeHistogramValidLUT )
{
    std::set<uint64_t> uniqueBitMasks;
    for ( const auto bitMask : precodeHistogramValidLUT ) {
        uniqueBitMasks.emplace( bitMask );
    }

    std::cerr << "Unique precode histogram lookup 64-bit compressed results:";
    for ( const auto bitMask : uniqueBitMasks ) {
        std::cerr << " " << bitMask;
    }
    std::cerr << "\n";

    /**
     * The histogram LUT is sized: 2 MiB and contains 25 unique values
     * -> We could compress the LUT values by storing the unique value ID in a second LUT.
     *    The unique value ID could be stored in 5 bits but 8-bits is probably better and the size of
     *    the second LUT is 25 * sizeof( uint64_t ) = 200 B, while the first LUT shrink from 64-bit values down to
     *    8-bit values, i.e., 2 MiB -> 256 KiB.
     *    -> We might even explode the 200 B values up by 8x (1600 B) to store the truth flags in bytes instead
     *       of bits to save some bit fiddling.
     * - It would look like this (assuming CHUNKED_NEIGHBORS is a power of 2):
     *   testValid(DICT[LUT[histogram >> ( CHUNKED_NEIGHBORS - 1 )] + ( histogram % CHUNKED_NEIGHBORS ) * CHUNK_SIZE])
     */
    std::cerr << "The histogram LUT is sized: "
              << formatBytes( precodeHistogramValidLUT.size() * sizeof( precodeHistogramValidLUT[0] ) ) << "\n"
              << "By adding another layer of indirection to compress duplicate values in a dictionary (LUT), we can\n"
              << "save further bytes. Calculations are done for different value sizes in chunks of one 64-bit value\n"
              << "up to multiple 64-bit values mapped to a single dictionary (LUT) entry:\n"
              << "\n"
              << "    " << printLUTStats< 1>( precodeHistogramValidLUT ) << "\n"
              << "    " << printLUTStats< 2>( precodeHistogramValidLUT ) << "\n"
              << "    " << printLUTStats< 4>( precodeHistogramValidLUT ) << "\n"
              << "    " << printLUTStats< 8>( precodeHistogramValidLUT ) << "\n"
              << "    " << printLUTStats<16>( precodeHistogramValidLUT ) << "\n"
              << "    " << printLUTStats<32>( precodeHistogramValidLUT ) << "\n"
              << "\n";
}


void
testSingleLUTImplementation()
{
    using namespace rapidgzip::PrecodeCheck::SingleLUT;

    testVLPHImplementation();

    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b000'000'000'000 ) == 0b0'0000'00000'00000'0000'000'00'0'00000UL );
    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b111'111'111'111 ) == 0b0'0100'00000'00000'0000'000'00'0'00100UL );
    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b111'001'000'111 ) == 0b0'0010'00000'00000'0000'000'00'1'00011UL );
    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b111'001'001'111 ) == 0b1'0010'00000'00000'0000'000'01'0'00100UL );
    static_assert( PRECODE_X4_TO_HISTOGRAM_LUT.at( 0b010'010'010'010 ) == 0b1'0000'00000'00000'0000'001'00'0'00100UL );

    testSingleLUTImplementation4Precodes();
    testSingleLUTImplementation8Precodes();
}


template<uint32_t FREQUENCY_COUNT>
void
analyzeValidPrecodeFrequencies()
{
    /* Without static, I'm getting SIGSEV! It might be that this results in a classical stack overflow because
     * those std::array LUTs are allocated on the insufficiently-sized stack when not static. */
    using namespace rapidgzip::PrecodeCheck;
    static const auto frequencyLUT = WalkTreeLUT::createPrecodeFrequenciesValidLUT<5, FREQUENCY_COUNT>();
    static const auto frequencyLUTAlternative = BruteForceLUT::createPrecodeFrequenciesValidLUT<5, FREQUENCY_COUNT>();
    REQUIRE_EQUAL( frequencyLUT.size(), frequencyLUTAlternative.size() );
    REQUIRE( frequencyLUT == frequencyLUTAlternative );

    const auto sizeInBytes = frequencyLUT.size() * sizeof( frequencyLUT[0] );
    std::cerr << "Precode frequency LUT containing " << static_cast<int>( FREQUENCY_COUNT ) << " bins is sized: "
              << formatBytes( sizeInBytes ) << ". ";

    uint64_t validCount{ 0 };
    for ( const auto& bits : frequencyLUT ) {
        validCount += std::bitset<std::numeric_limits<std::decay_t<decltype( bits )> >::digits>( bits ).count();
    }
    std::cerr << "There are " << validCount << " valid entries out of " << sizeInBytes * CHAR_BIT << " -> "
              << static_cast<double>( validCount ) / static_cast<double>( sizeInBytes * CHAR_BIT ) * 100 << " %\n";
}


template<typename CheckPrecode,
         typename RandomEngine>
uint64_t
testValidPrecodes( const CheckPrecode&                                   checkPrecode,
                   RandomEngine&                                         randomEngine,
                   const uint64_t                                        repetitionCount,
                   const std::string_view                                title,
                   std::unordered_map<rapidgzip::Error, uint64_t>* const errorCounts = nullptr,
                   const bool                                            isExact = false )
{
    uint64_t validPrecodeCount{ 0 };
    uint64_t filteredValids{ 0 };  // Precodes that are valid but were filtered erroneously (forbidden!)
    uint64_t unfilteredInvalids{ 0 };  // Precodes that are invalid but not filtered (allowed)
    for ( uint64_t i = 0; i < repetitionCount; ++i ) {
        using namespace rapidgzip::deflate;
        const auto precodeBits = randomEngine();
        const auto next4Bits = precodeBits & nLowestBitsSet<uint64_t, 4>();
        const auto next57Bits = ( precodeBits >> 4U ) & nLowestBitsSet<uint64_t>( MAX_PRECODE_COUNT * PRECODE_BITS );

        const auto error = rapidgzip::PrecodeCheck::WalkTreeLUT::checkPrecode( next4Bits, next57Bits );

        if ( errorCounts != nullptr ) {
            const auto [count, wasInserted] = errorCounts->try_emplace( error, 1 );
            if ( !wasInserted ) {
                count->second++;
            }
        }

        const auto isValid = error == rapidgzip::Error::NONE;
        validPrecodeCount += isValid ? 1 : 0;

        /* Compare ground truth with alternative checkPrecode functions. */
        const auto alternativeIsValid = checkPrecode( next4Bits, next57Bits ) == rapidgzip::Error::NONE;
        if ( alternativeIsValid && !isValid ) {
            ++unfilteredInvalids;
        }
        if ( !alternativeIsValid && isValid ) {
            ++filteredValids;
            REQUIRE( isValid && alternativeIsValid );
            const auto codeLengthCount = 4 + next4Bits;
            std::cerr << "    next 4 bits: " << std::bitset<4>( next4Bits ) << ", next 57 bits: "
                      << ( next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS ) ) << "\n";
        }
    }

    /* Filtered valid precodes are not allowed because we cannot recover from this!
     * Forgetting to filter some invalid precodes is allowed because there are stronger checks following the
     * precode check. The precode check was only introduced as a kind of "cache" to speed up those stronger checks! */
    REQUIRE_EQUAL( filteredValids, uint64_t( 0 ) );
    if ( isExact ) {
        REQUIRE_EQUAL( unfilteredInvalids, uint64_t( 0 ) );
    }

    if ( !title.empty() && unfilteredInvalids ) {
        std::cout << title << " is inexact. Number of invalid precodes that were not filtered: " << unfilteredInvalids
                  << " (" << static_cast<double>( unfilteredInvalids ) / static_cast<double>( repetitionCount ) * 100
                  << " %)\n";
    }

    return validPrecodeCount;
}


void
analyzeAndTestValidPrecodes()
{
    std::mt19937_64 randomEngine;

    /* Because we can not exhaustively search all 2^61 possible configurations, use Monte-Carlo sampling.
     * Actually, the search space is a bit smaller because the 57 bits are the maximum and the actual length
     * depends on the 4 bits. */
    static constexpr uint64_t MONTE_CARLO_TEST_COUNT = 100'000'000;
    uint64_t alternativesCount{ 0 };
    uint64_t validPrecodeCount{ 0 };
    std::unordered_map<rapidgzip::Error, uint64_t> errorCounts;

    /* Compare with alternative checkPrecode functions. */
    const auto checkExactAlternative =
        [&] ( const auto&            checkPrecode,
              const std::string_view title )
        {
            validPrecodeCount +=
                testValidPrecodes( checkPrecode, randomEngine, MONTE_CARLO_TEST_COUNT, title, &errorCounts, true );
            ++alternativesCount;
        };

    /* Titles are basically those in toString( CheckPrecodeMethod ) in benchmarkGzipBlockFinder. */
    using namespace rapidgzip::PrecodeCheck;
    checkExactAlternative( WithoutLUT::checkPrecodeUsingArray, "Without LUT" );
    checkExactAlternative( WithoutLUT::checkPrecode, "Without LUT Using Array" );

    const auto checkWithInexactErrorCode =
        [&] ( const auto&            checkPrecode,
              const std::string_view title )
        {
            testValidPrecodes( checkPrecode, randomEngine, MONTE_CARLO_TEST_COUNT, title, nullptr, true );
        };

    /* "Walk Tree LUT" is our ground truth, therefore not listed here separately. */
    checkWithInexactErrorCode( WalkTreeCompressedLUT::checkPrecode<4U, 4U>, "Walk Tree Compressed LUT (4 bins)" );
    checkWithInexactErrorCode( WalkTreeCompressedLUT::checkPrecode<5U, 16U>, "Walk Tree Compressed LUT (5 bins)" );
    checkWithInexactErrorCode( WalkTreeCompressedLUT::checkPrecode<6U, 64U>, "Walk Tree Compressed LUT (6 bins)" );
    checkWithInexactErrorCode( WalkTreeCompressedLUT::checkPrecode<7U, 512U>, "Walk Tree Compressed LUT (all bins)" );
    checkWithInexactErrorCode( WalkTreeCompressedSingleLUT::checkPrecode<512U>, "Walk Tree Compressed Single LUT" );

    /* The error code might be inexact (except for Error::NONE) AND it may even forget to miss to filter some invalid
     * precodes, which need to be filtered by subsequent more strict tests. */
    const auto checkFuzzyAlternative =
        [&] ( const auto&            checkPrecode,
              const std::string_view title )
        {
            testValidPrecodes( checkPrecode, randomEngine, MONTE_CARLO_TEST_COUNT, title, nullptr, false );
        };

    /* Single LUT is inexact. Number of invalid precodes that were not filtered: 368161 (0.368161 %)
     * Single Compressed LUT is inexact. Number of invalid precodes that were not filtered: 367646 (0.367646 %) */
    checkFuzzyAlternative( SingleLUT::checkPrecode, "Single LUT" );
    checkFuzzyAlternative( SingleCompressedLUT::checkPrecode<8U>, "Single Compressed LUT" );
    checkFuzzyAlternative( SingleCompressedLUTOptimized::checkPrecode<8U>, "Single Compressed LUT (optimized)" );
    std::cerr << "\n";

    {
        const auto totalCount = alternativesCount * MONTE_CARLO_TEST_COUNT;
        std::cerr << "Valid precodes " << validPrecodeCount << " out of " << totalCount << " tested -> "
                  << static_cast<double>( validPrecodeCount ) / static_cast<double>( totalCount ) * 100
                  << " %\n";

        std::multimap<uint64_t, rapidgzip::Error, std::greater<> > sortedErrorTypes;
        for ( const auto& [error, count] : errorCounts ) {
            sortedErrorTypes.emplace( count, error );
        }
        std::cerr << "Encountered errors:\n";
        for ( const auto& [count, error] : sortedErrorTypes ) {
            std::cerr << "    " << std::setw( 8 ) << count << " " << toString( error ) << "\n";
        }
        std::cerr << "\n";
    }

    /* Test frequency LUT */
}


template<uint8_t FREQUENCY_BITS,
         uint8_t FREQUENCY_COUNT>
[[nodiscard]] std::string
printCompressedHistogram( const CompressedHistogram histogram )
{
    std::stringstream result;
    for ( size_t length = 1; length <= FREQUENCY_COUNT; ++length ) {
        const auto count = static_cast<uint32_t>(
            ( histogram >> ( ( length - 1 ) * FREQUENCY_BITS ) )
            & nLowestBitsSet<uint64_t, FREQUENCY_BITS>() );
        if ( result.tellp() > 0 ) {
            result << " ";
        }
        result << length << ":" << count;
    }
    result << " (0x" << std::hex << std::setfill( '0' ) << std::setw( 64 / /* nibble */ 4 ) << histogram << ")";
    return std::move( result ).str();
}


constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
static constexpr uint32_t FREQUENCY_BITS = 5;  /* minimum bits to represent up to count 19. */
static constexpr uint32_t FREQUENCY_COUNT = 7;  /* maximum value with 3-bits */


[[nodiscard]] std::unordered_set<uint64_t>
computeValidPrecodeHistograms()
{
    std::unordered_set<uint64_t> validHistograms;
    const auto processValidHistogram =
        [&validHistograms] ( const uint64_t validHistogram ) constexpr { validHistograms.insert( validHistogram ); };

    PrecodeCheck::WalkTreeLUT::walkValidPrecodeCodeLengthFrequencies<FREQUENCY_BITS, FREQUENCY_COUNT>(
        processValidHistogram, MAX_CL_SYMBOL_COUNT );

    return validHistograms;
}


[[nodiscard]] std::unordered_set<uint64_t>
computeValidPrecodeHistogramsPacked()
{
    std::unordered_set<uint64_t> validHistograms;
    const auto processValidHistogram =
        [&validHistograms] ( const uint64_t validHistogram ) constexpr
        {
            constexpr auto MASK = 0b11111'11111'11111'11111'01111'00111'00011ULL;
            if ( ( validHistogram & MASK ) != validHistogram ) {
                throw std::logic_error( "Unexpected histogram!" );
            }

            const auto packed = ( ( validHistogram & 0b11111'11111'11111'11111'00000'00000'00000ULL ) >> 6U ) |
                                ( ( validHistogram & 0b01111'00000'00000ULL ) >> 5U ) |
                                ( ( validHistogram & 0b00111'00000ULL ) >> 3U ) |
                                ( validHistogram & 0b00011ULL );
            validHistograms.insert( packed );
        };

    PrecodeCheck::WalkTreeLUT::walkValidPrecodeCodeLengthFrequencies<FREQUENCY_BITS, FREQUENCY_COUNT>(
        processValidHistogram, MAX_CL_SYMBOL_COUNT );

    return validHistograms;
}


template<typename Substring,
         size_t   HISTOGRAM_LENGTH_IN_BITS,
         typename HistogramContainer>
void
analyzePrecodeHistogramLinearSearchTables( const HistogramContainer& validHistograms )
{
    constexpr auto SUBSTRING_LENGTH_IN_BITS = std::numeric_limits<Substring>::digits;

    std::unordered_set<Substring> validSubstrings;
    for ( size_t shift = 0; shift <= HISTOGRAM_LENGTH_IN_BITS - SUBSTRING_LENGTH_IN_BITS; ++shift ) {
        validSubstrings.clear();
        for ( const auto histogram : validHistograms ) {
            validSubstrings.insert( static_cast<Substring>( ( histogram >> shift )
                                    & nLowestBitsSet<Substring, SUBSTRING_LENGTH_IN_BITS>() ) );
        }

        const auto filterRate = 100.0 * static_cast<double>( validSubstrings.size() )
                                / static_cast<double>( 1ULL << SUBSTRING_LENGTH_IN_BITS );
        std::cerr << "Valid histogram substrings (" << SUBSTRING_LENGTH_IN_BITS << "-bit) (start position: lowest bit "
                  << shift << "): " << validSubstrings.size() << " -> filter rate: (" << filterRate << " %)\n";
    }

    std::cerr << "\n";
}

/**
 * The idea here is to avoid lookup tables, which are 99.999% zeros, and instead store all valid histograms
 * in a vector to linearly search. 1526 histograms * 64 bit would be 12768 B, which seems too large to linearly
 * search, especially as the most frequent case would be to not find a match, i.e., would have to read everything!
 *
 * For filtering, we do not have to be exact, therefore, similarly to nested lookup tables, we could search for
 * prefixes, suffixes, or even midfixes in a vector! Substring lengths of 8-bit, 16-bit or 32-bit would be ideal.
 * These might also be implemented with SIMD (or memchr for 8-bit?) by the compiler.
 * Naively, one would assume that the set size to linearly search would reduce in size respectively to the bits.
 * However, we might get lucky and get overlapping substrings, therefore yielding superlinear size reductions!
 * Let's test for that.
 *
 * @todo Compute actual filter rate with random data.
 *
 * Implement, test, and benchmark vectorized linear searches:

static inline bool contains_u16_u64(const uint16_t* p, size_t count, uint16_t key) noexcept {
    // Broadcast key to 4×16-bit words
    uint64_t k = uint64_t(key) * 0x0001000100010001ULL;

    for (size_t i = 0; i < count; i += 4) {
        uint64_t v;
        std::memcpy(&v, p + i, sizeof(v));  // safe unaligned load

        uint64_t x = v ^ k;  // 16-bit lane will be 0 if equal

        // Check if any 16-bit lane == 0
        // (x - 0x00010001...) & ~x & 0x8000800080008000ULL trick
        uint64_t z = (x - 0x0001000100010001ULL) & ~x & 0x8000800080008000ULL;
        if (z) return true;
    }
    return false;
}
 */
void
analyzePrecodeHistogramLinearSearchTables()
{
    const auto encodeHistogram =
        [] ( const auto& histogram )
        {
            constexpr std::array<uint8_t, deflate::MAX_PRECODE_LENGTH> MEMBER_BIT_WIDTHS = { 1, 2, 3, 3, 3, 2, 2 };
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
        };

    for ( size_t precodeCount = 4; precodeCount <= 19; ++precodeCount ) {
        std::set<uint16_t> validSmallHistograms;
        size_t totalHistograms{ 0 };
        for ( const auto& histogram : deflate::precode::VALID_HISTOGRAMS ) {
            const auto sum = std::accumulate( histogram.begin(), histogram.end(), size_t( 0 ) );
            if ( sum <= precodeCount ) {
                validSmallHistograms.insert( encodeHistogram( histogram ) );
                ++totalHistograms;
            }
        }

        const auto avxRegisterCount = static_cast<double>( validSmallHistograms.size() ) * CHAR_BIT / 256.;
        std::cout << "precode count: " << precodeCount << ": valid histograms packed into 16-bit: "
                  << validSmallHistograms.size() << " (out of " << totalHistograms << " qualifying) -> "
                  << avxRegisterCount << " 256-bit registers\n";
        /*
         * precode count:  4: 16-bit histograms:    5 (out of    5 qualifying) ->  0.15625  256-bit registers
         * precode count:  5: 16-bit histograms:    8 (out of    8 qualifying) ->  0.25     256-bit registers
         * precode count:  6: 16-bit histograms:   13 (out of   13 qualifying) ->  0.40625  256-bit registers
         * precode count:  7: 16-bit histograms:   22 (out of   22 qualifying) ->  0.6875   256-bit registers
         * precode count:  8: 16-bit histograms:   38 (out of   38 qualifying) ->  1.1875   256-bit registers
         * precode count:  9: 16-bit histograms:   65 (out of   65 qualifying) ->  2.03125  256-bit registers
         * precode count: 10: 16-bit histograms:  107 (out of  107 qualifying) ->  3.34375  256-bit registers
         * precode count: 11: 16-bit histograms:  166 (out of  166 qualifying) ->  5.1875   256-bit registers
         * precode count: 12: 16-bit histograms:  246 (out of  246 qualifying) ->  7.6875   256-bit registers
         *  -> This would be a very nice cut-off, especially as the ~8 register can be nicely aggregated
         *     in a binary tree fashion to reduce operation count and dependence. [(1|2)|(3|4)]|[(5|6)|(7|8)]
         * precode count: 13: 16-bit histograms:  350 (out of  350 qualifying) -> 10.9375   256-bit registers
         * precode count: 14: 16-bit histograms:  476 (out of  476 qualifying) -> 14.875    256-bit registers
         * precode count: 15: 16-bit histograms:  627 (out of  627 qualifying) -> 19.5938   256-bit registers
         * precode count: 16: 16-bit histograms:  808 (out of  808 qualifying) -> 25.25     256-bit registers
         * precode count: 17: 16-bit histograms: 1016 (out of 1017 qualifying) -> 31.75     256-bit registers
         *  -> This is the first case for which we get duplicates because of overflows.
         *     At this point the compressed histogram is more like a bloom filter via "hashes".
         * precode count: 18: 16-bit histograms: 1250 (out of 1255 qualifying) -> 39.0625   256-bit registers
         * precode count: 19: 16-bit histograms: 1514 (out of 1526 qualifying) -> 47.3125   256-bit registers
         */
    }

    {
        using namespace PrecodeCheck::SingleCompressedLUTOptimized::ShortVariableLengthPackedHistogram;

        alignas( 64 ) constexpr auto validHistograms = createValidHistogramsList<12>();
        std::cout << "Valid histograms with maximum precode count 12: " << validHistograms.size() << "\n";
        for ( const auto histogram : validHistograms ) {
            std::cout << " " << histogram;
        }
        std::cout << "\n";

        for ( const uint16_t value : { 2, 3, 4, 5, 37496 } ) {
            std::cout << "Contains " << value << ": " << std::boolalpha
                      << containsU16AVX2<validHistograms.size()>( validHistograms.data(), value ) << "\n";
        }
    }

    const auto validHistograms = computeValidPrecodeHistograms();
    std::cerr << "Valid histograms: " << validHistograms.size() << "\n\n";

    /* We can pack the histogram by using variable-bit-length per histogram bin.
     * This can save us 6 bits: 7 * 5 = 35 -> 29 bits. */
    const auto validHistogramsWithInvalidBinBitSet = std::count_if(
        validHistograms.begin(), validHistograms.end(), [] ( const auto histogram ) {
            return ( histogram & 0b10000'11000'11100ULL ) != 0ULL;
        } );
    std::cerr << "Valid histograms with invalid bin bits set: " << validHistogramsWithInvalidBinBitSet << "\n\n";  // 0

    constexpr auto HIGHEST_BIN_BIT_MASK = 0b10000'10000'10000'10000'11000'11100'11110ULL;
    /* Valid histograms with highest bin bit set: 1->1, 2 ->, 2->4, 3->7, 4->8, 5->9, 6->10, 7->11
     * The idea is to shave off even more bits because the highest allowed count per bin is often 0b10..0, i.e.,
     * only a single number with that highest bit is valid. We could therefore simply check in a very short "list"
     * of valid histogram counts in case any of those higher bits is set and then remove them to get an even smaller
     * lookup table. This also kinda is a nested LUT, but different.
     *
     * if ( ( histogram & HIGHEST_BIN_BIT_MASK ) != 0ULL ) {
     *     return validHistogramsWithThoseHighestBinBits.contains( histogram )
     *            ? Error::NONE : ERROR::INVALID_CODE_LENGTHS;
     * }
     * return validLUT[_pext_u64( ~HIGHEST_BIN_BIT_MASK )] ? Error::NONE : ERROR::INVALID_CODE_LENGTHS;
     *
     * As shown, above we have some trade-off. As each valid histogram takes up 64-bit and a cache line is often 64 B,
     * using those with 8 possible valid values might make sense. I don't think a second cache line would kill us
     * though and would save us 3 more bits, i.e., 8x more size reduction!
     * A 256-bit (32 B) SIMD register can hold 4 64-bit values. I.e., we could check existence with 2-3 SIMD
     * comparisons plus 2 or so logical ORs!
     * This would let us compress the histogram from 35 bits -> 29 bits -> 22 bits (4 Mi values, storable in 512 KiB!)
     */
    for ( size_t binCount : { 1U, 2U, 3U, 4U, 5U, 6U, 7U } ) {
        const auto bitMask = HIGHEST_BIN_BIT_MASK
                             & ( nLowestBitsSet<uint64_t>( binCount * 5U ) << ( ( 7U - binCount ) * 5U ) );
        const auto count = std::count_if(
            validHistograms.begin(), validHistograms.end(), [bitMask] ( const auto histogram ) {
                return ( histogram & bitMask ) != 0ULL;
            } );
        std::cerr << "Valid histograms with highest bin bit set (" << binCount << " largest CL bins only): "
                  << count << "\n";
    }
    std::cerr << "\n";

    /* We can extend the above idea to not only cut off the highest bits but also the next one. I fear that this
     * explodes the number of valid histogram counts we have to look up, but it might not be as bad as feared.
     * Valid histograms with highest two bin bits set (1-7 largest CL bins only):
     *   1->181, 2->359, 3->521, 4->633, 5->865, 6->1131, 7->1407
     * -> it is as bad as I feared, even worse. The last one comes close to the total of 1526 valid histograms!
     */
    for ( size_t binCount : { 1U, 2U, 3U, 4U, 5U, 6U, 7U } ) {
        constexpr auto TWO_HIGHEST_BIN_BIT_MASK = 0b11000'11000'11000'11000'11100'11110'11111ULL;
        const auto bitMask = TWO_HIGHEST_BIN_BIT_MASK
                             & ( nLowestBitsSet<uint64_t>( binCount * 5U ) << ( ( 7U - binCount ) * 5U ) );
        const auto count = std::count_if(
            validHistograms.begin(), validHistograms.end(), [bitMask] ( const auto histogram ) {
                return ( histogram & bitMask ) != 0ULL;
            } );
        std::cerr << "Valid histograms with highest two bin bits set (" << binCount << " largest CL bins only): "
                  << count << "\n";
    }
    std::cerr << "\n";

    /* 1->1, 2->542, 3->966, 4->1218, 5->1355, 6->1431, 7->1473 -> trash idea */
    for ( size_t binCount : { 1U, 2U, 3U, 4U, 5U, 6U, 7U } ) {
        constexpr auto HIGHEST_AND_LOWEST_BIN_BIT_MASK = 0b10001'10001'10001'10001'11001'11101'11111ULL;
        const auto bitMask = HIGHEST_AND_LOWEST_BIN_BIT_MASK
                             & ( nLowestBitsSet<uint64_t>( binCount * 5U ) << ( ( 7U - binCount ) * 5U ) );
        const auto count = std::count_if(
            validHistograms.begin(), validHistograms.end(), [bitMask] ( const auto histogram ) {
                return ( histogram & bitMask ) != 0ULL;
            } );
        std::cerr << "Valid histograms with highest and lowest bin bits set (" << binCount << " largest CL bins only): "
                  << count << "\n";
    }
    std::cerr << "\n";

    analyzePrecodeHistogramLinearSearchTables<uint8_t, FREQUENCY_BITS * FREQUENCY_COUNT>( validHistograms );
    analyzePrecodeHistogramLinearSearchTables<uint16_t, FREQUENCY_BITS * FREQUENCY_COUNT>( validHistograms );

    /* Do the same analysis as above but with the compressed histogram representation, which can only be used
     * after doing some checks prior. */
    const auto validHistogramsPacked = computeValidPrecodeHistogramsPacked();
    std::cerr << "Valid histograms (bit-packed): " << validHistograms.size() << "\n\n";

    analyzePrecodeHistogramLinearSearchTables<uint8_t, FREQUENCY_BITS * FREQUENCY_COUNT - 6U>( validHistogramsPacked );
    analyzePrecodeHistogramLinearSearchTables<uint16_t, FREQUENCY_BITS * FREQUENCY_COUNT - 6U>( validHistogramsPacked );
}

/*
Valid histograms: 1526

Valid histogram substrings (8-bit) (start position: lowest bit 0): 9 -> filter rate: (3.51562 %)
Valid histogram substrings (8-bit) (start position: lowest bit 1): 6 -> filter rate: (2.34375 %)
Valid histogram substrings (8-bit) (start position: lowest bit 2): 5 -> filter rate: (1.95312 %)
Valid histogram substrings (8-bit) (start position: lowest bit 3): 9 -> filter rate: (3.51562 %)
Valid histogram substrings (8-bit) (start position: lowest bit 4): 16 -> filter rate: (6.25 %)
Valid histogram substrings (8-bit) (start position: lowest bit 5): 24 -> filter rate: (9.375 %)
Valid histogram substrings (8-bit) (start position: lowest bit 6): 15 -> filter rate: (5.85938 %)
Valid histogram substrings (8-bit) (start position: lowest bit 7): 10 -> filter rate: (3.90625 %)
Valid histogram substrings (8-bit) (start position: lowest bit 8): 17 -> filter rate: (6.64062 %)
Valid histogram substrings (8-bit) (start position: lowest bit 9): 32 -> filter rate: (12.5 %)
Valid histogram substrings (8-bit) (start position: lowest bit 10): 56 -> filter rate: (21.875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 11): 44 -> filter rate: (17.1875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 12): 27 -> filter rate: (10.5469 %)
Valid histogram substrings (8-bit) (start position: lowest bit 13): 34 -> filter rate: (13.2812 %)
Valid histogram substrings (8-bit) (start position: lowest bit 14): 59 -> filter rate: (23.0469 %)
Valid histogram substrings (8-bit) (start position: lowest bit 15): 101 -> filter rate: (39.4531 %)
Valid histogram substrings (8-bit) (start position: lowest bit 16): 84 -> filter rate: (32.8125 %)
Valid histogram substrings (8-bit) (start position: lowest bit 17): 46 -> filter rate: (17.9688 %)
Valid histogram substrings (8-bit) (start position: lowest bit 18): 49 -> filter rate: (19.1406 %)
Valid histogram substrings (8-bit) (start position: lowest bit 19): 56 -> filter rate: (21.875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 20): 93 -> filter rate: (36.3281 %)
Valid histogram substrings (8-bit) (start position: lowest bit 21): 76 -> filter rate: (29.6875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 22): 44 -> filter rate: (17.1875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 23): 27 -> filter rate: (10.5469 %)
Valid histogram substrings (8-bit) (start position: lowest bit 24): 18 -> filter rate: (7.03125 %)
Valid histogram substrings (8-bit) (start position: lowest bit 25): 29 -> filter rate: (11.3281 %)
Valid histogram substrings (8-bit) (start position: lowest bit 26): 40 -> filter rate: (15.625 %)
Valid histogram substrings (8-bit) (start position: lowest bit 27): 24 -> filter rate: (9.375 %)

Valid histogram substrings (16-bit) (start position: lowest bit 0): 61 -> filter rate: (0.0930786 %)
Valid histogram substrings (16-bit) (start position: lowest bit 1): 82 -> filter rate: (0.125122 %)
Valid histogram substrings (16-bit) (start position: lowest bit 2): 125 -> filter rate: (0.190735 %)
Valid histogram substrings (16-bit) (start position: lowest bit 3): 153 -> filter rate: (0.233459 %)
Valid histogram substrings (16-bit) (start position: lowest bit 4): 154 -> filter rate: (0.234985 %)
Valid histogram substrings (16-bit) (start position: lowest bit 5): 268 -> filter rate: (0.408936 %)
Valid histogram substrings (16-bit) (start position: lowest bit 6): 345 -> filter rate: (0.526428 %)
Valid histogram substrings (16-bit) (start position: lowest bit 7): 339 -> filter rate: (0.517273 %)
Valid histogram substrings (16-bit) (start position: lowest bit 8): 415 -> filter rate: (0.63324 %)
Valid histogram substrings (16-bit) (start position: lowest bit 9): 417 -> filter rate: (0.636292 %)
Valid histogram substrings (16-bit) (start position: lowest bit 10): 667 -> filter rate: (1.01776 %)
Valid histogram substrings (16-bit) (start position: lowest bit 11): 857 -> filter rate: (1.30768 %)
Valid histogram substrings (16-bit) (start position: lowest bit 12): 598 -> filter rate: (0.912476 %)
Valid histogram substrings (16-bit) (start position: lowest bit 13): 464 -> filter rate: (0.708008 %)
Valid histogram substrings (16-bit) (start position: lowest bit 14): 464 -> filter rate: (0.708008 %)
Valid histogram substrings (16-bit) (start position: lowest bit 15): 464 -> filter rate: (0.708008 %)
Valid histogram substrings (16-bit) (start position: lowest bit 16): 406 -> filter rate: (0.619507 %)
Valid histogram substrings (16-bit) (start position: lowest bit 17): 237 -> filter rate: (0.361633 %)
Valid histogram substrings (16-bit) (start position: lowest bit 18): 183 -> filter rate: (0.279236 %)
Valid histogram substrings (16-bit) (start position: lowest bit 19): 150 -> filter rate: (0.228882 %)

Valid histograms (bit-packed): 1526

Valid histogram substrings (8-bit) (start position: lowest bit 0): 34 -> filter rate: (13.2812 %)
Valid histogram substrings (8-bit) (start position: lowest bit 1): 26 -> filter rate: (10.1562 %)
Valid histogram substrings (8-bit) (start position: lowest bit 2): 45 -> filter rate: (17.5781 %)
Valid histogram substrings (8-bit) (start position: lowest bit 3): 49 -> filter rate: (19.1406 %)
Valid histogram substrings (8-bit) (start position: lowest bit 4): 57 -> filter rate: (22.2656 %)
Valid histogram substrings (8-bit) (start position: lowest bit 5): 80 -> filter rate: (31.25 %)
Valid histogram substrings (8-bit) (start position: lowest bit 6): 45 -> filter rate: (17.5781 %)
Valid histogram substrings (8-bit) (start position: lowest bit 7): 51 -> filter rate: (19.9219 %)
Valid histogram substrings (8-bit) (start position: lowest bit 8): 60 -> filter rate: (23.4375 %)
Valid histogram substrings (8-bit) (start position: lowest bit 9): 101 -> filter rate: (39.4531 %)
Valid histogram substrings (8-bit) (start position: lowest bit 10): 84 -> filter rate: (32.8125 %)
Valid histogram substrings (8-bit) (start position: lowest bit 11): 46 -> filter rate: (17.9688 %)
Valid histogram substrings (8-bit) (start position: lowest bit 12): 49 -> filter rate: (19.1406 %)
Valid histogram substrings (8-bit) (start position: lowest bit 13): 56 -> filter rate: (21.875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 14): 93 -> filter rate: (36.3281 %)
Valid histogram substrings (8-bit) (start position: lowest bit 15): 76 -> filter rate: (29.6875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 16): 44 -> filter rate: (17.1875 %)
Valid histogram substrings (8-bit) (start position: lowest bit 17): 27 -> filter rate: (10.5469 %)
Valid histogram substrings (8-bit) (start position: lowest bit 18): 18 -> filter rate: (7.03125 %)
Valid histogram substrings (8-bit) (start position: lowest bit 19): 29 -> filter rate: (11.3281 %)
Valid histogram substrings (8-bit) (start position: lowest bit 20): 40 -> filter rate: (15.625 %)
Valid histogram substrings (8-bit) (start position: lowest bit 21): 24 -> filter rate: (9.375 %)

Valid histogram substrings (16-bit) (start position: lowest bit 0): 445 -> filter rate: (0.679016 %)
Valid histogram substrings (16-bit) (start position: lowest bit 1): 555 -> filter rate: (0.846863 %)
Valid histogram substrings (16-bit) (start position: lowest bit 2): 557 -> filter rate: (0.849915 %)
Valid histogram substrings (16-bit) (start position: lowest bit 3): 557 -> filter rate: (0.849915 %)
Valid histogram substrings (16-bit) (start position: lowest bit 4): 668 -> filter rate: (1.01929 %)
Valid histogram substrings (16-bit) (start position: lowest bit 5): 975 -> filter rate: (1.48773 %)
Valid histogram substrings (16-bit) (start position: lowest bit 6): 975 -> filter rate: (1.48773 %)
Valid histogram substrings (16-bit) (start position: lowest bit 7): 677 -> filter rate: (1.03302 %)
Valid histogram substrings (16-bit) (start position: lowest bit 8): 465 -> filter rate: (0.709534 %)
Valid histogram substrings (16-bit) (start position: lowest bit 9): 464 -> filter rate: (0.708008 %)
Valid histogram substrings (16-bit) (start position: lowest bit 10): 406 -> filter rate: (0.619507 %)
Valid histogram substrings (16-bit) (start position: lowest bit 11): 237 -> filter rate: (0.361633 %)
Valid histogram substrings (16-bit) (start position: lowest bit 12): 183 -> filter rate: (0.279236 %)
Valid histogram substrings (16-bit) (start position: lowest bit 13): 150 -> filter rate: (0.228882 %)

 -> Huh, it is Unexpected that packing leads to worse look up tables. But, I guess this is to be expected,
    because the first n-bits are the most "compressible" and therefore, including more bits, leads to
    more random values.
 -> The filter rate is also wrong. We would have to test with some random data to get the actual filter
    rate because the histogram value to look up is not uniformly random, quite far from it!
 -> The 61 16-bit values fit into 2 cache lines (64 B) and 4 256-bit SIMD registers!
    This sounds like it should be pretty fast.
*/


template<bool COMPARE_WITH_ALTERNATIVE_METHOD>
void
analyzeMaxValidPrecodeFrequencies()
{
    std::array<uint32_t, FREQUENCY_COUNT> maxFrequencies{};
    std::unordered_set<uint64_t> validHistograms;

    const auto processValidHistogram =
        [&maxFrequencies, &validHistograms] ( const uint64_t validHistogram ) constexpr
        {
            validHistograms.insert( validHistogram );

            for ( size_t codeLength = 0; codeLength < maxFrequencies.size(); ++codeLength ) {
                const auto count = static_cast<uint32_t>( ( validHistogram >> ( codeLength * FREQUENCY_BITS ) )
                                                          & nLowestBitsSet<uint64_t, FREQUENCY_BITS>() );
                maxFrequencies[codeLength] = std::max( maxFrequencies[codeLength], count );

                if ( count >= 16 ) {
                    std::cerr << "Valid Histogram with >=16 codes of the same length: "
                              << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( validHistogram ) << "\n";
                }
            }
        };

    PrecodeCheck::WalkTreeLUT::walkValidPrecodeCodeLengthFrequencies<FREQUENCY_BITS, FREQUENCY_COUNT>(
        processValidHistogram, MAX_CL_SYMBOL_COUNT );

    std::cerr << "\nMaximum length frequencies of valid histograms:\n";
    for ( size_t length = 1; length <= FREQUENCY_COUNT; ++length ) {
        std::cerr << "    Code Length " << length << " : " << maxFrequencies[length - 1] << "\n";
    }
    std::cerr << "\n";

    std::cerr << "Found in total " << validHistograms.size() << " valid histograms (corresponding to the maximum of "
              << "7 bins) equaling " << formatBytes( validHistograms.size() * sizeof( uint64_t ) ) << "\n";


    /* Check whether we can really ignore the 7-counts as the same number of valid histograms for 6 and 7 suggests.
     * -> We cannot IGNORE it! Rather, given a valid histogram with counts [1,6] specifies an exact required 7-count
     *    to keep the validity. Unfortunately, this cannot be used to trim down the LUT further because we need
     *    to test the 7-count, which filters another 255 out of 256 cases out. But, knowing that 6 counts already
     *    filters 700k values down to 1, it might be possible to do a more costly check for those rare possible values.
     */

    const auto getCount =
        [] ( const uint64_t histogram,
             const uint32_t codeLength )
        {
            assert( codeLength >= 1 );
            return ( histogram >> ( ( codeLength - 1 ) * FREQUENCY_BITS ) )
                   & nLowestBitsSet<uint64_t, FREQUENCY_BITS>();
        };

    std::unordered_set<uint64_t> alternativeValidHistogramsWithout7Counts;
    static constexpr auto HISTOGRAM_COUNT_WITHOUT_7_COUNTS = 1ULL << ( FREQUENCY_BITS * ( FREQUENCY_COUNT - 1 ) );
    for ( uint64_t histogram = 0; histogram < HISTOGRAM_COUNT_WITHOUT_7_COUNTS; ++histogram ) {
        if ( rapidgzip::PrecodeCheck::BruteForceLUT::checkPrecodeFrequencies<FREQUENCY_BITS, ( FREQUENCY_COUNT - 1 ) >(
                 histogram ) != rapidgzip::Error::NONE ) {
            continue;
        }

        /* For 0 or 1 code-lengths with 1 bit, there may be non-zero unused bits! */
        if ( histogram < 2 ) {
            alternativeValidHistogramsWithout7Counts.insert( histogram );
            continue;
        }

        /* Calculate unused symbol count */
        uint8_t unusedSymbolCount{ 2 };
        for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT - 1; ++bitLength ) {
            unusedSymbolCount -= getCount( histogram, bitLength );
            unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
        }

        const auto histogramWith7Count = histogram | ( static_cast<uint64_t>( unusedSymbolCount )
                                                       << ( ( FREQUENCY_COUNT - 1 ) * FREQUENCY_BITS ) );
        alternativeValidHistogramsWithout7Counts.insert( histogramWith7Count );
    }
    REQUIRE_EQUAL( validHistograms.size(), alternativeValidHistogramsWithout7Counts.size() );
    REQUIRE( validHistograms == alternativeValidHistogramsWithout7Counts );

    if ( validHistograms != alternativeValidHistogramsWithout7Counts ) {
        std::cerr << "Found in total " << alternativeValidHistogramsWithout7Counts.size()
                  << " valid histograms (corresponding to the maximum of 7 bins) equaling "
                  << formatBytes( alternativeValidHistogramsWithout7Counts.size() * sizeof( uint64_t ) ) << "\n";

        const auto alternativeIsSuperset = std::all_of(
            validHistograms.begin(), validHistograms.end(),
            [&] ( auto histogram ) { return contains( alternativeValidHistogramsWithout7Counts, histogram ); } );
        std::cerr << "Alternative histograms IS " << ( alternativeIsSuperset ? "" : "NOT " )
                  << "superset of histograms!\n";

        {
            std::cerr << "Histograms valid with alternative method but not with faster one:\n";
            size_t differingHistogramsToPrint{ 10 };
            for ( const auto histogram : alternativeValidHistogramsWithout7Counts ) {
                if ( !contains( validHistograms, histogram ) ) {
                    std::cerr << "    " << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram )
                              << "\n";
                    if ( --differingHistogramsToPrint == 0 ) {
                        break;
                    }
                }
            }
            std::cerr << "   ...\n\n";
        }

        {
            std::cerr << "Histograms valid with faster method but not with alternative one:\n";
            size_t differingHistogramsToPrint{ 10 };
            for ( const auto histogram : validHistograms ) {
                if ( !contains( alternativeValidHistogramsWithout7Counts, histogram ) ) {
                    std::cerr << "    " << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram )
                              << "\n";
                    if ( --differingHistogramsToPrint == 0 ) {
                        break;
                    }
                }
            }
            std::cerr << "   ...\n\n";
        }
    }

    if constexpr ( !COMPARE_WITH_ALTERNATIVE_METHOD ) {
        return;
    }

    std::array<uint32_t, FREQUENCY_COUNT> alternativeMaxFrequencies{};
    std::unordered_set<uint64_t> alternativeValidHistograms;
    static constexpr auto HISTOGRAM_COUNT = 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT );
    for ( uint64_t histogram = 0; histogram < HISTOGRAM_COUNT; ++histogram ) {
        if ( rapidgzip::PrecodeCheck::BruteForceLUT::checkPrecodeFrequencies<FREQUENCY_BITS,
                                                                             FREQUENCY_COUNT>( histogram )
             != rapidgzip::Error::NONE ) {
            continue;
        }

        alternativeValidHistograms.insert( histogram );
        for ( size_t codeLength = 1; codeLength <= FREQUENCY_COUNT; ++codeLength ) {
            const auto count = getCount( histogram, codeLength );
            alternativeMaxFrequencies[codeLength - 1] =
                std::max( alternativeMaxFrequencies[codeLength - 1], static_cast<uint32_t>( count ) );
        }
    }

    if ( validHistograms != alternativeValidHistograms ) {
        std::cerr << "Found in total " << alternativeValidHistograms.size()
                  << " valid histograms (corresponding to the maximum of 7 bins) equaling "
                  << formatBytes( alternativeValidHistograms.size() * sizeof( uint64_t ) ) << "\n";

        const auto alternativeIsSuperset = std::all_of(
            validHistograms.begin(), validHistograms.end(),
            [&] ( auto histogram ) { return contains( alternativeValidHistograms, histogram ); } );
        std::cerr << "Alternative histograms IS " << ( alternativeIsSuperset ? "" : "NOT " )
                  << "superset of histograms!\n";

        std::cerr << "Histograms valid with alternative method but not with faster one:\n";
        size_t differingHistogramsToPrint{ 10 };
        for ( const auto histogram : alternativeValidHistograms ) {
            if ( !contains( validHistograms, histogram ) ) {
                std::cerr << "    " << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram ) << "\n";
                if ( --differingHistogramsToPrint == 0 ) {
                    break;
                }
            }
        }
        std::cerr << "...\n\n";
    }

    REQUIRE( maxFrequencies == alternativeMaxFrequencies );
    REQUIRE_EQUAL( validHistograms.size(), alternativeValidHistograms.size() );
    REQUIRE( validHistograms == alternativeValidHistograms );
}


void
printValidHistograms()
{
    using deflate::precode::VALID_HISTOGRAMS;
    std::cerr << "== Valid histograms (" << VALID_HISTOGRAMS.size() << ") shown as \"code length: count\" ==\n\n";
    for ( const auto& histogram : VALID_HISTOGRAMS ) {
        std::cerr << "   ";
        for ( size_t i = 0; i < histogram.size(); ++i ) {
            if ( histogram[i] > 0 ) {
                std::cerr << " " << ( i + 1 ) << ":" << static_cast<int>( histogram[i] );
            }
        }
        std::cerr << "\n";
    }
    std::cerr << "\n";
}


void
printValidHistogramsByPrecodeCount()
{
    using deflate::precode::VALID_HISTOGRAMS;

    struct Statistics
    {
        size_t validHistograms{ 0 };
        size_t maxCodeLength{ 0 };
        std::array<uint8_t, deflate::MAX_PRECODE_LENGTH> maxFrequencies{ 0 };
    };

    std::array<Statistics, deflate::MAX_PRECODE_COUNT + 1> statisticsByPrecodeCount{};
    for ( const auto& histogram : VALID_HISTOGRAMS ) {
        const auto precodeCount = std::accumulate( histogram.begin(), histogram.end(), size_t( 0 ) );
        auto& statistics = statisticsByPrecodeCount.at( precodeCount );
        ++statistics.validHistograms;

        for ( size_t i = 0; i < histogram.size(); ++i ) {
            if ( histogram[i] > 0 ) {
                statistics.maxCodeLength = std::max( statistics.maxCodeLength, i + 1 );
            }
        }

        for ( size_t i = 0; i < histogram.size(); ++i ) {
            statistics.maxFrequencies[i] = std::max( statistics.maxFrequencies[i], histogram[i] );
        }
    }

    std::cerr << "== Valid histograms (" << VALID_HISTOGRAMS.size() << ") grouped by precode count ==\n\n";
    for ( size_t precodeCount = 0; precodeCount < statisticsByPrecodeCount.size(); ++precodeCount ) {
        auto& statistics = statisticsByPrecodeCount.at( precodeCount );
        std::cout << "  precode count " << precodeCount << " -> valid: " << statistics.validHistograms
                  << ", max code length: " << statistics.maxCodeLength << ", max. frequencies:";
        for ( size_t i = 0; i < statistics.maxFrequencies.size(); ++i ) {
            std::cout << " " << ( i + 1 ) << ":" << static_cast<int>( statistics.maxFrequencies.at( i ) );
        }
        std::cout << "\n";
    }
    std::cerr << "\n";

    /**
     * precode count 0 -> valid: 0, max code length: 0, max. frequencies: 1:0 2:0 3:0 4:0 5:0 6:0 7:0
     * precode count 1 -> valid: 1, max code length: 1, max. frequencies: 1:1 2:0 3:0 4:0 5:0 6:0 7:0
     * precode count 2 -> valid: 1, max code length: 1, max. frequencies: 1:2 2:0 3:0 4:0 5:0 6:0 7:0
     * precode count 3 -> valid: 1, max code length: 2, max. frequencies: 1:1 2:2 3:0 4:0 5:0 6:0 7:0
     * precode count 4 -> valid: 2, max code length: 3, max. frequencies: 1:1 2:4 3:2 4:0 5:0 6:0 7:0
     * precode count 5 -> valid: 3, max code length: 4, max. frequencies: 1:1 2:3 3:4 4:2 5:0 6:0 7:0
     * precode count 6 -> valid: 5, max code length: 5, max. frequencies: 1:1 2:3 3:4 4:4 5:2 6:0 7:0
     * precode count 7 -> valid: 9, max code length: 6, max. frequencies: 1:1 2:3 3:6 4:4 5:4 6:2 7:0
     *
     * Several considerations for precode count <= 7:
     *  - 7 precodes can be done with two chunked-by-4 precode-to-histogram table lookups and one addition.
     *  - We don't even need the bin for code length 7 for storing the valid histograms because max code length is 6.
     *    - We still need to compute it to filter correctly ...
     *  - We only need up to 3 bits per code length -> 3 * 6 = 18 bit
     *    - It is harder to get this to 16 bit to halve the amount of SIMD search operations...
     *    - Based on the maximum frequencies, the required bits would be: 2, 3, 3, 3, 3, 2, 0 = 16!
     *      No overflows for valid histograms! There might be overflows for invalids, but we do not care
     *      for false positives.
     *      Allowing for a single overflow, would get the bits per bin: 1, 2, 3, 2, 2, 1 = 11
     *      ALlowing for overlow in the highest bits is questionable though, as it would overflow the actual number.
     *  - Only 22 valid histograms This seems very searchable with SIMD!
     *    - 22 * 32 bit histograms = 704 bit = 2.75 * 256-bit SIMD register lookups.
     *    - 22 * 16 bit histograms = 352 bit... could be done in single 512-bit SIMD AVX2 (which I don't have)
     *
     * precode count  8 -> valid:  16, max code length: 7, max. frequencies: 1:1 2:3 3:8 4:6  5:4  6:4  7:2
     *  -> Allowing for "exact" overflows except in the highest bits, would get us bit widths: 1, 2, 3, 3, 2, 2, 2 = 15
     * precode count  9 -> valid:  27, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:8  5:6  6:4  7:4
     *  -> Allowing for "exact" overflows except in the highest bits, would get us bit widths: 1, 2, 3, 3, 3, 2, 3 = 17
     *     But, we probably can allow for the highest bin to overflow, it might only net us some more false positives.
     *  -> 65 * 16 = 1040 bit = 4x 256 bit... :( such bad luck. Maybe some of the overflown histograms results
     *     in a duplicate so that is suffices to check 64?
     * precode count 10 -> valid:  42, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:8  5:8  6:6  7:4
     *  -> For 8-10, the required bits would be: 1, 2, 4, 4, 4, 4, 3 = 22, long over 16, so yeah.
     *     I probably could make it work woth 16-bit even with the a single overflow for these!
     *  -> 107 valids * 16-bit = 1712 bit = 6.6875 256-bit SIMD registers. Does not sound much...
     * precode count 11 -> valid:  59, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:8  5:8  6:8  7:6
     *  -> Even this might still be workable because the only overflows are 8s!
     *  -> 2656 b = 332 B = 10.375 x 256 bit SIMD
     *  -> Allowing for "exact" overflows except in the highest bits, would get us bit widths: 1, 2, 3, 3, 3, 3, 3 = 18
     *
     * precode count 12 -> valid:  80, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:10 5:8  6:8  7:8
     *  -> here, I think that overflows become to get difficult because of the 10.
     * precode count 13 -> valid: 104, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:12 5:10 6:8  7:8
     * precode count 14 -> valid: 126, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:12 5:12 6:10 7:8
     * precode count 15 -> valid: 151, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:14 5:12 6:12 7:10
     *
     * Several considerations for precode count <= 7:
     *  - Because the 4 bits for the precode count - 4 is basically random, we have 12 out of 16 assumable values
     *    that lead to this case, i.e., 75 % (including precode count <= 7) or 8 out of 16 -> 50 % (excluding
     *    precode count <= 7). This is a big chunk that could profit from LUT and instruction count optimizations!
     *  - We could do the chunked histogram computation in 4 lookups instead of 5, saving one lookup and addition.
     *  - We would only need 4 bits per bin and could avoid checking for overflows.
     *  - Using variable-length encoding and don't caring about some of the smaller overflows, we could
     *    pack the histogram into 1 2 3 4 4 4 4 bits per bin totaling 22 bits (4 Mi) -> 512 KiB (without compression)
     *    Using a two-staged LUT, would be even more benign if we ignored al valid histograms for counts >= 16!
     *    This would leave 605 valid histograms (8 <= precode count <= 15) or 627 (precode count <= 15),
     *    almost a third of the total valid histogram count, possibly increasing compressibility of the LUT by 3.
     *
     * precode count 16 -> valid: 181, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:16 5:14 6:12 7:12
     * precode count 17 -> valid: 209, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:15 5:16 6:14 7:12
     * precode count 18 -> valid: 238, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:15 5:16 6:16 7:14
     * precode count 19 -> valid: 271, max code length: 7, max. frequencies: 1:1 2:3 3:7 4:15 5:16 6:16 7:16
     */
}


/**
 * @tparam SUBTABLE_SIZE Size is in number bits, i.e., actual size is 2^SUBTABLE_SIZE elements.
 */
template<uint8_t SUBTABLE_SIZE>
void
analyzeCompressedLUT()
{
    using SUBTABLE_ELEMENT = uint16_t;  /* Must be able to store IDs for each of the 1527 valid histogram. */

    using rapidgzip::deflate::precode::VALID_HISTOGRAMS;
    using namespace rapidgzip::PrecodeCheck::SingleLUT;
    using VariableLengthPackedHistogram::MEMBER_BIT_WIDTHS;
    using VariableLengthPackedHistogram::Histogram;
    using VariableLengthPackedHistogram::packHistogram;

    std::array<std::pair</* truncated address */ uint32_t,
                         /* number of valid histograms in same truncated address */ uint16_t>,
               VALID_HISTOGRAMS.size()> counts{};
    for ( const auto& histogram : VALID_HISTOGRAMS ) {
        const auto packedHistogram = packHistogram( histogram );
        if ( !packedHistogram ) {
            continue;
        }

        const auto histogramToLookUp = *packedHistogram >> MEMBER_BIT_WIDTHS.front();
    #if 0
        /* Worse result, twice as many unique addresses. */
        const auto histogramToTakeAddress =
            reverseBits( histogramToLookUp ) >> ( std::numeric_limits<Histogram>::digits - HISTOGRAM_TO_LOOK_UP_BITS );
    #else
        const auto histogramToTakeAddress = histogramToLookUp;
    #endif
        const auto truncatedAddress = histogramToTakeAddress >> SUBTABLE_SIZE;

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

    using AddressType = uint16_t;  /* Must be able to store addresses to all subtables */
    REQUIRE( requiredBits( uniqueAddresses ) <= std::numeric_limits<AddressType>::digits );
    const auto addressTypeSize = uniqueAddresses + 1 <= 256 ? /* uint8_t suffices! */ 1 : sizeof( AddressType );

    const auto lutSize = ( 1ULL << static_cast<uint8_t>( HISTOGRAM_TO_LOOK_UP_BITS - SUBTABLE_SIZE ) )
                         * addressTypeSize;
    const auto subtableCount = uniqueAddresses + 1 /* Empty subtable (no valid histograms) */;
    const auto subtableSize = ( 1U << SUBTABLE_SIZE ) * sizeof( SUBTABLE_ELEMENT );
    std::cerr << "Subtable size in number of bits: " << static_cast<int>( SUBTABLE_SIZE ) << "\n"
              << "    LUT size: " << formatBytes( lutSize ) << "\n"
              << "    Unique Subtables: " << subtableCount << "\n"
              << "    Subtable size: " << formatBytes( subtableSize ) << "\n"
              << "    Subtables size: " << formatBytes( subtableCount * subtableSize ) << "\n"
              << "    Total size: " << formatBytes( lutSize + subtableCount * subtableSize ) << "\n"
              << "\n";
}


void
testGetHistogramId( size_t validId )
{
    using rapidgzip::deflate::precode::VALID_HISTOGRAMS;
    using namespace rapidgzip::PrecodeCheck;

    const auto& histogram = VALID_HISTOGRAMS.at( validId );
    const auto packedHistogram = WalkTreeLUT::packHistogramWithNonZeroCount<5>( histogram );
    using rapidgzip::PrecodeCheck::SingleLUT::ValidHistogramID::getHistogramIdFromUniformlyPackedHistogram;
    REQUIRE_EQUAL( getHistogramIdFromUniformlyPackedHistogram( packedHistogram ), validId );

#if 0
    std::cerr << "histogram:";
    for ( const auto count : histogram ) {
        std::cerr << " " << (int)count;
    }
    std::cerr << "\n";
    std::cerr << "packed histogram with non-zero count: " << packedHistogram << "\n"
              << "    0b";
    for ( size_t i = 0; i < 8 * 5; ++i ) {
        if ( ( i % 5 == 0 ) && ( i > 0 ) ) {
            std::cerr << "'";
        }
        std::cerr << ( ( packedHistogram >> ( 8 * 5 - 1 - i ) ) & 1U );
    }
    std::cerr << "\n";
    std::cerr << "\n";
#endif
}


template<typename HuffmanCoding>
void
testHuffmanCoding( HuffmanCoding&              coding,
                   const std::vector<char>&    encoded,
                   const std::vector<uint8_t>& decoded )
{
    gzip::BitReader bitReader( std::make_unique<BufferedFileReader>( encoded ) );
    for ( const auto expectedSymbol : decoded ) {
        const auto decodedSymbol = coding.decode( bitReader );
        REQUIRE( decodedSymbol.has_value() );
        REQUIRE_EQUAL( static_cast<int>( *decodedSymbol ), static_cast<int>( expectedSymbol ) );
    }
}


void
testValidHuffmanCoding( const size_t                validId,
                        const std::vector<char>&    encoded,
                        const std::vector<uint8_t>& decoded )
{
    testHuffmanCoding( rapidgzip::deflate::precode::VALID_HUFFMAN_CODINGS.at( validId ), encoded, decoded );
}


void
testCachedCodingFromHistogram( const std::array<uint8_t, 7>& histogram,
                               const std::vector<char>&      encoded,
                               const std::vector<uint8_t>&   decoded )
{
    using rapidgzip::PrecodeCheck::SingleLUT::ValidHistogramID::getHistogramIdFromUniformlyPackedHistogram;
    using rapidgzip::PrecodeCheck::WalkTreeLUT::packHistogramWithNonZeroCount;

    testValidHuffmanCoding( getHistogramIdFromUniformlyPackedHistogram( packHistogramWithNonZeroCount<5>( histogram ) ),
                            encoded, decoded );
}


void
testCachedCodingFromPrecodes( const uint64_t              precodeBits,
                              const std::vector<char>&    encoded,
                              const std::vector<uint8_t>& decoded )
{
    using namespace rapidgzip::deflate;
    using namespace rapidgzip::PrecodeCheck;

    /* Get code lengths (CL) for alphabet P. */
    std::array<uint8_t, MAX_PRECODE_COUNT> codeLengthCL{};
    for ( size_t i = 0; i < MAX_PRECODE_COUNT; ++i ) {
        const auto codeLength = ( precodeBits >> ( i * PRECODE_BITS ) ) & nLowestBitsSet<uint64_t, PRECODE_BITS>();
        codeLengthCL[PRECODE_ALPHABET[i]] = codeLength;
    }

    rapidgzip::deflate::precode::PrecodeHuffmanCoding precodeHC;
    auto error = precodeHC.initializeFromLengths( VectorView<uint8_t>( codeLengthCL.data(), codeLengthCL.size() ) );
    REQUIRE( error == rapidgzip::Error::NONE );

    testHuffmanCoding( precodeHC, encoded, decoded );

    /* Alternative method using precached Huffman codings and alphabet translation in post. */

    /* This part is done inside checkPrecode and given as input to readDynamicHuffman. */
    const auto histogram = WalkTreeLUT::precodesToHistogram<PRECODE_BITS>( precodeBits );

    std::array<uint8_t, 8> offsets{};
    for ( size_t codeLength = 1; codeLength <= 7; ++codeLength ) {
        const auto count = ( ( histogram >> ( codeLength * 5 ) ) & nLowestBitsSet<uint64_t, 5>() );
        offsets[codeLength] = offsets[codeLength - 1] + count;
    }
    const auto oldOffsets = offsets;

    std::array<uint8_t, MAX_PRECODE_COUNT> alphabet{};
    for ( size_t symbol = 0; symbol < codeLengthCL.size(); ++symbol ) {
        const auto codeLength = codeLengthCL[symbol];
        if ( codeLength > 0 ) {
            const auto offset = offsets[codeLength - 1]++;
            alphabet[offset] = symbol;
        }
    }

    /* Check whether the partial sums / offsets were used correctly to distribute the alphabet symbols. */
    for ( size_t i = 0; i + 1 < offsets.size(); ++i ) {
        if ( offsets[i + 1] < offsets[i] ) {
            break;
        }

        REQUIRE_EQUAL( offsets[i], oldOffsets[i + 1] );
        if ( offsets[i] != oldOffsets[i + 1] ) {
            std::cerr << "old offsets:\n   ";
            for ( const auto o : oldOffsets ) {
                std::cerr << " " << static_cast<int>( o );
            }
            std::cerr << "\n -> offsets after creating alphabet:\n   ";
            for ( const auto o : offsets ) {
                std::cerr << " " << static_cast<int>( o );
            }
            std::cerr << "\n";
        }
    }

    const auto validId = SingleLUT::ValidHistogramID::getHistogramIdFromUniformlyPackedHistogram( histogram );
    if ( validId >= precode::VALID_HUFFMAN_CODINGS.size() ) {
        throw std::logic_error( "Only valid histograms should be specified in the optional argument!" );
    }
    const auto& cachedCoding = precode::VALID_HUFFMAN_CODINGS[validId];

    /* Check with Huffman coding. */
    {
        gzip::BitReader bitReader( std::make_unique<BufferedFileReader>( encoded ) );
        for ( const auto expectedSymbol : decoded ) {
            const auto decodedSymbol = cachedCoding.decode( bitReader );
            REQUIRE( decodedSymbol.has_value() );
            REQUIRE_EQUAL( static_cast<int>( alphabet[*decodedSymbol] ), static_cast<int>( expectedSymbol ) );
        }
    }
}


void
testValidHistograms()
{
    using namespace rapidgzip::deflate::precode;
    const auto codingsSize = VALID_HUFFMAN_CODINGS.size() * sizeof( VALID_HUFFMAN_CODINGS[0] );
    std::cerr << "Size of valid precomputed precode huffman codings: " << formatBytes( codingsSize ) << "\n";

    using namespace rapidgzip::PrecodeCheck::SingleLUT;
    REQUIRE( ValidHistogramID::getHistogramIdFromUniformlyPackedHistogram( 0 ) >= VALID_HISTOGRAMS.size() );

    testGetHistogramId( 0 );
    testGetHistogramId( 1 );
    testGetHistogramId( 2 );
    testGetHistogramId( 4 );
    testGetHistogramId( 7 );
    testGetHistogramId( 8 );
    testGetHistogramId( 16 );
    testGetHistogramId( 32 );
    testGetHistogramId( 123 );

    std::cerr << "\n";
}


void
testCachedHuffmanCodings()
{
    using namespace rapidgzip::deflate::precode;
    using rapidgzip::deflate::precode::Histogram;

    testValidHuffmanCoding( VALID_HISTOGRAMS.size() - 1, { char( 0b0110'0101 ) }, { 1, 0 } );
    testCachedCodingFromHistogram( Histogram{ { /* code length 1 */ 2, } }, { char( 0b0110'0101 ) }, { 1, 0 } );
    testCachedCodingFromHistogram( Histogram{ { 1, 2 } }, { char( 0b0110'0101 ) }, { 1, 1, 0, 2, 0 } );
    /* Precode code lengths:        0    18  17  16 */
    testCachedCodingFromPrecodes( 0b010'010'000'001, { char( 0b0110'0101 ) }, { 0, 0, 16, 18, 16 } );
}


template<uint16_t CHUNK_COUNT>
void
printCompressedPrecodeFrequenciesWTCSLUTSizes()
{
    using namespace rapidgzip::PrecodeCheck;

    std::cerr << "  " << ( CHUNK_COUNT * 64U ) << " bits (" << CHUNK_COUNT << " B) per subtable):\n";

    const auto& [histogramLUT, validLUT] =
        WalkTreeCompressedSingleLUT::PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<CHUNK_COUNT>;
    const auto histogramLUTSize = histogramLUT.size() * sizeof( histogramLUT[0] );
    const auto validLUTSize = validLUT.size() * sizeof( validLUT[0] );

    std::cerr << "    indexes          : " << histogramLUTSize << "\n";
    std::cerr << "    subtables size   : " << validLUTSize << "\n";
    std::cerr << "    subtables number : "
              << validLUTSize / ( CHUNK_COUNT * sizeof( uint64_t ) /* 64-bit chunks bit-packed */ ) << "\n";
    std::cerr << "    sum              : " << histogramLUTSize + validLUTSize << "\n";
}


template<uint16_t CHUNK_COUNT>
void
printCompressedPrecodeFrequenciesSingleLUTSizes()
{
    using namespace rapidgzip::PrecodeCheck;

    std::cerr << "  " << ( CHUNK_COUNT * 64U ) << " bits (" << CHUNK_COUNT << " B) per subtable):\n";

    const auto& [histogramLUT, validLUT] =
        SingleCompressedLUT::COMPRESSED_PRECODE_HISTOGRAM_VALID_LUT_DICT<CHUNK_COUNT>;
    const auto histogramLUTSize = histogramLUT.size() * sizeof( histogramLUT[0] );
    const auto validLUTSize = validLUT.size() * sizeof( validLUT[0] );

    std::cerr << "    indexes          : " << histogramLUTSize << "\n";
    std::cerr << "    subtables size   : " << validLUTSize << "\n";
    std::cerr << "    subtables number : " << validLUTSize / ( CHUNK_COUNT * 64U /* 64-bit chunks */ ) << "\n";
    std::cerr << "    sum              : " << histogramLUTSize + validLUTSize << "\n";
}


template<uint8_t  PRECODE_FREQUENCIES_LUT_COUNT,
         uint16_t CHUNK_COUNT>
void
printCompressedPrecodeFrequenciesLUTSizes()
{
    using namespace rapidgzip::PrecodeCheck;

    const auto MAX_CL = static_cast<size_t>( PRECODE_FREQUENCIES_LUT_COUNT );

    std::cerr << "  CL 1-" << MAX_CL << ", " << ( CHUNK_COUNT * 64U ) << " bits (" << CHUNK_COUNT
              << " B) per subtable):\n";

    const auto& [histogramLUT, validLUT] =
        WalkTreeCompressedLUT::PRECODE_FREQUENCIES_VALID_LUT_TWO_STAGES<PRECODE_FREQUENCIES_LUT_COUNT, CHUNK_COUNT>;
    const auto histogramLUTSize = histogramLUT.size() * sizeof( histogramLUT[0] );
    const auto validLUTSize = validLUT.size() * sizeof( validLUT[0] );

    std::cerr << "    indexes          : " << histogramLUTSize << "\n";
    std::cerr << "    subtables size   : " << validLUTSize << "\n";
    std::cerr << "    subtables number : " << validLUTSize / ( CHUNK_COUNT * 64U /* 64-bit chunks */ ) << "\n";
    std::cerr << "    sum              : " << histogramLUTSize + validLUTSize << "\n";
}


void
analyzeActualLUTCompression()
{
    std::cerr << "PRECODE_FREQUENCIES_1_TO_5_VALID_LUT      : "
              << sizeof( PrecodeCheck::WalkTreeLUT::PRECODE_FREQUENCIES_1_TO_5_VALID_LUT ) << "\n";  // 4 MiB
    std::cerr << "PRECODE_TO_FREQUENCIES_LUT<4>             : "
              << sizeof( PrecodeCheck::WalkTreeLUT::PRECODE_TO_FREQUENCIES_LUT<4> ) << "\n\n";  // 32 KiB

    std::cerr << "Precode code-length histogram LUT two-stage compressed:\n";
    printCompressedPrecodeFrequenciesLUTSizes<4U, 1U>();     // 16384 B +  640 B -> 17024 B
    printCompressedPrecodeFrequenciesLUTSizes<4U, 4U>();     //  4096 B + 2304 B ->  6400 B
    printCompressedPrecodeFrequenciesLUTSizes<4U, 8U>();     //  2048 B + 4608 B ->  6656 B
    printCompressedPrecodeFrequenciesLUTSizes<4U, 16U>();    //  1024 B + 9216 B -> 10240 B
    std::cerr << "\n";
    printCompressedPrecodeFrequenciesLUTSizes<5U, 8U>();     // 65536 B +  4096 B -> 69632 B
    printCompressedPrecodeFrequenciesLUTSizes<5U, 16U>();    // 32768 B +  8192 B -> 40960 B  (size-optimal)
    printCompressedPrecodeFrequenciesLUTSizes<5U, 32U>();    // 16384 B + 40960 B -> 57344 B
    std::cerr << "\n";
    printCompressedPrecodeFrequenciesLUTSizes<6U, 32U>();    // 524288 B +  26624 B -> 550912 B
    printCompressedPrecodeFrequenciesLUTSizes<6U, 64U>();    // 262144 B +  77824 B -> 339968 B  (size-optimal)
    printCompressedPrecodeFrequenciesLUTSizes<6U, 128U>();   // 131072 B + 204800 B -> 335872 B  (size-optimal)
    printCompressedPrecodeFrequenciesLUTSizes<6U, 256U>();   //  65536 B + 409600 B -> 475136 B
    std::cerr << "\n";
    /* Maximum precode code length is 7! Meaning no manual check necessary for this! */
    printCompressedPrecodeFrequenciesLUTSizes<7U, 128U>();   // 4   MiB +  212992 B -> 4407296 B
    printCompressedPrecodeFrequenciesLUTSizes<7U, 256U>();   // 2   MiB +  425984 B -> 2523136 B
    printCompressedPrecodeFrequenciesLUTSizes<7U, 512U>();   // 1   MiB +  851968 B -> 1900544 B  (size-optimal)
    printCompressedPrecodeFrequenciesLUTSizes<7U, 1024U>();  // 512 KiB + 3014656 B -> 3538944 B

    std::cerr << "\nFull precode code-length histogram Walk-Tree Single-LUT two-stage compressed plus bit-packed:\n";
    printCompressedPrecodeFrequenciesWTCSLUTSizes<32U>();    // 512 KiB + 16.5 kiB ( 66 subtables) -> 528.5 KiB
    printCompressedPrecodeFrequenciesWTCSLUTSizes<64U>();    // 256 KiB +   37 kiB ( 74 subtables) ->  293 KiB (opt.)
    printCompressedPrecodeFrequenciesWTCSLUTSizes<128U>();   // 128 KiB +   74 kiB ( 74 subtables) ->  292 KiB (opt.)
    printCompressedPrecodeFrequenciesWTCSLUTSizes<256U>();   //  64 KiB +  250 kiB (125 subtables) ->  314 KiB
    printCompressedPrecodeFrequenciesWTCSLUTSizes<512U>();   //  32 KiB +  476 kiB (119 subtables) ->  508 KiB
    printCompressedPrecodeFrequenciesWTCSLUTSizes<1024U>();  //  16 KiB +  760 kiB ( 95 subtables) ->  776 KiB
    printCompressedPrecodeFrequenciesWTCSLUTSizes<2048U>();  //   8 KiB + 1264 kiB ( 79 subtables) -> 1272 KiB

    std::cerr << "\nFull precode code-length histogram Single-LUT two-stage compressed:\n";
    printCompressedPrecodeFrequenciesSingleLUTSizes<1U>();   // 256 KiB +   1600 B ( 25 subtables) -> 263744 B
    printCompressedPrecodeFrequenciesSingleLUTSizes<4U>();   //  64 KiB +  14592 B (456 subtables) ->  80128 B
    printCompressedPrecodeFrequenciesSingleLUTSizes<8U>();   //  32 KiB +  32768 B (512 subtables) ->  65536 B
    printCompressedPrecodeFrequenciesSingleLUTSizes<16U>();  //  16 KiB +  60416 B (472 subtables) ->  76800 B
    printCompressedPrecodeFrequenciesSingleLUTSizes<32U>();  //   8 KiB + 202752 B (792 subtables) -> 210944 B
    printCompressedPrecodeFrequenciesSingleLUTSizes<64U>();  //   4 KiB + 327680 B (640 subtables) -> 331776 B
    printCompressedPrecodeFrequenciesSingleLUTSizes<128U>(); //   2 KiB + 458752 B (448 subtables) -> 460800 B
}


int
main()
{
    analyzePrecodeHistogramLinearSearchTables();  /** @todo fix filter rates */

    testCachedHuffmanCodings();

    testValidHistograms();

    analyzeCompressedLUT<4>();
    analyzeCompressedLUT<5>();
    analyzeCompressedLUT<6>();
    analyzeCompressedLUT<7>();
    analyzeCompressedLUT<8>();
    analyzeCompressedLUT<9>();
    analyzeCompressedLUT<10>();
    analyzeCompressedLUT<11>();
    analyzeCompressedLUT<12>();

    /** @see results/deflate/valid-precode-histograms.txt */
    //printValidHistograms();
    printValidHistogramsByPrecodeCount();

    testSingleLUTImplementation();

    analyzeMaxValidPrecodeFrequencies</* COMPARE_WITH_ALTERNATIVE_METHOD (quite slow and changes rarely) */ false>();
    analyzeAndTestValidPrecodes();

    analyzeValidPrecodeFrequencies<2>();
    analyzeValidPrecodeFrequencies<3>();
    analyzeValidPrecodeFrequencies<4>();
    analyzeValidPrecodeFrequencies<5>();
    //analyzeValidPrecodeFrequencies<6>();  // Creates 128 MiB LUT and 137 MiB binary!
    //analyzeValidPrecodeFrequencies<7>();  // Does not compile / link. I think the binary becomes too large

    std::cerr << "\n\n== Complete LUT for variable length packed precode histograms ==\n\n";
    analyzeSingleLUTCompression( rapidgzip::PrecodeCheck::SingleLUT::PRECODE_HISTOGRAM_VALID_LUT );

    std::cerr << "\n== LUT for fixed 5-bit length precode histograms for counts 1 to 5 ==\n\n";
    analyzeSingleLUTCompression( rapidgzip::PrecodeCheck::WalkTreeLUT::PRECODE_FREQUENCIES_1_TO_5_VALID_LUT );

    analyzeActualLUTCompression();

    std::cout << "\nTests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}


/*
cmake --build . -- testPrecodeCheck && taskset 0x08 src/tests/rapidgzip/testPrecodeCheck

Size of valid precomputed precode huffman codings: 476 KiB 896 B

Subtable size in number of bits: 4
    LUT size: 2 MiB
    Unique Subtables: 1096
    Subtable size: 32 B
    Subtables size: 34 KiB 256 B
    Total size: 2 MiB 34 KiB 256 B

Subtable size in number of bits: 5
    LUT size: 1 MiB
    Unique Subtables: 678
    Subtable size: 64 B
    Subtables size: 42 KiB 384 B
    Total size: 1 MiB 42 KiB 384 B

Subtable size in number of bits: 6
    LUT size: 512 KiB
    Unique Subtables: 465
    Subtable size: 128 B
    Subtables size: 58 KiB 128 B
    Total size: 570 KiB 128 B

Subtable size in number of bits: 7
    LUT size: 256 KiB
    Unique Subtables: 465
    Subtable size: 256 B
    Subtables size: 116 KiB 256 B
    Total size: 372 KiB 256 B

Subtable size in number of bits: 8
    LUT size: 128 KiB
    Unique Subtables: 276
    Subtable size: 512 B
    Subtables size: 138 KiB
    Total size: 266 KiB

Subtable size in number of bits: 9
    LUT size: 32 KiB
    Unique Subtables: 184
    Subtable size: 1 KiB
    Subtables size: 184 KiB
    Total size: 216 KiB

Subtable size in number of bits: 10
    LUT size: 16 KiB
    Unique Subtables: 150
    Subtable size: 2 KiB
    Subtables size: 300 KiB
    Total size: 316 KiB

Subtable size in number of bits: 11
    LUT size: 8 KiB
    Unique Subtables: 150
    Subtable size: 4 KiB
    Subtables size: 600 KiB
    Total size: 608 KiB

Subtable size in number of bits: 12
    LUT size: 4 KiB
    Unique Subtables: 88
    Subtable size: 8 KiB
    Subtables size: 704 KiB
    Total size: 708 KiB

Valid Histogram with >=16 codes of the same length: 1:0 2:0 3:0 4:16 5:0 6:0 7:0 (0x0000000000080000)
Valid Histogram with >=16 codes of the same length: 1:0 2:1 3:2 4:0 5:16 6:0 7:0 (0x0000000001000820)
Valid Histogram with >=16 codes of the same length: 1:0 2:2 3:0 4:0 5:16 6:0 7:0 (0x0000000001000040)
Valid Histogram with >=16 codes of the same length: 1:0 2:3 3:0 4:0 5:0 6:16 7:0 (0x0000000020000060)
Valid Histogram with >=16 codes of the same length: 1:1 2:0 3:0 4:0 5:16 6:0 7:0 (0x0000000001000001)
Valid Histogram with >=16 codes of the same length: 1:1 2:0 3:2 4:0 5:0 6:16 7:0 (0x0000000020000801)
Valid Histogram with >=16 codes of the same length: 1:1 2:1 3:0 4:0 5:0 6:16 7:0 (0x0000000020000021)
Valid Histogram with >=16 codes of the same length: 1:1 2:1 3:1 4:0 5:0 6:0 7:16 (0x0000000400000421)

Maximum length frequencies of valid histograms:
    Code Length 1 : 2
    Code Length 2 : 4
    Code Length 3 : 8
    Code Length 4 : 16
    Code Length 5 : 16
    Code Length 6 : 16
    Code Length 7 : 16

Found in total 1526 valid histograms (corresponding to the maximum of 7 bins) equaling 11 KiB 944 B
Valid precodes 408185 out of 100000000 tested -> 0.408185 %
Encountered errors:
    90010469 Constructing a Huffman coding from the given code length sequence failed!
     9581346 The Huffman coding is not optimal!
      408185 No error.

Precode frequency LUT containing 2 bins is sized: 128 B. There are 9 valid entries out of 1024 -> 0.878906 %
Precode frequency LUT containing 3 bins is sized: 4 KiB. There are 35 valid entries out of 32768 -> 0.106812 %
Precode frequency LUT containing 4 bins is sized: 128 KiB. There are 157 valid entries out of 1048576 -> 0.0149727 %
Precode frequency LUT containing 5 bins is sized: 4 MiB. There are 561 valid entries out of 33554432 -> 0.00167191 %
Precode frequency LUT containing 6 bins is sized: 128 MiB. There are 1526 valid entries out of 1073741824 -> 0.000142212 %
Precode frequency LUT containing 7 bins is sized: 4 GiB. There are 1526 valid entries out of 34359738368 -> 0.000004441 %


== Complete LUT for variable length packed precode histograms ==

Unique precode histogram lookup 64-bit compressed results: 0 1 2 4 8 18 256 512 1024 4608 65540 131144 262162 1179720 16778240 33572864 67113472 302008320 4295229458 17181048904 1099578741248 4398348519424 281492157759560 1125977220972578 72061992386447360
The histogram LUT is sized: 2 MiB
By adding another layer of indirection to compress duplicate values in a dictionary (LUT), we can
save further bytes. Calculations are done for different value sizes in chunks of one 64-bit value
up to multiple 64-bit values mapped to a single dictionary (LUT) entry:

    64-bit chunks: 1, unique values: 25, address size: 1 B
        LUT: 256 KiB
            value LUT (bits) : 200 B -> SUM: 256 KiB 200 B
            value LUT (bytes): 1 KiB 576 B -> SUM: 257 KiB 576 B
    64-bit chunks: 2, unique values: 45, address size: 1 B
        LUT: 128 KiB
            value LUT (bits) : 720 B -> SUM: 128 KiB 720 B
            value LUT (bytes): 5 KiB 640 B -> SUM: 133 KiB 640 B
    64-bit chunks: 4, unique values: 57, address size: 1 B
        LUT:  64 KiB
            value LUT (bits) : 1 KiB 800 B -> SUM: 65 KiB 800 B
            value LUT (bytes): 14 KiB 256 B -> SUM: 78 KiB 256 B
    64-bit chunks: 8, unique values: 64, address size: 2 B
        LUT:  64 KiB
            value LUT (bits) : 4 KiB -> SUM: 68 KiB
            value LUT (bytes): 32 KiB -> SUM: 96 KiB
    64-bit chunks: 16, unique values: 59, address size: 2 B
        LUT:  32 KiB
            value LUT (bits) : 7 KiB 384 B -> SUM: 39 KiB 384 B
            value LUT (bytes): 59 KiB -> SUM: 91 KiB
    64-bit chunks: 32, unique values: 99, address size: 2 B
        LUT:  16 KiB
            value LUT (bits) : 24 KiB 768 B -> SUM: 40 KiB 768 B
            value LUT (bytes): 198 KiB -> SUM: 214 KiB


== LUT for fixed 5-bit length precode histograms for counts 1 to 5 ==

Unique precode histogram lookup 64-bit compressed results: 0 1 2 4294967296 4294967298 8589934592 8589934594 8589934598
The histogram LUT is sized: 4 MiB
By adding another layer of indirection to compress duplicate values in a dictionary (LUT), we can
save further bytes. Calculations are done for different value sizes in chunks of one 64-bit value
up to multiple 64-bit values mapped to a single dictionary (LUT) entry:

    64-bit chunks: 1, unique values: 8, address size: 1 B
        LUT: 512 KiB
            value LUT (bits) : 64 B -> SUM: 512 KiB 64 B
            value LUT (bytes): 512 B -> SUM: 512 KiB 512 B
    64-bit chunks: 2, unique values: 8, address size: 1 B
        LUT: 256 KiB
            value LUT (bits) : 128 B -> SUM: 256 KiB 128 B
            value LUT (bytes): 1 KiB -> SUM: 257 KiB
    64-bit chunks: 4, unique values: 8, address size: 1 B
        LUT: 128 KiB
            value LUT (bits) : 256 B -> SUM: 128 KiB 256 B
            value LUT (bytes): 2 KiB -> SUM: 130 KiB
    64-bit chunks: 8, unique values: 8, address size: 1 B
        LUT:  64 KiB
            value LUT (bits) : 512 B -> SUM: 64 KiB 512 B
            value LUT (bytes): 4 KiB -> SUM: 68 KiB
    64-bit chunks: 16, unique values: 8, address size: 1 B
        LUT:  32 KiB
            value LUT (bits) : 1 KiB -> SUM: 33 KiB
            value LUT (bytes): 8 KiB -> SUM: 40 KiB
    64-bit chunks: 32, unique values: 20, address size: 2 B
        LUT:  32 KiB
            value LUT (bits) : 5 KiB -> SUM: 37 KiB
            value LUT (bytes): 40 KiB -> SUM: 72 KiB


Tests successful: 500000140 / 500000140
*/
