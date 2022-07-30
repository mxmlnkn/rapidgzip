#pragma once

#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>

#include <DecodedData.hpp>
#include <filereader/FileReader.hpp>
#include <filereader/Standard.hpp>
#include <pragzip.hpp>

#ifdef WITH_PYTHON_SUPPORT
    #include <filereader/Python.hpp>
#endif


namespace pragzip
{
enum StoppingPoint : uint32_t
{
    NONE                 = 0,
    END_OF_STREAM_HEADER = 1 << 0,
    END_OF_STREAM        = 1 << 1,  // after gzip footer has been read
    END_OF_BLOCK_HEADER  = 1 << 2,
    END_OF_BLOCK         = 1 << 3,
    ALL                  = 0xFFFF'FFFFU,
};


/**
 * A strictly sequential gzip interface that can iterate over multiple gzip streams and of course deflate blocks.
 * It cannot seek back nor is it parallelized but it can be used to implement a parallelization scheme.
 */
template<bool CALCULATE_CRC32 = false>
class GzipReader :
    public FileReader
{
public:
    using DeflateBlock = typename deflate::Block<CALCULATE_CRC32>;

public:
    explicit
    GzipReader( std::unique_ptr<FileReader> fileReader ) :
        m_bitReader( std::move( fileReader ) )
    {}

#ifdef WITH_PYTHON_SUPPORT
    explicit
    GzipReader( const std::string& filePath ) :
        m_bitReader( std::make_unique<StandardFileReader>( filePath ) )
    {}

    explicit
    GzipReader( int fileDescriptor ) :
        m_bitReader( std::make_unique<StandardFileReader>( fileDescriptor ) )
    {}

    explicit
    GzipReader( PyObject* pythonObject ) :
        m_bitReader( std::make_unique<PythonFileReader>( pythonObject ) )
    {}
#endif

    /* FileReader finals */

    [[nodiscard]] FileReader*
    clone() const final
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] int
    fileno() const final
    {
        return m_bitReader.fileno();
    }

    [[nodiscard]] bool
    seekable() const final
    {
        return m_bitReader.seekable();
    }

    void
    close() final
    {
        m_bitReader.close();
    }

    [[nodiscard]] bool
    closed() const final
    {
        return m_bitReader.closed();
    }

    [[nodiscard]] bool
    eof() const final
    {
        return m_atEndOfFile;
    }

