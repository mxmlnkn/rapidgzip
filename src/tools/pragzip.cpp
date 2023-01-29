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
#include <pragzip.hpp>
#include <ParallelGzipReader.hpp>
#include <Statistics.hpp>


[[nodiscard]] pragzip::Error
analyze( std::unique_ptr<FileReader> inputFile )
{
    using namespace pragzip;
    using Block = pragzip::deflate::Block</* CRC32 */ false, /* Statistics */ true>;

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
    std::vector<double> compressionRatios;
    std::map<deflate::CompressionType, size_t> compressionTypes;

    std::map<std::vector<uint8_t>, size_t> precodeCodings;
    std::map<std::vector<uint8_t>, size_t> distanceCodings;
    std::map<std::vector<uint8_t>, size_t> literalCodings;

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

            std::cout << "Gzip header:\n";
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

        block.symbolTypes.literal = 0;
        block.symbolTypes.backreference = 0;

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

        const auto compressedSizeInBits = bitReader.tell() - blockOffset;
        const auto compressionRatio = static_cast<double>( uncompressedBlockSize ) /
                                      static_cast<double>( compressedSizeInBits ) * BYTE_SIZE;
        compressionRatios.emplace_back( compressionRatio );

        const auto [compressionTypeCount, wasInserted] = compressionTypes.try_emplace( block.compressionType(), 1 );
        if ( !wasInserted ) {
            compressionTypeCount->second++;
        }

        const auto printCodeLengthStatistics =
            [] ( const auto& codeLenghts )
            {
                auto min = std::numeric_limits<uint32_t>::max();
                auto max = std::numeric_limits<uint32_t>::min();
                size_t nonZeroCount{ 0 };

                std::array<size_t, 128> lengthCounts{};

                for ( const auto codeLength : codeLenghts ) {
                    if ( codeLength > 0 ) {
                        min = std::min( min, static_cast<uint32_t>( codeLength ) );
                        max = std::max( max, static_cast<uint32_t>( codeLength ) );
                        nonZeroCount++;
                    }
                    lengthCounts.at( codeLength )++;
                }

                std::stringstream result;
                result << nonZeroCount << " CLs in [" << min << ", " << max << "] out of " << codeLenghts.size()
                       << ": CL:Count, ";
                bool requiresComma{ false };
                for ( size_t codeLength = 0; codeLength < lengthCounts.size(); ++codeLength ) {
                    if ( requiresComma ) {
                        result << ", ";
                        requiresComma = false;
                    }

                    const auto count = lengthCounts[codeLength];
                    if ( count > 0 ) {
                        result << codeLength << ":" << count;
                        requiresComma = true;
                    }
                }

                return std::move( result ).str();
            };

        const VectorView<uint8_t> precodeCL{ block.precodeCL().data(), block.precodeCL().size() };
        const VectorView<uint8_t> distanceCL{ block.distanceAndLiteralCL().data() + block.codeCounts.literal,
                                              block.codeCounts.distance };
        const VectorView<uint8_t> literalCL{ block.distanceAndLiteralCL().data(), block.codeCounts.literal };

        precodeCodings[static_cast<std::vector<uint8_t> >( precodeCL )]++;
        distanceCodings[static_cast<std::vector<uint8_t> >( distanceCL )]++;
        literalCodings[static_cast<std::vector<uint8_t> >( literalCL )]++;

        const auto formatSymbolType =
            [total = block.symbolTypes.literal + block.symbolTypes.backreference] ( const auto count )
            {
                std::stringstream result;
                result << count << " (" << static_cast<double>( count ) * 100.0 / static_cast<double>( total ) << " %)";
                return std::move( result ).str();
            };

        std::cout
            << "Deflate block:\n"
            << "    Final Block             : " << ( block.isLastBlock() ? "True" : "False" ) << "\n"
            << "    Compression Type        : " << toString( block.compressionType() ) << "\n"
            << "    File Statistics:\n"
            << "        Total Block Count   : " << totalBlockCount << "\n"
            << "        Compressed Offset   : " << formatBits( blockOffset ) << "\n"
            << "        Uncompressed Offset : " << uncompressedBlockOffset << " B\n"
            << "    Gzip Stream Statistics:\n"
            << "        Block Count         : " << streamBlockCount << "\n"
            << "        Compressed Offset   : " << formatBits( blockOffset - headerOffset ) << "\n"
            << "        Uncompressed Offset : " << uncompressedBlockOffsetInStream << " B\n"
            << "    Compressed Size         : " << formatBits( compressedSizeInBits ) << "\n"
            << "    Uncompressed Size       : " << uncompressedBlockSize << " B\n"
            << "    Compression Ratio       : " << compressionRatio << "\n";
        if ( block.compressionType() == deflate::CompressionType::DYNAMIC_HUFFMAN ) {
            std::cout
                << "    Huffman Alphabets:\n"
                << "        Precode  : " << printCodeLengthStatistics( precodeCL ) << "\n"
                << "        Distance : " << printCodeLengthStatistics( distanceCL ) << "\n"
                << "        Literals : " << printCodeLengthStatistics( literalCL ) << "\n";
        }
        if ( block.compressionType() != deflate::CompressionType::UNCOMPRESSED ) {
            std::cout
                << "    Symbol Types:\n"
                << "        Literal         : " << formatSymbolType( block.symbolTypes.literal ) << "\n"
                << "        Back-References : " << formatSymbolType( block.symbolTypes.backreference ) << "\n"
                << "\n";
        }

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

    const auto printCategorizedDuration =
        [totalDuration = block.durations.readDynamicHeader + block.durations.readData] ( const double duration )
        {
            std::stringstream result;
            result << duration << " s (" << duration / totalDuration * 100 << " %)";
            return std::move( result ).str();
        };

    const auto printHeaderDuration =
        [totalDuration = block.durations.readDynamicHeader] ( const double duration )
        {
            std::stringstream result;
            result << duration << " s (" << duration / totalDuration * 100 << " %)";
            return std::move( result ).str();
        };

    const auto printAlphabetStatistics =
        [] ( const auto& counts )
        {
            size_t total{ 0 };
            size_t duplicates{ 0 };
            for ( const auto& [_, count] : counts ) {
                if ( count > 1 ) {
                    duplicates += count - 1;
                }
                total += count;
            }

            std::stringstream result;
            result << duplicates << " duplicates out of " << total << " ("
                   << static_cast<double>( duplicates ) * 100. / static_cast<double>( total ) << " %)";
            return std::move( result ).str();
        };

    std::cout
        << "\n\n== Benchmark Profile (Cumulative Times) ==\n"
        << "\n"
        << "readDynamicHuffmanCoding : " << printCategorizedDuration( block.durations.readDynamicHeader ) << "\n"
        << "readData                 : " << printCategorizedDuration( block.durations.readData ) << "\n"
        << "Dynamic Huffman Initialization in Detail:\n"
        << "    Read precode       : " << printHeaderDuration( block.durations.readPrecode      ) << "\n"
        << "    Create precode HC  : " << printHeaderDuration( block.durations.createPrecodeHC  ) << "\n"
        << "    Apply precode HC   : " << printHeaderDuration( block.durations.applyPrecodeHC   ) << "\n"
        << "    Create distance HC : " << printHeaderDuration( block.durations.createDistanceHC ) << "\n"
        << "    Create literal HC  : " << printHeaderDuration( block.durations.createLiteralHC  ) << "\n"
        << "\n"
        << "\n"
        << "== Alphabet Statistics ==\n"
        << "\n"
        << "Precode  : " << printAlphabetStatistics( precodeCodings ) << "\n"
        << "Distance : " << printAlphabetStatistics( distanceCodings ) << "\n"
        << "Literals : " << printAlphabetStatistics( literalCodings ) << "\n"
        << "\n"
        << "\n"
        << "== Encoded Block Size Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ encodedBlockSizes, 8, "bits" }.plot()
        << "\n"
        << "\n"
        << "== Decoded Block Size Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ decodedBlockSizes, 8, "Bytes" }.plot()
        << "\n"
        << "\n== Compression Ratio Distribution ==\n"
        << "\n"
        << Histogram<double>{ compressionRatios, 8, "Bytes" }.plot()
        << "\n"
        << "== Deflate Block Compression Types ==\n"
        << "\n";

    for ( const auto& [compressionType, count] : compressionTypes ) {
        std::cout << std::setw( 10 ) << toString( compressionType ) << " : " << count << "\n";
    }

    std::cout << std::endl;

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
        ( "V,version", "Display software version." );

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
