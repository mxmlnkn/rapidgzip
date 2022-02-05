#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <BlockFinder.hpp>
#include <Cache.hpp>
#include <ParallelBitStringFinder.hpp>
#include <Prefetcher.hpp>
#include <ThreadPool.hpp>

#include "bzip2.hpp"


/**
 * Manages block data access. Calls to members are not thread-safe!
 * Requested blocks are cached and accesses may trigger prefetches,
 * which will be fetched in parallel using a thread pool.
 */
template<typename FetchingStrategy = FetchingStrategy::FetchNextSmart>
class BlockFetcher
{
public:
    using BlockFinder = ::BlockFinder<ParallelBitStringFinder<bzip2::MAGIC_BITS_SIZE> >;
    using BitReader = bzip2::BitReader;

    struct BlockHeaderData
    {
        size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
        size_t encodedSizeInBits{ 0 }; /**< When calling readBlockheader, only contains valid data if EOS block. */

        uint32_t expectedCRC{ 0 };  /**< if isEndOfStreamBlock == true, then this is the stream CRC. */
        bool isEndOfStreamBlock{ false };
        bool isEndOfFile{ false };
    };

    struct BlockData :
        public BlockHeaderData
    {
        std::vector<uint8_t> data;
        uint32_t calculatedCRC{ 0xFFFFFFFFL };
    };

public:
    BlockFetcher( BitReader                    bitReader,
                  std::shared_ptr<BlockFinder> blockFinder,
                  size_t                       parallelization ) :
        m_bitReader      ( bitReader ),
        m_blockFinder    ( std::move( blockFinder ) ),
        m_blockSize100k  ( bzip2::readBzip2Header( bitReader ) ),
        m_parallelization( parallelization == 0
                           ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
                           : parallelization ),
        m_cache          ( 16 + m_parallelization ),
        m_threadPool     ( m_parallelization )
    {}

    ~BlockFetcher()
    {
#if 0
        const auto cacheHitRate = ( m_cache.hits() + m_prefetchDirectHits )
                                  / static_cast<double>( m_cache.hits() + m_cache.misses() + m_prefetchDirectHits );
        std::cerr << (
            ThreadSafeOutput() << "[BlockFetcher::~BlockFetcher]"
            << "\n   Hits"
            << "\n       Cache                         :" << m_cache.hits()
            << "\n       Prefetch Queue                :" << m_prefetchDirectHits
            << "\n   Misses                            :" << m_cache.misses()
            << "\n   Hit Rate                          :" << cacheHitRate
            << "\n   Prefetched Blocks                 :" << m_prefetchCount
            << "\n   Prefetch Stall by BlockFinder     :" << m_waitOnBlockFinderCount
            << "\n   Time spent in:"
            << "\n       bzip2::readBlockData          :" << m_readBlockDataTotalTime << "s"
            << "\n       time spent in decodeBlock     :" << m_decodeBlockTotalTime   << "s"
            << "\n       time spent waiting on futures :" << m_futureWaitTotalTime    << "s"
        ).str();
#endif

        m_cancelThreads = true;
        m_cancelThreadsCondition.notify_all();
    }

