#pragma once

#include <BitReader.hpp>


namespace pragzip
{
/**
 * Using 64-bit instead of 32-bit improved performance by 10% when it was introduced.
 * This might be because of rarer (but longer) refilling of the bit buffer, which might
 * improve pipelining and branch prediction a bit.
 */
using BitReader = ::BitReader<false, uint64_t>;


/* Not that this describes bytes in the data format not on the host system, which is CHAR_BIT and might differ. */
constexpr auto BYTE_SIZE = 8;


/** For this namespace, refer to @see RFC 1951 "DEFLATE Compressed Data Format Specification version 1.3" */
namespace deflate
{
constexpr size_t MAX_WINDOW_SIZE = 32 * 1024;
/** This is because the length of an uncompressed block is a 16-bit number. */
constexpr size_t MAX_UNCOMPRESSED_SIZE = std::numeric_limits<uint16_t>::max();
/** This is because the code length alphabet can't encode any higher value and because length 0 is ignored! */
constexpr uint8_t MAX_CODE_LENGTH = 15;

constexpr size_t MAX_LITERAL_OR_LENGTH_SYMBOLS = 286;
constexpr uint8_t MAX_DISTANCE_SYMBOL_COUNT = 30;
/* next power of two (because binary tree) of MAX_LITERAL_OR_LENGTH_SYMBOLS. This is assuming that all symbols
 * are equally likely to appear, i.e., all codes would be encoded with the same number of bits (9). */
constexpr size_t MAX_LITERAL_HUFFMAN_CODE_COUNT = 512;
constexpr size_t MAX_RUN_LENGTH = 258;

enum class CompressionType : uint8_t
{
    UNCOMPRESSED    = 0b00,
    FIXED_HUFFMAN   = 0b01,
    DYNAMIC_HUFFMAN = 0b10,
    RESERVED        = 0b11,
};
}  // namespace deflate
}  // namespace pragzip
