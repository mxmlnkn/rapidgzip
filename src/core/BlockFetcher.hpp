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

private:
    static constexpr bool SHOW_PROFILE{ false };

protected:
    BlockFetcher( std::shared_ptr<BlockFinder> blockFinder,
                  size_t                       parallelization ) :
        m_blockFinder    ( std::move( blockFinder ) ),
        m_parallelization( parallelization == 0
                           ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
                           : parallelization ),
        m_cache          ( 16 + m_parallelization ),
        m_threadPool     ( m_parallelization )
    {}

public:
    virtual
    ~BlockFetcher()
    {
        if constexpr ( SHOW_PROFILE ) {
            const auto cacheHitRate = ( m_cache.hits() + m_prefetchDirectHits )
                                      / static_cast<double>( m_cache.hits() + m_cache.misses() + m_prefetchDirectHits );
            std::cerr << (
                ThreadSafeOutput() << "[BlockFetcher::~BlockFetcher]"
                << "\n   Parallelization                   :" << m_parallelization
                << "\n   Hits"
                << "\n       Cache                         :" << m_cache.hits()
                << "\n       Prefetch Queue                :" << m_prefetchDirectHits
                << "\n   Misses                            :" << m_cache.misses()
                << "\n   Hit Rate                          :" << cacheHitRate
                << "\n   Prefetched Blocks                 :" << m_prefetchCount
                << "\n   Prefetch Stall by BlockFinder     :" << m_waitOnBlockFinderCount
                << "\n   Time spent in:"
                << "\n       bzip2::readBlockData          :" << m_readBlockDataTotalTime << "s"
                << "\n       decodeBlock                   :" << m_decodeBlockTotalTime   << "s"
                << "\n       std::future::get              :" << m_futureWaitTotalTime    << "s"
                << "\n       get                           :" << m_getTotalTime           << "s"
            );
        }

        m_cancelThreads = true;
        m_cancelThreadsCondition.notify_all();
    }

    /**
     * Fetches, prefetches, caches, and returns result.
     */
    [[nodiscard]] virtual std::shared_ptr<BlockData>
    get( size_t                blockOffset,
         std::optional<size_t> dataBlockIndex = {} )
    {
        [[maybe_unused]] const auto tGetStart = now();
        auto resultFuture = takeFromPrefetchQueue( blockOffset );

        /* Access cache before data might get evicted!
         * Access cache after prefetch queue to avoid incrementing the cache misses counter.*/
        std::optional<std::shared_ptr<BlockData> > result;
        if ( !resultFuture.valid() ) {
            result = m_cache.get( blockOffset );
        }

        const auto validDataBlockIndex = dataBlockIndex ? *dataBlockIndex : m_blockFinder->find( blockOffset );

        /* Start requested calculation if necessary. */
        if ( !result && !resultFuture.valid() ) {
            resultFuture = m_threadPool.submitTask( [this, blockOffset, validDataBlockIndex] () {
                return decodeAndMeasureBlock( validDataBlockIndex, blockOffset );
            } );
            assert( resultFuture.valid() );
        }

        processReadyPrefetches();

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
                m_getTotalTime += duration( tGetStart );
            }

            return *result;
        }

        [[maybe_unused]] const auto tFutureGetStart = now();
        result = std::make_shared<BlockData>( resultFuture.get() );
        [[maybe_unused]] const auto futureGetDuration = duration( tFutureGetStart );

        m_cache.insert( blockOffset, *result );

        if constexpr ( SHOW_PROFILE ) {
            std::scoped_lock lock( m_analyticsMutex );
            m_futureWaitTotalTime += futureGetDuration;
            m_getTotalTime += duration( tGetStart );
        }

        return *result;
    }

    void
    clearCache()
    {
        m_cache.clear();
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
                ++m_prefetchDirectHits;
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
                m_cache.insert( prefetchedBlockOffset, std::make_shared<BlockData>( prefetchedFuture.get() ) );

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
        /* Get blocks to prefetch. In order to avoid oscillating caches, the fetching strategy should ony return
         * less than the cache size number of blocks. It is fine if that means no work is being done in the background
         * for some calls to 'get'! */
        m_fetchingStrategy.fetch( dataBlockIndex );
        auto blocksToPrefetch = m_fetchingStrategy.prefetch( /* maxAmountToPrefetch */ m_parallelization );

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
            do
            {
                prefetchBlockOffset = m_blockFinder->get( blockIndexToPrefetch, stopPrefetching() ? 0 : 0.0001 );
            }
            while ( !prefetchBlockOffset && !stopPrefetching() );

            if constexpr ( SHOW_PROFILE ) {
                if ( !prefetchBlockOffset.has_value() ) {
                    m_waitOnBlockFinderCount++;
                }
            }

            /* Do not prefetch already cached/prefetched blocks or block indexes which are not yet in the block map. */
            if ( !prefetchBlockOffset.has_value()
                 || ( m_prefetching.find( *prefetchBlockOffset ) != m_prefetching.end() )
                 || m_cache.test( *prefetchBlockOffset ) )
            {
                continue;
            }

            ++m_prefetchCount;
            auto prefetchedFuture = m_threadPool.submitTask(
                [this, offset = *prefetchBlockOffset, blockIndexToPrefetch] () {
                    return decodeAndMeasureBlock( blockIndexToPrefetch, offset );
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
    decodeBlock( size_t blockIndex,
                 size_t blockOffset ) const = 0;

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
    decodeAndMeasureBlock( size_t blockIndex,
                           size_t blockOffset ) const
    {
        [[maybe_unused]] const auto tDecodeStart = now();
        auto blockData = decodeBlock( blockIndex, blockOffset );
        if constexpr ( SHOW_PROFILE ) {
            std::scoped_lock lock( this->m_analyticsMutex );
            this->m_decodeBlockTotalTime += duration( tDecodeStart );
        }
        return blockData;
    }

private:
    /* Analytics */
    size_t m_prefetchCount{ 0 };
    size_t m_prefetchDirectHits{ 0 };
    size_t m_waitOnBlockFinderCount{ 0 };
    mutable double m_decodeBlockTotalTime{ 0 };
    mutable double m_futureWaitTotalTime{ 0 };
    mutable double m_getTotalTime{ 0 };

protected:
    mutable double m_readBlockDataTotalTime{ 0 };
    mutable std::mutex m_analyticsMutex;

    const std::shared_ptr<BlockFinder> m_blockFinder;

private:
    std::atomic<bool> m_cancelThreads{ false };
    std::condition_variable m_cancelThreadsCondition;

    const size_t m_parallelization;

    Cache</** block offset in bits */ size_t, std::shared_ptr<BlockData> > m_cache;
    FetchingStrategy m_fetchingStrategy;

    std::map<size_t, std::future<BlockData> > m_prefetching;
    ThreadPool m_threadPool;
};
