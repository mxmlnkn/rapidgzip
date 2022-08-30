#include <array>
#include <bitset>
#include <climits>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <type_traits>
#include <unordered_map>

#include <blockfinder/DynamicHuffman.hpp>
#include <TestHelpers.hpp>
#include <Error.hpp>


using CompressedHistogram = pragzip::blockfinder::CompressedHistogram;


template<uint8_t FREQUENCY_BITS,
         uint8_t FREQUENCY_COUNT>
[[nodiscard]] pragzip::Error
checkPrecodeFrequenciesAlternative( CompressedHistogram frequencies )
{
    static_assert( FREQUENCY_COUNT <= 7, "Precode code lengths go only up to 7!" );
    static_assert( FREQUENCY_COUNT * FREQUENCY_BITS <= std::numeric_limits<CompressedHistogram>::digits,
                   "Argument type does not fit as many values as to be processed!" );

    /* If all counts are zero, then either it is a valid frequency distribution for an empty code or we are
     * missing some higher counts and cannot determine whether the code is bloating because all missing counts
     * might be zero, which is a valid special case. It is also not invalid because counts are the smallest they
     * can be.
     * Similarly, the special case of only a single symbol encoded in 1-bit is also valid because there is no
     * valid (non-bloating) way to encode it! */
    constexpr auto bitsToProcessMask = nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS * FREQUENCY_COUNT>();
    if ( UNLIKELY( ( ( frequencies & bitsToProcessMask ) == 0 )
                     || ( frequencies & bitsToProcessMask ) == 1 ) ) [[unlikely]] {
        return pragzip::Error::NONE;
    }

    /* Because we do not know actual total count, we have to assume the most relaxed check for the bloating check. */
    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    auto remainingCount = MAX_CL_SYMBOL_COUNT;

    uint8_t unusedSymbolCount{ 2 };
    for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT; ++bitLength ) {
        const auto frequency = ( frequencies >> ( ( bitLength - 1U ) * FREQUENCY_BITS ) )
                               & nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS>();
        if ( frequency > unusedSymbolCount ) {
            return pragzip::Error::INVALID_CODE_LENGTHS;
        }

        unusedSymbolCount -= frequency;
        unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */

        remainingCount -= frequency;

        if ( unusedSymbolCount > remainingCount ) {
            return pragzip::Error::BLOATING_HUFFMAN_CODING;
        }
    }

    return pragzip::Error::NONE;
}


/**
 * This older, alternative precode frequency check LUT creation is thousands of times slower and requires much
 * more heap space during compilation than the newer one when made constexpr! Therefore, use the newer better
 * constexpr version and keep this test to check at test runtime whether the newer and alternative LUT creation
 * functions yield identical results.
 */
template<uint32_t FREQUENCY_BITS,
         uint32_t FREQUENCY_COUNT>
[[nodiscard]] auto
createPrecodeFrequenciesValidLUTAlternative()
{
    static_assert( ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) % 64U == 0,
                   "LUT size must be a multiple of 64-bit for the implemented bit-packing!" );
    std::array<uint64_t, ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) / 64U> result{};
    for ( size_t i = 0; i < result.size(); ++i ) {
        for ( size_t j = 0; j < 64U; ++j ) {
            const auto isValid = checkPrecodeFrequenciesAlternative<FREQUENCY_BITS, FREQUENCY_COUNT>( i * 64U + j )
                                 == pragzip::Error::NONE;
            result[i] |= static_cast<uint64_t>( isValid ) << j;
        }
    }
    return result;
}


