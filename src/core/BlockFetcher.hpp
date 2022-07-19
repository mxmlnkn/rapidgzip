#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include <Cache.hpp>
#include <common.hpp>
#include <Prefetcher.hpp>
#include <ThreadPool.hpp>


/**
 * Manages block data access. Calls to members are not thread-safe!
 * Requested blocks are cached and accesses may trigger prefetches,
 * which will be fetched in parallel using a thread pool.
 */
template<typename T_BlockFinder,
         typename T_BlockData,
         typename FetchingStrategy>
class BlockFetcher
{
public:
    using BlockFinder = T_BlockFinder;
    using BlockData = T_BlockData;
    using BlockCache = Cache</** block offset in bits */ size_t, std::shared_ptr<BlockData> >;

    struct Statistics
    {
    public:
        [[nodiscard]] double
        cacheHitRate() const
        {
            return static_cast<double>( cache.hits + prefetchCache.hits + prefetchDirectHits )
                   / static_cast<double>( gets );
        }

        [[nodiscard]] double
        uselessPrefetches() const
        {
            const auto totalFetches = prefetchCount + onDemandFetchCount;
            if ( totalFetches == 0 ) {
                return 0;
            }
            return static_cast<double>( prefetchCache.unusedEntries ) / static_cast<double>( totalFetches );
        }

        [[nodiscard]] std::string
        print() const
        {
            std::stringstream existingBlocks;
            existingBlocks << ( blockCountFinalized ? "" : ">=" ) << blockCount;

            std::stringstream out;
            out << "\n   Parallelization                   : " << parallelization
                << "\n   Cache"
                << "\n       Hits                          : " << cache.hits
                << "\n       Misses                        : " << cache.misses
                << "\n       Unused Entries                : " << cache.unusedEntries
                << "\n   Prefetch Cache"
                << "\n       Hits                          : " << prefetchCache.hits
                << "\n       Misses                        : " << prefetchCache.misses
                << "\n       Unused Entries                : " << prefetchCache.unusedEntries
                << "\n       Prefetch Queue Hit            : " << prefetchDirectHits
                << "\n   Cache Hit Rate                    : " << cacheHitRate() * 100 << " %"
                << "\n   Useless Prefetches                : " << uselessPrefetches() * 100 << " %"
                << "\n   Access Patterns"
                << "\n       Total Accesses                : " << gets
                << "\n       Duplicate Block Accesses      : " << repeatedBlockAccesses
                << "\n       Sequential Block Accesses     : " << sequentialBlockAccesses
                << "\n       Block Seeks Back              : " << backwardBlockAccesses
                << "\n       Block Seeks Forward           : " << forwardBlockAccesses
                << "\n   Blocks"
                << "\n       Total Existing                : " << existingBlocks.str()
                << "\n       Total Fetched                 : " << prefetchCount + onDemandFetchCount
                << "\n       Prefetched                    : " << prefetchCount
                << "\n       Fetched On-demand             : " << onDemandFetchCount
                << "\n   Prefetch Stall by BlockFinder     : " << waitOnBlockFinderCount
                << "\n   Time spent in:"
                << "\n       bzip2::readBlockData          : " << readBlockDataTotalTime << "s"
                << "\n       decodeBlock                   : " << decodeBlockTotalTime   << "s"
                << "\n       std::future::get              : " << futureWaitTotalTime    << "s"
                << "\n       get                           : " << getTotalTime           << "s";
            return out.str();
        }

        void
        recordBlockIndexGet( size_t blockindex )
        {
            ++gets;

            if ( !lastAccessedBlock ) {
                lastAccessedBlock = blockindex;  // effectively counts the first access as sequential
            }

            if ( blockindex > *lastAccessedBlock + 1 ) {
                forwardBlockAccesses++;
            } else if ( blockindex < *lastAccessedBlock ) {
                backwardBlockAccesses++;
            } else if ( blockindex == *lastAccessedBlock ) {
                repeatedBlockAccesses++;
            } else {
                sequentialBlockAccesses++;
            }

            lastAccessedBlock = blockindex;
        }

    public:
        size_t parallelization{ 0 };
        size_t blockCount{ 0 };
        bool blockCountFinalized{ false };

        typename BlockCache::Statistics cache;
        typename BlockCache::Statistics prefetchCache;

        size_t gets{ 0 };
        std::optional<size_t> lastAccessedBlock;
        size_t repeatedBlockAccesses{ 0 };
        size_t sequentialBlockAccesses{ 0 };
        size_t backwardBlockAccesses{ 0 };
        size_t forwardBlockAccesses{ 0 };

