#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bzip2.hpp"
#include "BitStringFinder.hpp"
#include "BZ2ReaderInterface.hpp"
#include "Cache.hpp"
#include "common.hpp"
#include "FileReader.hpp"
#include "ParallelBitStringFinder.hpp"
#include "Prefetcher.hpp"
#include "ThreadPool.hpp"

#ifdef WITH_PYTHON_SUPPORT
    #include "PythonFileReader.hpp"
#endif


/**
 * Stores results in the order they are pushed and also stores a flag signaling that nothing will be pushed anymore.
 * The blockfinder will push block offsets and other actors, e.g., the prefetcher, may wait for and read the offsets.
 * Results will never be deleted, so you can assume the size to only grow.
 */
template<typename Value>
class StreamedResults
{
public:
    /**
     * std::vector would work as well but the reallocations during appending might slow things down.
     * For the index access operations, a container with random access iterator, would yield better performance.
     */
    using Values = std::deque<Value>;

    class ResultsView
    {
    public:
        ResultsView( const Values* results,
                     std::mutex*   mutex ) :
            m_results( results ),
            m_lock( *mutex )
        {
            if ( m_results == nullptr ) {
                throw std::invalid_argument( "Arguments may not be nullptr!" );
            }
        }

        [[nodiscard]] const Values&
        results() const
        {
            return *m_results;
        }

    private:
        Values const * const m_results;
        std::scoped_lock<std::mutex> const m_lock;
    };

public:
    [[nodiscard]] size_t
    size() const
    {
        std::scoped_lock lock( m_mutex );
        return m_results.size();
    }

    /**
     * @param timeoutInSeconds Use infinity or 0 to wait forever or not wait at all.
     * @return the result at the requested position.
     */
    [[nodiscard]] std::optional<Value>
    get( size_t position,
         double timeoutInSeconds = std::numeric_limits<double>::infinity() ) const
    {
        std::unique_lock lock( m_mutex );

        if ( timeoutInSeconds > 0 ) {
            const auto predicate = [&] () { return m_finalized || ( position < m_results.size() ); };

            if ( std::isfinite( timeoutInSeconds ) ) {
                const auto timeout = std::chrono::nanoseconds( static_cast<size_t>( timeoutInSeconds * 1e9 ) );
                m_changed.wait_for( lock, timeout, predicate );
            } else {
                m_changed.wait( lock, predicate );
            }
        }

        if ( position < m_results.size() ) {
            return m_results[position];
        }
        return std::nullopt;
    }

    void
    push( Value value )
    {
        std::scoped_lock lock( m_mutex );

        if ( m_finalized ) {
            throw std::invalid_argument( "You may not push to finalized StreamedResults!" );
        }

        m_results.emplace_back( std::move( value ) );
        m_changed.notify_all();
    }

    void
    finalize( std::optional<size_t> resultsCount = {} )
    {
        std::scoped_lock lock( m_mutex );

        if ( resultsCount ) {
            if ( *resultsCount > m_results.size() ) {
                throw std::invalid_argument( "You may not finalize to a size larger than the current results buffer!" );
            }

            m_results.resize( *resultsCount );
        }

        m_finalized = true;
        m_changed.notify_all();
    }

    [[nodiscard]] bool
    finalized() const
    {
        return m_finalized;
    }


    /** @return a view to the results, which also locks access to it using RAII. */
    [[nodiscard]] ResultsView
    results() const
    {
        return ResultsView( &m_results, &m_mutex );
    }

    void
    setResults( Values results )
    {
        std::scoped_lock lock( m_mutex );

        m_results = std::move( results );
        m_finalized = true;
        m_changed.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_changed;

    Values m_results;
    std::atomic<bool> m_finalized = false;
};


/**
 * Will asynchronously find the next n block offsets after the last highest requested one.
 * It might find false positives and it won't find EOS blocks, so there is some post-processing necessary.
 */
class BlockFinder
{
public:
    using BitStringFinder = ParallelBitStringFinder<bzip2::MAGIC_BITS_SIZE>;
    using BlockOffsets = StreamedResults<size_t>::Values;

public:
    explicit
    BlockFinder( std::unique_ptr<FileReader> fileReader,
                 size_t                      parallelization ) :
        m_bitStringFinder(
            std::make_unique<BitStringFinder>( std::move( fileReader ), bzip2::MAGIC_BITS_BLOCK, parallelization ) )
    {}

