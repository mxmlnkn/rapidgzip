#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <igzip_lib.h>

#include <VectorView.hpp>

#include "definitions.hpp"
#include "gzip.hpp"


namespace rapidgzip
{
/**
 * This is a small wrapper around ISA-l. It is able to:
 *  - work on BitReader as input
 *  - start at deflate block offset as opposed to gzip start
 */
class IsalInflateWrapper
{
public:
    explicit
    IsalInflateWrapper( BitReader    bitReader,
                        const size_t untilOffset = std::numeric_limits<size_t>::max() ) :
        m_bitReader( std::move( bitReader ) ),
        m_encodedStartOffset( m_bitReader.tell() ),
        m_encodedUntilOffset( std::min( m_bitReader.size(), untilOffset ) )
    {
        initStream();
    }

    void
    initStream()
    {
        isal_inflate_init( &m_stream );
        m_stream.crc_flag = ISAL_DEFLATE;  // This way no gzip header or footer is read
        /* The next_in, avail_in, next_out, avail_out "interface" is the same as zlib. */
        m_stream.next_in = nullptr;
        m_stream.avail_in = 0;
        m_stream.read_in = 0;
        m_stream.read_in_length = 0;
    }

    void
    refillBuffer()
    {
        if ( ( m_stream.avail_in > 0 ) || ( m_bitReader.tell() >= m_encodedUntilOffset ) ) {
            return;
        }

        if ( m_bitReader.tell() % BYTE_SIZE != 0 ) {
            /* This might be called at the very first refillBuffer call when it does not start on a byte-boundary. */
            const auto nBitsToPrime = BYTE_SIZE - ( m_bitReader.tell() % BYTE_SIZE );
            inflatePrime( nBitsToPrime, m_bitReader.read( nBitsToPrime ) );
            assert( m_bitReader.tell() % BYTE_SIZE == 0 );
        } else if ( const auto remainingBits = m_encodedUntilOffset - m_bitReader.tell(); remainingBits < BYTE_SIZE ) {
            /* This might be called at the very last refillBuffer call, when it does not end on a byte-boundary. */
            inflatePrime( remainingBits, m_bitReader.read( remainingBits ) );
            return;
        }

        /* This reads byte-wise from BitReader. */
        m_stream.avail_in = m_bitReader.read(
            m_buffer.data(), std::min( ( m_encodedUntilOffset - m_bitReader.tell() ) / BYTE_SIZE, m_buffer.size() ) );
        m_stream.next_in = reinterpret_cast<unsigned char*>( m_buffer.data() );
    }

    void
    setWindow( VectorView<uint8_t> const& window )
    {
        m_setWindowSize = window.size();
        if ( isal_inflate_set_dict( &m_stream, window.data(), window.size() ) != COMP_OK ) {
            throw std::runtime_error( "Failed to set back-reference window in ISA-l!" );
        }
    }

