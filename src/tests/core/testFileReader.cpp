#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/FileReader.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


std::string
fillFile( const std::string& fileName )
{
    std::stringstream result;

    std::ofstream outFile( fileName );
    for ( int i = 0; i < 100; ++i ) {
        outFile << i;
        result << i;
    }

    return result.str();
}


void
testUniqueFilePointer( const std::string& tmpFileName,
                       const std::string& tmpFileContents )
{
    const auto file1 = throwingOpen( tmpFileName, "rb" );
    std::string readData( 2 * tmpFileContents.size(), '\0' );

    /* Read everything */
    {
        readData.assign( 2 * tmpFileContents.size(), '\0' );
        const auto nBytesRead = std::fread( readData.data(), 1 /* element size */, readData.size(), file1.get() );
        REQUIRE( nBytesRead == tmpFileContents.size() );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );
        REQUIRE( std::feof( file1.get() ) != 0 );
    }

    /* Read second time after seeking to start without clearing EOF */
    {
        readData.assign( 2 * tmpFileContents.size(), '\0' );
        std::fseek( file1.get(), 0, SEEK_SET );
        const auto nBytesRead = std::fread( readData.data(), 1 /* element size */, readData.size(), file1.get() );
        REQUIRE( nBytesRead == tmpFileContents.size() );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );
    }
}


void
testFileReader( const std::string& tmpFileContents,
                FileReader* const  fileReader )
{
    std::string readData( 2 * tmpFileContents.size(), '\0' );

    /* Read everything */
    {
        readData.assign( 2 * tmpFileContents.size(), '\0' );
        const auto nBytesRead = fileReader->read( readData.data(), readData.size() );
        REQUIRE( nBytesRead == std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );
        REQUIRE( fileReader->eof() != 0 );
        REQUIRE( fileReader->tell() == tmpFileContents.size() );
    }

    /* Read second time after seeking to start without clearing EOF */
    {
        readData.assign( 2 * tmpFileContents.size(), '\0' );

        fileReader->seekTo( 0 );
        REQUIRE( fileReader->tell() == 0 );

        const auto nBytesRead = fileReader->read( readData.data(), readData.size() );
        REQUIRE( nBytesRead == std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );

        REQUIRE( fileReader->tell() == tmpFileContents.size() );
    }

    /* Read from middle. */
    {
        readData.assign( 1, '\0' );
        fileReader->seek( -10, SEEK_END );
        REQUIRE( fileReader->tell() == tmpFileContents.size() - 10 );

        const auto nBytesRead = fileReader->read( readData.data(), readData.size() );
        REQUIRE( nBytesRead == std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.end() - 10, tmpFileContents.end() - 10 + 1, readData.begin() ) );
        REQUIRE( fileReader->tell() == tmpFileContents.size() - 9 );
    }
    {
        readData.assign( 1, '\0' );
        const auto nBytesRead = fileReader->read( readData.data(), readData.size() );
        REQUIRE( nBytesRead == std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.end() - 9, tmpFileContents.end() - 9 + 1, readData.begin() ) );
        REQUIRE( fileReader->tell() == tmpFileContents.size() - 8 );
    }

    /* Read multiple from middle. */
    {
        REQUIRE( fileReader->tell() == tmpFileContents.size() - 8 );
        fileReader->seek( -10, SEEK_CUR );
        REQUIRE( fileReader->tell() == tmpFileContents.size() - 18 );

        readData.assign( 5, '\0' );
        const auto nBytesRead = fileReader->read( readData.data(), readData.size() );
        REQUIRE( nBytesRead == std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.end() - 18, tmpFileContents.end() - 18 + 5, readData.begin() ) );
    }
}


int
main()
{
    const std::string tmpFileName = "testFileReader-test-file.tmp";
    const auto tmpFileContents = fillFile( tmpFileName );
    std::cerr << "Written data: " << tmpFileContents << "\n";
    std::cerr << "Wrote " << tmpFileContents.size() << " B\n";

    testUniqueFilePointer( tmpFileName, tmpFileContents );

    const auto standardFileReader = std::make_unique<StandardFileReader>( tmpFileName );
    testFileReader( tmpFileContents, standardFileReader.get() );

    const auto bufferedFileReader =
        std::make_unique<BufferedFileReader>( std::make_unique<StandardFileReader>( tmpFileName ) );
    testFileReader( tmpFileContents, bufferedFileReader.get() );

    const auto memoryFileReader =
        std::make_unique<BufferedFileReader>( std::vector<char>( tmpFileContents.begin(), tmpFileContents.end() ) );
    testFileReader( tmpFileContents, memoryFileReader.get() );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
