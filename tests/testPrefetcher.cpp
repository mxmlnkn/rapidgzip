
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <Cache.hpp>
#include <common.hpp>
#include <Prefetcher.hpp>


using namespace FetchingStrategy;


void testFetchNext()
{
    FetchNext strategy;
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    strategy.fetch( 24 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 25, 26, 27 } ) );
    strategy.fetch( 1 );
    REQUIRE_EQUAL( strategy.prefetch( 5 ), std::vector<size_t>( { 2, 3, 4, 5, 6 } ) );
}


void testFetchNextSmart()
{
    FetchNextSmart strategy;
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );

    /* Strictly speaking this is not a consecutive access and therefore an empty list could be correct.
     * However, duplicate fetches should not alter the returned prefetche list so that if there was not
     * enough time in the last call to prefetch everything, now on this call those missing prefetch suggestions
     * can be added to the cache. */
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );

    for ( size_t index = 24; index < 40; ++index ) {
        strategy.fetch( index );

        const auto maxPrefetchCount = 8;
        std::vector<size_t> expectedResult( maxPrefetchCount );
        std::iota( expectedResult.begin(), expectedResult.end(), index + 1 );
        REQUIRE_EQUAL( strategy.prefetch( maxPrefetchCount ), expectedResult );
    }

    /* A single random seek after a lot of consecutive ones should not result in an empty list at once. */
    strategy.fetch( 3 );
    for ( auto prefetchCount = 1; prefetchCount < 10; ++prefetchCount ) {
        REQUIRE( !strategy.prefetch( prefetchCount ).empty() );
        REQUIRE_EQUAL( strategy.prefetch( prefetchCount ).front(), size_t( 4 ) );
    }

    /* After a certain amount of non-consecutive fetches, an empty prefetch list should be returned. */
    {
        const size_t prefetchCount = 10;
        for ( size_t i = 0; i < 10000 * prefetchCount; i += prefetchCount ) {
            strategy.fetch( i );
        }
        REQUIRE( strategy.prefetch( prefetchCount ).empty() );
    }
}


/**
 * Trimmed down BlockFetcher class without the bzip2 decoding and without threading.
 * Threading is simulated and assumes that all task finish in equal time.
 * Conversion between block offsets and block indexes is obviously also stripped.
 */
template<typename FetchingStrategy>
class BlockFetcher
{
public:
    using Result = size_t;

public:
    explicit
    BlockFetcher( size_t parallelization ) :
        m_parallelization( parallelization )
    {}

    /**
     * Fetches, prefetches, caches, and returns result.
     */
    Result
    get( size_t dataBlockIndex )
    {
        /* Access cache before data might get evicted! */
        const auto result = m_cache.get( dataBlockIndex );

        m_fetchingStrategy.fetch( dataBlockIndex );
        auto blocksToPrefetch = m_fetchingStrategy.prefetch( m_parallelization - 1 /* fetched block */ );

        for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
            /* Do not prefetch already cached/prefetched blocks. */
            if ( m_cache.test( blockIndexToPrefetch ) ) {
                continue;
            }

            ++m_prefetchCount;
            /* Put directly into cache, assuming no computation time or rather because the multithreading is stripped */
            m_cache.insert( blockIndexToPrefetch, blockIndexToPrefetch );
        }

        /* Return cached result */
        if ( result ) {
            return *result;
        }

        m_cache.insert( dataBlockIndex, dataBlockIndex );
        return dataBlockIndex;
    }

    [[nodiscard]] size_t
    prefetchCount() const
    {
        return m_prefetchCount;
    }

    void
    resetPrefetchCount()
    {
        m_prefetchCount = 0;
    }

    [[nodiscard]] auto&
    cache()
    {
        return m_cache;
    }

private:
    size_t m_prefetchCount{ 0 };

    const size_t m_parallelization;

    Cache</** block offset in bits */ size_t, Result> m_cache{ 16 + m_parallelization };
    FetchingStrategy m_fetchingStrategy;
};


void
benchmarkFetchNext()
{
    std::cerr << "FetchNext strategy:\n";

    const size_t parallelization = 16;
    BlockFetcher<FetchNext> blockFetcher( parallelization );
    const auto cacheSize = blockFetcher.cache().capacity();

    size_t indexToGet = 0;

    /* Consecutive access should basically only result in a single miss at the beginning, rest is prefetched! */
    {
        constexpr size_t nConsecutive = 1000;
        for ( size_t i = 0; i < nConsecutive; ++i ) {
            blockFetcher.get( indexToGet + i );
        }
        indexToGet += nConsecutive;

        const auto hits = blockFetcher.cache().hits();
        const auto misses = blockFetcher.cache().misses();
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Sequential access: prefetches: " << prefetches
                  << ", misses: " << misses << ", hits: " << hits << "\n";

        REQUIRE_EQUAL( hits + misses, nConsecutive );
        REQUIRE_EQUAL( misses, size_t( 1 ) );
        REQUIRE_EQUAL( prefetches,
                       nConsecutive + parallelization
                       - /* first element does not get prefetched */ 1
                       - /* at the tail end only parallelization - 1 are prefetched */ 1 );
    }

    /* Even for random accesses always prefetch the next n elements */
    {
        indexToGet += parallelization;
        const size_t nRandomCoolDown = blockFetcher.cache().capacity();
        for ( size_t i = 0; i < nRandomCoolDown; ++i  ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }
        indexToGet += nRandomCoolDown * cacheSize * 2;

        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        constexpr size_t nRandom = 1000;
        for ( size_t i = 0; i < nRandom; ++i ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }

        const auto hits = blockFetcher.cache().hits();
        const auto misses = blockFetcher.cache().misses();
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Random access: prefetches: " << prefetches
                  << ", misses: " << misses << ", hits: " << hits << "\n";

        REQUIRE_EQUAL( misses, nRandom );
        REQUIRE_EQUAL( hits, size_t( 0 ) );
        REQUIRE_EQUAL( prefetches, nRandom * ( parallelization - 1 ) );
    }

    /* Always fetch the next n elements even after changing from random access to consecutive again. */
    {
        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        blockFetcher.get( 0 );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization - 1 );

        blockFetcher.get( 1 );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization );
    }
}


