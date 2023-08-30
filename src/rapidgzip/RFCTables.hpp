#pragma once

#include <array>
#include <cstdint>


namespace rapidgzip::deflate
{
/* Distance Code Table */

/**
 * Currently, only used for tests.
 */
[[nodiscard]] constexpr uint16_t
calculateDistance( uint16_t distance,
                   uint8_t  extraBitsCount,
                   uint16_t extraBits ) noexcept
{
    assert( distance >= 4 );
    return 1U + ( 1U << ( extraBitsCount + 1U ) ) + ( ( distance % 2U ) << extraBitsCount ) + extraBits;
};


[[nodiscard]] constexpr uint16_t
calculateDistanceExtraBits( uint16_t distance ) noexcept
{
    return distance <= 3U ? 0U : ( distance - 2U ) / 2U;
}


/**
 * This only makes sense to use for LUT creation because, else, calculating the extra bits count
 * would be work done twice.
 * @return A kind of intermediary distance. In order to get the real distance,
 *         extraBits need to be added to the returned value.
 */
[[nodiscard]] constexpr uint16_t
calculateDistance( uint16_t distance ) noexcept
{
    assert( distance >= 4 );
    const auto extraBitsCount = calculateDistanceExtraBits( distance );
    return 1U + ( 1U << ( extraBitsCount + 1U ) ) + ( ( distance % 2U ) << extraBitsCount );
};


using DistanceLUT = std::array<uint16_t, 30>;

[[nodiscard]] constexpr DistanceLUT
createDistanceLUT() noexcept
{
    DistanceLUT result{};
    for ( uint16_t i = 0; i < 4; ++i ) {
        result[i] = i + 1;
    }
    for ( uint16_t i = 4; i < result.size(); ++i ) {
        result[i] = calculateDistance( i );
    }
    return result;
}


alignas( 8 ) static constexpr DistanceLUT
distanceLUT = createDistanceLUT();


/* Length Code Table */


[[nodiscard]] constexpr uint16_t
calculateLength( uint16_t code ) noexcept
{
    assert( code < 285 - 261 );
    const auto extraBits = code / 4U;
    return 3U + ( 1U << ( extraBits + 2U ) ) + ( ( code % 4U ) << extraBits );
};


using LengthLUT = std::array<uint16_t, 285 - 261>;

[[nodiscard]] constexpr LengthLUT
createLengthLUT() noexcept
{
    LengthLUT result{};
    for ( uint16_t i = 0; i < result.size(); ++i ) {
        result[i] = calculateLength( i );
    }
    return result;
}


alignas( 8 ) static constexpr LengthLUT
lengthLUT = createLengthLUT();
}  // namespace rapidgzip::deflate
