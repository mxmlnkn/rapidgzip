#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <common.hpp>


template<uint8_t bitsSet>
uint32_t nLowestBitsSet32()
{
    return nLowestBitsSet<uint32_t, bitsSet>();
}

template<uint8_t bitsSet>
uint64_t nLowestBitsSet64()
{
    return nLowestBitsSet<uint64_t, bitsSet>();
}


int
main( int    argc,
      char** argv )
{
    REQUIRE( nLowestBitsSet<uint32_t>( 0 ) == uint32_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 1 ) == uint32_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 2 ) == uint32_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 3 ) == uint32_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 8 ) == uint32_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet<uint32_t>( 32 ) == uint32_t( 0xFFFF'FFFF ) );

    REQUIRE( nLowestBitsSet32<0 >() == uint32_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet32<1 >() == uint32_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet32<2 >() == uint32_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet32<3 >() == uint32_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet32<8 >() == uint32_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet32<32>() == uint32_t( 0xFFFF'FFFFU ) );

    REQUIRE( nLowestBitsSet<uint64_t>( 0 ) == uint64_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 1 ) == uint64_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 2 ) == uint64_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 3 ) == uint64_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 8 ) == uint64_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet<uint64_t>( 64 ) == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );

    REQUIRE( nLowestBitsSet64<0 >() == uint64_t( 0b0000'0000U ) );
    REQUIRE( nLowestBitsSet64<1 >() == uint64_t( 0b0000'0001U ) );
    REQUIRE( nLowestBitsSet64<2 >() == uint64_t( 0b0000'0011U ) );
    REQUIRE( nLowestBitsSet64<3 >() == uint64_t( 0b0000'0111U ) );
    REQUIRE( nLowestBitsSet64<8 >() == uint64_t( 0b1111'1111U ) );
    REQUIRE( nLowestBitsSet64<64>() == uint64_t( 0xFFFF'FFFF'FFFF'FFFFULL ) );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