    /**
     * May return fewer bytes than requested. Only reads one deflate stream per call so that it can return
     * the gzip footer appearing after each deflate stream.
     */
    [[nodiscard]] std::pair<size_t, std::optional<gzip::Footer> >
    readStream( uint8_t* const output,
                size_t   const outputSize )
    {
        m_stream.next_out = output;
        m_stream.avail_out = outputSize;
        m_stream.total_out = 0;

        size_t decodedSize{ 0 };
        while ( ( decodedSize + m_stream.total_out < outputSize ) && ( m_stream.avail_out > 0 ) ) {
            refillBuffer();
            if ( ( m_stream.avail_in == 0 ) && ( m_stream.read_in_length == 0 ) ) {
                break;
            }

            /* > If the crc_flag is set to ISAL_GZIP or ISAL_ZLIB, the
             * > gzip/zlib header is parsed, state->crc is set to the appropriate checksum,
             * > and the checksum is verified. If the crc_flag is set to ISAL_DEFLATE
             * > (default), then the data is treated as a raw deflate block. */
            const auto oldAvailIn = m_stream.avail_in;
            const auto oldReadInLength = m_stream.read_in_length;
            const auto errorCode = isal_inflate( &m_stream );
            const auto progressedBits = ( oldAvailIn - m_stream.avail_in ) * BYTE_SIZE
                                        + ( oldReadInLength - m_stream.read_in_length );
            if ( errorCode != ISAL_DECOMP_OK ) {
                std::stringstream message;
                message << "[IsalInflateWrapper][Thread " << std::this_thread::get_id() << "] "
                        << "Decoding failed with error code " << errorCode << ": " << getErrorString( errorCode )
                        << "! Already decoded " << m_stream.total_out << " B. "
                        << "Bit range to decode: [" << m_encodedStartOffset << ", " << m_encodedUntilOffset << "]. ";
                if ( m_setWindowSize ) {
                    message << "Set window size: " << *m_setWindowSize << " B.";
                } else {
                    message << "No window was set.";
                }
                throw std::runtime_error( std::move( message ).str() );
            }

            if ( decodedSize + m_stream.total_out > outputSize ) {
                throw std::logic_error( "Decoded more than fits into the output buffer!" );
            }

            if ( m_stream.block_state == ISAL_BLOCK_FINISH ) {
                decodedSize += m_stream.total_out;

                /* If we started with raw deflate, then we also have to skip over the gzip footer.
                 * Assuming we are decoding gzip and not zlib or multiple raw deflate streams. */
                std::optional<gzip::Footer> footer;
                footer = readGzipFooter();  // This resets m_stream.total_out
                if ( footer ) {
                    readGzipHeader();
                }

                m_stream.next_out = output + decodedSize;
                m_stream.avail_out = outputSize - decodedSize;

                return { decodedSize, footer };
            }

            if ( progressedBits == 0 ) {
                break;
            }
        }

        return { decodedSize + m_stream.total_out, std::nullopt };
    }

    [[nodiscard]] size_t
    tellEncoded() const
    {
        return m_bitReader.tell() - m_stream.avail_in * BYTE_SIZE - m_stream.read_in_length;
    }

private:
    [[nodiscard]] bool
    hasInput() const
    {
        return ( m_stream.avail_in > 0 ) || ( m_stream.read_in_length > 0 );
    }

    void
    inflatePrime( size_t   nBitsToPrime,
                  uint64_t bits )
    {
        m_stream.read_in |= bits << m_stream.read_in_length;
        m_stream.read_in_length += static_cast<int32_t>( nBitsToPrime );
    }

    /**
     * Only works on and modifies m_stream.avail_in and m_stream.next_in.
     */
    std::optional<gzip::Footer>
    readGzipFooter()
    {
        gzip::Footer footer{ 0, 0 };

        const auto remainingBits = m_stream.read_in_length % BYTE_SIZE;
        m_stream.read_in >>= remainingBits;
        m_stream.read_in_length -= remainingBits;

        constexpr auto FOOTER_SIZE = 8U;
        std::array<std::byte, FOOTER_SIZE> footerBuffer{};
        for ( auto stillToRemove = FOOTER_SIZE; stillToRemove > 0; ) {
            const auto footerSize = FOOTER_SIZE - stillToRemove;
            if ( m_stream.read_in_length > 0 ) {
                /* This should be ensured by making read_in_length % BYTE_SIZE == 0 prior. */
                assert( m_stream.read_in_length >= BYTE_SIZE );

                footerBuffer[footerSize] = static_cast<std::byte>( m_stream.read_in & 0xFFU );
                m_stream.read_in >>= BYTE_SIZE;
                m_stream.read_in_length -= BYTE_SIZE;
                --stillToRemove;
            } else if ( m_stream.avail_in >= stillToRemove ) {
                std::memcpy( footerBuffer.data() + footerSize, m_stream.next_in, stillToRemove );
                m_stream.avail_in -= stillToRemove;
                m_stream.next_in += stillToRemove;
                stillToRemove = 0;
            } else {
                std::memcpy( footerBuffer.data() + footerSize, m_stream.next_in, m_stream.avail_in );
                stillToRemove -= m_stream.avail_in;
                m_stream.avail_in = 0;
                refillBuffer();
                if ( m_stream.avail_in == 0 ) {
                    return std::nullopt;
                }
            }
        }

        /* Get CRC32 and size machine-endian-agnostically. */
        for ( auto i = 0U; i < 4U; ++i ) {
            const auto subbyte = static_cast<uint8_t>( footerBuffer[i] );
            footer.crc32 += static_cast<uint32_t>( subbyte ) << ( i * BYTE_SIZE );
        }
        for ( auto i = 0U; i < 4U; ++i ) {
            const auto subbyte = static_cast<uint8_t>( footerBuffer[4U + i] );
            footer.uncompressedSize += static_cast<uint32_t>( subbyte ) << ( i * BYTE_SIZE );
        }

        return footer;
    }

