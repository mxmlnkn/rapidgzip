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


namespace pragzip
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
        /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode. */
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

    [[nodiscard]] size_t
    read( uint8_t* const output,
          size_t   const outputSize )
    {
        m_stream.next_out = output;
        m_stream.avail_out = outputSize;
        m_stream.total_out = 0;

        size_t decodedSize{ 0 };
        while ( decodedSize + m_stream.total_out < outputSize ) {
            refillBuffer();
            if ( m_stream.avail_in == 0 ) {
                throw std::runtime_error( "Not enough input for requested output!" );
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
                throw std::logic_error( "Decoded more than fits into output buffer!" );
            }
            if ( decodedSize + m_stream.total_out == outputSize ) {
                return outputSize;
            }

            if ( errorCode == Z_STREAM_END ) {
                decodedSize += m_stream.total_out;

                const auto oldStream = m_stream;
                inflateEnd( &m_stream );  // All dynamically allocated data structures for this stream are freed.
                initStream();
                m_stream.avail_in = oldStream.avail_in;
                m_stream.next_in = oldStream.next_in;
                m_stream.total_out = oldStream.total_out;

                /* If we started with raw deflate, then we also have to skip other the gzip footer.
                 * Assuming we are decoding gzip and not zlib or multiple raw deflate streams. */
                if ( m_windowFlags < 0 ) {
                    for ( auto stillToRemove = 8U; stillToRemove > 0; ) {
                        if ( m_stream.avail_in >= stillToRemove ) {
                            m_stream.avail_in -= stillToRemove;
                            m_stream.next_in += stillToRemove;
                            stillToRemove = 0;
                        } else {
                            stillToRemove -= m_stream.avail_in;
                            m_stream.avail_in = 0;
                            refillBuffer();
                        }
                    }
                }

                /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode.
                 * > The current implementation of inflateInit2() does not process any header information --
                 * > that is deferred until inflate() is called.
                 * Because of this, we don't have to ensure that enough data is available and/or calling it a
                 * second time to read the rest of the header. */
                m_windowFlags = /* decode gzip */ 16 + /* 2^15 buffer */ 15;
                if ( inflateInit2( &m_stream, m_windowFlags ) != Z_OK ) {
                    throw std::runtime_error( "Probably encountered invalid gzip header!" );
                }

                m_stream.next_out = output + decodedSize;
                m_stream.avail_out = outputSize - decodedSize;
            }

            if ( m_stream.avail_out == 0 ) {
                return outputSize;
            }
        }

        return decodedSize;
    }

private:
    BitReader m_bitReader;
    int m_windowFlags{ 0 };
    z_stream m_stream{};
    /* Loading the whole encoded data (multiple MiB) into memory first and then
     * decoding it in one go is 4x slower than processing it in chunks of 128 KiB! */
    std::array<char, 128_Ki> m_buffer;
};
}  // namespace pragzip
