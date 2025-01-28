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

#include <blockfinder/precodecheck/BruteForceLUT.hpp>
#include <blockfinder/precodecheck/SingleCompressedLUT.hpp>
#include <blockfinder/precodecheck/SingleLUT.hpp>
#include <blockfinder/precodecheck/WalkTreeCompressedLUT.hpp>
#include <blockfinder/precodecheck/WalkTreeLUT.hpp>
#include <blockfinder/precodecheck/WithoutLUT.hpp>
#include <filereader/Buffered.hpp>
#include <precode.hpp>
#include <TestHelpers.hpp>
#include <Error.hpp>


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

    const auto getHistogram =
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

    /* Too many of the same value overflows the variable-length bit-packed histogram,
     * which should be detected and yield an error. */
    REQUIRE( check4Precodes( 0b001'010'001'001 ) != rapidgzip::Error::NONE );
}


void
testSingleLUTImplementation8Precodes()
{
    /* Starting with these tests there is more than one valid tree configuration and addition of partial histograms
     * comes into play and can be tested. */

    const auto check8Precodes =
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
analyzeActualLUTCompression()
{
    const auto printRealCompressedLUTStatistics =
        [] ( const auto&        compressedLUT,
             const size_t       chunkCount,
             const std::string& label )
        {
            const auto& [validLUT, validBitMasks] = compressedLUT;
            std::cerr << "    " << label << ":\n"
                      << "        Chunks     : " << chunkCount << "\n"
                      << "        LUT        : " << formatBytes( validLUT.size() * sizeof( validLUT[0] ) ) << "\n"
                      << "        Dictionary : "
                      << formatBytes( validBitMasks.size() * sizeof( validBitMasks[0] ) ) << "\n"
                      << "        -> Sum : "
                      << formatBytes( validLUT.size() * sizeof( validLUT[0] )
                                      + validBitMasks.size() * sizeof( validBitMasks[0] ) ) << "\n\n";
        };

    std::cerr << "\n== Sizes for actual implementations ==\n\n";
    using namespace rapidgzip::PrecodeCheck;
    printRealCompressedLUTStatistics( SingleCompressedLUT::COMPRESSED_PRECODE_HISTOGRAM_VALID_LUT_DICT,
                                      SingleCompressedLUT::COMPRESSED_PRECODE_HISTOGRAM_CHUNK_COUNT,
                                      "Whole LUT for variable-length bit-packed histogram" );
    printRealCompressedLUTStatistics( WalkTreeCompressedLUT::COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_VALID_LUT_DICT,
                                      WalkTreeCompressedLUT::COMPRESSED_PRECODE_FREQUENCIES_1_TO_5_CHUNK_COUNT,
                                      "LUT for frequencies 1 to 5 for uniformly bit-packed histogram" );
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


void
analyzeValidPrecodes()
{
    std::mt19937_64 randomEngine;

    /* Because we can not exhaustively search all 2^61 possible configurations, use Monte-Carlo sampling.
     * Actually, the search space is a bit smaller because the 57 bits are the maximum and the actual length
     * depends on the 4 bits. */
    static constexpr uint64_t MONTE_CARLO_TEST_COUNT = 100'000'000;
    uint64_t validPrecodeCount{ 0 };
    std::unordered_map<rapidgzip::Error, uint64_t> errorCounts;
    for ( uint64_t i = 0; i < MONTE_CARLO_TEST_COUNT; ++i ) {
        using namespace rapidgzip::deflate;
        const auto precodeBits = randomEngine();
        const auto next4Bits = precodeBits & nLowestBitsSet<uint64_t, 4>();
        const auto next57Bits = ( precodeBits >> 4U ) & nLowestBitsSet<uint64_t>( MAX_PRECODE_COUNT * PRECODE_BITS );

        const auto error = rapidgzip::PrecodeCheck::WalkTreeLUT::checkPrecode( next4Bits, next57Bits );

        const auto [count, wasInserted] = errorCounts.try_emplace( error, 1 );
        if ( !wasInserted ) {
            count->second++;
        }

        const auto isValid = error == rapidgzip::Error::NONE;
        validPrecodeCount += isValid ? 1 : 0;

        /* Compare with alternative checkPrecode functions. */
        const auto checkAlternative =
            [&] ( const auto& checkPrecode )
            {
                const auto alternativeIsValid = checkPrecode( next4Bits, next57Bits ) == rapidgzip::Error::NONE;
                REQUIRE_EQUAL( isValid, alternativeIsValid );
                if ( isValid != alternativeIsValid ) {
                    const auto codeLengthCount = 4 + next4Bits;
                    std::cerr << "    next 4 bits: " << std::bitset<4>( next4Bits ) << ", next 57 bits: "
                              << ( next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS ) ) << "\n";
                }
            };

        checkAlternative( rapidgzip::PrecodeCheck::WithoutLUT::checkPrecodeUsingArray );
        checkAlternative( rapidgzip::PrecodeCheck::WithoutLUT::checkPrecode );
        checkAlternative( rapidgzip::PrecodeCheck::SingleLUT::checkPrecode );
        checkAlternative( rapidgzip::PrecodeCheck::SingleCompressedLUT::checkPrecode );
        checkAlternative( rapidgzip::PrecodeCheck::WalkTreeCompressedLUT::checkPrecode );
    }

    {
        std::cerr << "Valid precodes " << validPrecodeCount << " out of " << MONTE_CARLO_TEST_COUNT << " tested -> "
                  << static_cast<double>( validPrecodeCount ) / static_cast<double>( MONTE_CARLO_TEST_COUNT ) * 100
                  << " %\n";

        std::multimap<uint64_t, rapidgzip::Error, std::greater<> > sortedErrorTypes;
        for ( const auto [error, count] : errorCounts ) {
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


/**
 * @param depth A depth of 1 means that we should iterate over 1-bit codes, which can only be 0,1,2.
 * @param freeBits This can be calculated from the histogram but it saves constexpr instructions when
 *        the caller updates this value outside.
 * @note This is an adaptation of @ref createPrecodeFrequenciesValidLUTHelper.
 */
template<uint32_t FREQUENCY_BITS,
         uint32_t FREQUENCY_COUNT,
         uint32_t DEPTH = 1,
         typename LUT = std::array<uint64_t, ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) / 64U> >
void
analyzeMaxValidPrecodeFrequenciesHelper( std::function<void( uint64_t )> processValidHistogram,
                                         uint32_t const                  remainingCount,
                                         uint64_t const                  histogram = 0,
                                         uint32_t const                  freeBits = 2 )
{
    static_assert( DEPTH <= FREQUENCY_COUNT, "Cannot descend deeper than the frequency counts!" );
    if ( ( histogram & nLowestBitsSet<uint64_t, ( DEPTH - 1 ) * FREQUENCY_BITS>() ) != histogram ) {
        throw std::invalid_argument( "Only frequency of bit-lengths less than the depth may be set!" );
    }

    const auto histogramWithCount =
        [histogram] ( auto count ) constexpr {
            return histogram | ( static_cast<uint64_t>( count ) << ( ( DEPTH - 1 ) * FREQUENCY_BITS ) );
        };

    /* The for loop maximum is given by the invalid Huffman code check, i.e.,
     * when there are more code lengths on a tree level than there are nodes. */
    for ( uint32_t count = 0; count <= std::min( remainingCount, freeBits ); ++count ) {
        const auto newFreeBits = ( freeBits - count ) * 2;
        [[maybe_unused]] const auto newRemainingCount = remainingCount - count;

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
                analyzeMaxValidPrecodeFrequenciesHelper<FREQUENCY_BITS, FREQUENCY_COUNT, DEPTH + 1>(
                    processValidHistogram, newRemainingCount, histogramWithCount( count ), newFreeBits );
            }
        }
    }
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


template<bool COMPARE_WITH_ALTERNATIVE_METHOD>
void
analyzeMaxValidPrecodeFrequencies()
{
    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    static constexpr uint32_t FREQUENCY_BITS = 5;  /* minimum bits to represent up to count 19. */
    static constexpr uint32_t FREQUENCY_COUNT = 7;  /* maximum value with 3-bits */

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

    analyzeMaxValidPrecodeFrequenciesHelper<FREQUENCY_BITS, FREQUENCY_COUNT>(
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
        if ( rapidgzip::PrecodeCheck::BruteForceLUT::checkPrecodeFrequencies<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram )
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
    using rapidgzip::deflate::precode::VALID_HISTOGRAMS;
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


int
main()
{
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

    testSingleLUTImplementation();

    analyzeMaxValidPrecodeFrequencies</* COMPARE_WITH_ALTERNATIVE_METHOD (quite slow and changes rarely) */ false>();
    analyzeValidPrecodes();

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
cmake --build . -- benchmarkGzipBlockFinder && taskset 0x08 src/benchmarks/benchmarkGzipBlockFinder random.gz

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

== Sizes for actual implementations ==

    Whole LUT for variable-length bit-packed histogram:
        Chunks     : 4
        LUT        : 64 KiB
        Dictionary : 14 KiB 256 B
        -> Sum : 78 KiB 256 B

    LUT for frequencies 1 to 5 for uniformly bit-packed histogram:
        Chunks     : 16
        LUT        : 32 KiB
        Dictionary : 8 KiB
        -> Sum : 40 KiB


Tests successful: 10 / 10
*/
