#include <cstdint>
#include <iostream>
#include <vector>

#include <common.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


void
testIsBase64()
{
    std::vector<uint8_t> test;
    REQUIRE( isBase64<uint8_t>( { test.data(), test.size() } ) );

    test = { 'b' };
    REQUIRE( isBase64<uint8_t>( { test.data(), test.size() } ) );

    test = { '6' };
    REQUIRE( isBase64<uint8_t>( { test.data(), test.size() } ) );

    test = { '\n' };
    REQUIRE( isBase64<uint8_t>( { test.data(), test.size() } ) );

    test = { '/' };
    REQUIRE( isBase64<uint8_t>( { test.data(), test.size() } ) );

    test = { '\2' };
    REQUIRE( !isBase64<uint8_t>( { test.data(), test.size() } ) );
}


void
testUnsignedSaturatingAddition()
{
    REQUIRE_EQUAL( saturatingAddition( 0U, 0U ), 0U );
    REQUIRE_EQUAL( saturatingAddition( 0U, 1U ), 1U );
    REQUIRE_EQUAL( saturatingAddition( 1U, 0U ), 1U );
    REQUIRE_EQUAL( saturatingAddition( 1U, 1U ), 2U );

    constexpr auto MAX = std::numeric_limits<uint64_t>::max();
    REQUIRE_EQUAL( saturatingAddition( MAX, uint64_t( 0 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( uint64_t( 0 ), MAX ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX, uint64_t( 1 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( uint64_t( 1 ), MAX ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX - 1U, uint64_t( 1 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( uint64_t( 1 ), MAX - 1U ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX - 1U, uint64_t( 2 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( uint64_t( 2 ), MAX - 1U ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX - 3U, uint64_t( 2 ) ), MAX - 1U );
    REQUIRE_EQUAL( saturatingAddition( uint64_t( 2 ), MAX - 3U ), MAX - 1U );

    REQUIRE_EQUAL( saturatingAddition( MAX, MAX ), MAX );
    REQUIRE_EQUAL( saturatingAddition( MAX - 1U, MAX - 1U ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX, MAX - 1U ), MAX );
    REQUIRE_EQUAL( saturatingAddition( MAX - 1U, MAX - 1U ), MAX );
}


void
testSignedSaturatingAddition()
{
    REQUIRE_EQUAL( saturatingAddition( 0, 0 ), 0 );
    REQUIRE_EQUAL( saturatingAddition( 0, 1 ), 1 );
    REQUIRE_EQUAL( saturatingAddition( 1, 0 ), 1 );
    REQUIRE_EQUAL( saturatingAddition( 1, 1 ), 2 );

    REQUIRE_EQUAL( saturatingAddition( 0, -1 ), -1 );
    REQUIRE_EQUAL( saturatingAddition( -1, 0 ), -1 );
    REQUIRE_EQUAL( saturatingAddition( -2, 1 ), -1 );
    REQUIRE_EQUAL( saturatingAddition( 1, -2 ), -1 );
    REQUIRE_EQUAL( saturatingAddition( -2, -1 ), -3 );
    REQUIRE_EQUAL( saturatingAddition( -1, -2 ), -3 );

    constexpr auto MAX = std::numeric_limits<int64_t>::max();
    REQUIRE_EQUAL( saturatingAddition( MAX, int64_t( 0 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( int64_t( 0 ), MAX ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX, int64_t( 1 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( int64_t( 1 ), MAX ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX - 1, int64_t( 1 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( int64_t( 1 ), MAX - 1 ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX - 1, int64_t( 2 ) ), MAX );
    REQUIRE_EQUAL( saturatingAddition( int64_t( 2 ), MAX - 1 ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX - 3, int64_t( 2 ) ), MAX - 1 );
    REQUIRE_EQUAL( saturatingAddition( int64_t( 2 ), MAX - 3 ), MAX - 1 );

    REQUIRE_EQUAL( saturatingAddition( MAX, MAX ), MAX );
    REQUIRE_EQUAL( saturatingAddition( MAX - 1, MAX - 1 ), MAX );

    REQUIRE_EQUAL( saturatingAddition( MAX, MAX - 1 ), MAX );
    REQUIRE_EQUAL( saturatingAddition( MAX - 1, MAX - 1 ), MAX );
}


void
testCountNewlines()
{
    REQUIRE_EQUAL( countNewlines( "" ), 0U );
    REQUIRE_EQUAL( countNewlines( " " ), 0U );
    REQUIRE_EQUAL( countNewlines( "\n" ), 1U );
    REQUIRE_EQUAL( countNewlines( "\n " ), 1U );
    REQUIRE_EQUAL( countNewlines( " \n" ), 1U );
    REQUIRE_EQUAL( countNewlines( "\n\n" ), 2U );
    REQUIRE_EQUAL( countNewlines( "\n \n" ), 2U );
    REQUIRE_EQUAL( countNewlines( " \n \n" ), 2U );
    REQUIRE_EQUAL( countNewlines( " \n \n " ), 2U );
}


void
testFindNthNewline()
{
    const auto NPOS = std::string_view::npos;

    const auto makeResult =
        [] ( std::size_t position, uint64_t lineCount ) {
            return FindNthNewlineResult{ position, lineCount };
        };

    REQUIRE_EQUAL( findNthNewline( "", 0 ), makeResult( NPOS, 0 ) );
    REQUIRE_EQUAL( findNthNewline( " ", 0 ), makeResult( NPOS, 0 ) );
    REQUIRE_EQUAL( findNthNewline( "\n ", 0 ), makeResult( NPOS, 0 ) );
    REQUIRE_EQUAL( findNthNewline( " \n", 0 ), makeResult( NPOS, 0 ) );

    REQUIRE_EQUAL( findNthNewline( "", 1 ), makeResult( NPOS, 1 ) );
    REQUIRE_EQUAL( findNthNewline( " ", 1 ), makeResult( NPOS, 1 ) );
    REQUIRE_EQUAL( findNthNewline( "\n ", 1 ), makeResult( 0, 0 ) );
    REQUIRE_EQUAL( findNthNewline( " \n", 1 ), makeResult( 1, 0 ) );
    REQUIRE_EQUAL( findNthNewline( " \n\n", 1 ), makeResult( 1, 0 ) );

    REQUIRE_EQUAL( findNthNewline( "", 2 ), makeResult( NPOS, 2 ) );
    REQUIRE_EQUAL( findNthNewline( " ", 2 ), makeResult( NPOS, 2 ) );
    REQUIRE_EQUAL( findNthNewline( "\n ", 2 ), makeResult( NPOS, 1 ) );
    REQUIRE_EQUAL( findNthNewline( " \n", 2 ), makeResult( NPOS, 1 ) );
    REQUIRE_EQUAL( findNthNewline( " \n\n", 2 ), makeResult( 2, 0 ) );
}


int
main()
{
    testIsBase64();
    testUnsignedSaturatingAddition();
    testSignedSaturatingAddition();
    testCountNewlines();
    testFindNthNewline();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
