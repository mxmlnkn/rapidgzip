#include <iostream>

#include <CompressedVector.hpp>
#include <TestHelpers.hpp>


void
testEmptyCompressedVector()
{
    CompressedVector vector;
    REQUIRE( vector.empty() );
    REQUIRE( vector.decompress()->empty() );
    REQUIRE( vector.compressedData()->empty() );
    REQUIRE_EQUAL( vector.compressedSize(), 0U );
    REQUIRE_EQUAL( vector.decompressedSize(), 0U );

    vector.clear();
    REQUIRE( vector.empty() );
    REQUIRE( vector.decompress()->empty() );
    REQUIRE( vector.compressedData()->empty() );
    REQUIRE_EQUAL( vector.compressedSize(), 0U );
    REQUIRE_EQUAL( vector.decompressedSize(), 0U );
}

void
testCompressedVector()
{
    const std::vector<uint8_t> toCompress = { 0, 0, 0, 1, 1, 1 };
    CompressedVector<std::vector<uint8_t> > vector( std::vector<uint8_t>( toCompress ), CompressionType::GZIP );
    REQUIRE( !vector.empty() );
    REQUIRE( !vector.compressedData()->empty() );
    REQUIRE_EQUAL( *vector.decompress(), toCompress );
    REQUIRE_EQUAL( vector.decompressedSize(), toCompress.size() );

    vector.clear();
    REQUIRE( vector.empty() );
    REQUIRE( vector.decompress()->empty() );
    REQUIRE( vector.compressedData()->empty() );
    REQUIRE_EQUAL( vector.compressedSize(), 0U );
    REQUIRE_EQUAL( vector.decompressedSize(), 0U );
}


int
main()
{
    testEmptyCompressedVector();
    testCompressedVector();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
