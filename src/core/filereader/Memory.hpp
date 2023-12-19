#pragma once

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "FileReader.hpp"


class MemoryFileReader :
    public FileReader
{
public:
    explicit
    MemoryFileReader( std::vector<char> data ) :
        m_data( std::move( data ) )
    {}

    [[nodiscard]] UniqueFileReader
    clone() const override
    {
        return std::make_unique<MemoryFileReader>( m_data );
    }

    void
    close() override
    {
        m_closed = true;
        m_currentPosition = 0;
    }

    [[nodiscard]] bool
    closed() const override
    {
        return m_closed;
    }

    [[nodiscard]] bool
    eof() const override
    {
        return tell() >= size();
    }

    [[nodiscard]] bool
    fail() const override
    {
        return false;
    }

    [[nodiscard]] int
    fileno() const override
    {
        throw std::invalid_argument( "Trying to get fileno of an in-memory file!" );
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
        const auto nBytesRead = m_currentPosition + nMaxBytesToRead > m_data.size()
                                ? m_data.size() - m_currentPosition
                                : nMaxBytesToRead;

        if ( nBytesRead == 0 ) {
            return 0;
        }

        if ( buffer != nullptr ) {
            std::memcpy( buffer, m_data.data() + m_currentPosition, nBytesRead );
        }

        m_currentPosition += nBytesRead;

        return nBytesRead;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        m_currentPosition = effectiveOffset( offset, origin );
        return m_currentPosition;
    }

    [[nodiscard]] std::optional<size_t>
    size() const override
    {
        return m_data.size();
    }

    [[nodiscard]] size_t
    tell() const override
    {
        return m_currentPosition;
    }

    void
    clearerr() override
    {}

protected:
    const std::vector<char> m_data;
    bool m_closed{ false };

    size_t m_currentPosition{ 0 };  /**< Only necessary for unseekable files. */
};
