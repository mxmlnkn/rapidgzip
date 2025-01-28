#include <iostream>

#include <FasterVector.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


template<typename Vector>
void
testVector()
{
    /* Simple construction and destruction of empty vector. */
    {
        Vector vector;
        REQUIRE_EQUAL( vector.size(), 0U );
        REQUIRE_EQUAL( vector.capacity(), 0U );
        REQUIRE( vector.empty() );
        REQUIRE( vector.begin() == vector.end() );
        REQUIRE( vector.cbegin() == vector.cend() );
        REQUIRE( vector.rbegin() == vector.rend() );
        REQUIRE( vector.crbegin() == vector.crend() );
        REQUIRE( vector == Vector() );
    }

    /* Resize, reserve, clear vector. */
    {
        static constexpr size_t initialSize = 17;
        Vector vector( initialSize );

        REQUIRE_EQUAL( vector.size(), initialSize );
        REQUIRE_EQUAL( vector.capacity(), initialSize );
        REQUIRE( !vector.empty() );
        REQUIRE( vector.begin() + vector.size() == vector.end() );
        REQUIRE( vector.cbegin() + vector.size() == vector.cend() );
        REQUIRE( vector.rbegin() + vector.size() == vector.rend() );
        REQUIRE( vector.crbegin() + vector.size() == vector.crend() );
        REQUIRE( vector != Vector() );

        vector.clear();
        REQUIRE_EQUAL( vector.size(), 0U );
        REQUIRE_EQUAL( vector.capacity(), initialSize );
        REQUIRE( vector.empty() );

        vector.reserve( initialSize - 1 );
        REQUIRE_EQUAL( vector.size(), 0U );
        REQUIRE_EQUAL( vector.capacity(), initialSize );
        REQUIRE( vector.empty() );

        vector.reserve( initialSize + 1 );
        REQUIRE_EQUAL( vector.size(), 0U );
        REQUIRE_EQUAL( vector.capacity(), initialSize + 1 );
        REQUIRE( vector.empty() );

        vector.resize( initialSize + 1 );
        REQUIRE_EQUAL( vector.size(), initialSize + 1 );
        REQUIRE_EQUAL( vector.capacity(), initialSize + 1 );
        REQUIRE( !vector.empty() );

        Vector toAppend( 13, 13 );
        REQUIRE_EQUAL( toAppend[0], 13 );
        REQUIRE_EQUAL( toAppend[toAppend.size() - 2], 13 );
        REQUIRE_EQUAL( toAppend.back(), 13 );

        vector.insert( vector.end(), toAppend.begin(), toAppend.end() );
        REQUIRE_EQUAL( vector.size(), initialSize + 1 + 13 );
        REQUIRE( vector.capacity() >= vector.size() );  // 32 for our custom FasterVector but 36 for std::vector!
        REQUIRE( !vector.empty() );
        REQUIRE_EQUAL( vector.back(), 13 );
    }
}


int
main()
{
    testVector<FasterVector<uint16_t> >();

    std::cout << "\nTests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
