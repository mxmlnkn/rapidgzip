#include <iostream>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

std::ostream&
operator<<( std::ostream&           out,
            const std::set<size_t>& values )
{
    out << "{ ";
    for ( const auto& value : values ) {
        out << value << ", ";
    }
    out << "}";
    return out;
}

#include <common.hpp>
#define WITHOUT_MAIN
#include <rapidgzip.cpp>
#undef WITHOUT_MAIN
#include <TestHelpers.hpp>

using namespace std::string_literals;


[[nodiscard]] std::vector<char>
createRandomWords( const size_t fileSize )
{
    static constexpr size_t WORD_SIZE{ 16 };
    std::vector<std::array<char, WORD_SIZE> > words( 32 );
    for ( auto& word : words ) {
        for ( auto& c : word ) {
            c = static_cast<char>( rand() );
        }
    }

    std::vector<char> result;
    result.reserve( fileSize );
    for ( size_t i = 0; i < fileSize; ) {
        const auto iWord = static_cast<size_t>( rand() ) % words.size();
        const auto& word = words[iWord];
        result.insert( result.end(), word.data(), word.data() + word.size() );
        i += words[iWord].size();
    }

    return result;
}


int
callRapidgzip( const std::vector<std::string>& arguments )
{
    std::vector<const char*> rawArguments;
    rawArguments.reserve( arguments.size() + 1 );
    rawArguments.emplace_back( "./rapidgzip" );
    std::transform( arguments.begin(), arguments.end(), std::back_inserter( rawArguments ),
                    [] ( const auto& string ) { return string.data(); } );
    return rapidgzipCLI( rawArguments.size(), rawArguments.data() );
}


void
testCLI( const std::vector<std::string>& arguments,
         const std::filesystem::path&    outputFile,
         const std::vector<char>&        decompressed )
{
    const auto writeToStdout = contains( arguments, "--stdout" ) || contains( arguments, "-c" );
    const auto doDecompress = contains( arguments, "-d" ) || contains( arguments, "--decompress" );
    const auto expectOutputFile = doDecompress && !writeToStdout;
    if ( expectOutputFile ) {
        std::filesystem::remove( outputFile );
    }

    const auto oldCerrBuffer = std::cerr.rdbuf();
    const auto oldCoutBuffer = std::cout.rdbuf();

    std::stringstream cerrIntercept;
    std::stringstream coutIntercept;

    std::cerr.rdbuf( cerrIntercept.rdbuf() );
    std::cout.rdbuf( coutIntercept.rdbuf() );

    std::optional<std::string> caughtException;
    int exitCode = 1;
    try {
        exitCode = callRapidgzip( arguments );
    } catch ( const std::exception& exception ) {
        caughtException = exception.what();
    }

    std::cerr.rdbuf( oldCerrBuffer );
    std::cout.rdbuf( oldCoutBuffer );

    if ( caughtException ) {
        REQUIRE( !caughtException.has_value() );
        std::cerr << "Caught exception: " << *caughtException << "\n";
    } else {
        REQUIRE_EQUAL( exitCode, 0 );
    }
    if ( exitCode != 0 ) {
        std::cerr << "=== command line ==\n\n";
        for ( const auto& argument : arguments ) {
            std::cerr << argument << " ";
        }
        std::cerr << "\n\n";
        std::cerr << "=== stdout ===\n\n" << coutIntercept.str() << "\n\n";
        std::cerr << "=== stderr ===\n\n" << cerrIntercept.str() << "\n\n";
    }

    if ( expectOutputFile ) {
        REQUIRE( std::filesystem::is_regular_file( outputFile ) );
        if ( std::filesystem::is_regular_file( outputFile ) ) {
            const auto fileContents = readFile<std::vector<char> >( outputFile );
            REQUIRE_EQUAL( fileContents.size(), decompressed.size() );
            REQUIRE( fileContents == decompressed );
        }
    }

    if ( writeToStdout ) {
        const auto fileContents = coutIntercept.str();
        REQUIRE_EQUAL( fileContents.size(), decompressed.size() );
        REQUIRE( ( std::equal( fileContents.begin(), fileContents.end(), decompressed.begin(), decompressed.end() ) ) );
    }

    const auto doCount = contains( arguments, "--count" );
    const auto doCountLines = contains( arguments, "--count-lines" );

    /* Store copy to variable because "split" only works with views. */
    const auto output = ( writeToStdout ? cerrIntercept : coutIntercept ).str();
    const auto lines = split( output, '\n' );
    if ( doCount ) {
        const auto searchString = ( doCountLines ? "Size: " : "" ) + std::to_string( decompressed.size() );
        const auto outputContainsCount = contains( lines, searchString );
        REQUIRE( outputContainsCount );
        if ( !outputContainsCount ) {
            std::cerr << "Lines: " << lines << "\n";
        }
    }

    if ( doCountLines ) {
        const auto lineCount = std::count( decompressed.begin(), decompressed.end(), '\n' );
        const auto searchString = ( doCount ? "Lines: " : "" ) + std::to_string( lineCount );
        const auto outputContainsCount = contains( lines, searchString );
        REQUIRE( outputContainsCount );
        if ( !outputContainsCount ) {
            std::cerr << "Lines: " << lines << "\n";
        }
    }
}


