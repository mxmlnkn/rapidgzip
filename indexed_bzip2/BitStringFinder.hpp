#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "common.hpp"
#include "FileReader.hpp"
#include "StandardFileReader.hpp"


/**
 * No matter the input, the data is read from an input buffer.
 * If a file is given, then that input buffer will be refilled when the input buffer empties.
 * It is less a file object and acts more like an iterator.
 * It offers a @ref find method returning the next match or std::numeric_limits<size_t>::max() if the end was reached.
 */
template<uint8_t bitStringSize>
class BitStringFinder
{
public:
    using ShiftedLUTTable = std::vector<std::pair</* shifted value to compare to */ uint64_t, /* mask */ uint64_t> >;

public:
    BitStringFinder( BitStringFinder&& ) = default;

    BitStringFinder( const BitStringFinder& other ) = delete;

    BitStringFinder& operator=( const BitStringFinder& ) = delete;

    BitStringFinder& operator=( BitStringFinder&& ) = delete;

    BitStringFinder( std::unique_ptr<FileReader> fileReader,
                     uint64_t                    bitStringToFind,
                     size_t                      fileBufferSizeBytes = 1*1024*1024 ) :
        m_bitStringToFind( bitStringToFind & mask<uint64_t>( bitStringSize ) ),
        m_movingBitsToKeep( bitStringSize > 0 ? bitStringSize - 1U : 0U ),
        m_movingBytesToKeep( ceilDiv( m_movingBitsToKeep, CHAR_BIT ) ),
        m_fileReader( std::move( fileReader ) ),
        m_fileChunksInBytes( std::max( fileBufferSizeBytes,
                                       static_cast<size_t>( ceilDiv( bitStringSize, CHAR_BIT ) ) ) )
    {
        if ( m_movingBytesToKeep >= m_fileChunksInBytes ) {
            std::stringstream msg;
            msg << "The file buffer size of " << m_fileChunksInBytes << "B is too small to look for strings with "
                << bitStringSize << " bits!";
            throw std::invalid_argument( msg.str() );
        }
    }

    /** @note Legacy constructor. Preferably, use the FileReader constructor. */
    BitStringFinder( std::string const& filePath,
                     uint64_t           bitStringToFind,
                     size_t             fileBufferSizeBytes = 1*1024*1024 ) :
        BitStringFinder( std::make_unique<StandardFileReader>( filePath ), bitStringToFind, fileBufferSizeBytes )
    {}

    /** @note Legacy constructor. Preferably, use the FileReader constructor. */
    BitStringFinder( int      fileDescriptor,
                     uint64_t bitStringToFind,
                     size_t   fileBufferSizeBytes = 1*1024*1024 ) :
        BitStringFinder( std::make_unique<StandardFileReader>( fileDescriptor ), bitStringToFind, fileBufferSizeBytes )
    {}

    /** @note This overload is used for the tests but can also be useful for other things. */
    BitStringFinder( const char* buffer,
                     size_t      size,
                     uint64_t    bitStringToFind ) :
        BitStringFinder( std::unique_ptr<FileReader>(), bitStringToFind )
    {
        m_buffer.assign( buffer, buffer + size );
    }

    virtual ~BitStringFinder() = default;

    [[nodiscard]] bool
    seekable() const
    {
        /* If m_fileReader is not set, then we are working on an in-memory buffer, which can be seeked. */
        return !m_fileReader || m_fileReader->seekable();
    }

    [[nodiscard]] bool
    eof() const
    {
        if ( m_fileReader ) {
            return bufferEof() && m_fileReader->eof();
        }
        return m_buffer.empty();
    }

    /**
     * @return the next match or std::numeric_limits<size_t>::max() if the end was reached.
     */
    [[nodiscard]] virtual size_t
    find();

protected:
    [[nodiscard]] bool
    bufferEof() const
    {
        return m_bufferBitsRead >= m_buffer.size() * CHAR_BIT;
    }

