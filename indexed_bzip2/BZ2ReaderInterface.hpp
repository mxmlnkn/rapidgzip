#pragma once

#include <cstddef>
#include <limits>
#include <map>

#include "FileReader.hpp"

using std::size_t;


class BZ2ReaderInterface :
    public FileReader
{
public:
    virtual ~BZ2ReaderInterface() = default;

    /**
     * @param[out] outputBuffer should at least be large enough to hold @p nBytesToRead bytes
     * @return number of bytes written
     */
    virtual size_t
    read( const int    outputFileDescriptor = -1,
          char* const  outputBuffer = nullptr,
          const size_t nBytesToRead = std::numeric_limits<size_t>::max() ) = 0;

    /* BZip2 specific methods */

    virtual bool
    blockOffsetsComplete() const = 0;

    /**
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    virtual std::map<size_t, size_t>
    blockOffsets() = 0;

    /**
     * Same as @ref blockOffsets but it won't force calculation of all blocks and simply returns
     * what is availabe at call time.
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    virtual std::map<size_t, size_t>
    availableBlockOffsets() const = 0;

    virtual void
    setBlockOffsets( std::map<size_t, size_t> offsets ) = 0;

    /**
     * @return number of processed bits of compressed bzip2 input file stream
     * @note Bzip2 is block based and blocks are currently read fully, meaning that the granularity
     *       of the returned position is ~100-900kB. It's only useful for a rough estimate.
     */
    virtual size_t
    tellCompressed() const = 0;
};
