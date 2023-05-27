#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common.hpp>
#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <blockfinder/Uncompressed.hpp>
#include <blockfinder/precodecheck/WithoutLUT.hpp>
#include <filereader/BufferView.hpp>
#include <Statistics.hpp>
#include <ThreadPool.hpp>


namespace
{
/**
 * Valid signature to look for deflate block:
 * - 0b0  Final Block: We ignore uninteresting final blocks (filter 50%)
 * - 0b10 Compression Type Dynamic Huffman (filters 75%)
 * - (Anything but 0b1111) + 1 bit
 *   Code Count 257 + (5-bit) <= 286, i.e., (5-bit) <= 29 (31 is 0b11111, 30 is 0b11110)
 *   (filters out 2 /32 = 6.25%)
 *   Beware that the >highest< 4 bits may not be 1 but this that we requrie all 5-bits to
 *   determine validity because they are lower significant first!
 * The returned position is only 0 if all of the above holds for a bitCount of 13
 * Next would be the 3-bit precode code lengths. One or two alone does not allow any filtering at all.
 * I think starting from three, it might become possible, e.g., if any two are 1, then all others must
 * be of 0-length because the tree is filled already.
 */
template<uint8_t bitCount>
constexpr bool
isDynamicHeader( uint32_t bits )
{
    if constexpr ( bitCount == 0 ) {
        return false;
    } else {
        /* Bit 0: final block flag */
        const auto isLastBlock = ( bits & 1U ) != 0;
        bits >>= 1U;
        bool matches = !isLastBlock;
        if constexpr ( bitCount <= 1U ) {
            return matches;
        }

        /* Bits 1-2: compression type */
        const auto compressionType = bits & nLowestBitsSet<uint32_t, 2U>();
        matches &= ( compressionType & 1U ) == 0;
        if constexpr ( bitCount <= 2U ) {
            return matches;
        }
        matches &= compressionType == 0b10;

        return matches;
    }
}


template<uint8_t bitCount>
constexpr uint8_t
nextDynamicHeader( uint32_t bits )
{
    if ( isDynamicHeader<bitCount>( bits ) ) {
        return 0;
    }

    if constexpr ( bitCount == 0 ) {
        return 0;
    } else {
        return 1U + nextDynamicHeader<bitCount - 1U>( bits >> 1U );
    }
}


template<uint8_t CACHED_BIT_COUNT>
constexpr auto NEXT_DYNAMIC_HEADER_LUT =
    [] ()
    {
        std::array<uint8_t, 1U << CACHED_BIT_COUNT> result{};
        for ( uint32_t i = 0; i < result.size(); ++i ) {
            result[i] = nextDynamicHeader<CACHED_BIT_COUNT>( i );
        }
        return result;
    }();


template<typename T>
class CountWithPercentage
{
public:
    void
    merge( T validCount,
           T testCount )
    {
        m_count.merge( validCount );
        m_percentage.merge( static_cast<double>( validCount ) / static_cast<double>( testCount ) * 100 );
    }

    [[nodiscard]] std::string
    toString() const
    {
        std::stringstream result;
        result << m_count.formatAverageWithUncertainty( false, 2 )
               << ", (" << m_percentage.formatAverageWithUncertainty( false, 2 ) << ") %";
        return std::move( result ).str();
    }

private:
    Statistics<T> m_count;
    Statistics<double> m_percentage;
};
}


template<typename T>
[[nodiscard]] std::vector<T>
createRandomData( size_t size )
{
    std::random_device randomDevice;
    std::mt19937 randomGenerator( randomDevice() );
    std::uniform_int_distribution<T> uniformBytes( std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max() );

    std::vector<T> randomData( size );
    for ( auto& x : randomData ) {
        x = uniformBytes( randomGenerator );
    }

    return randomData;
}