    /**
     * Fetches, prefetches, caches, and returns result.
     */
    [[nodiscard]] std::shared_ptr<BlockData>
    get( size_t                blockOffset,
         std::optional<size_t> dataBlockIndex = {} )
    {

        /* Check whether the desired offset is prefetched. */
        std::future<BlockData> resultFuture;
        const auto match = std::find_if(
            m_prefetching.begin(), m_prefetching.end(),
            [blockOffset] ( auto const& kv ){ return kv.first == blockOffset; }
        );

        if ( match != m_prefetching.end() ) {
            resultFuture = std::move( match->second );
            m_prefetching.erase( match );
            assert( resultFuture.valid() );

            ++m_prefetchDirectHits;
        }

        /* Access cache before data might get evicted!
         * Access cache after prefetch queue to avoid incrementing the cache misses counter.*/
        std::optional<std::shared_ptr<BlockData> > result;
        if ( !resultFuture.valid() ) {
            result = m_cache.get( blockOffset );
        }

        /* Start requested calculation if necessary. */
        if ( !result && !resultFuture.valid() ) {
            resultFuture = m_threadPool.submitTask( [this, blockOffset](){ return decodeBlock( blockOffset ); } );
            assert( resultFuture.valid() );
        }

        using namespace std::chrono_literals;

        /* Check for ready prefetches and move them to cache. */
        for ( auto it = m_prefetching.begin(); it != m_prefetching.end(); ) {
            auto& [prefetchedBlockOffset, prefetchedFuture] = *it;

            if ( prefetchedFuture.valid() && ( prefetchedFuture.wait_for( 0s ) == std::future_status::ready ) ) {
                m_cache.insert( prefetchedBlockOffset, std::make_shared<BlockData>( prefetchedFuture.get() ) );

                it = m_prefetching.erase( it );
            } else {
                ++it;
            }
        }

        /* Get blocks to prefetch. In order to avoid oscillating caches, the fetching strategy should ony return
         * less than the cache size number of blocks. It is fine if that means no work is being done in the background
         * for some calls to 'get'! */
        if ( !dataBlockIndex ) {
            dataBlockIndex = m_blockFinder->find( blockOffset );
        }
        m_fetchingStrategy.fetch( *dataBlockIndex );
        auto blocksToPrefetch = m_fetchingStrategy.prefetch( m_parallelization );

        for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
            if ( m_prefetching.size() + /* thread with the requested block */ 1 >= m_parallelization ) {
                break;
            }

            if ( blockIndexToPrefetch == *dataBlockIndex ) {
                throw std::logic_error( "The fetching strategy should not return the "
                                        "last fetched block for prefetching!" );
            }

            if ( m_blockFinder->finalized() && ( blockIndexToPrefetch >= m_blockFinder->size() ) ) {
                continue;
            }

            const auto requestedResultIsReady =
                [&result, &resultFuture]()
                {
                    return result.has_value() ||
                        ( resultFuture.valid() && ( resultFuture.wait_for( 0s ) == std::future_status::ready ) );
                };

            /* If the block with the requested index has not been found yet and if we have to wait on the requested
             * result future anyway, then wait a non-zero amount of time on the BlockFinder! */
            std::optional<size_t> prefetchBlockOffset;
            do
            {
                prefetchBlockOffset = m_blockFinder->get( blockIndexToPrefetch, requestedResultIsReady() ? 0 : 0.001 );
            }
            while ( !prefetchBlockOffset && !requestedResultIsReady() );

            if ( !prefetchBlockOffset.has_value() ) {
                m_waitOnBlockFinderCount++;
            }

            /* Do not prefetch already cached/prefetched blocks or block indexes which are not yet in the block map. */
            if ( !prefetchBlockOffset.has_value()
                 || ( m_prefetching.find( *prefetchBlockOffset ) != m_prefetching.end() )
                 || m_cache.test( *prefetchBlockOffset ) )
            {
                continue;
            }

            ++m_prefetchCount;
            auto decodeTask = [this, offset = *prefetchBlockOffset](){ return decodeBlock( offset ); };
            auto prefetchedFuture = m_threadPool.submitTask( std::move( decodeTask ) );
            const auto [_, wasInserted] = m_prefetching.emplace( *prefetchBlockOffset, std::move( prefetchedFuture ) );
            if ( !wasInserted ) {
                std::logic_error( "Submitted future could not be inserted to prefetch queue!" );
            }
        }

        if ( m_threadPool.unprocessedTasksCount() > m_parallelization ) {
            throw std::logic_error( "The thread pool should not have more tasks than there are prefetching futures!" );

        }

        /* Return result */
        if ( result ) {
            assert( !resultFuture.valid() );
            return *result;
        }

        const auto t0 = std::chrono::high_resolution_clock::now();
        result = std::make_shared<BlockData>( resultFuture.get() );
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto dt = std::chrono::duration<double>( t1 - t0 ).count();
        {
            std::scoped_lock lock( m_analyticsMutex );
            m_futureWaitTotalTime += dt;
        }

