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
 */
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

    BitReader( BitReader&& other ) = delete;
    BitReader& operator=( BitReader&& other ) = delete;
    BitReader& operator=( const BitReader& other ) = delete;

    BitReader( const BitReader& other ) :
        m_file( other.m_file ? other.m_file->clone() : nullptr ),
        m_offsetBits( other.m_offsetBits ),
        m_inbuf( other.m_inbuf )
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
        return ( m_inbufPos >= m_inbuf.size() ) && ( !m_file || m_file->eof() );
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
        m_inbuf.clear();
    }

    [[nodiscard]] bool
    closed() const override final
    {
        return !m_file && m_inbuf.empty();
    }

    uint32_t
    read( uint8_t bitsWanted );

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

    template<uint8_t bitsWanted>
    uint32_t
    read()
    {
        static_assert( bitsWanted < sizeof( m_inbufBits ) * CHAR_BIT, "Requested bits must fit in buffer!" );
        if ( bitsWanted <= m_inbufBitCount ) {
            m_inbufBitCount -= bitsWanted;
            return ( m_inbufBits >> m_inbufBitCount ) & nLowestBitsSet<decltype( m_inbufBits )>( bitsWanted );
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
        size_t position = m_inbufPos;
        if ( m_file ) {
            position += m_file->tell() - m_inbuf.size();
        }
        return position * 8U - m_inbufBitCount - m_offsetBits;
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
        return ( m_file ? m_file->size() : m_inbuf.size() ) * 8U - m_offsetBits;
    }

    [[nodiscard]] const std::vector<std::uint8_t>&
    buffer() const
    {
        return m_inbuf;
    }

private:
    uint32_t
    readSafe( uint8_t );

    void
    refillBuffer()
    {
        if ( !m_file ) {
            throw std::logic_error( "Can not refill buffer with data from non-existing file!" );
        }

        m_inbuf.resize( IOBUF_SIZE );
        const auto nBytesRead = m_file->read( reinterpret_cast<char*>( m_inbuf.data() ), m_inbuf.size() );

        if ( nBytesRead == 0 ) {
            // this will also happen for invalid file descriptor -1
            std::stringstream msg;
            msg
            << "[BitReader] Not enough data to read!\n"
            << "  File position: " << m_file->tell() << "\n"
            << "  File size: " << m_file->size() << "B\n"
            << "  Input buffer size: " << m_inbuf.size() << "B\n"
            << "  EOF: " << m_file->eof() << "\n"
            << "  Error: " << m_file->fail() << "\n"
            << "\n";
            throw std::domain_error( msg.str() );
        }

        m_inbuf.resize( nBytesRead );
        m_inbufPos = 0;
    }

    template<typename T>
    static T
    nLowestBitsSet( uint8_t nBitsSet )
    {
        static_assert( std::is_unsigned<T>::value, "Type must be signed!" );
        const auto nZeroBits = std::max( 0, std::numeric_limits<T>::digits - nBitsSet );
        return ~T(0) >> nZeroBits;
    }

    template<typename T, uint8_t nBitsSet>
    static T
    nLowestBitsSet()
    {
        static_assert( std::is_unsigned<T>::value, "Type must be signed!" );
        const auto nZeroBits = std::max( 0, std::numeric_limits<T>::digits - nBitsSet );
        return ~T(0) >> nZeroBits;
    }

private:
    std::unique_ptr<FileReader> m_file;

    /**
     * Ignore the first m_offsetBits in m_inbuf. Only used when initialized with a buffer.
     * Should only be changed by assignment or move operator.
     */
    uint8_t m_offsetBits = 0;

    std::vector<uint8_t> m_inbuf;
    size_t m_inbufPos = 0; /** stores current position of first valid byte in buffer */

public:
    /**
     * Bit buffer stores the last read bits from m_inbuf.
     * The bits are to be read from left to right. This means that not the least significant n bits
     * are to be returned on read but the most significant.
     * E.g. return 3 bits of 1011 1001 should return 101 not 001
     */
    uint32_t m_inbufBits = 0;
    uint8_t m_inbufBitCount = 0; // size of bitbuffer in bits
};


inline uint32_t
BitReader::read( const uint8_t bitsWanted )
{
    if ( bitsWanted <= m_inbufBitCount ) {
        m_inbufBitCount -= bitsWanted;
        return ( m_inbufBits >> m_inbufBitCount ) & nLowestBitsSet<decltype( m_inbufBits )>( bitsWanted );
    }
    return readSafe( bitsWanted );
}


