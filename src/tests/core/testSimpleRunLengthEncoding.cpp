#include <array>
#include <cstdint>
#include <limits>
#include <ranges>
#include <vector>

#include <core/common.hpp>
#include <core/SimpleRunLengthEncoding.hpp>
#include <core/TestHelpers.hpp>


using namespace rapidgzip;
using namespace rapidgzip::SimpleRunLengthEncoding;


void
testVarInt( uint64_t                    value,
            const std::vector<uint8_t>& expected = {} )
{
    for ( const auto offset : { 0, 3 } ) {
        std::vector<uint8_t> serialized( offset );
        writeVarInt( serialized, value );

        if ( !expected.empty() && ( offset == 0 ) ) {
            REQUIRE_EQUAL( serialized, expected );
        }

        /* Test the normal case. */
        {
            const auto [restored, nBytesRead] = readVarInt( serialized, offset );
            REQUIRE_EQUAL( restored, value );
            REQUIRE_EQUAL( static_cast<uint64_t>( nBytesRead ), serialized.size() - offset );
        }

        /* Test the error case, i.e., with the last byte missing.
         * Note that VarInt is completely agnostic to removing any of the first bytes!
         * It will simply decode a wrong number. */
        {
            serialized.resize( serialized.size() - 1 );
            const auto [restored, nBytesRead] = readVarInt( serialized, offset );
            REQUIRE_EQUAL( static_cast<uint64_t>( nBytesRead ), 0ULL );
        }
    }
}


void
testVarInt()
{
    testVarInt( 0, { 0 } );
    testVarInt( 1, { 1 } );
    testVarInt( 2, { 2 } );
    testVarInt( 127, { 127 } );
    testVarInt( 128, { 0b1000'0000, 0x01 } );
    testVarInt( 129, { 0b1000'0001, 0x01 } );
    /* 7 value bits per serialized byte! Input has 31 bits = 4 * 7 + 3 */
    testVarInt( 0x7F'FF'FF'FFULL, { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U } );
    testVarInt( std::numeric_limits<uint64_t>::max() );
}


void
testSimpleRunLengthEncoding( const std::vector<uint8_t> toCompress,
                             const std::vector<uint8_t> compressed )
{
    const auto encoded = simpleRunLengthEncode( VectorView<uint8_t>( toCompress ) );
    REQUIRE_EQUAL( encoded, compressed );

    const auto decoded = simpleRunLengthDecode<std::vector<uint8_t> >( encoded, toCompress.size() );
    REQUIRE_EQUAL( decoded.size(), toCompress.size() );
    REQUIRE_EQUAL( decoded, toCompress );
}


void
testSimpleRunLengthEncoding()
{
    constexpr auto A_COUNT = 7U;
    const std::vector<uint8_t> repeatedAs( A_COUNT, 'A' );
    const std::vector<uint8_t> encodedAs = { 0, 1, 'A', 1, 6 };
    testSimpleRunLengthEncoding( repeatedAs, encodedAs );
    testSimpleRunLengthEncoding( { 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!' },
                                 { 0, 12, 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!' } );
    testSimpleRunLengthEncoding( { 0, 9, 9, 9, 9, 9, 9, 9, 9, 9 }, { 0, 2, 0, 9, 1, 8 } );
}


constexpr void
testSimpleRunLengthEncodindConstexpr()
{
    constexpr auto A_COUNT = 7U;
    constexpr std::array<uint8_t, A_COUNT> REPEATED_AS{ 'A', 'A', 'A', 'A', 'A', 'A', 'A' };
    constexpr std::array<uint8_t, 5U> ENCODED_AS{ 0, 1, 'A', 1, 6 };

    constexpr auto decoded = simpleRunLengthDecode<std::array<uint8_t, A_COUNT> >( ENCODED_AS, A_COUNT );
    static_assert( std::ranges::equal( decoded, REPEATED_AS ) );
}


constexpr void
testSimpleRunLengthEncodindConstexpr2()
{
    constexpr auto A_COUNT = 7U;
    constexpr std::array<uint8_t, 2 * A_COUNT> REPEATED_AS{ 'A', 'A', 'A', 'A', 'A', 'A', 'A',
                                                            'B', 'B', 'B', 'B', 'B', 'B', 'B' };
    constexpr std::array<uint8_t, 10U> ENCODED_AS{ 0, 1, 'A', 1, 6, 0, 1, 'B', 1, 6 };

    constexpr auto decoded = simpleRunLengthDecode<std::array<uint8_t, 2 * A_COUNT> >( ENCODED_AS, 2 * A_COUNT );
    static_assert( std::ranges::equal( decoded, REPEATED_AS ) );
}


int
main()
{
    testVarInt();
    testSimpleRunLengthEncoding();
    testSimpleRunLengthEncodindConstexpr();
    testSimpleRunLengthEncodindConstexpr2();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
