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
#include <unordered_set>

#include <blockfinder/DynamicHuffman.hpp>
#include <TestHelpers.hpp>
#include <Error.hpp>


using CompressedHistogram = pragzip::blockfinder::CompressedHistogram;


/**
 * @note This check was pulled from HuffmanCodingBase::checkCodeLengthFrequencies
 * @param frequencies Stores FREQUENCY_COUNT FREQUENCY_BITS-bit-sized values starting with the counts for length 1.
 *                           The zero-counts are omitted in the histogram!
 */
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
    if ( UNLIKELY( ( frequencies & bitsToProcessMask ) == 1 ) ) [[unlikely]] {
        return pragzip::Error::NONE;
    }

    const auto getCount =
        [] ( const CompressedHistogram histogram,
             const uint8_t             bitLength )
        {
            return ( histogram >> ( ( bitLength - 1U ) * FREQUENCY_BITS ) )
                   & nLowestBitsSet<CompressedHistogram, FREQUENCY_BITS>();
        };

    /* Because we do not know actual total count, we have to assume the most relaxed check for the bloating check. */
    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    auto remainingCount = MAX_CL_SYMBOL_COUNT;

    uint8_t unusedSymbolCount{ 2 };
    for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT; ++bitLength ) {
        const auto frequency = getCount( frequencies, bitLength );
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

    /* In the deepest possible layer, we can do a more rigorous check against non-optimal huffman codes. */
    if constexpr ( FREQUENCY_COUNT == 7 ) {
        uint64_t nonZeroCount{ 0 };
        for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT; ++bitLength ) {
            const auto frequency = getCount( frequencies, bitLength );
            nonZeroCount += frequency;
        }

        if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount >  1 ) ) ||
             ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
            return pragzip::Error::BLOATING_HUFFMAN_CODING;
        }

        if ( nonZeroCount == 0 ) {
            return pragzip::Error::EMPTY_ALPHABET;
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
    static const auto frequencyLUT = pragzip::blockfinder::createPrecodeFrequenciesValidLUT<5, FREQUENCY_COUNT>();
    static const auto frequencyLUTAlternative = createPrecodeFrequenciesValidLUTAlternative<5, FREQUENCY_COUNT>();
    REQUIRE_EQUAL( frequencyLUT.size(), frequencyLUTAlternative.size() );
    REQUIRE( frequencyLUT == frequencyLUTAlternative );

    const auto sizeInBytes = frequencyLUT.size() * sizeof( frequencyLUT[0] );
    std::cerr << "Precode frequency LUT containing " << static_cast<int>( FREQUENCY_COUNT ) << " bins is sized: "
              << formatBytes( sizeInBytes ) << ". ";

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


/**
 * @param depth A depth of 1 means that we should iterate over 1-bit codes, which can only be 0,1,2.
 * @param freeBits This can be calculated from the histogram but it saves constexpr instructions when
 *        the caller updates this value outside.
 * @note This is an adaptation of @ref createPrecodeFrequenciesValidLUTHelper.
 */
template<uint32_t FREQUENCY_BITS,
         uint32_t FREQUENCY_COUNT,
         uint32_t DEPTH = 1,
         typename LUT = std::array<uint64_t, ( 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT ) ) / 64U> >
void
analyzeMaxValidPrecodeFrequenciesHelper( std::function<void( uint64_t )> processValidHistogram,
                                         uint32_t const                  remainingCount,
                                         uint64_t const                  histogram = 0,
                                         uint32_t const                  freeBits = 2 )
{
    static_assert( DEPTH <= FREQUENCY_COUNT, "Cannot descend deeper than the frequency counts!" );
    if ( ( histogram & nLowestBitsSet<uint64_t, ( DEPTH - 1 ) * FREQUENCY_BITS>() ) != histogram ) {
        throw std::invalid_argument( "Only frequency of bit-lengths less than the depth may be set!" );
    }

    const auto histogramWithCount =
        [histogram] ( auto count ) constexpr {
            return histogram | ( static_cast<uint64_t>( count ) << ( ( DEPTH - 1 ) * FREQUENCY_BITS ) );
        };

    /* The for loop maximum is given by the invalid Huffman code check, i.e.,
     * when there are more code lengths on a tree level than there are nodes. */
    for ( uint32_t count = 0; count <= std::min( remainingCount, freeBits ); ++count ) {
        const auto newFreeBits = ( freeBits - count ) * 2;
        const auto newRemainingCount = remainingCount - count;

        /* The first layer may not be fully filled or even empty. This does not fit any of the general tests. */
        if constexpr ( DEPTH == 1 ) {
            if ( count == 1 ) {
                processValidHistogram( histogramWithCount( count ) );
            }
        }

        if constexpr ( DEPTH == FREQUENCY_COUNT ) {
            if constexpr ( DEPTH == 7 ) {
                if ( newFreeBits == 0 ) {
                    processValidHistogram( histogramWithCount( count ) );
                }
            } else {
                /* This filters out bloating Huffman codes, i.e., when the number of free nodes in the tree
                 * is larger than the maximum possible remaining (precode) symbols to fit into the tree. */
                if ( newFreeBits <= newRemainingCount ) {
                    processValidHistogram( histogramWithCount( count ) );
                }
            }
        } else {
            if ( count == freeBits ) {
                processValidHistogram( histogramWithCount( count ) );
            } else {
                analyzeMaxValidPrecodeFrequenciesHelper<FREQUENCY_BITS, FREQUENCY_COUNT, DEPTH + 1>(
                    processValidHistogram, newRemainingCount, histogramWithCount( count ), newFreeBits );
            }
        }
    }
}


