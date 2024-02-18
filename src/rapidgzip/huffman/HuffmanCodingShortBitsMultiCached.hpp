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
#include "RFCTables.hpp"


namespace rapidgzip::deflate
{
/**
 * This version uses a lookup table (LUT) to avoid repetitive loops over BitReader::read<1>() to speed things up a lot.
 * This started as a copy of @ref HuffmanCodingReversedBitsCached, however:
 * - It limits the LUT to a fixed size instead of caching everything up to MAX_CODE_LENGTH because that
 *   would be too large for bzip2, for which MAX_CODE_LENGTH is 20 instead of 16 for gzip.
 */
template<uint8_t LUT_BITS_COUNT>
class HuffmanCodingShortBitsMultiCached :
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

    struct CacheEntry
    {
        uint8_t bitsToSkip{ 0 };  // ceil(log2 MAX_CODE_LENGTH(20)) = 5 bits would suffice
        Symbol symbol{ 0 };
    };

    using Symbols = std::pair</* packed symbols */ uint32_t, /* symbol count */ uint32_t>;

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
                m_codeCache[symbol].bitsToSkip = 0;
            }
        }

        /* Compute Huffman values. */

        struct HuffmanEntry
        {
            HuffmanCode reversedCode{ 0 };
            Symbol symbol{ 0 };
            uint8_t length{ 0 };
        };

        std::array<HuffmanEntry, MAX_LITERAL_HUFFMAN_CODE_COUNT> huffmanTable{};
        size_t huffmanTableSize{ 0 };

        if ( codeLengths.size() > huffmanTable.size() ) {
            return Error::INVALID_CODE_LENGTHS;
        }

        auto codeValues = this->m_minimumCodeValuesPerLevel;
        for ( size_t symbol = 0; symbol < codeLengths.size(); ++symbol ) {
            const auto length = codeLengths[symbol];
            /* Note that the preemptive continue for large lengths is not a bug even if we do not increment
             * codeValues because all symbols of the same length will either be filtered or not, so that
             * the missing increment does not matter because that code is either always used or never. */
            if ( ( length == 0 ) || ( length > m_lutBitsCount ) ) {
                continue;
            }

            auto& entry = huffmanTable[huffmanTableSize++];
            entry.length = length;
            entry.symbol = static_cast<Symbol>( symbol );
            entry.reversedCode = reverseBits( codeValues[length - this->m_minCodeLength]++, length );
        }

        /* Fill up cache. */

        for ( size_t i = 0; i < huffmanTableSize; ++i ) {
            const auto& huffmanEntry = huffmanTable[i];

            CacheEntry cacheEntry{};
            cacheEntry.bitsToSkip = huffmanEntry.length;
            cacheEntry.symbol = huffmanEntry.symbol;
            insertIntoCache( huffmanEntry.reversedCode, cacheEntry );
        }

        m_needsToBeZeroed = true;

        return Error::NONE;
    }

    template<typename BitReader>
    [[nodiscard]] forceinline Symbols
    decode( BitReader& bitReader ) const
    {
        try {
            const auto [bitsToSkip, symbol] = m_codeCache[bitReader.peek( m_lutBitsCount )];
            if ( bitsToSkip == 0 ) {
                return decodeLong( bitReader );
            }
            bitReader.seekAfterPeek( bitsToSkip );
            return { readLength( symbol, bitReader ), 1 };
        } catch ( const typename BitReader::EndOfFileReached& ) {
            /* Should only happen at the end of the file and probably not even there
             * because the bzip2 footer (EOS block) should be longer than the peek length. */
            const auto result = BaseType::decode( bitReader );
            if ( result ) {
                return { readLength( *result, bitReader ), 1 };
            }
            return { 0, 0 };
        }
    }

private:
    template<typename BitReader>
    [[nodiscard]] forceinline constexpr Symbols
    decodeLong( BitReader& bitReader ) const
    {
        HuffmanCode code = 0;

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
                    return { readLength( this->m_symbolsPerLength[subIndex], bitReader ), 1 };
                }
            }

            code <<= 1;
            code |= bitReader.template read<1>();
        }

        return { 0, 0 };
    }

    template<typename BitReader>
    [[nodiscard]] forceinline constexpr Symbol
    readLength( const Symbol symbol,
                BitReader&   bitReader ) const
    {
        if ( symbol <= 256 ) {
            return symbol;
        }
        /* Basically the same as ( 1 << 8U ) | getLengthMinus3 */
        return getLength( symbol, bitReader ) + 254U;
    }

    forceinline void
    insertIntoCache( HuffmanCode reversedCode,
                     CacheEntry  cacheEntry )
    {
        const auto length = cacheEntry.bitsToSkip;
        if ( length > m_lutBitsCount ) {
            return;
        }
        const auto fillerBitCount = m_lutBitsCount - length;

        const auto maximumPaddedCode = static_cast<HuffmanCode>(
            reversedCode | ( nLowestBitsSet<HuffmanCode>( fillerBitCount ) << length ) );
        assert( maximumPaddedCode < m_codeCache.size() );
        const auto increment = static_cast<HuffmanCode>( HuffmanCode( 1 ) << length );
        for ( auto paddedCode = reversedCode; paddedCode <= maximumPaddedCode; paddedCode += increment ) {
            m_codeCache[paddedCode] = cacheEntry;
        }
    }

private:
    alignas( 64 ) std::array<CacheEntry, ( 1UL << LUT_BITS_COUNT )> m_codeCache{};

    uint8_t m_lutBitsCount{ LUT_BITS_COUNT };
    uint8_t m_bitsToReadAtOnce{ LUT_BITS_COUNT };
    bool m_needsToBeZeroed{ false };
};
}  // namespace rapidgzip::deflate
