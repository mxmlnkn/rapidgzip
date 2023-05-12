
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include <BitReader.hpp>
#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <huffman/HuffmanCodingDoubleLiteralCached.hpp>
#include <huffman/HuffmanCodingLinearSearch.hpp>
#include <huffman/HuffmanCodingReversedBitsCached.hpp>
#include <huffman/HuffmanCodingReversedBitsCachedCompressed.hpp>
#include <huffman/HuffmanCodingReversedCodesPerLength.hpp>
#include <huffman/HuffmanCodingSymbolsPerLength.hpp>


using namespace pragzip;


template<typename HuffmanCoding>
std::pair<double, uint16_t>
benchmarkHuffmanCoding( const std::vector<typename HuffmanCoding::BitCount>& codeLengths,
                        const BufferedFileReader::AlignedBuffer&             encoded )
{
    const auto t0 = now();

    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( encoded ) );
    HuffmanCoding coding;
    const auto errorCode = coding.initializeFromLengths( codeLengths );
    if ( errorCode != Error::NONE ) {
        throw std::invalid_argument( "Could not create HuffmanCoding from given lengths: " + toString( errorCode ) );
    }

    uint16_t sum = 0;
#if 1
    while ( true )
    {
        try {
            const auto symbol = coding.decode( bitReader );
            if ( !symbol ) {
                break;
            }
            sum += *symbol;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }
    }
    /**
     * 16 MiB test data:
     * HuffmanCodingLinearSearch                 took 1.40798  to decode, checksum: 35627
     * HuffmanCodingSymbolsPerLength             took 0.302659 to decode, checksum: 35627
     * HuffmanCodingReversedBitsCached           took 0.148155 to decode, checksum: 35627
     * HuffmanCodingReversedBitsCachedCompressed took 0.115742 to decode, checksum: 35627
     * HuffmanCodingReversedCodesPerLength       took 2.28024  to decode, checksum: 35627
     * HuffmanCodingDoubleLiteralCached          took 0.123982 to decode, checksum: 35627
     */
#else
    /**
     * HuffmanCodingLinearSearch                 took 1.4368   to decode, checksum: 35627 -> slower
     * HuffmanCodingSymbolsPerLength             took 0.323144 to decode, checksum: 35627 -> slower
     * HuffmanCodingReversedBitsCached           took 0.157012 to decode, checksum: 35627 -> slower
     * HuffmanCodingReversedBitsCachedCompressed took 0.143247 to decode, checksum: 35627 -> much slower
     * HuffmanCodingReversedCodesPerLength       took 1.88771  to decode, checksum: 35627 -> much faster
     * HuffmanCodingDoubleLiteralCached          took 0.158814 to decode, checksum: 35627 -> much slower
     *
     * In general, when exception are thrown rarely, i.e., basically for every longer loop break condition,
     * then exceptions are faster! Note that in this case the eof() call is quite expensive because
     * it is not a simple flag check but queries and compares the position to the size of the FileReader.
     * @see https://stackoverflow.com/a/16785259/2191065
     */
    while ( !bitReader.eof() )
    {
        const auto symbol = coding.decode( bitReader );
        if ( !symbol ) {
            break;
        }
        sum += *symbol;
    }
#endif

    const auto t1 = now();
    return { duration( t0, t1 ), sum };
}


template<typename HuffmanCode,
         uint8_t  MAX_CODE_LENGTH,
         typename Symbol,
         size_t   MAX_SYMBOL_COUNT>
