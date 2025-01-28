/**
 * @file Executes benchmarks for varying gzip test files using the custom-written sequential deflate decompressor.
 *       This should yield a wide variety of timings that can be used to optimize the inflate hot-loop.
 */

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

#include <common.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <GzipReader.hpp>
#include <Statistics.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


[[nodiscard]] size_t
decompressWithRapidgzip( UniqueFileReader fileReader )
{
    using namespace rapidgzip;

    size_t totalDecodedBytes = 0;
    [[maybe_unused]] size_t blockCount = 0;

    GzipReader gzipReader( std::move( fileReader ) );
    std::vector<uint8_t> outputBuffer( 64_Mi );
    while ( true ) {
        const auto nBytesRead = gzipReader.read( -1,
                                                 reinterpret_cast<char*>( outputBuffer.data() ),
                                                 outputBuffer.size(),
                                                 StoppingPoint::END_OF_BLOCK_HEADER );
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        const auto currentPoint = gzipReader.currentPoint();
        if ( currentPoint == StoppingPoint::END_OF_BLOCK_HEADER ) {
            blockCount++;
        }
        totalDecodedBytes += nBytesRead;
    }

    return totalDecodedBytes;
}


void
printBandwidths( const std::vector<double>& durations,
                 size_t                     nBytesDecoded )
{
    std::cout << "    Runtime / s: " << Statistics<double>( durations ).formatAverageWithUncertainty( true ) << "\n";

    std::vector<double> decodedBandwidths( durations.size() );
    std::transform( durations.begin(), durations.end(), decodedBandwidths.begin(),
                    [nBytesDecoded] ( auto duration ) {
                        return static_cast<double>( nBytesDecoded ) / 1e6 / duration;
                    } );
    std::cout << "    Bandwidth on Decoded Data / (MB/s): "
              << Statistics<double>( decodedBandwidths ).formatAverageWithUncertainty( true ) << "\n";
};


enum class CompressionStrategy : int
{
    DEFAULT             = Z_DEFAULT_STRATEGY,
    FILTERED            = Z_FILTERED,
    RUN_LENGTH_ENCODING = Z_RLE,
    HUFFMAN_ONLY        = Z_HUFFMAN_ONLY,
    FIXED_HUFFMAN       = Z_FIXED,
};


[[nodiscard]] std::string_view
toString( const CompressionStrategy compressionStrategy )
{
    using namespace std::literals;

    switch ( compressionStrategy )
    {
    case CompressionStrategy::DEFAULT: return "Default"sv;
    case CompressionStrategy::FILTERED: return "Filtered"sv;
    case CompressionStrategy::RUN_LENGTH_ENCODING: return "Run-Length Encoding"sv;
    case CompressionStrategy::HUFFMAN_ONLY: return "Huffman Only"sv;
    case CompressionStrategy::FIXED_HUFFMAN: return "Fixed Huffman"sv;
    }
    return {};
}


[[nodiscard]] std::vector<std::byte>
compressWithZlib( const std::vector<std::byte>& toCompress,
                  const CompressionStrategy     compressionStrategy = CompressionStrategy::DEFAULT )
{
    std::vector<std::byte> output;
    output.reserve( toCompress.size() );

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = toCompress.size();
    stream.next_in = reinterpret_cast<Bytef*>( const_cast<std::byte*>( toCompress.data() ) );
    stream.avail_out = 0;
    stream.next_out = nullptr;

    /* > Add 16 to windowBits to write a simple gzip header and trailer around the
     * > compressed data instead of a zlib wrapper. */
    deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                  MAX_WBITS | 16, /* memLevel */ 8, static_cast<int>( compressionStrategy ) );

    auto status = Z_OK;
    constexpr auto CHUNK_SIZE = 1_Mi;
    while ( status == Z_OK ) {
        output.resize( output.size() + CHUNK_SIZE );
        stream.next_out = reinterpret_cast<Bytef*>( output.data() + output.size() - CHUNK_SIZE );
        stream.avail_out = CHUNK_SIZE;
        status = ::deflate( &stream, Z_FINISH );
    }

    deflateEnd( &stream );

    output.resize( stream.total_out );
    output.shrink_to_fit();

    return output;
}


void
benchmarkDecompression( const std::vector<std::byte>& dataToCompress,
                        const std::string_view        dataLabel,
                        const CompressionStrategy     compressionStrategy = CompressionStrategy::DEFAULT )
{
    const auto t0 = now();
    const auto fileContents = compressWithZlib( dataToCompress, compressionStrategy );
    const auto compressDuration = duration( t0 );
    const auto compressionRatio = static_cast<double>( dataToCompress.size() )
                                  / static_cast<double>( fileContents.size() );
    const auto compressionBandwidth = static_cast<double>( dataToCompress.size() ) / compressDuration;

    std::cout << "Compressed " << formatBytes( dataToCompress.size() )
              << " " << dataLabel << " to " << formatBytes( fileContents.size() )
              << ", compression ratio: " << compressionRatio
              << ", compression strategy: " << toString( compressionStrategy )
              << ", compression bandwidth: " << compressionBandwidth / 1e6 << " MB/s\n";

    const auto [size, durations] = benchmarkFunction<3>(
        [&fileContents] () {
            return decompressWithRapidgzip( std::make_unique<BufferViewFileReader>( fileContents ) );
        } );
    printBandwidths( durations, size );
    std::cout << "\n";
}


void
benchmarkDecompressionOfZeros()
{
    benchmarkDecompression( std::vector<std::byte>( 128_Mi, std::byte( 0 ) ), "zeros" );
}


