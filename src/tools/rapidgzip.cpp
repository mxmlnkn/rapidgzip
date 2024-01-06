#include <cstdio>
#include <iostream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cxxopts.hpp>

#include <AffinityHelpers.hpp>
#include <common.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <GzipAnalyzer.hpp>
#include <rapidgzip.hpp>
#include <Statistics.hpp>

#include "CLIHelper.hpp"
#include "licenses.hpp"


struct Arguments
{
    unsigned int decoderParallelism{ 0 };
    size_t chunkSize{ 4_Mi };
    std::string indexLoadPath;
    std::string indexSavePath;
    bool verbose{ false };
    bool crc32Enabled{ true };
};


void
printRapidgzipHelp( const cxxopts::Options& options )
{
    std::cout
    << options.help()
    << "\n"
    << "If no file names are given, rapidgzip decompresses from standard input to standard output.\n"
    << "If the output is discarded by piping to /dev/null, then the actual decoding step might\n"
    << "be omitted if neither -l nor -L nor --force are given.\n"
    << "\n"
    << "Examples:\n"
    << "\n"
    << "Decompress a file:\n"
    << "  rapidgzip -d file.gz\n"
    << "\n"
    << "Decompress a file in parallel:\n"
    << "  rapidgzip -d -P 0 file.gz\n"
    << "\n"
    << "List information about all gzip streams and deflate blocks:\n"
    << "  rapidgzip --analyze file.gz\n"
    << std::endl;
}


template<typename Reader>
void
printIndexAnalytics( const Reader& reader )
{
    const auto offsets = reader->blockOffsets();
    if ( offsets.size() <= 1 ) {
        return;
    }

    Statistics<double> encodedOffsetSpacings;
    Statistics<double> decodedOffsetSpacings;
    for ( auto it = offsets.begin(), nit = std::next( offsets.begin() ); nit != offsets.end(); ++it, ++nit ) {
        const auto& [encodedOffset, decodedOffset] = *it;
        const auto& [nextEncodedOffset, nextDecodedOffset] = *nit;
        if ( nextEncodedOffset - encodedOffset > 0 ) {
            encodedOffsetSpacings.merge( static_cast<double>( nextEncodedOffset - encodedOffset ) / CHAR_BIT / 1e6 );
            decodedOffsetSpacings.merge( static_cast<double>( nextDecodedOffset - decodedOffset ) / 1e6 );
        }
    }

    std::cerr
        << "[Seekpoints Index]\n"
        << "    Encoded offset spacings: ( min: " << encodedOffsetSpacings.min << ", "
        << encodedOffsetSpacings.formatAverageWithUncertainty()
        << ", max: " << encodedOffsetSpacings.max << " ) MB\n"
        << "    Decoded offset spacings: ( min: " << decodedOffsetSpacings.min << ", "
        << decodedOffsetSpacings.formatAverageWithUncertainty()
        << ", max: " << decodedOffsetSpacings.max << " ) MB\n";
}


template<typename Reader,
         typename WriteFunctor>
size_t
decompressParallel( const Arguments&    args,
                    const Reader&       reader,
                    const WriteFunctor& writeFunctor )
{
    reader->setShowProfileOnDestruction( args.verbose );
    reader->setCRC32Enabled( args.crc32Enabled );
    reader->setKeepIndex( !args.indexSavePath.empty() || !args.indexLoadPath.empty() );

    if ( !args.indexLoadPath.empty() ) {
        reader->importIndex( std::make_unique<StandardFileReader>( args.indexLoadPath ) );

        if ( args.verbose && ( !args.indexSavePath.empty() || !args.indexLoadPath.empty() ) ) {
            printIndexAnalytics( reader );
        }
    }

    const auto totalBytesRead = reader->read( writeFunctor );

    if ( !args.indexSavePath.empty() ) {
        const auto file = throwingOpen( args.indexSavePath, "wb" );
        const auto checkedWrite =
            [&file] ( const void* buffer, size_t size )
            {
                if ( std::fwrite( buffer, 1, size, file.get() ) != size ) {
                    throw std::runtime_error( "Failed to write data to index!" );
                }
            };

        reader->exportIndex( checkedWrite );
    }

    if ( args.verbose && args.indexLoadPath.empty() && !args.indexSavePath.empty() ) {
        printIndexAnalytics( reader );
    }

    return totalBytesRead;
}


/**
 * Dispatch to the appropriate ParallelGzipReader template arguments based on @p verbose.
 */