    size_t
    refillBuffer();

public:
    /**
     * @verbatim
     * 63                48                  32                  16        8         0
     * |                 |                   |                   |         |         |
     * 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 1111 1111 1111
     *                                                                  <------------>
     *                                                                    length = 12
     * @endverbatim
     *
     * @param length the number of lowest bits which should be 1 (rest are 0)
     */
    template<typename T>
    static constexpr T
    mask( uint8_t length )
    {
        return ~static_cast<T>( 0 ) >> ( sizeof( T ) * CHAR_BIT - length );
    }

    static ShiftedLUTTable
    createdShiftedBitStringLUT( uint64_t bitString,
                                bool     includeLastFullyShifted = false );

    static size_t
    findBitString( const uint8_t* buffer,
                   size_t         bufferSize,
                   uint64_t       bitString,
                   uint8_t        firstBitsToIgnore = 0 );

protected:
    const uint64_t m_bitStringToFind;

    std::vector<char> m_buffer;
    /**
     * How many bits from m_buffer bits are already read. The first bit string comparison will be done
     * after m_nTotalBytesRead * CHAR_BIT + m_bufferBitsRead >= bitStringSize
     */
    size_t m_bufferBitsRead = 0;

    /**
     * If the bit string is only one bit long, then we don't need to keep bits from the current buffer.
     * However, for a 2 bit string, one bit might be at the end of the current and the other at the beginning
     * of the next chunk, so we need to keep the last byte of that buffer but then mark the first 7 bits read
     * to avoid returning duplicate offsets!
     * For 8 bits, at worst 7 bits are in the current buffer and 1 in the next, so we need to keep 1 byte
     * and mark 1 bit read in the new buffer.
     * For 9 bits, keep 8 bits (1B) and mark 0 bits read.
     * This gives the formula for the bytes to keep as: ceil( ( bitStringSize - 1 ) / 8 ).
     */
    const uint8_t m_movingBitsToKeep;
    const uint8_t m_movingBytesToKeep;

    std::unique_ptr<FileReader> m_fileReader;

    /** This is not the current size of @ref m_buffer but the number of bytes to read from @ref m_file if it is empty */
    const size_t m_fileChunksInBytes;
    /**
     * This value is incremented whenever the buffer is refilled. It basically acts like an overflow counter
     * for @ref m_bufferBitsRead and is required to return the absolute bit pos.
     */
    size_t m_nTotalBytesRead = 0;

    /**
     * In some way this is the buffer for the input buffer.
     * It is a moving window of bitStringSize bits which can be directly compared to m_bitString
     * This moving window also ensure that bit strings at file chunk boundaries are recognized correctly!
     */
    uint64_t m_movingWindow = 0;
};


template<uint8_t bitStringSize>
typename BitStringFinder<bitStringSize>::ShiftedLUTTable
BitStringFinder<bitStringSize>::createdShiftedBitStringLUT( uint64_t bitString,
                                             bool     includeLastFullyShifted )
{
    const auto nWildcardBits = sizeof( uint64_t ) * CHAR_BIT - bitStringSize;
    ShiftedLUTTable shiftedBitStrings( nWildcardBits + includeLastFullyShifted );

    uint64_t shiftedBitString = bitString;
    uint64_t shiftedBitMask = std::numeric_limits<uint64_t>::max() >> nWildcardBits;
    for ( size_t i = 0; i < shiftedBitStrings.size(); ++i ) {
        shiftedBitStrings[shiftedBitStrings.size() - 1 - i] = std::make_pair( shiftedBitString, shiftedBitMask );
        shiftedBitString <<= 1;
        shiftedBitMask   <<= 1;
        assert( ( shiftedBitString & shiftedBitMask ) == shiftedBitString );
    }

    return shiftedBitStrings;
}


/**
 * @param bitString the lowest bitStringSize bits will be looked for in the buffer
 * @return size_t max if not found else position in buffer
 */
