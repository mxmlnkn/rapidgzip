#pragma once

#include <algorithm>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <AffinityHelpers.hpp>
#include <BlockMap.hpp>
#include <common.hpp>
#include <filereader/FileReader.hpp>
#include <filereader/Shared.hpp>

#ifdef WITH_PYTHON_SUPPORT
    #include <filereader/Python.hpp>
    #include <filereader/Standard.hpp>
#endif

#include "crc32.hpp"
#include "GzipChunkFetcher.hpp"
#include "GzipBlockFinder.hpp"
#include "gzip.hpp"
#include "IndexFileFormat.hpp"


namespace rapidgzip
{
/**
 * @note Calls to this class are not thread-safe! Even though they use threads to evaluate them in parallel.
 */
template<typename T_ChunkData = ChunkData,
         bool ENABLE_STATISTICS = false>
class ParallelGzipReader final :
    public FileReader

{
public:
    using ChunkData = T_ChunkData;
    /**
     * The fetching strategy should support parallelization via prefetching for sequential accesses while
     * avoiding a lot of useless prefetches for random or multi-stream sequential accesses like those occuring
     * via ratarmount.
     * The fetching strategy does not have to and also should not account for backward and strided accesses
     * because the prefetch and cache units are very large and striding or backward accessing over multiple
     * megabytes should be extremely rare.
     */
    using ChunkFetcher = rapidgzip::GzipChunkFetcher<FetchingStrategy::FetchMultiStream, ChunkData, ENABLE_STATISTICS>;
    using BlockFinder = typename ChunkFetcher::BlockFinder;
    using BitReader = rapidgzip::BitReader;
    using WriteFunctor = std::function<void ( const std::shared_ptr<ChunkData>&, size_t, size_t )>;

public:
    /**
     * Quick benchmarks for spacing on AMD Ryzen 3900X 12-core.
     *
     * @verbatim
     * base64 /dev/urandom | head -c $(( 4 * 1024 * 1024 * 1024 )) > 4GiB-base64
     * gzip 4GiB-base64
     *
     * function benchmarkWc()
     * {
     *     for chunkSize in 128 $(( 1*1024 )) $(( 2*1024 )) $(( 4*1024 )) $(( 8*1024 )) $(( 16*1024 )) $(( 32*1024 )); do
     *         echo "Chunk Size: $chunkSize KiB"
     *         for i in $( seq 5 ); do
     *             src/tools/rapidgzip --chunk-size $chunkSize -P 0 -d -c "$1" 2>rapidgzip.log | wc -c
     *             grep "Decompressed in total" rapidgzip.log
     *         done
     *     done
     * }
     *
     * m rapidgzip
     * benchmarkWc 4GiB-base64.gz
     *
     *
     * spacing | bandwidth / (MB/s) | file read multiplier
     * --------+--------------------+----------------------
     * 128 KiB | ~1200              | 2.08337
     *   1 MiB | ~2500              | 1.13272
     *   2 MiB | ~2700              | 1.06601
     *   4 MiB | ~2800              | 1.03457
     *   8 MiB | ~2750              | 1.0169
     *  16 MiB | ~2600              | 1.00799
     *  32 MiB | ~2300              | 1.00429
     * @endverbatim
     *
     * For higher chunk sizes, the bandwidths become very unstable,
     * probably because even work division becomes a problem realtive to the file size.
     * Furthermore, caching behavior might worsen for larger chunk sizes.
     *
     * @verbatim
     * wget https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip
     * mkdir -p silesia && ( cd silesia && unzip ../silesia.zip )
     * tar -cf silesia.tar silesia/  # 211957760 B -> 212 MB, 203 MiB, gzip 66 MiB -> compression factor: 3.08
     * for (( i=0; i<40; ++i )); do cat 'silesia.tar'; done | pigz > 40xsilesia.tar.gz
     * m rapidgzip
     * benchmarkWc 40xsilesia.tar.gz
     *
     * spacing | bandwidth / (MB/s)
     * --------+--------------------
     * 128 KiB | ~1150
     *   1 MiB | ~1950
     *   2 MiB | ~2150
     *   4 MiB | ~2300
     *   8 MiB | ~2400
     *  16 MiB | ~2400
     *  32 MiB | ~2300
     * @endverbatim
     *
     * Beware, on 2xAMD EPYC CPU 7702, when decoding with more than 64 cores, the optimal is
     * at 2 MiB instead of 4-8 MiB! Maybe these are NUMA domain + caching issues combined?
     *
     * AMD Ryzen 3900X Caches:
     *  - L1: 64 kiB (50:50 instruction:cache) per core -> 768 kiB
     *  - L2: 512 kiB per core -> 6 MiB
     *  - L3: 64 MiB shared (~5.3 MiB per core)
     *
     * AMD EPYC CPU 7702:
     *  - L1: 64 kiB (50:50 instruction:cache) per core -> 4 MiB
     *  - L2: 512 kiB per core -> 32 MiB
     *  - L3: 256 MiB shared (4 MiB per core)
     *
     * -> That EPYC processor is the same generation Zen 2 and therefore has identical L1 and L2 caches
     *    and the L3 cache size is even higher, so it must be a NUMA issue.
     *
     * Non-compressible data is a special case because it only needs to do a memcpy.
     *
     * @verbatim
     * head -c $(( 4 * 1024 * 1024 * 1024 )) /dev/urandom > 4GiB-random
     * gzip 4GiB-random.gz
     * m rapidgzip
     * benchmarkWc 4GiB-random.gz
     *
     * spacing | bandwidth / (MB/s) | file read multiplier
     * --------+--------------------+----------------------
     * 128 KiB | ~1450              | 2.00049
     *   1 MiB | ~2650              | 1.12502
     *   2 MiB | ~2900              | 1.06253
     *   4 MiB | ~2900              | 1.03129
     *   8 MiB | ~3400              | 1.01567
     *  16 MiB | ~3300              | 1.00786
     *  32 MiB | ~2900              | 1.00396
     * @endverbatim
     *
     * Another set of benchmarks that exclude the bottleneck for writing the results to a pipe by
     * using the rapidgzip option --count-lines. Note that in contrast to pugz, it the decompressed
     * blocks are still processed in sequential order. Processing them out of order by providing
     * a map-reduce like interface might accomplish even more speedups.
     *
     * @verbatim
     * m rapidgzip
     * for chunkSize in 128 $(( 1*1024 )) $(( 2*1024 )) $(( 4*1024 )) $(( 8*1024 )) $(( 16*1024 )) $(( 32*1024 )); do
     *     echo "Chunk Size: $chunkSize KiB"
     *     for i in $( seq 5 ); do
     *         src/tools/rapidgzip --chunk-size $chunkSize -P 0 --count-lines 4GiB-base64.gz 2>rapidgzip.log
     *         grep "Decompressed in total" rapidgzip.log
     *     done
     * done
     *
     * spacing | bandwidth / (MB/s)
     * --------+--------------------
     * 128 KiB | ~1550
     *   1 MiB | ~3200
     *   2 MiB | ~3400
     *   4 MiB | ~3600
     *   8 MiB | ~3800
     *  16 MiB | ~3800
     *  32 MiB | ~3600
     * @endverbatim
     *
     * The factor 2 amount of read data can be explained with the BitReader always buffering 128 KiB!
     * Therefore if the work chunk is too small, it leads to this problem.
     * @note Beware the actual result of wc -l! With the wrong vmsplice usage, it returned random results
     *       for chunk sizes smaller than 4 MiB or even for higher chunk sizes with alternative malloc
     *       implementations like mimalloc.
     * @note The optimum at ~8 MiB for incompressible data vs ~4 MiB for base64 data with a compression
     *       ratio ~1.3 might be explainable with a roughly equal decompressed block size. In general,
     *       we would like the chunk size to be measured in decompressed data because the decompressed
     *       bandwidth is much more stable than the compressed bandwidth over a variety of data.
     * @todo We might be able to reduce this overhead by buffering up to untilOffset and then
     *       only increase the buffer in much smaller steps, e.g., 8 KiB.
     *       This might actually be easy to implement by making the BitReader chunk size adjustable.
     * @todo Possibly increase the chunk size to 4 or 8 MiB again after implementing an out of memory
     *       guard for high compression ratios so that CTU-13-Dataset.tar.gz can be decompressed with
     *       less than 30 GB of RAM!
     *       Rebenchmark of course whether it makes sense or not anymore. E.g., speeding up the block
     *       finder might enable smaller chunk sizes.
     */
    explicit
    ParallelGzipReader( UniqueFileReader fileReader,
                        size_t           parallelization = 0,
                        uint64_t         chunkSizeInBytes = 4_Mi ) :
        m_chunkSizeInBytes( std::max( 8_Ki, chunkSizeInBytes ) ),
        m_maxDecompressedChunkSize( 20U * m_chunkSizeInBytes ),
        m_sharedFileReader( ensureSharedFileReader( std::move( fileReader ) ) ),
        m_fetcherParallelization( parallelization == 0 ? availableCores() : parallelization ),
        m_startBlockFinder(
            [this] () {
                return std::make_unique<BlockFinder>(
                    UniqueFileReader( m_sharedFileReader->clone() ),
                    /* spacing in bytes */ m_chunkSizeInBytes );
            }
        )
    {
        m_sharedFileReader->setStatisticsEnabled( ENABLE_STATISTICS );
        if ( !m_bitReader.seekable() ) {
            throw std::invalid_argument( "Parallel BZ2 Reader will not work on non-seekable input like stdin (yet)!" );
        }
    }

#ifdef WITH_PYTHON_SUPPORT
    /* These constructor overloads are for easier construction in the Cython-interface.
     * For C++, the FileReader constructor would have been sufficient. */

    explicit
    ParallelGzipReader( int    fileDescriptor,
                        size_t parallelization = 0 ) :
        ParallelGzipReader( std::make_unique<StandardFileReader>( fileDescriptor ), parallelization )
    {}

    explicit
    ParallelGzipReader( const std::string& filePath,
                        size_t             parallelization = 0 ) :
        ParallelGzipReader( std::make_unique<StandardFileReader>( filePath ), parallelization )
    {}

    explicit
    ParallelGzipReader( PyObject* pythonObject,
                        size_t    parallelization = 0 ) :
        ParallelGzipReader( std::make_unique<PythonFileReader>( pythonObject ), parallelization )
    {}
#endif

    ~ParallelGzipReader()
    {
        if ( m_showProfileOnDestruction && ENABLE_STATISTICS ) {
            std::cerr << "[ParallelGzipReader] Time spent:";
            std::cerr << "\n    Writing to output         : " << m_writeOutputTime << " s";
            std::cerr << "\n    Computing CRC32           : " << m_crc32Time << " s";
            std::cerr << "\n    Number of verified CRC32s : " << m_verifiedCRC32Count;
            std::cerr << std::endl;
        }
    }

    /**
     * @note Only will work if ENABLE_STATISTICS is true.
     */
    void
    setShowProfileOnDestruction( bool showProfileOnDestruction )
    {
        m_showProfileOnDestruction = showProfileOnDestruction;
        if ( m_chunkFetcher ) {
            m_chunkFetcher->setShowProfileOnDestruction( m_showProfileOnDestruction );
        }
        if ( m_sharedFileReader ) {
            m_sharedFileReader->setShowProfileOnDestruction( m_showProfileOnDestruction );
        }
    }

    /* FileReader overrides */

    [[nodiscard]] UniqueFileReader
    clone() const override
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] int
    fileno() const override
    {
        return m_bitReader.fileno();
    }

