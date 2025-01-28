#include <iostream>
#include <optional>

#include <Cache.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


void
testCacheReinsertion()
{
    Cache<size_t, double> cache( /* capacity */ 2 );

    cache.insert( 2, 4.0 );
    cache.insert( 1, 1.0 );
    /* Replacing an existing key's value (buffers with markers replaced) should not trigger evictions. */
    cache.insert( 1, 2.0 );

    /** Unused entries are those that got evicted without them being accessed first. */
    REQUIRE_EQUAL( cache.statistics().unusedEntries, 0U );
}


int
main()
{
    testCacheReinsertion();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
