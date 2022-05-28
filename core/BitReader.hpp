#pragma once

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <cassert>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <sys/stat.h>

#include "BitManipulation.hpp"
#include "common.hpp"
#include "FileReader.hpp"
#include "StandardFileReader.hpp"
#include "SharedFileReader.hpp"


/**
 * @todo Make BitReader access with copying and such work for input stream.
 *       This might need another abstraction layer which keeps chunks of data of the file until it has been read once!
 *       Normally, the BZ2 reader should indeed read everything once, meaning nothing should "leak".
 *       That abstraction layer would also open the file only a single time and then hold a mutex locked shared_ptr
 *       to it.
 * This bit reader, returns bits in the order appropriate for bzip2, i.e., it goes over all bytes in order
 * and then returns the bits of each byte >starting from the most significant<. This is contrary to the usual
 * bit numbering starting from the least-significant one and also contrary to to DEFLATE (RFC 1951)!
 * @see https://github.com/dsnet/compress/blob/master/doc/bzip2-format.pdf
 * @see https://tools.ietf.org/html/rfc1951
 *
 * Slowdowns when using a 64-bit or 16-bit (they are both similarly slow) vs. 32-bit buffer:
 *  - serial bzip2 decoding: 20%
 *  - parallel (24x) bzip2 decoding: 40%
 */
template<bool MOST_SIGNIFICANT_BITS_FIRST = true,
         typename BitBuffer = uint32_t>