void
benchmarkRandomNumberGeneration()
{
    const auto t0 = now();

    const auto randomData = createRandomData<char>( 64_Mi );

    const auto dt = duration( t0 );
    std::cout << "Generating " << formatBytes( randomData.size() ) << " of random data took " << dt << " s"
              << " -> " << static_cast<double>( randomData.size() ) / dt / 1e6 << " MB/s.\n";
}


void
findNonCompressedFalsePositives()
{
    constexpr size_t nRepetitions = 12;
    constexpr auto testDataSize = 1_Gi;

    const auto countFalsePositives =
        [] () {
            const auto randomData = createRandomData<char>( testDataSize );
            pragzip::BitReader bitReader( std::make_unique<BufferViewFileReader>( randomData ) );
            const auto bitReaderSize = bitReader.size();

            size_t matches{ 0 };
            for ( size_t offset = 0; offset < bitReaderSize;
                  offset = pragzip::blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader ).second )
            {
                ++matches;
                bitReader.seek( static_cast<long long int>( offset ) + 1 );
            }

            return matches;
        };

    ThreadPool threadPool;
    std::vector<std::future<size_t> > tasks;
    for ( size_t i = 0; i < nRepetitions; ++i ) {
        tasks.emplace_back( threadPool.submit( countFalsePositives ) );
    }

    CountWithPercentage<size_t> statistics;
    for ( auto& task : tasks ) {
        statistics.merge( task.get(), testDataSize * 8 );
    }

    std::cout << "False positives for non-compressed deflate block: " << statistics.toString() << "\n";
}


void
findDynamicBitTripletFalsePositives()
{
    using BitReader64 = BitReader<false, uint64_t>;
    constexpr size_t nRepetitions = 3;
    constexpr auto LUT = NEXT_DYNAMIC_HEADER_LUT<12>;
    constexpr size_t randomDataSize = 8_Mi;

    Statistics<double> statistics;
    for ( size_t i = 0; i < nRepetitions; ++i ) {
        const auto randomData = createRandomData<char>( randomDataSize );
        BitReader64 bitReader( std::make_unique<BufferViewFileReader>( randomData ) );

        //std::cerr << "Bit reader: position: " << bitReader.tell() << ", size: " << bitReader.size() << "\n";
        //std::cerr << "First match: " << (int) LUT[bitReader.peek<12>()] << "\n";

        size_t matches{ 0 };
        try {
            while ( true ) {
                const auto nextPosition = LUT[bitReader.peek<12>()];
                if ( nextPosition == 0 ) {
                    ++matches;
                    bitReader.seekAfterPeek( 1 );
                } else {
                    bitReader.seekAfterPeek( nextPosition );
                }
            }
        } catch ( const BitReader64::EndOfFileReached& ) {
            // EOF reached
        }

        const auto matchRatio = static_cast<double>( matches ) / 8. / randomDataSize;
        statistics.merge( matchRatio );
    }

    /* 12.5%, exactly like a naive approach of 1/2^3 would give us. */
    std::cout << "Match ratio: (" << statistics.average() * 100 << " +- " << statistics.standardDeviation() * 100
              << ") %\n";
}


static constexpr size_t MAXIMUM_CHECKED_TAIL_BITS =
    /* final block bit */ 1 +
    /* compression type */ 2 +
    /* precode count */ 5 +
    /* distance code count */ 5 +
    /* literal code count */ 4 +
    /* precode */
    pragzip::deflate::MAX_PRECODE_COUNT * pragzip::deflate::PRECODE_BITS +
    /* distance code lengths */
    pragzip::deflate::MAX_DISTANCE_SYMBOL_COUNT * pragzip::deflate::MAX_PRECODE_LENGTH +
    /* literal code lengths */
    pragzip::deflate::MAX_LITERAL_OR_LENGTH_SYMBOLS * pragzip::deflate::MAX_PRECODE_LENGTH;


// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class AnalyzeDynamicBlockFalsePositives
{
public:
    explicit
    AnalyzeDynamicBlockFalsePositives( size_t experimentCountArg ) :
        experimentCount( experimentCountArg )
    {
        countFalsePositives();
    }

    void
    printStatistics() const;

private:
    void
    countFalsePositives();

    void
    countFalsePositives( const std::vector<char>& data,
                         size_t                   nBitsToTest );

public:
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)

    pragzip::deflate::Block</* enable analysis */ true> block;

    const size_t experimentCount;

    /* Statistics */
    size_t m_offsetsTestedMoreInDepth{ 0 };
    size_t checkPrecodeFails{ 0 };
    size_t filteredByInvalidPrecode{ 0 };
    size_t filteredByBloatingPrecode{ 0 };
    size_t passedDeflateHeaderTest{ 0 };
    size_t foundOffsets{ 0 };

    size_t filteredByFinalBlock{ 0 };
    size_t filteredByCompressionType{ 0 };
    size_t filteredByLiteralCount{ 0 };

    size_t filteredByInvalidDistanceCoding{ 0 };
    size_t filteredByBloatingDistanceCoding{ 0 };
    size_t filteredByInvalidLiteralCoding{ 0 };
    size_t filteredByBloatingLiteralCoding{ 0 };

    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
    // NOLINTEND(misc-non-private-member-variables-in-classes)

private:
    std::unordered_map<pragzip::Error, uint64_t> m_errorCounts;

    /* Random generator */
    std::random_device m_randomDevice;
    std::mt19937 m_randomGenerator{ m_randomDevice() };
    std::uniform_int_distribution<char> m_uniformBytes{
        std::numeric_limits<char>::lowest(),
        std::numeric_limits<char>::max()
    };
};


void
AnalyzeDynamicBlockFalsePositives::countFalsePositives()
{
    std::vector<char> randomData;

    constexpr size_t TEST_CHUNK_SIZE = 128_Mi;
    for ( size_t iExperiment = 0; iExperiment < experimentCount; ++iExperiment ) {
        const auto oldSize = randomData.size();
        randomData.resize( TEST_CHUNK_SIZE + MAXIMUM_CHECKED_TAIL_BITS );
        for ( size_t i = oldSize; i < randomData.size(); ++i ) {
            randomData[i] = m_uniformBytes( m_randomGenerator );
        }

        const auto remainingExperiments = experimentCount - iExperiment;
        const auto nBitsToTestInChunk = std::min( remainingExperiments, TEST_CHUNK_SIZE * 8 );

        countFalsePositives( randomData, nBitsToTestInChunk );

        iExperiment += nBitsToTestInChunk;

        randomData.erase( randomData.begin(), randomData.begin() + TEST_CHUNK_SIZE );
    }
}


