#include "bzip2.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <BitStringFinder.hpp>
#include <ParallelBitStringFinder.hpp>
#include <common.hpp>

//#define BENCHMARK

namespace
{
constexpr uint64_t bitStringToFind = 0x314159265359; /* bcd(pi) */
//constexpr uint64_t bitStringToFind = 0x177245385090ULL; /* bcd(sqrt(pi)) */
constexpr uint8_t bitStringToFindSize = 48;
}


#if 0
/**
 * @param bitString the lowest bitStringSize bits will be looked for in the buffer
 * @return size_t max if not found else position in buffer
 */
size_t
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
        bytes = ( bytes << 8 ) | static_cast<uint8_t>( buffer[i] );
    }

    assert( bitStringSize == 48 ); /* this allows us to fixedly load always two bytes (16 bits) */
    for ( size_t i = 0; i < bufferSize; ++i ) {
        bytes = ( bytes << 8 ) | static_cast<uint8_t>( buffer[i] );
        if ( ++i >= bufferSize ) {
            break;
        }
        bytes = ( bytes << 8 ) | static_cast<uint8_t>( buffer[i] );

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
constexpr auto
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


auto
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
size_t
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


size_t
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
constexpr auto
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
constexpr auto
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
size_t
findBitStringBitStringTemplated( const uint8_t* buffer,
                                 size_t         bufferSize,
                                 uint8_t        firstBitsToIgnore = 0 )
{
    const auto shiftedBitStrings = createdShiftedBitStringLUTArrayTemplated<bitString, bitStringSize>(); // 1.85s
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


/** I think this version isn't even correct because magic bytes across buffer boundaries will be overlooked! */
std::vector<size_t>
findBitStrings( const std::string& filename )
{
    std::vector<size_t> blockOffsets;

    auto ufile = make_unique_file_ptr( filename.c_str(), "rb" );
    auto* const file = ufile.get();
    const auto movingBytesToKeep = ceilDiv( bitStringToFindSize, CHAR_BIT ); // 6
    std::vector<char> buffer( 2 * 1024 * 1024 + movingBytesToKeep ); // for performance testing
    //std::vector<char> buffer( 53 ); // for bug testing with bit strings accross buffer boundaries
    size_t nTotalBytesRead = 0;
    while ( true ) {
        size_t nBytesRead = 0;
        if ( nTotalBytesRead == 0 ) {
            nBytesRead = fread( buffer.data(), 1, buffer.size(), file );
            buffer.resize( nBytesRead );
        } else {
            std::memmove( buffer.data(), buffer.data() + buffer.size() - movingBytesToKeep, movingBytesToKeep );
            nBytesRead = fread( buffer.data() + movingBytesToKeep, 1, buffer.size() - movingBytesToKeep, file );
            buffer.resize( movingBytesToKeep + nBytesRead );
        }
        if ( nBytesRead == 0 ) {
            break;
        }

        for ( size_t bitpos = 0; bitpos < nBytesRead * CHAR_BIT; ) {
            const auto byteOffset = bitpos / CHAR_BIT; // round down because we can't give bit precision

            size_t relpos = 0;
            static constexpr int FIND_BITSTRING_VERSION = 3;
            if constexpr ( FIND_BITSTRING_VERSION == 0 ) {
                /* 1.85s */
                relpos = findBitString<bitStringToFindSize>(
                    reinterpret_cast<const uint8_t*>( buffer.data() )
                    + byteOffset,
                    buffer.size() - byteOffset,
                    bitStringToFind
                );
            } else if constexpr ( FIND_BITSTRING_VERSION == 1 ) {
                /* 2.05s */
                relpos = findBitStringBitStringTemplated<bitStringToFind, bitStringToFindSize>(
                    reinterpret_cast<const uint8_t*>( buffer.data() ) + byteOffset,
                    buffer.size() - byteOffset
                );
            } else if constexpr ( FIND_BITSTRING_VERSION == 2 ) {
                /* 3.45s */
                relpos = findBitStringNonTemplated(
                    reinterpret_cast<const uint8_t*>( buffer.data() )
                    + byteOffset,
                    buffer.size() - byteOffset,
                    bitStringToFind,
                    bitStringToFindSize
                );
            } else if constexpr ( FIND_BITSTRING_VERSION == 3 ) {
                /* Should normally be one of the above implementations! */
                relpos = BitStringFinder<bitStringToFindSize>::findBitString(
                    reinterpret_cast<const uint8_t*>( buffer.data() ) + byteOffset,
                    buffer.size() - byteOffset,
                    bitStringToFind
                );
            }

            if ( relpos == std::numeric_limits<size_t>::max() ) {
                break;
            }
            bitpos = byteOffset * CHAR_BIT + relpos;
            const auto foundOffset = ( nTotalBytesRead > movingBytesToKeep
                                       ? nTotalBytesRead - movingBytesToKeep
                                       : nTotalBytesRead ) * CHAR_BIT + bitpos;
            if ( blockOffsets.empty() || ( blockOffsets.back() != foundOffset ) ) {
                blockOffsets.push_back( foundOffset );
            }
            bitpos += bitStringToFindSize;
        }
        nTotalBytesRead += nBytesRead;
    }

    return blockOffsets;
}

/** use BitReader.read instead of the pre-shifted table trick */
std::vector<size_t>
findBitStrings2( const std::string& filename )
{
    std::vector<size_t> blockOffsets;

    BitReader bitReader( filename );

    uint64_t bytes = bitReader.read( bitStringToFindSize - 1 );
    while ( true ) {
        bytes = ( ( bytes << 1U ) | bitReader.read( 1 ) ) & 0xFFFF'FFFF'FFFFULL;
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
std::vector<size_t>
findBitStrings3( const std::string& filename )
{
    std::vector<size_t> blockOffsets;

    auto ufile = make_unique_file_ptr( filename.c_str(), "rb" );
    auto* const file = ufile.get();
    std::vector<char> buffer( 2 * 1024 * 1024 );
    size_t nTotalBytesRead = 0;
    uint64_t window = 0;
    while ( true ) {
        const auto nBytesRead = fread( buffer.data(), 1, buffer.size(), file );
        if ( nBytesRead == 0 ) {
            break;
        }

        for ( size_t i = 0; i < nBytesRead; ++i ) {
            const auto byte = static_cast<uint8_t>( buffer[i] );
            for ( int j = 0; j < CHAR_BIT; ++j ) {
                const auto nthBitMask = static_cast<uint8_t>( CHAR_BIT - 1 - j );
                /* Beware! Shift operator casts uint8_t input to int.
                 * @see https://en.cppreference.com/w/cpp/language/operator_arithmetic#Conversions */
                const auto bit = static_cast<uint8_t>( byte >> nthBitMask ) & 1U;
                window <<= 1U;
                window |= bit;
                if ( ( nTotalBytesRead + i ) * CHAR_BIT + j < bitStringToFindSize ) {
                    continue;
                }

                if ( ( window & 0xFFFF'FFFF'FFFFULL ) == bitStringToFind ) {
                    /* Dunno why the + 1 is necessary but it works (tm) */
                    blockOffsets.push_back( ( nTotalBytesRead + i ) * CHAR_BIT + j + 1 - bitStringToFindSize );
                }
            }
        }

        nTotalBytesRead += nBytesRead;
    }

    return blockOffsets;
}


std::vector<size_t>
findBitStrings4( const std::string& filename )
{
    std::vector<size_t> matches;

    BitStringFinder<bitStringToFindSize> bitStringFinder( filename, bitStringToFind );
    while( true )  {
        matches.push_back( bitStringFinder.find() );
        if ( matches.back() == std::numeric_limits<size_t>::max() ) {
            matches.pop_back();
            break;
        }
    }
    return matches;
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
std::vector<size_t>
findBitStrings5( const std::string& filename )
{
    std::vector<size_t> matches;

    const auto parallelisation = 48; //std::thread::hardware_concurrency();
    ParallelBitStringFinder<bitStringToFindSize> bitStringFinder(
        filename, bitStringToFind, parallelisation, 0, parallelisation * 1*1024*1024
    );
    while( true )  {
        matches.push_back( bitStringFinder.find() );
        if ( matches.back() == std::numeric_limits<size_t>::max() ) {
            matches.pop_back();
            break;
        }
        if ( ( matches.size() > 1 ) && ( matches[matches.size()-2] >= matches.back() ) ) {
            throw std::logic_error( "Returned offsets should be unique and monotonically increasing!" );
        }
    }
    return matches;
}


int main( int argc, char** argv )
{
    if ( argc < 2 ) {
        std::cerr << "A bzip2 file name to decompress must be specified!\n";
        return 1;
    }
    const std::string filename( argv[1] );

    /* comments contain tests on firefox-66.0.5.tar.bz2 */
    //const auto blockOffsets = findBitStrings( filename ); // ~520ms // ~1.7s on /dev/shm with 911MiB large.bz2
    //const auto blockOffsets = findBitStrings2( filename ); // ~9.5s // ~100s on /dev/shm with 911MiB large.bz2
    //const auto blockOffsets = findBitStrings3( filename ); // ~520ms // 6.4s on /dev/shm with 911MiB large.bz2
    //const auto blockOffsets = findBitStrings4( filename ); // ~1.8s on /dev/shm with 911MiB large.bz2
    const auto blockOffsets = findBitStrings5( filename ); // ~0.5s on /dev/shm with 911MiB large.bz2 and 24 threads
    /* lookup table and manual minimal bit reader were virtually equally fast
     * probably because the encrypted SSD was the limiting factor -> repeat with /dev/shm
     * => searching is roughly 4x slower, so multithreading on 4 threads should make it equally fast,
     *    which then makes double-buffering a viable option for a total speedup of hopefully 8x!
     */

    BitReader bitReader( filename );
    std::cerr << "Block offsets  :\n";
    for ( const auto offset : blockOffsets ) {
        std::cerr << offset / 8 << " B " << offset % 8 << " b";
        if ( offset < bitReader.size() ) {
            bitReader.seek( offset );
            const auto magicBytes = bitReader.read64( bitStringToFindSize );
            std::cerr << " -> magic bytes: 0x" << std::hex << magicBytes << std::dec;
            if ( magicBytes != bitStringToFind ) {
                throw std::logic_error( "Magic Bytes do not match!" );
            }
        }
        std::cerr << "\n";
    }
    std::cerr << "Found " << blockOffsets.size() << " blocks\n";

    return 0;
}
