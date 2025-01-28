#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <BitManipulation.hpp>
#include <BitStringFinder.hpp>
#include <bzip2.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Standard.hpp>
#include <ParallelBitStringFinder.hpp>
#include <Statistics.hpp>
#include <common.hpp>

//#define BENCHMARK


using namespace rapidgzip;


namespace
{
constexpr uint64_t bitStringToFind = 0x314159265359;  /* bcd(pi) */
//constexpr uint64_t bitStringToFind = 0x177245385090ULL; /* bcd(sqrt(pi)) */
constexpr uint8_t bitStringToFindSize = 48;
}


#if 0
/**
 * @param bitString the lowest bitStringSize bits will be looked for in the buffer
 * @return size_t max if not found else position in buffer
 */
[[nodiscard]] size_t
findBitString( const char* buffer,
               size_t      bufferSize,
               uint64_t    bitString,
               uint8_t     bitStringSize )
{
    if ( bufferSize * 8 < bitStringSize ) {
        return std::numeric_limits<size_t>::max();
    }
    assert( bufferSize % sizeof( uint64_t ) == 0 );

    /* create lookup table */
    const auto nWildcardBits = sizeof( uint64_t ) * 8 - bitStringSize;
    std::vector<uint64_t> shiftedBitStrings( nWildcardBits );
    std::vector<uint64_t> shiftedBitMasks( nWildcardBits );

    uint64_t shiftedBitString = bitString;
    uint64_t shiftedBitMask = std::numeric_limits<uint64_t>::max() >> nWildcardBits;
    for ( size_t i = 0; i < nWildcardBits; ++i ) {
        shiftedBitStrings[i] = shiftedBitString;
        shiftedBitMasks  [i] = shiftedBitMask;

        shiftedBitString <<= 1;
        shiftedBitMask   <<= 1;
    }

    /**
     *  0  1  2  3  4  5  6  7  8  9
     *  42 5a 68 31 31 41 59 26 53 59
     */
    const auto minBytesForSearchString = ( (size_t)bitStringSize + 8U - 1U ) / 8U;
    uint64_t bytes = 0;
    for ( size_t i = 0; i < std::min( minBytesForSearchString, bufferSize ); ++i ) {
        bytes = ( bytes << 8U ) | static_cast<uint8_t>( buffer[i] );
    }

    assert( bitStringSize == 48 );  /* this allows us to fixedly load always two bytes (16 bits) */
    for ( size_t i = 0; i < bufferSize; ++i ) {
        bytes = ( bytes << 8U ) | static_cast<uint8_t>( buffer[i] );
        if ( ++i >= bufferSize ) {
            break;
        }
        bytes = ( bytes << 8U ) | static_cast<uint8_t>( buffer[i] );

        for ( size_t j = 0; j < shiftedBitStrings.size(); ++j ) {
            if ( ( bytes & shiftedBitMasks[j] ) == shiftedBitStrings[j] ) {
                return ( i + 1 ) * 8 - bitStringSize - j;
            }
        }
    }

    return std::numeric_limits<size_t>::max();
}
#endif


template<uint8_t bitStringSize>
[[nodiscard]] constexpr auto
createdShiftedBitStringLUTArray( uint64_t bitString )
{
    constexpr auto nWildcardBits = sizeof( uint64_t ) * 8 - bitStringSize;
    using ShiftedLUTTable = std::array<std::pair</* shifted value to compare to */ uint64_t, /* mask */ uint64_t>,
                                       nWildcardBits>;
    ShiftedLUTTable shiftedBitStrings;

    uint64_t shiftedBitString = bitString;
    uint64_t shiftedBitMask = std::numeric_limits<uint64_t>::max() >> nWildcardBits;
    for ( size_t i = 0; i < nWildcardBits; ++i ) {
        shiftedBitStrings[i] = std::make_pair( shiftedBitString, shiftedBitMask );
        shiftedBitString <<= 1U;
        shiftedBitMask   <<= 1U;
    }

    return shiftedBitStrings;
}


[[nodiscard]] auto
createdShiftedBitStringLUT( uint64_t bitString,
                            uint8_t  bitStringSize,
                            bool     includeLastFullyShifted = false )
{
    const auto nWildcardBits = sizeof( uint64_t ) * CHAR_BIT - bitStringSize + ( includeLastFullyShifted ? 1 : 0 );
    using ShiftedLUTTable = std::vector<std::pair</* shifted value to compare to */ uint64_t, /* mask */ uint64_t> >;
    ShiftedLUTTable shiftedBitStrings( nWildcardBits );

    uint64_t shiftedBitString = bitString;
    uint64_t shiftedBitMask = std::numeric_limits<uint64_t>::max() >> nWildcardBits;
    for ( size_t i = 0; i < shiftedBitStrings.size(); ++i ) {
        shiftedBitStrings[shiftedBitStrings.size() - 1 - i] = std::make_pair( shiftedBitString, shiftedBitMask );
        shiftedBitString <<= 1U;
        shiftedBitMask   <<= 1U;
        assert( ( shiftedBitString & shiftedBitMask ) == shiftedBitString );
    }

    return shiftedBitStrings;
}


/**
 * @param bitString the lowest bitStringSize bits will be looked for in the buffer
 * @return size_t max if not found else position in buffer
 */
