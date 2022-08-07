#include <cassert>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <cxxopts.hpp>

#include <blockfinder/Bgzf.hpp>
#include <common.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <pragzip.hpp>
#include <ParallelGzipReader.hpp>
#include <Statistics.hpp>


[[nodiscard]] pragzip::Error
analyze( std::unique_ptr<FileReader> inputFile )
{
    using namespace pragzip;
    using Block = pragzip::deflate::Block</* CRC32 */ false>;

    pragzip::BitReader bitReader{ std::move( inputFile ) };

    std::optional<gzip::Header> gzipHeader;
    Block block;

    size_t totalBytesRead = 0;
    size_t streamBytesRead = 0;

    size_t totalBlockCount = 0;
    size_t streamBlockCount = 0;
    size_t streamCount = 0;

    size_t headerOffset = 0;

    std::vector<size_t> encodedBlockSizes;
    std::vector<size_t> decodedBlockSizes;

    while ( true ) {
        if ( !gzipHeader ) {
            headerOffset = bitReader.tell();

            const auto [header, error] = gzip::readHeader( bitReader );
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while trying to read gzip header!\n";
                return error;
            }

            gzipHeader = header;
            block.setInitialWindow();

            /* Analysis Information */

            streamCount += 1;
            streamBlockCount = 0;
            streamBytesRead = 0;

            std::cout << "Found gzip header:\n";
            std::cout << "    Gzip Stream Count   : " << streamCount << "\n";
            std::cout << "    Compressed Offset   : " << formatBits( headerOffset ) << "\n";
            std::cout << "    Uncompressed Offset : " << totalBytesRead << " B\n";
            if ( header.fileName ) {
                std::cout << "    File Name           : " << *header.fileName << "\n";
            }
            std::cout << "    Modification Time   : " << header.modificationTime << "\n";
            std::cout << "    OS                  : " << gzip::getOperatingSystemName( header.operatingSystem ) << "\n";
            std::cout << "    Flags               : " << gzip::getExtraFlagsDescription( header.extraFlags ) << "\n";
            if ( header.comment ) {
                std::cout << "    Comment             : " << *header.comment << "\n";
            }
            if ( header.extra ) {
                std::stringstream extraString;
                extraString << header.extra->size() << " B: ";
                for ( const auto value : *header.extra ) {
                    if ( static_cast<bool>( std::isprint( value ) ) ) {
                        extraString << value;
                    } else {
                        std::stringstream hexCode;
                        hexCode << std::hex << std::setw( 2 ) << std::setfill( '0' ) << static_cast<int>( value );
                        extraString << '\\' << 'x' << hexCode.str();
                    }
                }
                std::cout << "    Extra               : " << extraString.str() << "\n";
            }
            if ( header.crc16 ) {
                std::stringstream crc16String;
                crc16String << std::hex << std::setw( 16 ) << std::setfill( '0' ) << *header.crc16;
                std::cout << "    CRC16               : 0x" << crc16String.str() << "\n";
            }
            std::cout << "\n";
        }

        const auto blockOffset = bitReader.tell();
        {
            const auto error = block.readHeader( bitReader );
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while trying to read deflate header!\n";
                return error;
            }
        }

        size_t uncompressedBlockSize = 0;
        size_t uncompressedBlockOffset = totalBytesRead;
        size_t uncompressedBlockOffsetInStream = streamBytesRead;

        while ( !block.eob() ) {
            const auto [buffers, error] = block.read( bitReader, std::numeric_limits<size_t>::max() );
            const auto nBytesRead = buffers.size();
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while decompressing deflate block.\n";
            }
            totalBytesRead += nBytesRead;
            streamBytesRead += nBytesRead;

            /* No output necessary for analysis. */

            uncompressedBlockSize += nBytesRead;
        }

        /* Analysis Information */

        encodedBlockSizes.emplace_back( bitReader.tell() - blockOffset );
        decodedBlockSizes.emplace_back( uncompressedBlockSize );

        streamBlockCount += 1;
        totalBlockCount += 1;

        std::cout << "Found deflate block:\n";
        std::cout << "    Final Block             : " << ( block.isLastBlock() ? "True" : "False" ) << "\n";
        std::cout << "    Compression Type        : " << Block::toString( block.compressionType() ) << "\n";
        std::cout << "    File Statistics:\n";
        std::cout << "        Total Block Count   : " << totalBlockCount << "\n";
        std::cout << "        Compressed Offset   : " << formatBits( blockOffset ) << "\n";
        std::cout << "        Uncompressed Offset : " << uncompressedBlockOffset << " B\n";
        std::cout << "    Gzip Stream Statistics:\n";
        std::cout << "        Block Count         : " << streamBlockCount << "\n";
        std::cout << "        Compressed Offset   : " << formatBits( blockOffset - headerOffset ) << "\n";
        std::cout << "        Uncompressed Offset : " << uncompressedBlockOffsetInStream << " B\n";
        std::cout << "    Compressed Size         : " << formatBits( bitReader.tell() - blockOffset ) << "\n";
        std::cout << "    Uncompressed Size       : " << uncompressedBlockSize << " B\n";
        std::cout << "\n";


        if ( block.isLastBlock() ) {
            const auto footer = gzip::readFooter( bitReader );

            if ( static_cast<uint32_t>( streamBytesRead ) != footer.uncompressedSize ) {
                std::stringstream message;
                message << "Mismatching size (" << static_cast<uint32_t>( streamBytesRead )
                        << " <-> footer: " << footer.uncompressedSize << ") for gzip stream!";
                throw std::runtime_error( std::move( message ).str() );
            }

            if ( ( block.crc32() != 0 ) && ( block.crc32() != footer.crc32 ) ) {
                std::stringstream message;
                message << "Mismatching CRC32 (0x" << std::hex << block.crc32() << " <-> stored: 0x" << footer.crc32
                        << ") for gzip stream!";
            }

            if ( block.crc32() != 0 ) {
                std::stringstream message;
                message << "Validated CRC32 0x" << std::hex << block.crc32() << " for gzip stream!\n";
                std::cerr << message.str();
            }

            gzipHeader = {};
        }

        if ( bitReader.eof() ) {
            std::cout << "Bit reader EOF reached at " << formatBits( bitReader.tell() ) << "\n";
            break;
        }
    }

    std::cout << "\n== Encoded Block Size Distribution ==\n\n";
    std::cout << Histogram<size_t>{ encodedBlockSizes, 8, "bits" }.plot();

    std::cout << "\n== Decoded Block Size Distribution ==\n\n";
    std::cout << Histogram<size_t>{ decodedBlockSizes, 8, "Bytes" }.plot();

    return Error::NONE;
}