template<uint8_t FREQUENCY_BITS,
         uint8_t FREQUENCY_COUNT>
[[nodiscard]] std::string
printCompressedHistogram( const CompressedHistogram histogram )
{
    std::stringstream result;
    for ( size_t length = 1; length <= FREQUENCY_COUNT; ++length ) {
        const auto count = static_cast<uint32_t>(
            ( histogram >> ( ( length - 1 ) * FREQUENCY_BITS ) )
            & nLowestBitsSet<uint64_t, FREQUENCY_BITS>() );
        if ( result.tellp() > 0 ) {
            result << " ";
        }
        result << length << ":" << count;
    }
    result << " (0x" << std::hex << std::setfill( '0' ) << std::setw( 64 / /* nibble */ 4 ) << histogram << ")";
    return std::move( result ).str();
}


template<bool COMPARE_WITH_ALTERNATIVE_METHOD>
void
analyzeMaxValidPrecodeFrequencies()
{
    constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
    static constexpr uint32_t FREQUENCY_BITS = 5;  /* minimum bits to represent up to count 19. */
    static constexpr uint32_t FREQUENCY_COUNT = 7;  /* maximum value with 3-bits */

    std::array<uint32_t, FREQUENCY_COUNT> maxFrequencies{};
    std::unordered_set<uint64_t> validHistograms;

    const auto processValidHistogram =
        [&maxFrequencies, &validHistograms] ( const uint64_t validHistogram ) constexpr
        {
            validHistograms.insert( validHistogram );

            for ( size_t codeLength = 0; codeLength < maxFrequencies.size(); ++codeLength ) {
                const auto count = static_cast<uint32_t>( ( validHistogram >> ( codeLength * FREQUENCY_BITS ) )
                                                          & nLowestBitsSet<uint64_t, FREQUENCY_BITS>() );
                maxFrequencies[codeLength] = std::max( maxFrequencies[codeLength], count );

                if ( count >= 16 ) {
                    std::cerr << "Valid Histogram with >=16 codes of the same length: "
                              << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( validHistogram ) << "\n";
                }
            }
        };

    analyzeMaxValidPrecodeFrequenciesHelper<FREQUENCY_BITS, FREQUENCY_COUNT>(
        processValidHistogram, MAX_CL_SYMBOL_COUNT );

    std::cerr << "\nMaximum length frequencies of valid histograms:\n";
    for ( size_t length = 1; length <= FREQUENCY_COUNT; ++length ) {
        std::cerr << "    Code Length " << length << " : " << maxFrequencies[length - 1] << "\n";
    }
    std::cerr << "\n";

    std::cerr << "Found in total " << validHistograms.size() << " valid histograms (corresponding to the maximum of "
              << "7 bins) equaling " << formatBytes( validHistograms.size() * sizeof( uint64_t ) ) << "\n";


    /* Check whether we can really ignore the 7-counts as the same number of valid histograms for 6 and 7 suggests.
     * -> We cannot IGNORE it! Rather, given a valid histogram with counts [1,6] specifies an exact required 7-count
     *    to keep the validity. Unfortunately, this cannot be used to trim down the LUT further because we need
     *    to test the 7-count, which filters another 255 out of 256 cases out. But, knowing that 6 counts already
     *    filters 700k values down to 1, it might be possible to do a more costly check for those rare possible values.
     */

    const auto getCount =
        [] ( const uint64_t histogram,
             const uint32_t codeLength )
        {
            assert( codeLength >= 1 );
            return ( histogram >> ( ( codeLength - 1 ) * FREQUENCY_BITS ) )
                   & nLowestBitsSet<uint64_t, FREQUENCY_BITS>();
        };

    std::unordered_set<uint64_t> alternativeValidHistogramsWithout7Counts;
    static constexpr auto HISTOGRAM_COUNT_WITHOUT_7_COUNTS = 1ULL << ( FREQUENCY_BITS * ( FREQUENCY_COUNT - 1 ) );
    for ( uint64_t histogram = 0; histogram < HISTOGRAM_COUNT_WITHOUT_7_COUNTS; ++histogram ) {
        if ( checkPrecodeFrequenciesAlternative<FREQUENCY_BITS, ( FREQUENCY_COUNT - 1 ) >( histogram )
             != pragzip::Error::NONE ) {
            continue;
        }

        /* For 0 or 1 code-lengths with 1 bit, there may be non-zero unused bits! */
        if ( histogram < 2 ) {
            alternativeValidHistogramsWithout7Counts.insert( histogram );
            continue;
        }

        /* Calculate unused symbol count */
        uint8_t unusedSymbolCount{ 2 };
        for ( size_t bitLength = 1; bitLength <= FREQUENCY_COUNT - 1; ++bitLength ) {
            unusedSymbolCount -= getCount( histogram, bitLength );
            unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
        }

        const auto histogramWith7Count = histogram | ( static_cast<uint64_t>( unusedSymbolCount )
                                                       << ( ( FREQUENCY_COUNT - 1 ) * FREQUENCY_BITS ) );
        alternativeValidHistogramsWithout7Counts.insert( histogramWith7Count );

    }
    REQUIRE_EQUAL( validHistograms.size(), alternativeValidHistogramsWithout7Counts.size() );
    REQUIRE( validHistograms == alternativeValidHistogramsWithout7Counts );

    if ( validHistograms != alternativeValidHistogramsWithout7Counts ) {
        std::cerr << "Found in total " << alternativeValidHistogramsWithout7Counts.size()
                  << " valid histograms (corresponding to the maximum of 7 bins) equaling "
                  << formatBytes( alternativeValidHistogramsWithout7Counts.size() * sizeof( uint64_t ) ) << "\n";

        const auto alternativeIsSuperset = std::all_of(
            validHistograms.begin(), validHistograms.end(),
            [&] ( auto histogram ) { return contains( alternativeValidHistogramsWithout7Counts, histogram ); } );
        std::cerr << "Alternative histograms IS " << ( alternativeIsSuperset ? "" : "NOT " )
                  << "superset of histograms!\n";

        {
            std::cerr << "Histograms valid with alternative method but not with faster one:\n";
            size_t differingHistogramsToPrint{ 10 };
            for ( const auto histogram : alternativeValidHistogramsWithout7Counts ) {
                if ( !contains( validHistograms, histogram ) ) {
                    std::cerr << "    " << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram )
                              << "\n";
                    if ( --differingHistogramsToPrint == 0 ) {
                        break;
                    }
                }
            }
            std::cerr << "   ...\n\n";
        }

        {
            std::cerr << "Histograms valid with faster method but not with alternative one:\n";
            size_t differingHistogramsToPrint{ 10 };
            for ( const auto histogram : validHistograms ) {
                if ( !contains( alternativeValidHistogramsWithout7Counts, histogram ) ) {
                    std::cerr << "    " << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram )
                              << "\n";
                    if ( --differingHistogramsToPrint == 0 ) {
                        break;
                    }
                }
            }
            std::cerr << "   ...\n\n";
        }
    }

    if constexpr ( !COMPARE_WITH_ALTERNATIVE_METHOD ) {
        return;
    }

    std::array<uint32_t, FREQUENCY_COUNT> alternativeMaxFrequencies{};
    std::unordered_set<uint64_t> alternativeValidHistograms;
    static constexpr auto HISTOGRAM_COUNT = 1ULL << ( FREQUENCY_BITS * FREQUENCY_COUNT );
    for ( uint64_t histogram = 0; histogram < HISTOGRAM_COUNT; ++histogram ) {
        if ( checkPrecodeFrequenciesAlternative<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram )
             != pragzip::Error::NONE ) {
            continue;
        }

        alternativeValidHistograms.insert( histogram );
        for ( size_t codeLength = 1; codeLength <= FREQUENCY_COUNT; ++codeLength ) {
            const auto count = getCount( histogram, codeLength );
            alternativeMaxFrequencies[codeLength - 1] =
                std::max( alternativeMaxFrequencies[codeLength - 1], static_cast<uint32_t>( count ) );
        }
    }

    if ( validHistograms != alternativeValidHistograms ) {
        std::cerr << "Found in total " << alternativeValidHistograms.size()
                  << " valid histograms (corresponding to the maximum of 7 bins) equaling "
                  << formatBytes( alternativeValidHistograms.size() * sizeof( uint64_t ) ) << "\n";

        const auto alternativeIsSuperset = std::all_of(
            validHistograms.begin(), validHistograms.end(),
            [&] ( auto histogram ) { return contains( alternativeValidHistograms, histogram ); } );
        std::cerr << "Alternative histograms IS " << ( alternativeIsSuperset ? "" : "NOT " )
                  << "superset of histograms!\n";

        std::cerr << "Histograms valid with alternative method but not with faster one:\n";
        size_t differingHistogramsToPrint{ 10 };
        for ( const auto histogram : alternativeValidHistograms ) {
            if ( !contains( validHistograms, histogram ) ) {
                std::cerr << "    " << printCompressedHistogram<FREQUENCY_BITS, FREQUENCY_COUNT>( histogram ) << "\n";
                if ( --differingHistogramsToPrint == 0 ) {
                    break;
                }
            }
        }
        std::cerr << "...\n\n";
    }

    REQUIRE( maxFrequencies == alternativeMaxFrequencies );
    REQUIRE_EQUAL( validHistograms.size(), alternativeValidHistograms.size() );
    REQUIRE( validHistograms == alternativeValidHistograms );
}


