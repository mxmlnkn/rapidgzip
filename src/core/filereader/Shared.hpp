#pragma once

#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef _MSC_VER
    #include <unistd.h>
#endif

#include <common.hpp>
#include <Statistics.hpp>

#include "FileReader.hpp"
#ifndef _MSC_VER
    #include "Standard.hpp"
#endif


class SharedFileReader final :
    public FileReader
{
private:
    /**
     * Create a new shared file reader from an existing FileReader. Takes ownership of the given FileReader!
     */
    explicit
    SharedFileReader( FileReader* file ) :
        m_statistics( dynamic_cast<SharedFileReader*>( file ) == nullptr
                      ? std::make_shared<AccessStatistics>()
                      : dynamic_cast<SharedFileReader*>( file )->m_statistics ),
        m_mutex( dynamic_cast<SharedFileReader*>( file ) == nullptr
                 ? std::make_shared<std::mutex>()
                 : dynamic_cast<SharedFileReader*>( file )->m_mutex ),
        m_fileSizeBytes( file == nullptr ? 0 : file->size() ),
        m_currentPosition( file == nullptr ? 0 : file->tell() )
    {
        if ( file == nullptr ) {
            throw std::invalid_argument( "File reader may not be null!" );
        }

    #ifndef _MSC_VER
        if ( auto* const sharedFile = dynamic_cast<StandardFileReader*>( file ); sharedFile != nullptr ) {
            m_fileDescriptor = file->fileno();
        }
    #endif

        /* Fall back to a clone-like copy of all members if the source is also a SharedFileReader.
         * Most of the members are already copied inside the member initializer list. */
        if ( auto* const sharedFile = dynamic_cast<SharedFileReader*>( file ); sharedFile != nullptr ) {
            m_sharedFile = sharedFile->m_sharedFile;
            return;
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
    }

public:
    explicit
    SharedFileReader( UniqueFileReader file ) :
        SharedFileReader( file.release() )
    {}

    ~SharedFileReader()
    {
        if ( m_statistics && m_statistics->showProfileOnDestruction && ( m_statistics.use_count() == 1 ) ) {
            std::cerr << ( ThreadSafeOutput()
                << "[SharedFileReader::~SharedFileReader]\n"
                << "   seeks back    : (" << m_statistics->seekBack.formatAverageWithUncertainty( true )
                << " ) B (" << m_statistics->seekBack.count << "calls )\n"
                << "   seeks forward : (" << m_statistics->seekForward.formatAverageWithUncertainty( true )
                << " ) B (" << m_statistics->seekForward.count << "calls )\n"
                << "   reads         : (" << m_statistics->read.formatAverageWithUncertainty( true )
                << " ) B (" << m_statistics->read.count << "calls )\n"
                << "   locks         :" << m_statistics->locks << "\n"
                << "   read in total" << static_cast<uint64_t>( m_statistics->read.sum )
                << "B out of" << m_fileSizeBytes << "B,"
                << "i.e., read the file" << m_statistics->read.sum / m_fileSizeBytes << "times\n"
                << "   time spent seeking and reading:" << m_statistics->readingTime << "s\n"
            );
        }
    }

    /**
     * Creates a shallow copy of this file reader with an independent file position to access the underlying file.
     */
    [[nodiscard]] UniqueFileReader
    clone() const override
    {
        return UniqueFileReader( new SharedFileReader( *this ) );
    }

    void
    setStatisticsEnabled( bool enabled )
    {
        if ( m_statistics ) {
            m_statistics->enabled = enabled;
        }
    }

    void
    setShowProfileOnDestruction( bool showProfileOnDestruction )
    {
        if ( m_statistics ) {
            m_statistics->showProfileOnDestruction = showProfileOnDestruction;
        }
    }

private:
    /**
     * Create a new shared file reader from an existing SharedFileReader by copying shared pointers.
     * The underlying file and mutex are held as a shared_ptr and therefore not copied itself!
     * Make it private because only @ref clone should call this. It cannot be defaulted because the FileReader
     * base class deleted its copy constructor.
     */
    SharedFileReader( const SharedFileReader& other ) :
        m_statistics( other.m_statistics ),
        m_sharedFile( other.m_sharedFile ),
        m_fileDescriptor( other.m_fileDescriptor ),
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
        const auto lock = getLock();
        m_sharedFile.reset();
    }

    [[nodiscard]] bool
    closed() const override
    {
        const auto lock = getLock();
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
        const auto lock = getLock();
        return !m_sharedFile || m_sharedFile->fail();
    }

    [[nodiscard]] int
    fileno() const override
    {
        if ( m_fileDescriptor >= 0 ) {
            return m_fileDescriptor;
        }

        const auto lock = getLock();
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

        if ( !m_sharedFile ) {
            throw std::invalid_argument( "Invalid SharedFileReader cannot be read from!" );
        }

        nMaxBytesToRead = std::min( nMaxBytesToRead, m_fileSizeBytes - m_currentPosition );

        const auto t0 = now();
        size_t nBytesRead{ 0 };
    #ifndef _MSC_VER
        if ( m_fileDescriptor >= 0 ) {
            /* This statistic only approximates the actual pread behavior. The OS can probably reorder
             * concurrent pread calls and we would have to enclose pread itself in a lock, which defeats
             * the purpose of pread for speed. */
            if ( m_statistics && m_statistics->enabled ) {
                const std::scoped_lock lock{ m_statistics->mutex };
                const auto oldOffset = m_statistics->lastAccessOffset;
                if ( m_currentPosition > oldOffset ) {
                    m_statistics->seekForward.merge( m_currentPosition - oldOffset );
                } else if ( m_currentPosition < oldOffset ) {
                    m_statistics->seekBack.merge( oldOffset - m_currentPosition );
                }
                m_statistics->lastAccessOffset = m_currentPosition;
            }

            const auto nBytesReadWithPread = ::pread( m_sharedFile->fileno(), buffer, nMaxBytesToRead,
                                                      m_currentPosition );
            if ( nBytesReadWithPread < 0 ) {
                throw std::runtime_error( "Failed to read from file!" );
            }
            nBytesRead = static_cast<size_t>( nBytesReadWithPread );
        } else
    #endif
        {
            const auto fileLock = getLock();

            if ( m_statistics && m_statistics->enabled ) {
                const std::scoped_lock lock{ m_statistics->mutex };
                const auto oldOffset = m_sharedFile->tell();
                if ( m_currentPosition > oldOffset ) {
                    m_statistics->seekForward.merge( m_currentPosition - oldOffset );
                } else if ( m_currentPosition < oldOffset ) {
                    m_statistics->seekBack.merge( oldOffset - m_currentPosition );
                }
            }

            /* Seeking alone does not clear the EOF nor fail bit if the last read did set it. */
            m_sharedFile->clearerr();
            m_sharedFile->seek( m_currentPosition, SEEK_SET );
            nBytesRead = m_sharedFile->read( buffer, nMaxBytesToRead );
        }

        if ( m_statistics && m_statistics->enabled ) {
            const std::scoped_lock lock{ m_statistics->mutex };
            m_statistics->read.merge( nBytesRead );
            m_statistics->readingTime += duration( t0 );
        }

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
    [[nodiscard]] std::scoped_lock<std::mutex>
    getLock() const
    {
        if ( m_statistics && m_statistics->enabled ) {
            ++m_statistics->locks;
        }
        return std::scoped_lock( *m_mutex );
    }

private:
    struct AccessStatistics {
        bool showProfileOnDestruction{ false };
        bool enabled{ false };
        uint64_t lastAccessOffset{ 0 };  // necessary for pread because tell() won't work
        Statistics<uint64_t> read;
        Statistics<uint64_t> seekBack;
        Statistics<uint64_t> seekForward;
        double readingTime{ 0 };
        std::atomic<uint64_t> locks{ 0 };
        std::mutex mutex{};
    };

private:
    const std::shared_ptr<AccessStatistics> m_statistics;

    std::shared_ptr<FileReader> m_sharedFile;
    int m_fileDescriptor{ -1 };
    const std::shared_ptr<std::mutex> m_mutex;

    /** This is only for performance to avoid querying the file. */
    const size_t m_fileSizeBytes;

    /**
     * This is the independent file pointer that this class offers! Each seek call will only update this and
     * each read call will seek to this offset in an atomic manner before reading from the underlying file.
     */
    size_t m_currentPosition{ 0 };
};


[[nodiscard]] inline std::unique_ptr<SharedFileReader>
ensureSharedFileReader( UniqueFileReader&& fileReader )
{
    if ( !fileReader ) {
        throw std::invalid_argument( "File reader must not be null!" );
    }

    if ( auto* const casted = dynamic_cast<SharedFileReader*>( fileReader.get() ); casted != nullptr ) {
        fileReader.release();
        return std::unique_ptr<SharedFileReader>( casted );
    }
    return std::make_unique<SharedFileReader>( std::move( fileReader ) );
}