    ~BlockFinder()
    {
        std::scoped_lock lock( m_mutex );
        m_cancelThread = true;
        m_changed.notify_all();
    }

public:
    void
    startThreads()
    {
        if ( !m_bitStringFinder ) {
            throw std::invalid_argument( "You may not start the block finder without a valid bit string finder!" );
        }

        if ( !m_blockFinder ) {
            m_blockFinder = std::make_unique<JoiningThread>( &BlockFinder::blockFinderMain, this );
        }
    }

    void
    stopThreads()
    {
        {
            std::scoped_lock lock( m_mutex );
            m_cancelThread = true;
            m_changed.notify_all();
        }

        if ( m_blockFinder && m_blockFinder->joinable() ) {
            m_blockFinder->join();
        }
    }

    [[nodiscard]] size_t
    size() const
    {
        return m_blockOffsets.size();
    }

    /** Finalizes and will only keep the first @param blockCount blocks. */
    void
    finalize( std::optional<size_t> blockCount = {} )
    {
        stopThreads();
        m_bitStringFinder = {};
        m_blockOffsets.finalize( blockCount );
    }

    [[nodiscard]] bool
    finalized() const
    {
        return m_blockOffsets.finalized();
    }

    /**
     * This call will track the requested block so that the finder loop will look up to that block.
     * Per default, with the infinite timeout, either a result can be returned or if not it means
     * we are finalized and the requested block is out of range!
     */
    [[nodiscard]] std::optional<size_t>
    get( size_t blockNumber,
         double timeoutInSeconds = std::numeric_limits<double>::infinity() )
    {
        if ( !m_blockOffsets.finalized() ) {
            startThreads();
        }

        {
            std::scoped_lock lock( m_mutex );
            m_highestRequestedBlockNumber = std::max( m_highestRequestedBlockNumber, blockNumber );
            m_changed.notify_all();
        }

        return m_blockOffsets.get( blockNumber, timeoutInSeconds );
    }

    /** @return Index for the block at the requested offset. */
    [[nodiscard]] size_t
    find( size_t encodedBlockOffsetInBits ) const
    {
        std::scoped_lock lock( m_mutex );

        /* m_blockOffsets is effectively double-locked but that's the price of abstraction. */
        const auto lockedOffsets = m_blockOffsets.results();
        const auto& blockOffsets = lockedOffsets.results();

        /* Find in sorted vector by bisection. */
        const auto match = std::lower_bound( blockOffsets.begin(), blockOffsets.end(), encodedBlockOffsetInBits );
        if ( ( match == blockOffsets.end() ) || ( *match != encodedBlockOffsetInBits ) ) {
            throw std::out_of_range( "No block with the specified offset exists in the block map!" );
        }

        return std::distance( blockOffsets.begin(), match );
    }

    void
    setBlockOffsets( BlockOffsets blockOffsets )
    {
        /* First we need to cancel the asynchronous block finder thread. */
        stopThreads();
        m_bitStringFinder = {};

        /* Setting the results also finalizes them. No locking necessary because all threads have shut down. */
        m_blockOffsets.setResults( std::move( blockOffsets ) );
    }

private:
    void
    blockFinderMain()
    {
        while ( !m_cancelThread ) {
            std::unique_lock lock( m_mutex );
            /* m_blockOffsets.size() will only grow, so we don't need to be notified when it changes! */
            m_changed.wait( lock, [this]{
                return m_cancelThread || ( m_blockOffsets.size() <= m_highestRequestedBlockNumber + m_prefetchCount );
            } );
            if ( m_cancelThread ) {
                break;
            }

            /**
             * Assuming a valid BZ2 file, the time for this find method should be bounded and
             * responsive enough when reacting to cancelations.
             * During this compute intensive task, the lock should be unlocked!
             * Or else, the getter and other functions will never be able to acquire this loop
             * until this thread has finished reading the whole file!
             */
            lock.unlock(); // Unlock for a little while so that others can acquire the lock!
            const auto blockOffset = m_bitStringFinder->find();
            if ( blockOffset == std::numeric_limits<size_t>::max() ) {
                break;
            }

            lock.lock();
            m_blockOffsets.push( blockOffset );

        }

        m_blockOffsets.finalize();
    }

private:
    mutable std::mutex m_mutex; /**< Only variables accessed by the asynchronous main loop need to be locked. */
    std::condition_variable m_changed;