template<uint8_t bitStringSize>
[[nodiscard]] size_t
findBitString( const uint8_t* buffer,
               size_t         bufferSize,
               uint64_t       bitString,
               uint8_t        firstBitsToIgnore = 0 )
{
    #if 1
    const auto shiftedBitStrings = createdShiftedBitStringLUT( bitString, bitStringSize );
    #elif 0
    /* Not much of a difference. If anything, it feels like 1% slower */
    const auto shiftedBitStrings = createdShiftedBitStringLUTArray<bitStringSize>( bitString );
    #else
    /* This version actually takes 50% longer for some reason! */
    constexpr std::array<std::pair<uint64_t, uint64_t>, 17> shiftedBitStrings = {
        std::make_pair( 0x0000'3141'5926'5359ULL, 0x0000'ffff'ffff'ffffULL ),
        std::make_pair( 0x0000'6282'b24c'a6b2ULL, 0x0001'ffff'ffff'fffeULL ),
        std::make_pair( 0x0000'c505'6499'4d64ULL, 0x0003'ffff'ffff'fffcULL ),
        std::make_pair( 0x0001'8a0a'c932'9ac8ULL, 0x0007'ffff'ffff'fff8ULL ),
        std::make_pair( 0x0003'1415'9265'3590ULL, 0x000f'ffff'ffff'fff0ULL ),
        std::make_pair( 0x0006'282b'24ca'6b20ULL, 0x001f'ffff'ffff'ffe0ULL ),
        std::make_pair( 0x000c'5056'4994'd640ULL, 0x003f'ffff'ffff'ffc0ULL ),
        std::make_pair( 0x0018'a0ac'9329'ac80ULL, 0x007f'ffff'ffff'ff80ULL ),
        std::make_pair( 0x0031'4159'2653'5900ULL, 0x00ff'ffff'ffff'ff00ULL ),
        std::make_pair( 0x0062'82b2'4ca6'b200ULL, 0x01ff'ffff'ffff'fe00ULL ),
        std::make_pair( 0x00c5'0564'994d'6400ULL, 0x03ff'ffff'ffff'fc00ULL ),
        std::make_pair( 0x018a'0ac9'329a'c800ULL, 0x07ff'ffff'ffff'f800ULL ),
        std::make_pair( 0x0314'1592'6535'9000ULL, 0x0fff'ffff'ffff'f000ULL ),
        std::make_pair( 0x0628'2b24'ca6b'2000ULL, 0x1fff'ffff'ffff'e000ULL ),
        std::make_pair( 0x0c50'5649'94d6'4000ULL, 0x3fff'ffff'ffff'c000ULL ),
        std::make_pair( 0x18a0'ac93'29ac'8000ULL, 0x7fff'ffff'ffff'8000ULL ),
        std::make_pair( 0x3141'5926'5359'0000ULL, 0xffff'ffff'ffff'0000ULL )
    };
    #endif

    #if 0
    std::cerr << "Shifted Bit Strings:\n";
    for ( const auto [shifted, mask] : shiftedBitStrings ) {
        std::cerr << "0x" << std::hex << shifted << " 0x" << mask << "\n";
    }
    #endif

    /* Simply load bytewise even if we could load more (uneven) bits by rounding down.
     * This makes this implementation much less performant in comparison to the "% 8 = 0" version! */
    constexpr auto nBytesToLoadPerIteration = ( sizeof( uint64_t ) * CHAR_BIT - bitStringSize ) / CHAR_BIT;
    static_assert( nBytesToLoadPerIteration > 0,
                   "Bit string size must be smaller than or equal to 56 bit in order to load bytewise!" );

    /* Initialize buffer window. Note that we can't simply read an uint64_t because of the bit and byte order */
    if ( bufferSize * CHAR_BIT < bitStringSize ) {
        return std::numeric_limits<size_t>::max();
    }
    //std::cerr << "nBytesToLoadPerIteration: " << nBytesToLoadPerIteration << "\n"; // 2
    uint64_t window = 0;
    size_t i = 0;
    for ( ; i < std::min( sizeof( uint64_t ), bufferSize ); ++i ) {
        window = ( window << static_cast<uint8_t>( CHAR_BIT ) ) | buffer[i];
    }

    {
        size_t k = 0;
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                const auto foundBitOffset = i * CHAR_BIT - bitStringSize - ( shiftedBitStrings.size() - 1 - k );
                if ( ( foundBitOffset >= firstBitsToIgnore ) && ( foundBitOffset < bufferSize * CHAR_BIT ) ) {
                    return foundBitOffset - firstBitsToIgnore;
                }
            }
            ++k;
        }
    }

    for ( ; i < bufferSize; ) {
        for ( size_t j = 0; ( j < nBytesToLoadPerIteration ) && ( i < bufferSize ); ++j, ++i ) {
            window = ( window << static_cast<uint8_t>( CHAR_BIT ) ) | buffer[i];
        }

        /* use pre-shifted search bit string values and masks to test for the search string in the larger window */
        static constexpr int LOOP_METHOD = 0;
        if constexpr ( LOOP_METHOD == 0 ) {
            /* AMD Ryzen 9 3900X clang++ 10.0.0-4ubuntu1       -O3 -DNDEBUG               : 1.7s */
            /* AMD Ryzen 9 3900X clang++ 10.0.0-4ubuntu1       -O3 -DNDEBUG -march=native : 1.8s */
            /* AMD Ryzen 9 3900X g++     10.2.0-5ubuntu1~20.04 -O3 -DNDEBUG               : 2.8s */
            /* AMD Ryzen 9 3900X g++     10.2.0-5ubuntu1~20.04 -O3 -DNDEBUG -march=native : 3.0s */
            size_t k = 0;
            for ( const auto& [shifted, mask] : shiftedBitStrings ) {
                if ( ( window & mask ) == shifted ) {
                    return i * CHAR_BIT - bitStringSize - ( shiftedBitStrings.size() - 1 - k );
                }
                ++k;
            }
        } else if constexpr ( LOOP_METHOD == 1 ) {
            /* AMD Ryzen 9 3900X clang++ 10.0.0-4ubuntu1       -O3 -DNDEBUG: 2.0s */
            /* AMD Ryzen 9 3900X g++     10.2.0-5ubuntu1~20.04 -O3 -DNDEBUG: 3.3s */
            for ( size_t k = 0; k < shiftedBitStrings.size(); ++k ) {
                const auto& [shifted, mask] = shiftedBitStrings[k];
                if ( ( window & mask ) == shifted ) {
                    return i * CHAR_BIT - bitStringSize - k;
                }
            }
        } else if constexpr ( LOOP_METHOD == 2 ) {
            /* AMD Ryzen 9 3900X clang++ 10.0.0-4ubuntu1       -O3 -DNDEBUG               : 2.0s */
            /* AMD Ryzen 9 3900X clang++ 10.0.0-4ubuntu1       -O3 -DNDEBUG -march=native : 2.1s */
            /* AMD Ryzen 9 3900X g++     10.2.0-5ubuntu1~20.04 -O3 -DNDEBUG               : 3.3s */
            /* AMD Ryzen 9 3900X g++     10.2.0-5ubuntu1~20.04 -O3 -DNDEBUG -march=native : 3.6s */
            for ( size_t k = 0; k < shiftedBitStrings.size(); ++k ) {
                if ( ( window & shiftedBitStrings[k].second ) == shiftedBitStrings[k].first ) {
                    return i * CHAR_BIT - bitStringSize - k;
                }
            }
        } else if constexpr ( LOOP_METHOD == 3 ) {
            /* AMD Ryzen 9 3900X clang++ 10.0.0-4ubuntu1 -O3 -DNDEBUG : 2.0s */
            const auto match = std::find_if(
                shiftedBitStrings.begin(), shiftedBitStrings.end(),
                [window] ( const auto& pair ) { return ( window & pair.second ) == pair.first; }
            );
            if ( match != shiftedBitStrings.end() ) {
                return i * CHAR_BIT - bitStringSize - ( match - shiftedBitStrings.begin() );
            }
        }
    }

    return std::numeric_limits<size_t>::max();
}