template<typename ChunkData,
         typename WriteFunctor = std::function<void ( const std::shared_ptr<ChunkData>&, size_t, size_t )> >
size_t
decompressParallel( const Arguments&    args,
                    UniqueFileReader    inputFile,
                    const WriteFunctor& writeFunctor )
{
    if ( args.verbose ) {
        using Reader = rapidgzip::ParallelGzipReader<ChunkData, /* enable statistics */ true>;
        auto reader = std::make_unique<Reader>( std::move( inputFile ), args.decoderParallelism, args.chunkSize );
        return decompressParallel( args, std::move( reader ), writeFunctor );
    } else {
        using Reader = rapidgzip::ParallelGzipReader<ChunkData, /* enable statistics */ false>;
        auto reader = std::make_unique<Reader>( std::move( inputFile ), args.decoderParallelism, args.chunkSize );
        return decompressParallel( args, std::move( reader ), writeFunctor );
    }
}


int
rapidgzipCLI( int                  argc,
              char const * const * argv )
{
    /* Cleaned, checked, and typed arguments. */
    Arguments args;

    /**
     * @note For some reason implicit values do not mix very well with positional parameters!
     *       Parameters given to arguments with implicit values will be matched by the positional argument instead!
     */
    cxxopts::Options options( "rapidgzip",
                              "A gzip decompressor tool based on the rapidgzip backend from ratarmount" );
    options.add_options( "Decompression Options" )
        ( "c,stdout"     , "Output to standard output. This is the default, when reading from standard input." )
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
        ( "P,decoder-parallelism",
          "Use the parallel decoder. "
          "If an optional integer >= 1 is given, then that is the number of decoder threads to use. "
          "Note that there might be further threads being started with non-decoding work. "
          "If 0 is given, then the parallelism will be determined automatically.",
          cxxopts::value<unsigned int>()->default_value( "0" ) );

    options.add_options( "Advanced" )
        ( "chunk-size", "The chunk size decoded by the parallel workers in KiB.",
          cxxopts::value<unsigned int>()->default_value( "4096" ) )
        ( "verify", "Verify CRC32 checksum. Will slow down decompression and there are already some implicit "
                    "and explicit checks like whether the end of the file could be reached and whether the stream "
                    "size is correct. ",
          cxxopts::value( args.crc32Enabled )->implicit_value( "true" ) )
        ( "no-verify", "Do not verify CRC32 checksum. Might speed up decompression and there are already some implicit "
                       "and explicit checks like whether the end of the file could be reached and whether the stream "
                       "size is correct.",
          cxxopts::value( args.crc32Enabled )->implicit_value( "false" ) )
        ( "io-read-method", "Option to force a certain I/O method for reading. By default, pread will be used "
                            "when possible. Possible values: pread, sequential, locked-read",
          cxxopts::value<std::string>()->default_value( "pread" ) );

    options.add_options( "Output Options" )
        ( "h,help"   , "Print this help message." )
        ( "q,quiet"  , "Suppress noncritical error messages." )
        ( "v,verbose", "Print debug output and profiling statistics." )
        ( "V,version", "Display software version." )
        ( "oss-attributions", "Display open-source software licenses." );

    /* These options are offered because just piping to other tools can already bottleneck everything! */
    options.add_options( "Actions" )
        ( "d,decompress" , "Force decompression. Only for compatibility. No compression supported anyways." )
        ( "import-index" , "Uses an existing gzip index.", cxxopts::value<std::string>() )
        ( "export-index" , "Write out a gzip index file.", cxxopts::value<std::string>() )
        ( "count"        , "Prints the decompressed size." )
        ( "l,count-lines", "Prints the number of newline characters in the decompressed data." )
        ( "analyze"      , "Print output about the internal file format structure like the block types." );

    options.parse_positional( { "input" } );

    /* cxxopts allows to specify arguments multiple times. But if the argument type is not a vector, then only
     * the last value will be kept! Therefore, do not check against this usage and simply use that value.
     * Arguments may only be queried with as if they have (default) values. */

    const auto parsedArgs = options.parse( argc, argv );

    const auto force = parsedArgs["force"].as<bool>();
    const auto quiet = parsedArgs["quiet"].as<bool>();
    args.verbose = parsedArgs["verbose"].as<bool>();

    const auto getParallelism = [] ( const auto p ) { return p > 0 ? p : availableCores(); };
    args.decoderParallelism = getParallelism( parsedArgs["decoder-parallelism"].as<unsigned int>() );

    if ( args.verbose ) {
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
        printRapidgzipHelp( options );
        return 0;
    }

    if ( parsedArgs.count( "version" ) > 0 ) {
        std::cout << "rapidgzip, CLI to the parallelized, indexed, and seekable gzip decoding library rapidgzip "
                  << "version 0.11.2.\n";
        return 0;
    }

    if ( parsedArgs.count( "oss-attributions" ) > 0 ) {
        std::cout << licenses::CXXOPTS << "\n"
        #ifdef WITH_ISAL
                  << licenses::ISAL << "\n"
        #endif
        #ifdef WITH_RPMALLOC
                  << licenses::RPMALLOC << "\n"
        #endif
                  << licenses::ZLIB;
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

    std::string inputFilePath;  /* Can be empty. Then, read from STDIN. */
    if ( parsedArgs.count( "input" ) == 1 ) {
        inputFilePath = parsedArgs["input"].as<std::string>();
        if ( !inputFilePath.empty() && !fileExists( inputFilePath ) ) {
            std::cerr << "Input file could not be found! Specified path: " << inputFilePath << "\n";
            return 1;
        }
    }

    auto inputFile = openFileOrStdin( inputFilePath );
    const auto ioReadMethod = parsedArgs["io-read-method"].as<std::string>();
    if ( ioReadMethod == "sequential" ) {
        inputFile = std::make_unique<SinglePassFileReader>( std::move( inputFile ) );
    } else if ( ( ioReadMethod == "locked-read" ) || ( ioReadMethod == "pread" ) ) {
        auto sharedFile = ensureSharedFileReader( std::move( inputFile ) );
        sharedFile->setUsePread( ioReadMethod == "pread" );
        inputFile = std::move( sharedFile );
    }

    /* Check if analysis is requested. */

    if ( parsedArgs.count( "analyze" ) > 0 ) {
        return rapidgzip::deflate::analyze( std::move( inputFile ), args.verbose ) == rapidgzip::Error::NONE ? 0 : 1;
    }

    /* Parse action arguments. */

    const auto countBytes = parsedArgs.count( "count" ) > 0;
    const auto countLines = parsedArgs.count( "count-lines" ) > 0;
    const auto decompress = parsedArgs.count( "decompress" ) > 0;

    /* Parse output file specifications. */

    auto outputFilePath = getFilePath( parsedArgs, "output" );
    /* Automatically determine output file path if none has been given and not writing to stdout. */
    if ( ( parsedArgs.count( "stdout" ) == 0 ) && outputFilePath.empty() && !inputFilePath.empty() ) {
        const std::string& suffix = ".gz";
        if ( endsWith( inputFilePath, suffix, /* case sensitive */ false ) ) {
            outputFilePath = std::string( inputFilePath.begin(),
                                          inputFilePath.end()
                                          - static_cast<std::string::difference_type>( suffix.size() ) );
        } else {
            outputFilePath = inputFilePath + ".out";
            if ( !quiet && decompress ) {
                std::cerr << "[Warning] Could not deduce output file name. Will write to '" << outputFilePath << "'\n";
            }
        }
    }

    /* Parse other arguments. */

    if ( decompress && ( outputFilePath != "/dev/null" ) && fileExists( outputFilePath ) && !force ) {
        std::cerr << "Output file '" << outputFilePath << "' already exists! Use --force to overwrite.\n";
        return 1;
    }

    args.indexLoadPath = parsedArgs.count( "import-index" ) > 0
                         ? parsedArgs["import-index"].as<std::string>()
                         : std::string();
    args.indexSavePath = parsedArgs.count( "export-index" ) > 0
                         ? parsedArgs["export-index"].as<std::string>()
                         : std::string();
    if ( !args.indexLoadPath.empty() && !args.indexSavePath.empty() ) {
        std::cerr << "[Warning] Importing and exporting an index makes limited sense.\n";
    }
    if ( ( !args.indexLoadPath.empty() || !args.indexSavePath.empty() ) && ( args.decoderParallelism == 1 ) ) {
        std::cerr << "[Warning] The index only has an effect for parallel decoding.\n";
    }
    if ( !args.indexLoadPath.empty() && !fileExists( args.indexLoadPath ) ) {
        std::cerr << "The index to import was not found!\n";
        return 1;
    }

    /* Actually do things as requested. */

    if ( decompress || countBytes || countLines || !args.indexSavePath.empty() ) {
        if ( decompress && args.verbose ) {
            std::cerr << "Decompress " << ( inputFilePath.empty() ? "<stdin>" : inputFilePath.c_str() )
                      << " -> " << ( outputFilePath.empty() ? "<stdout>" : outputFilePath.c_str() ) << "\n";
        }

        if ( !inputFile ) {
            std::cerr << "Could not open input file: " << inputFilePath << "!\n";
            return 1;
        }

        std::unique_ptr<OutputFile> outputFile;
        if ( decompress ) {
            outputFile = std::make_unique<OutputFile>( outputFilePath );
        }
        const auto outputFileDescriptor = outputFile ? outputFile->fd() : -1;

        uint64_t newlineCount{ 0 };

        const auto t0 = now();

        const auto writeAndCount =
            [outputFileDescriptor, countLines, &newlineCount]
            ( const std::shared_ptr<rapidgzip::ChunkData>& chunkData,
              size_t const                               offsetInBlock,
              size_t const                               dataToWriteSize )
            {
                writeAll( chunkData, outputFileDescriptor, offsetInBlock, dataToWriteSize );
                if ( countLines ) {
                    using rapidgzip::deflate::DecodedData;
                    for ( auto it = DecodedData::Iterator( *chunkData, offsetInBlock, dataToWriteSize );
                          static_cast<bool>( it ); ++it )
                    {
                        const auto& [buffer, size] = *it;
                        newlineCount += countNewlines( { reinterpret_cast<const char*>( buffer ), size } );
                    }
                }
            };

        args.chunkSize = parsedArgs["chunk-size"].as<unsigned int>() * 1_Ki;

        size_t totalBytesRead{ 0 };
        if ( ( outputFileDescriptor == -1 ) && args.indexSavePath.empty() && countBytes && !countLines
             && !args.crc32Enabled )
        {
            /* Need to do nothing with the chunks because decompressParallel returns the decompressed size.
             * Note that we use rapidgzip::ChunkDataCounter to speed up decompression. Therefore an index
             * will not be created and there also will be no checksum verification! */
            totalBytesRead = decompressParallel<rapidgzip::ChunkDataCounter>(
                args, std::move( inputFile ), /* do nothing */ {} );
        } else {
            std::function<void( const std::shared_ptr<rapidgzip::ChunkData>, size_t, size_t )> writeFunctor;
            /* Else, do nothing. An empty functor will lead to decompression to be skipped if the index is finalized! */
            if ( ( outputFileDescriptor != -1 ) || countLines ) {
                writeFunctor = writeAndCount;
            }
            totalBytesRead = decompressParallel<rapidgzip::ChunkData>( args, std::move( inputFile ), writeFunctor );
        }

        const auto writeToStdErr = outputFile && outputFile->writingToStdout();
        if ( outputFile ) {
            outputFile->truncate( totalBytesRead );
            outputFile.reset();  // Close the file here to include it in the time measurement.
        }

        const auto t1 = now();
        if ( args.verbose ) {
            std::cerr << "Decompressed in total " << totalBytesRead << " B in " << duration( t0, t1 ) << " s -> "
                      << static_cast<double>( totalBytesRead ) / 1e6 / duration( t0, t1 ) << " MB/s\n";
        }

        auto& out = writeToStdErr ? std::cerr : std::cout;
        if ( countBytes != countLines ) {
            out << ( countBytes ? totalBytesRead : newlineCount );
        } else if ( countBytes && countLines ) {
            out << "Size: " << totalBytesRead << "\n";
            out << "Lines: " << newlineCount << "\n";
        }

        return 0;
    }

    std::cerr << "No suitable arguments were given. Please refer to the help!\n\n";

    printRapidgzipHelp( options );

    return 1;
}


#if !defined( WITH_PYTHON_SUPPORT ) && !defined( WITHOUT_MAIN )
int
main( int argc, char** argv )
{
    try
    {
        return rapidgzipCLI( argc, argv );
    }
    catch ( const rapidgzip::BitReader::EndOfFileReached& exception )
    {
        std::cerr << "Unexpected end of file. Truncated or invalid gzip?\n";
        return 1;
    }
    catch ( const std::exception& exception )
    {
        const std::string_view message{ exception.what() };
        if ( message.empty() ) {
            std::cerr << "Caught exception with typeid: " << typeid( exception ).name() << "\n";
        } else {
            std::cerr << "Caught exception: " << message << "\n";
        }
        return 1;
    }

    return 1;
}
#endif