    [[nodiscard]] bool
    seekable() const override
    {
        return m_bitReader.seekable();
    }

    void
    close() override
    {
        m_chunkFetcher.reset();
        m_blockFinder.reset();
        m_bitReader.close();
        m_sharedFileReader.reset();
    }

    [[nodiscard]] bool
    closed() const override
    {
        return m_bitReader.closed();
    }

    [[nodiscard]] bool
    eof() const override
    {
        return m_atEndOfFile;
    }

    [[nodiscard]] bool
    fail() const override
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] size_t
    tell() const override
    {
        if ( m_atEndOfFile ) {
            return size();
        }
        return m_currentPosition;
    }

    [[nodiscard]] size_t
    size() const override
    {
        if ( !m_blockMap->finalized() ) {
            throw std::invalid_argument( "Cannot get stream size in gzip when not finished reading at least once!" );
        }
        return m_blockMap->back().second;
    }

    void
    clearerr() override
    {
        m_bitReader.clearerr();
        m_atEndOfFile = false;
        throw std::invalid_argument( "Not fully tested!" );
    }

    [[nodiscard]] size_t
    read( char*  outputBuffer,
          size_t nBytesToRead ) override
    {
        return read( -1, outputBuffer, nBytesToRead );
    }

    /* Simpler file reader interface for Python-interfacing */

    size_t
    read( const int    outputFileDescriptor = -1,
          char* const  outputBuffer         = nullptr,
          const size_t nBytesToRead         = std::numeric_limits<size_t>::max() )
    {
        const auto writeFunctor =
            [nBytesDecoded = uint64_t( 0 ), outputFileDescriptor, outputBuffer]
            ( const std::shared_ptr<ChunkData>& chunkData,
              size_t const                      offsetInBlock,
              size_t const                      dataToWriteSize ) mutable
            {
                if ( dataToWriteSize == 0 ) {
                    return;
                }

                writeAll( chunkData, outputFileDescriptor, offsetInBlock, dataToWriteSize );

                if ( outputBuffer != nullptr ) {
                    using rapidgzip::deflate::DecodedData;

                    size_t nBytesCopied{ 0 };
                    for ( auto it = DecodedData::Iterator( *chunkData, offsetInBlock, dataToWriteSize );
                          static_cast<bool>( it ); ++it )
                    {
                        const auto& [buffer, bufferSize] = *it;
                        auto* const currentBufferPosition = outputBuffer + nBytesDecoded + nBytesCopied;
                        std::memcpy( currentBufferPosition, buffer, bufferSize );
                        nBytesCopied += bufferSize;
                    }
                }

                nBytesDecoded += dataToWriteSize;
            };

        return read( writeFunctor, nBytesToRead );
    }

    size_t
    read( const WriteFunctor& writeFunctor,
          const size_t        nBytesToRead = std::numeric_limits<size_t>::max() )
    {
        if ( closed() ) {
            throw std::invalid_argument( "You may not call read on closed ParallelGzipReader!" );
        }

        if ( eof() || ( nBytesToRead == 0 ) ) {
            return 0;
        }

        size_t nBytesDecoded = 0;
        while ( ( nBytesDecoded < nBytesToRead ) && !eof() ) {
            const auto blockResult = chunkFetcher().get( m_currentPosition );
            if ( !blockResult ) {
                m_atEndOfFile = true;
                break;
            }
            const auto& [decodedOffsetInBytes, chunkData] = *blockResult;

            if ( chunkData->containsMarkers() ) {
                throw std::logic_error( "Did not expect to get results with markers!" );
            }

            /* Copy data from fetched block to output. */

            const auto offsetInBlock = m_currentPosition - decodedOffsetInBytes;
            const auto blockSize = chunkData->decodedSizeInBytes;
            if ( offsetInBlock >= blockSize ) {
                std::stringstream message;
                message << "[ParallelGzipReader] Block does not contain the requested offset! "
                        << "Requested offset from chunk fetcher: " << m_currentPosition
                        << " (" << formatBytes( m_currentPosition ) << ")"
                        << ", decoded offset: " << decodedOffsetInBytes
                        << " (" << formatBytes( decodedOffsetInBytes ) << ")"
                        << ", block data encoded offset: " << formatBits( chunkData->encodedOffsetInBits )
                        << ", block data encoded size: " << formatBits( chunkData->encodedSizeInBits )
                        << ", block data size: " << chunkData->decodedSizeInBytes
                        << " (" << formatBytes( chunkData->decodedSizeInBytes ) << ")"
                        << " markers: " << chunkData->dataWithMarkersSize();
                throw std::logic_error( std::move( message ).str() );
            }

        #ifdef WITH_PYTHON_SUPPORT
            checkPythonSignalHandlers();
        #endif

            const auto nBytesToDecode = std::min( blockSize - offsetInBlock, nBytesToRead - nBytesDecoded );

            [[maybe_unused]] const auto tCRC32Start = now();
            processCRC32( chunkData, offsetInBlock, nBytesToDecode );
            if constexpr ( ENABLE_STATISTICS ) {
                m_crc32Time += duration( tCRC32Start );
            }

            if ( writeFunctor ) {
                [[maybe_unused]] const auto tWriteStart = now();
                writeFunctor( chunkData, offsetInBlock, nBytesToDecode );
                if constexpr ( ENABLE_STATISTICS ) {
                    m_writeOutputTime += duration( tWriteStart );
                }
            }

            nBytesDecoded += nBytesToDecode;
            m_currentPosition += nBytesToDecode;
        }

        return nBytesDecoded;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        if ( closed() ) {
            throw std::invalid_argument( "You may not call seek on closed ParallelGzipReader!" );
        }

        switch ( origin )
        {
        case SEEK_CUR:
            offset = tell() + offset;
            break;
        case SEEK_SET:
            break;
        case SEEK_END:
            /* size() requires the block offsets to be available! */
            if ( !m_blockMap->finalized() ) {
                read();
            }
            offset = size() + offset;
            break;
        }

        const auto positiveOffset = static_cast<size_t>( std::max<decltype( offset )>( 0, offset ) );

        if ( positiveOffset == tell() ) {
            return positiveOffset;
        }

        /* Backward seeking is no problem at all! 'tell' may only return <= size()
         * as value meaning we are now < size() and therefore EOF can be cleared! */
        if ( positiveOffset < tell() ) {
            m_atEndOfFile = false;
            m_currentPosition = positiveOffset;
            return positiveOffset;
        }

        /* m_blockMap is only accessed by read and seek, which are not to be called from different threads,
         * so we do not have to lock it. */
        const auto blockInfo = m_blockMap->findDataOffset( positiveOffset );
        if ( positiveOffset < blockInfo.decodedOffsetInBytes ) {
            throw std::logic_error( "Block map returned unwanted block!" );
        }

        if ( blockInfo.contains( positiveOffset ) ) {
            m_atEndOfFile = false;
            m_currentPosition = positiveOffset;
            return tell();
        }

        assert( positiveOffset - blockInfo.decodedOffsetInBytes > blockInfo.decodedSizeInBytes );
        if ( m_blockMap->finalized() ) {
            m_atEndOfFile = true;
            m_currentPosition = size();
            return tell();
        }

        /* Jump to furthest known point as performance optimization. Note that even if that is right after
         * the last byte, i.e., offset == size(), then no eofbit is set even in ifstream! In ifstream you
         * can even seek to after the file end with no fail bits being set in my tests! */
        m_atEndOfFile = false;
        m_currentPosition = blockInfo.decodedOffsetInBytes + blockInfo.decodedSizeInBytes;
        read( -1, nullptr, positiveOffset - tell() );
        return tell();
    }

    /* Block compression specific methods */

    [[nodiscard]] bool
    blockOffsetsComplete() const
    {
        return m_blockMap->finalized();
    }

    /**
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    [[nodiscard]] std::map<size_t, size_t>
    blockOffsets()
    {
        if ( !m_blockMap->finalized() ) {
            read();
            if ( !m_blockMap->finalized() || !blockFinder().finalized() ) {
                throw std::logic_error( "Reading everything should have finalized the block map!" );
            }
        }

        return m_blockMap->blockOffsets();
    }

    [[nodiscard]] GzipIndex
    gzipIndex()
    {
        const auto offsets = blockOffsets();  // Also finalizes reading implicitly.
        if ( offsets.empty() ) {
            return {};
        }

        GzipIndex index;
        index.compressedSizeInBytes = ceilDiv( offsets.rbegin()->first, 8U );
        index.uncompressedSizeInBytes = offsets.rbegin()->second;
        index.windowSizeInBytes = 32_Ki;

        /* Heuristically determine a checkpoint spacing from the existing checkpoints. */
        std::vector<size_t> uncompressedSpacings;
        for ( auto it = offsets.begin(), nit = std::next( offsets.begin() ); nit != offsets.end(); ++it, ++nit ) {
            uncompressedSpacings.push_back( nit->second - it->second );
        }
        index.checkpointSpacing = ceilDiv(
            *std::max_element( uncompressedSpacings.begin(), uncompressedSpacings.end() ),
            32_Ki ) * 32_Ki;

        for ( const auto& [compressedOffsetInBits, uncompressedOffsetInBytes] : offsets ) {
            Checkpoint checkpoint;
            checkpoint.compressedOffsetInBits = compressedOffsetInBits;
            checkpoint.uncompressedOffsetInBytes = uncompressedOffsetInBytes;

            const auto window = m_windowMap->get( compressedOffsetInBits );
            if ( window ) {
                checkpoint.window.assign( window->begin(), window->end() );
            }

            index.checkpoints.emplace_back( std::move( checkpoint ) );
        }

        return index;
    }

    /**
     * Same as @ref blockOffsets but it won't force calculation of all blocks and simply returns
     * what is availabe at call time.
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    [[nodiscard]] std::map<size_t, size_t>
    availableBlockOffsets() const
    {
        return m_blockMap->blockOffsets();
    }

    [[nodiscard]] auto
    statistics() const
    {
        if ( !m_chunkFetcher ) {
            throw std::invalid_argument( "No chunk fetcher initialized!" );
        }
        return m_chunkFetcher->statistics();
    }

    void
    setCRC32Enabled( bool enabled )
    {
        if ( m_crc32.enabled() == enabled ) {
            return;
        }

        m_crc32.setEnabled( enabled && ( tell() == 0 ) );
        if ( m_chunkFetcher ) {
            m_chunkFetcher->setCRC32Enabled( m_crc32.enabled() );
        }
    }

    void
    setMaxDecompressedChunkSize( uint64_t maxDecompressedChunkSize )
    {
        /* Anything smaller than the chunk size doesn't make much sense. Even that would be questionable.
         * as it would lead to slow downs in almost every case. */
        m_maxDecompressedChunkSize = std::max( m_chunkSizeInBytes, maxDecompressedChunkSize );
        if ( m_chunkFetcher ) {
            m_chunkFetcher->setMaxDecompressedChunkSize( m_maxDecompressedChunkSize );
        }
    }

    [[nodiscard]] uint64_t
    maxDecompressedChunkSize() const noexcept
    {
        return m_maxDecompressedChunkSize;
    }

