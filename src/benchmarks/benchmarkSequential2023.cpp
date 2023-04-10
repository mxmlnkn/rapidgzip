/**
 * @file While the other benchmarks test varying situations and parameters for single components,
 *       this file is a collection of benchmarks for selected (best) versions for each component
 *       to get an overview of the current state of pragzip.
 */

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <AffinityHelpers.hpp>
#include <BitReader.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <blockfinder/Uncompressed.hpp>
#include <common.hpp>
#include <DecodedData.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <pragzip.hpp>
#include <Statistics.hpp>
#include <TestHelpers.hpp>
#include <ThreadPool.hpp>


constexpr size_t REPEAT_COUNT{ 100 };


[[nodiscard]] size_t
getOpenFileHandleCount()
{
    std::filesystem::directory_iterator entries( "/proc/self/fd" );
    return std::distance( begin( entries ), end( entries ) );
}


[[nodiscard]] std::string
formatBandwidth( const std::vector<double>& times,
                 const size_t               nBytes )
{
    std::vector<double> bandwidths( times.size() );
    std::transform( times.begin(), times.end(), bandwidths.begin(),
                    [nBytes] ( double time ) { return static_cast<double>( nBytes ) / time / 1e6; } );
    Statistics<double> bandwidthStats{ bandwidths };

    /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
     * physically possible but many noisy influences can slow things down, i.e., the minimum time is
     * the value closest to be free of noise. */
    std::stringstream result;
    result << "( min: " << bandwidthStats.min << ", " << bandwidthStats.formatAverageWithUncertainty()
           << ", max: " << bandwidthStats.max << " ) MB/s";
    return result.str();
}


using BenchmarkFunction = std::function<std::pair</* duration */ double, /* checksum */ uint64_t>()>;


[[nodiscard]] std::vector<double>
repeatBenchmarks( const BenchmarkFunction& toMeasure,
                  const size_t             repeatCount = REPEAT_COUNT )
{
    std::cout << "Repeating benchmarks " << repeatCount << " times ... " << std::flush;
    const auto tStart = now();

    std::optional<uint64_t> checksum;
    std::vector<double> times( repeatCount );
    for ( auto& time : times ) {
        const auto [measuredTime, calculatedChecksum] = toMeasure();
        time = measuredTime;

        if ( !checksum ) {
            checksum = calculatedChecksum;
        } else if ( *checksum != calculatedChecksum ) {
            throw std::runtime_error( "Indeterministic or wrong result observed!" );
        }
    }

    std::cout << "Done (" << duration( tStart ) << " s)\n";
    return times;
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReader( const std::vector<char>& data,
                    const uint8_t            nBits )
{
    using pragzip::BitReader;
    BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    const auto t0 = now();  // NOLINT(clang-analyzer-deadcode.DeadStores)

    uint64_t sum = 0;
    try {
        /* Without unrolling ~1.4 GB/s and with unrolling ~1.8 GB/s! */
        #pragma GCC unroll 4
        while ( true ) {
            sum += bitReader.read( nBits );
        }
    } catch ( const typename BitReader::EndOfFileReached& ) {
        /* Ignore EOF exception. Checking for it in each loop is expensive! */
    }

    return { duration( t0 ), sum };
}


void
benchmarkBitReaderBitReads( const std::vector<uint8_t>& nBitsToTest )
{
    std::ofstream dataFile( "result-bitreader-reads.dat" );
    dataFile << "# 64-bit buffer LSB (gzip) order\n";
    dataFile << "# bitsPerReadCall dataSize/B runtime/s\n";

    for ( const auto nBits : nBitsToTest ) {
        /* Scale benchmark size with bits to get roughly equally long-running benchmarks and therefore also
         * roughly equally good error estimates. */
        std::vector<char> data( 2_Mi * nBits );
        for ( auto& x : data ) {
            x = static_cast<char>( rand() );
        }

        const auto times = repeatBenchmarks( [&] () { return benchmarkBitReader( data, nBits ); } );
        std::cout << "[BitReader::read " << static_cast<int>( nBits ) << " bits in loop] Decoded with "
                  << formatBandwidth( times, data.size() ) << "\n";

        for ( const auto time : times ) {
            dataFile << static_cast<int>( nBits ) << " " << data.size() << " " << time << std::endl;
        }
    }
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkUncompressedBlockFinder( const std::vector<char>& data )
{
    const auto t0 = now();

    using pragzip::BitReader;
    BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    uint64_t count{ 0 };
    while ( true ) {
        const auto& [min, max] = pragzip::blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader );
        if ( min == std::numeric_limits<size_t>::max() ) {
            break;
        }
        ++count;
        bitReader.seek( static_cast<long long int>( max ) + 1 );
    }

    return { duration( t0 ), count };
}


void
benchmarkFindUncompressedBlocks()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 32_Mi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    std::ofstream dataFile( "result-find-uncompressed.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    const auto times = repeatBenchmarks( [&] () { return benchmarkUncompressedBlockFinder( data ); } );
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Uncompressed block finder] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkDynamicBlockFinder( const std::vector<char>& data )
{
    const auto t0 = now();

    using pragzip::BitReader;
    BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    uint64_t count{ 0 };
    while ( true ) {
        const auto offset = pragzip::blockfinder::seekToNonFinalDynamicDeflateBlock( bitReader );
        if ( offset == std::numeric_limits<size_t>::max() ) {
            break;
        }
        ++count;
        bitReader.seek( static_cast<long long int>( offset ) + 1 );
    }

    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinder()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 4_Mi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return benchmarkDynamicBlockFinder( data ); } );

    std::ofstream dataFile( "result-find-dynamic.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder] " << formatBandwidth( times, data.size() ) << "\n";
}


