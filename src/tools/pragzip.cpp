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
#include <utility>
#include <vector>

#include <cxxopts.hpp>

#include <AffinityHelpers.hpp>
#include <blockfinder/Bgzf.hpp>
#include <common.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <GzipAnalyzer.hpp>
#include <pragzip.hpp>
#include <ParallelGzipReader.hpp>
#include <Statistics.hpp>

#include "licenses.cpp"


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

        ( "chunk-size"   , "The chunk size decoded by the parallel workers in KiB.",
          cxxopts::value<unsigned int>()->default_value( "0" ) )

        ( "P,decoder-parallelism",
          "Use the parallel decoder. "
          "If an optional integer >= 1 is given, then that is the number of decoder threads to use. "
          "Note that there might be further threads being started with non-decoding work. "
          "If 0 is given, then the parallelism will be determiend automatically.",
          cxxopts::value<unsigned int>()->default_value( "0" ) )

        ( "import-index", "Uses an existing gzip index.", cxxopts::value<std::string>() )
        ( "export-index", "Write out a gzip index file.", cxxopts::value<std::string>() );

    options.add_options( "Output" )
        ( "h,help"   , "Print this help mesage." )
        ( "q,quiet"  , "Suppress noncritical error messages." )
        ( "v,verbose", "Be verbose. A second -v (or shorthand -vv) gives even more verbosity." )
        ( "V,version", "Display software version." )
        ( "oss-attributions", "Display open-source software licenses." );

    /* These options are offered because just piping to other tools can already bottleneck everything! */
    options.add_options( "Processing" )
        ( "count"      , "Prints the decompressed size." )
        ( "l,count-lines", "Prints the number of newline characters in the decompressed data." );

    options.parse_positional( { "input" } );

    /* cxxopts allows to specifiy arguments multiple times. But if the argument type is not a vector, then only
     * the last value will be kept! Therefore, do not check against this usage and simply use that value.
     * Arguments may only be queried with as if they have (default) values. */

    const auto parsedArgs = options.parse( argc, argv );

    const auto force   = parsedArgs["force"  ].as<bool>();
    const auto quiet   = parsedArgs["quiet"  ].as<bool>();
    const auto verbose = parsedArgs["verbose"].as<bool>();

    const auto getParallelism = [] ( const auto p ) { return p > 0 ? p : availableCores(); };
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
                  << "version 0.5.0.\n";
        return 0;
    }

    if ( parsedArgs.count( "oss-attributions" ) > 0 ) {
        std::cout << licenses::CXXOPTS << "\n"
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

    std::string inputFilePath; /* Can be empty. Then, read from STDIN. */
    if ( parsedArgs.count( "input" ) == 1 ) {
        inputFilePath = parsedArgs["input"].as<std::string>();
    }

    auto inputFile = openFileOrStdin( inputFilePath );

    /* Check if analysis is requested. */

    if ( parsedArgs.count( "analyze" ) > 0 ) {
        return pragzip::deflate::analyze( std::move( inputFile ) ) == pragzip::Error::NONE ? 0 : 1;
    }

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
            if ( !quiet ) {
                std::cerr << "Could not deduce output file name. Will write to '" << outputFilePath << "'\n";
            }
        }
    }

    /* Parse other arguments. */

    const auto countBytes = parsedArgs.count( "count" ) > 0;
    const auto countLines = parsedArgs.count( "count-lines" ) > 0;
    const auto decompress = ( parsedArgs.count( "decompress" ) > 0 ) || ( !countBytes && !countLines );

    if ( decompress && ( outputFilePath != "/dev/null" ) && fileExists( outputFilePath ) && !force ) {
        std::cerr << "Output file '" << outputFilePath << "' already exists! Use --force to overwrite.\n";
        return 1;
    }

    const auto indexLoadPath = parsedArgs.count( "import-index" ) > 0
                               ? parsedArgs["import-index"].as<std::string>()
                               : std::string();
    const auto indexSavePath = parsedArgs.count( "export-index" ) > 0
                               ? parsedArgs["export-index"].as<std::string>()
                               : std::string();
    if ( !indexLoadPath.empty() && !indexSavePath.empty() ) {
        std::cerr << "[Warning] Importing and exporting an index makes limited sense.\n";
    }
    if ( ( !indexLoadPath.empty() || !indexSavePath.empty() ) && ( decoderParallelism == 1 ) ) {
        std::cerr << "[Warning] The index only has an effect for parallel decoding.\n";
    }
    if ( !indexLoadPath.empty() && !fileExists( indexLoadPath ) ) {
        std::cerr << "The index to import was not found!\n";
        return 1;
    }

    /* Actually do things as requested. */

    if ( decompress || countBytes || countLines ) {
        if ( decompress && verbose ) {
            std::cerr << "Decompress " << ( inputFilePath.empty() ? "<stdin>" : inputFilePath.c_str() )
                      << " -> " << ( outputFilePath.empty() ? "<stdout>" : outputFilePath.c_str() ) << "\n";
        }

        if ( !inputFile ) {
            std::cerr << "Could not open input file: " << inputFilePath << "!\n";
            return 1;
        }

        /* Open either stdout, the given file, or nothing as necessary. */
        int outputFileDescriptor{ -1 };  // Use this for file access.
        unique_file_ptr outputFile;  // This should not be used, it is only for automatic closing!
        bool writingToStdout{ false };
        size_t oldOutputFileSize{ 0 };

    #ifndef _MSC_VER
        unique_file_descriptor ownedFd;  // This should not be used, it is only for automatic closing!
    #endif

        if ( decompress ) {
            if ( outputFilePath.empty() ) {
                writingToStdout = true;

            #ifdef _MSC_VER
                outputFileDescriptor = _fileno( stdout );
                _setmode( outputFileDescriptor, _O_BINARY );
            #else
                outputFileDescriptor = ::fileno( stdout );
            #endif
            } else {
            #ifndef _MSC_VER
                if ( fileExists( outputFilePath ) ) {
                    oldOutputFileSize = fileSize( outputFilePath );
                    /* Opening an existing file and overwriting its data can be much slower because posix_fallocate
                     * can be relatively slow compared to the decoding speed and memory bandwidth! Note that std::fopen
                     * would open a file with O_TRUNC, deallocating all its contents before it has to be reallocated. */
                    outputFileDescriptor = ::open( outputFilePath.c_str(), O_WRONLY );
                    ownedFd = unique_file_descriptor( outputFileDescriptor );
                }
            #endif

                if ( outputFileDescriptor == -1 ) {
                    outputFile = make_unique_file_ptr( outputFilePath.c_str(), "wb" );
                    if ( !outputFile ) {
                        std::cerr << "Could not open output file: " << outputFilePath << " for writing!\n";
                        return 1;
                    }
                    outputFileDescriptor = ::fileno( outputFile.get() );
                }
            }
        }

        const auto printIndexAnalytics =
            [&] ( const auto& reader )
            {
                if ( !verbose || ( indexSavePath.empty() && indexLoadPath.empty() ) ) {
                    return;
                }

                const auto offsets = reader->blockOffsets();
                if ( offsets.size() <= 1 ) {
                    return;
                }

                Statistics<double> encodedOffsetSpacings;
                Statistics<double> decodedOffsetSpacings;
                for ( auto it = offsets.begin(), nit = std::next( offsets.begin() );
                      nit != offsets.end(); ++it, ++nit ) {
                    const auto& [encodedOffset, decodedOffset] = *it;
                    const auto& [nextEncodedOffset, nextDecodedOffset] = *nit;
                    if ( nextEncodedOffset - encodedOffset > 0 ) {
                        encodedOffsetSpacings.merge( static_cast<double>( nextEncodedOffset - encodedOffset )
                                                     / CHAR_BIT / 1e6 );
                        decodedOffsetSpacings.merge( static_cast<double>( nextDecodedOffset - decodedOffset )
                                                     / 1e6 );
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
            };

        uint64_t newlineCount{ 0 };

        const auto t0 = now();

        size_t totalBytesRead{ 0 };
        if ( decoderParallelism == 1 ) {
            const auto writeAndCount =
                [outputFileDescriptor, countLines, &newlineCount]
                ( const void* const buffer,
                  uint64_t const    size )
                {
                    if ( outputFileDescriptor >= 0 ) {
                        writeAllToFd( outputFileDescriptor, buffer, size );
                    }
                    if ( countLines ) {
                        newlineCount += countNewlines( { reinterpret_cast<const char*>( buffer ),
                                                         static_cast<size_t>( size ) } );
                    }
                };

            pragzip::GzipReader</* CRC32 */ false> gzipReader{ std::move( inputFile ) };
            totalBytesRead = gzipReader.read( writeAndCount );
        } else {
            const auto writeAndCount =
                [outputFileDescriptor, countLines, &newlineCount]
                ( const std::shared_ptr<pragzip::BlockData>& blockData,
                  size_t const                               offsetInBlock,
                  size_t const                               dataToWriteSize )
                {
                    writeAll( blockData, outputFileDescriptor, offsetInBlock, dataToWriteSize );
                    if ( countLines ) {
                        using pragzip::deflate::DecodedData;
                        for ( auto it = DecodedData::Iterator( *blockData, offsetInBlock, dataToWriteSize );
                              static_cast<bool>( it ); ++it )
                        {
                            const auto& [buffer, size] = *it;
                            newlineCount += countNewlines( { reinterpret_cast<const char*>( buffer ), size } );
                        }
                    }
                };

            const auto chunkSize = parsedArgs["chunk-size"].as<unsigned int>();

            const auto decompressParallel =
                [&] ( const auto& reader )
                {
                    if ( !indexLoadPath.empty() ) {
                        reader->setBlockOffsets(
                            readGzipIndex( std::make_unique<StandardFileReader>( indexLoadPath ) ) );
                        printIndexAnalytics( reader );
                    }

                    totalBytesRead = reader->read( writeAndCount );

                    if ( !indexSavePath.empty() ) {
                        const auto file = throwingOpen( indexSavePath, "wb" );

                        const auto checkedWrite =
                            [&file] ( const void* buffer, size_t size )
                            {
                                if ( std::fwrite( buffer, 1, size, file.get() ) != size ) {
                                    throw std::runtime_error( "Failed to write data to index!" );
                                }
                            };

                        writeGzipIndex( reader->gzipIndex(), checkedWrite );
                    }

                    if ( indexLoadPath.empty() ) {
                        printIndexAnalytics( reader );
                    }
                };


            if ( verbose ) {
                using GzipReader = pragzip::ParallelGzipReader</* enable statistics */ true, /* show profile */ true>;
                auto reader =
                    chunkSize > 0
                    ? std::make_unique<GzipReader>( std::move( inputFile ), decoderParallelism, chunkSize * 1024 )
                    : std::make_unique<GzipReader>( std::move( inputFile ), decoderParallelism );
                decompressParallel( std::move( reader ) );
            } else {
                using GzipReader = pragzip::ParallelGzipReader</* enable statistics */ false, /* show profile */ false>;
                auto reader =
                    chunkSize > 0
                    ? std::make_unique<GzipReader>( std::move( inputFile ), decoderParallelism, chunkSize * 1024 )
                    : std::make_unique<GzipReader>( std::move( inputFile ), decoderParallelism );
                decompressParallel( std::move( reader ) );
            }

        }

    #ifndef _MSC_VER
        if ( ( *ownedFd != -1 ) && ( oldOutputFileSize > totalBytesRead ) ) {
            if ( ::ftruncate( outputFileDescriptor, totalBytesRead ) == -1 ) {
                std::cerr << "[Error] Failed to truncate file because of: " << strerror( errno )
                          << " (" << errno << ")\n";
            }
        }
    #endif

        const auto t1 = now();
        std::cerr << "Decompressed in total " << totalBytesRead << " B in " << duration( t0, t1 ) << " s -> "
                  << static_cast<double>( totalBytesRead ) / 1e6 / duration( t0, t1 ) << " MB/s\n";

        auto& out = writingToStdout ? std::cerr : std::cout;
        if ( countBytes != countLines ) {
            out << ( countBytes ? totalBytesRead : newlineCount );
        } else if ( countBytes && countLines ) {
            out << "Size: " << totalBytesRead << "\n";
            out << "Lines: " << newlineCount << "\n";
        }

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
    catch ( const pragzip::BitReader::EndOfFileReached& exception )
    {
        std::cerr << "Unexpected end of file. Truncated or invalid gzip?\n";
        return 1;
    }
    catch ( const std::exception& exception )
    {
        std::cerr << "Caught exception:\n" << exception.what() << ", typeid: " << typeid( exception ).name() << "\n";
        return 1;
    }

    return 1;
}
#endif