[[nodiscard]] size_t
findBitStringNonTemplated( const uint8_t* buffer,
                           size_t         bufferSize,
                           uint64_t       bitString,
                           uint8_t        bitStringSize,
                           uint8_t        firstBitsToIgnore = 0 )
{
    const auto shiftedBitStrings = createdShiftedBitStringLUT( bitString, bitStringSize );

    /* Simply load bytewise even if we could load more (uneven) bits by rounding down.
     * This makes this implementation much less performant in comparison to the "% 8 = 0" version! */
    const auto nBytesToLoadPerIteration = ( sizeof( uint64_t ) * CHAR_BIT - bitStringSize ) / CHAR_BIT;

    /* Initialize buffer window. Note that we can't simply read an uint64_t because of the bit and byte order */
    if ( bufferSize * CHAR_BIT < bitStringSize ) {
        return std::numeric_limits<size_t>::max();
    }
    //std::cerr << "nBytesToLoadPerIteration: " << nBytesToLoadPerIteration << "\n"; // 2
    uint64_t window = 0;
    size_t i = 0;
    for ( ; i < std::min( sizeof( uint64_t ), bufferSize ); ++i ) {
        window = ( window << static_cast<uint8_t>( CHAR_BIT ) ) | buffer[i];
    }

    {
        size_t k = 0;
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                const auto foundBitOffset = i * CHAR_BIT - bitStringSize - ( shiftedBitStrings.size() - 1 - k );
                if ( ( foundBitOffset >= firstBitsToIgnore ) && ( foundBitOffset < bufferSize * 8 ) ) {
                    return foundBitOffset - firstBitsToIgnore;
                }
            }
            ++k;
        }
    }

    for ( ; i < bufferSize; ) {
        for ( size_t j = 0; ( j < nBytesToLoadPerIteration ) && ( i < bufferSize ); ++j, ++i ) {
            window = ( window << static_cast<uint8_t>( CHAR_BIT ) ) | buffer[i];
        }

        size_t k = 0;
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                return i * CHAR_BIT - bitStringSize - ( shiftedBitStrings.size() - 1 - k );
            }
            ++k;
        }
    }

    return std::numeric_limits<size_t>::max();
}


template<uint64_t bitString,
         uint8_t  bitStringSize>
[[nodiscard]] constexpr auto
createdShiftedBitStringLUTArrayTemplated()
{
    constexpr auto nWildcardBits = sizeof( uint64_t ) * 8 - bitStringSize;
    using ShiftedLUTTable = std::array<std::pair</* shifted value to compare to */ uint64_t, /* mask */ uint64_t>,
                                       nWildcardBits>;
    ShiftedLUTTable shiftedBitStrings;

    uint64_t shiftedBitString = bitString;
    uint64_t shiftedBitMask = std::numeric_limits<uint64_t>::max() >> nWildcardBits;
    for ( size_t i = 0; i < nWildcardBits; ++i ) {
        shiftedBitStrings[i] = std::make_pair( shiftedBitString, shiftedBitMask );
        shiftedBitString <<= 1U;
        shiftedBitMask   <<= 1U;
    }

    return shiftedBitStrings;
}


template<uint64_t bitString,
         uint8_t  bitStringSize>