inline uint32_t
BitReader::readSafe( const uint8_t bitsWanted )
{
    uint32_t bits = 0;
    assert( bitsWanted <= sizeof( bits ) * CHAR_BIT );

    // If we need to get more data from the byte buffer, do so.  (Loop getting
    // one byte at a time to enforce endianness and avoid unaligned access.)
    auto bitsNeeded = bitsWanted;
    while ( m_inbufBitCount < bitsNeeded ) {
        // If we need to read more data from file into byte buffer, do so
        if ( m_inbufPos >= m_inbuf.size() ) {
            refillBuffer();
        }

        // Avoid 32-bit overflow (dump bit buffer to top of output)
        if ( m_inbufBitCount >= sizeof( m_inbufBits ) * CHAR_BIT - CHAR_BIT ) {
            bits = m_inbufBits & nLowestBitsSet<decltype( m_inbufBits )>( m_inbufBitCount );
            bitsNeeded -= m_inbufBitCount;
            bits <<= bitsNeeded;
            m_inbufBitCount = 0;
        }

        // Grab next 8 bits of input from buffer.
        m_inbufBits = ( m_inbufBits << CHAR_BIT ) | m_inbuf[m_inbufPos++];
        m_inbufBitCount += CHAR_BIT;
    }

    // Calculate result
    m_inbufBitCount -= bitsNeeded;
    bits |= ( m_inbufBits >> m_inbufBitCount ) & nLowestBitsSet<decltype( m_inbufBits )>( bitsNeeded );
    assert( bits == ( bits & ( ~decltype( m_inbufBits )( 0 ) >> ( sizeof( m_inbufBits ) * CHAR_BIT - bitsWanted ) ) ) );
    return bits;
}


inline size_t
BitReader::seek( long long int offsetBits,
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

    offsetBits += m_offsetBits;

    if ( static_cast<size_t>( offsetBits ) == tell() ) {
        return static_cast<size_t>( offsetBits );
    }

    if ( offsetBits < 0 ) {
        throw std::invalid_argument( "Effective offset is before file start!" );
    }

    if ( static_cast<size_t>( offsetBits ) >= size() ) {
        throw std::invalid_argument( "Effective offset is after file end!" );
    }

    if ( !seekable() && ( static_cast<size_t>( offsetBits ) < tell() ) ) {
        throw std::invalid_argument( "File is not seekable!" );
    }

    const auto bytesToSeek = static_cast<size_t>( offsetBits ) >> 3U;
    const auto subBitsToSeek = static_cast<uint8_t>( static_cast<size_t>( offsetBits ) & 7U );

    m_inbufBits = 0;
    m_inbufBitCount = 0;

    if ( !m_file ) {
        /* Handle seeking in buffer only as it cannot be refilled. */
        if ( bytesToSeek >= m_inbuf.size() ) {
            std::logic_error( "Trying to seek after the end should have been checked earlier!" );
        }

        m_inbufPos = bytesToSeek;
        if ( subBitsToSeek > 0 ) {
            m_inbufBitCount = uint8_t( 8 ) - subBitsToSeek;
            m_inbufBits = m_inbuf[m_inbufPos++];
        }
    } else {
        m_inbuf.clear();
        m_inbufPos = 0;

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
#if 0
            if ( !m_file ) {
                throw std::logic_error( "The case without a file should be handled by the seekable case "
                                        "because it is a simple memory buffer!" );
            }

            /** @todo This is so flawed! It does not take into account m_inbufPos,
             * it did try to seek 8 times as many bytes and so on ... */
            std::vector<char> buffer( IOBUF_SIZE );
            auto nBytesToRead = bytesToSeek - ( tell() >> 3U );
            for ( size_t nBytesRead = 0; nBytesRead < nBytesToRead; nBytesRead += buffer.size() ) {
                const auto nChunkBytesToRead = std::min( nBytesToRead - nBytesRead, IOBUF_SIZE );
                const auto nChunkBytesRead = m_file->read( buffer.data(), nBytesToRead );

                bytesRead += currentPosition * 8U;
                if ( nChunkBytesRead < nChunkBytesToRead ) {
                    m_inbufBitCount
                    return nBytesToRead;
                }
            }
#endif
        }

        if ( subBitsToSeek > 0 ) {
            /* Here we skipped all the bytes possible, now we must read the next byte,
             * skip some bits and write the remaining bits to the buffer. */
            m_inbufBitCount = uint8_t( 8 ) - subBitsToSeek;

            char c = 0;
            m_file->read( &c, 1 );
            m_inbufBits = static_cast<uint32_t>( c );
        }
    }

    return offsetBits;
}
