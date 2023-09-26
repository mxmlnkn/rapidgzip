#pragma once

#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <common.hpp>


class FileReader;

using UniqueFileReader = std::unique_ptr<FileReader>;


/**
 * This file interface is heavily inspired by IOBase from Python because it is to be used from Python.
 * However, it strips anything related to file modification resulting a read-only file object.
 * @see https://docs.python.org/3/library/io.html#class-hierarchy
 */
class FileReader
{
public:
    FileReader() = default;

    virtual
    ~FileReader() = default;

    /* Delete copy constructors and assignments to avoid slicing. */

    FileReader( const FileReader& ) = delete;

    FileReader( FileReader&& ) = delete;

    FileReader&
    operator=( const FileReader& ) = delete;

    FileReader&
    operator=( FileReader&& ) = delete;

    [[nodiscard]] virtual UniqueFileReader
    clone() const = 0;

    virtual void
    close() = 0;

    [[nodiscard]] virtual bool
    closed() const = 0;

    [[nodiscard]] virtual bool
    eof() const = 0;

    [[nodiscard]] virtual bool
    fail() const = 0;

    [[nodiscard]] virtual int
    fileno() const = 0;

    [[nodiscard]] virtual bool
    seekable() const = 0;

    [[nodiscard]] virtual size_t
    read( char*  buffer,
          size_t nMaxBytesToRead ) = 0;

    virtual size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) = 0;

    [[nodiscard]] virtual std::optional<size_t>
    size() const = 0;

    [[nodiscard]] virtual size_t
    tell() const = 0;

    virtual void
    clearerr() = 0;

    [[nodiscard]] size_t
    effectiveOffset( long long int offset,
                     int           origin ) const
    {
        offset = [&] () {
            switch ( origin )
            {
            case SEEK_CUR:
                return saturatingAddition( static_cast<long long int>( tell() ), offset );
            case SEEK_SET:
                return offset;
            case SEEK_END:
                if ( const auto fileSize = size(); fileSize.has_value() ) {
                    return saturatingAddition( static_cast<long long int>( *fileSize ), offset );
                }
                throw std::logic_error( "File size is not available to seek from end!" );
            }
            throw std::invalid_argument( "Invalid seek origin supplied: " + std::to_string( origin ) );
        } ();

        const auto positiveOffset = static_cast<size_t>( std::max( offset, 0LL ) );
        /* Streaming file readers may return nullopt until EOF has been reached. */
        const auto fileSize = size();
        return fileSize.has_value() ? std::min( positiveOffset, *fileSize ) : positiveOffset;
    }
};