using ChosenIndexes = std::vector<std::set<size_t> >;


[[nodiscard]] ChosenIndexes
chooseFrom( const size_t nToChoose,
            const size_t nToChooseFrom )
{
    if ( nToChoose > nToChooseFrom ) {
        throw std::invalid_argument( "Not enough to choose from!" );
    }

    /* Idea: Choose one and combine with all combinations for choosing nToChoose - 1 from the remaining set. */

    const std::function<ChosenIndexes( size_t, const std::set<size_t>& )> chooseFromSetRecursively =
        [&chooseFromSetRecursively]
        ( size_t                  nToChooseRecursively,
          const std::set<size_t>& values )
        {
            ChosenIndexes result;
            if ( values.empty() || ( nToChooseRecursively == 0 ) ) {
                return result;
            }

            for ( const auto chosenValue : values ) {
                if ( nToChooseRecursively == 1 ) {
                    result.emplace_back( std::set<size_t>{ { chosenValue } } );
                    continue;
                }

                auto remainingValues = values;
                remainingValues.erase( chosenValue );
                auto subChoices = chooseFromSetRecursively( nToChooseRecursively - 1U, remainingValues );

                for ( auto& chosenRecursively : subChoices ) {
                    std::set<size_t> chosen = { chosenValue };
                    chosen.merge( chosenRecursively );
                    if ( std::find( result.begin(), result.end(), chosen ) == result.end() ) {
                        result.emplace_back( std::move( chosen ) );
                    }
                }
            }
            return result;
        };


    std::set<size_t> values;
    for ( size_t i = 0; i < nToChooseFrom; ++i ) {
        values.insert( i );
    }
    return chooseFromSetRecursively( nToChoose, values );
}


void
testChooseFrom()
{
    /* All those necessary parentheses and curly brackets are insane! */
    REQUIRE_EQUAL( ( chooseFrom( 0, 4 ) ), ( ChosenIndexes{} ) );
    REQUIRE_EQUAL( ( chooseFrom( 1, 4 ) ), ( ChosenIndexes{ { { { 0 } }, { { 1 } }, { { 2 } }, { { 3 } } } } ) );
    REQUIRE_EQUAL( ( chooseFrom( 2, 4 ) ), ( ChosenIndexes{ {
        { { 0, 1 } }, { { 0, 2 } }, { { 0, 3 } },
        { { 1, 2 } }, { { 1, 3 } },
        { { 2, 3 } }
    } } ) );
    REQUIRE_EQUAL( ( chooseFrom( 3, 4 ) ), ( ChosenIndexes{ {
        { { 0, 1, 2 } }, { { 0, 1, 3 } }, { { 0, 2, 3 } }, { { 1, 2, 3 } }
    } } ) );
    REQUIRE_EQUAL( ( chooseFrom( 4, 4 ) ), ( ChosenIndexes{ { { { 0, 1, 2, 3 } } } } ) );
}


using ArgumentLists = std::vector<std::vector<std::string> >;


[[nodiscard]] ArgumentLists
concatenateCombinations( const ArgumentLists& values )
{
    ArgumentLists combinations;
    for ( size_t nChoices = 1; nChoices <= values.size(); ++nChoices ) {
        for ( const auto& indexes : chooseFrom( nChoices, values.size() ) ) {
            std::vector<std::string> combination;
            combination.reserve( indexes.size() );
            for ( const auto i : indexes ) {
                const auto& value = values[i];
                combination.insert( combination.end(), value.begin(), value.end() );
            }
            combinations.emplace_back( std::move( combination ) );
        }
    }
    return combinations;
}


[[nodiscard]] ArgumentLists
concatenateChoices( const ArgumentLists& a,
                    const ArgumentLists& toAppend )
{
    if ( a.empty() ) {
        return toAppend;
    }

    if ( toAppend.empty() ) {
        return a;
    }

    ArgumentLists combinations;
    for ( const auto& arguments : toAppend ) {
        for ( auto combination : a ) {
            combination.insert( combination.end(), arguments.begin(), arguments.end() );
            combinations.emplace_back( std::move( combination ) );
        }
    }

    return combinations;
}