private:
    void
    setBlockOffsets( std::map<size_t, size_t> offsets )
    {
        /**
         * @todo Join very small consecutive block offsets until it roughly reflects the chunk size?
         * Because currently, the version with the BGZI index is slower than without!
         * rapidgzip -d -o /dev/null 4GiB-base64.bgz
         * > Decompressed in total 4294967296 B in 0.565016 s -> 7601.49 MB/s
         * rapidgzip -d -o /dev/null --import-index 4GiB-base64.bgz
         * > Decompressed in total 4294901760 B in 1.22275 s -> 3512.5 MB/s
         */

        if ( offsets.empty() ) {
            if ( m_blockMap->dataBlockCount() == 0 ) {
                return;
            }
            throw std::invalid_argument( "May not clear offsets. Construct a new ParallelGzipReader instead!" );
        }

        setBlockFinderOffsets( offsets );

        if ( offsets.size() < 2 ) {
            throw std::invalid_argument( "Block offset map must contain at least one valid block and one EOS block!" );
        }
        m_blockMap->setBlockOffsets( std::move( offsets ) );
    }

public:
    void
    setBlockOffsets( const GzipIndex& index )
    {
        if ( index.checkpoints.empty() ) {
            return;
        }

        /* Generate simple compressed to uncompressed offset map from index. */
        std::map<size_t, size_t> newBlockOffsets;
        for ( size_t i = 0; i < index.checkpoints.size(); ++i ) {
            const auto& checkpoint = index.checkpoints[i];

            /**
             * Skip emission of an index, if the next checkpoint would still let us be below the chunk size.
             * Always copy the zeroth index as is necessary for a valid index!
             *
             * This is for merging very small index points as might happen when importing BGZF indexes.
             * Small index will lead to relatively larger overhead for the threading and will degrade performance:
             * Merge n blocks:
             *     0 -> ~3.3 GB/s, Total existing blocks: 65793
             *     2 -> ~5.0 GB/s, Total existing blocks: 32897
             *     4 -> ~5.7 GB/s, Total existing blocks: 16449
             *     8 -> ~6.0 GB/s, Total existing blocks: 8225
             *    16 -> ~6.8 GB/s, Total existing blocks: 4113
             *    32 -> ~6.8 GB/s, Total existing blocks: 2057
             *    64 -> ~7.2 GB/s, Total existing blocks: 1029
             *   128 -> ~6.9 GB/s, Total existing blocks: 515
             *
             * Without index import (chunk size 4 MiB):
             * src/tools/rapidgzip -d -o /dev/null 4GiB-base64.bgz
             *   Total existing blocks: 766 blocks
             *   Index reading took: 0.00259098 s
             *
             *   Decompressed in total 4294967296 B in 0.580731 s -> 7395.79 MB/s
             *   Decompressed in total 4294967296 B in 0.576022 s -> 7456.26 MB/s
             *   Decompressed in total 4294967296 B in 0.597594 s -> 7187.1 MB/s
             */
            if ( !newBlockOffsets.empty() && ( i + 1 < index.checkpoints.size() )
                 && ( index.checkpoints[i + 1].uncompressedOffsetInBytes - newBlockOffsets.rbegin()->second
                      <= m_chunkSizeInBytes ) )
            {
                continue;
            }

            newBlockOffsets.emplace( checkpoint.compressedOffsetInBits, checkpoint.uncompressedOffsetInBytes );

            /* Copy window data.
             * For some reason, indexed_gzip also stores windows for the very last checkpoint at the end of the file,
             * which is useless because there is nothing thereafter. But, don't filter it here so that exportIndex
             * mirrors importIndex better. */
            m_windowMap->emplace( checkpoint.compressedOffsetInBits,
                                  WindowMap::Window( checkpoint.window.data(),
                                                     checkpoint.window.data() + checkpoint.window.size() ) );
        }

        /* Input file-end offset if not included in checkpoints. */
        if ( const auto fileEndOffset = newBlockOffsets.find( index.compressedSizeInBytes * 8 );
             fileEndOffset == newBlockOffsets.end() )
        {
            newBlockOffsets.emplace( index.compressedSizeInBytes * 8, index.uncompressedSizeInBytes );
        } else if ( fileEndOffset->second != index.uncompressedSizeInBytes ) {
            throw std::invalid_argument( "Index has contradicting information for the file end information!" );
        }

        setBlockOffsets( std::move( newBlockOffsets ) );

        chunkFetcher().clearCache();
    }

    void
    importIndex( UniqueFileReader indexFile )
    {
        setBlockOffsets( readGzipIndex( std::move( indexFile ), m_sharedFileReader->clone() ) );
    }

