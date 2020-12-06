#pragma once

#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <unistd.h>         // dup, fileno

#include "BitReader.hpp"
#include "BitStringFinder.hpp"
#include "common.hpp"
#include "ThreadPool.hpp"


/**
 * No matter the input, the data is read from an input buffer.
 * If a file is given, then that input buffer will be refilled when the input buffer empties.
 * It is less a file object and acts more like an iterator.
 * It offers a @ref find method returning the next match or std::numeric_limits<size_t>::max() if the end was reached.
 */
template<uint8_t bitStringSize>
class ParallelBitStringFinder :
    public BitStringFinder<bitStringSize>
{
public:
    using BaseType = BitStringFinder<bitStringSize>;

public:
    ParallelBitStringFinder( std::string filePath,
                             uint64_t    bitStringToFind,
                             size_t      parallelisation = std::max( 1U, std::thread::hardware_concurrency() / 8U ),
                             size_t      requestedBytes = 0,
                             size_t      fileBufferSizeBytes = 1*1024*1024 ) :
        BaseType( bitStringToFind, chunkSize( fileBufferSizeBytes, requestedBytes, parallelisation ) ),
        m_threadPool( parallelisation )
    {
        this->m_file = std::fopen( filePath.c_str(), "rb" );
        if ( BaseType::seekable() ) {
            fseek( this->m_file, 0, SEEK_SET );
        }
    }

    ParallelBitStringFinder( int      fileDescriptor,
                             uint64_t bitStringToFind,
                             size_t   parallelisation = std::max( 1U, std::thread::hardware_concurrency() / 8U ),
                             size_t   requestedBytes = 0,
                             size_t   fileBufferSizeBytes = 1*1024*1024 ) :
        BaseType( bitStringToFind, chunkSize( fileBufferSizeBytes, requestedBytes, parallelisation ) ),
        m_threadPool( parallelisation )
    {
        this->m_file = fdopen( dup( fileDescriptor ), "rb" );
        if ( BaseType::seekable() ) {
            fseek( this->m_file, 0, SEEK_SET );
        }
    }

    ParallelBitStringFinder( const char* buffer,
                             size_t      size,
                             uint64_t    bitStringToFind,
                             size_t      requestedBytes = 0 ) :
        BaseType( bitStringToFind )
    {
        this->m_buffer.assign( buffer, buffer + size );
    }

    /**
     * @return the next match and the requested bytes or nullopt if at end of file.
     */
    //std::optional<std::pair<size_t, BitReader> >
    size_t
    find();

private:
    /**
     * The worker pushes found offsets during which it locks the mutex and the reader locks and pops from the
     * queue. After the worker has finished, which can be queried with the future, and when the found offsets
     * have all been read, this struct can be deleted or reused. The worker thread sets finished and notifies
     * with the condition variable. It also notifies when pushing to the queue.
     */
    struct ThreadResults
    {
        std::queue<size_t>      foundOffsets;
        std::mutex              mutex;
        std::future<void>       future;
        std::condition_variable changed;
    };

private:
    [[nodiscard]] static constexpr size_t
    chunkSize( size_t const fileBufferSizeBytes,
               size_t const requestedBytes,
               size_t const parallelisation )
    {
        /* This implementation has the limitation that it might at worst try to read as many as bitStringSize
         * bits from the buffered chunk. It makes no sense to remove this limitation. It might slow things down. */
        const auto result = std::max( fileBufferSizeBytes,
                                      static_cast<size_t>( ceilDiv( bitStringSize, 8 ) ) * parallelisation );
        /* With the current implementation it is impossible to have a chunk size smaller than the requested bytes
         * and have it work for non-seekable inputs. In the worst case, the bit string is at the end, so we have to
         * read almost everything of the next chunk. */
        return std::max( result, requestedBytes );
    }

    /**
     * The findBitString function only returns the first result, so this worker main basically just calls it in a
     * a loop with increasing start offsets. It also handles all the parallel synchronization stuff like sending
     * the results to the reading thread through the result buffer.
     * When it is finished, it will return std::numeric_limits<size_t>::max() to signal that the future can be waited
     * for.
     *
     * @param buffer pointer data to look for bitstrings. [buffer, buffer+size) will be searched.
     * @param firstBitsToIgnore This will effectively force matches to only be returned if
     *                          foundOffset >= firstBitsToIgnore
     * @param bitOffsetToAdd All found offsets relative to @p buffer should add this in order to return the global
     *                       bit offsets of interest.
     */
    static void
    workerMain( const char*    const buffer,
                size_t         const size,
                uint8_t        const firstBitsToIgnore,
                uint64_t       const bitStringToFind,
                size_t         const bitOffsetToAdd,
                ThreadResults* const result )
    {
        for ( size_t bufferBitsRead = firstBitsToIgnore; bufferBitsRead < size * CHAR_BIT; ) {
            const auto byteOffset = bufferBitsRead / CHAR_BIT;
            const auto bitOffset  = bufferBitsRead % CHAR_BIT;

            const auto relpos = BaseType::findBitString( reinterpret_cast<const uint8_t*>( buffer ) + byteOffset,
                                                         size - byteOffset, bitStringToFind, bitOffset );
            if ( relpos == std::numeric_limits<size_t>::max() ) {
                break;
            }

            bufferBitsRead += relpos;

            {
                std::lock_guard<std::mutex> lock( result->mutex );
                result->foundOffsets.push( bitOffsetToAdd + bufferBitsRead );
                result->changed.notify_one();
            }
            bufferBitsRead += 1;
        }

        std::lock_guard<std::mutex> lock( result->mutex );
        result->foundOffsets.push( std::numeric_limits<size_t>::max() );
        result->changed.notify_one();
    }

private:
    /** Return at least this amount of bytes after and including the found bit strings. */
    const size_t m_requestedBytes = 0;

    ThreadPool m_threadPool;

    std::list<ThreadResults> m_threadResults;
};


