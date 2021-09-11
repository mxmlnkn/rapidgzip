#pragma once

#include <algorithm>
#include <cstdio>       // fread
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>

#include "common.hpp"   // unistd, S_ISFIFO, fstat, ...
#include "FileReader.hpp"


class StandardFileReader :
    public FileReader
{
public:
    StandardFileReader( std::string filePath ) :
        m_file( throwingOpen( filePath, "rb" ) ),
        m_fileDescriptor( ::fileno( fp() ) ),
        m_filePath( std::move( filePath ) ),
        m_seekable( determineSeekable( m_fileDescriptor ) ),
        m_fileSizeBytes( determineFileSize( m_fileDescriptor ) )
    {
        init();
    }

    StandardFileReader( int fileDescriptor ) :
        /* Use dup here so that the following fclose will not close the original file descriptor,
         * which probably is still in use by the caller! Note that dup will not guarantee independent
         * file positions though, so restore the previous position after closing. */
        m_file( throwingOpen( dup( fileDescriptor ), "rb" ) ),
        m_fileDescriptor( ::fileno( fp() ) ),
        m_filePath( fdFilePath( m_fileDescriptor ) ),
        m_seekable( determineSeekable( m_fileDescriptor ) ),
        m_fileSizeBytes( determineFileSize( m_fileDescriptor ) )
    {
        init();
    }

    ~StandardFileReader()
    {
        close();
    }

    [[nodiscard]] FileReader*
    clone() const override
    {
        throw std::invalid_argument( "Cloning file path reader not allowed because the internal file position "
                                     "should not be modified by multiple owners!" );
    }

    /* Copying is simply not allowed because that might interfere with the file position state, use SharedFileReader! */

    void
    close() override
    {
        if ( !m_file ) {
            return;
        }

        /* Try to restore the file position the file had before it was given to us. */
        if ( m_seekable ) {
            std::fsetpos( m_file.get(), &m_initialPosition );
        }

        m_file.reset();
    }

    [[nodiscard]] bool
    closed() const override
    {
        return !m_file;
    }

    [[nodiscard]] bool
    eof() const override
    {
        return m_seekable ? tell() >= size() : !m_lastReadSuccessful;
    }

    [[nodiscard]] bool
    fail() const override
    {
        return std::ferror( fp() );
    }

    [[nodiscard]] int
    fileno() const override
    {
        if ( m_file ) {
            return m_fileDescriptor;
        }
        throw std::invalid_argument( "Trying to get fileno of an invalid file!" );
    }

    [[nodiscard]] bool
    seekable() const override
    {
        return m_seekable;
    }

    [[nodiscard]] size_t
    read( char*  buffer,
          size_t nMaxBytesToRead ) override
    {
        if ( !m_file ) {
            throw std::invalid_argument( "Invalid or file can't be seeked!" );
        }

        if ( nMaxBytesToRead == 0 ) {
            return 0;
        }

        const auto nBytesRead = std::fread( buffer, /* element size */ 1, nMaxBytesToRead, m_file.get() );

        if ( nBytesRead == 0 ) {
        #if 1
            /* fread retuning 0 might traditionally be a valid case if the file position was after the last byte.
             * EOF is only set after reading after the end not when the file position is at the end. */
            m_lastReadSuccessful = false;
            return 0;
        #else
            /* I had some very weird issues of fread returning 0 bytes read even thought ftell was 0 and no EOF
             * bit nor fail bit was set. After that read no error was set but the EOF bit. It makes not sense to me.
             * It almost looks like ftell is broken and the actual position was not moved from the file end.
             * The problem disappeared after trimming down the seek method to basically directly call std::fseek
             * instead of manually transforming the offset and doing some preemptive returns, so maybe it was that. */
            std::stringstream message;
            message
            << "[StandardFileReader] Read call failed (" << nBytesRead << " B read)!\n"
            << "  Buffer: " << (void*)buffer << "\n"
            << "  nMaxBytesToRead: " << nMaxBytesToRead << " B\n"
            << "  File pointer: " << (void*)m_file.get() << "\n"
            << "  File size: " << m_fileSizeBytes << " B\n"
            << "  EOF: " << std::feof( m_file.get() ) << "\n"
            << "  ferror: " << std::ferror( m_file.get() ) << "\n"
            << "  fileno: " << m_fileDescriptor << "\n"
            << "  file path: " << m_filePath << "\n"
            << "  m_currentPosition: " << m_currentPosition << "\n"
            << "  ftell: " << std::ftell( m_file.get() ) << "\n"
            << "\n";
            throw std::domain_error( message.str() );
        #endif
        }

        m_currentPosition += nBytesRead;
        m_lastReadSuccessful = nBytesRead == nMaxBytesToRead;

        return nBytesRead;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        if ( !m_file || !m_seekable ) {
            throw std::invalid_argument( "Invalid or file can't be seeked!" );
        }

        const auto returnCode = std::fseek( m_file.get(), offset, SEEK_SET );
        if ( returnCode != 0 ) {
            throw std::runtime_error( "Seeking failed!" );
        }

        if ( origin == SEEK_SET ) {
            m_currentPosition = offset;
        } else {
            /* Note that the file must be seekable at this point, meaning std::ftell will work! */
            m_currentPosition = std::ftell( m_file.get() );
        }

        return m_currentPosition;
    }

    [[nodiscard]] virtual size_t
    size() const override
    {
        return m_fileSizeBytes;
    }

    [[nodiscard]] size_t
    tell() const override
    {
        if ( m_seekable ) {
            return std::ftell( fp() );
        }
        return m_currentPosition;
    }

    void
    clearerr() override
    {
        std::clearerr( fp() );
    }

private:
    void
    init()
    {
        std::fgetpos( fp(), &m_initialPosition );

        /* On macOS opening special files like /dev/fd/3 might result in the file position
         * not being 0 in the case it has been seeked or read from somewhere else! */
        if ( m_seekable ) {
            seek( 0, SEEK_SET );
        }
    }

    [[nodiscard]] static size_t
    determineFileSize( int fileNumber )
    {
        struct stat fileStats;
        fstat( fileNumber, &fileStats );
        return fileStats.st_size;
    }

    [[nodiscard]] static bool
    determineSeekable( int fileNumber )
    {
        struct stat fileStats;
        fstat( fileNumber, &fileStats );
        return !S_ISFIFO( fileStats.st_mode );
    }

    [[nodiscard]] FILE*
    fp() const
    {
        if ( m_file ) {
            return m_file.get();
        }
        throw std::invalid_argument( "Operation not allowed on an invalid file!" );
    }

protected:
    unique_file_ptr m_file;
    const int m_fileDescriptor;
    const std::string m_filePath;

    std::fpos_t m_initialPosition;
    const bool m_seekable;
    const size_t m_fileSizeBytes;

    size_t m_currentPosition{ 0 };  /**< Only necessary for unseekable files. */
    bool m_lastReadSuccessful{ true };
};