[[nodiscard]] constexpr auto
createdShiftedBitStringLUTArrayTemplatedConstexpr()
{
    constexpr auto nWildcardBits = sizeof( uint64_t ) * 8 - bitStringSize;
    using ShiftedLUTTable = std::array<std::pair</* shifted value to compare to */ uint64_t, /* mask */ uint64_t>,
                                       nWildcardBits>;
    ShiftedLUTTable shiftedBitStrings;

    constexpr auto shiftedBitMask = std::numeric_limits<uint64_t>::max() >> nWildcardBits;
    for ( size_t i = 0; i < nWildcardBits; ++i ) {
        shiftedBitStrings[i].first = bitString << i;
        shiftedBitStrings[i].second = shiftedBitMask << i;
    }

    return shiftedBitStrings;
}


template<uint64_t bitString,
         uint8_t  bitStringSize>
[[nodiscard]] size_t
findBitStringBitStringTemplated( const uint8_t* buffer,
                                 size_t         bufferSize,
                                 uint8_t        firstBitsToIgnore = 0 )
{
    const auto shiftedBitStrings = createdShiftedBitStringLUTArrayTemplated<bitString, bitStringSize>();  // 1.85s
    //constexpr auto shiftedBitStrings = createdShiftedBitStringLUTArrayTemplatedConstexpr<bitString, bitStringSize>(); // 2.65s

    /* Simply load bytewise even if we could load more (uneven) bits by rounding down.
     * This makes this implementation much less performant in comparison to the "% 8 = 0" version! */
    constexpr auto nBytesToLoadPerIteration = ( sizeof( uint64_t ) * CHAR_BIT - bitStringSize ) / CHAR_BIT;
    static_assert( nBytesToLoadPerIteration > 0,
                   "Bit string size must be smaller than or equal to 56 bit in order to load bytewise!" );

    /* Initialize buffer window. Note that we can't simply read an uint64_t because of the bit and byte order */
    if ( bufferSize * CHAR_BIT < bitStringSize ) {
        return std::numeric_limits<size_t>::max();
    }
    //std::cerr << "nBytesToLoadPerIteration: " << nBytesToLoadPerIteration << "\n"; // 2
    uint64_t window = 0;
    size_t i = 0;
    for ( ; i < std::min( sizeof( uint64_t ), bufferSize ); ++i ) {
        window = ( window << static_cast<uint8_t>( CHAR_BIT ) ) | buffer[i];
    }

    {
        size_t k = 0;
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                const auto foundBitOffset = i * CHAR_BIT - bitStringSize - k;
                if ( ( foundBitOffset >= firstBitsToIgnore ) && ( foundBitOffset < bufferSize * 8 ) ) {
                    return foundBitOffset - firstBitsToIgnore;
                }
            }
            ++k;
        }
    }

    for ( ; i < bufferSize; ) {
        for ( size_t j = 0; ( j < nBytesToLoadPerIteration ) && ( i < bufferSize ); ++j, ++i ) {
            window = ( window << static_cast<uint8_t>( CHAR_BIT ) ) | buffer[i];
        }

        size_t k = 0;
        for ( const auto& [shifted, mask] : shiftedBitStrings ) {
            if ( ( window & mask ) == shifted ) {
                return i * CHAR_BIT - bitStringSize - k;
            }
            ++k;
        }
    }

    return std::numeric_limits<size_t>::max();
}


enum FindBitStringImplementation : int
{
    TEMPLATE_SIZE,
    TEMPLATE_SIZE_AND_PATTERN,
    NON_TEMPLATED,
};


template<FindBitStringImplementation FIND_BITSTRING_VERSION>
[[nodiscard]] std::vector<size_t>
findBitStrings( const std::vector<char>& buffer )
{
    std::vector<size_t> blockOffsets;

    for ( size_t bitpos = 0; bitpos < buffer.size() * CHAR_BIT; ) {
        const auto byteOffset = bitpos / CHAR_BIT;  // round down because we can't give bit precision

        size_t relpos = 0;
        if constexpr ( FIND_BITSTRING_VERSION == TEMPLATE_SIZE ) {
            relpos = findBitString<bitStringToFindSize>(
                reinterpret_cast<const uint8_t*>( buffer.data() )
                + byteOffset,
                buffer.size() - byteOffset,
                bitStringToFind
            );
        } else if constexpr ( FIND_BITSTRING_VERSION == TEMPLATE_SIZE_AND_PATTERN ) {
            relpos = findBitStringBitStringTemplated<bitStringToFind, bitStringToFindSize>(
                reinterpret_cast<const uint8_t*>( buffer.data() ) + byteOffset,
                buffer.size() - byteOffset
            );
        } else if constexpr ( FIND_BITSTRING_VERSION == NON_TEMPLATED ) {
            relpos = findBitStringNonTemplated(
                reinterpret_cast<const uint8_t*>( buffer.data() )
                + byteOffset,
                buffer.size() - byteOffset,
                bitStringToFind,
                bitStringToFindSize
            );
        }

        if ( relpos == std::numeric_limits<size_t>::max() ) {
            break;
        }
        const auto foundOffset = byteOffset * CHAR_BIT + relpos;
        if ( blockOffsets.empty() || ( blockOffsets.back() != foundOffset ) ) {
            blockOffsets.push_back( foundOffset );
        }
        bitpos = foundOffset + bitStringToFindSize;
    }

    return blockOffsets;
}


