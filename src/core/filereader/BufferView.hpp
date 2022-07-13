#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "FileReader.hpp"


class BufferViewFileReader :
    public FileReader
{
public:
    explicit
    BufferViewFileReader( const std::vector<char>& buffer ) :
        m_buffer( buffer )
    {}

    [[nodiscard]] FileReader*
    clone() const override
    {
        throw std::invalid_argument( "Cloning this file reader is not allowed because the internal file position "
                                     "should not be modified by multiple owners!" );
    }

    /* Copying is simply not allowed because that might interfere with the file position state, use SharedFileReader! */

    void
    close() override
    {
        m_closed = true;
    }

    [[nodiscard]] bool
    closed() const override
    {
        return m_closed;
    }

    [[nodiscard]] bool
    eof() const override
    {
        return m_bufferPosition >= m_buffer.size();
    }

    [[nodiscard]] bool
    fail() const override
    {
        return false;
    }

    [[nodiscard]] int
    fileno() const override
    {
        throw std::invalid_argument( "Trying to get fileno of an in-memory or closed file!" );
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
        if ( closed() ) {
            throw std::invalid_argument( "Cannot read from closed file!" );
        }

        if ( ( nMaxBytesToRead == 0 ) || ( m_bufferPosition >= m_buffer.size() ) ) {
            return 0;
        }

        const auto nBytesToReadFromBuffer = std::min( m_buffer.size() - m_bufferPosition, nMaxBytesToRead );
        std::memcpy( buffer, m_buffer.data() + m_bufferPosition, nBytesToReadFromBuffer );
        m_bufferPosition += nBytesToReadFromBuffer;
        return nBytesToReadFromBuffer;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        if ( closed() ) {
            throw std::invalid_argument( "Cannot seek closed file!" );
        }

        /* Translate into SEEK_CUR. */
        long long int relativeOffset = 0;

        switch ( origin )
        {
        case SEEK_SET:
            relativeOffset = offset - static_cast<long long int>( tell() );
            break;
        case SEEK_CUR:
            relativeOffset = offset;
            break;
        case SEEK_END:
            relativeOffset = size() + offset - static_cast<long long int>( tell() );
            break;
        }

        /* Check if we can simply seek inside the buffer. */
        const auto newBufferPosition = static_cast<long long int>( m_bufferPosition ) + relativeOffset;
        if ( ( newBufferPosition >= 0 ) && ( static_cast<size_t>( newBufferPosition ) <= m_buffer.size() ) ) {
            m_bufferPosition = newBufferPosition;
            return tell();
        }

        throw std::invalid_argument( "Cannot seek outside of in-memory file range!" );
    }

    [[nodiscard]] size_t
    size() const override
    {
        return m_buffer.size();
    }

    [[nodiscard]] size_t
    tell() const override
    {
        return m_bufferPosition;
    }

    void
    clearerr() override
    {}

protected:
    bool m_closed{ false };
    const std::vector<char>& m_buffer;
    size_t m_bufferPosition{ 0 };
};
