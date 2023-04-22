#pragma once

#include <cstddef>
#include <iostream>
#include <string_view>

#include "zlib.h"


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
        status = deflate( &stream, Z_FINISH );
    }

    deflateEnd( &stream );

    output.resize( stream.total_out );
    output.shrink_to_fit();

    return output;
}