template<uint32_t FREQUENCY_COUNT>
void
analyzeValidPrecodeFrequencies()
{
    /* Without static, I'm getting SIGSEV! It might be that this results in a classical stack overflow because
     * those std::array LUTs are allocated on the insufficiently-sized stack when not static. */
    static constexpr auto frequencyLUT = pragzip::blockfinder::createPrecodeFrequenciesValidLUT<5, FREQUENCY_COUNT>();
    static const auto frequencyLUTAlternative = createPrecodeFrequenciesValidLUTAlternative<5, FREQUENCY_COUNT>();
    REQUIRE( frequencyLUT.size() == frequencyLUTAlternative.size() );
    REQUIRE( frequencyLUT == frequencyLUTAlternative );

    const auto sizeInBytes = frequencyLUT.size() * sizeof( frequencyLUT[0] );
    std::cerr << "Precode frequency LUT containing " << static_cast<int>( FREQUENCY_COUNT ) << " bins is sized: ";
    if ( sizeInBytes > 32_Ki ) {
        std::cerr << sizeInBytes / 1024 << " KiB";
    } else {
        std::cerr << sizeInBytes << " B";
    }
    std::cerr << ". ";

    uint64_t validCount{ 0 };
    for ( const auto& bits : frequencyLUT ) {
        validCount += std::bitset<std::numeric_limits<std::decay_t<decltype( bits )> >::digits>( bits ).count();
    }
    std::cerr << "There are " << validCount << " valid entries out of " << sizeInBytes * CHAR_BIT << " -> "
              << static_cast<double>( validCount ) / static_cast<double>( sizeInBytes * CHAR_BIT ) * 100 << " %\n";
}


void
analyzeValidPrecodes()
{
    std::mt19937_64 randomEngine;

    static constexpr uint64_t MONTE_CARLO_TEST_COUNT = 100'000'000;
    uint64_t validPrecodeCount{ 0 };
    std::unordered_map<pragzip::Error, uint64_t> errorCounts;
    for ( uint64_t i = 0; i < MONTE_CARLO_TEST_COUNT; ++i ) {
        using namespace pragzip::deflate;
        const auto precodeBits = randomEngine();
        const auto error = pragzip::blockfinder::checkPrecode(
            precodeBits & nLowestBitsSet<uint64_t, 4>(),
            ( precodeBits >> 4U ) & nLowestBitsSet<uint64_t>( MAX_PRECODE_COUNT * PRECODE_BITS ) );
        const auto [count, wasInserted] = errorCounts.try_emplace( error, 1 );
        if ( !wasInserted ) {
            count->second++;
        }

        validPrecodeCount += ( error == pragzip::Error::NONE ) ? 1 : 0;
    }

    {
        std::cerr << "Valid precodes " << validPrecodeCount << " out of " << MONTE_CARLO_TEST_COUNT << " tested -> "
                  << static_cast<double>( validPrecodeCount ) / static_cast<double>( MONTE_CARLO_TEST_COUNT ) * 100
                  << " %\n";

        std::multimap<uint64_t, pragzip::Error, std::greater<> > sortedErrorTypes;
        for ( const auto [error, count] : errorCounts ) {
            sortedErrorTypes.emplace( count, error );
        }
        std::cerr << "Encountered errors:\n";
        for ( const auto& [count, error] : sortedErrorTypes ) {
            std::cerr << "    " << std::setw( 8 ) << count << " " << toString( error ) << "\n";
        }
        std::cerr << "\n";
    }

    /* Test frequency LUT */

}


int
main()
{
    analyzeValidPrecodes();

    analyzeValidPrecodeFrequencies<2>();
    analyzeValidPrecodeFrequencies<3>();
    analyzeValidPrecodeFrequencies<4>();
    analyzeValidPrecodeFrequencies<5>();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}


/*
cmake --build . -- benchmarkGzipBlockFinder && taskset 0x08 src/benchmarks/benchmarkGzipBlockFinder random.gz

Valid precodes 402535 out of 100000000 tested -> 0.402535 %
Encountered errors:
    89813152 Constructing a Huffman coding from the given code length sequence failed!
     9784313 The Huffman coding is not optimal!
      402535 No error.

Precode frequency LUT containing 2 bins is sized: 128 B. There are 9 valid entries out of 1024 -> 0.878906 %
Precode frequency LUT containing 3 bins is sized: 4096 B. There are 35 valid entries out of 32768 -> 0.106812 %
Precode frequency LUT containing 4 bins is sized: 128 KiB. There are 158 valid entries out of 1048576 -> 0.0150681 %
Precode frequency LUT containing 5 bins is sized: 4096 KiB. There are 562 valid entries out of 33554432 -> 0.00167489 %
Tests successful: 8 / 8
*/
