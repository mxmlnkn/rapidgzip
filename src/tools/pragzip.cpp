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

#include <sys/poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cxxopts.hpp>

#include <blockfinder/Bgzf.hpp>
#include <common.hpp>
#include <filereader/Standard.hpp>
#include <pragzip.hpp>
#include <ParallelGzipReader.hpp>
#include <Statistics.hpp>


[[nodiscard]] bool
stdinHasInput()
{
    pollfd fds;  // NOLINT
    fds.fd = STDIN_FILENO;
    fds.events = POLLIN;
    return poll(&fds, 1, /* timeout in ms */ 0 ) == 1;
}


[[nodiscard]] bool
stdoutIsDevNull()
{
    struct stat devNull;  // NOLINT
    struct stat stdOut;  // NOLINT
    return ( fstat( STDOUT_FILENO, &stdOut ) == 0 ) &&
           ( stat( "/dev/null", &devNull ) == 0 ) &&
           S_ISCHR( stdOut.st_mode ) &&  // NOLINT
           ( devNull.st_dev == stdOut.st_dev ) &&
           ( devNull.st_ino == stdOut.st_ino );
}


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
    << "be omitted if neither --test nor -l nor -L nor --force are given.\n"
    << "\n"
    << "Examples:\n"
    << "\n"
    << "Decompress a file:\n"
    << "  pragzip -d file.gz\n"
    << "\n"
    << "Decompress a file in parallel:\n"
    << "  pragzip -d -P file.gz\n"
    << "\n"
    << "Find and list the bzip2 block offsets to be used for another tool:\n"
    << "  pragzip -l blockoffsets.dat -- file.gz\n"
    << "\n"
    << "List block offsets in both the compressed as well as the decompressed data during downloading:\n"
    << "  wget -O- 'ftp://example.com/file.gz' | tee saved-file.gz | pragzip -L blockoffsets.dat > /dev/null\n"
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
cli( int argc, char** argv )
{
    /**
     * @note For some reason implicit values do not mix very well with positional parameters!
     *       Parameters given to arguments with implicit values will be matched by the positional argument instead!
     */
    cxxopts::Options options( "pragzip",
                              "A bzip2 decompressor tool based on the indexed_bzip2 backend from ratarmount" );
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
        ( "t,test"       , "Test compressed file integrity." )
        ( "analyze"      , "Print output about the internal file format structure like the block types." )

        ( "p,block-finder-parallelism",
          "This only has an effect if the parallel decoder is used with the -P option. "
          "If an optional integer >= 1 is given, then that is the number of threads to use for finding bzip2 blocks. "
          "If 0 is given, then the parallelism will be determiend automatically.",
          cxxopts::value<unsigned int>()->default_value( "1" ) )

        ( "P,decoder-parallelism",
          "Use the parallel decoder. "
          "If an optional integer >= 1 is given, then that is the number of decoder threads to use. "
          "Note that there might be further threads being started with non-decoding work. "
          "If 0 is given, then the parallelism will be determiend automatically.",
          cxxopts::value<unsigned int>()->default_value( "1" ) );

    options.add_options( "Output" )
        ( "h,help"   , "Print this help mesage." )
        ( "q,quiet"  , "Suppress noncritical error messages." )
        ( "v,verbose", "Be verbose. A second -v (or shorthand -vv) gives even more verbosity." )
        ( "V,version", "Display software version." )
        ( "l,list-compressed-offsets",
          "List only the bzip2 block offsets given in bits one per line to the specified output file. "
          "If no file is given, it will print to stdout or to stderr if the decoded data is already written to stdout. "
          "Specifying '-' as file path, will write to stdout.",
          cxxopts::value<std::string>() )
        ( "L,list-offsets",
          "List bzip2 block offsets in bits and also the corresponding offsets in the decoded data at the beginning "
          "of each block in bytes as a comma separated pair per line '<encoded bits>,<decoded bytes>'. "
          "Specifying '-' as file path, will write to stdout.",
          cxxopts::value<std::string>() );

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
        for ( auto const* const path : { "input", "output", "list-compressed-offsets", "list-offsets" } ) {
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
        std::cout << "pragzip, CLI to the indexed and seekable bzip2 decoding library indexed-bzip2 version 1.2.0.\n";
        return 0;
    }

    /* Parse input file specifications. */

    if ( parsedArgs.count( "input" ) > 1 ) {
        std::cerr << "One or none bzip2 filename to decompress must be specified!\n";
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

    auto inputFile = inputFilePath.empty()
                     ? std::make_unique<StandardFileReader>( STDIN_FILENO )
                     : std::make_unique<StandardFileReader>( inputFilePath );

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
                            || ( parsedArgs.count( "list-offsets" ) > 0 )
                            || force;

    const auto offsetsFilePath = getFilePath( parsedArgs, "list-offsets" );
    if ( !offsetsFilePath.empty() && fileExists( offsetsFilePath ) && !force ) {
        std::cerr << "Output file for offsets'" << offsetsFilePath
                  << "' for offsets already exists! Use --force to overwrite.\n";
        return 1;
    }

    const auto compressedOffsetsFilePath = getFilePath( parsedArgs, "list-compressed-offsets" );
    if ( !compressedOffsetsFilePath.empty() && fileExists( compressedOffsetsFilePath ) && !force ) {
        std::cerr << "Output file compressed offsets '" << compressedOffsetsFilePath
                  << "' for offsets already exists! Use --force to overwrite.\n";
        return 1;
    }

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

        auto outputFileDescriptor = STDOUT_FILENO;
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

    /** @todo Implement actual output format for lists @see ibzip2.
     * For Bgzf, also get the uncompressed sizes from the gzip footers! */
    if ( parsedArgs.count( "list-compressed-offsets" ) > 0 ) {
        if ( verbose ) {
            std::cerr << "Find block offsets\n";
        }

        std::cerr << "Bgzf block offsets:\n";
        pragzip::blockfinder::Bgzf blockFinder( std::make_unique<StandardFileReader>( inputFilePath ) );
        for ( auto offset = blockFinder.find();
              offset != std::numeric_limits<size_t>::max();
              offset = blockFinder.find() )
        {
            std::cerr << ( offset / 8 ) << " B " << ( offset % 8 ) << " b\n";
        }

        return 1;
    }

    std::cerr << "No suitable arguments were given. Please refer to the help!\n\n";

    printHelp( options );

    return 1;
}


int
main( int argc, char** argv )
{
    try
    {
        return cli( argc, argv );
    }
    catch ( const std::exception& exception )
    {
        std::cerr << "Caught exception:\n" << exception.what() << "\n";
        return 1;
    }

    return 1;
}