    [[nodiscard]] bool
    fail() const final
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] size_t
    tell() const final
    {
        if ( m_atEndOfFile ) {
            return size();
        }
        return m_currentPosition;
    }

    [[nodiscard]] size_t
    size() const final
    {
        if ( m_atEndOfFile ) {
            return m_currentPosition;
        }

        throw std::invalid_argument( "Can't get stream size when not finished reading at least once!" );
    }

    size_t
    seek( long long int /* offset */,
          int           /* origin */ = SEEK_SET ) final
    {
        throw std::logic_error( "Not implemented (yet)!" );
    }

    void
    clearerr() final
    {
        m_bitReader.clearerr();
        m_atEndOfFile = false;
        throw std::invalid_argument( "Not fully tested!" );
    }

    [[nodiscard]] size_t
    read( char*  outputBuffer,
          size_t nBytesToRead ) final
    {
        return read( -1,  outputBuffer, nBytesToRead );
    }


    /* Gzip specific methods */

    /**
     * @return number of processed bits of compressed bzip2 input file stream
     * @note Bzip2 is block based and blocks are currently read fully, meaning that the granularity
     *       of the returned position is ~100-900kB. It's only useful for a rough estimate.
     */
    [[nodiscard]] size_t
    tellCompressed() const
    {
        return m_bitReader.tell();
    }

    [[nodiscard]] std::optional<StoppingPoint>
    currentPoint() const
    {
        return m_currentPoint;
    }

    [[nodiscard]] const auto&
    currentDeflateBlock() const
    {
        return m_currentDeflateBlock;
    }

    /**
     * @param[out] outputBuffer should at least be large enough to hold @p nBytesToRead bytes
     * @return number of bytes written
     */
    size_t
    read( const int     outputFileDescriptor = -1,
          char* const   outputBuffer         = nullptr,
          const size_t  nBytesToRead         = std::numeric_limits<size_t>::max(),
          StoppingPoint stoppingPoint        = StoppingPoint::NONE )
    {
        size_t nBytesDecoded = 0;

        /* This loop is basically a state machine over m_currentPoint and will process different things
         * depending on m_currentPoint and after each processing step it needs to recheck for EOF!
         * First read metadata so that even with nBytesToRead == 0, the position can be advanced over those. */
        while ( !m_bitReader.eof() && !eof() ) {
            if ( !m_currentPoint.has_value() || ( *m_currentPoint == StoppingPoint::END_OF_BLOCK_HEADER ) ) {
                const auto nBytesDecodedInStep = readBlock(
                    outputFileDescriptor,
                    outputBuffer == nullptr ? nullptr : outputBuffer + nBytesDecoded,
                    nBytesToRead - nBytesDecoded );

                nBytesDecoded += nBytesDecodedInStep;
                m_streamBytesCount += nBytesDecodedInStep;

                /* After this call to readBlock, m_currentPoint is either unchanged END_OF_BLOCK_HEADER,
                 * std::nullopt (block not fully read) or END_OF_BLOCK. In the last case, we should try to read
                 * possible gzip footers and headers even if we already have the requested amount of bytes. */

                if ( !m_currentPoint.has_value() || ( *m_currentPoint == StoppingPoint::END_OF_BLOCK_HEADER ) ) {
                    if ( nBytesDecoded >= nBytesToRead ) {
                        break;
                    }

                    if ( nBytesDecodedInStep == 0 ) {
                        /* We did not advance after the readBlock call and did not even read any amount of bytes.
                         * Something went wrong with flushing. Break to avoid infinite loop. */
                        break;
                    }
                }
            } else {
                /* This else branch only handles headers and footers and will always advance
                 * the current point while not actually decoding any bytes. */
                switch ( *m_currentPoint )
                {
                case StoppingPoint::NONE:
                case StoppingPoint::END_OF_STREAM:
                    readGzipHeader();
                    break;

                case StoppingPoint::END_OF_STREAM_HEADER:
                case StoppingPoint::END_OF_BLOCK:
                    if ( m_currentDeflateBlock.has_value() && m_currentDeflateBlock->eos() ) {
                        readGzipFooter();
                    } else {
                        readBlockHeader();
                    }
                    break;

                case StoppingPoint::END_OF_BLOCK_HEADER:
                    assert( false && "Should have been handled before the switch!" );
                    break;

                case StoppingPoint::ALL:
                    assert( false && "Should only be specified by the user not appear internally!" );
                    break;
                }
            }

            if ( m_currentPoint.has_value() && testFlags( *m_currentPoint, stoppingPoint ) ) {
                break;
            }
        }

        m_currentPosition += nBytesDecoded;
        return nBytesDecoded;
    }

private:
    /**
     * @note Only to be used by readBlock!
     * @param  outputBuffer A char* to which the data is written.
     *                      You should ensure that at least @p maxBytesToFlush bytes can fit there!
     * @return The number of actually flushed bytes, which might be hindered,
     *         e.g., if the output file descriptor can't be written to!
     */
    size_t
    flushOutputBuffer( int    outputFileDescriptor = -1,
                       char*  outputBuffer         = nullptr,
                       size_t maxBytesToFlush      = std::numeric_limits<size_t>::max() );

    void
    readBlockHeader()
    {
        if ( !m_currentDeflateBlock.has_value() ) {
            throw std::logic_error( "Call readGzipHeader before calling readBlockHeader!" );
        }
        const auto error = m_currentDeflateBlock->readHeader( m_bitReader );
        if ( error != pragzip::Error::NONE ) {
            std::stringstream message;
            message << "Encountered error: " << pragzip::toString( error ) << " while trying to read deflate header!";
            throw std::domain_error( std::move( message ).str() );
        }
        m_currentPoint = StoppingPoint::END_OF_BLOCK_HEADER;
    }

    /**
     * Decodes data from @ref m_currentDeflateBlock and writes it to the file descriptor and/or the output buffer.
     * It will either return when the whole block has been read or when the requested amount of bytes has been read.
     */
    [[nodiscard]] size_t
    readBlock( int    outputFileDescriptor,
               char*  outputBuffer,
               size_t nMaxBytesToDecode );

    void
    readGzipHeader()
    {
        const auto [header, error] = pragzip::gzip::readHeader( m_bitReader );
        if ( error != pragzip::Error::NONE ) {
            std::stringstream message;
            message << "Encountered error: " << pragzip::toString( error ) << " while trying to read gzip header!";
            throw std::domain_error( std::move( message ).str() );
        }

        m_lastGzipHeader = std::move( header );
        m_currentDeflateBlock.emplace();
        m_currentDeflateBlock->setInitialWindow();
        m_streamBytesCount = 0;
        m_currentPoint = StoppingPoint::END_OF_STREAM_HEADER;
    }

    void
    readGzipFooter();

    [[nodiscard]] bool
    bufferHasBeenFlushed() const
    {
        return !m_offsetInLastBuffers.has_value();
    }

    [[nodiscard]] bool
    endOfStream() const
    {
        return !m_currentDeflateBlock.has_value() || !m_currentDeflateBlock->isValid()
               || ( bufferHasBeenFlushed() && m_currentDeflateBlock->eos() );
    }

