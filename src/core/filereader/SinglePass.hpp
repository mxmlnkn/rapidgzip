#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <common.hpp>
#include <FasterVector.hpp>

#include "FileReader.hpp"


/**
 * This FileReader implementation acts like a buffered file reader with infinite buffer.
 * This file reader will only read sequentially from the given input file reader.
 * It buffers all the sequentially read data and enables seeking inside that buffer.
 * It can therefore be used to make non-seekable file readers seekable.
 *
 * In order to reduce memory consumption it also offers an interface to unload everything
 * before a given offset. Seeking back before that offset will throw an exception!
 *
 * This class is not thread-safe. It should be wrapped inside SharedFileReader to make it so.
 */
class SinglePassFileReader :
    public FileReader
{
public:
    static constexpr size_t CHUNK_SIZE = 4_Mi;

private:
    /**
     * m rapidgzip && src/tools/rapidgzip -P 24 -d -o /dev/null <( cat 4GiB-base64.gz )
     * @verbatim
     * input  m_maxReusableChunkCount  Chunk           Bandwidth        Non-reused chunks
     *  cat         0                  FasterVector    1125 1133 1139   778
     * fcat         0                  FasterVector    1505 1578 1566   778
     *  cat         0                  std::vector     1385 1384 1374   778
     * fcat         0                  std::vector     1987 1994 1990   778
     *
     *  cat         1                  FasterVector    1329 1345 1346   461 451 453
     * fcat         1                  FasterVector    2019 1996 1993   504 476 484
     *  cat         1                  std::vector     1491 1466 1458   470 463 455
     * fcat         1                  std::vector     2155 2208 2269   495 500 484
     *
     *  cat         2                  FasterVector    1424 1396 1425
     * fcat         2                  FasterVector    2021 2119 2055   355 342 358
     *  cat         2                  std::vector     1511 1512 1486
     * fcat         2                  std::vector     2250 2311 2199
     *
     *  cat         8                  FasterVector    1619 1583 1626
     * fcat         8                  FasterVector    2585 2505 2484    83  96 106
     *  cat         8                  std::vector     1598 1574 1574
     * fcat         8                  std::vector     2361 2329 2354
     *
     * fcat        12                  FasterVector    2617 2605 2601    37  47  55
     * fcat        16                  FasterVector    2616 2660 2645    28  27  28
     * fcat        24                  FasterVector    2628 2682 2708    16  20  17
     * fcat        32                  FasterVector    2623 2635 2619    11  11  13
     * fcat        40                  FasterVector    2661 2554 2667     0   0   1
     * fcat        47                  FasterVector    2686 2623 2615     0   0   0
     * fcat        48                  FasterVector    2628 2686 2623     0   0   0
     *
     *  cat        64                  FasterVector    1702 1687 1660     0
     * fcat        64                  FasterVector    2687 2647 2618     0
     *  cat        64                  std::vector     1580 1567 1567     0
     * fcat        64                  std::vector     2344 2275 2314     0
     * @endverbatim
     *
     * Observations:
     * - std::vector is up to 20% faster than rpmalloc when there is none or few chunks being reused
     * - rpmalloc is 10% faster than std::vector if all chunks are reused. I think one missing advantage
     *   for rpmalloc here is that allocations are serialized anyway, even if they are done from different threads.
     * - Up to 40 chunks were observed to be unused at the same time. The maximum limit is probably higher.
     *   - The worst case should be when everything has been prefetched at the same time, as is the case at the start.
     *     Then, all chunks are processed / written out and to be released before the next prefetches allocate new
     *     new buffers.
     *   - Beware about the difference between SinglePassFileReader chunks and ParallelGzipReader chunks.
     *     - In the above benchmarks, both were set to 4 MiB.
     *     - The ParallelGzipReader chunks are only tentative and depending on the deflate block size, they could
     *       become arbitrarily large in the pathological case. The pathological case is bounded and will throw
     *       an exception though.
     *     - Normally, the chunks read up to 128 KiB after the tentative chunk end. However, the BitReader also has
     *       a buffer size (128 KiB), which needs to be added to that for the worst case.
     *   - BlockFetcher caches up to max(16, P) chunks and prefetches up to 2 * P chunks additionally.
     *     Note that for simple sequential decompression via rapidgzip, the cache should always be sized 1.
     *   - The main thread only releases up to the beginning of the last used chunk in order to enable full cache
     *     clearing and continuing, which would trigger the last used chunk to be computed anew. This basically
     *     deducts one from the possibly cached chunk count, which balances out with the one chunk added because
     *     of reading over the end and caching inside the BitReader.
     *   -> The worst case approximately would result in:
     *          ceil[ ( 2 * P + 1 ) * ParallelGzipReader::m_chunkSizeInBytes / SinglePassFileReader::CHUNK_SIZE ]
     *      chunks being released and not yet reused.
     * - The performance benefits of chunk reuse is already saturated quite a lot below the limit for which there
     *   is no unnecessary allocation at all.
     */
    using Chunk = FasterVector<std::byte>;

public:
    explicit
    SinglePassFileReader( UniqueFileReader fileReader ) :
        m_file( std::move( fileReader ) )
    {}

    ~SinglePassFileReader()
    {
        close();
    }

    [[nodiscard]] UniqueFileReader
    clone() const override
    {
        throw std::invalid_argument( "Cloning file reader not allowed because the internal file position "
                                     "should not be modified by multiple owners!" );
    }

    /* Copying is simply not allowed because that might interfere with the file position state, use SharedFileReader! */

    void
    close() override
    {
        if ( m_file ) {
            m_file->close();
        }
    }

    [[nodiscard]] bool
    closed() const override
    {
        return !m_file || m_file->closed();
    }

    [[nodiscard]] bool
    eof() const override
    {
        return m_underlyingFileEOF && ( m_currentPosition >= m_numberOfBytesRead );
    }

    [[nodiscard]] bool
    fail() const override
    {
        /* We are a buffer. We do not have a fail state. We do not want to lock in order to query @ref m_file. */
        return false;
    }

    [[nodiscard]] int
    fileno() const override
    {
        if ( m_file ) {
            return m_file->fileno();
        }
        throw std::invalid_argument( "Trying to get fileno of an invalid file!" );
    }

    [[nodiscard]] bool
    seekable() const override
    {
        return true;
    }

    [[nodiscard]] size_t
    read( char*  buffer,
          size_t nMaxBytesToRead ) override
    {
        if ( !m_file ) {
            throw std::invalid_argument( "Invalid or file cannot be seeked!" );
        }

        if ( nMaxBytesToRead == 0 ) {
            return 0;
        }

        bufferUpTo( saturatingAddition( m_currentPosition, nMaxBytesToRead ) );
        const auto startChunk = getChunkIndex( m_currentPosition );

        size_t nBytesRead{ 0 };
        for ( size_t i = startChunk; ( i < m_buffer.size() ) && ( nBytesRead < nMaxBytesToRead ); ++i ) {
            const auto chunkOffset = i * CHUNK_SIZE;
            const auto& chunk = getChunk( i );
            const auto* sourceOffset = chunk.data();
            auto nAvailableBytes = chunk.size();

            if ( chunkOffset < m_currentPosition ) {
                if ( m_currentPosition - chunkOffset > nAvailableBytes ) {
                    throw std::logic_error( "Calculation of start chunk seems to be wrong!" );
                }

                const auto nBytesToSkip = m_currentPosition - chunkOffset;
                nAvailableBytes -= nBytesToSkip;
                sourceOffset += nBytesToSkip;
            }

            const auto nBytesToCopy = std::min( nAvailableBytes, nMaxBytesToRead - nBytesRead );
            if ( buffer != nullptr ) {
                std::memcpy( buffer + nBytesRead, sourceOffset, nBytesToCopy );
            }
            nBytesRead += nBytesToCopy;
        }

        m_currentPosition += nBytesRead;

        return nBytesRead;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        if ( !m_file ) {
            throw std::invalid_argument( "Invalid or file cannot be seeked!" );
        }

        switch ( origin )
        {
        case SEEK_CUR:
            offset = static_cast<long long int>( tell() ) + offset;
            break;
        case SEEK_SET:
            break;
        case SEEK_END:
            bufferUpTo( std::numeric_limits<size_t>::max() );
            offset += static_cast<long long int>( m_numberOfBytesRead );
            break;
        }

        bufferUpTo( offset );
        m_currentPosition = static_cast<size_t>( std::max( 0LL, offset ) );

        return m_currentPosition;
    }

    [[nodiscard]] std::optional<size_t>
    size() const override
    {
        if ( m_underlyingFileEOF ) {
            return m_numberOfBytesRead;
        }
        return std::nullopt;
    }

    [[nodiscard]] size_t
    tell() const override
    {
        return m_currentPosition;
    }

    void
    clearerr() override
    {
        /* No need to do anything. We do not enter error states because everything is buffered and EOF
         * is automatically derived from the current position and does not need to be cleared. */
    }

    void
    releaseUpTo( const size_t untilOffset )
    {
        /* Always leave the last chunk as is. Else, interleaved calls to bufferUpTo und releaseUpTo might
         * read the whole file while only ever leaving m_chunk.size() == 1, meaning the assumption that
         * offset = chunk index * CHUNK_SIZE will not hold! */
        if ( m_buffer.size() <= 1 ) {
            return;
        }

        const auto lastChunkToRelease = std::min( untilOffset / CHUNK_SIZE, m_buffer.size() - 2 );
        for ( auto i = m_releasedChunkCount; i <= lastChunkToRelease; ++i ) {
            if ( m_reusableChunks.size() < m_maxReusableChunkCount ) {
                std::swap( m_buffer[i], m_reusableChunks.emplace_back() );
            } else {
                m_buffer[i] = Chunk();
            }
        }
        m_releasedChunkCount = lastChunkToRelease + 1;
    }

    [[nodiscard]] size_t
    setMaxReusableChunkCount() const noexcept
    {
        return m_maxReusableChunkCount;
    }

    void
    setMaxReusableChunkCount( const size_t maxReusableChunkCount )
    {
        m_maxReusableChunkCount = maxReusableChunkCount;
        if ( m_reusableChunks.size() > m_maxReusableChunkCount ) {
            m_reusableChunks.resize( m_maxReusableChunkCount );
        }
    }

private:
    void
    bufferUpTo( const size_t untilOffset )
    {
        while ( m_file && !m_underlyingFileEOF && ( m_numberOfBytesRead < untilOffset ) ) {
            /* If the last chunk is already full, create a new empty one. */
            if ( m_buffer.empty() || ( m_buffer.back().size() >= CHUNK_SIZE ) ) {
                if ( m_reusableChunks.empty() ) {
                    m_buffer.emplace_back();
                } else {
                    m_buffer.emplace_back( std::move( m_reusableChunks.back() ) );
                    m_buffer.back().clear();
                    m_reusableChunks.pop_back();
                }
            }

            /* Fill up the last buffer chunk to CHUNK_SIZE. */
            const auto oldChunkSize = m_buffer.back().size();
            m_buffer.back().resize( CHUNK_SIZE );
            const auto nBytesRead = m_file->read( reinterpret_cast<char*>( m_buffer.back().data() + oldChunkSize ),
                                                  m_buffer.back().size() - oldChunkSize );
            m_buffer.back().resize( oldChunkSize + nBytesRead );

            m_numberOfBytesRead += nBytesRead;
            m_underlyingFileEOF = nBytesRead == 0;
        }
    }

    [[nodiscard]] size_t
    getChunkIndex( const size_t offset ) const
    {
        /* Find start chunk to start reading from. */
        const auto startChunk = offset / CHUNK_SIZE;
        if ( offset < m_numberOfBytesRead ) {
            if ( startChunk >= m_buffer.size() ) {
                throw std::logic_error( "[SinglePassFileReader] Current position is inside file but failed to find "
                                        "chunk!" );
            }
            if ( m_buffer[startChunk].empty() ) {
                throw std::invalid_argument( "[SinglePassFileReader] Trying to access a chunk that has already been "
                                             "released!" );
            }
        }

        return startChunk;
    }

    [[nodiscard]] const Chunk&
    getChunk( size_t index ) const
    {
        const auto& chunk = m_buffer.at( index );

        if ( ( index + 1 < m_buffer.size() ) && ( chunk.size() != CHUNK_SIZE ) ) {
            std::stringstream message;
            message << "[SinglePassFileReader] All but the last chunk must be of equal size! Chunk "
                    << index << " out of " << m_buffer.size() << " has size " << formatBytes( chunk.size() )
                    << " instead of expected " << formatBytes( CHUNK_SIZE ) << "!";
            throw std::logic_error( std::move( message ).str() );
        }

        return chunk;
    }

protected:
    const UniqueFileReader m_file;
    bool m_underlyingFileEOF{ false };

    size_t m_numberOfBytesRead{ 0 };
    /**
     * This only exists to speed up subsequent @ref releaseUpTo calls to avoid having to check all chunks up
     * to the specified position. In the future these chunks might actually be fully removed from @ref m_buffer.
     * Then, @ref m_releasedChunkCount would become necessary to find the correct chunk in @ref m_buffer.
     */
    size_t m_releasedChunkCount{ 0 };
    std::deque<Chunk> m_buffer;

    size_t m_maxReusableChunkCount{ 1 };
    std::deque<Chunk> m_reusableChunks;

    size_t m_currentPosition{ 0 };
};