[[nodiscard]] std::vector<std::byte>
createRandomData( uint64_t size )
{
    std::mt19937_64 randomEngine;
    std::vector<std::byte> result( size );
    for ( auto& x : result ) {
        x = static_cast<std::byte>( randomEngine() );
    }
    return result;
}


[[nodiscard]] std::vector<std::byte>
createRandomData( uint64_t                      size,
                  const std::vector<std::byte>& allowedSymbols )
{
    std::mt19937_64 randomEngine;
    std::vector<std::byte> result( size );
    for ( auto& x : result ) {
        x = allowedSymbols[static_cast<size_t>( randomEngine() ) % allowedSymbols.size()];
    }
    return result;
}


void
benchmarkDecompressionOfNonCompressible()
{
    const auto t0 = now();
    const auto randomData = createRandomData( 128_Mi );
    const auto creationDuration = duration( t0 );
    std::cout << "Created " << formatBytes( randomData.size() ) << " random data in " << creationDuration << " s\n";
    benchmarkDecompression( randomData, "random data", CompressionStrategy::HUFFMAN_ONLY );
}


void
benchmarkDecompressionOfRandomBase64( const CompressionStrategy compressionStrategy )
{
    std::vector<std::byte> allowedSymbols( BASE64_SYMBOLS.size() );
    std::transform( BASE64_SYMBOLS.begin(), BASE64_SYMBOLS.end(), allowedSymbols.begin(),
                    [] ( const auto c ) { return static_cast<std::byte>( c ); } );

    const auto t0 = now();
    const auto randomData = createRandomData( 128_Mi, allowedSymbols );
    const auto creationDuration = duration( t0 );
    std::cout << "Created " << formatBytes( randomData.size() )
              << " random base64 data in " << creationDuration << " s\n";
    benchmarkDecompression( randomData, "random base64 data", compressionStrategy );
}


void
benchmarkDecompressionOfRandomBackreferences()
{
    const auto t0 = now();

    std::mt19937_64 randomEngine;

    constexpr auto INITIAL_RANDOM_SIZE = rapidgzip::deflate::MAX_WINDOW_SIZE;
    auto randomData = createRandomData( INITIAL_RANDOM_SIZE );
    randomData.resize( 128_Mi );

    for ( size_t i = INITIAL_RANDOM_SIZE; i < randomData.size(); ) {
        const auto distance = randomEngine() % INITIAL_RANDOM_SIZE;
        const auto remainingSize = randomData.size() - i;
        const auto length = std::min<size_t>( randomEngine() % 256, remainingSize );
        if ( ( length < 4 ) || ( length > distance ) ) {
            continue;
        }

        std::memcpy( randomData.data() + i, randomData.data() + ( i - distance ), length );
        i += length;
    }

    const auto creationDuration = duration( t0 );
    std::cout << "Created " << formatBytes( randomData.size() )
              << " data with random backreferences in " << creationDuration << " s\n";
    benchmarkDecompression( randomData, "data with random backreferences" );
}


int
main()
{
    benchmarkDecompressionOfZeros();
    benchmarkDecompressionOfNonCompressible();
    benchmarkDecompressionOfRandomBase64( CompressionStrategy::HUFFMAN_ONLY );
    benchmarkDecompressionOfRandomBase64( CompressionStrategy::FIXED_HUFFMAN );
    benchmarkDecompressionOfRandomBackreferences();

    return 0;
}


/*
cmake --build . -- benchmarkInflate && src/benchmarks/benchmarkInflate

Compressed 128 MiB zeros to 127 KiB 430 B, compression ratio: 1028.66, compression strategy: Default, compression bandwidth: 277.491 MB/s
    Runtime / s: 0.06918 <= 0.06929 +- 0.00021 <= 0.06953
    Bandwidth on Decoded Data / (MB/s): 1930 <= 1937 +- 6 <= 1940

Created 128 MiB random data in 0.220642 s
Compressed 128 MiB random data to 128 MiB 40 KiB 23 B, compression ratio: 0.999695, compression strategy: Huffman Only, compression bandwidth: 89.9106 MB/s
    Runtime / s: 0.0792 <= 0.0801 +- 0.0012 <= 0.0814
    Bandwidth on Decoded Data / (MB/s): 1648 <= 1677 +- 25 <= 1694

Created 128 MiB random base64 data in 1.45205 s
Compressed 128 MiB random base64 data to 96 MiB 850 KiB 494 B, compression ratio: 1.3219, compression strategy: Huffman Only, compression bandwidth: 86.7805 MB/s
    Runtime / s: 0.6330 <= 0.6348 +- 0.0019 <= 0.6368
    Bandwidth on Decoded Data / (MB/s): 210.8 <= 211.4 +- 0.6 <= 212.0

Created 128 MiB random base64 data in 1.44896 s
Compressed 128 MiB random base64 data to 127 MiB 119 KiB 122 B, compression ratio: 1.00695, compression strategy: Fixed Huffman, compression bandwidth: 30.0892 MB/s
    Runtime / s: 0.7622 <= 0.7634 +- 0.0018 <= 0.7654
    Bandwidth on Decoded Data / (MB/s): 175.4 <= 175.8 +- 0.4 <= 176.1

Created 128 MiB data with random backreferences in 0.0487171 s
Compressed 128 MiB data with random backreferences to 5 MiB 736 KiB 719 B, compression ratio: 22.3798, compression strategy: Default, compression bandwidth: 82.8308 MB/s
    Runtime / s: 0.1458 <= 0.1466 +- 0.0006 <= 0.1470
    Bandwidth on Decoded Data / (MB/s): 913 <= 916 +- 4 <= 920
*/
