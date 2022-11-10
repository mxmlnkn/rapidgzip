#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <Cache.hpp>
#include <common.hpp>
#include <TestHelpers.hpp>


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


int main()
{
    testIsBase64();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
