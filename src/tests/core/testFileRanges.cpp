#include <iostream>

#include <FileRanges.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;

using FileRanges = std::vector<FileRange>;


void
testFileRanges()
{
    REQUIRE_EQUAL( parseFileRanges( "" ), FileRanges{} );
    REQUIRE_EQUAL( parseFileRanges( "  " ), FileRanges{} );
    REQUIRE_EQUAL( parseFileRanges( "1@0" ), ( FileRanges{ FileRange{ 0, 1 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1B@0" ), ( FileRanges{ FileRange{ 0, 1 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1 B@0" ), ( FileRanges{ FileRange{ 0, 1 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1 kB@ 2 kiB" ), ( FileRanges{ FileRange{ 2048, 1000 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1 MB@ 2 MiB" ), ( FileRanges{ FileRange{ 2ULL << 20U, 1000'000 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1 GB@ 2 GiB" ), ( FileRanges{ FileRange{ 2ULL << 30U, 1000'000'000 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1k@2ki" ), ( FileRanges{ FileRange{ 2048, 1000 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1M@2Mi" ), ( FileRanges{ FileRange{ 2ULL << 20U, 1000'000 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1G@2Gi" ), ( FileRanges{ FileRange{ 2ULL << 30U, 1000'000'000 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "  1  @  0  " ), ( FileRanges{ FileRange{ 0, 1 } } ) );
    REQUIRE_EQUAL( parseFileRanges( " ,, 1  @  4  , 2@3 " ), ( FileRanges{ FileRange{ 4, 1 }, FileRange{ 3, 2 } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1L@2" ), ( FileRanges{ FileRange{ 2, 1, false, true } } ) );
    REQUIRE_EQUAL( parseFileRanges( "1@2KiL" ), ( FileRanges{ FileRange{ 2048, 1, true, false } } ) );
    REQUIRE_THROWS( parseFileRanges( "a" ) );
}


int
main()
{
    testFileRanges();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
