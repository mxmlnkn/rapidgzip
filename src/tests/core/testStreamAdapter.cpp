#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <common.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/FileReader.hpp>
#include <filereader/StreamAdapter.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


void
testFileStream( const std::string& tmpFileContents,
                std::istream&      fileStream )
{
    std::string readData( 2 * tmpFileContents.size(), '\0' );

    /* Read everything without triggering EOF */
    {
        readData.assign( tmpFileContents.size(), '\0' );
        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );
        REQUIRE( !fileStream.eof() );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), tmpFileContents.size() );
        fileStream.seekg( 0 );
        REQUIRE_EQUAL( fileStream.tellg(), 0 );
    }

    /* Read everything */
    {
        readData.assign( 2 * tmpFileContents.size(), '\0' );
        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );
        REQUIRE( fileStream.eof() );
        /* In contrast to FileReader::tell, FileReaderStream::tellg returns -1 on eof! */
        REQUIRE_EQUAL( fileStream.tellg(), -1 );
    }

    /* Read second time after seeking to start */
    {
        readData.assign( 2 * tmpFileContents.size(), '\0' );

        fileStream.clear();  // Unset EOF state. seekg does not do it automatically unlike FileReader::seek
        fileStream.seekg( 0 );
        REQUIRE_EQUAL( fileStream.tellg(), 0 );

        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.begin(), tmpFileContents.end(), readData.begin() ) );

        REQUIRE( fileStream.eof() );
        /* In contrast to FileReader::tell, FileReaderStream::tellg returns -1 on eof! */
        REQUIRE_EQUAL( fileStream.tellg(), -1 );
    }

    const auto middleToEnd = tmpFileContents.size() / 2;
    const auto middle = tmpFileContents.size() - tmpFileContents.size() / 2;

    /* Read from middle. */
    {
        readData.assign( 1, '\0' );
        fileStream.clear();  // Unset EOF state. seekg does not do it automatically unlike FileReader::seek
        fileStream.seekg( -static_cast<long long int>( middleToEnd ), std::ios_base::end );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), middle );

        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, size_t( 1 ) );
        REQUIRE( std::equal( tmpFileContents.end() - middleToEnd,
                             tmpFileContents.end() - middleToEnd + 1,
                             readData.begin() ) );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), middle + 1 );
    }
    {
        readData.assign( 1, '\0' );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), middle + 1 );
        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, size_t( 1 ) );
        REQUIRE_EQUAL( tmpFileContents[middle + 1], readData[0] );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), middle + 2 );
    }

    /* Read multiple from middle. */
    {
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), tmpFileContents.size() - middleToEnd + 2 );
        fileStream.seekg( -2, std::ios_base::cur );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), tmpFileContents.size() - middleToEnd );

        const auto sizeToRead = tmpFileContents.size() / 4;
        readData.assign( sizeToRead, '\0' );
        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.end() - middleToEnd,
                             tmpFileContents.end() - middleToEnd + sizeToRead,
                             readData.begin() ) );
    }

    /* Large relative seek. */
    {
        fileStream.seekg( -fileStream.tellg() + 1, std::ios_base::cur );
        REQUIRE_EQUAL( static_cast<size_t>( fileStream.tellg() ), size_t( 1 ) );

        readData.assign( 10'000, '\0' );
        fileStream.read( readData.data(), readData.size() );
        const auto nBytesRead = static_cast<size_t>( fileStream.gcount() );
        REQUIRE_EQUAL( nBytesRead, std::min( tmpFileContents.size(), readData.size() ) );
        REQUIRE( std::equal( tmpFileContents.begin() + 1,
                             tmpFileContents.begin() + 1 + readData.size(),
                             readData.begin() ) );
    }
}


int
main()
{
    std::stringstream result;
    for ( int i = 0; i < 10'000; ++i ) {
        result << i;
    }
    const auto tmpFileContentsString = result.str();

    result.seekg( 0 );
    testFileStream( tmpFileContentsString, result );

    std::vector<char> tmpFileContents{ tmpFileContentsString.begin(), tmpFileContentsString.end() };

    std::cerr << "Wrote " << formatBytes( tmpFileContents.size() ) << "\n";

    FileReaderStream fileStream( std::make_unique<BufferViewFileReader>( tmpFileContents ) );
    testFileStream( tmpFileContentsString, fileStream );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