template<uint8_t bitStringSize>
size_t
BitStringFinder<bitStringSize>::findBitString( const uint8_t* buffer,
                                               size_t         bufferSize,
                                               uint64_t       bitString,
                                               uint8_t        firstBitsToIgnore )
{
    /* Simply load bytewise even if we could load more (uneven) bits by rounding down.
     * This makes this implementation much less performant in comparison to the "% 8 = 0" version! */
    const auto nBytesToLoadPerIteration = ( sizeof( uint64_t ) * CHAR_BIT - bitStringSize ) / CHAR_BIT;
    if ( nBytesToLoadPerIteration <= 0 ) {
        throw std::invalid_argument( "Bit string size must be smaller than or equal to 56 bit in order to "
                                     "load bytewise!" );
    }

    /* Initialize buffer window. Note that we can't simply read an uint64_t because of the bit and byte order */
    if ( bufferSize * CHAR_BIT < bitStringSize ) {
        return std::numeric_limits<size_t>::max();
    }
    size_t i = 0;
    uint64_t window = 0;
    for ( ; i < std::min( sizeof( window ), bufferSize ); ++i ) {
        window = ( window << CHAR_BIT ) | buffer[i];
    }

    /* The extra checks on firstBitsToIgnore and foundBitOffset < bufferSize *8 are only necessary here at
     * the beginning because the uint64_t "buffer" might not be all full and firstBitsToIgnore will also only
     * matter for the first 8 bits. But in the tight loop below, these checks slow down the bit string finder
     * from 2.0s to 2.7s! */
    if ( firstBitsToIgnore >= CHAR_BIT ) {
        std::stringstream msg;
        msg << "Only up to 7 bits should be to be ignored. Else incremenent the input buffer pointer instead! "
            << "However, we are to ignore " << firstBitsToIgnore << " bits!";
        throw std::invalid_argument( msg.str() );
    }
    {
        /* Use pre-shifted search bit string values and masks to test for the search string in the larger window.
         * Only for this very first check is it possible that we have the pattern fully shifted by
         * nBytesToLoadPerIteration. That's why we need to iterate over a bit shift table which has one more
         * entry than the tight loop below needs to have! In later iterations, i.e., in the tight loop down
         * below, this can't happen because then the pattern woudl have been found in one of the prior iterations. */
        size_t k = 0;
        const auto shiftedBitStrings = createdShiftedBitStringLUT( bitString, true );
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                const auto foundBitOffset = i * CHAR_BIT - bitStringSize - ( shiftedBitStrings.size() - 1 - k );
                if ( ( foundBitOffset >= firstBitsToIgnore ) && ( foundBitOffset < bufferSize * CHAR_BIT ) ) {
                    return foundBitOffset - firstBitsToIgnore;
                }
            }
            ++k;
        }
    }

    /* This tight loop is the performance critical part! */
    const auto shiftedBitStrings = createdShiftedBitStringLUT( bitString, false );
    for ( ; i < bufferSize; ) {
        for ( size_t j = 0; ( j < nBytesToLoadPerIteration ) && ( i < bufferSize ); ++i, ++j ) {
            window = ( window << CHAR_BIT ) | buffer[i];
        }

        /* Use pre-shifted search bit string values and masks to test for the search string in the larger window.
         * Note that the order of the shiftedBitStrings matter because we return the first found match! */
        size_t k = 0;
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                return i * CHAR_BIT - bitStringSize - ( shiftedBitStrings.size() - 1 - k ) - firstBitsToIgnore;
            }
            ++k;
        }
    }

    return std::numeric_limits<size_t>::max();
}


