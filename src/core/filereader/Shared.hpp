#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "FileReader.hpp"


class SharedFileReader final :
    public FileReader
{
public:
    /**
     * Create a new shared file reader from an existing FileReader. Takes ownership of the given FileReader!
     */
    explicit
    SharedFileReader( FileReader* file ) :
        m_mutex( std::make_shared<std::mutex>() ),
        m_fileSizeBytes( ( file != nullptr ) ? file->size() : 0 )
    {
        if ( file == nullptr ) {
            throw std::invalid_argument( "File reader may not be null!" );
        }

        if ( dynamic_cast<SharedFileReader*>( file ) != nullptr ) {
            throw std::invalid_argument( "It makes no sense to wrap a SharedFileReader in another one. Use clone!" );
        }

        if ( !file->seekable() ) {
            throw std::invalid_argument( "This class heavily relies on seeking and won't work with unseekable files!" );
        }


        m_sharedFile =
            std::shared_ptr<FileReader>(
                file,
                [] ( auto* const p ) {
                    if ( ( p != nullptr ) && !p->closed() ) {
                        p->close();
                    }
                    delete p;
                }
            );

        m_currentPosition = m_sharedFile->tell();
    }

    explicit
    SharedFileReader( std::unique_ptr<FileReader> file ) :
        SharedFileReader( file.release() )
    {}

    [[nodiscard]] FileReader*
    clone() const override
    {
        return new SharedFileReader( *this );
    }

private:
    /**
     * Create a new shared file reader from an existing SharedFileReader by copying shared pointers.
     * The underlying file and mutex are held as a shared_ptr and therefore not copied itself!
     */
    SharedFileReader( const SharedFileReader& other ) :
        m_sharedFile( other.m_sharedFile ),
        m_mutex( other.m_mutex ),
        m_fileSizeBytes( other.m_fileSizeBytes ),
        m_currentPosition( other.m_currentPosition )
    {}

public:
    void
    close() override
    {
        /* This is a shared file. Closing the underlying file while it might be used by another process,
         * seems like a bug-prone functionality. If you really want to close a file, delete all SharedFileReader
         * classes using it. It will be closed by the last destructor @see deleter set in the constructor. */
        std::scoped_lock lock( *m_mutex );
        m_sharedFile.reset();
    }

    [[nodiscard]] bool
    closed() const override
    {
        std::scoped_lock lock( *m_mutex );
        return !m_sharedFile || m_sharedFile->closed();
    }

    [[nodiscard]] bool
    eof() const override
    {
        /* m_sharedFile->eof() won't work because some other thread might set the EOF bit on the underlying file! */
        return m_currentPosition >= m_fileSizeBytes;
    }

    [[nodiscard]] bool
    fail() const override
    {
        std::scoped_lock lock( *m_mutex );
        return !m_sharedFile || m_sharedFile->fail();
    }

    [[nodiscard]] int
    fileno() const override
    {
        std::scoped_lock lock( *m_mutex );
        if ( m_sharedFile ) {
            return m_sharedFile->fileno();
        }
        throw std::invalid_argument( "Invalid or closed SharedFileReader has no associated fileno!" );
    }

    [[nodiscard]] bool
    seekable() const override
    {
        return true;
    }

    [[nodiscard]] size_t
    size() const override
    {
        return m_fileSizeBytes;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        std::scoped_lock lock( *m_mutex );

        if ( !m_sharedFile || m_sharedFile->closed() ) {
            throw std::invalid_argument( "Invalid or closed SharedFileReader can't be seeked!" );
        }

        switch ( origin )
        {
        case SEEK_CUR:
            offset += static_cast<long long int>( m_currentPosition );
            break;
        case SEEK_SET:
            break;
        case SEEK_END:
            offset += static_cast<long long int>( m_fileSizeBytes );
            break;
        }

        m_currentPosition = std::min( static_cast<size_t>( std::max( 0LL, offset ) ), m_fileSizeBytes );
        return m_currentPosition;
    }

    [[nodiscard]] size_t
    read( char*  buffer,
          size_t nMaxBytesToRead ) override
    {
        if ( buffer == nullptr ) {
            throw std::invalid_argument( "Buffer may not be nullptr!" );
        }

        if ( nMaxBytesToRead == 0 ) {
            return 0;
        }

        std::scoped_lock lock( *m_mutex );

        if ( !m_sharedFile || m_sharedFile->closed() ) {
            throw std::invalid_argument( "Invalid or closed SharedFileReader can't be read from!" );
        }

        nMaxBytesToRead = std::min( nMaxBytesToRead, m_fileSizeBytes - m_currentPosition );

        /* Seeking alone does not clear the EOF nor fail bit if the last read did set it. */
        m_sharedFile->clearerr();
        m_sharedFile->seek( m_currentPosition, SEEK_SET );
        const auto nBytesRead = m_sharedFile->read( buffer, nMaxBytesToRead );
        m_currentPosition += nBytesRead;
        return nBytesRead;
    }

    [[nodiscard]] size_t
    tell() const override
    {
        /* Do not use m_sharedFile->tell() because another thread might move that internal file position! */
        return m_currentPosition;
    }

    void
    clearerr() override
    {
        throw std::invalid_argument( "Not implemented because after clearing error another thread might "
                                     "set an error again right away, which makes this interface useless." );
    }

private:
    std::shared_ptr<FileReader> m_sharedFile;
    const std::shared_ptr<std::mutex> m_mutex;

    /* These are only for performance to avoid some unnecessary locks. */
    const size_t m_fileSizeBytes;
    size_t m_currentPosition{ 0 };
};
