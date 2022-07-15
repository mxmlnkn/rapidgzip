#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>


[[nodiscard]] inline bool
isLittleEndian()
{
    constexpr uint16_t endianTestNumber = 1;
    return *reinterpret_cast<const uint8_t*>( &endianTestNumber ) == 1;
}


[[nodiscard]] constexpr uint64_t
byteSwap( uint64_t value )
{
    value = ( ( value & uint64_t( 0x0000'0000'FFFF'FFFFULL ) ) << 32 ) |
            ( ( value & uint64_t( 0xFFFF'FFFF'0000'0000ULL ) ) >> 32 );
    value = ( ( value & uint64_t( 0x0000'FFFF'0000'FFFFULL ) ) << 16 ) |
            ( ( value & uint64_t( 0xFFFF'0000'FFFF'0000ULL ) ) >> 16 );
    value = ( ( value & uint64_t( 0x00FF'00FF'00FF'00FFULL ) ) << 8  ) |
            ( ( value & uint64_t( 0xFF00'FF00'FF00'FF00ULL ) ) >> 8  );
    return value;
}

[[nodiscard]] constexpr uint32_t
byteSwap( uint32_t value )
{
    value = ( ( value & uint32_t( 0x0000'FFFFUL ) ) << 16 ) | ( ( value & uint32_t( 0xFFFF'0000UL ) ) >> 16 );
    value = ( ( value & uint32_t( 0x00FF'00FFUL ) ) << 8  ) | ( ( value & uint32_t( 0xFF00'FF00UL ) ) >> 8  );
    return value;
}

[[nodiscard]] constexpr uint16_t
byteSwap( uint16_t value )
{
    value = ( ( value & uint16_t( 0x00FFU ) ) << 8  ) | ( ( value & uint16_t( 0xFF00U ) ) >> 8  );
    return value;
}


/**
 * @verbatim
 * 63                48                  32                  16        8         0
 * |                 |                   |                   |         |         |
 * 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 1111 1111 1111
 *                                                                  <------------>
 *                                                                   nBitsSet = 12
 * @endverbatim
 *
 * @param nBitsSet the number of lowest bits which should be 1 (rest are 0)
 */
template<typename T>
[[nodiscard]] constexpr T
nLowestBitsSet( uint8_t nBitsSet )
{
    static_assert( std::is_unsigned_v<T>, "Type must be unsigned!" );
    if ( nBitsSet == 0 ) {
        return T(0);
    }
    if ( nBitsSet >= std::numeric_limits<T>::digits ) {
        return static_cast<T>( ~T(0) );
    }
    const auto nZeroBits = std::max( 0, std::numeric_limits<T>::digits - nBitsSet );
    return static_cast<T>( static_cast<T>( ~T(0) ) >> nZeroBits );
}


template<typename T, uint8_t nBitsSet>
[[nodiscard]] constexpr T
nLowestBitsSet()
{
    static_assert( std::is_unsigned_v<T>, "Type must be unsigned!" );
    if constexpr ( nBitsSet == 0 ) {
        return T(0);
    } else if constexpr ( nBitsSet >= std::numeric_limits<T>::digits ) {
        return static_cast<T>( ~T(0) );
    } else {
        const auto nZeroBits = std::max( 0, std::numeric_limits<T>::digits - nBitsSet );
        return static_cast<T>( static_cast<T>( ~T(0) ) >> nZeroBits );
    }
}


template<typename T>
[[nodiscard]] constexpr T
nHighestBitsSet( uint8_t nBitsSet )
{
    static_assert( std::is_unsigned_v<T>, "Type must be unsigned!" );
    if ( nBitsSet == 0 ) {
        return T(0);
    }
    if ( nBitsSet >= std::numeric_limits<T>::digits ) {
        return static_cast<T>( ~T(0) );
    }
    const auto nZeroBits = std::max( 0, std::numeric_limits<T>::digits - nBitsSet );
    return static_cast<T>( static_cast<T>( ~T(0) ) << nZeroBits );
}


template<typename T, uint8_t nBitsSet>
[[nodiscard]] constexpr T
nHighestBitsSet()
{
    static_assert( std::is_unsigned_v<T>, "Type must be unsigned!" );
    if constexpr ( nBitsSet == 0 ) {
        return T(0);
    } else if constexpr ( nBitsSet >= std::numeric_limits<T>::digits ) {
        return static_cast<T>( ~T(0) );
    } else {
        const auto nZeroBits = std::max( 0, std::numeric_limits<T>::digits - nBitsSet );
        return static_cast<T>( static_cast<T>( ~T(0) ) << nZeroBits );
    }
}


[[nodiscard]] constexpr uint8_t
reverseBits( uint8_t data )
{
    /* Reverse bits using bit-parallelism in a recursive fashion, i.e., first swap every bit with its neighbor,
     * then swap each half nibble with its neighbor, then each nibble. */
    constexpr std::array<uint8_t, 3> masks = { 0b0101'0101U, 0b0011'0011U, 0b0000'1111U };
    for ( uint8_t i = 0; i < masks.size(); ++i ) {
        data = ( ( data &  masks[i] ) << ( 1U << i ) ) |
               ( ( data & ~masks[i] ) >> ( 1U << i ) );
    }
    return data;
}


[[nodiscard]] constexpr uint16_t
reverseBits( uint16_t data )
{
    /* Reverse bits using bit-parallelism in a recursive fashion, i.e., first swap every bit with its neighbor,
     * then swap each half nibble with its neighbor, then each nibble, then each byte. */
    constexpr std::array<uint16_t, 4> masks = {
        0b0101'0101'0101'0101U,
        0b0011'0011'0011'0011U,
        0b0000'1111'0000'1111U,
        0b0000'0000'1111'1111U,
    };
    for ( uint8_t i = 0; i < masks.size(); ++i ) {
        data = ( ( data &  masks[i] ) << ( 1U << i ) ) |
               ( ( data & ~masks[i] ) >> ( 1U << i ) );
    }
    return data;
}


[[nodiscard]] constexpr uint32_t
reverseBits( uint32_t data )
{
    /* Reverse bits using bit-parallelism in a recursive fashion, i.e., first swap every bit with its neighbor,
     * then swap each half nibble with its neighbor, then each nibble, then each byte, then each word. */
    constexpr std::array<uint32_t, 5> masks = {
        0b0101'0101'0101'0101'0101'0101'0101'0101U,
        0b0011'0011'0011'0011'0011'0011'0011'0011U,
        0b0000'1111'0000'1111'0000'1111'0000'1111U,
        0b0000'0000'1111'1111'0000'0000'1111'1111U,
        0b0000'0000'0000'0000'1111'1111'1111'1111U,
    };
    for ( uint8_t i = 0; i < masks.size(); ++i ) {
        data = ( ( data &  masks[i] ) << ( 1U << i ) ) |
               ( ( data & ~masks[i] ) >> ( 1U << i ) );
    }
    return data;
}


[[nodiscard]] constexpr uint64_t
reverseBits( uint64_t data )
{
    /* Reverse bits using bit-parallelism in a recursive fashion, i.e., first swap every bit with its neighbor,
     * then swap each half nibble with its neighbor, then each nibble, then each byte, then each word, then each
     * double word. */
    constexpr std::array<uint64_t, 6> masks = {
        0b0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101U,
        0b0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011U,
        0b0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111U,
        0b0000'0000'1111'1111'0000'0000'1111'1111'0000'0000'1111'1111'0000'0000'1111'1111U,
        0b0000'0000'0000'0000'1111'1111'1111'1111'0000'0000'0000'0000'1111'1111'1111'1111U,
        0b0000'0000'0000'0000'0000'0000'0000'0000'1111'1111'1111'1111'1111'1111'1111'1111U,
    };
    /* Using godbolt, shows that both gcc 8.6 and clang will unroll this loop! Clang 13 seems to produce even shorter
     * code by somehow skipping word and dword swaps, maybe implementing those via moves instead. Both, evaluate
     * the 1U << i and ~masks[i] expressions at compile-time. Note that ~mask is -mask on two's-complement platforms! */
    for ( uint8_t i = 0; i < masks.size(); ++i ) {
        data = ( ( data &  masks[i] ) << ( 1U << i ) ) |
               ( ( data & ~masks[i] ) >> ( 1U << i ) );
    }
    return data;
}


template<typename T>
[[nodiscard]] constexpr std::array<T, std::numeric_limits<T>::max()>
createReversedBitsLUT()
{
    static_assert( std::is_unsigned_v<T>, "Type must be unsigned!" );
    std::array<T, std::numeric_limits<T>::max()> result{};
    for ( T i = 0; i < result.size(); ++i ) {
        result[i] = reverseBits( i );
    }
    return result;
}


alignas(8) static constexpr std::array<uint16_t, std::numeric_limits<uint16_t>::max() >
reversedBitsLUT16 = createReversedBitsLUT<uint16_t>();