/**
 * Idea:
 *   1. Load one chunk if first iteration
 *   2. Use the serial BitStringFinder in parallel on equal-sized sized sub chunks.
 *   3. Filter out results we already could have found in the chunk before if more than bitStringSize-1
 *      bits were loaded from it.
 *   4. Translate the returned bit offsets of the BitStringFinders to global offsets.
 *   5. Copy requested bytes after match into result buffer.
 *   6. Load the next chunk plus at least the last bitStringSize-1 bits from the chunk before.
 *   7. Use that new chunk to append more of the requested bytes after matches to the result buffer.
 *      More than one chunk should not be necessary for this! This is ensured in the chunkSize method.
 *
 * @return the next match and the requested bytes or nullopt if at end of file.
 */
template<uint8_t bitStringSize>
//std::optional<std::pair<size_t, BitReader> >
size_t
ParallelBitStringFinder<bitStringSize>::find()
{
    if ( bitStringSize == 0 ) {
        return std::numeric_limits<size_t>::max();
    }

    while ( !BaseType::eof() )
    {
        /* Check whether there are results available and return those. Take care to return results in order! */
        for ( auto& result : m_threadResults ) {
            /* Check if some results are already calculated. No locking necessary between the queue empty check
             * and the future valid check because only we can make it invalid when calling get on it. */
            std::unique_lock<std::mutex> lock( result.mutex );
            while( !result.foundOffsets.empty() || result.future.valid() ) {
                /* In the easiest case we have something to return already. */
                if ( !result.foundOffsets.empty() ) {
                    if ( result.foundOffsets.front() == std::numeric_limits<size_t>::max() ) {
                        result.foundOffsets.pop();
                        if ( result.future.valid() ) {
                            result.future.get();
                        }
                        break;
                    }
                    const auto foundOffset = result.foundOffsets.front();
                    result.foundOffsets.pop();
                    return foundOffset;
                }

                /* Wait for thread to finish or push new results. Note that this may hang if the worker thread
                 * crashes because the predicate check is only done on condition variable notifies and on spurious
                 * wakeups but those are not guaranteed. */
                result.changed.wait( lock, [&result] () {
                    return !result.foundOffsets.empty() ||
                           ( result.future.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready );
                } );

                if ( result.future.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready ) {
                    result.future.get();
                }
            }
        }

        /* At this point all previous worker threads working on m_threadResults are finished! */
        if ( std::any_of( m_threadResults.begin(), m_threadResults.end(),
                          [] ( const auto& x ) { return x.future.valid() || !x.foundOffsets.empty(); } ) ) {
            throw std::logic_error( "All worker threads should have finished and no results should be left over!" );
        }
        m_threadResults.clear();

        /* Check whether we have read everything in the current buffer. */
        if ( this->m_bufferBitsRead >= this->m_buffer.size() * CHAR_BIT ) {
            //std::cerr << "refill buffer\n";
            const auto nBytesRead = BaseType::refillBuffer();
            if ( nBytesRead == 0 ) {
                return std::numeric_limits<size_t>::max();
            }
        }

        /* Start worker threads using the thread pool and the current buffer. */
        //std::cerr << "Start new workers\n";
        const auto nSubdivisions = m_threadPool.size();
        const auto subdivisionSize = ceilDiv( this->m_buffer.size(), nSubdivisions ) + this->m_movingBytesToKeep;
        assert( subdivisionSize > this->m_movingBytesToKeep );
        for ( size_t i = 0; i < nSubdivisions; ++i ) {
            auto& result = m_threadResults.emplace_back();

            const auto byteOffset        = this->m_bufferBitsRead / CHAR_BIT;
            const auto firstBitsToIgnore = this->m_bufferBitsRead % CHAR_BIT;

            const auto* buffer = this->m_buffer.data() + byteOffset;
            assert( byteOffset < this->m_buffer.size() );
            const auto size = std::min( subdivisionSize, this->m_buffer.size() - byteOffset );

            //std::cerr << " Find from offset " << byteOffset << "B " << firstBitsToIgnore << "b size " << size << "\n";
            result.future = m_threadPool.submitTask( [=, &result] () {
                workerMain(
                    buffer,
                    size,
                    firstBitsToIgnore,
                    this->m_bitStringToFind,
                    ( this->m_nTotalBytesRead + byteOffset ) * CHAR_BIT,
                    &result
                );
            } );

            this->m_bufferBitsRead += subdivisionSize * CHAR_BIT - this->m_movingBitsToKeep;
        }
    }

    return std::numeric_limits<size_t>::max();
}
