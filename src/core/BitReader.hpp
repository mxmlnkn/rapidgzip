#pragma once

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <sys/stat.h>

#include <BitManipulation.hpp>
#include <common.hpp>
#include <filereader/FileReader.hpp>
#include <filereader/Standard.hpp>
#include <filereader/Shared.hpp>


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
 */
template<bool MOST_SIGNIFICANT_BITS_FIRST,
         typename BitBuffer>
class BitReader :
    public FileReader
{
public:
    static_assert( std::is_unsigned_v<BitBuffer>, "Bit buffer type must be unsigned!" );

    /**
     * If it is too large, then the use case of only reading one Bzip2 block per opened BitReader
     * will load much more data than necessary because of the too large buffer.
     * The size should also be a multiple of the block size of the underlying device.
     * Any power of 2 larger than 4096 (4k blocks) should be safe bet.
     * 4K is too few, and will lead to a 2x slowdown in some test because of the frequent buffer refills.
     */
    static constexpr size_t IOBUF_SIZE = 128 * 1024;
    static constexpr int NO_FILE = -1;
    static constexpr auto MAX_BIT_BUFFER_SIZE = std::numeric_limits<BitBuffer>::digits;

    class BitReaderException :
        public std::exception
    {};

    /**
     * This exception is thrown by refillBitBuffer when the I/O buffer has been emptied.
     * Because the I/O buffer is rather large compared to the bit buffer, it only empties infrequently.
     * Using an exception instead of conditionals **doubled** performance in a synthetic BitReader benchmark
     * and improved the gzip decoder performance by ~20%!
     */
    class BufferNeedsToBeRefilled :
        public BitReaderException
    {};

    /**
     * An exception that may be returned by @ref BitReader::peek. An exception is used instead of std::optional
     * because this only happens once for the whole input stream, so very rarely, and it is expensive to still
     * check the optional each time when it isn't needed 99.999% of the time! A try-catch block instead can act
     * as a kind of zero-cost conditional in case nothing is thrown and is sufficiently cheap for the very rare
     * case that an exception has to be thrown.
     */
    class EndOfFileReached :
        public BitReaderException
    {};

public:
    explicit
    BitReader( std::unique_ptr<FileReader> fileReader ) :
        m_file( dynamic_cast<SharedFileReader*>( fileReader.get() ) == nullptr
                ? std::unique_ptr<FileReader>( std::make_unique<SharedFileReader>( std::move( fileReader ) ) )
                : std::move( fileReader ) )
    {}

    BitReader( BitReader&& other ) = default;
    BitReader& operator=( BitReader&& other ) = default;
    BitReader& operator=( const BitReader& other ) = delete;

    BitReader( const BitReader& other ) :
        m_file( other.m_file ? other.m_file->clone() : nullptr ),
        m_inputBuffer( other.m_inputBuffer )
    {
        assert( static_cast<bool>( m_file ) == static_cast<bool>( other.m_file ) );
        if ( UNLIKELY( m_file && !m_file->seekable() ) ) [[unlikely]] {
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

    /**
     * @note This function is not as cheap as one might thing because it dynamically tests for EOF using the
     *       position and size instead of checking an internal (cached) flag!
     */
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
     * Note that splitting part of this method off into readSafe made the compiler actually
     * inline this now small function and thereby sped up runtimes significantly!
     */
    forceinline BitBuffer
    read( uint8_t bitsWanted )
    {
        /* Handling bitsWanted == 0 here would incure a 75% slowdown for the benchmark reading single bits!
         * Just let the caller handle that case. Performance comes first, especially at this steep price for safety. */
        assert( bitsWanted > 0 );
        /* Reading the whole buffer is not allowed because it would require another expensive rarely used branch! */
        assert( bitsWanted < MAX_BIT_BUFFER_SIZE );

        if ( LIKELY( bitsWanted <= m_bitBufferSize ) ) [[likely]] {
            const auto result = peekUnsafe( bitsWanted );
            m_bitBufferSize -= bitsWanted;
            return result;
        }

        const auto bitsInResult = m_bitBufferSize;
        const auto bitsNeeded = bitsWanted - bitsInResult;
        BitBuffer bits{ 0 };

        /* Read out the remaining bits from m_bitBuffer to the lowest bits in @ref bits.
         * This is copy-pasted from @ref peekUnsafe because for MSB, we don't have to do the expensive
         * m_bitBufferSize == 0 branching! */
        if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
            bits = m_bitBuffer & nLowestBitsSet<BitBuffer>( m_bitBufferSize );
        } else {
            bits = m_bitBufferSize == 0
                   ? BitBuffer( 0 )
                   : ( m_bitBuffer >> ( MAX_BIT_BUFFER_SIZE - m_bitBufferSize ) )
                     & nLowestBitsSet<BitBuffer>( m_bitBufferSize );
        }

        if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST && ( ENDIAN == Endian::LITTLE ) ) {
            constexpr uint8_t BYTES_WANTED = sizeof( BitBuffer );
            constexpr uint8_t BITS_WANTED = sizeof( BitBuffer ) * CHAR_BIT;

            if ( LIKELY( m_inputBufferPosition + BYTES_WANTED < m_inputBuffer.size() ) ) [[likely]] {
                m_originalBitBufferSize = BITS_WANTED;
                m_bitBufferSize = BITS_WANTED;
                m_bitBuffer = loadUnaligned<BitBuffer>( &m_inputBuffer[m_inputBufferPosition] );

                m_inputBufferPosition += BYTES_WANTED;

                bits |= peekUnsafe( bitsNeeded ) << bitsInResult;
                m_bitBufferSize -= bitsNeeded;
                return bits;
            }
        }

        try {
            clearBitBuffer();
            fillBitBuffer();
        } catch ( const BufferNeedsToBeRefilled& ) {
            refillBuffer();
            try {
                refillBitBuffer();
            } catch ( const BufferNeedsToBeRefilled& ) {
                /* When fillBitBuffer does not throw, then it has been filled almost completely and it is ensured
                 * that we have enough bits as long as fewer than the bit buffer size were requested.
                 * Removing this if from the non-throwing frequent path, improves performance measurably! */
                if ( UNLIKELY( bitsNeeded > m_bitBufferSize ) ) [[unlikely]] {
                    throw EndOfFileReached();
                }
            }
        }

        /* Append remaining requested bits. */
        if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
            bits = ( bits << bitsNeeded ) | peekUnsafe( bitsNeeded );
        } else {
            bits |= peekUnsafe( bitsNeeded ) << bitsInResult;
        }
        m_bitBufferSize -= bitsNeeded;

        return bits;
    }

    /**
     * This is a performant unchecked helper to seek forward the same amount that has already been peeked.
     * Calling this function without calling peek beforehand with the same number of bits may corrupt the BitReader!
     */
    forceinline void
    seekAfterPeek( uint8_t bitsWanted )
    {
        assert( bitsWanted <= m_bitBufferSize );
        m_bitBufferSize -= bitsWanted;
    }

    /**
     * Forcing to inline this function is superimportant because it depends even between gcc versions,
     * whether it is actually inlined or not but inlining can save 30%!
     */
    template<uint8_t bitsWanted>
    forceinline BitBuffer
    read()
    {
        if constexpr ( bitsWanted == 0 ) {
            return 0;
        } else {
            static_assert( bitsWanted <= MAX_BIT_BUFFER_SIZE, "Requested bits must fit in buffer!" );
            return read( bitsWanted );
        }
    }

    /**
     * @return Number of bytes read.
     */
    [[nodiscard]] size_t
    read( char*  outputBuffer,
          size_t nBytesToRead ) override
    {
        const auto oldTell = tell();

        if ( UNLIKELY( outputBuffer == nullptr ) ) [[unlikely]] {
            seek( nBytesToRead, SEEK_CUR );
        } else if ( oldTell % CHAR_BIT != 0 ) {
            for ( size_t i = 0; i < nBytesToRead; ++i ) {
                outputBuffer[i] = static_cast<char>( read( CHAR_BIT ) );
            }
        } else {
            size_t nBytesRead{ 0 };
            /* Should be true because of oldTell % BYTE_SIZE == 0! */
            assert( m_bitBufferSize % CHAR_BIT == 0 );

            /* 1. Empty bit buffer */
            for ( ; ( nBytesRead < nBytesToRead ) && ( m_bitBufferSize >= CHAR_BIT ); ++nBytesRead ) {
                outputBuffer[nBytesRead] = peekUnsafe( CHAR_BIT );
                seekAfterPeek( CHAR_BIT );
            }

            /* 2. Empty byte buffer */
            const auto nBytesReadFromBuffer = std::min( nBytesToRead - nBytesRead,
                                                        m_inputBuffer.size() - m_inputBufferPosition );
            if ( nBytesReadFromBuffer > 0 ) {
                std::memcpy( outputBuffer + nBytesRead, m_inputBuffer.data() + m_inputBufferPosition,
                             nBytesReadFromBuffer );
                nBytesRead += nBytesReadFromBuffer;
                m_inputBufferPosition += nBytesReadFromBuffer;
            }

            /* 3. Read directly from underlying file */
            const auto nBytesToReadFromFile = nBytesToRead - nBytesRead;
            if ( ( nBytesToReadFromFile > 0 ) && m_file ) {
                /* We don't need the return value because we are using tell! */
                [[maybe_unused]] const auto nBytesReadFromFile =
                    m_file->read( outputBuffer + nBytesRead, nBytesToReadFromFile );
            }
        }

        const auto nBitsRead = tell() - oldTell;
        if ( UNLIKELY( nBitsRead % CHAR_BIT != 0 ) ) [[unlikely]] {
            throw std::runtime_error( "Read not a multiple of CHAR_BIT, probably because EOF was encountered!" );
        }
        return nBitsRead / CHAR_BIT;
    }

    template<uint8_t bitsWanted>
    forceinline BitBuffer
    peek()
    {
        if constexpr ( bitsWanted == 0 ) {
            return 0;
        } else {
            static_assert( bitsWanted <= MAX_BIT_BUFFER_SIZE, "Requested bits must fit in buffer!" );
            return peek( bitsWanted );
        }
    }

    forceinline BitBuffer
    peek( uint8_t bitsWanted )
    {
        assert( ( bitsWanted <= MAX_BIT_BUFFER_SIZE - ( CHAR_BIT - 1 ) )
                && "The last 7 bits of the buffer may not be readable because we can only refill 8-bits at a time." );
        assert( bitsWanted > 0 );

        if ( UNLIKELY( bitsWanted > m_bitBufferSize ) ) [[unlikely]] {
            if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST && ( ENDIAN == Endian::LITTLE ) ) {
                if ( LIKELY( m_inputBufferPosition + sizeof( BitBuffer ) < m_inputBuffer.size() ) ) [[likely]] {
                    /* There is no way around this special case because of the damn undefined behavior when shifting! */
                    if ( m_bitBufferSize == 0 ) {
                        m_originalBitBufferSize = sizeof( BitBuffer ) * CHAR_BIT;
                        m_bitBufferSize = sizeof( BitBuffer ) * CHAR_BIT;
                        m_bitBuffer = loadUnaligned<BitBuffer>( &m_inputBuffer[m_inputBufferPosition] );

                        m_inputBufferPosition += sizeof( BitBuffer );
                        return peekUnsafe( bitsWanted );
                    }

                    const auto shrinkedBitBufferSize = ceilDiv( m_bitBufferSize, CHAR_BIT ) * CHAR_BIT;
                    const auto bitsToLoad  = static_cast<uint8_t>( MAX_BIT_BUFFER_SIZE - shrinkedBitBufferSize );
                    const auto bytesToLoad = static_cast<uint8_t>( bitsToLoad / CHAR_BIT );

                    /* Load new bytes directly to the left if the (virtually) shrinked bit buffer.
                     * This is possibly because read but still "loaded" (m_originalBitBufferSize) bits are to the
                     * right. */
                    const auto bytesToAppend = loadUnaligned<BitBuffer>( &m_inputBuffer[m_inputBufferPosition] );
                    m_bitBuffer = ( m_bitBuffer >> bitsToLoad )
                                  | ( bytesToAppend << ( MAX_BIT_BUFFER_SIZE - bitsToLoad ) );

                    m_originalBitBufferSize = MAX_BIT_BUFFER_SIZE;
                    m_bitBufferSize += bitsToLoad;
                    m_inputBufferPosition += bytesToLoad;

                    return peekUnsafe( bitsWanted );
                }
            }

            try {
                /* In the case of the shortcut for filling the bit buffer by reading 64-bit, don't inline
                 * the very rarely used fallback to keep this function rather small for inlining. */
                if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST && ( ENDIAN == Endian::LITTLE ) ) {
                    /* This point should only happen rarely, e.g., when the byte buffer needs to be refilled. */
                    refillBitBuffer();
                } else {
                    if ( m_bitBufferSize == 0 ) {
                        m_bitBuffer = 0;
                        m_originalBitBufferSize = 0;
                    } else {
                        shrinkBitBuffer();

                        if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST ) {
                            m_bitBuffer >>= MAX_BIT_BUFFER_SIZE - m_originalBitBufferSize;
                        }
                    }

                    fillBitBuffer();
                }
            } catch ( const BufferNeedsToBeRefilled& ) {
                refillBuffer();
                try {
                    refillBitBuffer();
                } catch ( const BufferNeedsToBeRefilled& ) {
                    /* When fillBitBuffer does not throw, then it has been filled almost completely and it is ensured
                     * that we have enough bits as long as fewer than the bit buffer size were requested.
                     * Removing this if from the non-throwing frequent path, improves performance measurably! */
                    if ( UNLIKELY( bitsWanted > m_bitBufferSize ) ) [[unlikely]] {
                        throw EndOfFileReached();
                    }
                }
            }
        }

        return peekUnsafe( bitsWanted );
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
            if ( UNLIKELY( static_cast<size_t>( filePosition ) < m_inputBuffer.size() ) ) [[unlikely]] {
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
        if ( UNLIKELY( m_file ) ) [[unlikely]] {
            throw std::invalid_argument( "The file is not open!" );
        }
        return m_file->fileno();
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
        if ( UNLIKELY( position < m_bitBufferSize ) ) [[unlikely]] {
            std::logic_error( "The bit buffer should not contain data if the byte buffer doesn't!" );
        }
        return position - m_bitBufferSize;
    }

    void
    refillBuffer()
    {
        if ( UNLIKELY( !m_file ) ) [[unlikely]] {
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

    /**
     * Decreases m_originalBitBufferSize by CHAR_BIT until it is as close to m_bitBufferSize as possible
     * and clears all bits outside of m_originalBitBufferSize.
     */
    void
    shrinkBitBuffer()
    {
        if ( m_originalBitBufferSize == m_bitBufferSize ) {
            return;
        }

        assert( ( m_originalBitBufferSize % CHAR_BIT == 0 ) &&
                "Not necessary but should be true because we only load byte-wise and only shrink byte-wise!" );
        assert( m_originalBitBufferSize >= m_bitBufferSize );
        assert( m_originalBitBufferSize >= ceilDiv( m_bitBufferSize, CHAR_BIT ) * CHAR_BIT );

        m_originalBitBufferSize = ceilDiv( m_bitBufferSize, CHAR_BIT ) * CHAR_BIT;

        if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
            m_bitBuffer &= nLowestBitsSet<BitBuffer>( m_originalBitBufferSize );
        } else {
            m_bitBuffer &= nHighestBitsSet<BitBuffer>( m_originalBitBufferSize );
        }
    }

    void
    refillBitBuffer()
    {
        /* Skip refill if it already is full (except for up to 7 empty bits) */
        if ( m_bitBufferSize + CHAR_BIT > MAX_BIT_BUFFER_SIZE ) {
            return;
        }

        if ( m_bitBufferSize == 0 ) {
            m_bitBuffer = 0;
            m_originalBitBufferSize = 0;
        } else {
            shrinkBitBuffer();

            if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST ) {
                assert( m_originalBitBufferSize > 0 );
                /* Always checking for m_originalBitBufferSize for this damn bit shift would be too cost-prohibitive.
                 * It should never happen because in this branch we know that m_bitBufferSize > 0 and at any point
                 * in time m_originalBitBufferSize >= m_bitBufferSize should be true!
                 * Run unit tests in debug mode to ensure that the assert won't be triggered. */
                // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign)
                m_bitBuffer >>= MAX_BIT_BUFFER_SIZE - m_originalBitBufferSize;
            }
        }

        fillBitBuffer();
    }

    void
    fillBitBuffer()
    {
        /* Using an ad-hoc destructor to emulate a 'finally' does not seem to hurt performance
         * when comparing it to manually copy-pasting the shift in all outer catch clauses. */
        struct ShiftBackOnReturn
        {
            ShiftBackOnReturn( BitBuffer&     bitBuffer,
                               const uint8_t& originalBitBufferSize ) noexcept :
                m_bitBuffer( bitBuffer ),
                m_originalBitBufferSize( originalBitBufferSize )
            {}

            ~ShiftBackOnReturn() noexcept {
                /* Move LSB bits (which are filled left-to-right) to the left if so necessary
                 * so that the format is the same as for MSB bits! */
                if constexpr ( !MOST_SIGNIFICANT_BITS_FIRST ) {
                    m_bitBuffer <<= MAX_BIT_BUFFER_SIZE - m_originalBitBufferSize;
                }
            }

        private:
            BitBuffer& m_bitBuffer;
            const uint8_t& m_originalBitBufferSize;
        } shiftBackOnExit( m_bitBuffer, m_originalBitBufferSize );

        /* Refill buffer one byte at a time to enforce endianness and avoid unaligned access. */
        for ( ; m_originalBitBufferSize + CHAR_BIT <= MAX_BIT_BUFFER_SIZE;
              m_bitBufferSize += CHAR_BIT, m_originalBitBufferSize += CHAR_BIT )
        {
            if ( UNLIKELY( m_inputBufferPosition >= m_inputBuffer.size() ) ) [[unlikely]] {
                throw BufferNeedsToBeRefilled();
            }

            if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
                m_bitBuffer <<= CHAR_BIT;
                m_bitBuffer |= static_cast<BitBuffer>( m_inputBuffer[m_inputBufferPosition++] );
            } else {
                m_bitBuffer |= ( static_cast<BitBuffer>( m_inputBuffer[m_inputBufferPosition++] )
                               << m_originalBitBufferSize );
                /**
                 * Avoiding the single shift before and after the loop for LSB by modifying how the bits are
                 * appended slows it down by ~10%, probably because one additional shift per loop iteration
                 * is necessary:
                 * @verbatim
                 * m_bitBuffer <<= CHAR_BIT;
                 * m_bitBuffer |= ( static_cast<BitBuffer>( m_inputBuffer[m_inputBufferPosition++] )
                 *                << ( MAX_BIT_BUFFER_SIZE - CHAR_BIT ) );
                 * @endverbatim
                 */
            }
        }
    }

    [[nodiscard]] forceinline BitBuffer
    peekUnsafe( uint8_t bitsWanted ) const
    {
        assert( bitsWanted <= m_bitBufferSize );
        assert( bitsWanted > 0 );

        if constexpr ( MOST_SIGNIFICANT_BITS_FIRST ) {
            return ( m_bitBuffer >> ( m_bitBufferSize - bitsWanted ) )
                   & nLowestBitsSet<BitBuffer>( bitsWanted );
        } else {
            assert( m_bitBufferSize > 0 );
            /* Always checking for m_bitBufferSize for this damn bit shift would be too cost-prohibitive.
             * It should only happen when the caller tries to read, e.g., 0 bits, in which case undefined behavior
             * for the shift result value does not matter. Run unit tests in debug mode to ensure that the assert
             * won't be triggered. */
            // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
            return ( m_bitBuffer >> ( MAX_BIT_BUFFER_SIZE - m_bitBufferSize ) )
                   & nLowestBitsSet<BitBuffer>( bitsWanted );
        }
    }

    forceinline void
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

    offsetBits = std::clamp( offsetBits, 0LL, static_cast<long long int>( size() ) );

    if ( static_cast<size_t>( offsetBits ) == tell() ) {
        return static_cast<size_t>( offsetBits );
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
            if ( stillToSeek % CHAR_BIT > 0 ) {
                read( stillToSeek % CHAR_BIT );
            }

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
            << " subbit " << static_cast<int>( subBitsToSeek )
            << ", size: " << m_file->size()
            << ", feof: " << m_file->eof()
            << ", ferror: " << m_file->fail()
            << ", newPosition: " << newPosition;
            throw std::invalid_argument( std::move( msg ).str() );
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