class GzipWrapper
{
public:
    static constexpr auto WINDOW_SIZE = 32_Ki;

    enum class Format
    {
        AUTO,
        RAW,
        GZIP,
    };

public:
    explicit
    GzipWrapper( Format format = Format::AUTO )
    {
        m_stream.zalloc = Z_NULL;     /* used to allocate the internal state */
        m_stream.zfree = Z_NULL;      /* used to free the internal state */
        m_stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

        m_stream.avail_in = 0;        /* number of bytes available at next_in */
        m_stream.next_in = Z_NULL;    /* next input byte */

        m_stream.avail_out = 0;       /* remaining free space at next_out */
        m_stream.next_out = Z_NULL;   /* next output byte will go here */

        m_stream.msg = nullptr;

        int windowBits = 15;  // maximum value corresponding to 32kiB;
        switch ( format )
        {
        case Format::AUTO:
            windowBits += 32;
            break;

        case Format::RAW:
            windowBits *= -1;
            break;

        case Format::GZIP:
            windowBits += 16;
            break;
        }

        auto ret = inflateInit2( &m_stream, windowBits );
        if ( ret != Z_OK ) {
            throw std::domain_error( std::to_string( ret ) );
        }
    }

    GzipWrapper( const GzipWrapper& ) = delete;
    GzipWrapper( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper& ) = delete;

    ~GzipWrapper()
    {
        inflateEnd( &m_stream );
    }

    bool
    tryInflate( unsigned char const* compressed,
                size_t               compressedSize,
                size_t               bitOffset = 0 )
    {
        if ( inflateReset( &m_stream ) != Z_OK ) {
            return false;
        }

        if ( ceilDiv( bitOffset, CHAR_BIT ) >= compressedSize ) {
            return false;
        }

        const auto bitsToSeek = bitOffset % CHAR_BIT;
        const auto byteOffset = bitOffset / CHAR_BIT;
        m_stream.avail_in = compressedSize - byteOffset;
        /* const_cast should be safe because zlib presumably only uses this in a const manner.
         * I'll probably have to roll out my own deflate decoder anyway so I might be able
         * to change this bothersome interface. */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        m_stream.next_in = const_cast<unsigned char*>( compressed ) + byteOffset;

        const auto outputPreviouslyAvailable = std::min( 8_Ki, m_outputBuffer.size() );
        m_stream.avail_out = outputPreviouslyAvailable;
        m_stream.next_out = m_outputBuffer.data();

        /* Using std::fill leads to 10x slowdown -.-!!! Memset probably better.
         * Well, or not necessary at all because we are not interested in the specific output values anyway.
         * std::memset only incurs a 30% slowdown. */
        //std::fill( m_window.begin(), m_window.end(), '\0' );
        //std::memset( m_window.data(), 0, m_window.size() );
        if ( bitsToSeek > 0 ) {
            m_stream.next_in += 1;
            m_stream.avail_in -= 1;

            auto errorCode = inflatePrime( &m_stream, static_cast<int>( 8U - bitsToSeek ),
                                           compressed[byteOffset] >> bitsToSeek );
            if ( errorCode != Z_OK ) {
                return false;
            }
        }

        auto errorCode = inflateSetDictionary( &m_stream, m_window.data(), m_window.size() );
        if ( errorCode != Z_OK ) {}

        errorCode = inflate( &m_stream, Z_BLOCK );
        if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
            return false;
        }

        if ( errorCode == Z_STREAM_END ) {
            /* We are not interested in blocks close to the stream end.
             * Because either this is close to the end and no parallelization is necessary,
             * or this means the gzip file is compromised of many gzip streams, which are a tad
             * easier to search for than raw deflate streams! */
            return false;
        }
        const auto nBytesDecoded = outputPreviouslyAvailable - m_stream.avail_out;
        return nBytesDecoded >= outputPreviouslyAvailable;
    }

private:
    z_stream m_stream{};
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32_Ki, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64_Mi );
};