class BitReader :
    public FileReader
{
public:
    /**
     * If it is too large, then the use case of only reading one Bzip2 block per opened BitReader
     * will load much more data than necessary because of the too large buffer.
     * The size should also be a multiple of the block size of the underlying device.
     * Any power of 2 larger than 4096 (4k blocks) should be safe bet.
     */
    static constexpr size_t IOBUF_SIZE = 128*1024;
    static constexpr int NO_FILE = -1;
    static constexpr auto MAX_BIT_BUFFER_SIZE = std::numeric_limits<BitBuffer>::digits;

public:
    explicit
    BitReader( std::string filePath ) :
        m_file( std::make_unique<SharedFileReader>( new StandardFileReader( filePath ) ) )
    {}

    /**
     * @param fileReader ownership is taken.
     */
    explicit
    BitReader( FileReader* fileReader ) :
        m_file( std::make_unique<SharedFileReader>( fileReader ) )
    {}

    explicit
    BitReader( std::unique_ptr<FileReader> fileReader ) :
        m_file( std::make_unique<SharedFileReader>( std::move( fileReader ) ) )
    {}

    BitReader( BitReader&& other ) = delete;
    BitReader& operator=( BitReader&& other ) = delete;
    BitReader& operator=( const BitReader& other ) = delete;

    BitReader( const BitReader& other ) :
        m_file( other.m_file ? other.m_file->clone() : nullptr ),
        m_inputBuffer( other.m_inputBuffer )
    {
        assert( static_cast<bool>( m_file ) == static_cast<bool>( other.m_file ) );
        if ( m_file && !m_file->seekable() ) {
            throw std::invalid_argument( "Copying BitReader to unseekable file not supported yet!" );
        }
        seek( other.tell() );
    }

    [[nodiscard]] std::unique_ptr<FileReader>
    cloneSharedFileReader() const
    {
        return std::unique_ptr<FileReader>( m_file->clone() );
    }

    /* File Reader Interface Implementation */

    [[nodiscard]] FileReader*
    clone() const override final
    {
        return new BitReader( *this );
    }

    [[nodiscard]] bool
    fail() const override final
    {
        throw std::logic_error( "Not implemented" );
    }

    [[nodiscard]] bool
    eof() const override final
    {
        if ( seekable() ) {
            return tell() >= size();
        }
        return ( m_inputBufferPosition >= m_inputBuffer.size() ) && ( !m_file || m_file->eof() );
    }

    [[nodiscard]] bool
    seekable() const override final
    {
        return !m_file || m_file->seekable();
    }

    void
    close() override final
    {
        m_file.reset();
        m_inputBuffer.clear();
    }

    [[nodiscard]] bool
    closed() const override final
    {
        return !m_file && m_inputBuffer.empty();
    }

    /* Bit Reading Methods */

    /**
     * Forcing to inline this function is superimportant because it depends even between gcc versions,
     * whether it is actually inlined or not but inlining can save 30%!
     */
    BitBuffer
    forceinline read( uint8_t bitsWanted );

    uint64_t
    read64( uint8_t bitsWanted )
    {
        if ( bitsWanted <= 32 ) {
            return read( bitsWanted );
        }

        if ( bitsWanted > 64 ) {
            throw std::invalid_argument( "Can't return this many bits in a 64-bit integer!" );
        }


        uint64_t result = 0;
        constexpr uint8_t maxReadSize = 32;
        for ( auto bitsRead = 0; bitsRead < bitsWanted; bitsRead += maxReadSize ) {
            const auto bitsToRead = bitsWanted - bitsRead < maxReadSize
                                    ? bitsWanted - bitsRead
                                    : maxReadSize;
            assert( bitsToRead >= 0 );
            result <<= static_cast<uint8_t>( bitsToRead );
            result |= static_cast<uint64_t>( read( bitsToRead ) );
        }

        return result;
    }

    /**
     * Forcing to inline this function is superimportant because it depends even between gcc versions,
     * whether it is actually inlined or not but inlining can save 30%!
     */
    template<uint8_t bitsWanted>
    forceinline BitBuffer
    read()
    {
        static_assert( bitsWanted <= MAX_BIT_BUFFER_SIZE, "Requested bits must fit in buffer!" );

        if ( bitsWanted <= m_bitBufferSize ) {
            if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
                m_bitBufferSize -= bitsWanted;
                return ( m_bitBuffer >> m_bitBufferSize ) & nLowestBitsSet<decltype( m_bitBuffer )>( bitsWanted );
            } else {
                const auto result = ( m_bitBuffer >> ( MAX_BIT_BUFFER_SIZE - m_bitBufferSize ) )
                                    & nLowestBitsSet<decltype( m_bitBuffer )>( bitsWanted );
                m_bitBufferSize -= bitsWanted;
                return result;
            }
        }

        return readSafe( bitsWanted );
    }

    [[nodiscard]] size_t
    read( char*  outputBuffer,
          size_t nBytesToRead ) override
    {
        const auto oldTell = tell();
        for ( size_t i = 0; i < nBytesToRead; ++i ) {
            outputBuffer[i] = static_cast<char>( read( CHAR_BIT ) );
        }
        return tell() - oldTell;
    }

    /**
     * @return current position / number of bits already read.
     */
    [[nodiscard]] size_t
    tell() const override final
    {
        size_t position = tellBuffer();
        if ( m_file ) {
            const auto filePosition = m_file->tell();
            if ( static_cast<size_t>( filePosition ) < m_inputBuffer.size() ) {
                throw std::logic_error( "The byte buffer should not contain more data than the file position!" );
            }
            position += ( filePosition - m_inputBuffer.size() ) * CHAR_BIT;
        }
        return position;
    }

    void
    clearerr() override
    {
        if ( m_file ) {
            m_file->clearerr();
        }
    }

public:
    [[nodiscard]] int
    fileno() const override final
    {
        if ( m_file ) {
            return m_file->fileno();
        }
        throw std::invalid_argument( "The file is not open!" );
    }

    size_t
    seek( long long int offsetBits,
          int           origin = SEEK_SET ) override final;

    [[nodiscard]] size_t
    size() const override final
    {
        return ( m_file ? m_file->size() : m_inputBuffer.size() ) * CHAR_BIT;
    }

    [[nodiscard]] const std::vector<std::uint8_t>&
    buffer() const
    {
        return m_inputBuffer;
    }