        size_t onDemandFetchCount{ 0 };
        size_t prefetchCount{ 0 };
        size_t prefetchDirectHits{ 0 };
        size_t waitOnBlockFinderCount{ 0 };

        double decodeBlockTotalTime{ 0 };
        double futureWaitTotalTime{ 0 };
        double getTotalTime{ 0 };
        double readBlockDataTotalTime{ 0 };
    };

private:
    static constexpr bool SHOW_PROFILE{ false };

protected:
    BlockFetcher( std::shared_ptr<BlockFinder> blockFinder,
                  size_t                       parallelization ) :
        m_parallelization( parallelization == 0
                           ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
                           : parallelization ),
        m_blockFinder    ( std::move( blockFinder ) ),
        m_cache          ( std::max( size_t( 16 ), m_parallelization ) ),
        m_prefetchCache  ( 2 * m_parallelization /* Only m_parallelization would lead to lot of cache pollution! */ ),
        m_threadPool     ( m_parallelization )
    {
        if ( !m_blockFinder ) {
            throw std::invalid_argument( "BlockFinder must be valid!" );
        }
    }

public:
    virtual
    ~BlockFetcher()
    {
        if constexpr ( SHOW_PROFILE ) {
            std::cerr << ( ThreadSafeOutput() << "[BlockFetcher::~BlockFetcher]" << statistics().print() );
        }

        m_cancelThreads = true;
        m_cancelThreadsCondition.notify_all();
    }

    /**
     * Fetches, prefetches, caches, and returns result.
     * @return BlockData to requested blockOffset. Undefined what happens for an invalid blockOffset as input.
     */
    [[nodiscard]] std::shared_ptr<BlockData>
    get( size_t                blockOffset,
         std::optional<size_t> dataBlockIndex = {},
         bool                  onlyCheckCaches = false )
    {
        [[maybe_unused]] const auto tGetStart = now();

        /* In case of a late prefetch, this might return an unfinished future. */
        auto resultFuture = takeFromPrefetchQueue( blockOffset );

        /* Access cache before data might get evicted!
         * Access cache after prefetch queue to avoid incrementing the cache misses counter.*/
        std::optional<std::shared_ptr<BlockData> > result;
        if ( !resultFuture.valid() ) {
            result = m_cache.get( blockOffset );
            if ( !result ) {
                /* On prefetch cache hit, move the value into the normal cache. */
                result = m_prefetchCache.get( blockOffset );
                if ( result ) {
                    m_prefetchCache.evict( blockOffset );
                    m_cache.insert( blockOffset, *result );
                }
            }
        }

        const auto validDataBlockIndex = dataBlockIndex ? *dataBlockIndex : m_blockFinder->find( blockOffset );
        const auto nextBlockOffset = m_blockFinder->get( validDataBlockIndex + 1 );

        if constexpr ( SHOW_PROFILE ) {
            m_statistics.recordBlockIndexGet( validDataBlockIndex );
        }

        /* Start requested calculation if necessary. */
        if ( !result && !resultFuture.valid() ) {
            if ( onlyCheckCaches ) {
                return {};
            }

            ++m_statistics.onDemandFetchCount;
            resultFuture = m_threadPool.submitTask( [this, blockOffset, nextBlockOffset] () {
                return decodeAndMeasureBlock(
                    blockOffset, nextBlockOffset ? *nextBlockOffset : std::numeric_limits<size_t>::max() );
            } );
            assert( resultFuture.valid() );
        }

        prefetchNewBlocks(
            validDataBlockIndex,
            [&] () {
                using namespace std::chrono_literals;
                return result.has_value() ||
                       ( resultFuture.valid() && ( resultFuture.wait_for( 0s ) == std::future_status::ready ) );
            }
        );

        /* Return result */
        if ( result ) {
            assert( !resultFuture.valid() );

            if constexpr ( SHOW_PROFILE ) {
                std::scoped_lock lock( m_analyticsMutex );
                m_statistics.getTotalTime += duration( tGetStart );
            }

            return *result;
        }

        [[maybe_unused]] const auto tFutureGetStart = now();
        result = std::make_shared<BlockData>( resultFuture.get() );
        [[maybe_unused]] const auto futureGetDuration = duration( tFutureGetStart );

        m_cache.insert( blockOffset, *result );

        if constexpr ( SHOW_PROFILE ) {
            std::scoped_lock lock( m_analyticsMutex );
            m_statistics.futureWaitTotalTime += futureGetDuration;
            m_statistics.getTotalTime += duration( tGetStart );
        }

        return *result;
    }