/** use BitReader.read instead of the pre-shifted table trick */
[[nodiscard]] std::vector<size_t>
findBitStringsBitReaderRead( const std::vector<char>& data )
{
    std::vector<size_t> blockOffsets;

    bzip2::BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    uint64_t bytes = bitReader.read( bitStringToFindSize - 1 );
    while ( true ) {
        bytes = ( ( bytes << 1U ) | bitReader.read<1>() ) & 0xFFFF'FFFF'FFFFULL;
        if ( bitReader.eof() ) {
            break;
        }

        if ( bytes == bitStringToFind ) {
            blockOffsets.push_back( bitReader.tell() - bitStringToFindSize );
        }
    }

    return blockOffsets;
}


/** always get one more bit but avoid slow BitReader.read calls */
[[nodiscard]] std::vector<size_t>
findBitStringsBitWiseWithoutBitReader( const std::vector<char>& buffer )
{
    std::vector<size_t> blockOffsets;

    uint64_t window = 0;
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        const auto byte = static_cast<uint8_t>( buffer[i] );
        for ( int j = 0; j < CHAR_BIT; ++j ) {
            const auto nthBitMask = static_cast<uint8_t>( CHAR_BIT - 1 - j );
            /* Beware! Shift operator casts uint8_t input to int.
             * @see https://en.cppreference.com/w/cpp/language/operator_arithmetic#Conversions */
            const auto bit = static_cast<uint8_t>( byte >> nthBitMask ) & 1U;
            window <<= 1U;
            window |= bit;
            if ( i * CHAR_BIT + j < bitStringToFindSize ) {
                continue;
            }

            if ( ( window & 0xFFFF'FFFF'FFFFULL ) == bitStringToFind ) {
                /* Dunno why the + 1 is necessary but it works (tm) */
                blockOffsets.push_back( i * CHAR_BIT + j + 1 - bitStringToFindSize );
            }
        }
    }

    return blockOffsets;
}


[[nodiscard]] std::vector<size_t>
findStrings( const std::string_view& data,
             const std::string_view& stringToFind )
{
    std::vector<size_t> blockOffsets;
    for ( auto position = data.find( stringToFind, 0 );
          position != std::string_view::npos;
          position = data.find( stringToFind, position + 1U ) )
    {
        blockOffsets.push_back( position );
    }
    return blockOffsets;
}


[[nodiscard]] std::vector<char>
msbToString( uint64_t bitString,
             uint8_t  bitStringSize )
{
    std::vector<char> result( bitStringSize / 8U );
    for ( auto& c : result ) {
        bitStringSize -= 8U;
        c = static_cast<char>( ( bitString >> bitStringSize ) & 0xFFU );
    }
    return result;
}


[[nodiscard]] std::vector<size_t>
findBitStringsWithStringView( const std::vector<char>& buffer )
{
    const std::string_view stringView( buffer.data(), buffer.size() );

    /* Without shift is too much a special case, so handle it here separately. */
    const auto unshiftedStringToFind = msbToString( bitStringToFind, bitStringToFindSize );
    auto blockOffsets = findStrings( stringView, { unshiftedStringToFind.data(), unshiftedStringToFind.size() } );
    for ( auto& offset : blockOffsets ) {
        offset *= 8U;
    }

    for ( uint32_t shift = 1U; shift < 8U; ++shift ) {
        const auto stringToFind = msbToString( bitStringToFind >> shift, bitStringToFindSize - 8U );
        const auto newBlockOffsets = findStrings( stringView, { stringToFind.data(), stringToFind.size() } );

        /* Try to estimate reserve from first bit-shifted for all subsequent ones. */
        blockOffsets.reserve( blockOffsets.size() + newBlockOffsets.size() * ( shift == 1U ? 7U : 1U ) );

        const auto subStringSize = bitStringToFindSize / 8U - 1U;
        for ( const auto offset : newBlockOffsets ) {
            if ( ( offset == 0 ) || ( offset + subStringSize >= buffer.size() ) ) {
                continue;
            }

            const auto nBitsAfter = shift;
            const auto nBitsBefore = 8U - shift;
            const auto headMatches = ( static_cast<uint8_t>( buffer[offset - 1] )
                                       & nLowestBitsSet<uint64_t>( nBitsBefore ) )
                                     == ( ( bitStringToFind >> ( bitStringToFindSize - nBitsBefore ) )
                                          & nLowestBitsSet<uint64_t>( nBitsBefore ) );
            const auto tailMatches = ( static_cast<uint64_t>( static_cast<uint8_t>( buffer[offset + subStringSize] ) )
                                       >> ( 8U - nBitsAfter ) )
                                     == ( bitStringToFind & nLowestBitsSet<uint64_t>( nBitsAfter ) );

            if ( headMatches && tailMatches ) {
                blockOffsets.push_back( offset * 8U - nBitsBefore );
            }
        }
    }

    std::sort( blockOffsets.begin(), blockOffsets.end() );
    return blockOffsets;
}


template<typename Finder>
[[nodiscard]] std::vector<size_t>
findBitStringsFinder( const std::vector<char>& data )
{
    std::vector<size_t> matches;

    Finder bitStringFinder( std::make_unique<BufferViewFileReader>( data ), bitStringToFind );
    while ( true ) {
        matches.push_back( bitStringFinder.find() );
        if ( matches.back() == std::numeric_limits<size_t>::max() ) {
            matches.pop_back();
            break;
        }
    }
    return matches;
}