template<uint8_t bitStringSize>
size_t
BitStringFinder<bitStringSize>::find()
{
    if ( bitStringSize == 0 ) {
        return std::numeric_limits<size_t>::max();
    }

    while ( !eof() )
    {
        if ( bufferEof() ) {
            const auto nBytesRead = refillBuffer();
            if ( nBytesRead == 0 ) {
                return std::numeric_limits<size_t>::max();
            }
        }

        #define ALGO 1
        #if ALGO == 1

        for ( ; m_bufferBitsRead < m_buffer.size() * CHAR_BIT; ) {
            const auto byteOffset = m_bufferBitsRead / CHAR_BIT;
            const auto firstBitsToIgnore = static_cast<uint8_t>( m_bufferBitsRead % CHAR_BIT );

            const auto relpos = findBitString(
                reinterpret_cast<const uint8_t*>( m_buffer.data() ) + byteOffset,
                m_buffer.size() - byteOffset,
                m_bitStringToFind,
                firstBitsToIgnore
            );
            if ( relpos == std::numeric_limits<size_t>::max() ) {
                m_bufferBitsRead = m_buffer.size() * CHAR_BIT;
                break;
            }

            m_bufferBitsRead += relpos;

            const auto foundOffset = m_nTotalBytesRead * CHAR_BIT + m_bufferBitsRead;
            m_bufferBitsRead += 1;
            return foundOffset;
        }

        #elif ALGO == 2

        const auto bitMask = mask<uint64_t>( bitStringSize );

        /* Initialize the moving window with bitStringSize-1 bits.
         * Note that one additional bit is loaded before the first comparison.
         * At this point, we know that there are at least bitStringSize unread bits in the buffer. */
        if ( m_nTotalBytesRead * CHAR_BIT + m_bufferBitsRead < bitStringSize-1u ) {
            const auto nBitsToRead = bitStringSize-1 - ( m_nTotalBytesRead * CHAR_BIT + m_bufferBitsRead );
            for ( size_t i = 0; i < nBitsToRead; ++i, ++m_bufferBitsRead ) {
                const auto byte = static_cast<unsigned char>( m_buffer[m_bufferBitsRead / CHAR_BIT] );
                const auto bit = ( byte >> ( 7 - ( m_bufferBitsRead & 7U ) ) ) & 1U;
                m_movingWindow = ( ( m_movingWindow << 1 ) | bit ) & bitMask;
            }
        }

        for ( ; m_bufferBitsRead < m_buffer.size() * CHAR_BIT; ) {
            const auto byte = static_cast<unsigned char>( m_buffer[m_bufferBitsRead / CHAR_BIT] );
            for ( int j = m_bufferBitsRead & 7U; j < CHAR_BIT; ++j, ++m_bufferBitsRead ) {
                const auto bit = ( byte >> ( 7 - j ) ) & 1U;
                m_movingWindow = ( ( m_movingWindow << 1 ) | bit ) & bitMask;
                if ( m_movingWindow == m_bitStringToFind )
                {
                    ++m_bufferBitsRead;
                    return m_nTotalBytesRead * CHAR_BIT + m_bufferBitsRead - bitStringSize;
                }
            }
        }

        #endif
    }

    return std::numeric_limits<size_t>::max();
}


template<uint8_t bitStringSize>
size_t
BitStringFinder<bitStringSize>::refillBuffer()
{
    if ( !m_fileReader || m_fileReader->eof() ) {
        m_nTotalBytesRead += m_buffer.size();
        m_buffer.clear();
        return 0;
    }

    /* Read chunk of data from file into buffer. */
    size_t nBytesRead = 0;
    if ( m_buffer.empty() ) {
        assert( m_nTotalBytesRead == 0 );
        assert( m_bufferBitsRead == 0 );

        m_buffer.resize( m_fileChunksInBytes );
        nBytesRead = m_fileReader->read( m_buffer.data(), m_buffer.size() );
        m_buffer.resize( nBytesRead );
    } else {
        m_nTotalBytesRead += m_buffer.size() - m_movingBytesToKeep;
        m_bufferBitsRead = m_movingBytesToKeep * CHAR_BIT - m_movingBitsToKeep;

        /* On subsequent refills, keep the last bits in order to find bit strings on buffer boundaries. */
        std::memmove( m_buffer.data(), m_buffer.data() + m_buffer.size() - m_movingBytesToKeep, m_movingBytesToKeep );

        const auto nBytesToRead = m_buffer.size() - m_movingBytesToKeep;
        nBytesRead = m_fileReader->read( m_buffer.data() + m_movingBytesToKeep, nBytesToRead );
        m_buffer.resize( m_movingBytesToKeep + nBytesRead );
    }

    return nBytesRead;
}