void
benchmarkFetchNextSmart()
{
    std::cerr << "FetchNextSmart strategy:\n";

    const size_t parallelization = 16;
    BlockFetcher<FetchNextSmart> blockFetcher( parallelization );
    const auto cacheSize = blockFetcher.cache().capacity();

    size_t indexToGet = 0;

    /* Consecutive access should basically only result in a single miss at the beginning, rest is prefetched! */
    {
        constexpr size_t nConsecutive = 1000;

        blockFetcher.get( indexToGet );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization - 1 );

        blockFetcher.get( indexToGet + 1 );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization );

        for ( size_t i = 0; i < nConsecutive - 2; ++i ) {
            blockFetcher.get( indexToGet + 2 + i );
        }
        indexToGet += nConsecutive;

        const auto hits = blockFetcher.cache().hits();
        const auto misses = blockFetcher.cache().misses();
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Sequential access: prefetches: " << prefetches
                  << ", misses: " << misses << ", hits: " << hits << "\n";

        REQUIRE_EQUAL( hits + misses, nConsecutive );
        REQUIRE_EQUAL( misses, size_t( 1 ) );
        REQUIRE_EQUAL( prefetches,
                       nConsecutive + parallelization
                       - /* first element does not get prefetched */ 1
                       - /* at the tail end only parallelization - 1 are prefetched */ 1 );
    }

    /* Random accesses should after a time, not prefetch anything anymore. */
    {
        indexToGet += parallelization;
        const size_t nRandomCoolDown = blockFetcher.cache().capacity();
        for ( size_t i = 0; i < nRandomCoolDown; ++i  ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }
        indexToGet += nRandomCoolDown * cacheSize * 2;

        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        constexpr size_t nRandom = 1000;
        for ( size_t i = 0; i < nRandom; ++i ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }

        const auto hits = blockFetcher.cache().hits();
        const auto misses = blockFetcher.cache().misses();
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Random access: prefetches: " << prefetches
                  << ", misses: " << misses << ", hits: " << hits << "\n";

        REQUIRE_EQUAL( misses, nRandom );
        REQUIRE_EQUAL( hits, size_t( 0 ) );
        REQUIRE_EQUAL( prefetches, size_t( 0 ) );
    }

    /* Double access to same should be cached. */
    {
        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        blockFetcher.get( 100 );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), size_t( 0 ) );

        blockFetcher.get( 100 );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), size_t( 0 ) );
    }

    /* After random accesses, consecutive accesses, should start prefetching again. */
    {
        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        /* First access still counts as random one because last access was to a very high index! */
        blockFetcher.get( 0 );

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), size_t( 0 ) );

        /* After 1st consecutive access begin to slowly prefetch with exponential speed up to maxPrefetchCount! */
        blockFetcher.get( 1 );

        std::cerr << "  After 2nd new consecutive access: prefetches: " << blockFetcher.prefetchCount()
                  << ", misses: " << blockFetcher.cache().misses() << ", hits: " << blockFetcher.cache().hits() << "\n";

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 2 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 0 ) );
        REQUIRE( blockFetcher.prefetchCount() >= 1 );

        blockFetcher.get( 2 );

        std::cerr << "  After 3rd new consecutive access: prefetches: " << blockFetcher.prefetchCount()
                  << ", misses: " << blockFetcher.cache().misses() << ", hits: " << blockFetcher.cache().hits() << "\n";

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 2 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 1 ) );
        REQUIRE( blockFetcher.prefetchCount() >= 1 );

        /* At the latest after four consecutive acceses should it prefetch at full parallelization! */
        blockFetcher.get( 3 );

        std::cerr << "  After 3rd new consecutive access: prefetches: " << blockFetcher.prefetchCount()
                  << ", misses: " << blockFetcher.cache().misses() << ", hits: " << blockFetcher.cache().hits() << "\n";

        REQUIRE_EQUAL( blockFetcher.cache().misses(), size_t( 2 ) );
        REQUIRE_EQUAL( blockFetcher.cache().hits(), size_t( 2 ) );
        REQUIRE( blockFetcher.prefetchCount() > parallelization );
    }
}


int
main()
{
    testFetchNext();
    testFetchNextSmart();

    benchmarkFetchNext();
    benchmarkFetchNextSmart();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