int
main()
{
    analyzeMaxValidPrecodeFrequencies</* COMPARE_WITH_ALTERNATIVE_METHOD (quite slow and changes rarely) */ false>();
    analyzeValidPrecodes();

    analyzeValidPrecodeFrequencies<2>();
    analyzeValidPrecodeFrequencies<3>();
    analyzeValidPrecodeFrequencies<4>();
    analyzeValidPrecodeFrequencies<5>();
    //analyzeValidPrecodeFrequencies<6>();  // Creates 128 MiB LUT and 137 MiB binary!
    //analyzeValidPrecodeFrequencies<7>();  // Does not compile / link. I think the binary becomes too large

    std::cout << "\nTests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}


/*
cmake --build . -- benchmarkGzipBlockFinder && taskset 0x08 src/benchmarks/benchmarkGzipBlockFinder random.gz

Valid Histogram with >=16 codes of the same length: 1:0 2:0 3:0 4:16 5:0 6:0 7:0 (0x0000000000080000)
Valid Histogram with >=16 codes of the same length: 1:0 2:1 3:2 4:0 5:16 6:0 7:0 (0x0000000001000820)
Valid Histogram with >=16 codes of the same length: 1:0 2:2 3:0 4:0 5:16 6:0 7:0 (0x0000000001000040)
Valid Histogram with >=16 codes of the same length: 1:0 2:3 3:0 4:0 5:0 6:16 7:0 (0x0000000020000060)
Valid Histogram with >=16 codes of the same length: 1:1 2:0 3:0 4:0 5:16 6:0 7:0 (0x0000000001000001)
Valid Histogram with >=16 codes of the same length: 1:1 2:0 3:2 4:0 5:0 6:16 7:0 (0x0000000020000801)
Valid Histogram with >=16 codes of the same length: 1:1 2:1 3:0 4:0 5:0 6:16 7:0 (0x0000000020000021)
Valid Histogram with >=16 codes of the same length: 1:1 2:1 3:1 4:0 5:0 6:0 7:16 (0x0000000400000421)

Maximum length frequencies of valid histograms:
    Code Length 1 : 2
    Code Length 2 : 4
    Code Length 3 : 8
    Code Length 4 : 16
    Code Length 5 : 16
    Code Length 6 : 16
    Code Length 7 : 16

Found in total 1526 valid histograms (corresponding to the maximum of 7 bins) equaling 11 KiB 944 B
Valid precodes 400814 out of 100000000 tested -> 0.400814 %
Encountered errors:
    90010469 Constructing a Huffman coding from the given code length sequence failed!
     9588717 The Huffman coding is not optimal!
      400814 No error.

Precode frequency LUT containing 2 bins is sized: 128 B. There are 9 valid entries out of 1024 -> 0.878906 %
Precode frequency LUT containing 3 bins is sized: 4 KiB. There are 35 valid entries out of 32768 -> 0.106812 %
Precode frequency LUT containing 4 bins is sized: 128 KiB. There are 157 valid entries out of 1048576 -> 0.0149727 %
Precode frequency LUT containing 5 bins is sized: 4 MiB. There are 561 valid entries out of 33554432 -> 0.00167191 %
Precode frequency LUT containing 6 bins is sized: 128 MiB. There are 1526 valid entries out of 1073741824 -> 0.000142212 %
Precode frequency LUT containing 7 bins is sized: 4 GiB. There are 1526 valid entries out of 34359738368 -> 0.000004441 %

Tests successful: 10 / 10
*/