    StreamedResults<size_t> m_blockOffsets;

    size_t m_highestRequestedBlockNumber{ 0 };

    /**
     * Only hardware_concurrency slows down decoding! I guess because in the worst case all decoding
     * threads finish at the same time and now the bit string finder would need to find n new blocks
     * in the time it takes to decode one block! In general, the higher this number, the higher the
     * longer will be the initial CPU utilization.
     */
    const size_t m_prefetchCount = 3 * std::thread::hardware_concurrency();

    std::unique_ptr<BitStringFinder> m_bitStringFinder;
    std::atomic<bool> m_cancelThread{ false };

    std::unique_ptr<JoiningThread> m_blockFinder;
};


/**
 * Should get block offsets and decoded sizes and will do conversions between decoded and encoded offsets!
 * The idea is that at first any forward seeking should be done using read calls and the read call will
 * push all block information to the BlockMapBuilder. And because ParallelBZ2Reader should not be called from
 * different threads, there should never be a case in which lookups to this function should have to wait for
 * other threads to push data into us!
 * This is used by the worker threads, so it must be thread-safe!
 */
class BlockMap
{
public:
    struct BlockInfo
    {
    public:
        [[nodiscard]] bool
        contains( size_t dataOffset ) const
        {
            return ( decodedOffsetInBytes <= dataOffset ) && ( dataOffset < decodedOffsetInBytes + decodedSizeInBytes );
        }

    public:
        /**< each BZ2 block in the stream will be given an increasing index number. */
        size_t blockIndex{ 0 };
        size_t encodedOffsetInBits{ 0 };
        size_t encodedSizeInBits{ 0 };
        size_t decodedOffsetInBytes{ 0 };
        size_t decodedSizeInBytes{ 0 };
    };

public:
    BlockMap() = default;

    void
    push( size_t encodedBlockOffset,
          size_t encodedSize,
          size_t decodedSize )
    {
        std::scoped_lock lock( m_mutex );

        if ( m_finalized ) {
            throw std::invalid_argument( "May not insert into finalized block map!" );
        }

        std::optional<size_t> decodedOffset;
        if ( m_blockToDataOffsets.empty() ) {
            decodedOffset = 0;
        } else if ( encodedBlockOffset > m_blockToDataOffsets.back().first ) {
            decodedOffset = m_blockToDataOffsets.back().second + m_lastBlockDecodedSize;
        }

        /* If successive value or empty, then simply append */
        if ( decodedOffset ) {
            m_blockToDataOffsets.emplace_back( encodedBlockOffset, *decodedOffset );
            if ( decodedSize == 0 ) {
                m_eosBlocks.emplace_back( encodedBlockOffset );
            }
            m_lastBlockDecodedSize = decodedSize;
            m_lastBlockEncodedSize = encodedSize;
            return;
        }

        /* Generally, block inserted offsets should always be increasing!
         * But do ignore duplicates after confirming that there is no data inconsistency. */
        const auto match = std::lower_bound(
            m_blockToDataOffsets.begin(), m_blockToDataOffsets.end(), std::make_pair( encodedBlockOffset, 0 ),
            [] ( const auto& a, const auto& b ) { return a.first < b.first; } );

        if ( ( match == m_blockToDataOffsets.end() ) || ( match->first != encodedBlockOffset ) ) {
            throw std::invalid_argument( "Inserted block offsets should be strictly increasing!" );
        }

        if ( std::next( match ) == m_blockToDataOffsets.end() ) {
            throw std::logic_error( "In this case, the new block should already have been appended above!" );
        }

        const auto impliedDecodedSize = std::next( match )->second - match->second;
        if ( impliedDecodedSize != decodedSize ) {
            throw std::invalid_argument( "Got duplicate block offset with inconsistent size!" );
        }

        /* Quietly ignore duplicate insertions. */
    }