private:
    [[nodiscard]] size_t
    tellBuffer() const
    {
        size_t position = m_inputBufferPosition * CHAR_BIT;
        if ( position < m_bitBufferSize ) {
            std::logic_error( "The bit buffer should not contain data if the byte buffer doesn't!" );
        }
        return position - m_bitBufferSize;
    }

    BitBuffer
    readSafe( uint8_t );

    void
    refillBuffer()
    {
        if ( !m_file ) {
            throw std::logic_error( "Can not refill buffer with data from non-existing file!" );
        }

        const auto oldBufferSize = m_inputBuffer.size();
        m_inputBuffer.resize( IOBUF_SIZE );
        const auto nBytesRead = m_file->read( reinterpret_cast<char*>( m_inputBuffer.data() ), m_inputBuffer.size() );
        if ( nBytesRead == 0 ) {
            m_inputBuffer.resize( oldBufferSize );
            return;
        }

        m_inputBuffer.resize( nBytesRead );
        m_inputBufferPosition = 0;
    }

    void
    refillBitBuffer()
    {
        if ( m_bitBufferSize != 0 ) {
            throw std::invalid_argument( "Will only refill empty bit buffers!" );
        }

        /* Refill buffer one byte at a time to enforce endianness and avoid unaligned access. */
        m_bitBuffer = 0;
        for ( ; m_bitBufferSize + CHAR_BIT <= std::numeric_limits<decltype( m_bitBuffer )>::digits;
              m_bitBufferSize += CHAR_BIT )
        {
            if ( m_inputBufferPosition >= m_inputBuffer.size() ) {
                refillBuffer();
                if ( m_inputBufferPosition >= m_inputBuffer.size() ) {
                    break;
                }
            }

            if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
                m_bitBuffer <<= CHAR_BIT;
                m_bitBuffer |= static_cast<BitBuffer>( m_inputBuffer[m_inputBufferPosition++] );
            } else {
                m_bitBuffer |= ( static_cast<BitBuffer>( m_inputBuffer[m_inputBufferPosition++] )
                               << m_bitBufferSize );
            }
        }

        m_originalBitBufferSize = m_bitBufferSize;

        /* Move LSB bits (which are filled left-to-right) to the left if so necessary
         * so that the format is the same as for MSB bits! */
        if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST ) {
            const auto leftPadding = MAX_BIT_BUFFER_SIZE - m_bitBufferSize;
            if ( leftPadding > 0 ) {
                m_bitBuffer <<= leftPadding;
            }
        }
    }

    void
    clearBitBuffer()
    {
        m_originalBitBufferSize = 0;
        m_bitBufferSize = 0;
        m_bitBuffer = 0;
    }

private:
    std::unique_ptr<FileReader> m_file;

    std::vector<uint8_t> m_inputBuffer;
    size_t m_inputBufferPosition = 0; /** stores current position of first valid byte in buffer */

public:
    /**
     * For MOST_SIGNIFICANT_BITS_FIRST == true (bzip2):
     *
     * m_bitBuffer stores the last read bits from m_inputBuffer on the >right side< (if not fully filled).
     * The bits are to be read from >left to right< up to a maximum of m_bitBufferSize.
     * This means that not the least significant n bits are to be returned on read but the most significant.
     * E.g., for the following example requesting 3 bits should return 011, i.e., start from m_bitBufferSize
     * and return the requested amount of bits to the right of it. After that, decrement m_bitBufferSize.
     *
     * @verbatim
     *        result = 0b011
     *        bitsWanted = 3
     *            <->
     * +-------------------+
     * |    | 101|0111|0011|
     * +-------------------+
     *        ^   ^  ^
     *        |   |  m_bitBufferSize - bitsWanted = 5 = m_bitBufferSize after reading bitsWanted
     *        |   m_bitBufferSize = 8
     *        m_originalBitBufferSize = 11
     * @endverbatim
     *
     * For MOST_SIGNIFICANT_BITS_FIRST == false (gzip):
     *
     * Bit buffer stores the last read bits from m_inputBuffer on the >left side< of m_bitBuffer (if not fully filled).
     * Basically, this works in a mirrored way of MOST_SIGNIFICANT_BITS_FIRST == true in order to be able to only
     * require one size and not an additional offset position for the bit buffer.
     * Because the bits are to be read from >right to left<, the left-alignment makes the left-most bits always
     * those valid the longest. I.e., we only have to decrement m_bitBufferSize.
     *
     * @verbatim
     *   result = 0b111
     *   bitsWanted = 3
     *        <->
     * +-------------------+
     * |0101|0111|001 |    |
     * +-------------------+
     *       ^  ^   ^
     *       |  |   m_originalBitBufferSize = 11
     *       |  m_bitBufferSize = 8
     *       m_bitBufferSize - bitsWanted = 5
     * @endverbatim
     *
     * In both cases, the amount of bits wanted are extracted by shifting to the right and and'ing with a bit mask.
     */
    BitBuffer m_bitBuffer = 0;
    uint8_t m_bitBufferSize = 0; // size of bitbuffer in bits
    uint8_t m_originalBitBufferSize = 0; // size of valid bitbuffer bits including already read ones
};


