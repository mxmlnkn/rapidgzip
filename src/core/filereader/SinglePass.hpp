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

private:
    void
    bufferUpTo( const size_t untilOffset )
    {
        while ( m_file && !m_underlyingFileEOF && ( m_numberOfBytesRead < untilOffset ) ) {
            /* If the last chunk is already full, create a new empty one. */
            if ( m_buffer.empty() || ( m_buffer.back().size() >= CHUNK_SIZE ) ) {
                m_buffer.emplace_back();
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
    std::deque<Chunk> m_buffer;

    size_t m_currentPosition{ 0 };
};