void
printHelp( const cxxopts::Options& options )
{
    std::cout
    << options.help()
    << "\n"
    << "If no file names are given, pragzip decompresses from standard input to standard output.\n"
    << "If the output is discarded by piping to /dev/null, then the actual decoding step might\n"
    << "be omitted if neither -l nor -L nor --force are given.\n"
    << "\n"
    << "Examples:\n"
    << "\n"
    << "Decompress a file:\n"
    << "  pragzip -d file.gz\n"
    << "\n"
    << "Decompress a file in parallel:\n"
    << "  pragzip -d -P 0 file.gz\n"
    << "\n"
    << "List information about all gzip streams and deflate blocks:\n"
    << "  pragzip --analyze file.gz\n"
    << std::endl;
}


std::string
getFilePath( cxxopts::ParseResult const& parsedArgs,
             std::string          const& argument )
{
    if ( parsedArgs.count( argument ) > 0 ) {
        auto path = parsedArgs[argument].as<std::string>();
        if ( path != "-" ) {
            return path;
        }
    }
    return {};
}


int
pragzipCLI( int argc, char** argv )
{
    /**
     * @note For some reason implicit values do not mix very well with positional parameters!
     *       Parameters given to arguments with implicit values will be matched by the positional argument instead!
     */
    cxxopts::Options options( "pragzip",
                              "A gzip decompressor tool based on the pragzip backend from ratarmount" );
    options.add_options( "Decompression" )
        ( "c,stdout"     , "Output to standard output. This is the default, when reading from standard input." )
        ( "d,decompress" , "Force decompression. Only for compatibility. No compression supported anyways." )
        ( "f,force"      , "Force overwriting existing output files. "
                           "Also forces decompression even when piped to /dev/null." )
        ( "i,input"      , "Input file. If none is given, data is read from standard input.",
          cxxopts::value<std::string>() )
        ( "o,output"     ,
          "Output file. If none is given, use the input file name with '.gz' stripped or '<input file>.out'. "
          "If no input is read from standard input and not output file is given, then will write to standard output.",
          cxxopts::value<std::string>() )
        ( "k,keep"       , "Keep (do not delete) input file. Only for compatibility. "
                           "This tool will not delete anything automatically!" )
        ( "analyze"      , "Print output about the internal file format structure like the block types." )

        ( "P,decoder-parallelism",
          "Use the parallel decoder. "
          "If an optional integer >= 1 is given, then that is the number of decoder threads to use. "
          "Note that there might be further threads being started with non-decoding work. "
          "If 0 is given, then the parallelism will be determiend automatically.",
          cxxopts::value<unsigned int>()->default_value( "0" ) );

    options.add_options( "Output" )
        ( "h,help"   , "Print this help mesage." )
        ( "q,quiet"  , "Suppress noncritical error messages." )
        ( "v,verbose", "Be verbose. A second -v (or shorthand -vv) gives even more verbosity." )
        ( "V,version", "Display software version." );

    options.parse_positional( { "input" } );

    /* cxxopts allows to specifiy arguments multiple times. But if the argument type is not a vector, then only
     * the last value will be kept! Therefore, do not check against this usage and simply use that value.
     * Arguments may only be queried with as if they have (default) values. */

    const auto parsedArgs = options.parse( argc, argv );

    const auto force   = parsedArgs["force"  ].as<bool>();
    const auto quiet   = parsedArgs["quiet"  ].as<bool>();
    const auto verbose = parsedArgs["verbose"].as<bool>();

    const auto getParallelism = [] ( const auto p ) { return p > 0 ? p : std::thread::hardware_concurrency(); };
    const auto decoderParallelism = getParallelism( parsedArgs["decoder-parallelism"].as<unsigned int>() );

    if ( verbose ) {
        for ( auto const* const path : { "input", "output" } ) {
            std::string value = "<none>";
            try {
                value = parsedArgs[path].as<std::string>();
            } catch ( ... ) {}
            std::cerr << "file path for " << path << ": " << value << "\n";
        }
    }

    /* Check against simple commands like help and version. */

    if ( parsedArgs.count( "help" ) > 0 ) {
        printHelp( options );
        return 0;
    }

    if ( parsedArgs.count( "version" ) > 0 ) {
        std::cout << "pragzip, CLI to the parallelized, indexed, and seekable gzip decoding library pragzip "
                  << "version 0.2.1.\n";
        return 0;
    }

    /* Parse input file specifications. */

    if ( parsedArgs.count( "input" ) > 1 ) {
        std::cerr << "One or none gzip filename to decompress must be specified!\n";
        return 1;
    }

    if ( !stdinHasInput() && ( parsedArgs.count( "input" ) != 1 ) ) {
        std::cerr << "Either stdin must have input, e.g., by piping to it, or an input file must be specified!\n";
        return 1;
    }

    std::string inputFilePath; /* Can be empty. Then, read from STDIN. */
    if ( parsedArgs.count( "input" ) == 1 ) {
        inputFilePath = parsedArgs["input"].as<std::string>();
    }

    auto inputFile = openFileOrStdin( inputFilePath );

    /* Check if analysis is requested. */

    if ( parsedArgs.count( "analyze" ) > 0 ) {
        return analyze( std::move( inputFile ) ) == pragzip::Error::NONE ? 0 : 1;
    }

    /* Parse output file specifications. */

    auto outputFilePath = getFilePath( parsedArgs, "output" );
    if ( ( parsedArgs.count( "stdout" ) == 0 ) && !inputFilePath.empty() ) {
        const std::string& suffix = ".gz";
        if ( endsWith( inputFilePath, suffix, /* case sensitive */ false ) ) {
            outputFilePath = std::string( inputFilePath.begin(),
                                          inputFilePath.end()
                                          - static_cast<std::string::difference_type>( suffix.size() ) );
        } else {
            outputFilePath = inputFilePath + ".out";
            if ( !quiet ) {
                std::cerr << "Could not deduce output file name. Will write to '" << outputFilePath << "'\n";
            }
        }
    }

    if ( ( outputFilePath != "/dev/null" ) && fileExists( outputFilePath ) && !force ) {
        std::cerr << "Output file '" << outputFilePath << "' already exists! Use --force to overwrite.\n";
        return 1;
    }

    /* Parse other arguments. */

    const auto decompress = ( ( parsedArgs.count( "decompress" ) > 0 )
                              && ( ( outputFilePath.empty() && !stdoutIsDevNull() )
                                   || ( !outputFilePath.empty() && ( outputFilePath != "/dev/null" ) ) ) )
                            || force;

    /* Actually do things as requested. */

    if ( decompress ) {
        if ( verbose ) {
            std::cerr << "Decompress " << ( inputFilePath.empty() ? "<stdin>" : inputFilePath.c_str() )
                      << " -> " << ( outputFilePath.empty() ? "<stdout>" : outputFilePath.c_str() ) << "\n";
        }

        if ( !inputFile ) {
            std::cerr << "Could not open input file: " << inputFilePath << "!\n";
            return 1;
        }

    #ifdef _MSC_VER
        auto outputFileDescriptor = _fileno( stdout );
        if ( outputFilePath.empty() ) {
            _setmode( outputFileDescriptor, _O_BINARY );
        }
    #else
        auto outputFileDescriptor = ::fileno( stdout );
    #endif

        unique_file_ptr outputFile;
        if ( !outputFilePath.empty() ) {
            outputFile = make_unique_file_ptr( outputFilePath.c_str(), "wb" );
            if ( !outputFile ) {
                std::cerr << "Could not open output file: " << outputFilePath << " for writing!\n";
                return 1;
            }
            outputFileDescriptor = ::fileno( outputFile.get() );
        }

        const auto t0 = now();

        size_t totalBytesRead{ 0 };
        if ( decoderParallelism == 1 ) {
            pragzip::GzipReader</* CRC32 */ false> gzipReader{ std::move( inputFile ) };
            totalBytesRead = gzipReader.read( outputFileDescriptor );
        } else {
            ParallelGzipReader reader( std::move( inputFile ), decoderParallelism );
            totalBytesRead = reader.read( outputFileDescriptor );
        }

        const auto t1 = now();
        std::cerr << "Decompressed in total " << totalBytesRead << " B in " << duration( t0, t1 ) << " s -> "
                  << static_cast<double>( totalBytesRead ) / 1e6 / duration( t0, t1 ) << " MB/s\n";


        /** @todo need to write out block offsets here, see ibzip2 */

        return 0;
    }

    std::cerr << "No suitable arguments were given. Please refer to the help!\n\n";

    printHelp( options );

    return 1;
}


#ifndef WITH_PYTHON_SUPPORT
int
main( int argc, char** argv )
{
    try
    {
        return pragzipCLI( argc, argv );
    }
    catch ( const std::exception& exception )
    {
        std::cerr << "Caught exception:\n" << exception.what() << "\n";
        return 1;
    }

    return 1;
}
#endif