    void
    clearCache()
    {
        m_cache.clear();
    }

    [[nodiscard]] Statistics
    statistics() const
    {
        auto result = m_statistics;
        if ( m_blockFinder ) {
            result.blockCountFinalized = m_blockFinder->finalized();
            result.blockCount = m_blockFinder->size();
        }
        result.cache = m_cache.statistics();
        result.prefetchCache = m_prefetchCache.statistics();
        result.readBlockDataTotalTime = m_readBlockDataTotalTime;
        return result;
    }

private:
    [[nodiscard]] std::future<BlockData>
    takeFromPrefetchQueue( size_t blockOffset )
    {
        /* Check whether the desired offset is prefetched. */
        std::future<BlockData> resultFuture;
        const auto match = std::find_if(
            m_prefetching.begin(), m_prefetching.end(),
            [blockOffset] ( auto const& kv ) { return kv.first == blockOffset; }
        );

        if ( match != m_prefetching.end() ) {
            resultFuture = std::move( match->second );
            m_prefetching.erase( match );
            assert( resultFuture.valid() );

            if constexpr ( SHOW_PROFILE ) {
                ++m_statistics.prefetchDirectHits;
            }
        }

        return resultFuture;
    }

    /* Check for ready prefetches and move them to cache. */
    void
    processReadyPrefetches()
    {
        using namespace std::chrono_literals;

        for ( auto it = m_prefetching.begin(); it != m_prefetching.end(); ) {
            auto& [prefetchedBlockOffset, prefetchedFuture] = *it;

            if ( prefetchedFuture.valid() && ( prefetchedFuture.wait_for( 0s ) == std::future_status::ready ) ) {
                m_prefetchCache.insert( prefetchedBlockOffset, std::make_shared<BlockData>( prefetchedFuture.get() ) );

                it = m_prefetching.erase( it );
            } else {
                ++it;
            }
        }
    }

    /**
     * Fills m_prefetching up with a maximum of m_parallelization-1 new tasks predicted
     * based on the given last accessed block index(es).
     * @param dataBlockIndex The currently accessed block index as needed to query the prefetcher.
     * @param stopPrefetching The prefetcher might wait a bit on the block finder but when stopPrefetching returns true
     *                        it will stop that and return before having completely filled the prefetch queue.
     */
    void
    prefetchNewBlocks( size_t                       dataBlockIndex,
                       const std::function<bool()>& stopPrefetching )
    {
        /* Make space for new asynchronous prefetches. */
        processReadyPrefetches();

        /* Get blocks to prefetch. In order to avoid oscillating caches, the fetching strategy should ony return
         * less than the cache size number of blocks. It is fine if that means no work is being done in the background
         * for some calls to 'get'! */
        m_fetchingStrategy.fetch( dataBlockIndex );
        auto blocksToPrefetch = m_fetchingStrategy.prefetch( /* maxAmountToPrefetch */ m_parallelization );

        const auto touchInCacheIfExists =
            [this] ( size_t blockIndex )
            {
                if ( m_prefetchCache.test( blockIndex ) ) {
                    m_prefetchCache.touch( blockIndex );
                }
                if ( m_cache.test( blockIndex ) ) {
                    m_cache.touch( blockIndex );
                }
            };

        /* Touch all blocks to be prefetched to avoid evicting them while doing the prefetching of other blocks! */
        for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
            touchInCacheIfExists( blockIndexToPrefetch );
        }

        for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
            if ( m_prefetching.size() + /* thread with the requested block */ 1 >= m_parallelization ) {
                break;
            }

            if ( blockIndexToPrefetch == dataBlockIndex ) {
                throw std::logic_error( "The fetching strategy should not return the "
                                        "last fetched block for prefetching!" );
            }

            if ( m_blockFinder->finalized() && ( blockIndexToPrefetch >= m_blockFinder->size() ) ) {
                continue;
            }

            /* If the block with the requested index has not been found yet and if we have to wait on the requested
             * result future anyway, then wait a non-zero amount of time on the BlockFinder! */
            std::optional<size_t> prefetchBlockOffset;
            std::optional<size_t> nextPrefetchBlockOffset;
            do
            {
                prefetchBlockOffset = m_blockFinder->get( blockIndexToPrefetch, stopPrefetching() ? 0 : 0.0001 );
                const auto wasFinalized = m_blockFinder->finalized();
                nextPrefetchBlockOffset = m_blockFinder->get( blockIndexToPrefetch + 1,
                                                              stopPrefetching() ? 0 : 0.0001 );
                if ( wasFinalized && !nextPrefetchBlockOffset ) {
                    nextPrefetchBlockOffset = std::numeric_limits<size_t>::max();
                }
            }
            while ( !prefetchBlockOffset && !nextPrefetchBlockOffset && !stopPrefetching() );

