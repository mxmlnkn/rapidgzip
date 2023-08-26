#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <zlib.h>

#include <VectorView.hpp>

#include "definitions.hpp"
#include "gzip.hpp"


namespace rapidgzip
{
enum class CompressionStrategy : int
{
    DEFAULT             = Z_DEFAULT_STRATEGY,
    FILTERED            = Z_FILTERED,
    RUN_LENGTH_ENCODING = Z_RLE,
    HUFFMAN_ONLY        = Z_HUFFMAN_ONLY,
    FIXED_HUFFMAN       = Z_FIXED,
};


[[nodiscard]] inline std::string_view
toString( const CompressionStrategy compressionStrategy )
{
    using namespace std::literals;

    switch ( compressionStrategy )
    {
    case CompressionStrategy::DEFAULT: return "Default"sv;
    case CompressionStrategy::FILTERED: return "Filtered"sv;
    case CompressionStrategy::RUN_LENGTH_ENCODING: return "Run-Length Encoding"sv;
    case CompressionStrategy::HUFFMAN_ONLY: return "Huffman Only"sv;
    case CompressionStrategy::FIXED_HUFFMAN: return "Fixed Huffman"sv;
    }
    return {};
}


[[nodiscard]] inline std::vector<std::byte>
compressWithZlib( const std::vector<std::byte>& toCompress,
                  const CompressionStrategy     compressionStrategy = CompressionStrategy::DEFAULT )
{
    std::vector<std::byte> output;
    output.reserve( toCompress.size() );

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = toCompress.size();
    stream.next_in = reinterpret_cast<Bytef*>( const_cast<std::byte*>( toCompress.data() ) );
    stream.avail_out = 0;
    stream.next_out = nullptr;

    /* > Add 16 to windowBits to write a simple gzip header and trailer around the
     * > compressed data instead of a zlib wrapper. */
    deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                  MAX_WBITS | 16, /* memLevel */ 8, static_cast<int>( compressionStrategy ) );

    auto status = Z_OK;
    constexpr auto CHUNK_SIZE = 1_Mi;
    while ( status == Z_OK ) {
        output.resize( output.size() + CHUNK_SIZE );
        stream.next_out = reinterpret_cast<Bytef*>( output.data() + output.size() - CHUNK_SIZE );
        stream.avail_out = CHUNK_SIZE;
        status = ::deflate( &stream, Z_FINISH );
    }

    deflateEnd( &stream );

    output.resize( stream.total_out );
    output.shrink_to_fit();

    return output;
}


/**
 * This is a small wrapper around zlib. It is able to:
 *  - work on BitReader as input
 *  - start at deflate block offset as opposed to gzip start
 */
class ZlibInflateWrapper
{
public:
    struct Footer
    {
        gzip::Footer gzipFooter;
        size_t footerEndEncodedOffset{ 0 };
    };

public:
    explicit
    ZlibInflateWrapper( BitReader    bitReader,
                        const size_t untilOffset = std::numeric_limits<size_t>::max() ) :
        m_bitReader( std::move( bitReader ) ),
        m_encodedStartOffset( m_bitReader.tell() ),
        m_encodedUntilOffset( std::min( m_bitReader.size(), untilOffset ) )
    {
        initStream();
        /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode.
         * n in [8,15]
         * -n for raw inflate, not looking for zlib/gzip header and not generating a check value!
         * n + 16 for gzip decoding but not zlib
         * n + 32 for gzip or zlib decoding with automatic detection */
        m_windowFlags = -15;
        if ( inflateInit2( &m_stream, m_windowFlags ) != Z_OK ) {
            throw std::runtime_error( "Probably encountered invalid deflate data!" );
        }
    }

    ~ZlibInflateWrapper()
    {
        inflateEnd( &m_stream );
    }

    void
    initStream();

    void
    refillBuffer();

    void
    setWindow( VectorView<uint8_t> const& window )
    {
        m_setWindowSize = window.size();
        if ( inflateSetDictionary( &m_stream, window.data(), window.size() ) != Z_OK ) {
            throw std::runtime_error( "Failed to set back-reference window in zlib!" );
        }
    }

    /**
     * May return fewer bytes than requested. Only reads one deflate stream per call so that it can return
     * the gzip footer appearing after each deflate stream.
     */
    [[nodiscard]] std::pair<size_t, std::optional<Footer> >
    readStream( uint8_t* const output,
                size_t   const outputSize );

