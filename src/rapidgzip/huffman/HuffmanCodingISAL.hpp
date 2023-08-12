#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include <common.hpp>           // forceinline
#include <definitions.hpp>      // BitReader
#include <Error.hpp>
#include <VectorView.hpp>

#include <igzip_lib.h>


namespace rapidgzip
{
/**
 * A wrapper around the Huffman decoder from ISA-l
 */
class HuffmanCodingISAL
{
public:
    static constexpr auto LIT_LEN_ELEMS = 514U;

    static constexpr auto MAX_LIT_LEN_CODE_LEN = 21U;
    static constexpr auto MAX_LIT_LEN_COUNT = MAX_LIT_LEN_CODE_LEN + 2U;
    static constexpr auto LIT_LEN = ISAL_DEF_LIT_LEN_SYMBOLS;

    static constexpr uint8_t len_extra_bit_count[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02,
        0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04,
        0x05, 0x05, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00
    };

public:
    [[nodiscard]] Error
    initializeFromLengths( const VectorView<uint8_t>& codeLengths )
    {
        std::array<huff_code, LIT_LEN_ELEMS> lit_and_dist_huff{};
        std::array<uint16_t, MAX_LIT_LEN_COUNT> lit_count{};
        std::array<uint16_t, MAX_LIT_LEN_COUNT> lit_expand_count{};

        /* Decode the lit/len and dist huffman codes using the code huffman code */
        for ( size_t i = 0; i < codeLengths.size(); ++i ) {
            const auto symbol = codeLengths[i];

            lit_count[symbol]++;
            write_huff_code( &lit_and_dist_huff[i], 0, symbol );

            if ( ( symbol != 0 ) && ( i >= 264 /* Lit/Len with no extra bits */ ) ) {
                const auto extra_count = len_extra_bit_count[i - 257U];
                lit_expand_count[symbol]--;
                lit_expand_count[symbol + extra_count] += 1U << extra_count;
            }
        }

        std::array<uint32_t, LIT_LEN_ELEMS + 2> code_list{};  /* The +2 is for the extra codes in the static header */

        if ( set_and_expand_lit_len_huffcode( lit_and_dist_huff.data(), LIT_LEN,
                                              lit_count.data(), lit_expand_count.data(), code_list.data() )
             != ISAL_DECOMP_OK ) {
            m_error = Error::INVALID_HUFFMAN_CODE;
            return m_error;
        }

        make_inflate_huff_code_lit_len( &m_huffmanCode, lit_and_dist_huff.data(), LIT_LEN_ELEMS,
                                        lit_count.data(), code_list.data(), 0 );

        m_error = Error::NONE;
        return Error::NONE;
    }

    [[nodiscard]] bool
    isValid() const
    {
        return m_error == Error::NONE;
    }

private:
    static void
    write_huff_code( huff_code * const huff_code,
                     uint32_t    const code,
                     uint32_t    const length )
    {
        huff_code->code_and_length = code | ( length << 24U );
    }

public:
    /* Decodes the next symbol symbol in in_buffer using the huff code defined by
     * huff_code  and returns the value in next_lits and sym_count */
    [[nodiscard]] forceinline std::pair<uint32_t, uint32_t>
    decode( BitReader& bitReader ) const
    {
        static constexpr auto LARGE_SHORT_SYM_LEN = 25U;
        static constexpr auto LARGE_SHORT_SYM_MASK = ( 1U << LARGE_SHORT_SYM_LEN ) - 1U;
        static constexpr auto LARGE_LONG_SYM_LEN = 10U;
        static constexpr auto LARGE_LONG_SYM_MASK = ( 1U << LARGE_LONG_SYM_LEN ) - 1U;
        static constexpr auto LARGE_FLAG_BIT = 1U << 25U;
        static constexpr auto LARGE_SHORT_CODE_LEN_OFFSET = 28U;
        static constexpr auto LARGE_SYM_COUNT_OFFSET = 26U;
        static constexpr auto LARGE_SYM_COUNT_MASK = ( 1U << 2U ) - 1U;
        static constexpr auto LARGE_SHORT_MAX_LEN_OFFSET = 26U;
        static constexpr auto LARGE_LONG_CODE_LEN_OFFSET = 10U;
        static constexpr auto INVALID_SYMBOL = 0x1FFFU;

        auto next_bits = bitReader.peek<ISAL_DECODE_LONG_BITS /* 12 */>();

        /* next_sym is a possible symbol decoded from next_bits. If bit 15 is 0,
         * next_code is a symbol. Bits 9:0 represent the symbol, and bits 14:10
         * represent the length of that symbol's huffman code. If next_sym is not
         * a symbol, it provides a hint of where the large symbols containing
         * this code are located. Note the hint is at largest the location the
         * first actual symbol in the long code list.*/
        uint32_t next_sym = m_huffmanCode.short_code_lookup[next_bits];
        if ( LIKELY( ( next_sym & LARGE_FLAG_BIT ) == 0 ) ) [[likely]] {
            /* Return symbol found if next_code is a complete huffman code
             * and shift in buffer over by the length of the next_code */
            const auto bit_count = next_sym >> LARGE_SHORT_CODE_LEN_OFFSET;
            bitReader.seekAfterPeek( bit_count );

            if ( bit_count == 0 ) {
                next_sym = INVALID_SYMBOL;
            }

            return { next_sym & LARGE_SHORT_SYM_MASK,
                     ( next_sym >> LARGE_SYM_COUNT_OFFSET ) & LARGE_SYM_COUNT_MASK };
        }

        /* If a symbol is not found, do a lookup in the long code
         * list starting from the hint in next_sym */
        next_bits = bitReader.peek( next_sym >> LARGE_SHORT_MAX_LEN_OFFSET );
        next_sym = m_huffmanCode.long_code_lookup[( next_sym & LARGE_SHORT_SYM_MASK )
                                                  + ( next_bits >> ISAL_DECODE_LONG_BITS )];
        const auto bit_count = next_sym >> LARGE_LONG_CODE_LEN_OFFSET;
        bitReader.seekAfterPeek( bit_count );

        if ( bit_count == 0 ) {
            next_sym = INVALID_SYMBOL;
        }

        return { next_sym & LARGE_LONG_SYM_MASK, 1 };
    }

private:
    Error m_error{ Error::INVALID_HUFFMAN_CODE };
    inflate_huff_code_large m_huffmanCode;
};
}  // namespace rapidgzip