[[nodiscard]] std::pair<double, uint64_t>
findDeflateBlocksZlib( const std::vector<char>& buffer )
{
    const auto t0 = now();

    uint64_t count{ 0 };
    GzipWrapper gzip( GzipWrapper::Format::RAW );

    for ( size_t offset = 0; offset <= ( buffer.size() - 1 ) * sizeof( buffer[0] ) * CHAR_BIT; ++offset ) {
        if ( gzip.tryInflate( reinterpret_cast<unsigned char const*>( buffer.data() ),
                              buffer.size() * sizeof( buffer[0] ),
                              offset ) ) {
            ++count;
        }
    }

    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinderZlib()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 32_Ki );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return findDeflateBlocksZlib( data ); } );

    std::ofstream dataFile( "result-find-dynamic-zlib.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder using zlib] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
findDeflateBlocksPragzip( const std::vector<char>& buffer )
{
    using DeflateBlock = pragzip::deflate::Block<>;

    const auto nBitsToTest = buffer.size() * CHAR_BIT;
    pragzip::BitReader bitReader( std::make_unique<BufferViewFileReader>( buffer ) );

    const auto t0 = now();

    uint64_t count{ 0 };

    pragzip::deflate::Block block;
    for ( size_t offset = 0; offset <= nBitsToTest; ++offset ) {
        bitReader.seek( static_cast<long long int>( offset ) );
        try
        {
            auto error = block.readHeader</* count last block as error */ true>( bitReader );
            if ( ( error != pragzip::Error::NONE )
                 || ( block.compressionType() == DeflateBlock::CompressionType::DYNAMIC_HUFFMAN ) ) {
                continue;
            }

            ++count;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }
    }
    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinderPragzip()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 512_Ki );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return findDeflateBlocksPragzip( data ); } );

    std::ofstream dataFile( "result-find-dynamic-pragzip.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder using pragzip] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
findDeflateBlocksPragzipLUT( const std::vector<char>& buffer )
{
    using DeflateBlock = pragzip::deflate::Block<>;
    constexpr auto CACHED_BIT_COUNT = pragzip::blockfinder::OPTIMAL_NEXT_DEFLATE_LUT_SIZE;

    /* Testing a dozen positions less should not make a difference but avoid EOF exceptions. */
    const auto nBitsToTest = buffer.size() * CHAR_BIT - CACHED_BIT_COUNT;
    pragzip::BitReader bitReader( std::make_unique<BufferViewFileReader>( buffer ) );

    const auto t0 = now();

    uint64_t count{ 0 };

    auto bitBufferForLUT = bitReader.peek<CACHED_BIT_COUNT>();

    pragzip::deflate::Block block;
    for ( size_t offset = 0; offset <= nBitsToTest; ) {
        const auto nextPosition =
            pragzip::blockfinder::NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT<CACHED_BIT_COUNT>[bitBufferForLUT];

        const auto bitsToLoad = std::max( static_cast<std::decay_t<decltype( nextPosition )> >( 1 ), nextPosition );
        bitBufferForLUT >>= bitsToLoad;
        bitBufferForLUT |= bitReader.read( bitsToLoad )
                           << static_cast<uint8_t>( CACHED_BIT_COUNT - bitsToLoad );
        if ( nextPosition > 0 ) {
            offset += nextPosition;
            continue;
        }

        bitReader.seek( static_cast<long long int>( offset ) );
        try
        {
            auto error = block.readHeader</* count last block as error */ true>( bitReader );
            if ( ( error != pragzip::Error::NONE )
                 || ( block.compressionType() == DeflateBlock::CompressionType::DYNAMIC_HUFFMAN ) ) {
                continue;
            }

            ++count;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }

        ++offset;
    }
    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinderPragzipLUT()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 2_Mi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return findDeflateBlocksPragzipLUT( data ); } );

    std::ofstream dataFile( "result-find-dynamic-pragzip-skip-lut.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder using skip LUT and pragzip] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkFileReader( const std::string& path,
                     const size_t       chunkSize )
{
    const auto t0 = now();

    StandardFileReader fileReader( path );

    std::vector<char> buffer( chunkSize );
    uint64_t checksum{ 0 };
    while ( true ) {
        const auto nBytesRead = fileReader.read( buffer.data(), buffer.size() );
        checksum += nBytesRead + buffer[buffer.size() / 2];
        if ( nBytesRead == 0 ) {
            break;
        }
    }

    return { duration( t0 ), checksum };
}


/* Create a temporary file for benchmarking that is cleaned up with RAII. */
struct TemporaryFile
{
    explicit
    TemporaryFile( const size_t requestedSize ) :
        size( requestedSize )
    {
        std::ofstream file( path );
        std::vector<char> dataToWrite( 1_Mi );
        for ( auto& x : dataToWrite ) {
            x = static_cast<char>( rand() );
        }
        for ( size_t nBytesWritten = 0; nBytesWritten < size; nBytesWritten += dataToWrite.size() ) {
            file.write( dataToWrite.data(), static_cast<std::streamsize>( dataToWrite.size() ) );
        }
        file.close();
    }

