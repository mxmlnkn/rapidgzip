#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <BitManipulation.hpp>
#include <common.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


const std::vector<std::string_view> ENCODE_TABLE = {
    "",
    "A",
    "B",
    "AA",
    "BA",
    "AB",
    "BB",
    "AAA",
    "BAA",
    "ABA",
    "BBA",
    "AAB",
    "BAB",
    "ABB",
    "BBB",
    "AAAA",
    "BAAA",
    "ABAA",
    "BBAA",
    "AABA",
    "BABA",
    "ABBA",
    "BBBA",
    "AAAB",
    "BAAB",
    "ABAB",
    "BBAB",
    "AABB",
    "BABB",
    "ABBB",
    "BBBB",
    "AAAAA",
    "BAAAA",
    "ABAAA",
    "BBAAA",
    "AABAA",
    "BABAA",
    "ABBAA",
    "BBBAA",
    "AAABA",
    "BAABA",
    "ABABA",
    "BBABA",
    "AABBA",
    "BABBA",
    "ABBBA",
    "BBBBA",
    "AAAAB",
    "BAAAB",
    "ABAAB",
    "BBAAB",
    "AABAB",
    "BABAB",
    "ABBAB",
    "BBBAB",
    "AAABB",
    "BAABB",
    "ABABB",
    "BBABB",
    "AABBB",
    "BABBB",
    "ABBBB",
    "BBBBB",
    "AAAAAA",
};


class RunDecoder
{
public:
    void
    merge( char c )
    {
        switch ( c )
        {
        case 'A':
        case 'B':
            m_bits |= ( c - 'A' ) << m_bitCount;
            ++m_bitCount;
            break;
        default:
            throw std::logic_error( "Only A or B allowed!" );
        }
    }

    [[nodiscard]] uint32_t
    value() const
    {
        //return ( ( 1U << m_bitCount ) | reverseBits( m_bits, m_bitCount ) ) - 1U;
        return ( ( 1U << m_bitCount ) | m_bits ) - 1U;
    }

private:
    uint8_t m_bitCount{ 0 };
    uint32_t m_bits{ 0 };
};


void
testAB()
{
    for ( size_t length = 1; length < ENCODE_TABLE.size(); ++length ) {
        const auto& sequence = ENCODE_TABLE[length];
        RunDecoder decoder;
        for ( const auto c : sequence ) {
            decoder.merge( c );
        }
        std::cerr << "Decode: " << sequence << ", which should be " << length << ", and got: "
                  << decoder.value() << "\n";
        REQUIRE( decoder.value() == length );
    }
}


int
main()
{
    testAB();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