void
AnalyzeDynamicBlockFalsePositives::countFalsePositives( const std::vector<char>& data,
                                                        size_t                   nBitsToTest )
{
    pragzip::BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    static constexpr auto CACHED_BIT_COUNT = 14;

    for ( size_t offset = 0; offset <= nBitsToTest; ++offset ) {
        bitReader.seek( static_cast<long long int>( offset ) );

        try
        {
            using namespace pragzip::blockfinder;
            const auto peeked = bitReader.peek<CACHED_BIT_COUNT>();

            const auto isFinalBlock = ( peeked & 1U ) != 0;
            if ( isFinalBlock ) {
                ++filteredByFinalBlock;
                continue;
            }

            const auto compressionType = ( peeked >> 1U ) & 0b11U;
            if ( compressionType != 0b10 ) {
                ++filteredByCompressionType;
                continue;
            }

            const auto literalCodeCount = ( peeked >> 3U ) & 0b11111U;
            if ( literalCodeCount >= 30 ) {
                ++filteredByLiteralCount;
                continue;
            }

            ++passedDeflateHeaderTest;

            bitReader.seek( static_cast<long long int>( offset ) + 13 );
            const auto next4Bits = bitReader.read( pragzip::deflate::PRECODE_COUNT_BITS );
            const auto next57Bits = bitReader.peek( pragzip::deflate::MAX_PRECODE_COUNT
                                                    * pragzip::deflate::PRECODE_BITS );
            static_assert( pragzip::deflate::MAX_PRECODE_COUNT * pragzip::deflate::PRECODE_BITS
                           <= pragzip::BitReader::MAX_BIT_BUFFER_SIZE,
                           "This optimization requires a larger BitBuffer inside BitReader!" );
            /* Do not use a LUT because it cannot return specific errors. */
            using pragzip::PrecodeCheck::WithoutLUT::checkPrecode;
            const auto precodeError = checkPrecode( next4Bits, next57Bits );
            switch ( precodeError )
            {
            case pragzip::Error::NONE:
                break;
            case pragzip::Error::EMPTY_ALPHABET:
            case pragzip::Error::INVALID_CODE_LENGTHS:
                ++filteredByInvalidPrecode;
                break;
            case pragzip::Error::BLOATING_HUFFMAN_CODING:
                ++filteredByBloatingPrecode;
                break;
            default:
                throw std::logic_error( "Unexpected error for checkPrecode!" );
            }

            m_offsetsTestedMoreInDepth++;
            auto error = precodeError;
            if ( precodeError == pragzip::Error::NONE ) {
                const auto oldFailedDistanceInit = block.failedDistanceInit;
                const auto oldFailedLiteralInit = block.failedLiteralInit;

                bitReader.seek( static_cast<long long int>( offset ) + 3 );
                error = block.readDynamicHuffmanCoding( bitReader );

                if ( block.failedPrecodeInit > 0 ) {
                    using namespace pragzip::deflate;

                    const auto codeLengthCount = 4 + next4Bits;
                    const auto precodeBits = next57Bits & nLowestBitsSet<uint64_t>( codeLengthCount * PRECODE_BITS );

                    std::cerr << "Failed to handle the following precode correctly:\n";
                    std::cerr << "    bitReader.tell(): " << bitReader.tell() << " out of " << bitReader.size() << "\n";
                    std::cerr << "    precode code length count: " << codeLengthCount << "\n",
                    std::cerr << "    code lengths:";
                    for ( size_t i = 0; i < codeLengthCount; ++i ) {
                        std::cerr << " " << ( ( precodeBits >> ( i * PRECODE_BITS ) ) & 0b111U );
                    }
                    std::cerr << "\n";
                    std::cerr << std::bitset<57>( precodeBits ) << "\n";

                    throw std::logic_error( "After checkPrecode, it shouldn't fail inside the block!" );
                }

                if ( oldFailedDistanceInit != block.failedDistanceInit ) {
                    switch ( error )
                    {
                    case pragzip::Error::NONE:
                        break;
                    case pragzip::Error::EMPTY_ALPHABET:
                    case pragzip::Error::INVALID_CODE_LENGTHS:
                        ++filteredByInvalidDistanceCoding;
                        break;
                    case pragzip::Error::BLOATING_HUFFMAN_CODING:
                        ++filteredByBloatingDistanceCoding;
                        break;
                    default:
                        throw std::logic_error( "Unexpected error for Distance Huffman init!" );
                    }
                }

                if ( oldFailedLiteralInit != block.failedLiteralInit ) {
                    switch ( error )
                    {
                    case pragzip::Error::NONE:
                        break;
                    case pragzip::Error::EMPTY_ALPHABET:
                    case pragzip::Error::INVALID_CODE_LENGTHS:
                        ++filteredByInvalidLiteralCoding;
                        break;
                    case pragzip::Error::BLOATING_HUFFMAN_CODING:
                        ++filteredByBloatingLiteralCoding;
                        break;
                    default:
                        throw std::logic_error( "Unexpected error for Literal Huffman init!" );
                    }
                }
            } else {
                ++checkPrecodeFails;
            }

            const auto [count, wasInserted] = m_errorCounts.try_emplace( error, 1 );
            if ( !wasInserted ) {
                count->second++;
            }

            if ( error != pragzip::Error::NONE ) {
                continue;
            }

            ++foundOffsets;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            throw std::logic_error( "EOF reached. Trailing buffer calculation must be wrong!" );
            break;
        }
    }

    if ( block.failedPrecodeInit > 0 ) {
        throw std::logic_error( "After checkPrecode, it shouldn't file inside the block!" );
    }
}