void
testConcatenateCombinations()
{
    /* Goddamn parentheses and curly brackets and even literals. Without using string literals, it seems that
     * one of the arguments gets interpreted as size or something; the second "c" simply gets lost!
     * No compiler warning whatsoever. */
    REQUIRE_EQUAL( ( concatenateCombinations( { { { "-a"s } }, { { "-b"s, "c"s } } } ) ),
                   ( ArgumentLists{ { "-a" }, { "-b", "c" }, { "-a", "-b", "c" } } ) );
}


void
testCLI()
{
    /* Write to /dev/shm if possible because writing ~240 GB to any disk is probably not want you want for
     * a simple test and because my SSD locks p after some amount of write, which increases the test time. */
    const auto tmpFolder = createTemporaryDirectory( std::filesystem::is_directory( "/dev/shm" )
                                                     ? "/dev/shm/rapidgzip.testCLI"
                                                     : "rapidgzip.testCLI" );

    const auto filePath = tmpFolder.path() / "random-words";
    const auto compressedFilePath = tmpFolder.path() / "random-words.gz";
    const auto indexFilePath = tmpFolder.path() / "random-words.gz.index";

    const auto decompressed = createRandomWords( 128_Mi );  // Compresses to only ~8 MiB.
    const auto compressed = rapidgzip::compressWithZlib<std::vector<char> >( decompressed );
    {
        std::ofstream file( compressedFilePath );
        file.write( compressed.data(), compressed.size() );
    }

    /* Create index for import tests. */
    callRapidgzip( { "--export-index"s, indexFilePath, compressedFilePath.string() } );

    const auto testWithoutFile =
        [&] ( const std::vector<std::string>& arguments ) { testCLI( arguments, filePath, decompressed ); };

    const auto testFile =
        [&] ( std::vector<std::string> arguments ) {
            arguments.emplace_back( compressedFilePath );
            testCLI( arguments, /* expected output file (except for --stdout) */ filePath, decompressed );
            std::filesystem::remove( filePath );
        };

    testWithoutFile( { "--version" } );
    testWithoutFile( { "--oss-attributions" } );
    testWithoutFile( { "--help" } );

    /* Special subcommand that will ignore most of the other output options. */
    testFile( { "--analyze" } );

    /* Without --decompress, it only process the data without writing out the raw decompressed stream.
     * I think all of these combinations are valuable to test. */
    const ArgumentLists combinableActions = {
        { { "--count"s } },
        { { "--count-lines"s } },
        { { "--export-index"s, ( tmpFolder.path() / "index-file" ).string() } },
        /* Any combination of --decompress with the three processing options
         * should be doable to implement because decompression requires holding all data,
         * which makes it trivial to do any post-processing on the data. */
        { { "--decompress"s } },
    };

    /* We probably should trim some of these combinations! E.g.
     * - only test with single action or all actions together
     * - Avoid redundant combinations such as -P 1 -P 4 ? */
    ArgumentLists combinableOptions;
    combinableOptions = concatenateChoices( combinableOptions, { { { "-P"s, "1"s } }, { { "-P"s, "4"s } } } );
    combinableOptions = concatenateChoices( combinableOptions, { {}, { { "--import-index"s, indexFilePath } } } );
#ifndef SHORT_TESTS
    combinableOptions = concatenateChoices( combinableOptions, { { { "--verify"s } }, {} } );
#endif

    ArgumentLists combinedArguments;
    for ( const auto& actionArguments : concatenateCombinations( combinableActions ) ) {
        combinedArguments.emplace_back( actionArguments );
        for ( const auto& optionArguments : combinableOptions ) {
            auto arguments = actionArguments;
            arguments.insert( arguments.end(), optionArguments.begin(), optionArguments.end() );
            combinedArguments.emplace_back( arguments );
        }
    }

    for ( size_t i = 0; i < combinedArguments.size(); ++i ) {
        std::cout << "Testing CLI " << i << " out of " << combinedArguments.size() << ":";
        for ( const auto& argument : combinedArguments[i] ) {
            std::cout << " " << argument;
        }
        std::cout << std::endl;

        testFile( combinedArguments[i] );
    }

    /* Do everything all at once to --stdout.
     * @note Unfortunately the std::cout.rdbuf cannot capture the output caused by --stdout because
     *       the output is directly written to the stdout file descriptor without any std::cout call. */
    //const auto indexPath = ( tmpFolder.path() / "index-file" ).string();
    //testFile( { "--count", "--count-lines", "--decompress", "--export-index", indexPath, "--stdout" } );
}


int
main( int    argc,
      char** argv )
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    testChooseFrom();
    testConcatenateCombinations();
    testCLI();

    const std::string binaryFilePath( argv[0] );
    std::string binaryFolder = std::filesystem::path( binaryFilePath ).parent_path();
    if ( binaryFolder.empty() ) {
        binaryFolder = ".";
    }
    const auto rootFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/base64-256KiB.bgz" )
        ) / "src" / "tests" / "data";

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