protected:
    pragzip::BitReader m_bitReader;

    size_t m_currentPosition{ 0 }; /** the current position as can only be modified with read or seek calls. */
    bool m_atEndOfFile{ false };

private:
    pragzip::gzip::Header m_lastGzipHeader;
    /**
     * The deflate block will be reused during a gzip stream because each block depends on the last output
     * of the previous block. But after the gzip stream end, this optional will be cleared and in case of
     * another concatenated gzip stream, it will be created anew.
     */
    std::optional<DeflateBlock> m_currentDeflateBlock;
    /** Holds non-owning views to the data decoded in the last call to m_currentDeflateBlock.read. */
    deflate::DecodedDataView m_lastBlockData;

    /**
     * If m_currentPoint has no value, then it means it is currently inside a deflate block.
     * Because a gzip file can contain multiple streams, the file beginning can generically be treated
     * as being at the end of a previous (empty) stream.
     * m_currentPoint may only every have exactly one StoppingPoint set, it may not contain or'ed values!
     */
    std::optional<StoppingPoint> m_currentPoint{ END_OF_STREAM };

    size_t m_streamBytesCount{ 0 };

    /* These are necessary states to return partial results and resume returning further ones.
     * I.e., things which would not be necessary with coroutines supports. This optional has no value
     * iff there is no current deflate block or if we have read all data from it already. */
    std::optional<size_t> m_offsetInLastBuffers;
};


template<bool CALCULATE_CRC32>
inline size_t
GzipReader<CALCULATE_CRC32>::flushOutputBuffer( int    const outputFileDescriptor,
                                                char*  const outputBuffer,
                                                size_t const maxBytesToFlush )
{
    if ( !m_offsetInLastBuffers.has_value()
         || !m_currentDeflateBlock.has_value()
         || !m_currentDeflateBlock->isValid() ) {
        return 0;
    }

    size_t totalBytesFlushed = 0;
    size_t bufferOffset = 0;
    for ( const auto& buffer : m_lastBlockData.data ) {
        if ( ( *m_offsetInLastBuffers >= bufferOffset ) && ( *m_offsetInLastBuffers < bufferOffset + buffer.size() ) ) {
            const auto offsetInBuffer = *m_offsetInLastBuffers - bufferOffset;
            const auto nBytesToWrite = std::min( buffer.size() - offsetInBuffer, maxBytesToFlush - totalBytesFlushed );
            const auto* const bufferStart = buffer.data() + offsetInBuffer;

            size_t nBytesFlushed = nBytesToWrite;  // default when there is neither output buffer nor file device given

            if ( outputFileDescriptor >= 0 ) {
                const auto nBytesWritten = write( outputFileDescriptor, bufferStart, nBytesToWrite );
                nBytesFlushed = std::max<decltype( nBytesWritten )>( 0, nBytesWritten );
            }

            if ( outputBuffer != nullptr ) {
                std::memcpy( outputBuffer + totalBytesFlushed, bufferStart, nBytesFlushed );
            }

            *m_offsetInLastBuffers += nBytesFlushed;
            totalBytesFlushed += nBytesFlushed;

            if ( nBytesFlushed != nBytesToWrite ) {
                break;
            }
        }

        bufferOffset += buffer.size();
    }

    /* Reset optional offset if end has been reached. */
    size_t totalBufferSize = 0;
    for ( const auto& buffer : m_lastBlockData.data ) {
        totalBufferSize += buffer.size();
    }
    if ( m_offsetInLastBuffers >= totalBufferSize ) {
        m_offsetInLastBuffers = std::nullopt;
    }

    return totalBytesFlushed;
}