void
benchmarkHuffmanCodings( const std::vector<uint8_t>&              codeLengths,
                         const BufferedFileReader::AlignedBuffer& encoded )
{
    struct Result
    {
        std::string name;
        double duration{ 0 };
        uint16_t sum{ 0 };
        std::string exception{};
    };
    std::vector<Result> results;

    /* Obiously very slow, especially for longer code lengths. It also has almost no sanity checks! */
    results.emplace_back( Result{ "Linear Search" } );
    try {
        using HuffmanCoding = HuffmanCodingLinearSearch<HuffmanCode, Symbol>;
        std::tie( results.back().duration, results.back().sum ) =
            benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded );
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    /* Very slow, especially for longer code lengths. */
    results.emplace_back( Result{ "Reversed Codes Per Length" } );
    try {
        using HuffmanCoding =
            HuffmanCodingReversedCodesPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        std::tie( results.back().duration, results.back().sum ) =
            benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded );
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    results.emplace_back( Result{ "Symbols Per Length" } );
    try {
        using HuffmanCoding = HuffmanCodingSymbolsPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        std::tie( results.back().duration, results.back().sum ) =
            benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded );
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    results.emplace_back( Result{ "Reversed Bits Cached" } );
    try {
        using HuffmanCoding = HuffmanCodingReversedBitsCached<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        std::tie( results.back().duration, results.back().sum ) =
            benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded );
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    /* Somtimes ~10% faster then "Double Literal Cached", especially for longer code lengths. */
    results.emplace_back( Result{ "Reversed Bits Cached Compressed" } );
    try {
        using HuffmanCoding =
            HuffmanCodingReversedBitsCachedCompressed<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        std::tie( results.back().duration, results.back().sum ) =
            benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded );
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    /* Often the fastest. */
    results.emplace_back( Result{ "Double Literal Cached" } );
    try {
        using HuffmanCoding = HuffmanCodingDoubleLiteralCached<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        std::tie( results.back().duration, results.back().sum ) =
            benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded );
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    const auto smallerDuration = [] ( const auto& a, const auto& b ) { return a.duration < b.duration; };
    const auto fastestResult = std::min_element( results.begin(), results.end(), smallerDuration );
    const auto slowestResult = std::max_element( results.begin(), results.end(), smallerDuration );
    for ( const auto& result : results ) {
        if ( result.exception.empty() ) {
            std::stringstream alignedDuration;
            alignedDuration << std::setw( 10 ) << result.duration;
            std::cout << "Took " << alignedDuration.str() << " s for "
                      << result.name;
            if ( result.duration == fastestResult->duration ) {
                std::cout << " (FASTEST)";
            }
            if ( result.duration == slowestResult->duration ) {
                std::cout << " (SLOWEST)";
            }
        } else {
            std::cout << "Exception thrown for " << result.name << ": " << result.exception;
        }
        std::cout << "\n";

        if ( result.sum != results.front().sum ) {
            std::cout << "Checksum " << result.sum << " differs from first decoder's checksum " << results.front().sum
                      << "!\n";
        }
    }
}


int main()
{
    /* Set it to 15 no matter the benchmark because we want to test the deflate szenario. */
    static constexpr auto MAX_CODE_LENGTH = 15;
    static constexpr auto MAX_SYMBOL_COUNT = 512;

    /* The "fun" thing with Huffman Coding is that are no wasted bits and therefore also no possible sanity checks.
     * And more interestingly, based on randomly distributed bits of data, a non-uniform symbol distribution can
     * be created using a Huffman tree representing the desired distribution. */
    BufferedFileReader::AlignedBuffer encoded( 16_Mi );
    for ( auto& c : encoded ) {
        c = static_cast<std::decay_t<decltype( c )> >( rand() % 256 );
    }

    {
        constexpr size_t MAX_LITERAL_OR_LENGTH_SYMBOLS = 286;
        std::vector<uint8_t> codeLengths( MAX_LITERAL_OR_LENGTH_SYMBOLS + 2, 8 );
        std::fill( codeLengths.begin() + 144, codeLengths.begin() + 256, 9 );
        std::fill( codeLengths.begin() + 256, codeLengths.begin() + 280, 7 );

        std::cout << "=== Fixed Huffman Coding (Code lengths 7,8,9 as is Common for ASCII) ===\n";

        benchmarkHuffmanCodings<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT>( codeLengths, encoded );
    }
    std::cout << "\n";

    for ( uint8_t bitLength = 1; bitLength <= 9; ++bitLength ) {
        std::vector<uint8_t> codeLengths( 1U << bitLength, bitLength );
        std::cout << "=== Equal-Sized Code Lengths (" << static_cast<int>( bitLength ) << "-bit) ===\n";

        benchmarkHuffmanCodings<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT>( codeLengths, encoded );
    }
    std::cout << "\n";

    for ( uint8_t longestCode = 2; longestCode <= 15; ++longestCode ) {
        std::vector<uint8_t> codeLengths( longestCode );
        for ( size_t i = 0; i < codeLengths.size(); ++i ) {
            codeLengths[i] = i + 1;
        }
        codeLengths.push_back( longestCode );

        std::cout << "=== All Code Lengths Appearing (1-" << static_cast<int>( longestCode ) << ") ===\n";

        benchmarkHuffmanCodings<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT>( codeLengths, encoded );
    }

    return 0;
}


