#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>

#include <core/common.hpp>
#include <core/Statistics.hpp>
#include <filereader/Buffered.hpp>
#include <huffman/HuffmanCodingLinearSearch.hpp>
#include <huffman/HuffmanCodingShortBitsCached.hpp>
#include <huffman/HuffmanCodingSymbolsPerLength.hpp>
#include <rapidgzip/gzip/definitions.hpp>
#include <rapidgzip/huffman/HuffmanCodingDoubleLiteralCached.hpp>
#include <rapidgzip/huffman/HuffmanCodingReversedBitsCached.hpp>
#include <rapidgzip/huffman/HuffmanCodingReversedBitsCachedCompressed.hpp>
#include <rapidgzip/huffman/HuffmanCodingReversedBitsCachedSeparateLengths.hpp>
#include <rapidgzip/huffman/HuffmanCodingReversedCodesPerLength.hpp>


using namespace rapidgzip;


struct Result
{
    std::string name;
    struct {
        Statistics<double> decode;
        Statistics<double> construction;
        Statistics<double> total;
    } durations;
    size_t encodedSizeInBits{ 0 };
    size_t decodedSizeInBytes{ 0 };
    std::string exception{};
};


template<typename HuffmanCoding>
[[nodiscard]] Result
benchmarkHuffmanCoding( const std::vector<typename HuffmanCoding::BitCount>& codeLengths,
                        const BufferedFileReader::AlignedBuffer&             encoded )
{
    gzip::BitReader bitReader( std::make_unique<BufferedFileReader>( encoded ) );

    Result result;
    result.encodedSizeInBits = bitReader.size().value();

    for ( size_t i = 0; i < 100; ++i ) {
        bitReader.seek( 0 );

        HuffmanCoding coding;
        const auto t0 = now();
        const auto errorCode = coding.initializeFromLengths( codeLengths );
        if ( errorCode != Error::NONE ) {
            throw std::invalid_argument( "Could not create HuffmanCoding from given lengths: " +
                                         toString( errorCode ) );
        }
        const auto t1 = now();

        size_t count{ 0 };
    #if 1
        while ( true )
        {
            try {
                const auto symbol = coding.decode( bitReader );
                if ( !symbol ) {
                    break;
                }
                ++count;
            } catch ( const gzip::BitReader::EndOfFileReached& ) {
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
            ++count;
        }
    #endif

        const auto t2 = now();
        result.durations.construction.merge( duration( t0, t1 ) );
        result.durations.decode.merge( duration( t1, t2 ) );
        result.durations.total.merge( duration( t0, t2 ) );
        result.decodedSizeInBytes = count;
    }

    return result;
}


template<typename HuffmanCode,
         uint8_t  MAX_CODE_LENGTH,
         typename Symbol,
         size_t   MAX_SYMBOL_COUNT>
std::vector<Result>
benchmarkHuffmanCodings( const std::vector<uint8_t>&              codeLengths,
                         const BufferedFileReader::AlignedBuffer& encoded )
{
    std::vector<Result> results;

    /* Obviously very slow, especially for longer code lengths. It also has almost no sanity checks! */
    try {
        using HuffmanCoding = HuffmanCodingLinearSearch<HuffmanCode, Symbol>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Linear Search";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    /* Very slow, especially for longer code lengths. */
    try {
        using HuffmanCoding =
            HuffmanCodingReversedCodesPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Reversed Codes Per Length";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    try {
        using HuffmanCoding = HuffmanCodingSymbolsPerLength<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Symbols Per Length";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    try {
        using HuffmanCoding = HuffmanCodingReversedBitsCached<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Reversed Bits Cached";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    try {
        using HuffmanCoding =
            HuffmanCodingReversedBitsCachedCompressed<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Reversed Bits Cached Compressed";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    try {
        using HuffmanCoding =
            HuffmanCodingReversedBitsCachedSeparateLengths<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Reversed Bits Cached Separate Lengths";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    try {
        using HuffmanCoding = HuffmanCodingShortBitsCached<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT,
                                                           /* LUT_BITS_COUNT */ 10, /* REVERSE_BITS */ true>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Reversed Bits Short Cached";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    /* Often the fastest. */
    try {
        using HuffmanCoding = HuffmanCodingDoubleLiteralCached<HuffmanCode, MAX_CODE_LENGTH, Symbol, MAX_SYMBOL_COUNT>;
        results.emplace_back( benchmarkHuffmanCoding<HuffmanCoding>( codeLengths, encoded ) );
        results.back().name = "Double Literal Cached";
    } catch ( const std::exception& e ) {
        results.back().exception = e.what();
    }

    const auto smallerDuration =
        [] ( const auto& a, const auto& b ) { return a.durations.total.average() < b.durations.total.average(); };
    const auto fastestResult = std::min_element( results.begin(), results.end(), smallerDuration );
    const auto slowestResult = std::max_element( results.begin(), results.end(), smallerDuration );
    for ( const auto& result : results ) {
        if ( result.exception.empty() ) {
            std::stringstream alignedDuration;
            const auto duration = result.durations.total.average();
            alignedDuration << std::setw( 10 ) << duration;
            std::cout << "Took " << alignedDuration.str() << " s for "
                      << result.name;
            if ( duration == fastestResult->durations.total.average() ) {
                std::cout << " (FASTEST)";
            }
            if ( duration == slowestResult->durations.total.average() ) {
                std::cout << " (SLOWEST)";
            }
        } else {
            std::cout << "Exception thrown for " << result.name << ": " << result.exception;
        }
        std::cout << "\n";

        if ( result.decodedSizeInBytes != results.front().decodedSizeInBytes ) {
            std::cout << "Decoded size " << result.decodedSizeInBytes << " B differs from first decoder's checksum "
                      << results.front().decodedSizeInBytes << "!\n";
        }
    }

    return results;
}


void
benchmarkHuffmanCodingsWithData( size_t encodedSize )
{
    std::ofstream outputFile{ "benchmarkHuffmanCoding-" + std::to_string( encodedSize / 1024 ) + "KiB.dat" };
    outputFile << "Implementation;Code Length Distribution;Minimum Length;Maximum Length;Runtime Average;"
                  "Runtime StdDev;Construction Time Average;Construction Time StdDev;Decode Time Average;"
                  "Decode Time StdDev\n";

    /* Set it to 15 no matter the benchmark because we want to test the deflate scenario. */
    static constexpr auto MAX_CODE_LENGTH = 15;
    static constexpr auto MAX_SYMBOL_COUNT = 512;

    /* The "fun" thing with Huffman Coding is that are no wasted bits and therefore also no possible sanity checks.
     * And more interestingly, based on randomly distributed bits of data, a non-uniform symbol distribution can
     * be created using a Huffman tree representing the desired distribution.
     * @note Wait! This means that I can guess the compression ratio from the Huffman Tree (disregarding LZ77)!
     *       Even LZ77 compression ratio may be cased from the non-literal symbols. They require some reading of
     *       additional bits, but the general reference length, off by a factor of 2, can be guessed without those. */
    BufferedFileReader::AlignedBuffer encoded( encodedSize );
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

    const auto printValue =
        [&outputFile] ( const Statistics<double>& value )
        {
            outputFile << ';' << value.average() << ';' << value.standardDeviation();
        };

    for ( uint8_t bitLength = 1; bitLength <= 9; ++bitLength ) {
        std::vector<uint8_t> codeLengths( 1U << bitLength, bitLength );
        std::cout << "=== Equal-Sized Code Lengths (" << static_cast<int>( bitLength ) << "-bit) ===\n";

        const auto results =
            benchmarkHuffmanCodings<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT>( codeLengths, encoded );
        for ( const auto& result : results ) {
            outputFile << result.name << ";Equal-Sized Code Lengths;"
                       << static_cast<int>( bitLength ) << ";" << static_cast<int>( bitLength );
            printValue( result.durations.total );
            printValue( result.durations.construction );
            printValue( result.durations.decode );
            outputFile << std::endl;
        }
    }
    std::cout << "\n";

    for ( uint8_t longestCode = 2; longestCode <= 15; ++longestCode ) {
        std::vector<uint8_t> codeLengths( longestCode );
        for ( size_t i = 0; i < codeLengths.size(); ++i ) {
            codeLengths[i] = i + 1;
        }
        codeLengths.push_back( longestCode );

        std::cout << "=== All Code Lengths Appearing (1-" << static_cast<int>( longestCode ) << ") ===\n";

        const auto results =
            benchmarkHuffmanCodings<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_SYMBOL_COUNT>( codeLengths, encoded );
        for ( const auto& result : results ) {
            outputFile << result.name << ";All Code Lengths Appearing;" << 1 << ';' << static_cast<int>( longestCode );
            printValue( result.durations.total );
            printValue( result.durations.construction );
            printValue( result.durations.decode );
            outputFile << std::endl;
        }
    }
}


int
main()
{
    for ( const auto size : { 4_Ki, 32_Ki, 128_Ki } ) {
        benchmarkHuffmanCodingsWithData( size );
    }
    return 0;
}