template<bool CALCULATE_CRC32>
inline size_t
GzipReader<CALCULATE_CRC32>::readBlock( int    const outputFileDescriptor,
                                        char*  const outputBuffer,
                                        size_t const nMaxBytesToDecode )
{
    if ( eof() || ( nMaxBytesToDecode == 0 ) ) {
        return 0;
    }

    /* Try to flush remnants in output buffer from interrupted last call. */
    size_t nBytesDecoded = flushOutputBuffer( outputFileDescriptor, outputBuffer, nMaxBytesToDecode );
    if ( !bufferHasBeenFlushed() ) {
        return nBytesDecoded;
    }

    while ( true ) {
        if ( bufferHasBeenFlushed() ) {
            if ( !m_currentDeflateBlock.has_value() || !m_currentDeflateBlock->isValid() ) {
                throw std::logic_error( "Call readGzipHeader and readBlockHeader before calling readBlock!" );
            }

            if ( m_currentDeflateBlock->eob() ) {
                m_currentPoint = StoppingPoint::END_OF_BLOCK;
                return nBytesDecoded;
            }

            /* Decode more data from current block. It can then be accessed via Block::lastBuffers. */
            auto error = Error::NONE;
            std::tie( m_lastBlockData, error ) =
                m_currentDeflateBlock->read( m_bitReader, std::numeric_limits<size_t>::max() );
            if ( error != pragzip::Error::NONE ) {
                std::stringstream message;
                message << "Encountered error: " << pragzip::toString( error ) << " while decompressing deflate block.";
                throw std::domain_error( std::move( message ).str() );
            }

            if ( ( m_lastBlockData.size() == 0 ) && !m_currentDeflateBlock->eob() ) {
                throw std::logic_error( "Could not read anything so it should be the end of the block!" );
            }
            m_offsetInLastBuffers = 0;
        }

        if ( nBytesDecoded >= nMaxBytesToDecode ) {
            break;
        }

        m_currentPoint = {};

        const auto flushedCount = flushOutputBuffer(
            outputFileDescriptor,
            outputBuffer == nullptr ? nullptr : outputBuffer + nBytesDecoded,
            nMaxBytesToDecode - nBytesDecoded );

        if ( ( flushedCount == 0 ) && !bufferHasBeenFlushed() ) {
            /* Something went wrong with flushing and this would lead to an infinite loop. */
            break;
        }
        nBytesDecoded += flushedCount;
    }

    return nBytesDecoded;
}


template<bool CALCULATE_CRC32>
inline void
GzipReader<CALCULATE_CRC32>::readGzipFooter()
{
    const auto footer = pragzip::gzip::readFooter( m_bitReader );

    if ( static_cast<uint32_t>( m_streamBytesCount ) != footer.uncompressedSize ) {
        std::stringstream message;
        message << "Mismatching size (" << static_cast<uint32_t>( m_streamBytesCount ) << " <-> footer: "
                << footer.uncompressedSize << ") for gzip stream!";
        throw std::domain_error( std::move( message ).str() );
    }

    if ( !m_currentDeflateBlock.has_value() || !m_currentDeflateBlock->isValid() ) {
        /* A gzip stream should at least contain an end-of-stream block! */
        throw std::logic_error( "Call readGzipHeader and readBlockHeader before readGzipFooter!" );
    }

    if ( ( m_currentDeflateBlock->crc32() != 0 ) && ( m_currentDeflateBlock->crc32() != footer.crc32 ) ) {
        std::stringstream message;
        message << "Mismatching CRC32 (0x" << std::hex << m_currentDeflateBlock->crc32()
                << " <-> stored: 0x" << footer.crc32 << ") for gzip stream!";
        throw std::domain_error( std::move( message ).str() );
    }

    if ( m_bitReader.eof() ) {
        m_atEndOfFile = true;
    }

    m_currentPoint = StoppingPoint::END_OF_STREAM;
}


[[nodiscard]] std::string
toString( StoppingPoint stoppingPoint )
{
    switch ( stoppingPoint )
    {
    case StoppingPoint::NONE                 : return "None";
    case StoppingPoint::END_OF_STREAM_HEADER : return "End of Stream Header";
    case StoppingPoint::END_OF_STREAM        : return "End of Stream";
    case StoppingPoint::END_OF_BLOCK_HEADER  : return "End of Block Header";
    case StoppingPoint::END_OF_BLOCK         : return "End of Block";
    case StoppingPoint::ALL                  : return "All";
    };
    return "Unknown";
}
}  // namespace pragzip
