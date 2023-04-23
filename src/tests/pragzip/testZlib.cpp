#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <common.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Shared.hpp>
#include <TestHelpers.hpp>
#include <zlib.hpp>

using namespace pragzip;


void
testMultiGzipStream()
{
    const std::vector<std::byte> dataToCompress = { std::byte( 'A' ) };
    auto compressedData = compressWithZlib( dataToCompress );

    /* Duplicate gzip stream. */
    const auto singleGzipStreamSize = compressedData.size();
    compressedData.resize( 2 * singleGzipStreamSize );
    std::copy( compressedData.begin(), compressedData.begin() + singleGzipStreamSize,
               compressedData.begin() + singleGzipStreamSize );
    const std::vector<std::byte> expectedResult = { std::byte( 'A' ), std::byte( 'A' ) };

    auto fileReader = std::make_unique<SharedFileReader>(
        std::make_unique<BufferViewFileReader>( compressedData ) );
    pragzip::BitReader bitReader( std::move( fileReader ) );
    bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
    ZlibDeflateWrapper deflateWrapper( std::move( bitReader ) );

    std::vector<std::byte> decompressedResult( expectedResult.size(), std::byte( 1 ) );
    const auto decompressedSize = deflateWrapper.read(
        reinterpret_cast<uint8_t*>( decompressedResult.data() ), decompressedResult.size() );
    REQUIRE_EQUAL( decompressedSize, 2U );
    REQUIRE_EQUAL( decompressedSize, expectedResult.size() );
    REQUIRE( decompressedResult == expectedResult );
}


int
main()
{
    testMultiGzipStream();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