    ~TemporaryFile()
    {
        std::filesystem::remove( path );
    }

    TemporaryFile( const TemporaryFile& ) = delete;
    TemporaryFile( TemporaryFile&& ) = delete;
    TemporaryFile& operator=( const TemporaryFile& ) = delete;
    TemporaryFile& operator=( TemporaryFile&& ) = delete;

    const std::string path{ "/dev/shm/pragzip-benchmark-random-file.dat" };
    const size_t size;
};


void
benchmarkFileReader()
{
    TemporaryFile temporaryFile( 1_Gi );

    const auto times = repeatBenchmarks( [&] () { return benchmarkFileReader( temporaryFile.path,
                                                                              pragzip::BitReader::IOBUF_SIZE ); } );

    std::ofstream dataFile( "result-read-file.dat" );
    dataFile << "# dataSize/B chunkSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << temporaryFile.size << " " << pragzip::BitReader::IOBUF_SIZE << " " << time << "\n";
    }

    std::cout << "[File Reading] " << formatBandwidth( times, temporaryFile.size ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkFileReaderParallel( ThreadPool&        threadPool,
                             const std::string& path,
                             const size_t       chunkSize )
{
    const auto t0 = now();

    const auto shareableFileReader = std::make_unique<SharedFileReader>( std::make_unique<StandardFileReader>( path ) );

    const auto readStrided =
        [chunkSize]
        ( const UniqueFileReader& fileReader,
          const size_t            offset,
          const size_t            stride ) -> uint64_t
        {
            std::vector<char> buffer( chunkSize );

            uint64_t checksum{ 0 };
            const auto fileSize = fileReader->size();
            for ( auto currentOffset = offset; currentOffset < fileSize; currentOffset += stride ) {
                fileReader->seek( static_cast<long long int>( currentOffset ) );
                const auto nBytesRead = fileReader->read( buffer.data(), buffer.size() );
                checksum += nBytesRead + buffer[buffer.size() / 2];
                if ( nBytesRead == 0 ) {
                    break;
                }
            }
            return checksum;
        };

    std::vector<std::future<uint64_t> > results;
    const auto parallelism = threadPool.capacity();
    for ( size_t i = 0; i < parallelism; ++i ) {
        auto sharedFileReader = shareableFileReader->clone();
        results.emplace_back( threadPool.submit(
            [chunkSize, i, parallelism, fileReader = std::move( sharedFileReader ), &readStrided] () mutable {
                return readStrided( fileReader, i * chunkSize, parallelism * chunkSize );
            }
        ) );
    }

    uint64_t checksum{ 0 };
    for ( auto& result : results ) {
        checksum += result.get();
    }

    return { duration( t0 ), checksum };
}


[[nodiscard]] std::vector<double>
benchmarkFileReaderParallelRepeatedly( const size_t                     fileSize,
                                       const size_t                     threadCount,
                                       const ThreadPool::ThreadPinning& threadPinning )
{
    TemporaryFile temporaryFile( fileSize );

    ThreadPool threadPool( threadCount, threadPinning );

    auto times = repeatBenchmarks( [&] () {
        return benchmarkFileReaderParallel( threadPool, temporaryFile.path, pragzip::BitReader::IOBUF_SIZE );
    }, /* repeat count */ 50 );

    return times;
}


[[nodiscard]] size_t
getCoreTopDown( size_t index,
                size_t coreCount )
{
    /* Note that to be 100% perfect, we would have to use the hwloc information about NUMA nodes and cache hierarchy.
     * But, for the systems I'm interested in, spreading the pinning apart as far as possible is sufficient.
     * E.g., my Ryzen 3700X 12/24-core has a hierarchy of 1 NUMA node with 24 process units, containing 4 L3 caches
     * used by 3 cores / 6 processing units each. */
    std::vector<size_t> factors;
    for ( auto remainder = coreCount; remainder > 1; ) {
        for ( size_t factor = 2; factor <= remainder; ++factor ) {
            if ( remainder % factor == 0 ) {
                factors.emplace_back( factor );
                break;
            }
        }
        remainder /= factors.back();
    }

    if ( factors.empty() ) {
        throw std::logic_error( "There should be prime factors!" );
    }

    if ( factors.front() != 2 ) {
        throw std::invalid_argument( "Assumed an even number of virtual cores because of SMT!" );
    }

    factors.erase( factors.begin() );

    const auto usesSMT = index >= coreCount / 2;

    auto id = index % ( coreCount / 2 );
    size_t coreId{ 0 };
    size_t stride{ coreCount / 2 };
    for ( auto factor : factors ) {
        stride /= factor;
        coreId += ( id % factor ) * stride;
        id /= factor;
    }

    return usesSMT ? coreCount / 2 + coreId : coreId;
}


void
benchmarkFileReaderParallel()
{
    const auto coreCount = availableCores();
    std::cout << "Available core count: " << coreCount << "\n";

    const size_t fileSize{ coreCount * 64_Mi };

    const std::vector<size_t> threadCounts = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        10, 12, 14, 16,      // delta = 2
        20, 24, 28, 32,      // delta = 4
        40, 48, 56, 64,      // delta = 8
        80, 96, 112, 128,    // delta = 16
        160, 192, 224, 256,  // delta = 32
    };

    enum class PinningScheme {
        NONE,
        SEQUENTIAL,
        STRIDED,
        RECURSIVE,
    };

    REQUIRE_EQUAL( getCoreTopDown( 0, 16 ), size_t( 0 ) );
    REQUIRE_EQUAL( getCoreTopDown( 1, 16 ), size_t( 4 ) );
    REQUIRE_EQUAL( getCoreTopDown( 2, 16 ), size_t( 2 ) );
    REQUIRE_EQUAL( getCoreTopDown( 3, 16 ), size_t( 6 ) );
    REQUIRE_EQUAL( getCoreTopDown( 4, 16 ), size_t( 1 ) );
    REQUIRE_EQUAL( getCoreTopDown( 5, 16 ), size_t( 5 ) );

    REQUIRE_EQUAL( getCoreTopDown( 0, 24 ), size_t( 0 ) );
    REQUIRE_EQUAL( getCoreTopDown( 1, 24 ), size_t( 6 ) );
    REQUIRE_EQUAL( getCoreTopDown( 2, 24 ), size_t( 3 ) );
    REQUIRE_EQUAL( getCoreTopDown( 3, 24 ), size_t( 9 ) );
    REQUIRE_EQUAL( getCoreTopDown( 4, 24 ), size_t( 1 ) );
    REQUIRE_EQUAL( getCoreTopDown( 5, 24 ), size_t( 7 ) );
    REQUIRE_EQUAL( getCoreTopDown( 6, 24 ), size_t( 4 ) );
    REQUIRE_EQUAL( getCoreTopDown( 7, 24 ), size_t( 10 ) );
    REQUIRE_EQUAL( getCoreTopDown( 8, 24 ), size_t( 2 ) );
    REQUIRE_EQUAL( getCoreTopDown( 9, 24 ), size_t( 8 ) );
    REQUIRE_EQUAL( getCoreTopDown( 10, 24 ), size_t( 5 ) );
    REQUIRE_EQUAL( getCoreTopDown( 11, 24 ), size_t( 11 ) );
    REQUIRE_EQUAL( getCoreTopDown( 12, 24 ), size_t( 12 ) );

    const auto toString =
        [] ( const PinningScheme scheme )
        {
            switch ( scheme )
            {
            case PinningScheme::NONE:
                return "No pinning";
            case PinningScheme::SEQUENTIAL:
                return "Sequential pinning";
            case PinningScheme::STRIDED:
                return "Strided pinning";
            case PinningScheme::RECURSIVE:
                return "Recursive pinning";
            }
            return "";
        };

    const auto toFileSuffix =
        [] ( const PinningScheme scheme )
        {
            switch ( scheme )
            {
            case PinningScheme::NONE:
                return "no-pinning";
            case PinningScheme::SEQUENTIAL:
                return "sequential-pinning";
            case PinningScheme::STRIDED:
                return "strided-pinning";
            case PinningScheme::RECURSIVE:
                return "recursive-pinning";
            }
            return "";
        };

    for ( const auto scheme : { PinningScheme::NONE, PinningScheme::SEQUENTIAL, PinningScheme::RECURSIVE } ) {
        std::stringstream fileName;
        fileName << "result-read-file-parallel-" << toFileSuffix( scheme ) << ".dat";
        std::ofstream dataFile( fileName.str() );
        dataFile << "# threadCount dataSize/B chunkSize/B runtime/s\n";

        for ( const auto threadCount : threadCounts ) {
            if ( threadCount > coreCount ) {
                continue;
            }

            ThreadPool::ThreadPinning threadPinning;
            switch ( scheme )
            {
            case PinningScheme::NONE:
                break;

            case PinningScheme::SEQUENTIAL:
                for ( size_t i = 0; i < threadCount; ++i ) {
                    threadPinning.emplace( i, i );
                }
                break;

            case PinningScheme::STRIDED:
                {
                    const auto stride = 1U << static_cast<size_t>(
                        std::ceil( std::log2( static_cast<double>( coreCount )
                                              / static_cast<double>( threadCount ) ) ) );
                    uint32_t coreId{ 0 };
                    for ( size_t i = 0; i < threadCount; ++i ) {
                        threadPinning.emplace( i, coreId );
                        coreId += stride;
                        if ( coreId >= coreCount ) {
                            coreId = coreId % coreCount + 1;
                        }
                    }
                }
                break;

            case PinningScheme::RECURSIVE:
                {
                    std::unordered_set<size_t> coreIds;
                    for ( size_t i = 0; i < threadCount; ++i ) {
                        const auto coreId = getCoreTopDown( i, coreCount );
                        coreIds.emplace( coreId );
                        threadPinning.emplace( i, coreId );
                    }

                    if ( coreIds.size() != threadCount ) {
                        throw std::logic_error( "Duplicate core IDs found in mapping!" );
                    }
                }
                break;
            }

            const auto times = benchmarkFileReaderParallelRepeatedly( fileSize, threadCount, threadPinning );
            for ( const auto time : times ) {
                dataFile << threadCount << " " << fileSize << " " << pragzip::BitReader::IOBUF_SIZE << " " << time
                         << std::endl;
            }

            std::cout << "[Parallel File Reading (" << toString( scheme ) << ")] Using " << threadCount << " threads "
                      << formatBandwidth( times, fileSize ) << "\n";
        }
    }
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkCountNewlines( const std::vector<char>& data )
{
    const auto t0 = now();

    const auto count = countNewlines( { data.data(), data.size() } );

    return { duration( t0 ), count };
}


void
benchmarkCountNewlines()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<char> data( 1_Gi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    const auto t1 = now();
    std::cout << "Done (" << duration( t0, t1 ) << " s)\n";

    const auto times = repeatBenchmarks( [&] () { return benchmarkCountNewlines( data ); } );

    std::ofstream dataFile( "result-count-newlines.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Count newlines] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkApplyWindow( std::vector<uint16_t>       data,
                      const std::vector<uint8_t>& window )
{
    pragzip::deflate::DecodedData decoded;
    decoded.dataWithMarkers.emplace_back( std::move( data ) );

    const auto t0 = now();
    decoded.applyWindow( { window.data(), window.size() } );
    const auto checksum = decoded.data.front().at( decoded.data.front().size() / 2 );

    return { duration( t0 ), checksum };
}


void
benchmarkApplyWindow()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... " << std::flush;
    std::vector<uint16_t> data( 32_Mi );
    for ( auto& x : data ) {
        do {
            x = static_cast<uint16_t>( rand() );
        } while ( ( x > std::numeric_limits<uint8_t>::max() ) && ( x < pragzip::deflate::MAX_WINDOW_SIZE ) );
    }
    std::vector<uint8_t> window( 32_Ki );
    for ( auto& x : window ) {
        x = static_cast<uint8_t>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&] () { return benchmarkApplyWindow( data, window ); } );

    std::ofstream dataFile( "result-apply-window.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Apply window] Output(!) bandwidth of 8-bit symbols (input is 16-bit symbols): "
              << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkWrite( const std::string&       filePath,
                const std::vector<char>& data,
                const size_t             chunkSize )
{
    /* ftruncate( fd, 0 ) is not sufficient!!! At least not without closing and reopening the file it seems!
     * It will still yield the same results as a preallocated file! */
    if ( fileExists( filePath ) ) {
        std::filesystem::remove( filePath );
    }
    unique_file_descriptor ufd( ::open( filePath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR ) );

    const auto t0 = now();
    uint64_t sum{ 0 };
    for ( size_t i = 0; i < data.size(); i += chunkSize ) {
        const auto sizeToWrite = std::min( chunkSize, data.size() - i );
        writeAllToFd( *ufd, &data[i], sizeToWrite );
        sum += sizeToWrite;
    }

    ufd.close();
    return { duration( t0 ), sum };
}


void
benchmarkWrite()
{
    const std::vector<char> data( 1_Gi, 1 );
    const std::string filePath( "/dev/shm/pragzip-benchmark-random-file.dat" );
    const auto times = repeatBenchmarks( [&] () { return benchmarkWrite( filePath, data, data.size() ); } );

    std::ofstream dataFile( "result-file-write.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Write to File] Output bandwidth : " << formatBandwidth( times, data.size() ) << "\n";

    if ( fileExists( filePath ) ) {
        std::filesystem::remove( filePath );
    }
}


int
main()
{
    benchmarkWrite();

    benchmarkDynamicBlockFinderPragzipLUT();
    benchmarkDynamicBlockFinderPragzip();
    benchmarkDynamicBlockFinderZlib();
    benchmarkDynamicBlockFinder();
    benchmarkFindUncompressedBlocks();

    benchmarkApplyWindow();

    benchmarkCountNewlines();

    benchmarkFileReaderParallel();
    //benchmarkFileReader();

    /* This is nice for testing. Probably should add this to the tests or maybe run this benchmark also as a test? */
    //benchmarkBitReaderBitReads( { 1, 2, 8, 15, 16 } );
    std::vector<uint8_t> nBitsToTest( 32 );
    std::iota( nBitsToTest.begin(), nBitsToTest.end(), 1 );
    benchmarkBitReaderBitReads( nBitsToTest );

    return 0;
}


/*
cmake --build . -- benchmarkSequential2023 && src/benchmarks/benchmarkSequential2023 2>&1 | tee benchmarks2023.log
sed -r '/[.]{3}/d; /Open file handles/d' benchmarks2023.log

[Dynamic block finder using skip LUT and pragzip] ( min: 20.6126, 22.66 +- 0.25, max: 22.9213 ) MB/s
[Dynamic block finder using pragzip] ( min: 3.95542, 4.11 +- 0.03, max: 4.15403 ) MB/s
[Dynamic block finder using zlib] ( min: 0.165136, 0.1766 +- 0.0021, max: 0.180283 ) MB/s
[Dynamic block finder] ( min: 61.9047, 67.1 +- 1.0, max: 69.1426 ) MB/s
[Uncompressed block finder] ( min: 383.5, 413 +- 9, max: 428.144 ) MB/s
[Apply window] Output(!) bandwidth of 8-bit symbols (input is 16-bit symbols): ( min: 974.843, 1290 +- 70, max: 1445.49 ) MB/s
[Count newlines] ( min: 7430.55, 12300 +- 500, max: 12698.3 ) MB/s

[Parallel File Reading (No pinning)] Using 1 threads ( min: 7765.86, 9700 +- 300, max: 10033 ) MB/s
[Parallel File Reading (No pinning)] Using 2 threads ( min: 10956.6, 16100 +- 800, max: 16505.7 ) MB/s
[Parallel File Reading (No pinning)] Using 3 threads ( min: 14365.3, 20400 +- 1000, max: 21179.6 ) MB/s
[Parallel File Reading (No pinning)] Using 4 threads ( min: 17296.3, 23700 +- 1100, max: 24511 ) MB/s
[Parallel File Reading (No pinning)] Using 5 threads ( min: 18336.3, 25200 +- 1200, max: 26077.1 ) MB/s
[Parallel File Reading (No pinning)] Using 6 threads ( min: 19787.4, 26000 +- 1100, max: 26888.7 ) MB/s
[Parallel File Reading (No pinning)] Using 7 threads ( min: 19850.1, 26000 +- 1300, max: 27042.3 ) MB/s
[Parallel File Reading (No pinning)] Using 8 threads ( min: 21734.9, 26200 +- 1000, max: 27366.7 ) MB/s
[Parallel File Reading (No pinning)] Using 10 threads ( min: 22015.8, 26800 +- 1000, max: 27749.3 ) MB/s
[Parallel File Reading (No pinning)] Using 12 threads ( min: 23215.6, 27300 +- 900, max: 28396.9 ) MB/s
[Parallel File Reading (No pinning)] Using 14 threads ( min: 21293.3, 26800 +- 1100, max: 27976 ) MB/s
[Parallel File Reading (No pinning)] Using 16 threads ( min: 22175.6, 26900 +- 1000, max: 28101.7 ) MB/s
[Parallel File Reading (No pinning)] Using 20 threads ( min: 20362.3, 26900 +- 1100, max: 27905 ) MB/s
[Parallel File Reading (No pinning)] Using 24 threads ( min: 19845.3, 26600 +- 1200, max: 27761.1 ) MB/s

[Parallel File Reading (Sequential pinning)] Using 1 threads ( min: 7899.77, 9680 +- 290, max: 9941.72 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 2 threads ( min: 13419.5, 17000 +- 600, max: 17394.8 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 3 threads ( min: 18696.2, 21600 +- 600, max: 22115.5 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 4 threads ( min: 18118.7, 23200 +- 900, max: 23947.2 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 5 threads ( min: 20274.8, 24400 +- 800, max: 25125.9 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 6 threads ( min: 20816.7, 24800 +- 900, max: 25595.8 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 7 threads ( min: 22132.4, 25700 +- 900, max: 26505.8 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 8 threads ( min: 19537.3, 25800 +- 1600, max: 26989.2 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 10 threads ( min: 20380.3, 26200 +- 1600, max: 27742.1 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 12 threads ( min: 23573.8, 27400 +- 800, max: 28322.4 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 14 threads ( min: 23802.9, 27100 +- 700, max: 27977.4 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 16 threads ( min: 23330.2, 27000 +- 800, max: 27909.9 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 20 threads ( min: 21252, 27000 +- 900, max: 27680.5 ) MB/s
[Parallel File Reading (Sequential pinning)] Using 24 threads ( min: 21097.4, 26500 +- 1200, max: 27878.3 ) MB/s

[Parallel File Reading (Recursive pinning)] Using 1 threads ( min: 7793.84, 9610 +- 280, max: 9824.07 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 2 threads ( min: 10778.3, 15900 +- 800, max: 16180.5 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 3 threads ( min: 13518.8, 20600 +- 1100, max: 21217.6 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 4 threads ( min: 15992.3, 23300 +- 1200, max: 24158.6 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 5 threads ( min: 18268, 24900 +- 1200, max: 26085.9 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 6 threads ( min: 18990.9, 25200 +- 1300, max: 26936.9 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 7 threads ( min: 20661.6, 26000 +- 1000, max: 27023.8 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 8 threads ( min: 21391.7, 26600 +- 1100, max: 27636.9 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 10 threads ( min: 21839.7, 27000 +- 1000, max: 27966.3 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 12 threads ( min: 23964.2, 27200 +- 1000, max: 28509.7 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 14 threads ( min: 23641.5, 27200 +- 1000, max: 28102.7 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 16 threads ( min: 22968.6, 26900 +- 1100, max: 28171.2 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 20 threads ( min: 21711.2, 26900 +- 1000, max: 27753.3 ) MB/s
[Parallel File Reading (Recursive pinning)] Using 24 threads ( min: 20829, 26800 +- 1100, max: 27786.1 ) MB/s

[BitReader::read 1 bits in loop] Decoded with ( min: 130.902, 157 +- 3, max: 160.084 ) MB/s
[BitReader::read 2 bits in loop] Decoded with ( min: 278.868, 309 +- 6, max: 315.761 ) MB/s
[BitReader::read 3 bits in loop] Decoded with ( min: 401.837, 444 +- 10, max: 461.124 ) MB/s
[BitReader::read 4 bits in loop] Decoded with ( min: 485.851, 585 +- 17, max: 601.181 ) MB/s
[BitReader::read 5 bits in loop] Decoded with ( min: 587.697, 696 +- 20, max: 723.905 ) MB/s
[BitReader::read 6 bits in loop] Decoded with ( min: 655.309, 834 +- 20, max: 855.084 ) MB/s
[BitReader::read 7 bits in loop] Decoded with ( min: 625.689, 910 +- 50, max: 965.365 ) MB/s
[BitReader::read 8 bits in loop] Decoded with ( min: 778.256, 1100 +- 40, max: 1130.15 ) MB/s
[BitReader::read 9 bits in loop] Decoded with ( min: 874.205, 1150 +- 30, max: 1187.62 ) MB/s
[BitReader::read 10 bits in loop] Decoded with ( min: 936.633, 1200 +- 60, max: 1266.75 ) MB/s
[BitReader::read 11 bits in loop] Decoded with ( min: 1001.61, 1290 +- 60, max: 1347.6 ) MB/s
[BitReader::read 12 bits in loop] Decoded with ( min: 1131.49, 1420 +- 50, max: 1466.57 ) MB/s
[BitReader::read 13 bits in loop] Decoded with ( min: 913.561, 1480 +- 100, max: 1538.22 ) MB/s
[BitReader::read 14 bits in loop] Decoded with ( min: 1265.8, 1600 +- 50, max: 1633.45 ) MB/s
[BitReader::read 15 bits in loop] Decoded with ( min: 1159.89, 1670 +- 90, max: 1720.81 ) MB/s
[BitReader::read 16 bits in loop] Decoded with ( min: 1724.22, 1930 +- 30, max: 1972.57 ) MB/s
[BitReader::read 17 bits in loop] Decoded with ( min: 1426.6, 1820 +- 70, max: 1891.34 ) MB/s
[BitReader::read 18 bits in loop] Decoded with ( min: 1670.77, 1940 +- 50, max: 1983.86 ) MB/s
[BitReader::read 19 bits in loop] Decoded with ( min: 1658.15, 1970 +- 50, max: 2030.54 ) MB/s
[BitReader::read 20 bits in loop] Decoded with ( min: 1421.19, 2060 +- 60, max: 2109.23 ) MB/s
[BitReader::read 21 bits in loop] Decoded with ( min: 1977.52, 2100 +- 30, max: 2149.96 ) MB/s
[BitReader::read 22 bits in loop] Decoded with ( min: 2086.99, 2184 +- 28, max: 2224.98 ) MB/s
[BitReader::read 23 bits in loop] Decoded with ( min: 1866.89, 2210 +- 60, max: 2269.94 ) MB/s
[BitReader::read 24 bits in loop] Decoded with ( min: 1921.59, 2370 +- 60, max: 2428.28 ) MB/s
[BitReader::read 25 bits in loop] Decoded with ( min: 1786, 2320 +- 80, max: 2390.14 ) MB/s
[BitReader::read 26 bits in loop] Decoded with ( min: 1469.37, 2400 +- 120, max: 2457.7 ) MB/s
[BitReader::read 27 bits in loop] Decoded with ( min: 1682.94, 2380 +- 130, max: 2488.32 ) MB/s
[BitReader::read 28 bits in loop] Decoded with ( min: 2110.26, 2500 +- 70, max: 2576.88 ) MB/s
[BitReader::read 29 bits in loop] Decoded with ( min: 2363.93, 2530 +- 40, max: 2582.87 ) MB/s
[BitReader::read 30 bits in loop] Decoded with ( min: 2332.82, 2560 +- 60, max: 2635.76 ) MB/s
[BitReader::read 31 bits in loop] Decoded with ( min: 2179.75, 2600 +- 50, max: 2660.19 ) MB/s
[BitReader::read 32 bits in loop] Decoded with ( min: 2385.62, 3150 +- 120, max: 3246.73 ) MB/s
*/