    /**
     * Returns the block containing the given data offset. May return a block which does not contain the given
     * offset. In that case it will be the last block.
     */
    [[nodiscard]] BlockInfo
    findDataOffset( size_t dataOffset ) const
    {
        std::scoped_lock lock( m_mutex );

        BlockInfo result;

        /* find offset from map (key and values should be sorted in ascending order, so we can bisect!) */
        const auto blockOffset = std::lower_bound(
            m_blockToDataOffsets.rbegin(), m_blockToDataOffsets.rend(), std::make_pair( 0, dataOffset ),
            [] ( std::pair<size_t, size_t> a, std::pair<size_t, size_t> b ) { return a.second > b.second; } );

        if ( blockOffset == m_blockToDataOffsets.rend() ) {
            return result;
        }

        if ( dataOffset < blockOffset->second ) {
            throw std::logic_error( "Algorithm for finding the block to an offset is faulty!" );
        }

        result.encodedOffsetInBits = blockOffset->first;
        result.decodedOffsetInBytes = blockOffset->second;
        result.blockIndex = std::distance( blockOffset, m_blockToDataOffsets.rend() ) - 1;

        if ( blockOffset == m_blockToDataOffsets.rbegin() ) {
            result.decodedSizeInBytes = m_lastBlockDecodedSize;
            result.encodedSizeInBits = m_lastBlockEncodedSize;
        } else {
            const auto higherBlock = std::prev( /* reverse! */ blockOffset );
            if ( higherBlock->second < blockOffset->second ) {
                std::logic_error( "Data offsets are not monotonically increasing!" );
            }
            result.decodedSizeInBytes = higherBlock->second - blockOffset->second;
            result.encodedSizeInBits = higherBlock->first - blockOffset->first;
        }

        return result;
    }

    /**
     * Returns number of non-EOS blocks. This is necessary to have a number in sync with BlockFinder,
     * which does not find EOS blocks!
     */
    [[nodiscard]] size_t
    dataBlockCount() const
    {
        std::scoped_lock lock( m_mutex );
        return m_blockToDataOffsets.size() - m_eosBlocks.size();
    }

    void
    finalize()
    {
        std::scoped_lock lock( m_mutex );
        m_finalized = true;
    }

    [[nodiscard]] bool
    finalized() const
    {
        std::scoped_lock lock( m_mutex );
        return m_finalized;
    }

    void
    setBlockOffsets( std::map<size_t, size_t> const& blockOffsets )
    {
        std::scoped_lock lock( m_mutex );

        m_blockToDataOffsets.assign( blockOffsets.begin(), blockOffsets.end() );
        m_lastBlockEncodedSize = 0;
        m_lastBlockDecodedSize = 0;

        /* Find EOS blocks in map. */
        m_eosBlocks.clear();
        for ( auto it = m_blockToDataOffsets.begin(), nit = std::next( m_blockToDataOffsets.begin() );
              nit != m_blockToDataOffsets.end(); ++it, ++nit )
        {
            /* Only push blocks with no data, i.e., EOS blocks. */
            if ( it->second == nit->second ) {
                m_eosBlocks.push_back( it->first );
            }
        }
        /* Last block is assumed to be EOS. */
        m_eosBlocks.push_back( m_blockToDataOffsets.back().first );

        m_finalized = true;
    }

    [[nodiscard]] std::map<size_t, size_t>
    blockOffsets() const
    {
        std::scoped_lock lock( m_mutex );

        return std::map<size_t, size_t>( m_blockToDataOffsets.begin(), m_blockToDataOffsets.end() );
    }

    [[nodiscard]] std::pair<size_t, size_t>
    back() const
    {
        std::scoped_lock lock( m_mutex );

        if ( m_blockToDataOffsets.empty() ) {
            throw std::out_of_range( "Can not return last element of empty block map!" );
        }
        return m_blockToDataOffsets.back();
    }

private:
    mutable std::mutex m_mutex;

    /** If complete, the last block will be of size 0 and indicate the end of stream! */
    std::vector< std::pair<size_t, size_t> > m_blockToDataOffsets;
    std::vector<size_t> m_eosBlocks;
    bool m_finalized{ false };

