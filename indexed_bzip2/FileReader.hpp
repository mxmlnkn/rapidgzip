#pragma once

#include <cstddef>
#include <cstdio>


/**
 * This file interface is heavily inspired by IOBase from Python because it is to be used from Python.
 * However, it strips anything related to file modification resulting a read-only file object.
 * @see https://docs.python.org/3/library/io.html#class-hierarchy
 */
class FileReader
{
public:
    FileReader() = default;

    virtual ~FileReader() = default;

    /* Delete copy constructors and assignments to avoid slicing. */

    FileReader( const FileReader& ) = delete;
    FileReader( FileReader&& ) = delete;

    FileReader& operator=( const FileReader& ) = delete;
    FileReader& operator=( FileReader&& ) = delete;

    [[nodiscard]] virtual FileReader*
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

    [[nodiscard]] virtual size_t
    size() const = 0;

    [[nodiscard]] virtual size_t
    tell() const = 0;

    virtual void
    clearerr() = 0;
};
