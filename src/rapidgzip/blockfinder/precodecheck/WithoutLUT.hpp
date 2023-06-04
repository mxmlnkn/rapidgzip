#pragma once

#include <array>
#include <cstdint>

#include <BitManipulation.hpp>
#include <deflate.hpp>
#include <Error.hpp>


namespace rapidgzip::PrecodeCheck::WithoutLUT
{
/**
 * @note Requires 4 (precode count) + 57 (maximum precode count * 3) bits to check for validity.
 *       Get all 57 bits at once to avoid a data dependency on the precode count. Note that this is only
 *       possible assuming a 64-bit gzip footer, else, this could be a wrong transformation because it wouldn't
 *       be able to find very small deflate blocks close to the end of the file. because they trigger an EOF.
 *       Note that such very small blocks would normally be Fixed Huffman decoding anyway.
 */
[[nodiscard]] constexpr rapidgzip::Error
checkPrecode( const uint64_t next4Bits,
              const uint64_t next57Bits )
{
    using namespace rapidgzip::deflate;
    constexpr auto UNIFORM_FREQUENCY_BITS = 5U;
    using CompressedHistogram = uint64_t;

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );

    CompressedHistogram bitLengthFrequencies{ 0 };
    for ( size_t i = 0; i < codeLengthCount; ++i ) {
        const auto value = ( precodeBits >> ( i * PRECODE_BITS ) )
                           & nLowestBitsSet<CompressedHistogram, PRECODE_BITS>();
        bitLengthFrequencies += 1ULL << ( value * UNIFORM_FREQUENCY_BITS );
    }

    const auto zeroCounts = bitLengthFrequencies & nLowestBitsSet<CompressedHistogram, UNIFORM_FREQUENCY_BITS>();
    const auto nonZeroCount = codeLengthCount - zeroCounts;

    /* Note that bitLengthFrequencies[0] must not be checked because multiple symbols may have code length
     * 0 simply when they do not appear in the text at all! And this may very well happen because the
     * order for the code lengths per symbol in the bit stream is fixed. */
    bool invalidCodeLength{ false };
    uint32_t unusedSymbolCount{ 2 };
    constexpr auto MAX_LENGTH = ( 1U << PRECODE_BITS );
    for ( size_t bitLength = 1; bitLength < MAX_LENGTH; ++bitLength ) {
        const auto frequency = ( bitLengthFrequencies >> ( bitLength * UNIFORM_FREQUENCY_BITS ) )
                               & nLowestBitsSet<CompressedHistogram, UNIFORM_FREQUENCY_BITS>();
        invalidCodeLength |= frequency > unusedSymbolCount;
        unusedSymbolCount -= frequency;
        unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
    }
    if ( invalidCodeLength ) {
        return rapidgzip::Error::INVALID_CODE_LENGTHS;
    }

    /* Using bit-wise 'and' and 'or' to avoid expensive branching does not improve performance measurably.
     * It is likely that GCC 11 already does the same optimization because it can deduce that the branched
     * comparison have no side-effects. Therefore, keep using logical operations because they are more
     * readable. Note that the standard defines bool to int conversion as true->1, false->0. */
    if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount != ( 1U << ( MAX_LENGTH - 1U ) ) ) ) ||
         ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
        return rapidgzip::Error::BLOATING_HUFFMAN_CODING;
    }

    if ( nonZeroCount == 0 ) {
        return rapidgzip::Error::EMPTY_ALPHABET;
    }

    return rapidgzip::Error::NONE;
}


[[nodiscard]] constexpr rapidgzip::Error
checkPrecodeUsingArray( const uint64_t next4Bits,
                        const uint64_t next57Bits )
{
    using namespace rapidgzip::deflate;
    constexpr auto MAX_LENGTH = ( 1U << PRECODE_BITS );

    const auto codeLengthCount = 4 + next4Bits;
    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );

    std::array<uint8_t, MAX_LENGTH> bitLengthFrequencies{};
    for ( size_t i = 0; i < codeLengthCount; ++i ) {
        const auto value = ( precodeBits >> ( i * PRECODE_BITS ) ) & nLowestBitsSet<uint64_t, PRECODE_BITS>();
        bitLengthFrequencies[value]++;
    }

    const auto nonZeroCount = codeLengthCount - bitLengthFrequencies[0];

    /* Note that bitLengthFrequencies[0] must not be checked because multiple symbols may have code length
     * 0 simply when they do not appear in the text at all! And this may very well happen because the
     * order for the code lengths per symbol in the bit stream is fixed. */
    bool invalidCodeLength{ false };
    uint32_t unusedSymbolCount{ 2 };
    for ( size_t bitLength = 1; bitLength < MAX_LENGTH; ++bitLength ) {
        const auto frequency = bitLengthFrequencies[bitLength];
        invalidCodeLength |= frequency > unusedSymbolCount;
        unusedSymbolCount -= frequency;
        unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
    }
    if ( invalidCodeLength ) {
        return rapidgzip::Error::INVALID_CODE_LENGTHS;
    }

    /* Using bit-wise 'and' and 'or' to avoid expensive branching does not improve performance measurably.
     * It is likely that GCC 11 already does the same optimization because it can deduce that the branched
     * comparison have no side-effects. Therefore, keep using logical operations because they are more
     * readable. Note that the standard defines bool to int conversion as true->1, false->0. */
    if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount != ( 1U << ( MAX_LENGTH - 1U ) ) ) ) ||
         ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
        return rapidgzip::Error::BLOATING_HUFFMAN_CODING;
    }

    if ( nonZeroCount == 0 ) {
        return rapidgzip::Error::EMPTY_ALPHABET;
    }

    return rapidgzip::Error::NONE;
}
}  // namespace rapidgzip::PrecodeCheck::WithoutLUT