template<uint8_t bitCount>
[[nodiscard]] constexpr uint8_t
nextBitStringCandidate( uint32_t bits )
{
    if constexpr ( bitCount == 0 ) {
        return 0;
    } else {
        static_assert( bitCount <= bitStringToFindSize, "LUT sized > 2^48 should be reasonable anyway!" );
        if ( ( bitStringToFind >> static_cast<uint8_t>( bitStringToFindSize - bitCount ) ) == bits ) {
            return 0;
        }
        return 1U + nextBitStringCandidate<bitCount - 1U>( bits & nLowestBitsSet<uint32_t, bitCount - 1U>() );
    }
}


template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] constexpr std::array<uint8_t, 1U << CACHED_BIT_COUNT>
createNextBitStringCandidateLUT()
{
    std::array<uint8_t, 1U << CACHED_BIT_COUNT> result{};
    for ( uint32_t i = 0; i < result.size(); ++i ) {
        result[i] = nextBitStringCandidate<CACHED_BIT_COUNT>( i );
    }
    return result;
}


template<uint8_t bitCount>
[[nodiscard]] constexpr uint8_t
nextBitStringCandidateNonTemplate( uint32_t bits,
                                   uint64_t bitString,
                                   uint8_t  bitStringSize )
{
    if constexpr ( bitCount == 0 ) {
        return 0;
    } else {
        if ( bitCount > bitStringSize ) {
            throw std::invalid_argument( "LUT sized > 2^48 should be reasonable anyway!" );
        }
        if ( ( bitString >> static_cast<uint8_t>( bitStringSize - bitCount ) ) == bits ) {
            return 0;
        }
        return 1U + nextBitStringCandidateNonTemplate<bitCount - 1U>( bits & nLowestBitsSet<uint32_t, bitCount - 1U>(),
                                                                      bitString, bitStringSize );
    }
}


template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] std::array<uint8_t, 1U << CACHED_BIT_COUNT>
createNextBitStringCandidateLUTNonTemplate( uint64_t bitString,
                                            uint8_t  bitStringSize )
{
    std::array<uint8_t, 1U << CACHED_BIT_COUNT> result{};
    for ( uint32_t i = 0; i < result.size(); ++i ) {
        result[i] = nextBitStringCandidateNonTemplate<CACHED_BIT_COUNT>( i, bitString, bitStringSize );
    }
    return result;
}


template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] std::vector<size_t>
findBitStringsLUT( const std::vector<char>& data )
{
    bzip2::BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    std::vector<size_t> bitOffsets;

    /* constexpr vs non constexpr is not visibly different from each other as it should be because it should be
     * negligible work for the setup as opposed to searching. */
    //static constexpr auto nextBitStringCandidateLUT = createNextBitStringCandidateLUT<CACHED_BIT_COUNT>();
    const auto nextBitStringCandidateLUT = createNextBitStringCandidateLUTNonTemplate<CACHED_BIT_COUNT>(
        bitStringToFind, bitStringToFindSize );

    /* 0x3141'5926'5359 : 0x31 == 0b0011'0001, 0x41 == 0b0100'0001 */
    if ( nextBitStringCandidate<0>( 0b0 ) != 0 ) { throw std::logic_error( "" ); }

    if ( nextBitStringCandidate<1>( 0b1 ) != 1 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<1>( 0b0 ) != 0 ) { throw std::logic_error( "" ); }

    if ( nextBitStringCandidate<2>( 0b00 ) != 0 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<2>( 0b01 ) != 2 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<2>( 0b10 ) != 1 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<2>( 0b11 ) != 2 ) { throw std::logic_error( "" ); }

    if ( nextBitStringCandidate<3>( 0b001 ) != 0 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b000 ) != 1 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b011 ) != 3 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b010 ) != 2 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b101 ) != 3 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b100 ) != 1 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b111 ) != 3 ) { throw std::logic_error( "" ); }
    if ( nextBitStringCandidate<3>( 0b110 ) != 2 ) { throw std::logic_error( "" ); }

    try
    {
        while ( true ) {
            const auto peeked = bitReader.peek<CACHED_BIT_COUNT>();
            const auto nextPosition = nextBitStringCandidateLUT[peeked];

            /* If we can skip forward, then that means that the new position only has been partially checked.
             * Therefore, rechecking the LUT for non-zero skips not only ensures that we aren't wasting time in
             * readHeader but it also ensures that we can avoid checking the first three bits again inside readHeader
             * and instead start reading and checking the dynamic Huffman code directly! */
            if ( nextPosition > 0 ) {
                bitReader.seekAfterPeek( nextPosition );
                continue;
            }

            if ( bitReader.peek( bitStringToFindSize ) == bitStringToFind ) {
                bitOffsets.push_back( bitReader.tell() );
            }
            bitReader.seekAfterPeek( 1 );
        }
    } catch ( const bzip2::BitReader::EndOfFileReached& ) {
        /* Break condition for infinite loop inside. */
    }

    return bitOffsets;
}


