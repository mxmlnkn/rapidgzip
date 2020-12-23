#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "bzip2.hpp"
#include "BitReader.hpp"
#include "BZ2ReaderInterface.hpp"
#include "FileReader.hpp"


class BZ2Reader :
    public BZ2ReaderInterface
{
public:
    using BlockHeader = bzip2::Block;

public:
    static constexpr size_t IOBUF_SIZE = 4096;

public:
    /* Constructors */

    explicit
    BZ2Reader( const std::string& filePath ) :
        m_bitReader( filePath )
    {}

    explicit
    BZ2Reader( int fileDescriptor ) :
        m_bitReader( fileDescriptor )
    {}

    BZ2Reader( const char*  bz2Data,
               const size_t size ) :
        m_bitReader( reinterpret_cast<const uint8_t*>( bz2Data ), size )
    {}


    /* FileReader overrides */

    int
    fileno() const override
    {
        return ::fileno( m_bitReader.fp() );
    }

    bool
    seekable() const override
    {
        return m_bitReader.seekable();
    }

    void
    close() override
    {
        m_bitReader.close();
    }

    bool
    closed() const override
    {
        return m_bitReader.closed();
    }

    bool
    eof() const override
    {
        return m_atEndOfFile;
    }

    size_t
    tell() const override
    {
        if ( m_atEndOfFile ) {
            return size();
        }
        return m_currentPosition;
    }

    size_t
    size() const override
    {
        if ( !m_blockToDataOffsetsComplete ) {
            throw std::invalid_argument( "Can't get stream size in BZ2 when not finished reading at least once!" );
        }
        return m_blockToDataOffsets.rbegin()->second;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override;


    /* BZip2 specific methods */

    uint32_t
    crc() const
    {
        return m_calculatedStreamCRC;
    }

    bool
    blockOffsetsComplete() const override
    {
        return m_blockToDataOffsetsComplete;
    }

    /**
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    std::map<size_t, size_t>
    blockOffsets() override
    {
        if ( !m_blockToDataOffsetsComplete ) {
            read();
        }

        return m_blockToDataOffsets;
    }

    /**
     * Same as @ref blockOffsets but it won't force calculation of all blocks and simply returns
     * what is availabe at call time.
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    std::map<size_t, size_t>
    availableBlockOffsets() const override
    {
        return m_blockToDataOffsets;
    }

    void
    setBlockOffsets( std::map<size_t, size_t> offsets ) override
    {
        if ( offsets.size() < 2 ) {
            throw std::invalid_argument( "Block offset map must contain at least one valid block and one EOS block!" );
        }
        m_blockToDataOffsetsComplete = true;
        m_blockToDataOffsets = std::move( offsets );
    }

    /**
     * @return number of processed bits of compressed bzip2 input file stream
     * @note Bzip2 is block based and blocks are currently read fully, meaning that the granularity
     *       of the returned position is ~100-900kB. It's only useful for a rough estimate.
     */
    size_t
    tellCompressed() const override
    {
        return m_bitReader.tell();
    }

    /**
     * @param[out] outputBuffer should at least be large enough to hold @p nBytesToRead bytes
     * @return number of bytes written
     */
    size_t
    read( const int    outputFileDescriptor = -1,
          char* const  outputBuffer = nullptr,
          const size_t nBytesToRead = std::numeric_limits<size_t>::max() ) override
    {
        size_t nBytesDecoded = 0;
        while ( ( nBytesDecoded < nBytesToRead ) && !m_bitReader.eof() && !eof() ) {
            /* The input may be a concatenation of multiple BZip2 files (like produced by pbzip2).
             * Therefore, iterate over those mutliple files and decode them to the specified output. */
            if ( ( m_bitReader.tell() == 0 ) || m_lastHeader.eos() ) {
                readBzip2Header();
            }
            nBytesDecoded += decodeStream( outputFileDescriptor, outputBuffer, nBytesToRead - nBytesDecoded );
        }
        m_currentPosition += nBytesDecoded;
        return nBytesDecoded;
    }

private:
    /**
     * @param  outputBuffer A char* to which the data is written.
     *                      You should ensure that at least @p maxBytesToFlush bytes can fit there!
     * @return The number of actually flushed bytes, which might be hindered,
     *         e.g., if the output file descriptor can't be written to!
     */
    size_t
    flushOutputBuffer( int    outputFileDescriptor = -1,
                       char*  outputBuffer         = nullptr,
                       size_t maxBytesToFlush      = std::numeric_limits<size_t>::max() );

    /**
     * Undo burrows-wheeler transform on intermediate buffer @ref dbuf to @ref outBuf
     *
     * Burrows-wheeler transform is described at:
     * @see http://dogma.net/markn/articles/bwt/bwt.htm
     * @see http://marknelson.us/1996/09/01/bwt/
     *
     * @return number of actually decoded bytes
     */
    size_t
    decodeStream( int    outputFileDescriptor = -1,
                  char*  outputBuffer         = nullptr,
                  size_t nMaxBytesToDecode    = std::numeric_limits<size_t>::max() );

    BlockHeader
    readBlockHeader( size_t bitsOffset );

protected:
    void
    readBzip2Header()
    {
        m_blockSize100k = bzip2::readBzip2Header( m_bitReader );
        m_calculatedStreamCRC = 0;
    }

protected:
    BitReader m_bitReader;

    uint8_t m_blockSize100k = 0;
    uint32_t m_streamCRC = 0; /** CRC of stream as last block says */
    uint32_t m_calculatedStreamCRC = 0;
    bool m_blockToDataOffsetsComplete = false;
    size_t m_currentPosition = 0; /** the current position as can only be modified with read or seek calls. */
    bool m_atEndOfFile = false;

    std::map<size_t, size_t> m_blockToDataOffsets;

private:
    BlockHeader m_lastHeader;

    /* This buffer is needed for decoding because decoding of runtime length encoded strings might lead to more
     * output data for the copies of a character than we can write out. In order to not save the outstanding copies
     * and the character to copy, this buffer acts as a kind of generalized "current decoder state". */
    std::vector<char> m_decodedBuffer = std::vector<char>( IOBUF_SIZE );
    /* it's strictly increasing during decoding and no previous data in m_decodedBuffer is acced,
     * so we can almost at any position clear m_decodedBuffer and set m_decodedBufferPos to 0, which is done for flushing! */
    size_t m_decodedBufferPos = 0;

    /** The sum over all decodeBuffer calls. This is used to create the block offset map */
    size_t m_decodedBytesCount = 0;
};


size_t
BZ2Reader::seek( long long int offset,
                 int           origin )
{
    switch ( origin )
    {
    case SEEK_CUR:
        offset = tell() + offset;
        break;
    case SEEK_SET:
        break;
    case SEEK_END:
        /* size() requires the block offsets to be available! */
        if ( !m_blockToDataOffsetsComplete ) {
            read();
        }
        offset = size() + offset;
        break;
    }

    if ( static_cast<long long int>( tell() ) == offset ) {
        return offset;
    }

    /* When block offsets are not complete yet, emulate forward seeking with a read. */
    if ( !m_blockToDataOffsetsComplete && ( offset > static_cast<long long int>( tell() ) ) ) {
        read( -1, nullptr, offset - tell() );
        return tell();
    }

    /* size() and then seeking requires the block offsets to be available! */
    if ( !m_blockToDataOffsetsComplete ) {
        read();
    }

    offset = std::max<decltype( offset )>( 0, offset );
    m_currentPosition = offset;

    flushOutputBuffer(); // ensure that no old data is left over

    m_atEndOfFile = static_cast<size_t>( offset ) >= size();
    if ( m_atEndOfFile ) {
        return size();
    }

    /* find offset from map (key and values are sorted, so we can bisect!) */
    const auto blockOffset = std::lower_bound(
        m_blockToDataOffsets.rbegin(), m_blockToDataOffsets.rend(), std::make_pair( 0, offset ),
        [] ( std::pair<size_t, size_t> a, std::pair<size_t, size_t> b ) { return a.second > b.second; } );

    if ( ( blockOffset == m_blockToDataOffsets.rend() ) || ( static_cast<size_t>( offset ) < blockOffset->second ) ) {
        throw std::runtime_error( "Could not find block to seek to for given offset" );
    }
    const auto nBytesSeekInBlock = offset - blockOffset->second;

    m_lastHeader = readBlockHeader( blockOffset->first );
    m_lastHeader.readBlockData();
    /* no decodeBzip2 necessary because we only seek inside one block! */
    const auto nBytesDecoded = decodeStream( -1, nullptr, nBytesSeekInBlock );

    if ( nBytesDecoded != nBytesSeekInBlock ) {
        std::stringstream msg;
        msg << "Could not read the required " << nBytesSeekInBlock
        << " to seek in block but only " << nBytesDecoded << "\n";
        throw std::runtime_error( msg.str() );
    }

    return offset;
}


inline BZ2Reader::BlockHeader
BZ2Reader::readBlockHeader( size_t offsetBits )
{
    /* note that blocks are NOT byte-aligned! Only the end of the stream has a necessary padding. */
    if ( !m_blockToDataOffsetsComplete ) {
        m_blockToDataOffsets.insert( { offsetBits, m_decodedBytesCount } );
    }

    m_bitReader.seek( offsetBits );
    BlockHeader header( m_bitReader );

    if ( header.eos() ) {
        /* EOS block contains CRC for whole stream */
        m_streamCRC = header.bwdata.headerCRC;

        if ( !m_blockToDataOffsetsComplete && ( m_streamCRC != m_calculatedStreamCRC ) ) {
            std::stringstream msg;
            msg << "[BZip2 block header] Stream CRC 0x" << std::hex << m_streamCRC
            << " does not match calculated CRC 0x" << m_calculatedStreamCRC;
            throw std::runtime_error( msg.str() );
        }
    }

    m_atEndOfFile = header.eof();
    if ( header.eof() ) {
        m_blockToDataOffsetsComplete = true;
    }

    return header;
}


inline size_t
BZ2Reader::flushOutputBuffer( int    const outputFileDescriptor,
                              char*  const outputBuffer,
                              size_t const maxBytesToFlush )
{
    const auto nBytesToFlush = std::min( m_decodedBufferPos, maxBytesToFlush );
    size_t nBytesFlushed = nBytesToFlush; // default then there is neither output buffer nor file device given

    if ( outputFileDescriptor >= 0 ) {
        const auto nBytesWritten = write( outputFileDescriptor, m_decodedBuffer.data(), nBytesToFlush );
        nBytesFlushed = std::max<decltype( nBytesWritten )>( 0, nBytesWritten );
    }

    if ( outputBuffer != nullptr ) {
        std::memcpy( outputBuffer, m_decodedBuffer.data(), nBytesFlushed );
    }

    if ( nBytesFlushed > 0 ) {
        m_decodedBytesCount += nBytesFlushed;
        m_decodedBufferPos  -= nBytesFlushed;
        std::memmove( m_decodedBuffer.data(), m_decodedBuffer.data() + nBytesFlushed, m_decodedBufferPos );
    }

    return nBytesFlushed;
}


inline size_t
BZ2Reader::decodeStream( int    const outputFileDescriptor,
                         char*  const outputBuffer,
                         size_t const nMaxBytesToDecode )
{
    if ( eof() || ( nMaxBytesToDecode == 0 ) ) {
        return 0;
    }

    /* try to flush remnants in output buffer from interrupted last call */
    size_t nBytesDecoded = flushOutputBuffer( outputFileDescriptor, outputBuffer, nMaxBytesToDecode );

    while ( nBytesDecoded < nMaxBytesToDecode ) {
        /* If we need to refill dbuf, do it. Only won't be required for resuming interrupted decodations. */
        if ( m_lastHeader.bwdata.writeCount == 0 ) {
            m_lastHeader = readBlockHeader( m_bitReader.tell() );
            if ( m_lastHeader.eos() ) {
                return nBytesDecoded;
            }
            m_lastHeader.readBlockData();
        }

        /* m_decodedBufferPos should either be cleared by flush after or by flush before while!
         * It might happen that this is not the case when, e.g., the output file descriptor can't be written to.
         * However, if this happens, nBytesDecoded is very likely to not grow anymore and thereby we have to
         * throw to exit the infinite loop. */
        if ( m_decodedBufferPos > 0 ) {
            throw std::runtime_error( "[BZ2Reader::decodeStream] Could not write any of the decoded bytes to the "
                                      "file descriptor or buffer!" );
        }

        /* the max bytes to decode does not account for copies caused by RLE!
         * There can be at maximum 255 copies! */
        assert( m_decodedBuffer.size() >= 255 );
        const auto nBytesToDecode = std::min( m_decodedBuffer.size() - 255, nMaxBytesToDecode - nBytesDecoded );
        m_decodedBufferPos = m_lastHeader.bwdata.decodeBlock( nBytesToDecode, m_decodedBuffer.data() );

        if ( ( m_lastHeader.bwdata.writeCount == 0 ) && !m_blockToDataOffsetsComplete ) {
            m_calculatedStreamCRC = ( ( m_calculatedStreamCRC << 1 ) | ( m_calculatedStreamCRC >> 31 ) )
                                    ^ m_lastHeader.bwdata.dataCRC;
        }

        /* required for correct data offsets in readBlockHeader and for while condition of course */
        nBytesDecoded += flushOutputBuffer( outputFileDescriptor,
                                            outputBuffer == nullptr ? nullptr : outputBuffer + nBytesDecoded,
                                            nMaxBytesToDecode - nBytesDecoded );
    }

    return nBytesDecoded;
}
