#pragma once

#include <string>


namespace pragzip
{
enum class Error
{
    NONE                        = 0x00,

    EOF_ZERO_STRING             = 0x10,
    EOF_UNCOMPRESSED            = 0x11,

    EXCEEDED_CL_LIMIT           = 0x20,
    EXCEEDED_SYMBOL_RANGE       = 0x21,
    EXCEEDED_LITERAL_RANGE      = 0x22,
    EXCEEDED_DISTANCE_RANGE     = 0x23,
    EXCEEDED_WINDOW_RANGE       = 0x24,

    EMPTY_INPUT                 = 0x30,

    INVALID_HUFFMAN_CODE        = 0x40,
    NON_ZERO_PADDING            = 0x41,
    LENGTH_CHECKSUM_MISMATCH    = 0x42,
    INVALID_COMPRESSION         = 0x43,
    INVALID_CL_BACKREFERENCE    = 0x44,
    INVALID_BACKREFERENCE       = 0x45,
    EMPTY_ALPHABET              = 0x46,
    INVALID_GZIP_HEADER         = 0x47,
    INVALID_CODE_LENGTHS        = 0x48,
    BLOATING_HUFFMAN_CODING     = 0x49,

    UNEXPECTED_LAST_BLOCK       = 0x50,
};


[[nodiscard]] static inline std::string
toString( Error error )
{
    switch ( error )
    {
    case Error::EOF_ZERO_STRING:
        return "End of file encountered when trying to read zero-terminated string!";
    case Error::EOF_UNCOMPRESSED:
        return "End of file encountered when trying to copy uncompressed block from file!";
    case Error::EMPTY_ALPHABET:
        return "All code lengths are zero!";
    case Error::EXCEEDED_CL_LIMIT:
        return "The number of code lengths may not exceed the maximum possible value!";
    case Error::EMPTY_INPUT:
        return "Container must not be empty!";
    case Error::EXCEEDED_SYMBOL_RANGE:
        return "The range of the symbol type cannot represent the implied alphabet!";
    case Error::INVALID_HUFFMAN_CODE:
        return "Failed to decode Huffman bits!";
    case Error::NON_ZERO_PADDING:
        return "Assumed padding seems to contain some kind of data!";
    case Error::LENGTH_CHECKSUM_MISMATCH:
        return "Integrity check for length of uncompressed deflate block failed!";
    case Error::INVALID_COMPRESSION:
        return "Invalid block compression type!";
    case Error::EXCEEDED_LITERAL_RANGE:
        return "Invalid number of literal/length codes!";
    case Error::EXCEEDED_DISTANCE_RANGE:
        return "Invalid number of distance codes!";
    case Error::INVALID_CL_BACKREFERENCE:
        return "Cannot copy last length because this is the first one!";
    case Error::INVALID_BACKREFERENCE:
        return "Backreferenced data does not exist!";
    case Error::INVALID_GZIP_HEADER:
        return "Invalid gzip magic bytes!";
    case Error::INVALID_CODE_LENGTHS:
        return "Constructing a Huffman coding from the given code length sequence failed!";
    case Error::BLOATING_HUFFMAN_CODING:
        return "The Huffman coding is not optimal!";
    case Error::UNEXPECTED_LAST_BLOCK:
        return "The block is the last of the stream even though it should not be!";
    case Error::EXCEEDED_WINDOW_RANGE:
        return "The backreferenced distance lies outside the window buffer!";
    case Error::NONE:
        return "No error.";
    }
    return "Unknown error code!";
}
}  // namespace pragzip