    size_t m_lastBlockEncodedSize{ 0 }; /**< Encoded block size of m_blockToDataOffsets.rbegin() */
    size_t m_lastBlockDecodedSize{ 0 }; /**< Decoded block size of m_blockToDataOffsets.rbegin() */
};



/**
 * Manages block data access. Calls to members are not thread-safe!
 * Requested blocks are cached and accesses may trigger prefetches,
 * which will be fetched in parallel using a thread pool.
 */
template<typename FetchingStrategy = FetchingStrategy::FetchNextSmart>
class BlockFetcher
{
public:
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
            << "\n   Cache hits                 :" << m_cache.hits()
            << "\n   misses                     :" << m_cache.misses()
            << "\n   prefetched blocks          :" << m_prefetchCount
            << "\n   direct prefetch queue hits :" << m_prefetchDirectHits
            << "\n   hit rate                   :" << cacheHitRate
            << "\n   time spent in:"
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


/**
 * @note Calls to this class are not thread-safe! Even though they use threads to evaluate them in parallel.
 */
class ParallelBZ2Reader final :
    public BZ2ReaderInterface
{
public:
    using BlockFetcher = ::BlockFetcher<FetchingStrategy::FetchNextSmart>;

public:
    /* Constructors */

    explicit
    ParallelBZ2Reader( int    fileDescriptor,
                       size_t parallelization = 0 ) :
        m_bitReader( new StandardFileReader( fileDescriptor ) ),
        m_fetcherParallelization( parallelization == 0
                                  ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
                                  : parallelization ),
        m_startBlockFinder( [&](){ return std::make_shared<BlockFinder>( m_bitReader.cloneSharedFileReader(),
                                                                         m_finderParallelization ); } )
    {
        if ( !m_bitReader.seekable() ) {
            throw std::invalid_argument( "Parallel BZ2 Reader will not work on non-seekable input like stdin (yet)!" );
        }
    }

    explicit
    ParallelBZ2Reader( const std::string& filePath,
                       size_t             parallelization = 0 ) :
        m_bitReader( new StandardFileReader( filePath ) ),
        m_fetcherParallelization( parallelization == 0
                                  ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
                                  : parallelization ),
        m_startBlockFinder( [&](){ return std::make_shared<BlockFinder>( m_bitReader.cloneSharedFileReader(),
                                                                         m_finderParallelization ); } )
    {}


#ifdef WITH_PYTHON_SUPPORT
    explicit
    ParallelBZ2Reader( PyObject* pythonObject,
                       size_t    parallelization = 0 ) :
        m_bitReader( new PythonFileReader( pythonObject ) ),
        m_fetcherParallelization( parallelization == 0
                                  ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
                                  : parallelization ),
        m_startBlockFinder( [&](){ return std::make_shared<BlockFinder>( m_bitReader.cloneSharedFileReader(),
                                                                         m_finderParallelization ); } )
    {}
#endif

    /* FileReader overrides */

    [[nodiscard]] FileReader*
    clone() const override
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] int
    fileno() const override
    {
        return m_bitReader.fileno();
    }

    [[nodiscard]] bool
    seekable() const override
    {
        return m_bitReader.seekable();
    }

    void
    close() override
    {
        m_blockFetcher = {};
        m_blockFinder = {};
        m_bitReader.close();
    }

    [[nodiscard]] bool
    closed() const override
    {
        return m_bitReader.closed();
    }

    [[nodiscard]] bool
    eof() const override
    {
        return m_atEndOfFile;
    }

    [[nodiscard]] bool
    fail() const override
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] size_t
    tell() const override
    {
        if ( m_atEndOfFile ) {
            return size();
        }
        return m_currentPosition;
    }

    [[nodiscard]] size_t
    size() const override
    {
        if ( !m_blockMap->finalized() ) {
            throw std::invalid_argument( "Can't get stream size in BZ2 when not finished reading at least once!" );
        }
        return m_blockMap->back().second;
    }

    void
    clearerr() override
    {
        m_bitReader.clearerr();
        m_atEndOfFile = false;
        throw std::invalid_argument( "Not fully tested!" );
    }

    /* BZ2ReaderInterface overrides */

