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
    BufferViewFileReader( const void* const buffer,
                          const size_t      size ) :
        m_buffer( reinterpret_cast<const std::byte*>( buffer ) ),
        m_size( size )
    {}

    explicit
    BufferViewFileReader( const std::vector<char>& buffer ) :
        m_buffer( reinterpret_cast<const std::byte*>( buffer.data() ) ),
        m_size( buffer.size() )
    {}

    explicit
    BufferViewFileReader( const std::vector<unsigned char>& buffer ) :
        m_buffer( reinterpret_cast<const std::byte*>( buffer.data() ) ),
        m_size( buffer.size() )
    {}

    explicit
    BufferViewFileReader( const std::vector<std::byte>& buffer ) :
        m_buffer( buffer.data() ),
        m_size( buffer.size() )
    {}

    [[nodiscard]] UniqueFileReader
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
        return m_bufferPosition >= m_size;
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

        if ( ( nMaxBytesToRead == 0 ) || ( m_bufferPosition >= m_size ) ) {
            return 0;
        }

        const auto nBytesToReadFromBuffer = std::min( m_size - m_bufferPosition, nMaxBytesToRead );
        std::memcpy( buffer, m_buffer + m_bufferPosition, nBytesToReadFromBuffer );
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

        /* Translate offset. */
        const auto newBufferPosition =
            [this, offset, origin] () {
                switch ( origin )
                {
                case SEEK_SET: return offset;
                case SEEK_CUR: return static_cast<long long int>( m_bufferPosition ) + offset;
                case SEEK_END: return static_cast<long long int>( size() ) + offset;
                }
                throw std::invalid_argument( "Invalid origin value!" );
            }();

        /* Check if we can simply seek inside the buffer. */
        if ( ( newBufferPosition >= 0 ) && ( static_cast<size_t>( newBufferPosition ) <= m_size ) ) {
            m_bufferPosition = newBufferPosition;
            return tell();
        }

        throw std::invalid_argument( "Cannot seek outside of in-memory file range!" );
    }

    [[nodiscard]] size_t
    size() const override
    {
        return m_size;
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
    const std::byte* const m_buffer;
    const size_t m_size;
    size_t m_bufferPosition{ 0 };
};