void
AnalyzeDynamicBlockFalsePositives::printStatistics() const
{
    const auto nBitsToTest = experimentCount;

    std::cout << "Filtering cascade:\n"
              << "+-> Total number of test locations: " << nBitsToTest
              << "\n"
              << "    Filtered by deflate header test jump LUT: " << ( nBitsToTest - passedDeflateHeaderTest ) << " ("
              << static_cast<double>( nBitsToTest - passedDeflateHeaderTest ) / static_cast<double>( nBitsToTest )
                 * 100 << " %)\n"
              << "    Remaining locations to test: " << passedDeflateHeaderTest << "\n"
              << "    +-> Failed checkPrecode calls: " << checkPrecodeFails << " ("
              << static_cast<double>( checkPrecodeFails ) / static_cast<double>( passedDeflateHeaderTest ) * 100
              << " %)\n"
              << "        Remaining locations to test: " << ( passedDeflateHeaderTest - checkPrecodeFails ) << "\n"
              << "        +-> Invalid Distance Huffman Coding: " << block.failedDistanceInit << " ("
              << static_cast<double>( block.failedDistanceInit )
                 / static_cast<double>( passedDeflateHeaderTest - checkPrecodeFails ) * 100 << " %)\n"
              << "            Remaining locations: "
              << ( passedDeflateHeaderTest - checkPrecodeFails - block.failedDistanceInit ) << "\n"
              << "            +-> Failing precode HC usage or literal/distance HC construction: "
              << ( passedDeflateHeaderTest - checkPrecodeFails - block.failedDistanceInit - foundOffsets ) << "\n"
              << "                Location candidates: " << foundOffsets << "\n\n";
}