    size_t
    read( const int    outputFileDescriptor = -1,
          char* const  outputBuffer = nullptr,
          const size_t nBytesToRead = std::numeric_limits<size_t>::max() ) override
    {
        if ( closed() ) {
            throw std::invalid_argument( "You may not call read on closed ParallelBZ2Reader!" );
        }

        if ( eof() || ( nBytesToRead == 0 ) ) {
            return 0;
        }

        size_t nBytesDecoded = 0;
        while ( ( nBytesDecoded < nBytesToRead ) && !eof() ) {
            std::shared_ptr<BlockFetcher::BlockData> blockData;

            auto blockInfo = m_blockMap->findDataOffset( m_currentPosition );
            if ( !blockInfo.contains( m_currentPosition ) ) {
                /* Fetch new block for the first time and add information to block map. */
                const auto dataBlockIndex = m_blockMap->dataBlockCount();
                const auto encodedOffsetInBits = blockFinder().get( dataBlockIndex );
                if ( !encodedOffsetInBits ) {
                    m_blockMap->finalize();
                    m_atEndOfFile = true;
                    break;
                }

                blockData = blockFetcher().get( *encodedOffsetInBits, dataBlockIndex );
                m_blockMap->push( blockData->encodedOffsetInBits,
                                  blockData->encodedSizeInBits,
                                  blockData->data.size() );

                /* Check whether the next block is an EOS block, which has a different magic byte string
                 * and therefore will not be found by the block finder! Such a block will span 48 + 32 + (0..7) bits.
                 * However, the last 0 to 7 bits are only padding and not needed! */
                if ( !blockData->isEndOfFile ) {
                    const auto nextBlockHeaderData = blockFetcher().readBlockHeader( blockData->encodedOffsetInBits +
                                                                                     blockData->encodedSizeInBits );
                    if ( nextBlockHeaderData.isEndOfStreamBlock ) {
                        m_blockMap->push( nextBlockHeaderData.encodedOffsetInBits,
                                          nextBlockHeaderData.encodedSizeInBits,
                                          0 );

                        const auto nextStreamOffsetInBits = nextBlockHeaderData.encodedOffsetInBits +
                                                            nextBlockHeaderData.encodedSizeInBits;
                        if ( nextStreamOffsetInBits < m_bitReader.size() ) {
                            try
                            {
                                BitReader nextBzip2StreamBitReader( m_bitReader );
                                nextBzip2StreamBitReader.seek( nextStreamOffsetInBits );
                                bzip2::readBzip2Header( nextBzip2StreamBitReader );
                            }
                            catch ( const std::domain_error& )
                            {
                                std::cerr << "[Warning] Trailing garbage after EOF ignored!\n";
                                /**
                                 * Stop reading the file here! 'bzip2-tests' comes with a test in which there actually
                                 * comes further valid bzip2 data after a gap. But that data, which the block finder will
                                 * find without problems, should not be read anymore!
                                 * The block finder already might have prefetched further values, so we need to truncate it!
                                 * @todo Maybe add an --ignore-invalid option like with tarfile's --ignore-zeros.
                                 */
                                m_blockFinder->finalize( m_blockMap->dataBlockCount() );
                            }
                        }
                    }
                }

                /* We could also directly continue but then the block would be refetched, and fetching quite complex. */
                blockInfo = m_blockMap->findDataOffset( m_currentPosition );
                if ( !blockInfo.contains( m_currentPosition ) ) {
                    continue;
                }
            } else {
                blockData = blockFetcher().get( blockInfo.encodedOffsetInBits );
            }

            /* Copy data from fetched block to output. */

            const auto offsetInBlock = m_currentPosition - blockInfo.decodedOffsetInBytes;

            if ( offsetInBlock >= blockData->data.size() ) {
                throw std::logic_error( "Block does not contain the requested offset even though it "
                                        "shouldn't be according to block map!" );
            }

            const auto nBytesToDecode = std::min( blockData->data.size() - offsetInBlock,
                                                  nBytesToRead - nBytesDecoded );
            const auto nBytesWritten = writeResult(
                outputFileDescriptor,
                outputBuffer == nullptr ? nullptr : outputBuffer + nBytesDecoded,
                reinterpret_cast<const char*>( blockData->data.data() + offsetInBlock ),
                nBytesToDecode
            );

            if ( nBytesWritten != nBytesToDecode ) {
                std::stringstream msg;
                msg << "Less (" << nBytesWritten << ") than the requested number of bytes (" << nBytesToDecode
                    << ") were written to the output!";
                throw std::logic_error( msg.str() );
            }

            nBytesDecoded += nBytesToDecode;
            m_currentPosition += nBytesToDecode;
        }

        return nBytesDecoded;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        if ( closed() ) {
            throw std::invalid_argument( "You may not call seek on closed ParallelBZ2Reader!" );
        }

        switch ( origin )
        {
        case SEEK_CUR:
            offset = tell() + offset;
            break;
        case SEEK_SET:
            break;
        case SEEK_END:
            /* size() requires the block offsets to be available! */
            if ( !m_blockMap->finalized() ) {
                read();
            }
            offset = size() + offset;
            break;
        }

        const auto positiveOffset = static_cast<size_t>( std::max<decltype( offset )>( 0, offset ) );

        if ( positiveOffset == tell() ) {
            return positiveOffset;
        }

        /* Backward seeking is no problem at all! 'tell' may only return <= size()
         * as value meaning we are now < size() and therefore EOF can be cleared! */
        if ( positiveOffset < tell() ) {
            m_atEndOfFile = false;
            m_currentPosition = positiveOffset;
            return positiveOffset;
        }

        /* m_blockMap is only accessed by read and seek, which are not to be called from different threads,
         * so we do not have to lock it. */
        const auto blockInfo = m_blockMap->findDataOffset( positiveOffset );
        if ( positiveOffset < blockInfo.decodedOffsetInBytes ) {
            throw std::logic_error( "Block map returned unwanted block!" );
        }

        if ( blockInfo.contains( positiveOffset ) ) {
            m_atEndOfFile = false;
            m_currentPosition = positiveOffset;
            return tell();
        }

        assert( positiveOffset - blockInfo.decodedOffsetInBytes > blockInfo.decodedSizeInBytes );
        if ( m_blockMap->finalized() ) {
            m_atEndOfFile = true;
            m_currentPosition = size();
            return tell();
        }

        /* Jump to furthest known point as performance optimization. Note that even if that is right after
         * the last byte, i.e., offset == size(), then no eofbit is set even in ifstream! In ifstream you
         * can even seek to after the file end with no fail bits being set in my tests! */
        m_atEndOfFile = false;
        m_currentPosition = blockInfo.decodedOffsetInBytes + blockInfo.decodedSizeInBytes;
        read( -1, nullptr, positiveOffset - tell() );
        return tell();
    }