    void
    readGzipHeader()
    {
        const auto oldNextOut = m_stream.next_out;

        /* Note that inflateInit and inflateReset set total_out to 0 among other things. */
        isal_inflate_reset( &m_stream );
        m_stream.crc_flag = ISAL_DEFLATE;  // This way no gzip header or footer is read

        isal_gzip_header gzipHeader{};
        isal_gzip_header_init( &gzipHeader );

        refillBuffer();
        while ( m_stream.avail_in > 0 ) {
            const auto errorCode = isal_read_gzip_header( &m_stream, &gzipHeader );
            if ( errorCode == ISAL_DECOMP_OK ) {
                break;
            }

            if ( errorCode != ISAL_END_INPUT ) {
                std::stringstream message;
                message << "Failed to parse gzip header (" << errorCode << ": " << getErrorString( errorCode ) << ")!";
                throw std::runtime_error( std::move( message ).str() );
            }

            refillBuffer();
        }

        if ( m_stream.next_out != oldNextOut ) {
            throw std::logic_error( "ISA-l wrote some output even though we only wanted to read the gzip header!" );
        }
    }

    [[nodiscard]] static std::string_view
    getErrorString( int errorCode ) noexcept
    {
        switch ( errorCode )
        {
        case ISAL_DECOMP_OK          /*  0 */ : return "No errors encountered while decompressing";
        case ISAL_END_INPUT          /*  1 */ : return "End of input reached";
        case ISAL_OUT_OVERFLOW       /*  2 */ : return "End of output reached";
        case ISAL_NAME_OVERFLOW      /*  3 */ : return "End of gzip name buffer reached";
        case ISAL_COMMENT_OVERFLOW   /*  4 */ : return "End of gzip name buffer reached";
        case ISAL_EXTRA_OVERFLOW     /*  5 */ : return "End of extra buffer reached";
        case ISAL_NEED_DICT          /*  6 */ : return "Stream needs a dictionary to continue";
        case ISAL_INVALID_BLOCK      /* -1 */ : return "Invalid deflate block found";
        case ISAL_INVALID_SYMBOL     /* -2 */ : return "Invalid deflate symbol found";
        case ISAL_INVALID_LOOKBACK   /* -3 */ : return "Invalid lookback distance found";
        case ISAL_INVALID_WRAPPER    /* -4 */ : return "Invalid gzip/zlib wrapper found";
        case ISAL_UNSUPPORTED_METHOD /* -5 */ : return "Gzip/zlib wrapper specifies unsupported compress method";
        case ISAL_INCORRECT_CHECKSUM /* -6 */ : return "Incorrect checksum found";
        }
        return "Unknown Error";
    }

private:
    BitReader m_bitReader;
    const size_t m_encodedStartOffset;
    const size_t m_encodedUntilOffset;
    std::optional<size_t> m_setWindowSize;

    inflate_state m_stream{};
    /* Loading the whole encoded data (multiple MiB) into memory first and then
     * decoding it in one go is 4x slower than processing it in chunks of 128 KiB! */
    std::array<char, 128_Ki> m_buffer;
};
}  // namespace rapidgzip
