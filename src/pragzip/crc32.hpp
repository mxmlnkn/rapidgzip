#pragma once

#include <array>
#include <cstdint>


namespace pragzip
{
/* CRC32 according to RFC 1952 */

using CRC32LookupTable = std::array<uint32_t, 256>;

[[nodiscard]] constexpr CRC32LookupTable
createCRC32LookupTable() noexcept
{
    CRC32LookupTable table{};
    for ( uint32_t n = 0; n < table.size(); ++n ) {
        auto c = static_cast<unsigned long>( n );
        for ( int j = 0; j < 8; ++j ) {
            if ( c & 1UL ) {
                c = 0xEDB88320UL ^ ( c >> 1 );
            } else {
                c >>= 1;
            }
        }
        table[n] = c;
    }
    return table;
}

static constexpr int CRC32_LOOKUP_TABLE_SIZE = 256;

/* a small lookup table: raw data -> CRC32 value to speed up CRC calculation */
alignas(8) constexpr static CRC32LookupTable CRC32_TABLE = createCRC32LookupTable();

[[nodiscard]] constexpr uint32_t
updateCRC32( uint32_t crc,
             uint8_t  data ) noexcept
{
    const auto result = ( crc >> 8U ) ^ CRC32_TABLE[( crc ^ data ) & 0xFFU];
    return result;
}
}
