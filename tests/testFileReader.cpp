#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "common.hpp"
#include "FileReader.hpp"
#include "FileReader.hpp"
#include "SharedFileReader.hpp"


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


int main()
{
    const std::string tmpFileName = "testFileReader-test-file.tmp";
    const auto writtenData = fillFile( tmpFileName );
    std::cerr << "Written data: " << writtenData << "\n";
    std::cerr << "Wrote " << writtenData.size() << " B\n";

    const auto file1 = throwingOpen( tmpFileName, "rb" );
    std::string readData( 2 * writtenData.size(), '\0' );

    /* Read everything */
    {
        const auto nBytesRead = std::fread( readData.data(), 1 /* element size */, readData.size(), file1.get() );
        assert( nBytesRead == writtenData.size() );
        assert( std::equal( writtenData.begin(), writtenData.end(), readData.begin() ) );
        assert( std::feof( file1.get() ) != 0 );
    }

    /* Read second time after seeking to start without clearing EOF */
    {
        readData.assign( 2 * writtenData.size(), '\0' );
        std::fseek( file1.get(), 0, SEEK_SET );
        const auto nBytesRead = std::fread( readData.data(), 1 /* element size */, readData.size(), file1.get() );
        assert( nBytesRead == writtenData.size() );
        assert( std::equal( writtenData.begin(), writtenData.end(), readData.begin() ) );
    }

    return 0;
}