/**
 * Note that splitting part of this method off into readSafe made the compiler actually
 * inline this now small function and thereby sped up runtimes significantly!
 */
template<bool MOST_SIGNIFICANT_BITS_FIRST, typename BitBuffer>
inline BitBuffer
BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>::read( const uint8_t bitsWanted )
{
    if ( bitsWanted <= m_bitBufferSize ) {
        if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
            m_bitBufferSize -= bitsWanted;
            return ( m_bitBuffer >> m_bitBufferSize ) & nLowestBitsSet<decltype( m_bitBuffer )>( bitsWanted );
        } else {
            const auto result = ( m_bitBuffer >> ( MAX_BIT_BUFFER_SIZE - m_bitBufferSize ) )
                                & nLowestBitsSet<decltype( m_bitBuffer )>( bitsWanted );
            m_bitBufferSize -= bitsWanted;
            return result;
        }
    }

    return readSafe( bitsWanted );
}


template<bool MOST_SIGNIFICANT_BITS_FIRST, typename BitBuffer>
inline BitBuffer
BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>::readSafe( const uint8_t bitsWanted )
{
    assert( bitsWanted > m_bitBufferSize );

    const auto bitsInResult = m_bitBufferSize;
    auto bits = read( m_bitBufferSize );
    const auto bitsNeeded = bitsWanted - bitsInResult;
    assert( bitsWanted <= std::numeric_limits<decltype( bits )>::digits );

    refillBitBuffer();

    if ( bitsNeeded > m_bitBufferSize ) {
        // this will also happen for invalid file descriptor -1
        std::stringstream msg;
        msg
        << "[BitReader] Not enough data for requested bits!\n"
        << "  Bits requested    : " << (int)bitsWanted << "\n"
        << "  Bits already read : " << (int)bitsInResult << "\n"
        << "  Bits still needed : " << (int)bitsNeeded << "\n"
        << "  File position     : " << m_file->tell() << "\n"
        << "  File size         : " << m_file->size() << "B\n"
        << "  Input buffer size : " << m_inputBuffer.size() << "B\n"
        << "  EOF               : " << m_file->eof() << "\n"
        << "  Error             : " << m_file->fail() << "\n"
        << "\n";
        throw std::domain_error( msg.str() );
    }

    /* Append remaining requested bits. */
    if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
        bits = ( bits << bitsNeeded ) | read( bitsNeeded );
    } else {
        bits |= read( bitsNeeded ) << bitsInResult;
    }

    /* Check that no bits after the bitsWanted-th lowest bit are set! I.e., that no junk is returned. */
    assert( bits == ( bits & nLowestBitsSet<decltype( bits )>( bitsWanted ) ) );
    return bits;
}