        m_cache.insert( blockOffset, *result );
        return *result;
    }

    [[nodiscard]] BlockHeaderData
    readBlockHeader( size_t blockOffset ) const
    {
        BitReader bitReader( m_bitReader );
        bitReader.seek( blockOffset );
        bzip2::Block block( bitReader );

        BlockHeaderData result;
        result.encodedOffsetInBits = blockOffset;
        result.isEndOfStreamBlock = block.eos();
        result.isEndOfFile = block.eof();
        result.expectedCRC = block.bwdata.headerCRC;

        if ( block.eos() ) {
            result.encodedSizeInBits = block.encodedSizeInBits;
        }

        return result;
    }

private:
    [[nodiscard]] BlockData
    decodeBlock( size_t blockOffset ) const
    {
        const auto t0 = std::chrono::high_resolution_clock::now();

        BitReader bitReader( m_bitReader );
        bitReader.seek( blockOffset );
        bzip2::Block block( bitReader );

        BlockData result;
        result.encodedOffsetInBits = blockOffset;
        result.isEndOfStreamBlock = block.eos();
        result.isEndOfFile = block.eof();
        result.expectedCRC = block.bwdata.headerCRC;

        /* Actually, this should never happen with the current implementation because only blocks found by the
         * block finder will be handled here and the block finder does not search for EOS magic bits. */
        if ( block.eos() ) {
            result.encodedSizeInBits = block.encodedSizeInBits;
            return result;
        }


        const auto t2 = std::chrono::high_resolution_clock::now();
        block.readBlockData();
        const auto t3 = std::chrono::high_resolution_clock::now();
        const auto dt2 = std::chrono::duration<double>( t3 - t2 ).count();
        {
            std::scoped_lock lock( m_analyticsMutex );
            m_readBlockDataTotalTime += dt2;
        }

        size_t decodedDataSize = 0;
        do
        {
            /* Increase buffer for next batch. Unfortunately we can't find the perfect size beforehand because
             * we don't know the amount of decoded bytes in the block. */
            /** @todo We do have that information after the block index has been built! */
            if ( result.data.empty() ) {
                /* Just a guess to avoid reallocations at smaller sizes. Must be >= 255 though because the decodeBlock
                 * method might return up to 255 copies caused by the runtime length decoding! */
                result.data.resize( m_blockSize100k * 100'000 + 255 );
            } else {
                result.data.resize( result.data.size() * 2 );
            }

            decodedDataSize += block.bwdata.decodeBlock(
                result.data.size() - 255U - decodedDataSize,
                reinterpret_cast<char*>( result.data.data() ) + decodedDataSize
            );
        }
        while ( block.bwdata.writeCount > 0 );

        result.data.resize( decodedDataSize );
        result.encodedSizeInBits = block.encodedSizeInBits;
        result.calculatedCRC = block.bwdata.dataCRC;

        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto dt = std::chrono::duration<double>( t1 - t0 ).count();
        {
            std::scoped_lock lock( m_analyticsMutex );
            m_decodeBlockTotalTime += dt;
        }

        return result;
    }

private:
    /* Analytics */
    size_t m_prefetchCount{ 0 };
    size_t m_prefetchDirectHits{ 0 };
    size_t m_waitOnBlockFinderCount{ 0 };
    mutable double m_readBlockDataTotalTime{ 0 };
    mutable double m_decodeBlockTotalTime{ 0 };
    mutable double m_futureWaitTotalTime{ 0 };
    mutable std::mutex m_analyticsMutex;

    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    const std::shared_ptr<BlockFinder> m_blockFinder;
    uint8_t m_blockSize100k;

    /** Future holding the number of found magic bytes. Used to determine whether the thread is still running. */
    std::atomic<bool> m_cancelThreads{ false };
    std::condition_variable m_cancelThreadsCondition;

    const size_t m_parallelization;

    Cache</** block offset in bits */ size_t, std::shared_ptr<BlockData> > m_cache;
    FetchingStrategy m_fetchingStrategy;

    std::map<size_t, std::future<BlockData> > m_prefetching;
    ThreadPool m_threadPool;
};
