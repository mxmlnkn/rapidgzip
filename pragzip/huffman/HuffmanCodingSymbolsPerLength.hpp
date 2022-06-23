#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include "HuffmanCodingBase.hpp"


namespace pragzip
{
template<typename HuffmanCode,
         uint8_t  MAX_CODE_LENGTH,
         typename Symbol,
         size_t   MAX_SYMBOL_COUNT>
class HuffmanCodingSymbolsPerLength :
    public HuffmanCodingBase<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>
{
public:
    using BaseType = HuffmanCodingBase<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
    using BitCount = typename BaseType::BitCount;
    using CodeLengthFrequencies = typename BaseType::CodeLengthFrequencies;

protected:
    constexpr void
    initializeSymbolsPerLength( const VectorView<BitCount>&  codeLengths,
                                const CodeLengthFrequencies& bitLengthFrequencies )
    {
        /* Calculate cumulative frequency sums to be used as offsets for each code-length
         * for the code-length-sorted alphabet vector. */
        size_t sum = 0;
        for ( uint8_t bitLength = this->m_minCodeLength; bitLength <= this->m_maxCodeLength; ++bitLength ) {
            m_offsets[bitLength - this->m_minCodeLength] = sum;
            sum += bitLengthFrequencies[bitLength];
        }
        m_offsets[this->m_maxCodeLength - this->m_minCodeLength + 1] = sum;

        /* The codeLengths.size() check above should implicitly check this already. */
        assert( sum <= m_symbolsPerLength.size() && "Specified max symbol range exceeded!" );

        /* Fill the code-length-sorted alphabet vector. */
        auto sizes = m_offsets;
        for ( Symbol symbol = 0; symbol < codeLengths.size(); ++symbol ) {
            if ( codeLengths[symbol] != 0 ) {
                const auto k = codeLengths[symbol] - this->m_minCodeLength;
                m_symbolsPerLength[sizes[k]++] = symbol;
            }
        }
    }

public:
    [[nodiscard]] constexpr Error
    initializeFromLengths( const VectorView<BitCount>& codeLengths )
    {
        if ( const auto errorCode = BaseType::initializeMinMaxCodeLengths( codeLengths );
             errorCode != Error::NONE )
        {
            return errorCode;
        }

        CodeLengthFrequencies bitLengthFrequencies = {};
        for ( const auto value : codeLengths ) {
            ++bitLengthFrequencies[value];
        }

        if ( const auto errorCode = BaseType::checkCodeLengthFrequencies( bitLengthFrequencies, codeLengths.size() );
             errorCode != Error::NONE ) {
            return errorCode;
        }

        BaseType::initializeMinimumCodeValues( bitLengthFrequencies );  // resets bitLengthFrequencies[0] to 0!

        initializeSymbolsPerLength( codeLengths, bitLengthFrequencies );

        return Error::NONE;
    }

    [[nodiscard]] forceinline std::optional<Symbol>
    decode( BitReader& bitReader ) const
    {
        HuffmanCode code = 0;

        /** Read the first n bytes. Note that we can't call the bitReader with argument > 1 because the bit order
         * would be inversed. @todo Reverse the Huffman codes and prepend bits instead of appending, so that this
         * first step can be conflated and still have the correct order for comparison! */
        for ( BitCount i = 0; i < this->m_minCodeLength; ++i ) {
            code = ( code << 1 ) | ( bitReader.read<1>() );
        }

        for ( BitCount k = 0; k <= this->m_maxCodeLength - this->m_minCodeLength; ++k ) {
            const auto minCode = this->m_minimumCodeValuesPerLevel[k];
            if ( minCode <= code ) {
                const auto subIndex = m_offsets[k] + static_cast<size_t>( code - minCode );
                if ( subIndex < m_offsets[k + 1] ) {
                    return m_symbolsPerLength[subIndex];
                }
            }

            code <<= 1;
            code |= bitReader.read<1>();
        }

        return {};
    }

protected:
    /**
     * Contains the alphabet, first sorted by code length, then by given alphabet order. E.g., it could look like this:
     * @verbatim
     * +-------+-----+---+
     * | B D E | A F | C |
     * +-------+-----+---+
     *   CL=3   CL=4  CL=5
     * @endverbatim
     * The starting index for a given code length (CL) can be queried with m_offsets.
     */
    std::array<Symbol, MAX_SYMBOL_COUNT> m_symbolsPerLength{};
    /* +1 because it stores the size at the end as well as 0 in the first element, which might be redundant but fast. */
    std::array<uint16_t, MAX_CODE_LENGTH + 1> m_offsets{};
    static_assert( MAX_SYMBOL_COUNT + MAX_CODE_LENGTH <= std::numeric_limits<uint16_t>::max(),
                   "Offset type must be able to point at all symbols!" );
};
}  // namespace pragzip
