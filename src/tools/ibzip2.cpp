#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <cxxopts.hpp>

#include <AffinityHelpers.hpp>
#include <bzip2.hpp>
#include <BitReader.hpp>
#include <BitStringFinder.hpp>
#include <BZ2Reader.hpp>
#include <common.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <ParallelBZ2Reader.hpp>
#include <ParallelBitStringFinder.hpp>

#include "CLIHelper.hpp"
#include "licenses.hpp"


/* Check whether the found offsets actually point to BZ2 magic bytes. */
void
checkOffsets( const std::string&         filePath,
              const std::vector<size_t>& offsets )
{
    if ( !fileExists( filePath ) ) {
        return;
    }

    const std::set<uint64_t> bitStringsToFind = { bzip2::MAGIC_BITS_BLOCK, bzip2::MAGIC_BITS_EOS };
    bzip2::BitReader bitReader( std::make_unique<StandardFileReader>( filePath ) );
    for ( const auto offset : offsets ) {
        bitReader.seek( static_cast<long long int>( offset ) );
        /* Because bitReader is limited to 32-bit. */
        static_assert( bzip2::MAGIC_BITS_SIZE % 2 == 0, "Assuming magic bit string size to be an even number." );
        constexpr uint8_t BITS_PER_READ = bzip2::MAGIC_BITS_SIZE / 2;
        const auto magicBytes = ( static_cast<uint64_t>( bitReader.read( BITS_PER_READ ) ) << BITS_PER_READ )
                                | bitReader.read( BITS_PER_READ );

        if ( bitStringsToFind.count( magicBytes ) == 0 ) {
            std::stringstream msg;
            msg << "Magic bytes " << std::hex << magicBytes << std::dec << " at offset "
                << ( offset / CHAR_BIT ) << " B " << ( offset % CHAR_BIT ) << "b "
                << "do not match bzip2 magic bytes!";
            throw std::logic_error( std::move( msg ).str() );
        }
    }
}


void
dumpOffsets( std::ostream&              out,
             const std::vector<size_t>& offsets )
{
    if ( !out.good() ) {
        return;
    }

    for ( const auto offset : offsets ) {
        out << offset << "\n";
    }
}


void
dumpOffsets( std::ostream&                   out,
             const std::map<size_t, size_t>& offsets )
{
    if ( !out.good() ) {
        return;
    }

    for ( const auto [compressedOffset, offset] : offsets ) {
        out << compressedOffset << "," << offset << "\n";
    }
}


void
findCompressedBlocks( const std::string& inputFilePath,
                      const std::string& offsetOutputFilePath,
                      const unsigned int parallelism,
                      const unsigned int bufferSize,
                      const bool         test,
                      const bool         verbose )
{
    /* Having to go over the file twice is not optimal. This is because, the bit string finder is highly
     * optimized for finding non-EOS blocks for parallel decoding. Allowing a list of bit strings to test
     * for would make that algorithm more complex and possibly slower. It was not intended for debugging output */
    std::vector<size_t> offsets;
    const std::set<uint64_t> bitStringsToFind = { bzip2::MAGIC_BITS_BLOCK, bzip2::MAGIC_BITS_EOS };
    for ( const auto bitStringToFind : bitStringsToFind ) {
        using Finder = BitStringFinder<bzip2::MAGIC_BITS_SIZE>;
        using ParallelFinder = ParallelBitStringFinder<bzip2::MAGIC_BITS_SIZE>;

        auto file = openFileOrStdin( inputFilePath );

        std::unique_ptr<Finder> finder =
            parallelism == 1
            ? std::make_unique<Finder>( std::move( file ), bitStringToFind, bufferSize )
            : std::make_unique<ParallelFinder>( std::move( file ), bitStringToFind, parallelism, 0, bufferSize );

        for ( auto offset = finder->find(); offset != std::numeric_limits<size_t>::max(); offset = finder->find() ) {
            offsets.push_back( offset );
        }
    }

    std::sort( offsets.begin(), offsets.end() );

    if ( test ) {
        checkOffsets( inputFilePath, offsets );
    }

    if ( offsetOutputFilePath.empty() ) {
        for ( const auto offset : offsets ) {
            std::cout << offset << "\n";
        }
    } else {
        std::ofstream file( offsetOutputFilePath );
        dumpOffsets( file, offsets );
    }

    if ( verbose ) {
        std::cout << "Found " << offsets.size() << " blocks\n";
    }
}