            if constexpr ( SHOW_PROFILE ) {
                if ( !prefetchBlockOffset.has_value() ) {
                    m_statistics.waitOnBlockFinderCount++;
                }
            }

            touchInCacheIfExists( blockIndexToPrefetch );

            /* Do not prefetch already cached/prefetched blocks or block indexes which are not yet in the block map. */
            if ( !prefetchBlockOffset.has_value()
                 || !nextPrefetchBlockOffset.has_value()
                 || ( m_prefetching.find( *prefetchBlockOffset ) != m_prefetching.end() )
                 || m_cache.test( *prefetchBlockOffset )
                 || m_prefetchCache.test( *prefetchBlockOffset ) )
            {
                continue;
            }

            /* Avoid cache pollution by stopping prefetching when we would evict usable results. */
            if ( m_prefetchCache.size() >= m_prefetchCache.capacity() ) {
                const auto toBeEvicted = m_prefetchCache.cacheStrategy().nextEviction();
                if ( toBeEvicted && contains( blocksToPrefetch, *toBeEvicted ) ) {
                    break;
                }
            }

            ++m_statistics.prefetchCount;
            auto prefetchedFuture = m_threadPool.submitTask(
                [this, offset = *prefetchBlockOffset, nextOffset = *nextPrefetchBlockOffset] () {
                    return decodeAndMeasureBlock( offset, nextOffset );
                } );
            const auto [_, wasInserted] = m_prefetching.emplace( *prefetchBlockOffset, std::move( prefetchedFuture ) );
            if ( !wasInserted ) {
                std::logic_error( "Submitted future could not be inserted to prefetch queue!" );
            }
        }

        /* Note that only m_parallelization-1 blocks will be prefetched. Meaning that even with the unconditionally
         * submitted requested block, the thread pool should never contain more than m_parallelization tasks!
         * All tasks submitted to the thread pool, should either exist in m_prefetching or only temporary inside
         * 'resultFuture' in the 'read' method. */
        if ( m_threadPool.unprocessedTasksCount() > m_parallelization ) {
            throw std::logic_error( "The thread pool should not have more tasks than there are prefetching futures!" );
        }
    }

protected:
    [[nodiscard]] virtual BlockData
    decodeBlock( size_t blockOffset,
                 size_t nextBlockOffset ) const = 0;

    /**
     * This must be called before variables that are used by @ref decodeBlock are destructed, i.e.,
     * it must be called in the inheriting class.
     */
    void
    stopThreadPool()
    {
        m_threadPool.stop();
    }

private:
    [[nodiscard]] BlockData
    decodeAndMeasureBlock( size_t blockOffset,
                           size_t nextBlockOffset ) const
    {
        [[maybe_unused]] const auto tDecodeStart = now();
        auto blockData = decodeBlock( blockOffset, nextBlockOffset );
        if constexpr ( SHOW_PROFILE ) {
            std::scoped_lock lock( this->m_analyticsMutex );
            this->m_statistics.decodeBlockTotalTime += duration( tDecodeStart );
        }
        return blockData;
    }

private:
    /* Analytics */
    mutable Statistics m_statistics;

protected:
    mutable double m_readBlockDataTotalTime{ 0 };
    mutable std::mutex m_analyticsMutex;

private:
    std::atomic<bool> m_cancelThreads{ false };
    std::condition_variable m_cancelThreadsCondition;

    const size_t m_parallelization;

    /**
     * The block finder is used to prefetch blocks among others.
     * But, in general, it only returns unconfirmed guesses for block offsets (at first)!
     * Confirmed block offsets are written to the BlockMap but adding that in here seems a bit overkill
     * and would need further logic to get the next blocks given a specific one.
     * Therefore, the idea is to update and confirm the blocks inside the block finder, which would invalidate
     * the block indexes! In order for that, to not lead to problems, the block finder should only be used by
     * the managing thread not by the worker threads!
     */
    const std::shared_ptr<BlockFinder> m_blockFinder;

    BlockCache m_cache;
    BlockCache m_prefetchCache;
    FetchingStrategy m_fetchingStrategy;

    std::map</* block offset */ size_t, std::future<BlockData> > m_prefetching;
    ThreadPool m_threadPool;
};
