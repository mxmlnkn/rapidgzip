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
    virtual ~FileReader() = default;

    virtual void
    close() = 0;

    virtual bool
    closed() const = 0;

    virtual bool
    eof() const = 0;

    virtual int
    fileno() const = 0;

    virtual bool
    seekable() const = 0;

    virtual size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) = 0;

    virtual size_t
    size() const = 0;

    virtual size_t
    tell() const = 0;

    /** @todo Some kind of read function. Unfortunately, they are not yet sufficiently uniform. */
};