#ifdef WITH_PYTHON_SUPPORT
    void
    importIndex( PyObject* pythonObject )
    {
        importIndex( std::make_unique<PythonFileReader>( pythonObject ) );
    }

    void
    exportIndex( PyObject* pythonObject )
    {
        const auto file = std::make_unique<PythonFileReader>( pythonObject );
        const auto checkedWrite =
            [&file] ( const void* buffer, size_t size )
            {
                if ( file->write( reinterpret_cast<const char*>( buffer ), size ) != size ) {
                    throw std::runtime_error( "Failed to write data to index!" );
                }
            };

        writeGzipIndex( gzipIndex(), checkedWrite );
    }
#endif

    /**
     * @return number of processed bits of compressed bzip2 input file stream
     * @note Bzip2 is block based and blocks are currently read fully, meaning that the granularity
     *       of the returned position is ~100-900kB. It's only useful for a rough estimate.
     */
    [[nodiscard]] size_t
    tellCompressed() const
    {
        if ( !m_blockMap || m_blockMap->empty() ) {
            return 0;
        }

        const auto blockInfo = m_blockMap->findDataOffset( m_currentPosition );
        if ( blockInfo.contains( m_currentPosition ) ) {
            return blockInfo.encodedOffsetInBits;
        }
        return m_blockMap->back().first;
    }

    /**
     * Closes all threads and saves the work. They will be restarted when needed again, e.g., on seek or read.
     * This is intended for use with fusepy. You can start a ParallelGzipReader use it to create the block map
     * and print out user output and then you join all threads before FUSE forks the process. FUSE requires
     * threads to be created after it forks, it seems:
     * @see https://github.com/libfuse/libfuse/wiki/FAQ#how-should-threads-be-started
     * Personally, the only problem I observed was background process not finishing even after unmounting,
     * however, contrary to the FAQ it seems that threads were not joined because the file system seemed to work.
     */
    void
    joinThreads()
    {
        m_chunkFetcher.reset();
        m_blockFinder.reset();
    }