void
findDynamicFalsePositives( const size_t nBitsToTest )
{
    ThreadPool threadPool;

    using Task = std::future<std::unique_ptr<AnalyzeDynamicBlockFalsePositives> >;
    constexpr auto nThreads = 12;
    std::vector<Task> tasks;
    tasks.reserve( nThreads );

    for ( int i = 0; i < nThreads; ++i ) {
        tasks.emplace_back(
            threadPool.submit(
                [nBitsToTest] ()
                {
                    return std::make_unique<AnalyzeDynamicBlockFalsePositives>( nBitsToTest );
                } ) );
    }

    struct {
        CountWithPercentage<size_t> filteredByDeflateHeaderTest;

        CountWithPercentage<size_t> filteredByFinalBlock;
        CountWithPercentage<size_t> filteredByCompressionType;
        CountWithPercentage<size_t> filteredByLiteralCount;
        CountWithPercentage<size_t> passedDeflateHeaderTest;

        CountWithPercentage<size_t> checkPrecodeFails;
        CountWithPercentage<size_t> filteredByInvalidPrecode;
        CountWithPercentage<size_t> filteredByBloatingPrecode;
        CountWithPercentage<size_t> passedPrecodeCheck;

        CountWithPercentage<size_t> failedDistanceInit;
        CountWithPercentage<size_t> filteredByInvalidDistanceCoding;
        CountWithPercentage<size_t> filteredByBloatingDistanceCoding;

        CountWithPercentage<size_t> passedDistanceInitCheck;
        CountWithPercentage<size_t> failedLiteralInitCheck;
        CountWithPercentage<size_t> filteredByInvalidLiteralCoding;
        CountWithPercentage<size_t> filteredByBloatingLiteralCoding;
        CountWithPercentage<size_t> filteredByPrecodeApply;
        CountWithPercentage<size_t> passedReadHeader;

        Statistics<size_t> foundOffsets;
    } stats;

    for ( auto& task : tasks ) {
        const auto result = task.get();

        const auto filteredByDeflateHeaderTest = nBitsToTest - result->passedDeflateHeaderTest;
        stats.filteredByDeflateHeaderTest.merge( filteredByDeflateHeaderTest, nBitsToTest );
        stats.filteredByFinalBlock.merge( result->filteredByFinalBlock, nBitsToTest );
        stats.filteredByCompressionType.merge( result->filteredByCompressionType, nBitsToTest );
        stats.filteredByLiteralCount.merge( result->filteredByLiteralCount, nBitsToTest );
        stats.passedDeflateHeaderTest.merge( result->passedDeflateHeaderTest, nBitsToTest );

        stats.checkPrecodeFails.merge( result->checkPrecodeFails, result->passedDeflateHeaderTest );
        stats.filteredByInvalidPrecode.merge( result->filteredByInvalidPrecode, result->passedDeflateHeaderTest );
        stats.filteredByBloatingPrecode.merge( result->filteredByBloatingPrecode, result->passedDeflateHeaderTest );

        const auto passedPrecodeCheck = result->passedDeflateHeaderTest - result->checkPrecodeFails;
        stats.passedPrecodeCheck.merge( passedPrecodeCheck, result->passedDeflateHeaderTest );

        stats.filteredByInvalidDistanceCoding.merge( result->filteredByInvalidDistanceCoding, passedPrecodeCheck );
        stats.filteredByBloatingDistanceCoding.merge( result->filteredByBloatingDistanceCoding, passedPrecodeCheck );
        stats.filteredByPrecodeApply.merge( result->block.failedPrecodeApply, passedPrecodeCheck );

        const auto passedDistanceInitCheck = passedPrecodeCheck
                                             - result->block.failedPrecodeApply
                                             - result->block.failedDistanceInit;
        stats.passedDistanceInitCheck.merge( passedDistanceInitCheck, passedPrecodeCheck );

        stats.filteredByInvalidLiteralCoding.merge( result->filteredByInvalidLiteralCoding, passedDistanceInitCheck );
        stats.filteredByBloatingLiteralCoding.merge( result->filteredByBloatingLiteralCoding, passedDistanceInitCheck );

        const auto passedReadHeader = passedDistanceInitCheck
                                      - result->filteredByInvalidLiteralCoding
                                      - result->filteredByBloatingLiteralCoding;
        const auto passedReadHeader2 = passedDistanceInitCheck - result->block.failedLiteralInit;
        if ( passedReadHeader != passedReadHeader2 ) {
            throw std::logic_error( "Sum of passed counts should be the same!" );
        }
        stats.passedReadHeader.merge( passedReadHeader, passedDistanceInitCheck );

        stats.foundOffsets.merge( result->foundOffsets );
    }

    std::cout
        << "Filtering cascade:\n"
        << "+-> Total number of test locations: " << nBitsToTest
        << "\n"
        << "    Filtered by final block bit: " << stats.filteredByFinalBlock.toString() << "\n"
        << "    Filtered by compression type: " << stats.filteredByCompressionType.toString() << "\n"
        << "    Filtered by literal code length count: " << stats.filteredByLiteralCount.toString() << "\n"
        << "    +-> Remaining locations to test: " << stats.passedDeflateHeaderTest.toString()
        << " (filtered: " << stats.filteredByDeflateHeaderTest.toString() << ")\n"
        << "        Filtered by invalid precode: " << stats.filteredByInvalidPrecode.toString() << "\n"
        << "        Filtered by non-optimal precode: " << stats.filteredByBloatingPrecode.toString() << "\n"
        << "        +-> Remaining locations to test: " << stats.passedPrecodeCheck.toString()
        << " (filtered: " << stats.checkPrecodeFails.toString() << ")\n"
        << "            Failing precode usage: " << stats.filteredByPrecodeApply.toString() << "\n"
        << "            Invalid Distance Huffman Coding: " << stats.filteredByInvalidDistanceCoding.toString() << "\n"
        << "            Non-Optimal Distance Huffman Coding: "
        << stats.filteredByBloatingDistanceCoding.toString() << "\n"
        << "            +-> Remaining locations to test: " << stats.passedDistanceInitCheck.toString() << "\n"
        << "                Invalid Literal Huffman Coding: " << stats.filteredByInvalidLiteralCoding.toString() << "\n"
        << "                Non-Optimal Literal Huffman Coding: "
        << stats.filteredByBloatingLiteralCoding.toString() << "\n"
        << "                +-> Remaining locations to test: " << stats.passedReadHeader.toString() << "\n"
        << "                    Location candidates: " << stats.foundOffsets.formatAverageWithUncertainty( false, 2 )
        << "\n\n";
}