void
printIbzip2Help( const cxxopts::Options& options )
{
    std::cout
    << options.help()
    << "\n"
    << "If no file names are given, ibzip2 decompresses from standard input to standard output.\n"
    << "If the output is discarded by piping to /dev/null, then the actual decoding step might\n"
    << "be omitted if neither --test nor -l nor -L nor --force are given.\n"
    << "\n"
    << "Examples:\n"
    << "\n"
    << "Decompress a file:\n"
    << "  ibzip2 -d file.bz2\n"
    << "\n"
    << "Decompress a file in parallel:\n"
    << "  ibzip2 -d -P 0 file.bz2\n"
    << "\n"
    << "Find and list the bzip2 block offsets to be used for another tool:\n"
    << "  ibzip2 -l blockoffsets.dat -- file.bz2\n"
    << "\n"
    << "List block offsets in both the compressed as well as the decompressed data during downloading:\n"
    << "  wget -O- 'ftp://example.com/file.bz2' | tee saved-file.bz2 | ibzip2 -L blockoffsets.dat > /dev/null\n"
    << std::endl;
}


int
ibzip2CLI( int argc, char** argv )
{
    /**
     * @note For some reason implicit values do not mix very well with positional parameters!
     *       Parameters given to arguments with implicit values will be matched by the positional argument instead!
     */
    cxxopts::Options options( "ibzip2",
                              "A bzip2 decompressor tool based on the indexed_bzip2 backend from ratarmount" );
    options.add_options( "Decompression" )
        ( "c,stdout"     , "Output to standard output. This is the default, when reading from standard input." )
        ( "d,decompress" , "Force decompression. Only for compatibility. No compression supported anyways." )
        ( "f,force"      , "Force overwriting existing output files. "
                           "Also forces decompression even when piped to /dev/null." )
        ( "i,input"      , "Input file. If none is given, data is read from standard input.",
          cxxopts::value<std::string>() )
        ( "o,output"     ,
          "Output file. If none is given, use the input file name with '.bz2' stripped or '<input file>.out'. "
          "If no input is read from standard input and not output file is given, then will write to standard output.",
          cxxopts::value<std::string>() )
        ( "k,keep"       , "Keep (do not delete) input file. Only for compatibility. "
                           "This tool will not delete anything automatically!" )
        ( "t,test"       , "Test compressed file integrity." )

        ( "p,block-finder-parallelism",
          "This only has an effect if the parallel decoder is used with the -P option. "
          "If an optional integer >= 1 is given, then that is the number of threads to use for finding bzip2 blocks. "
          "If 0 is given, then the parallelism will be determined automatically.",
          cxxopts::value<unsigned int>()->default_value( "1" ) )

        ( "P,decoder-parallelism",
          "Use the parallel decoder. "
          "If an optional integer >= 1 is given, then that is the number of decoder threads to use. "
          "Note that there might be further threads being started with non-decoding work. "
          "If 0 is given, then the parallelism will be determined automatically.",
          cxxopts::value<unsigned int>()->default_value( "0" ) );

    options.add_options( "Output" )
        ( "h,help"   , "Print this help message." )
        ( "q,quiet"  , "Suppress noncritical error messages." )
        ( "v,verbose", "Be verbose. A second -v (or shorthand -vv) gives even more verbosity." )
        ( "V,version", "Display software version." )
        ( "oss-attributions", "Display open-source software licenses." )
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

    options.add_options( "Advanced" )
        ( "buffer-size",
          "Specifies the output buffer size between calls to the Bzip2 decoder and writing to standard out. "
          "If only --list-offsets is used with nothing else then it affects the input buffer for the block finder.",
          cxxopts::value<unsigned int>()->default_value( "0" ) );

    options.parse_positional( { "input" } );

    /* cxxopts allows to specify arguments multiple times. But if the argument type is not a vector, then only
     * the last value will be kept! Therefore, do not check against this usage and simply use that value.
     * Arguments may only be queried with as if they have (default) values. */

    const auto parsedArgs = options.parse( argc, argv );

    const auto force   = parsedArgs["force"  ].as<bool>();
    const auto quiet   = parsedArgs["quiet"  ].as<bool>();
    const auto test    = parsedArgs["test"   ].as<bool>();
    const auto verbose = parsedArgs["verbose"].as<bool>();

    const auto getParallelism = [] ( const auto p ) { return p > 0 ? p : availableCores(); };
    const auto blockFinderParallelism = getParallelism( parsedArgs["block-finder-parallelism"].as<unsigned int>() );
    const auto decoderParallelism     = getParallelism( parsedArgs["decoder-parallelism"     ].as<unsigned int>() );

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
        printIbzip2Help( options );
        return 0;
    }

    if ( parsedArgs.count( "version" ) > 0 ) {
        std::cout << "ibzip2, CLI to the indexed and seekable bzip2 decoding library indexed-bzip2 version 1.5.0.\n";
        return 0;
    }

    if ( parsedArgs.count( "oss-attributions" ) > 0 ) {
        std::cout << licenses::CXXOPTS;
        return 0;
    }

    /* Parse input and output file specifications. */

    if ( parsedArgs.count( "input" ) > 1 ) {
        std::cerr << "One or none bzip2 filename to decompress must be specified!\n";
        return 1;
    }

    if ( !stdinHasInput() && ( parsedArgs.count( "input" ) != 1 ) ) {
        std::cerr << "Either stdin must have input, e.g., by piping to it, or an input file must be specified!\n";
        return 1;
    }

    std::string inputFilePath;  /* Can be empty. Then, read from STDIN. */
    if ( parsedArgs.count( "input" ) == 1 ) {
        inputFilePath = parsedArgs["input"].as<std::string>();
    }

    auto outputFilePath = getFilePath( parsedArgs, "output" );
    if ( ( parsedArgs.count( "stdout" ) == 0 ) && outputFilePath.empty() && !inputFilePath.empty() ) {
        const std::string& suffix = ".bz2";
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

    const auto listOffsets = parsedArgs.count( "list-offsets" ) > 0;
    const auto decompress = parsedArgs.count( "decompress" ) > 0;

    if ( decompress && ( outputFilePath != "/dev/null" ) && fileExists( outputFilePath ) && !force ) {
        std::cerr << "Output file '" << outputFilePath << "' already exists! Use --force to overwrite.\n";
        return 1;
    }

    const auto bufferSize = parsedArgs["buffer-size"].as<unsigned int>();

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

    if ( decompress || listOffsets ) {
        if ( verbose ) {
            std::cerr << "Decompress " << inputFilePath << " -> " << outputFilePath << " with " << decoderParallelism
                      << " threads\n";
        }

        auto fileReader = openFileOrStdin( inputFilePath );

        std::unique_ptr<OutputFile> outputFile;
        if ( decompress ) {
            outputFile = std::make_unique<OutputFile>( outputFilePath );
        }
        const auto outputFileDescriptor = outputFile ? outputFile->fd() : -1;

        std::unique_ptr<BZ2ReaderInterface> reader;
        if ( decoderParallelism == 1 ) {
            reader = std::make_unique<BZ2Reader>( std::move( fileReader ) );
        } else {
            reader = std::make_unique<ParallelBZ2Reader>( std::move( fileReader ), decoderParallelism );
        }

        size_t nBytesWrittenTotal = 0;
        if ( bufferSize > 0 ) {
            do {
                std::vector<char> buffer( bufferSize, 0 );
                const auto nBytesRead = reader->read( -1, buffer.data(), buffer.size() );
                assert( nBytesRead <= buffer.size() );

                const auto nBytesWritten = write( outputFileDescriptor, buffer.data(), nBytesRead );
                if ( static_cast<size_t>( nBytesWritten ) != nBytesRead ) {
                    std::cerr << "Could not write all the decoded data to the specified output!\n";
                }

                nBytesWrittenTotal += nBytesWritten;
            } while ( !reader->eof() );
        } else {
            nBytesWrittenTotal = reader->read( outputFileDescriptor );
        }

        const auto writeToStdErr = outputFile && outputFile->writingToStdout();
        auto& out = writeToStdErr ? std::cerr : std::cout;
        if ( outputFile ) {
            outputFile->truncate( nBytesWrittenTotal );
            outputFile.reset();  // Close the file here to include it in the time measurement.
        }


        const auto offsets = reader->blockOffsets();

        std::vector<size_t> compressedOffsets;
        compressedOffsets.reserve( offsets.size() );
        for ( const auto [offset, _] : offsets ) {
            compressedOffsets.push_back( offset );
        }

        if ( verbose ) {
            out << "Found " << offsets.size() << " blocks\n";
        }

        if ( test ) {
            checkOffsets( inputFilePath, compressedOffsets );

            const auto readerSize = reader->size();
            if ( !readerSize  ) {
                throw std::logic_error( "Bzip2 reader size should be available at this point!" );
            }
            if ( nBytesWrittenTotal != *readerSize ) {
                std::stringstream msg;
                msg << "Wrote less bytes (" << nBytesWrittenTotal << " B) than decoded stream is large("
                    << *readerSize << " B)!";
                throw std::logic_error( std::move( msg ).str() );
            }
        }

        if ( parsedArgs.count( "list-offsets" ) > 0 ) {
            const auto filePath = parsedArgs["list-offsets"].as<std::string>();
            if ( !filePath.empty() ) {
                std::ofstream file( filePath );
                dumpOffsets( file, offsets );
            } else if ( outputFilePath.empty() ) {
                dumpOffsets( std::cerr, offsets );
            } else {
                dumpOffsets( std::cout, offsets );
            }
        }

        if ( parsedArgs.count( "list-compressed-offsets" ) > 0 ) {
            const auto filePath = parsedArgs["list-compressed-offsets"].as<std::string>();
            if ( !filePath.empty() ) {
                std::ofstream file( filePath );
                dumpOffsets( file, compressedOffsets );
            } else if ( outputFilePath.empty() ) {
                dumpOffsets( std::cerr, compressedOffsets );
            } else {
                dumpOffsets( std::cout, compressedOffsets );
            }
        }

        return 0;
    }

    if ( parsedArgs.count( "list-compressed-offsets" ) > 0 ) {
        if ( verbose ) {
            std::cerr << "Find block offsets\n";
        }

        /* If effectively only the compressed offsets are requested, then we can use the blockfinder directly! */
        findCompressedBlocks( inputFilePath,
                              parsedArgs["list-compressed-offsets"].as<std::string>(),
                              blockFinderParallelism,
                              bufferSize > 0 ? bufferSize : 32_Ki,
                              test,
                              parsedArgs.count( "verbose" ) > 0 );
        return 0;
    }

    std::cerr << "No suitable arguments were given. Please refer to the help!\n\n";

    printIbzip2Help( options );

    return 1;
}


#ifndef WITH_PYTHON_SUPPORT
int
main( int argc, char** argv )
{
    try
    {
        return ibzip2CLI( argc, argv );
    }
    catch ( const std::exception& exception )
    {
        std::cerr << "Caught exception:\n" << exception.what() << "\n";
        return 1;
    }

    return 1;
}
#endif