private:
    BlockFinder&
    blockFinder()
    {
        if ( m_blockFinder ) {
            return *m_blockFinder;
        }

        if ( !m_startBlockFinder ) {
            throw std::logic_error( "Block finder creator was not initialized correctly!" );
        }

        m_blockFinder = m_startBlockFinder();
        if ( !m_blockFinder ) {
            throw std::logic_error( "Block finder creator failed to create new block finder!" );
        }

        if ( m_blockMap->finalized() ) {
            setBlockFinderOffsets( m_blockMap->blockOffsets() );
        }

        return *m_blockFinder;
    }

    ChunkFetcher&
    chunkFetcher()
    {
        if ( m_chunkFetcher ) {
            return *m_chunkFetcher;
        }

        /* As a side effect, blockFinder() creates m_blockFinder if not already initialized! */
        blockFinder();

        m_chunkFetcher = std::make_unique<ChunkFetcher>( m_bitReader, m_blockFinder, m_blockMap, m_windowMap,
                                                         m_fetcherParallelization );

        if ( !m_chunkFetcher ) {
            throw std::logic_error( "Block fetcher should have been initialized!" );
        }

        m_chunkFetcher->setCRC32Enabled( m_crc32.enabled() );
        m_chunkFetcher->setMaxDecompressedChunkSize( m_maxDecompressedChunkSize );
        m_chunkFetcher->setShowProfileOnDestruction( m_showProfileOnDestruction );

        return *m_chunkFetcher;
    }

    void
    setBlockFinderOffsets( const std::map<size_t, size_t>& offsets )
    {
        if ( offsets.empty() ) {
            throw std::invalid_argument( "A non-empty list of block offsets is required!" );
        }

        typename BlockFinder::BlockOffsets encodedBlockOffsets;
        for ( auto it = offsets.begin(), nit = std::next( offsets.begin() ); nit != offsets.end(); ++it, ++nit )
        {
            /* Ignore blocks with no data, i.e., EOS blocks. */
            if ( it->second != nit->second ) {
                encodedBlockOffsets.push_back( it->first );
            }
        }
        /* The last block is not pushed because "std::next( it )" is end but last block must be EOS anyways
         * or else BlockMap will not work correctly because the implied size of that last block is 0! */

        blockFinder().setBlockOffsets( std::move( encodedBlockOffsets ) );
    }

    void
    processCRC32( const std::shared_ptr<ChunkData>& chunkData,
                  [[maybe_unused]] size_t const     offsetInBlock,
                  [[maybe_unused]] size_t const     dataToWriteSize )
    {
        if ( ( m_nextCRC32ChunkOffset == 0 ) && m_blockFinder ) {
            const auto [offset, errorCode] = m_blockFinder->get( /* block index */ 0, /* timeout */ 0 );
            if ( offset && ( errorCode == BlockFinder::GetReturnCode::SUCCESS ) ) {
                m_nextCRC32ChunkOffset = *offset;
            }
        }

        if ( !m_crc32.enabled()
             || ( m_nextCRC32ChunkOffset != chunkData->encodedOffsetInBits )
             || chunkData->crc32s.empty() ) {
            return;
        }

        m_nextCRC32ChunkOffset = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;

        /* As long as CRC32 is enabled, this should not happen and we filter above for !m_crc32.enabled(). */
        if ( chunkData->crc32s.size() != chunkData->footers.size() + 1 ) {
            throw std::logic_error( "Fewer CRC32s in chunk than expected based on the gzip footers!" );
        }

        const auto totalCRC32StreamSize = std::accumulate(
            chunkData->crc32s.begin(), chunkData->crc32s.end(), size_t( 0 ),
            [] ( size_t sum, const auto& calculator ) { return sum + calculator.streamSize(); } );
        if ( totalCRC32StreamSize != chunkData->decodedSizeInBytes ) {
            std::stringstream message;
            message << "CRC32 computation stream size (" << formatBytes( totalCRC32StreamSize ) << ") differs from "
                    << "chunk size: " << formatBytes( chunkData->decodedSizeInBytes ) << "!\n"
                    << "Please open an issue or disable integrated CRC32 verification as a quick workaround.";
            throw std::logic_error( std::move( message ).str() );
        }

        /* Process CRC32 of chunk. */
        m_crc32.append( chunkData->crc32s.front() );
        for ( size_t i = 0; i < chunkData->footers.size(); ++i ) {
            if ( m_crc32.verify( chunkData->footers[i].gzipFooter.crc32 ) ) {
                m_verifiedCRC32Count++;
            }
            m_crc32 = chunkData->crc32s.at( i + 1 );
        }
    }