int
main( int    argc,
      char** argv )
{
    std::cout << "MAXIMUM_CHECKED_TAIL_BITS: " << MAXIMUM_CHECKED_TAIL_BITS << "\n\n";

    size_t nBitsToTest = 10'000'000'000;
    if ( argc > 1 ) {
        try {
            nBitsToTest = static_cast<size_t>( std::stoll( argv[1] ) );
        } catch( const std::out_of_range& ) {
            std::cerr << "Out of range number of bits to test specified (" << argv[1] << ")!\n";
            std::exit( 1 );
        } catch( const std::invalid_argument& ) {
            std::cerr << "Invalid number of bits to test specified (" << argv[1] << ")!\n";
            std::exit( 1 );
        }
    }

    benchmarkRandomNumberGeneration();
    std::cout << "\n";

    findNonCompressedFalsePositives();
    std::cout << "\n";

    findDynamicBitTripletFalsePositives();
    std::cout << "\n";

    findDynamicFalsePositives( nBitsToTest );

    return 0;
}


/*
Filtering cascade:
+-> Total number of test locations: 1000000000000
    Filtered by final block bit: 500000100000 +- 900000, (50.00001 +- 0.00009) %
    Filtered by compression type: 375000000000 +- 800000, (37.50000 +- 0.00008) %
    Filtered by literal code length count: 7812470000 +- 140000, (0.781247 +- 0.000014) %
    +-> Remaining locations to test: 117187500000 +- 400000, (11.71875 +- 0.00004) % (filtered: 882812500000 +- 400000, (88.28125 +- 0.00004) %)
        Filtered by invalid precode: 77451600000 +- 600000, (66.0920 +- 0.0003) %
        Filtered by non-optimal precode: 39256900000 +- 400000, (33.4993 +- 0.0003) %
        +-> Remaining locations to test: 478940000 +- 40000, (0.40870 +- 0.00004) % (filtered: 116708500000 +- 400000, (99.59130 +- 0.00004) %)
            Failing precode usage: 386660000 +- 50000, (80.733 +- 0.004) %
            Invalid Distance Huffman Coding: 14291000 +- 6000, (2.9839 +- 0.0013) %
            Non-Optimal Distance Huffman Coding: 77126000 +- 16000, (16.103 +- 0.004) %
            +-> Remaining locations to test: 858000 +- 1700, (0.1791 +- 0.0004) %
                Invalid Literal Huffman Coding: 340600 +- 1000, (39.69 +- 0.10) %
                Non-Optimal Literal Huffman Coding: 517200 +- 1400, (60.28 +- 0.10) %
                +-> Remaining locations to test: 202 +- 27, (0.024 +- 0.003) %
                    Location candidates: 202 +- 27


cmake --build . -- empiricalFalsePositiveRate &&
for i in 1 10 100 1000; do
     src/benchmarks/empiricalFalsePositiveRate $(( i * 1000 * 1000 * 1000 )) 2>&1 |
         tee empiricalFalsePositiveRate-${i}Gb.log
done
*/
