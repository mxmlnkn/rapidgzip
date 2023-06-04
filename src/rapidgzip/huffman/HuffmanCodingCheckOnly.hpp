#pragma once

#include <optional>
#include <stdexcept>

#include "HuffmanCodingBase.hpp"


namespace rapidgzip
{
template<typename HuffmanCode,
         uint8_t  MAX_CODE_LENGTH,
         typename Symbol,
         size_t   MAX_SYMBOL_COUNT>
class HuffmanCodingCheckOnly:
    public HuffmanCodingBase<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>
{
public:
    using BaseType = HuffmanCodingBase<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
    using BitCount = typename BaseType::BitCount;
    using CodeLengthFrequencies = typename BaseType::CodeLengthFrequencies;

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

        return BaseType::checkCodeLengthFrequencies( bitLengthFrequencies, codeLengths.size() );
    }

    [[nodiscard]] forceinline std::optional<Symbol>
    decode( BitReader& ) const
    {
        throw std::invalid_argument( "This class only checks the Huffman coding it does not decode!" );
    }
};
}  // namespace rapidgzip
