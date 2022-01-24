#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include <FileReader.hpp>
#include <JoiningThread.hpp>
#include <ParallelBitStringFinder.hpp>

#include "bzip2.hpp"
#include "StreamedResults.hpp"


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
