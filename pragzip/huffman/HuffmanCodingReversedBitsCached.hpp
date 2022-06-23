#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "HuffmanCodingSymbolsPerLength.hpp"


namespace pragzip
{
template<typename HuffmanCode,
         uint8_t  MAX_CODE_LENGTH,
         typename Symbol,
         size_t   MAX_SYMBOL_COUNT>
class HuffmanCodingReversedBitsCached :
    public HuffmanCodingSymbolsPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>
{
public:
    using BaseType = HuffmanCodingSymbolsPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
    using BitCount = typename BaseType::BitCount;
    using CodeLengthFrequencies = typename BaseType::CodeLengthFrequencies;

    static constexpr auto CACHED_BIT_COUNT = MAX_CODE_LENGTH;

public:
    [[nodiscard]] constexpr Error
    initializeFromLengths( const VectorView<BitCount>& codeLengths )
    {
        if ( const auto errorCode = BaseType::initializeFromLengths( codeLengths );
             errorCode != Error::NONE )
        {
            return errorCode;
        }

        /* Initialize the cache.
         * In benchmarks, this takes 28µs out of ~ 30µs for total initialization.
         * And for decoding 13403 deflate blocks in 5.7s, this makes a total overhead of 0.38s (6.6%).
         * The actual block decoding as opposed to header reading, takes roughly 400µs (total over blocks: 5.3s)
         *  -> This adds up to the observed timings and shows that the header reading is still more than
         *     a magnitude faster and could still do some more setup if it reduces decoding more than that!.
         * So it isn't all that large but also doesn't improve speed by all that much either :(
         * Maybe try smaller lookup table to stay in L1 cache?
         * The test processor, a Ryzen 3900X has
         *   L1 Cache: 64K (per core)
         *   L2 Cache: 512K (per core)
         *   L3 Cache: 64MB (shared)
         * So, theoretically it shouldn't exceed the L1 cache size but who knows. */
        //const auto t0 = now();
        auto codeValues = this->m_minimumCodeValuesPerLevel;
        for ( Symbol symbol = 0; symbol < codeLengths.size(); ++symbol ) {
            const auto length = codeLengths[symbol];
            if ( length == 0 ) {
                continue;
            }

            const auto k = length - this->m_minCodeLength;
            const auto code = codeValues[k]++;

            HuffmanCode reversedCode{ 0 };
            if constexpr ( sizeof( HuffmanCode ) <= sizeof( reversedBitsLUT16[0] ) ) {
                reversedCode = reversedBitsLUT16[code];
            } else {
                reversedCode = reverseBits( code );
            }
            reversedCode >>= ( std::numeric_limits<decltype( code )>::digits - length );

            const auto fillerBitCount = CACHED_BIT_COUNT - length;
            for ( HuffmanCode fillerBits = 0; fillerBits < ( 1UL << fillerBitCount ); ++fillerBits ) {
                const auto paddedCode = static_cast<HuffmanCode>( ( fillerBits << length ) | reversedCode );
                assert( paddedCode < m_codeCache.size() );
                m_codeCache[paddedCode].first = length;
                m_codeCache[paddedCode].second = symbol;
            }
        }
        //const auto t1 = now();
        //std::cerr << "Creating Huffman LUT took " << duration(t0,t1) << " s\n";
        // Some timings for small.gz: 2.3676e-05 s, 2.186e-05 s, 2.3607e-05 s, 2.1791e-05 s, 2.3606e-05 s, 3.6038e-05 s

        return Error::NONE;
    }

    [[nodiscard]] forceinline std::optional<Symbol>
    decode( BitReader& bitReader ) const
    {
        try {
            const auto value = bitReader.peek<CACHED_BIT_COUNT>();

            assert( value < m_codeCache.size() );
            const auto [length, symbol] = m_codeCache[(int)value];

            /* Unfortunately, read is much faster than a simple seek forward,
             * probably because of inlining and extraneous checks. For some reason read seems even faster than
             * the newly introduced and trimmed down seekAfterPeek ... */
            bitReader.read( length );
            if ( length == 0 ) {
                throw std::logic_error( "Invalid Huffman code encountered!" );
            }
            return symbol;
        } catch ( const BitReader::EndOfFileReached& ) {
            /* Should only happen at the end of the file and probably not even there
             * because the gzip footer should be longer than the peek length. */
            return BaseType::decode( bitReader );
        }
    }

private:
    alignas(8) std::array<std::pair</* length */ uint8_t, Symbol>, ( 1UL << CACHED_BIT_COUNT )> m_codeCache{};
};
}  // namespace pragzip