/**
 * benchmark on ~8GiB file:
 *    head -c $(( 8 * 1024 * 1024 * 1024 )) /dev/urandom | lbzcat --compress > /dev/shm/huge.bz2
 * make blockfinder && time ./blockfinder /dev/shm/huge.bz2
 *    ~4.2s
 * Vary parallelisation and increase chunk size proportionally so that the subdivisions chunks are constant:
 *  p | real time
 * ---+-----------
 *  1 |   17.1 s
 *  2 |   10.5 s
 *  4 |    7.9 s
 *  8 |    5.6 s
 * 16 |    4.9 s
 * 24 |    4.2 s
 * 32 |    4.6 s
 * 48 |    4.2 s
 *  -> Problem with the current implementation is very likely stragglers! -> trace it.
 *     Because I'm not double buffering and therefore have to wait for all to finish before starting the next batch!
 *     Ideally, I'd start a new parallel thread as soon as I know it ended.
 *     Also note that the results of 4.2s mean ~2GB/s bandwidth!
 *          sudo apt install sysbench
 *          sysbench memory --memory-block-size=$(( 256*1024*1024 )) run
 *              Total operations: 400 (   41.71 per second)
 *
 *              102400.00 MiB transferred (10677.87 MiB/sec)
 *
 *
 *              General statistics:
 *                  total time:                          9.5886s
 *                  total number of events:              400
 *
 *              Latency (ms):
 *                      min:                                   22.97
 *                      avg:                                   23.97
 *                      max:                                   32.39
 *                      95th percentile:                       25.74
 *                      sum:                                 9586.51
 *           => ~10.4 GiB/s, so roughly factor 5 faster than I can search in RAM.
 *      Double buffering would also allow to fill the buffer in the background in parallel!
 *      This might help a lot, assuming the buffer filling is the serial bottleneck.
 */
[[nodiscard]] std::vector<size_t>
findBitStringsParallel( const std::vector<char>& data )
{
    std::vector<size_t> matches;

    const auto parallelization = std::thread::hardware_concurrency();
    ParallelBitStringFinder<bitStringToFindSize> bitStringFinder(
        std::make_unique<BufferViewFileReader>( data ),
        bitStringToFind, parallelization, 0, parallelization * 1_Mi
    );
    while ( true ) {
        matches.push_back( bitStringFinder.find() );
        if ( matches.back() == std::numeric_limits<size_t>::max() ) {
            matches.pop_back();
            break;
        }
        if ( ( matches.size() > 1 ) && ( matches[matches.size() - 2] >= matches.back() ) ) {
            throw std::logic_error( "Returned offsets should be unique and monotonically increasing!" );
        }
    }
    return matches;
}


void
benchmarkFindBitString( const std::vector<char>& data )
{
    static constexpr int LABEL_WIDTH = 31;

    const auto formatBandwidth =
        [&] ( const std::vector<double>& times )
        {
            std::vector<double> bandwidths( times.size() );
            std::transform( times.begin(), times.end(), bandwidths.begin(),
                            [size = data.size()] ( double time ) { return static_cast<double>( size ) / time / 1e6; } );
            Statistics<double> bandwidthStats{ bandwidths };

            /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
             * physically possible but many noisy influences can slow things down, i.e., the minimum time is
             * the value closest to be free of noise. */
            std::stringstream result;
            result << "( " + bandwidthStats.formatAverageWithUncertainty()
                   << ", max: " << bandwidthStats.max << " ) MB/s";
            return result.str();
        };

    /* block offsets are used as "checksum", i.e., some "small" result that can be compared. */
    std::optional<std::vector<size_t> > checksum;

    const auto checkBlockOffsets =
        [] ( const std::vector<size_t>& blockOffsets,
             const std::vector<char>&   buffer )
        {
            bzip2::BitReader bitReader( std::make_unique<BufferViewFileReader>( buffer ) );
            for ( const auto offset : blockOffsets ) {
                if ( offset < bitReader.size().value() ) {
                    bitReader.seek( static_cast<long long int>( offset ) );

                    /* Because bitReader is limited to 32-bit. */
                    static_assert( bitStringToFindSize % 2 == 0,
                                   "Assuming magic bit string size to be an even number." );
                    constexpr uint8_t BITS_PER_READ = bitStringToFindSize / 2;
                    const auto magicBytes =
                        ( static_cast<uint64_t>( bitReader.read( BITS_PER_READ ) ) << BITS_PER_READ )
                        | bitReader.read( BITS_PER_READ );

                    if ( magicBytes != bitStringToFind ) {
                        std::stringstream msg;
                        msg << "Magic bytes at offset " << offset / 8 << " B " << offset % 8 << " b"
                            << "(0x" << std::hex << magicBytes << std::dec << ") do not match!";
                        throw std::logic_error( std::move( msg ).str() );
                    }
                }
            }
        };

    const auto measureTimes =
        [&] ( const std::string& benchmarkType,
              const auto&        toMeasure )
        {
            std::optional<std::vector<size_t> > batchChecksum;
            std::vector<double> times( 4 );
            for ( auto& time : times ) {
                const auto t0 = now();
                const auto calculatedChecksum = toMeasure();
                time = duration( t0 );

                if ( !batchChecksum ) {
                    checkBlockOffsets( calculatedChecksum, data );
                    batchChecksum = calculatedChecksum;
                } else if ( *batchChecksum != calculatedChecksum ) {
                    std::stringstream message;
                    message << "Indeterministic result for " << benchmarkType << "!";
                    throw std::runtime_error( std::move( message ).str() );
                }
                if ( checksum && batchChecksum && ( *checksum != *batchChecksum ) ) {
                    std::cerr << "Found " << batchChecksum->size() << " blocks for \"" << benchmarkType << "\"\n";
                    std::stringstream message;
                    message << "Wrong result for " << benchmarkType << "!";
                    throw std::runtime_error( std::move( message ).str() );
                }
                if ( !checksum ) {
                    checksum = batchChecksum;
                }
            }

            checksum = batchChecksum;

            /* Remove two (arbitrary) outliers. */
            if ( times.size() >= 5 ) {
                times.erase( std::min_element( times.begin(), times.end() ) );
                times.erase( std::max_element( times.begin(), times.end() ) );
            }

            std::cout << "[" << std::setw( LABEL_WIDTH ) << benchmarkType << "] ";
            std::cout << "Processed with " << formatBandwidth( times ) << std::endl;
        };

    // *INDENT-OFF*
    measureTimes( "ParallelBitStringFinder"        , [&] () { return findBitStringsParallel( data ); } );
    measureTimes( "Using std::string_view"         , [&] () { return findBitStringsWithStringView( data ); } );
    measureTimes( "BitStringFinder",
                  [&] () { return findBitStringsFinder<BitStringFinder<bitStringToFindSize> >( data ); } );
    measureTimes( "Boyer-Moore like LUT (8 bits)"  , [&] () { return findBitStringsLUT<8>( data ); } );
    measureTimes( "Boyer-Moore like LUT (12 bits)" , [&] () { return findBitStringsLUT<12>( data ); } );
    measureTimes( "Boyer-Moore like LUT (13 bits)" , [&] () { return findBitStringsLUT<13>( data ); } );
    measureTimes( "Boyer-Moore like LUT (14 bits)" , [&] () { return findBitStringsLUT<14>( data ); } );
    measureTimes( "Boyer-Moore like LUT (15 bits)" , [&] () { return findBitStringsLUT<15>( data ); } );
    measureTimes( "Boyer-Moore like LUT (16 bits)" , [&] () { return findBitStringsLUT<16>( data ); } );
    measureTimes( "Boyer-Moore like LUT (17 bits)" , [&] () { return findBitStringsLUT<17>( data ); } );
    measureTimes( "Boyer-Moore like LUT (18 bits)" , [&] () { return findBitStringsLUT<18>( data ); } );
    measureTimes( "findBitString<pattern, size>()" ,
                  [&] () { return findBitStrings<TEMPLATE_SIZE_AND_PATTERN>( data ); } );
    measureTimes( "findBitString<size>( pattern )" , [&] () { return findBitStrings<TEMPLATE_SIZE>( data ); } );
    measureTimes( "findBitStrings( pattern, size )", [&] () { return findBitStrings<NON_TEMPLATED>( data ); } );
    measureTimes( "Avoid BitReader::read<1>()"     , [&] () { return findBitStringsBitWiseWithoutBitReader( data ); } );
    measureTimes( "BitReader::read<1>()"           , [&] () { return findBitStringsBitReaderRead( data ); } );
    // *INDENT-ON*
}


