#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include <huffman/HuffmanCodingSymbolsPerLength.hpp>

#include "definitions.hpp"


namespace rapidgzip::deflate
{
/**
 * This started as a copy of @ref HuffmanCodingShortBitsCached, however, it incorporates deflate-specific things:
 *  - Default arguments for HuffmanCode and Symbol types as well as assuming REVERSE_BITT=true.
 */
template<uint8_t LUT_BITS_COUNT>
class HuffmanCodingShortBitsCachedDeflate :
    public HuffmanCodingSymbolsPerLength<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_LITERAL_HUFFMAN_CODE_COUNT,
                                         /* CHECK_OPTIMALITY */ true>
{
public:
    using Symbol = uint16_t;
    using HuffmanCode = uint16_t;
    using BaseType = HuffmanCodingSymbolsPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_LITERAL_HUFFMAN_CODE_COUNT,
                                                   /* CHECK_OPTIMALITY */ true>;
    using BitCount = typename BaseType::BitCount;
    using CodeLengthFrequencies = typename BaseType::CodeLengthFrequencies;

public:
    [[nodiscard]] constexpr Error
    initializeFromLengths( const VectorView<BitCount>& codeLengths )
    {
        if ( const auto errorCode = BaseType::initializeFromLengths( codeLengths );
             errorCode != Error::NONE )
        {
            return errorCode;
        }

        m_lutBitsCount = std::min( LUT_BITS_COUNT, this->m_maxCodeLength );
        m_bitsToReadAtOnce = std::max( LUT_BITS_COUNT, this->m_minCodeLength );

        /* Initialize the cache. */
        if ( m_needsToBeZeroed ) {
            // Works constexpr
            for ( size_t symbol = 0; symbol < m_codeCache.size(); ++symbol ) {
                m_codeCache[symbol].length = 0;
            }
        }

        auto codeValues = this->m_minimumCodeValuesPerLevel;
        for ( size_t symbol = 0; symbol < codeLengths.size(); ++symbol ) {
            const auto length = codeLengths[symbol];
            if ( ( length == 0 ) || ( length > m_lutBitsCount ) ) {
                continue;
            }

            const auto fillerBitCount = m_lutBitsCount - length;
            const auto k = length - this->m_minCodeLength;
            const auto code = codeValues[k]++;
            const auto reversedCode = reverseBits( code, length );
            const auto maximumPaddedCode = static_cast<HuffmanCode>(
                reversedCode | ( nLowestBitsSet<HuffmanCode>( fillerBitCount ) << length ) );
            assert( maximumPaddedCode < m_codeCache.size() );
            const auto increment = static_cast<HuffmanCode>( HuffmanCode( 1 ) << length );
            for ( auto paddedCode = reversedCode; paddedCode <= maximumPaddedCode; paddedCode += increment ) {
                m_codeCache[paddedCode].length = length;
                m_codeCache[paddedCode].symbol = static_cast<Symbol>( symbol );
            }
        }

        m_needsToBeZeroed = true;

        return Error::NONE;
    }

    template<typename BitReader>
    [[nodiscard]] forceinline std::optional<Symbol>
    decode( BitReader& bitReader ) const
    {
        try {
            const auto [length, symbol] = m_codeCache[bitReader.peek( m_lutBitsCount )];
            if ( length == 0 ) {
                return decodeLong( bitReader );
            }
            bitReader.seekAfterPeek( length );
            return symbol;
        } catch ( const typename BitReader::EndOfFileReached& ) {
            /* Should only happen at the end of the file and probably not even there
             * because the bzip2 footer (EOS block) should be longer than the peek length. */
            return BaseType::decode( bitReader );
        }
    }

private:
    template<typename BitReader>
    [[nodiscard]] forceinline constexpr std::optional<Symbol>
    decodeLong( BitReader& bitReader ) const
    {
        HuffmanCode code = 0;

        /** Read the first n bytes. Note that we can't call the bitReader with argument > 1 because the bit order
         * would be inversed. @todo Reverse the Huffman codes and prepend bits instead of appending, so that this
         * first step can be conflated and still have the correct order for comparison! */
        for ( BitCount i = 0; i < m_bitsToReadAtOnce; ++i ) {
            code = ( code << 1U ) | ( bitReader.template read<1>() );
        }

        for ( BitCount k = m_bitsToReadAtOnce - this->m_minCodeLength;
              k <= this->m_maxCodeLength - this->m_minCodeLength; ++k )
        {
            const auto minCode = this->m_minimumCodeValuesPerLevel[k];
            if ( minCode <= code ) {
                const auto subIndex = this->m_offsets[k] + static_cast<size_t>( code - minCode );
                if ( subIndex < this->m_offsets[k + 1] ) {
                    return this->m_symbolsPerLength[subIndex];
                }
            }

            code <<= 1;
            code |= bitReader.template read<1>();
        }

        return std::nullopt;
    }

private:
    struct CacheEntry
    {
        uint8_t length{ 0 };  // ceil(log2 MAX_CODE_LENGTH(20)) = 5 bits would suffice
        Symbol symbol{ 0 };
    };
    static_assert( sizeof( CacheEntry ) == 2 * sizeof( Symbol ), "CacheEntry is larger than assumed!" );

    /**
     * sizeof(CacheEntry) = 4 B for Symbol=uint16_t.
     * Total m_codeCache sizes for varying LUT_BITS_COUNT:
     *  - 10 bits -> 4 KiB
     *  - 11 bits -> 8 KiB
     *  - 12 bits -> 16 KiB
     */
    alignas( 64 ) std::array<CacheEntry, ( 1UL << LUT_BITS_COUNT )> m_codeCache{};

    uint8_t m_lutBitsCount{ LUT_BITS_COUNT };
    uint8_t m_bitsToReadAtOnce{ LUT_BITS_COUNT };
    bool m_needsToBeZeroed{ false };
};
}  // namespace rapidgzip::deflate
