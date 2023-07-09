#pragma once

#include <array>
#include <cstddef>
#include <iostream>
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
class ZlibDeflateWrapper
{
public:
    explicit
    ZlibDeflateWrapper( BitReader bitReader ) :
        m_bitReader( std::move( bitReader ) )
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

    ~ZlibDeflateWrapper()
    {
        inflateEnd( &m_stream );
    }

    void
    initStream()
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

    void
    refillBuffer()
    {
        if ( m_stream.avail_in > 0 ) {
            return;
        }

        if ( m_bitReader.tell() % BYTE_SIZE != 0 ) {
            const auto nBitsToPrime = BYTE_SIZE - ( m_bitReader.tell() % BYTE_SIZE );
            if ( inflatePrime( &m_stream, nBitsToPrime, m_bitReader.read( nBitsToPrime ) ) != Z_OK ) {
                throw std::runtime_error( "InflatePrime failed!" );
            }
            assert( m_bitReader.tell() % BYTE_SIZE == 0 );
        }

        m_stream.avail_in = m_bitReader.read(
            m_buffer.data(), std::min( ( m_bitReader.size() - m_bitReader.tell() ) / BYTE_SIZE, m_buffer.size() ) );
        m_stream.next_in = reinterpret_cast<unsigned char*>( m_buffer.data() );
    }

    void
    setWindow( VectorView<uint8_t> const& window )
    {
        if ( inflateSetDictionary( &m_stream, window.data(), window.size() ) != Z_OK ) {
            throw std::runtime_error( "Failed to set back-reference window in zlib!" );
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
            if ( m_stream.avail_in == 0 ) {
                break;
            }

            const auto errorCode = inflate( &m_stream, Z_BLOCK );
            if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
                std::stringstream message;
                message << "[" << std::this_thread::get_id() << "] "
                        << "Decoding failed with error code " << errorCode << " "
                        << ( m_stream.msg == nullptr ? "" : m_stream.msg ) << "! "
                        << "Already decoded " << m_stream.total_out << " B.";
                throw std::runtime_error( std::move( message ).str() );
            }

            if ( decodedSize + m_stream.total_out > outputSize ) {
                throw std::logic_error( "Decoded more than fits into the output buffer!" );
            }

            if ( errorCode == Z_STREAM_END ) {
                decodedSize += m_stream.total_out;

                /* If we started with raw deflate, then we also have to skip other the gzip footer.
                 * Assuming we are decoding gzip and not zlib or multiple raw deflate streams. */
                std::optional<gzip::Footer> footer;
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
        }

        return { decodedSize + m_stream.total_out, std::nullopt };
    }

    [[nodiscard]] size_t
    tellEncoded() const
    {
        return m_bitReader.tell() - m_stream.avail_in * BYTE_SIZE;
    }

private:
    /**
     * Only works on and modifies m_stream.avail_in and m_stream.next_in.
     */
    std::optional<gzip::Footer>
    readGzipFooter()
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

        return footer;
    }

    void
    readGzipHeader()
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
            const auto errorCode = inflate( &m_stream, Z_BLOCK );
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

private:
    BitReader m_bitReader;
    int m_windowFlags{ 0 };
    z_stream m_stream{};
    /* Loading the whole encoded data (multiple MiB) into memory first and then
     * decoding it in one go is 4x slower than processing it in chunks of 128 KiB! */
    std::array<char, 128_Ki> m_buffer;
};
}  // namespace rapidgzip