int
main( int    argc,
      char** argv )
{
    std::vector<char> data;
    if ( argc == 2 ) {
        std::filesystem::path filename{ argv[1] };
        if ( !std::filesystem::exists( filename ) ) {
            std::cerr << filename << " is not a file!\n";
            return 1;
        }
        data.resize( std::filesystem::file_size( filename ) );
        data.resize( StandardFileReader( filename ).read( data.data(), data.size() ) );
    } else {
        std::cerr << "Using a random buffer for testing. Because this will rarely result in positives, "
                  << "the correctness of the bit string find algorithms should already have been verified!\n";
        data.resize( 256_Mi );
        for ( size_t i = 0; i + 3 < data.size(); i += 4 ) {
            const auto randomNumber = static_cast<uint32_t>( rand() );
            data[i + 0] = static_cast<char>( ( randomNumber >>  0U ) & 0xFFU );
            data[i + 1] = static_cast<char>( ( randomNumber >>  8U ) & 0xFFU );
            data[i + 2] = static_cast<char>( ( randomNumber >> 16U ) & 0xFFU );
            data[i + 3] = static_cast<char>( ( randomNumber >> 24U ) & 0xFFU );
        }
    }

    benchmarkFindBitString( data );

    return 0;
}


/*
Results for 256 MiB of random data on Ryzen 3700X (12-core) with parallelization = 24:
[        ParallelBitStringFinder] Processed with ( 6000    +- 300 , max: 6399.00 ) MB/s
[         Using std::string_view] Processed with ( 1491    +- 8   , max: 1498.69 ) MB/s
[                BitStringFinder] Processed with ( 1780    +- 60  , max: 1817.67 ) MB/s
[  Boyer-Moore like LUT (8 bits)] Processed with (  206.7  +- 0.4 , max: 207.256 ) MB/s
[ Boyer-Moore like LUT (12 bits)] Processed with (  302.64 +- 0.18, max: 302.862 ) MB/s
[ Boyer-Moore like LUT (13 bits)] Processed with (  317    +- 1   , max: 317.977 ) MB/s
[ Boyer-Moore like LUT (14 bits)] Processed with (  330.1  +- 1.6 , max: 331.569 ) MB/s
[ Boyer-Moore like LUT (15 bits)] Processed with (  360    +- 0.4 , max: 360.515 ) MB/s
[ Boyer-Moore like LUT (16 bits)] Processed with (  332.9  +- 1.3 , max: 333.946 ) MB/s
[ Boyer-Moore like LUT (17 bits)] Processed with (  317    +- 3   , max: 320.255 ) MB/s
[ Boyer-Moore like LUT (18 bits)] Processed with (  321    +- 4   , max: 325.359 ) MB/s
[ findBitString<size>( pattern )] Processed with (  275    +- 6   , max: 280.458 ) MB/s
[ findBitString<pattern, size>()] Processed with (  398    +- 5   , max: 401.782 ) MB/s
[findBitStrings( pattern, size )] Processed with (  260.2  +- 2   , max: 263.136 ) MB/s
[     Avoid BitReader::read<1>()] Processed with (  132    +- 27  , max: 161.978 ) MB/s
[           BitReader::read<1>()] Processed with (   26    +- 0.17, max: 26.2077 ) MB/s
*/