    /* BZip2 specific methods */

    [[nodiscard]] bool
    blockOffsetsComplete() const override
    {
        return m_blockMap->finalized();
    }

    /**
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    [[nodiscard]] std::map<size_t, size_t>
    blockOffsets() override
    {
        if ( !m_blockMap->finalized() ) {
            read();
            if ( !m_blockMap->finalized() || !blockFinder().finalized() ) {
                throw std::logic_error( "Reading everything should have finalized the block map!" );
            }
        }

        return m_blockMap->blockOffsets();
    }

    /**
     * Same as @ref blockOffsets but it won't force calculation of all blocks and simply returns
     * what is availabe at call time.
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    [[nodiscard]] std::map<size_t, size_t>
    availableBlockOffsets() const override
    {
        return m_blockMap->blockOffsets();
    }

    void
    setBlockOffsets( std::map<size_t, size_t> offsets ) override
    {
        if ( offsets.empty() ) {
            throw std::invalid_argument( "May not clear offsets. Construct a new ParallelBZ2Reader instead!" );
        }

        setBlockFinderOffsets( offsets );

        if ( offsets.size() < 2 ) {
            throw std::invalid_argument( "Block offset map must contain at least one valid block and one EOS block!" );
        }
        m_blockMap->setBlockOffsets( std::move( offsets ) );
    }

    /**
     * @return number of processed bits of compressed bzip2 input file stream
     * @note Bzip2 is block based and blocks are currently read fully, meaning that the granularity
     *       of the returned position is ~100-900kB. It's only useful for a rough estimate.
     */
    [[nodiscard]] size_t
    tellCompressed() const override
    {

        const auto blockInfo = m_blockMap->findDataOffset( m_currentPosition );
        if ( blockInfo.contains( m_currentPosition ) ) {
            return blockInfo.encodedOffsetInBits;
        }
        return 0;
    }


