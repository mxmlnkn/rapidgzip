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
} // namespace pragzip
