#include <array>
#include <iostream>

#include <core/BitManipulation.hpp>
#include <core/TestHelpers.hpp>
#include <rapidgzip/blockfinder/precodecheck/CountAllocatedLeaves.hpp>
#include <rapidgzip/gzip/definitions.hpp>
#include <rapidgzip/gzip/precode.hpp>


using namespace rapidgzip;
using namespace PrecodeCheck::CountAllocatedLeaves;


[[nodiscard]] Error
checkPrecodeDirectly( size_t   next4Bits,
                      uint64_t precodeBits )
{
    const auto codeLengthCount = 4 + next4Bits;

    /* Get code lengths (CL) for alphabet P. */
    std::array<uint8_t, deflate::MAX_PRECODE_COUNT> precodeCL{};
    std::memset( precodeCL.data(), 0, precodeCL.size() * sizeof( precodeCL[0] ) );
    for ( size_t i = 0; i < codeLengthCount; ++i ) {
        precodeCL[deflate::PRECODE_ALPHABET[i]] = ( precodeBits >> ( i * 3U ) ) & 0b111U;
    }

    deflate::precode::PrecodeHuffmanCoding precodeHC;
    return precodeHC.initializeFromLengths( VectorView<uint8_t>( precodeCL.data(), precodeCL.size() ) );
}


void
test4Precodes()
{
    using namespace PrecodeCheck::CountAllocatedLeaves;

    const auto check4Precodes =
        [] ( const auto values ) { return checkPrecode( 4, values ); };

    REQUIRE( check4Precodes( 0b000'000'000'000 ) != Error::NONE );

    /* Only one non-zero value that is not 1 leads to a non-optimal tree. */
    REQUIRE( check4Precodes( 0b000'000'000'010 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'000'011 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'000'100 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'010'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'011'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'100'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'010'000'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'011'000'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'100'000'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b010'000'000'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b011'000'000'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b100'000'000'000 ) != Error::NONE );

    REQUIRE( check4Precodes( 0b000'000'001'000 ) == Error::NONE );

    REQUIRE_EQUAL( checkPrecodeDirectly( 0, 0b001'000'000'001 ), Error::NONE );
    REQUIRE_EQUAL( checkPrecode        ( 0, 0b001'000'000'001 ), Error::NONE );

    REQUIRE_EQUAL( checkPrecodeDirectly( 0, 0b010'000'010'001 ), Error::NONE );
    REQUIRE_EQUAL( checkPrecode        ( 0, 0b010'000'010'001 ), Error::NONE );

    REQUIRE_EQUAL( checkPrecodeDirectly( 0, 0b000'000'001'000 ), Error::NONE );
    REQUIRE_EQUAL( checkPrecode        ( 0, 0b000'000'001'000 ), Error::NONE );

    REQUIRE( checkPrecodeDirectly( 0, 0b000'000'010'000 ) == Error::BLOATING_HUFFMAN_CODING );
    REQUIRE( checkPrecode        ( 0, 0b000'000'010'000 ) != Error::NONE );

    /* A single code length with 1 bit is valid. */
    REQUIRE( check4Precodes( 0b000'000'000'001 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'001'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'000'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'000'000 ) == Error::NONE );

    /* Two non-zero values are only valid if both of them are of length 1. */
    REQUIRE( check4Precodes( 0b001'001'000'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'001'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'000'001 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'001'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'000'001 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'001'001 ) == Error::NONE );

    /* If there is a code length longer than one out of the two, then the tree will be non-optimal. */
    REQUIRE( check4Precodes( 0b001'011'000'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'011'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b001'000'000'011 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'011'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'000'011 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'000'011'001 ) != Error::NONE );

    /* Even with 3 values, there is still only one tree that is valid: code lengths: 1, 2, 2. */
    REQUIRE( check4Precodes( 0b001'010'010'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b001'010'000'010 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b001'010'000'010 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b010'001'010'000 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'010'010 ) == Error::NONE );
    REQUIRE( check4Precodes( 0b000'010'010'001 ) == Error::NONE );

    REQUIRE( check4Precodes( 0b001'010'011'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b011'010'000'010 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b001'110'000'010 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b010'001'011'000 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'001'010'110 ) != Error::NONE );
    REQUIRE( check4Precodes( 0b000'010'010'101 ) != Error::NONE );

    /* And even with 4 values, there is still only one tree that is valid: code lengths: 2, 2, 2, 2. */
    REQUIRE( check4Precodes( 0b010'010'010'010 ) == Error::NONE );

    REQUIRE( check4Precodes( 0b001'010'001'001 ) != Error::NONE );

    /* Because it 19 with code length 7 will overflow the histogram, do note expect a correct result.
     * But, more importantly, check that it does not segfault when accessing the LUT with such an overflow! */
    REQUIRE( check4Precodes( ~uint64_t( 0 ) ) != Error::NONE );
}


void
test8Precodes()
{
    const auto check8Precodes =
        [] ( const auto values ) { return PrecodeCheck::CountAllocatedLeaves::checkPrecode( 8, values ); };

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
    static_assert( check8Precodes( 0b000'000'000'100'100'011'010'001 ) == Error::NONE );
    static_assert( check8Precodes( 0b000'000'100'100'011'010'001'000 ) == Error::NONE );
    static_assert( check8Precodes( 0b000'100'100'011'010'001'000'000 ) == Error::NONE );
    static_assert( check8Precodes( 0b100'100'011'010'001'000'000'000 ) == Error::NONE );

    static_assert( check8Precodes( 0b000'000'000'001'011'011'011'011 ) == Error::NONE );
    static_assert( check8Precodes( 0b000'000'000'011'011'011'011'001 ) == Error::NONE );
    static_assert( check8Precodes( 0b000'000'011'011'011'011'001'000 ) == Error::NONE );
    static_assert( check8Precodes( 0b000'011'011'011'011'001'000'000 ) == Error::NONE );
    static_assert( check8Precodes( 0b011'011'011'011'001'000'000'000 ) == Error::NONE );
}


void
testValidHistograms()
{
    for ( const auto histogram : deflate::precode::VALID_HISTOGRAMS ) {
        uint64_t codeLengthCount{ 0 };
        uint64_t precode{ 0 };
        for ( size_t codeLength = 0; codeLength < histogram.size(); ++codeLength ) {
            for ( size_t i = 0; i < histogram[codeLength]; ++i ) {
                precode = ( precode << deflate::PRECODE_BITS ) | ( 1U + codeLength );
                ++codeLengthCount;
            }
        }

        /* We must not get false negatives! All valid precodes must be detected as such. */
        REQUIRE( checkPrecode( codeLengthCount, precode ) == Error::NONE );
    }
}


int
main()
{
    testValidHistograms();

    test4Precodes();
    test8Precodes();

    std::cout << "\nTests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";
    return gnTestErrors == 0 ? 0 : 1;
}