template<bool MOST_SIGNIFICANT_BITS_FIRST, typename BitBuffer>
inline size_t
BitReader<MOST_SIGNIFICANT_BITS_FIRST, BitBuffer>::seek(
    long long int offsetBits,
    int           origin )
{
    switch ( origin )
    {
    case SEEK_CUR:
        offsetBits = tell() + offsetBits;
        break;
    case SEEK_SET:
        break;
    case SEEK_END:
        offsetBits = size() + offsetBits;
        break;
    }

    if ( offsetBits < 0 ) {
        throw std::invalid_argument( "Effective offset is before file start!" );
    }

    if ( static_cast<size_t>( offsetBits ) == tell() ) {
        return static_cast<size_t>( offsetBits );
    }

    if ( static_cast<size_t>( offsetBits ) >= size() ) {
        throw std::invalid_argument( "Effective offset is after file end!" );
    }

    if ( !seekable() && ( static_cast<size_t>( offsetBits ) < tell() ) ) {
        throw std::invalid_argument( "File is not seekable!" );
    }

    /* Currently, buffer-only is not supported, use BufferedFileReader as a memory-only file reader! */
    if ( !m_file ) {
        throw std::logic_error( "File has already been closed!" );
    }

    /* Performance optimizations for faster seeking inside the buffer to avoid expensive refillBuffer calls. */
    const auto relativeOffsets = offsetBits - static_cast<long long int>( tell() );
    if ( relativeOffsets >= 0 ) {
        if ( relativeOffsets <= m_bitBufferSize ) {
            m_bitBufferSize -= relativeOffsets;
            return static_cast<size_t>( offsetBits );
        }

        if ( tellBuffer() + relativeOffsets <= m_inputBuffer.size() ) {
            auto stillToSeek = relativeOffsets - m_bitBufferSize;
            clearBitBuffer();

            m_inputBufferPosition += stillToSeek / CHAR_BIT;
            read( stillToSeek % CHAR_BIT );

            return static_cast<size_t>( offsetBits );
        }
    } else {
        if ( static_cast<size_t>( -relativeOffsets ) + m_bitBufferSize <= m_originalBitBufferSize ) {
            m_bitBufferSize += -relativeOffsets;
            return static_cast<size_t>( offsetBits );
        }

        const auto seekBackWithBuffer = -relativeOffsets + m_bitBufferSize;
        const auto bytesToSeekBack = static_cast<size_t>( ceilDiv( seekBackWithBuffer, CHAR_BIT ) );
        if ( bytesToSeekBack <= m_inputBufferPosition ) {
            m_inputBufferPosition -= bytesToSeekBack;
            clearBitBuffer();

            const auto bitsToSeekForward = bytesToSeekBack * CHAR_BIT - seekBackWithBuffer;
            if ( bitsToSeekForward > 0 ) {
                read( bitsToSeekForward );
            }

            return static_cast<size_t>( offsetBits );
        }
    }

    /* Do a full-fledged seek. */

    const auto bytesToSeek = static_cast<size_t>( offsetBits ) >> 3U;
    const auto subBitsToSeek = static_cast<uint8_t>( static_cast<size_t>( offsetBits ) & 7U );

    clearBitBuffer();

    m_inputBuffer.clear();
    m_inputBufferPosition = 0;

    if ( seekable() ) {
        const auto newPosition = m_file->seek( static_cast<long long int>( bytesToSeek ), SEEK_SET );
        if ( m_file->eof() || m_file->fail() ) {
            std::stringstream msg;
            msg << "[BitReader] Could not seek to specified byte " << bytesToSeek
            << " subbit " << subBitsToSeek
            << ", size: " << m_file->size()
            << ", feof: " << m_file->eof()
            << ", ferror: " << m_file->fail()
            << ", newPosition: " << newPosition;
            throw std::invalid_argument( msg.str() );
        }
    } else if ( static_cast<size_t>( offsetBits ) < tell() ) {
        throw std::logic_error( "Can not emulate backward seeking on non-seekable file!" );
    } else {
        /* Emulate forward seeking on non-seekable file by reading. */
        throw std::logic_error( "Seeking forward on non-seekable input is an unfinished feature!" );
    }

    if ( subBitsToSeek > 0 ) {
        read( subBitsToSeek );
    }

    return offsetBits;
}