    [[nodiscard]] size_t
    tellCompressed() const
    {
        return m_bitReader.tell() - getUnusedBits();
    }

private:
    [[nodiscard]] size_t
    getUnusedBits() const
    {
        /* > on return inflate() always sets strm->data_type to the
         * > number of unused bits in the last byte taken from strm->next_in, plus 64 if
         * > inflate() is currently decoding the last block in the deflate stream [...]
         * > The number of unused bits may in general be greater than seven, except when bit 7 of
         * > data_type is set, in which case the number of unused bits will be less than eight.
         * @see https://github.com/madler/zlib/blob/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851/zlib.h#L443C22-L444C66 */
        return m_stream.avail_in * BYTE_SIZE + ( m_stream.data_type & 0b11'1111U );
    }

    /**
     * Only works on and modifies m_stream.avail_in and m_stream.next_in.
     */
    std::optional<Footer>
    readGzipFooter();

    void
    readGzipHeader();

private:
    BitReader m_bitReader;
    const size_t m_encodedStartOffset;
    const size_t m_encodedUntilOffset;
    std::optional<size_t> m_setWindowSize;

    int m_windowFlags{ 0 };
    z_stream m_stream{};
    /* Loading the whole encoded data (multiple MiB) into memory first and then
     * decoding it in one go is 4x slower than processing it in chunks of 128 KiB! */
    std::array<char, 128_Ki> m_buffer;
};


inline void
ZlibInflateWrapper::initStream()
{
    m_stream = {};

    m_stream.zalloc = Z_NULL;     /* used to allocate the internal state */
    m_stream.zfree = Z_NULL;      /* used to free the internal state */
    m_stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

    m_stream.avail_in = 0;        /* number of bytes available at next_in */
    m_stream.next_in = Z_NULL;    /* next input byte */

    m_stream.avail_out = 0;       /* remaining free space at next_out */
    m_stream.next_out = Z_NULL;   /* next output byte will go here */
    m_stream.total_out = 0;       /* total amount of bytes read */

    m_stream.msg = nullptr;
}


inline void
ZlibInflateWrapper::refillBuffer()
{
    if ( ( m_stream.avail_in > 0 ) || ( m_bitReader.tell() >= m_encodedUntilOffset ) ) {
        return;
    }

    if ( m_bitReader.tell() % BYTE_SIZE != 0 ) {
        /* This might be called at the very first refillBuffer call when it does not start on a byte-boundary. */
        const auto nBitsToPrime = BYTE_SIZE - ( m_bitReader.tell() % BYTE_SIZE );
        if ( inflatePrime( &m_stream, nBitsToPrime, m_bitReader.read( nBitsToPrime ) ) != Z_OK ) {
            throw std::runtime_error( "InflatePrime failed!" );
        }
        assert( m_bitReader.tell() % BYTE_SIZE == 0 );
    } else if ( const auto remainingBits = m_encodedUntilOffset - m_bitReader.tell(); remainingBits < BYTE_SIZE ) {
        /* This might be called at the very last refillBuffer call, when it does not end on a byte-boundary. */
        if ( inflatePrime( &m_stream, remainingBits, m_bitReader.read( remainingBits ) ) != Z_OK ) {
            throw std::runtime_error( "InflatePrime failed!" );
        }
        return;
    }

    /* This reads byte-wise from BitReader. */
    m_stream.avail_in = m_bitReader.read(
        m_buffer.data(), std::min( ( m_encodedUntilOffset - m_bitReader.tell() ) / BYTE_SIZE, m_buffer.size() ) );
    m_stream.next_in = reinterpret_cast<unsigned char*>( m_buffer.data() );
}


[[nodiscard]] inline std::pair<size_t, std::optional<ZlibInflateWrapper::Footer> >
ZlibInflateWrapper::readStream( uint8_t* const output,
                                size_t   const outputSize )
{
    m_stream.next_out = output;
    m_stream.avail_out = outputSize;
    m_stream.total_out = 0;

    size_t decodedSize{ 0 };
    /* Do not check for avail_out == 0 here so that progress can still be made on empty blocks as might
     * appear in pigz files or at the end of BGZF files. Note that zlib's inflate should return Z_BUF_ERROR
     * anyway if the output buffer is full. It might be that the check was only here to idiomatically avoid
     * a "while ( true )". */
    while ( true ) {
        refillBuffer();

        const auto oldUnusedBits = getUnusedBits();
        const auto oldTotalOut = m_stream.total_out;

        /* == actual zlib inflate call == */
        const auto errorCode = ::inflate( &m_stream, Z_BLOCK );

        /**
         * > Z_BUF_ERROR if no progress was possible or if there was not enough room in the output
         * > buffer when Z_FINISH is used
         * @see https://github.com/madler/zlib/blob/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851/zlib.h#L511C68-L513C31
         */
        if ( errorCode == Z_BUF_ERROR ) {
            break;
        }

        if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
            std::stringstream message;
            message << "[ZlibInflateWrapper][Thread " << std::this_thread::get_id() << "] "
                    << "Decoding failed with error code " << errorCode << " "
                    << ( m_stream.msg == nullptr ? "" : m_stream.msg ) << "! "
                    << "Already decoded " << m_stream.total_out << " B. "
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

        const auto progressedBits = oldUnusedBits != getUnusedBits();
        const auto progressedOutput = m_stream.total_out != oldTotalOut;

        if ( errorCode == Z_STREAM_END ) {
            decodedSize += m_stream.total_out;

            /* If we started with raw deflate, then we also have to skip over the gzip footer.
             * Assuming we are decoding gzip and not zlib or multiple raw deflate streams. */
            std::optional<Footer> footer;
            if ( m_windowFlags < 0 ) {
                footer = readGzipFooter();
                if ( footer ) {
                    readGzipHeader();
                }
            }

            m_stream.next_out = output + decodedSize;
            m_stream.avail_out = outputSize - decodedSize;

            return { decodedSize, footer };
        }

        if ( !progressedBits && !progressedOutput ) {
            break;
        }
    }

    return { decodedSize + m_stream.total_out, std::nullopt };
}


inline std::optional<ZlibInflateWrapper::Footer>
ZlibInflateWrapper::readGzipFooter()
{
    gzip::Footer footer{ 0, 0 };

    constexpr auto FOOTER_SIZE = 8U;
    std::array<std::byte, FOOTER_SIZE> footerBuffer;
    size_t footerSize{ 0 };
    for ( auto stillToRemove = FOOTER_SIZE; stillToRemove > 0; ) {
        if ( m_stream.avail_in >= stillToRemove ) {
            std::memcpy( footerBuffer.data() + footerSize, m_stream.next_in, stillToRemove );
            footerSize += stillToRemove;

            m_stream.avail_in -= stillToRemove;
            m_stream.next_in += stillToRemove;
            stillToRemove = 0;
        } else {
            std::memcpy( footerBuffer.data() + footerSize, m_stream.next_in, m_stream.avail_in );
            footerSize += m_stream.avail_in;

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

    Footer result;
    result.gzipFooter = footer;
    result.footerEndEncodedOffset = tellCompressed();
    return result;
}


inline void
ZlibInflateWrapper::readGzipHeader()
{
    const auto oldNextOut = m_stream.next_out;

    /* Note that inflateInit and inflateReset set total_out to 0 among other things. */
    if ( inflateReset2( &m_stream, /* decode gzip */ 16 + /* 2^15 buffer */ 15 ) != Z_OK ) {
        throw std::logic_error( "Probably encountered invalid gzip header!" );
    }

    gz_header gzipHeader{};
    if ( inflateGetHeader( &m_stream, &gzipHeader ) != Z_OK ) {
        throw std::logic_error( "Failed to initialize gzip header structure. Inconsistent zlib stream object?" );
    }

    refillBuffer();
    while ( ( m_stream.avail_in > 0 ) && ( gzipHeader.done == 0 ) ) {
        const auto errorCode = ::inflate( &m_stream, Z_BLOCK );
        if ( errorCode != Z_OK ) {
            /* Even Z_STREAM_END would be unexpected here because we test for avail_in > 0. */
            throw std::runtime_error( "Failed to parse gzip header!" );
        }

        if ( gzipHeader.done == 1 ) {
            break;
        }

        if ( gzipHeader.done == 0 ) {
            refillBuffer();
            continue;
        }

        throw std::runtime_error( "Failed to parse gzip header! Is it a Zlib stream?" );
    }

    if ( m_stream.next_out != oldNextOut ) {
        throw std::logic_error( "Zlib wrote some output even though we only wanted to read the gzip header!" );
    }

    if ( inflateReset2( &m_stream, m_windowFlags ) != Z_OK ) {
        throw std::logic_error( "Probably encountered invalid gzip header!" );
    }
}
}  // namespace rapidgzip
