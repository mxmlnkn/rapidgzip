#include <cstdint>
#include <iostream>
#include <string_view>

#include <crc32.hpp>
#include <TestHelpers.hpp>


using namespace std::string_view_literals;

using namespace pragzip;


[[nodiscard]] uint32_t
crc32( std::string_view data )
{
    return ~updateCRC32<>( ~uint32_t( 0 ), data.data(), data.size() );
}


void
testCRC32()
{
    /* Ground truths have been generated with the crc32 command line tool:
     * > The Archive::Zip module was written by Ned Konz. */
    REQUIRE_EQUAL( crc32( "" ), 0x0000'0000U );
    REQUIRE_EQUAL( crc32( "1" ), 0x83DC'EFB7U );
    REQUIRE_EQUAL( crc32( "12" ), 0x4F53'44CDU );
    REQUIRE_EQUAL( crc32( "1234" ), 0x9BE3'E0A3U );
    REQUIRE_EQUAL( crc32( "12345678" ), 0x9AE0'DAAFU );
    REQUIRE_EQUAL( crc32( "1234567890ABCDEF" ), 0xAC5B'E0BFU );
    REQUIRE_EQUAL( crc32( "1234567890ABCDEFHIJKLMNOPQRSTUVW" ), 0x7AD4'02D9U );
    REQUIRE_EQUAL( crc32( "1234567890ABCDEFHIJKLMNOPQRSTUVWX" ), 0x48C7'8839U );
    REQUIRE_EQUAL( crc32( "1234567890ABCDEFHIJKLMNOPQRSTUVWXY" ), 0x9FF8'495DU );
    REQUIRE_EQUAL( crc32( "1234567890ABCDEFHIJKLMNOPQRSTUVWXYZ" ), 0x4CF9'8267U );

    /* Add ground truths for the appended substrings when comparing to the previous test. */
    REQUIRE_EQUAL( crc32( "Z" ), 0x59BC'5767U );
    REQUIRE_EQUAL( crc32( "Y" ), 0xC0B5'06DDU );
    REQUIRE_EQUAL( crc32( "X" ), 0xB7B2'364BU );
    REQUIRE_EQUAL( crc32( "HIJKLMNOPQRSTUVW" ), 0xB4FF'5FC5U );
    REQUIRE_EQUAL( crc32( "90ABCDEF" ), 0x5D77'6DA7U );
    REQUIRE_EQUAL( crc32( "5678" ), 0x7E52'5607U );
    REQUIRE_EQUAL( crc32( "34" ), 0x9406'837AU );
    REQUIRE_EQUAL( crc32( "2" ), 0x1AD5'BE0DU );

    REQUIRE_EQUAL( crc32( "\0"sv ), 0xD202'EF8DU );
    REQUIRE_EQUAL( crc32( "\0\0"sv ), 0x41D9'12FFU );
}


void
testCRC32Combine()
{
    const auto combineCRC32 =
        [] ( std::string_view a, std::string_view b ) {
            return pragzip::combineCRC32( crc32( a ), crc32( b ), b.size() );
        };

    /** @see testCRC32 for the taken CRC values. */
    REQUIRE_EQUAL( combineCRC32( "", "1" ), 0x83DC'EFB7U );
    REQUIRE_EQUAL( combineCRC32( "1", "" ), 0x83DC'EFB7U );
    REQUIRE_EQUAL( combineCRC32( "1", "2" ), 0x4F53'44CDU );
    REQUIRE_EQUAL( combineCRC32( "1234", "567890ABCDEF" ), 0xAC5B'E0BFU );
    REQUIRE_EQUAL( combineCRC32( "123456789", "0ABCDEF" ), 0xAC5B'E0BFU );
}


void
testCRC32Calculator()
{
    CRC32Calculator crc32;
    REQUIRE_EQUAL( crc32.crc32(), 0x0000'0000U );

    crc32.update( "A", 1 );
    REQUIRE_EQUAL( crc32.crc32(), 0xD3D9'9E8BU );

    crc32.update( "", 0 );
    REQUIRE_EQUAL( crc32.crc32(), 0xD3D9'9E8BU );

    /* Combine with empty */

    crc32.prepend( CRC32Calculator() );
    REQUIRE_EQUAL( crc32.crc32(), 0xD3D9'9E8BU );

    crc32.append( CRC32Calculator() );
    REQUIRE_EQUAL( crc32.crc32(), 0xD3D9'9E8BU );

    /* Combine empty with non-empty */

    CRC32Calculator appended;
    appended.append( crc32 );
    REQUIRE_EQUAL( appended.crc32(), 0xD3D9'9E8BU );

    CRC32Calculator prepended;
    prepended.prepend( crc32 );
    REQUIRE_EQUAL( prepended.crc32(), 0xD3D9'9E8BU );

    const auto initCalculator =
        [] ( std::string_view data ) {
            CRC32Calculator result;
            result.update( data.data(), data.size() );
            REQUIRE_EQUAL( result.streamSize(), data.size() );
            return result;
        };

    /* Prepend two times */
    CRC32Calculator chainedPrepend;
    chainedPrepend.prepend( initCalculator( "" ) );
    REQUIRE_EQUAL( chainedPrepend.crc32(), 0x0000'0000U );

    chainedPrepend.prepend( initCalculator( "2" ) );
    REQUIRE_EQUAL( chainedPrepend.crc32(), 0x1AD5'BE0DU );
    REQUIRE_EQUAL( chainedPrepend.streamSize(), uint64_t( 1 ) );

    chainedPrepend.prepend( initCalculator( "1" ) );
    REQUIRE_EQUAL( chainedPrepend.crc32(), 0x4F53'44CDU );

    /* Append two times */
    CRC32Calculator chainedAppend;
    chainedAppend.append( initCalculator( "" ) );
    REQUIRE_EQUAL( chainedAppend.crc32(), 0x0000'0000U );

    chainedAppend.append( initCalculator( "1" ) );
    REQUIRE_EQUAL( chainedAppend.crc32(), 0x83DC'EFB7U );
    REQUIRE_EQUAL( chainedAppend.streamSize(), uint64_t( 1 ) );

    chainedAppend.append( initCalculator( "2" ) );
    REQUIRE_EQUAL( chainedAppend.crc32(), 0x4F53'44CDU );
}


int
main()
{
    testCRC32();
    testCRC32Combine();
    testCRC32Calculator();

    std::cout << "\nTests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}


/*
cmake --build . -- testCRC32 && taskset 0x08 src/pragzip/testCRC32 random.gz
*/
