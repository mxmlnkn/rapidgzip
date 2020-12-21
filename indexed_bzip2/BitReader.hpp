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

#include <unistd.h>
#include <sys/stat.h>

#include "FileReader.hpp"


class BitReader :
    public FileReader
{
public:
    static constexpr size_t IOBUF_SIZE = 4096;
    static constexpr int NO_FILE = -1;

public:
    /** dup is not strong enough to be able to independently seek in the old and the dup'ed fd! */
    static std::string
    fdFilePath( int fileDescriptor )
    {
        std::stringstream filename;
        filename << "/proc/self/fd/" << fileDescriptor;
        return filename.str();
    }

public:
    explicit
    BitReader( const std::string& filePath ) :
        m_file( fopen( filePath.c_str(), "rb" ) ),
        m_seekable( determineSeekable( ::fileno( m_file ) ) ),
        m_fileSizeBytes( determineFileSize( ::fileno( m_file ) ) )
    {
        init();
    }

    explicit
    BitReader( int fileDescriptor ) :
        m_file( fopen( fdFilePath( fileDescriptor ).c_str(), "rb" ) ),
        m_seekable( determineSeekable( ::fileno( m_file ) ) ),
        m_fileSizeBytes( determineFileSize( ::fileno( m_file ) ) )
    {
        init();
    }

    BitReader( const uint8_t* buffer,
               size_t         size,
               uint8_t        offsetBits = 0 ) :
        m_seekable( true ),
        m_fileSizeBytes( size ),
        m_offsetBits( offsetBits ),
        m_inbuf( buffer, buffer + size )
    {
        seek( 0, SEEK_SET ); /* seeks to m_offsetBits under the hood */
    }

    explicit
    BitReader( std::vector<uint8_t>&& buffer,
               uint8_t                offsetBits = 0 ) :
        m_seekable( true ),
        m_fileSizeBytes( buffer.size() ),
        m_offsetBits( offsetBits ),
        m_inbuf( std::move( buffer ) )
    {
        seek( 0, SEEK_SET ); /* seeks to m_offsetBits under the hood */
    }

    BitReader( BitReader&& ) = default;

    /**
     * Copy constructor opens a new independent file descriptor and pointer.
     */
    BitReader( const BitReader& other ) :
        m_file( other.m_file == nullptr ? nullptr : fopen( fdFilePath( ::fileno( other.m_file ) ).c_str(), "rb" ) ),
        m_seekable( other.m_seekable ),
        m_fileSizeBytes( other.m_fileSizeBytes ),
        m_offsetBits( other.m_offsetBits )
    {
        init();
    }

    /**
     * Reassignment would have to close the old file and open a new.
     * Reusing the same file reader for a different file seems error prone.
     * If you really want that, just create a new object.
     */
    BitReader& operator=( const BitReader& ) = delete;

    BitReader& operator=( BitReader&& ) = delete;

    ~BitReader()
    {
        if ( m_file != nullptr ) {
            fclose( m_file );
        }
    }

    bool
    eof() const override
    {
        return m_seekable ? tell() >= size() : !m_lastReadSuccessful;
    }

    bool
    seekable() const override
    {
        return m_seekable;
    }

    void
    close() override
    {
        fclose( fp() );
        m_file = nullptr;
        m_inbuf.clear();
    }

    bool
    closed() const override
    {
        return ( m_file == nullptr ) && m_inbuf.empty();
    }

    uint32_t
    read( uint8_t );


    template<uint8_t bitsWanted>
    uint32_t
    read()
    {
        static_assert( bitsWanted < sizeof( m_inbufBits ) * CHAR_BIT, "Requested bits must fit in buffer!" );
        if ( bitsWanted <= m_inbufBitCount ) {
            m_readBitsCount += bitsWanted;
            m_inbufBitCount -= bitsWanted;
            return ( m_inbufBits >> m_inbufBitCount ) & nLowestBitsSet<decltype( m_inbufBits )>( bitsWanted );
        }
        return readSafe( bitsWanted );
    }

    size_t
    read( char*  outputBuffer,
          size_t nBytesToRead )
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
    size_t
    tell() const override
    {
        if ( m_seekable ) {
            return ( ftell( fp() ) - m_inbuf.size() + m_inbufPos ) * 8ULL - m_inbufBitCount - m_offsetBits;
        }
        return m_readBitsCount;
    }

    FILE*
    fp() const
    {
        return m_file;
    }

    int
    fileno() const override
    {
        if ( m_file == nullptr ) {
            throw std::invalid_argument( "The file is not open!" );
        }
        return ::fileno( m_file );
    }

    size_t
    seek( long long int offsetBits,
          int           origin = SEEK_SET ) override;

    size_t
    size() const override
    {
        return m_fileSizeBytes * 8 - m_offsetBits;
    }

    const std::vector<std::uint8_t>&
    buffer() const
    {
        return m_inbuf;
    }

private:
    static size_t
    determineFileSize( int fileNumber )
    {
        struct stat fileStats;
        fstat( fileNumber, &fileStats );
        return fileStats.st_size;
    }

    static size_t
    determineSeekable( int fileNumber )
    {
        struct stat fileStats;
        fstat( fileNumber, &fileStats );
        return !S_ISFIFO( fileStats.st_mode );
    }

    void
    init()
    {
        if ( m_seekable ) {
            fseek( fp(), 0, SEEK_SET );
        }
    }

    uint32_t
    readSafe( uint8_t );

    void
    refillBuffer()
    {
        m_inbuf.resize( IOBUF_SIZE );
        const size_t nBytesRead = fread( m_inbuf.data(), 1, m_inbuf.size(), m_file );
        if ( nBytesRead < m_inbuf.size() ) {
            m_lastReadSuccessful = false;
        }
        if ( nBytesRead == 0 ) {
            // this will also happen for invalid file descriptor -1
            std::stringstream msg;
            msg
            << "[BitReader] Not enough data to read!\n"
            << "  File pointer: " << (void*)m_file << "\n"
            << "  File position: " << ftell( m_file ) << "\n"
            << "  Input buffer size: " << m_inbuf.size() << "\n"
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
    FILE*         m_file = nullptr;
    bool    const m_seekable;
    size_t  const m_fileSizeBytes;
    uint8_t const m_offsetBits = 0; /** ignore the first m_offsetBits in m_inbuf. Only used when initialized with a buffer. */

    std::vector<uint8_t> m_inbuf;
    uint32_t m_inbufPos = 0; /** stores current position of first valid byte in buffer */
    bool m_lastReadSuccessful = true;

public:
    /**
     * Bit buffer stores the last read bits from m_inbuf.
     * The bits are to be read from left to right. This means that not the least significant n bits
     * are to be returned on read but the most significant.
     * E.g. return 3 bits of 1011 1001 should return 101 not 001
     */
    uint32_t m_inbufBits = 0;
    uint8_t m_inbufBitCount = 0; // size of bitbuffer in bits

    size_t m_readBitsCount = 0;
};


inline uint32_t
BitReader::read( const uint8_t bitsWanted )
{
    if ( bitsWanted <= m_inbufBitCount ) {
        m_readBitsCount += bitsWanted;
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
    // Commenting this single line improves speed by 2%! This shows how performance critical this function is.
    m_readBitsCount += bitsWanted;

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
        return offsetBits;
    }

    if ( offsetBits < 0 ) {
        throw std::invalid_argument( "Effective offset is before file start!" );
    }

    if ( static_cast<size_t>( offsetBits ) >= size() ) {
        throw std::invalid_argument( "Effective offset is after file end!" );
    }

    if ( !m_seekable ) {
        throw std::invalid_argument( "File is not seekable!" );
    }

    const size_t bytesToSeek = offsetBits >> 3;
    const size_t subBitsToSeek = offsetBits & 7;

    m_inbuf.clear();
    m_inbufPos = 0;
    m_inbufBits = 0;
    m_inbufBitCount = 0;

    if ( m_file == nullptr ) {
        if ( bytesToSeek >= m_inbuf.size() ) {
            std::stringstream msg;
            msg << "[BitReader] Could not seek to specified byte " << bytesToSeek;
            std::invalid_argument( msg.str() );
        }

        m_inbufPos = bytesToSeek;
        if ( subBitsToSeek > 0 ) {
            m_inbufBitCount = 8 - subBitsToSeek;
            m_inbufBits = m_inbuf[m_inbufPos++];
        }
    } else {
        const auto returnCodeSeek = fseek( m_file, bytesToSeek, SEEK_SET );
        if ( subBitsToSeek > 0 ) {
            m_inbufBitCount = 8 - subBitsToSeek;
            m_inbufBits = fgetc( m_file );
        }

        if ( ( returnCodeSeek != 0 ) || feof( m_file ) || ferror( m_file ) ) {
            std::stringstream msg;
            msg << "[BitReader] Could not seek to specified byte " << bytesToSeek
            << " subbit " << subBitsToSeek << ", feof: " << feof( m_file ) << ", ferror: " << ferror( m_file )
            << ", returnCodeSeek: " << returnCodeSeek;
            throw std::invalid_argument( msg.str() );
        }
    }

    return offsetBits;
}
