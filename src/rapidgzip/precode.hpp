#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "definitions.hpp"
#include "huffman/HuffmanCodingReversedBitsCachedCompressed.hpp"


namespace rapidgzip::deflate::precode
{
constexpr uint8_t MAX_DEPTH = 7;

/* Contains how often a the code lengths [1,7] appear. */
using Histogram = std::array<uint8_t, MAX_DEPTH>;


/* operator== is only constexpr for std::array since C++20 */
[[nodiscard]] constexpr bool
operator==( const Histogram& a,
            const Histogram& b )
{
    for ( size_t i = 0; i < a.size(); ++i ) {
        if ( a[i] != b[i] ) {
            return false;
        }
    }
    return true;
}


/**
 * This alternative version tries to reduce the number of instructions required for creation so that it can be
 * used with MSVC, which is the worst in evaluating @ref createPrecodeFrequenciesValidLUT constexpr and runs into
 * internal compiler errors or out-of-heap-space errors.
 * This alternative implementation uses the fact only very few of the LUT entries are actually valid, meaning
 * we can reduce instructions by initializing to invalid and then iterating only over the valid possibilities.
 * @param depth A depth of 1 means that we should iterate over 1-bit codes, which can only be 0,1,2.
 * @param freeBits This can be calculated from the histogram but it saves constexpr instructions when
 *        the caller updates this value outside.
 */
template<typename Result,
         typename Functor,
         uint8_t  DEPTH = 1>
constexpr void
iterateValidPrecodeHistograms( Result&        result,
                               const Functor& processValidHistogram,
                               const uint32_t remainingCount = rapidgzip::deflate::MAX_PRECODE_COUNT,
                               Histogram      histogram = Histogram{},
                               const uint32_t freeBits = 2 )
{
    static_assert( DEPTH <= MAX_DEPTH, "Cannot descend deeper than the frequency counts!" );

    /* The for loop maximum is given by the invalid Huffman code check, i.e.,
     * when there are more code lengths on a tree level than there are nodes. */
    for ( uint32_t count = 0; count <= std::min( remainingCount, freeBits ); ++count ) {
        histogram[DEPTH - 1] = count;
        const auto newFreeBits = ( freeBits - count ) * 2;

        /* The first layer may not be fully filled or even empty. This does not fit any of the general tests. */
        if constexpr ( DEPTH == 1 ) {
            if ( count == 1 ) {
                processValidHistogram( histogram );
            }
        }

        if constexpr ( DEPTH == MAX_DEPTH ) {
            if ( newFreeBits == 0 ) {
                processValidHistogram( histogram );
            }
        } else {
            if ( count == freeBits ) {
                processValidHistogram( histogram );
            } else {
                const auto newRemainingCount = remainingCount - count;
                iterateValidPrecodeHistograms<Result, Functor, DEPTH + 1>( result, processValidHistogram,
                                                                           newRemainingCount, histogram, newFreeBits );
            }
        }
    }
}


constexpr auto VALID_HISTOGRAMS_COUNT =
    [] ()
    {
        size_t validCount{ 0 };
        iterateValidPrecodeHistograms( validCount, [&validCount] ( const auto& ) { ++validCount; } );
        return validCount;
    }();

static_assert( VALID_HISTOGRAMS_COUNT == 1526 );


static constexpr auto VALID_HISTOGRAMS =
    [] ()
    {
        size_t validCount{ 0 };
        std::array<Histogram, VALID_HISTOGRAMS_COUNT> validHistograms{};

        iterateValidPrecodeHistograms(
            validHistograms,
            [&validCount, &validHistograms] ( const Histogram& histogram ) {
                validHistograms[validCount++] = histogram;
            } );

        return validHistograms;
    }();

static_assert( VALID_HISTOGRAMS.back() == Histogram{ { /* code length 1 */ 2, } } );


using PrecodeHuffmanCoding = HuffmanCodingReversedBitsCachedCompressed<uint8_t, MAX_PRECODE_LENGTH,
                                                                       uint8_t, MAX_PRECODE_COUNT>;


static constexpr auto VALID_HUFFMAN_CODINGS =
    [] ()
    {
        std::array<PrecodeHuffmanCoding, VALID_HISTOGRAMS_COUNT> huffmanCodings{};
        for ( size_t i = 0; i < VALID_HISTOGRAMS.size(); ++i ) {
            const auto& histogram = VALID_HISTOGRAMS[i];

            std::array<uint8_t, MAX_PRECODE_COUNT> precodeCLs{};
            uint8_t symbol{ 0 };
            for ( size_t codeLength = 0; codeLength < histogram.size(); ++codeLength ) {
                /* actually code length + 1 because 0 is not included */
                const auto count = histogram[codeLength];
                for ( uint8_t j = 0; j < count; ++j ) {
                    precodeCLs[symbol++] = codeLength + 1;
                }
            }

            const auto error = huffmanCodings[i].initializeFromLengths( {
                precodeCLs.data(), symbol /* also acts as non-zero size. */ } );
            if ( error != rapidgzip::Error::NONE ) {
                throw std::logic_error( "Cannot construct Huffman tree from supposedly valid code length histogram!" );
            }
        }
        return huffmanCodings;
    }();


[[nodiscard]] auto constexpr
getAlphabetFromCodeLengths( const uint64_t precodeBits,
                            const uint64_t histogramWith5BitCounts )
{
    /* Get code lengths (CL) for alphabet P. */
    std::array<uint8_t, MAX_PRECODE_COUNT> codeLengthCL{};
    for ( size_t i = 0; i < MAX_PRECODE_COUNT; ++i ) {
        const auto codeLength = ( precodeBits >> ( i * PRECODE_BITS ) ) & nLowestBitsSet<uint64_t, PRECODE_BITS>();;
        codeLengthCL[PRECODE_ALPHABET[i]] = codeLength;
    }

    std::array<uint8_t, 8> offsets{};
    for ( size_t codeLength = 1; codeLength <= 7; ++codeLength ) {
        const auto count = ( histogramWith5BitCounts >> ( codeLength * 5 ) ) & nLowestBitsSet<uint64_t, 5>();
        offsets[codeLength] = offsets[codeLength - 1] + count;
    }

    std::array<uint8_t, MAX_PRECODE_COUNT> alphabet{};
    for ( size_t symbol = 0; symbol < codeLengthCL.size(); ++symbol ) {
        const auto codeLength = codeLengthCL[symbol];
        if ( codeLength > 0 ) {
            const auto offset = offsets[codeLength - 1]++;
            alphabet[offset] = symbol;
        }
    }

    return alphabet;
}
}  // namespace rapidgzip::deflate::precode