private:
    const uint64_t m_chunkSizeInBytes{ 4_Mi };
    uint64_t m_maxDecompressedChunkSize{ std::numeric_limits<size_t>::max() };

    std::unique_ptr<SharedFileReader> m_sharedFileReader;
    BitReader m_bitReader{ UniqueFileReader( m_sharedFileReader->clone() ) };

    size_t m_currentPosition = 0;  /**< the current position as can only be modified with read or seek calls. */
    bool m_atEndOfFile = false;

    /** Benchmarking */
    bool m_showProfileOnDestruction{ false };
    double m_writeOutputTime{ 0 };
    double m_crc32Time{ 0 };
    uint64_t m_verifiedCRC32Count{ 0 };

    size_t const m_fetcherParallelization;

    std::function<std::shared_ptr<BlockFinder>( void )> const m_startBlockFinder;

    /** Necessary for prefetching decoded blocks in parallel. */
    std::shared_ptr<BlockFinder>     m_blockFinder;
    std::shared_ptr<BlockMap>  const m_blockMap{ std::make_shared<BlockMap>() };
    /**
     * The window map should contain windows to all encoded block offsets inside @ref m_blockMap.
     * The windows are stored in a separate map even though all keys should be identical because BlockMap is
     * too "finished". I don't see how to generically and readably add generic user data / windows to it.
     * Furthermore, the windows might potentially be written out-of-order while block offsets should be inserted
     * in order into @ref m_blockMap.
     */
    std::shared_ptr<WindowMap> const m_windowMap{ std::make_shared<WindowMap>() };
    std::unique_ptr<ChunkFetcher> m_chunkFetcher;

    CRC32Calculator m_crc32;
    uint64_t m_nextCRC32ChunkOffset{ 0 };
};
}  // namespace rapidgzip