/*
=== Equal-Sized Code Lengths (1-bit) ===
Took   0.810038 s for Linear Search
Took    0.79643 s for Reversed Codes Per Length
Took    0.38527 s for Symbols Per Length
Took   0.856254 s for Reversed Bits Cached (SLOWEST)
Took   0.538269 s for Reversed Bits Cached Compressed
Took   0.343892 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (2-bit) ===
Took    0.68265 s for Linear Search (SLOWEST)
Took   0.593397 s for Reversed Codes Per Length
Took   0.343956 s for Symbols Per Length
Took    0.44481 s for Reversed Bits Cached
Took   0.291244 s for Reversed Bits Cached Compressed
Took   0.194862 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (3-bit) ===
Took   0.576786 s for Linear Search (SLOWEST)
Took   0.511455 s for Reversed Codes Per Length
Took   0.347661 s for Symbols Per Length
Took    0.31227 s for Reversed Bits Cached
Took   0.211581 s for Reversed Bits Cached Compressed
Took   0.145525 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (4-bit) ===
Took   0.560461 s for Linear Search (SLOWEST)
Took   0.520016 s for Reversed Codes Per Length
Took   0.332594 s for Symbols Per Length
Took   0.241404 s for Reversed Bits Cached
Took    0.16906 s for Reversed Bits Cached Compressed
Took   0.121014 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (5-bit) ===
Took   0.579661 s for Linear Search
Took   0.647883 s for Reversed Codes Per Length (SLOWEST)
Took   0.331862 s for Symbols Per Length
Took   0.205235 s for Reversed Bits Cached
Took   0.146611 s for Reversed Bits Cached Compressed
Took    0.10696 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (6-bit) ===
Took   0.659404 s for Linear Search
Took   0.903315 s for Reversed Codes Per Length (SLOWEST)
Took   0.333728 s for Symbols Per Length
Took    0.17741 s for Reversed Bits Cached
Took   0.128402 s for Reversed Bits Cached Compressed
Took  0.0982536 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (7-bit) ===
Took   0.853871 s for Linear Search
Took    1.38795 s for Reversed Codes Per Length (SLOWEST)
Took   0.330009 s for Symbols Per Length
Took   0.158273 s for Reversed Bits Cached
Took   0.116837 s for Reversed Bits Cached Compressed
Took  0.0911961 s for Double Literal Cached (FASTEST)
=== Equal-Sized Code Lengths (8-bit) ===
Took    1.17068 s for Linear Search
Took    2.28946 s for Reversed Codes Per Length (SLOWEST)
Took   0.306933 s for Symbols Per Length
Took   0.143362 s for Reversed Bits Cached
Took   0.108542 s for Reversed Bits Cached Compressed (FASTEST)
Took   0.119244 s for Double Literal Cached
=== Equal-Sized Code Lengths (9-bit) ===
Took    1.78795 s for Linear Search
Took    3.93928 s for Reversed Codes Per Length (SLOWEST)
Took   0.329174 s for Symbols Per Length
Took   0.134136 s for Reversed Bits Cached
Took   0.103737 s for Reversed Bits Cached Compressed (FASTEST)
Took   0.112457 s for Double Literal Cached

=== Fixed Huffman Coding (Code lengths 7,8,9 as is Common for ASCII) ===
Took    3.42653 s for Linear Search (SLOWEST)
Took    2.13444 s for Reversed Codes Per Length
Took   0.372026 s for Symbols Per Length
Took   0.148632 s for Reversed Bits Cached
Took   0.112945 s for Reversed Bits Cached Compressed (FASTEST)
Took   0.124708 s for Double Literal Cached

=== All Code Lengths Appearing (1-2) ===
Took   0.842039 s for Linear Search (SLOWEST)
Took   0.792084 s for Reversed Codes Per Length
Took   0.640206 s for Symbols Per Length
Took   0.585409 s for Reversed Bits Cached
Took     0.3832 s for Reversed Bits Cached Compressed
Took   0.256252 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-3) ===
Took   0.897192 s for Linear Search (SLOWEST)
Took   0.774614 s for Reversed Codes Per Length
Took   0.725051 s for Symbols Per Length
Took   0.515345 s for Reversed Bits Cached
Took   0.334274 s for Reversed Bits Cached Compressed
Took   0.225243 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-4) ===
Took   0.921199 s for Linear Search (SLOWEST)
Took   0.761546 s for Reversed Codes Per Length
Took   0.761542 s for Symbols Per Length
Took   0.482102 s for Reversed Bits Cached
Took   0.320332 s for Reversed Bits Cached Compressed
Took   0.217611 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-5) ===
Took   0.946446 s for Linear Search (SLOWEST)
Took   0.752814 s for Reversed Codes Per Length
Took   0.773827 s for Symbols Per Length
Took    0.46975 s for Reversed Bits Cached
Took   0.308598 s for Reversed Bits Cached Compressed
Took   0.211035 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-6) ===
Took   0.993986 s for Linear Search (SLOWEST)
Took   0.749185 s for Reversed Codes Per Length
Took   0.783136 s for Symbols Per Length
Took   0.460773 s for Reversed Bits Cached
Took   0.306047 s for Reversed Bits Cached Compressed
Took   0.211094 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-7) ===
Took     1.0262 s for Linear Search (SLOWEST)
Took   0.743417 s for Reversed Codes Per Length
Took     0.7826 s for Symbols Per Length
Took    0.45987 s for Reversed Bits Cached
Took   0.302487 s for Reversed Bits Cached Compressed
Took    0.20824 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-8) ===
Took    1.03322 s for Linear Search (SLOWEST)
Took   0.745695 s for Reversed Codes Per Length
Took   0.788473 s for Symbols Per Length
Took   0.456154 s for Reversed Bits Cached
Took   0.303881 s for Reversed Bits Cached Compressed
Took   0.208436 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-9) ===
Took    1.05525 s for Linear Search (SLOWEST)
Took   0.746463 s for Reversed Codes Per Length
Took   0.790904 s for Symbols Per Length
Took   0.459929 s for Reversed Bits Cached
Took    0.30227 s for Reversed Bits Cached Compressed
Took   0.207971 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-10) ===
Took    1.10546 s for Linear Search (SLOWEST)
Took   0.745278 s for Reversed Codes Per Length
Took   0.789403 s for Symbols Per Length
Took   0.455847 s for Reversed Bits Cached
Took    0.30398 s for Reversed Bits Cached Compressed
Took   0.206102 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-11) ===
Took    1.09999 s for Linear Search (SLOWEST)
Took   0.746982 s for Reversed Codes Per Length
Took   0.785725 s for Symbols Per Length
Took   0.454616 s for Reversed Bits Cached
Took   0.303296 s for Reversed Bits Cached Compressed
Took   0.212632 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-12) ===
Took    1.11883 s for Linear Search (SLOWEST)
Took   0.744249 s for Reversed Codes Per Length
Took    0.79613 s for Symbols Per Length
Took    0.45834 s for Reversed Bits Cached
Took    0.30287 s for Reversed Bits Cached Compressed
Took   0.206616 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-13) ===
Took    1.14722 s for Linear Search (SLOWEST)
Took   0.747109 s for Reversed Codes Per Length
Took   0.791284 s for Symbols Per Length
Took   0.457262 s for Reversed Bits Cached
Took   0.303916 s for Reversed Bits Cached Compressed
Took   0.206534 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-14) ===
Took     1.1621 s for Linear Search (SLOWEST)
Took   0.745025 s for Reversed Codes Per Length
Took   0.794581 s for Symbols Per Length
Took   0.458304 s for Reversed Bits Cached
Took   0.301142 s for Reversed Bits Cached Compressed
Took   0.206986 s for Double Literal Cached (FASTEST)
=== All Code Lengths Appearing (1-15) ===
Took    1.19848 s for Linear Search (SLOWEST)
Took   0.744602 s for Reversed Codes Per Length
Took   0.787647 s for Symbols Per Length
Took   0.458113 s for Reversed Bits Cached
Took   0.302403 s for Reversed Bits Cached Compressed
Took   0.212021 s for Double Literal Cached (FASTEST)
*/