    /**
     * Closes all threads and saves the work. They will be restarted when needed again, e.g., on seek or read.
     * This is intended for use with fusepy. You can start a ParallelBZ2Reader use it to create the block map
     * and print out user output and then you join all threads before FUSE forks the process. FUSE requires
     * threads to be created after it forks, it seems:
     * @see https://github.com/libfuse/libfuse/wiki/FAQ#how-should-threads-be-started
     * Personally, the only problem I observed was background process not finishing even after unmounting,
     * however, contrary to the FAQ it seems that threads were not joined because the file system seemed to work.
     */
    void
    joinThreads()
    {
        m_blockFetcher = {};
        m_blockFinder = {};
    }

private:
    BlockFinder&
    blockFinder()
    {
        if ( m_blockFinder ) {
            return *m_blockFinder;
        }

        if ( !m_startBlockFinder ) {
            throw std::logic_error( "Block finder creator was not initialized correctly!" );
        }

        m_blockFinder = m_startBlockFinder();
        if ( !m_blockFinder ) {
            throw std::logic_error( "Block finder creator failed to create new block finder!" );
        }

        if ( m_blockMap->finalized() ) {
            setBlockFinderOffsets( m_blockMap->blockOffsets() );
        }

        return *m_blockFinder;
    }


    BlockFetcher&
    blockFetcher()
    {
        if ( m_blockFetcher ) {
            return *m_blockFetcher;
        }

        /* As a side effect, blockFinder() creates m_blockFinder if not already initialized! */
        if ( !blockFinder().finalized() ) {
            blockFinder().startThreads();
        }

        m_blockFetcher = std::make_unique<BlockFetcher>( m_bitReader, m_blockFinder, m_fetcherParallelization );

        if ( !m_blockFetcher ) {
            throw std::logic_error( "Block fetcher should have been initialized!" );
        }

        return *m_blockFetcher;
    }


    void
    setBlockFinderOffsets( const std::map<size_t, size_t>& offsets )
    {
        if ( offsets.empty() ) {
            throw std::invalid_argument( "A non-empty list of block offsets is required!" );
        }

        BlockFinder::BlockOffsets encodedBlockOffsets;
        for ( auto it = offsets.begin(), nit = std::next( offsets.begin() ); nit != offsets.end(); ++it, ++nit )
        {
            /* Ignore blocks with no data, i.e., EOS blocks. */
            if ( it->second != nit->second ) {
                encodedBlockOffsets.push_back( it->first );
            }
        }
        /* The last block is not pushed because "std::next( it )" is end but last block must be EOS anyways
         * or else BlockMap will not work correctly because the implied size of that last block is 0! */

        blockFinder().setBlockOffsets( std::move( encodedBlockOffsets ) );
    }


    size_t
    writeResult( int         const outputFileDescriptor,
                 char*       const outputBuffer,
                 char const* const dataToWrite,
                 size_t      const dataToWriteSize )
    {
        size_t nBytesFlushed = dataToWriteSize; // default then there is neither output buffer nor file device given

        if ( outputFileDescriptor >= 0 ) {
            const auto nBytesWritten = write( outputFileDescriptor, dataToWrite, dataToWriteSize );
            nBytesFlushed = std::max<decltype( nBytesWritten )>( 0, nBytesWritten );
        }

        if ( outputBuffer != nullptr ) {
            std::memcpy( outputBuffer, dataToWrite, nBytesFlushed );
        }

        return nBytesFlushed;
    }

private:
    BitReader m_bitReader;

    size_t m_currentPosition = 0; /**< the current position as can only be modified with read or seek calls. */
    bool m_atEndOfFile = false;

private:
    size_t const m_fetcherParallelization;
    /** The block finder is much faster than the fetcher and therefore does not require es much parallelization! */
    size_t const m_finderParallelization{ ceilDiv( m_fetcherParallelization, 8U ) };

    std::function<std::shared_ptr<BlockFinder>(void)> const m_startBlockFinder;

    /* These are the three larger "sub modules" of ParallelBZ2Reader */

    /** Necessary for prefetching decoded blocks in parallel. */
    std::shared_ptr<BlockFinder>    m_blockFinder;
    std::unique_ptr<BlockMap> const m_blockMap{ std::make_unique<BlockMap>() };
    std::unique_ptr<BlockFetcher>   m_blockFetcher;
};
