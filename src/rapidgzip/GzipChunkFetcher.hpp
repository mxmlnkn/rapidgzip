#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>

#include <BitStringFinder.hpp>
#include <BlockFetcher.hpp>
#include <BlockMap.hpp>
#include <common.hpp>
#include <FasterVector.hpp>

#include "blockfinder/DynamicHuffman.hpp"
#include "blockfinder/Uncompressed.hpp"
#include "ChunkData.hpp"
#include "deflate.hpp"
#include "gzip.hpp"
#include "GzipBlockFinder.hpp"
#ifdef WITH_ISAL
    #include "isal.hpp"
#endif
#include "WindowMap.hpp"
#include "zlib.hpp"


namespace rapidgzip
{
class DecompressionError :
    public std::runtime_error
{
public:
    DecompressionError( const std::string& message ) :
        std::runtime_error( message )
    {}
};


class NoBlockInRange :
    public DecompressionError
{
public:
    NoBlockInRange( const std::string& message ) :
        DecompressionError( message )
    {}
};


template<typename T_FetchingStrategy,
         typename T_ChunkData = ChunkData>
class GzipChunkFetcher :
    public BlockFetcher<GzipBlockFinder, T_ChunkData, T_FetchingStrategy>
{
public:
    using FetchingStrategy = T_FetchingStrategy;
    using ChunkData = T_ChunkData;
    using ChunkConfiguration = typename ChunkData::Configuration;
    using BaseType = BlockFetcher<GzipBlockFinder, ChunkData, FetchingStrategy>;
    using BitReader = rapidgzip::BitReader;
    using SharedWindow = WindowMap::SharedWindow;
    using SharedDecompressedWindow = std::shared_ptr<const FasterVector<uint8_t> >;
    using WindowView = VectorView<uint8_t>;
    using BlockFinder = typename BaseType::BlockFinder;
    using PostProcessingFutures = std::map</* block offset */ size_t, std::future<void> >;
    using UniqueSharedFileReader = std::unique_ptr<SharedFileReader>;

    static constexpr bool REPLACE_MARKERS_IN_PARALLEL = true;

    /**
     * Rpmalloc does worse than standard malloc (Clang 13) for the case when using 128 cores, chunk size 4 MiB
     * with imported index of Silesia (compression ratio ~3.1), i.e., the decompressed chunk sizes are ~12 MiB
     * and probably deviate wildly in size (4-100 MiB maybe?). I guess that this leads to overallocation and
     * memory slab reuse issues in rpmalloc.
     * Allocating memory chunks in much more deterministic sizes seems to alleviate this problem immensely!
     *
     * Problematic commit:
     *     dd678c7 2022-11-06 mxmlnkn [performance] Use rpmalloc to increase multi-threaded malloc performance
     *
     * Approximate benchmark results for:
     *     rapidgzip -P $( nproc ) -o /dev/null -f --export-index index silesia-256x.tar.pigz
     *     taskset --cpu-list 0-127 rapidgzip -P 128 -d -o /dev/null --import-index index silesia-256x.tar.pigz
     *
     * Commit:
     *     rapidgzip-v0.5.0   16 GB/s
     *     dd678c7~1        16 GB/s
     *     dd678c7           8 GB/s
     *
     * dd678c7 with ALLOCATION_CHUNK_SIZE:
     *     64  KiB       19.4 19.7       GB/s
     *     256 KiB       20.8 20.7       GB/s
     *     1   MiB       21.2 20.7 20.8  GB/s
     *     4   MiB       8.4 8.5 8.3     GB/s
     *
     * It seems to be pretty stable across magnitudes as long as the number of allocations doesn't get too
     * large and as long as the allocation chunk size is much smaller than the decompressed data chunk size.
     * 1 MiB seems like the natural choice because the optimum (compressed) chunk size is around 4 MiB
     * and it would also be exactly one hugepage if support for that would ever be added.
     * Beware that each chunk is only as large as one gzip stream. And bgzip creates gzip streams that are only
     * ~64 KiB each! Therefore, when decoding bgzip while importing the index, we need to account for this here
     * and avoid frequent overallocations and resizes, which slow down the program immensely!
     *
     * Test with:
     *     m rapidgzip && src/tools/rapidgzip -o /dev/null -d \
     *       --import-index silesia-32x.tar.bgzip.gzi silesia-32x.tar.bgzip
     *
     * ALLOCATION_CHUNK_SIZE  Bandwidth
     *        32  KiB         2.4 GB/s
     *        64  KiB         2.5 GB/s
     *        128 KiB         2.5 GB/s
     *        256 KiB         2.4 GB/s
     *        512 KiB         1.5 GB/s
     *        1   MiB         370 MB/s
     */
    static constexpr size_t ALLOCATION_CHUNK_SIZE = 128_Ki;

    struct Statistics :
        public ChunkData::Statistics
    {
    public:
        using BaseType = typename ChunkData::Statistics;

    public:
        void
        merge( const ChunkData& chunkData )
        {
            const std::scoped_lock lock( mutex );
            BaseType::merge( chunkData.statistics );
            preemptiveStopCount += chunkData.stoppedPreemptively ? 1 : 0;
        }

    public:
        mutable std::mutex mutex;
        uint64_t preemptiveStopCount{ 0 };
        double queuePostProcessingDuration{ 0 };
    };

public:
    GzipChunkFetcher( UniqueSharedFileReader       sharedFileReader,
                      std::shared_ptr<BlockFinder> blockFinder,
                      std::shared_ptr<BlockMap>    blockMap,
                      std::shared_ptr<WindowMap>   windowMap,
                      size_t                       parallelization ) :
        BaseType( blockFinder, parallelization ),
        m_sharedFileReader( std::move( sharedFileReader ) ),
        m_blockFinder( std::move( blockFinder ) ),
        m_blockMap( std::move( blockMap ) ),
        m_windowMap( std::move( windowMap ) ),
        m_isBgzfFile( m_blockFinder->fileType() == FileType::BGZF )
    {
        if ( !m_sharedFileReader ) {
            throw std::invalid_argument( "Shared file reader must be valid!" );
        }
        if ( !m_blockMap ) {
            throw std::invalid_argument( "Block map must be valid!" );
        }
        if ( !m_windowMap ) {
            throw std::invalid_argument( "Window map must be valid!" );
        }

        if ( m_windowMap->empty() ) {
            const auto firstBlockInStream = m_blockFinder->get( 0 );
            if ( !firstBlockInStream ) {
                throw std::logic_error( "The block finder is required to find the first block itself!" );
            }
            m_windowMap->emplace( *firstBlockInStream, {}, CompressionType::NONE );
        }

        /* Choose default value for CRC32 enable flag. Can still be overwritten by the setter. */
        if ( hasCRC32( m_blockFinder->fileType() ) ) {
            m_crc32Enabled = false;
        }
    }

    virtual
    ~GzipChunkFetcher()
    {
        m_cancelThreads = true;
        this->stopThreadPool();

        if ( BaseType::m_showProfileOnDestruction ) {
            const auto formatCount =
                [] ( const uint64_t count )
                {
                    auto result = std::to_string( count );
                    std::string delimited;
                    static constexpr size_t DIGIT_GROUP_SIZE = 3;
                    delimited.reserve( result.size()
                                       + ( result.empty() ? 0 : ( result.size() - 1 ) / DIGIT_GROUP_SIZE ) );
                    for ( size_t i = 0; i < result.size(); ++i ) {
                        const auto distanceFromBack = result.size() - i;
                        if ( ( i > 0 ) && ( distanceFromBack % 3 == 0 ) ) {
                            delimited.push_back( '\'' );
                        }
                        delimited.push_back( result[i] );
                    }
                    return delimited;
                };


            const auto totalDecompressedCount = m_statistics.nonMarkerCount + m_statistics.markerCount;

            std::stringstream out;
            out << "[GzipChunkFetcher::GzipChunkFetcher] First block access statistics:\n";
            out << "    Number of false positives                : " << m_statistics.falsePositiveCount << "\n";
            out << "    Time spent in block finder               : " << m_statistics.blockFinderDuration << " s\n";
            out << "    Time spent decoding with custom inflate  : " << m_statistics.decodeDuration << " s\n";
            out << "    Time spent decoding with inflate wrapper : " << m_statistics.decodeDurationInflateWrapper
                << " s\n";
            out << "    Time spent decoding with ISA-L           : " << m_statistics.decodeDurationIsal << " s\n";
            out << "    Time spent allocating and copying        : " << m_statistics.appendDuration << " s\n";
            out << "    Time spent applying the last window      : " << m_statistics.applyWindowDuration << " s\n";
            out << "    Time spent computing the checksum        : " << m_statistics.computeChecksumDuration << " s\n";
            out << "    Time spent compressing seek points       : " << m_statistics.compressWindowDuration << " s\n";
            out << "    Time spent queuing post-processing       : " << m_statistics.queuePostProcessingDuration
                << " s\n";
            out << "    Total decompressed bytes                 : " << formatCount( totalDecompressedCount ) << "\n";
            out << "    Non-marker symbols                       : " << formatCount( m_statistics.nonMarkerCount );
            if ( totalDecompressedCount > 0 ) {
                out << " (" << static_cast<double>( m_statistics.nonMarkerCount )
                               / static_cast<double>( totalDecompressedCount ) * 100
                    << " %)";
            }
            out << "\n";
            out << "    Replaced marker symbol buffers           : " << formatCount( m_statistics.markerCount );
            if ( totalDecompressedCount > 0 ) {
                out << " (" << static_cast<double>( m_statistics.markerCount )
                               / static_cast<double>( totalDecompressedCount ) * 100
                    << " %)";
            }
            out << "\n";
            /* realMarkerCount can be zero if computation is disabled because it is too expensive. */
            if ( m_statistics.realMarkerCount > 0 ) {
                out << "    Actual marker symbol count in buffers    : " << formatCount( m_statistics.realMarkerCount );
                if ( m_statistics.markerCount > 0 ) {
                    out << " (" << static_cast<double>( m_statistics.realMarkerCount )
                                   / static_cast<double>( m_statistics.markerCount ) * 100
                        << " %)";
                }
                out << "\n";
            }
            out << "    Chunks exceeding max. compression ratio  : " << m_statistics.preemptiveStopCount << "\n";

            const auto& fetcherStatistics = BaseType::statistics();
            const auto decodeDuration =
                fetcherStatistics.decodeBlockStartTime && fetcherStatistics.decodeBlockEndTime
                ? duration( *fetcherStatistics.decodeBlockStartTime, *fetcherStatistics.decodeBlockEndTime )
                : 0.0;
            const auto optimalDecodeDuration = ( fetcherStatistics.decodeBlockTotalTime
                                                 + m_statistics.applyWindowDuration
                                                 + m_statistics.computeChecksumDuration )
                                               / fetcherStatistics.parallelization;
            /* The pool efficiency only makes sense when the thread pool is smaller or equal the CPU cores. */
            const auto poolEfficiency = optimalDecodeDuration / decodeDuration;

            out << "    Thread Pool Utilization:\n";
            out << "        Total Real Decode Duration    : " << decodeDuration << " s\n";
            out << "        Theoretical Optimal Duration  : " << optimalDecodeDuration << " s\n";
            out << "        Pool Efficiency (Fill Factor) : " << poolEfficiency * 100 << " %\n";

            std::cerr << std::move( out ).str();
        }
    }

    /**
     * @param offset The current offset in the decoded data. (Does not have to be a block offset!)
     * Does not return the whole BlockInfo object because it might not fit the chunk from the cache
     * because of dynamic chunk splitting. E.g., when the BlockMap already contains the smaller split
     * chunks while the cache still contains the unsplit chunk.
     */
    [[nodiscard]] std::optional<std::pair</* decoded offset */ size_t, std::shared_ptr<ChunkData> > >
    get( size_t offset )
    {
        /* In case we already have decoded the block once, we can simply query it from the block map and the fetcher. */
        auto blockInfo = m_blockMap->findDataOffset( offset );
        if ( blockInfo.contains( offset ) ) {
            return getIndexedChunk( offset, blockInfo );
        }

        /* If the requested offset lies outside the last known block, then we need to keep fetching the next blocks
         * and filling the block- and window map until the end of the file is reached or we found the correct block. */
        std::shared_ptr<ChunkData> chunkData;
        for ( ; !blockInfo.contains( offset ); blockInfo = m_blockMap->findDataOffset( offset ) ) {
            chunkData = processNextChunk();
            if ( !chunkData ) {
                return std::nullopt;
            }
        }
        return std::make_pair( blockInfo.decodedOffsetInBytes, chunkData );
    }

    void
    setCRC32Enabled( bool enabled )
    {
        m_crc32Enabled = enabled;
    }

    void
    setMaxDecompressedChunkSize( size_t maxDecompressedChunkSize )
    {
        m_maxDecompressedChunkSize = maxDecompressedChunkSize;
    }

    [[nodiscard]] size_t
    maxDecompressedChunkSize() const
    {
        return m_maxDecompressedChunkSize;
    }

    void
    setWindowCompressionType( std::optional<CompressionType> windowCompressionType )
    {
        m_windowCompressionType = windowCompressionType;
    }

private:
    [[nodiscard]] std::pair</* decoded offset */ size_t, std::shared_ptr<ChunkData> >
    getIndexedChunk( const size_t               offset,
                     const BlockMap::BlockInfo& blockInfo )
    {
        const auto blockOffset = blockInfo.encodedOffsetInBits;
        /* Try to look up the offset based on the offset of the unsplit block.
         * Do not use BaseType::get because it has too many side effects. Even if we know that the cache
         * contains the chunk, the access might break the perfect sequential fetching pattern because
         * the chunk was split into multiple indexes in the fetching strategy while we might now access
         * an earlier index, e.g., chunk 1 split into 1,2,3, then access offset belonging to split chunk 2. */
        if ( const auto unsplitBlock = m_unsplitBlocks.find( blockOffset );
             ( unsplitBlock != m_unsplitBlocks.end() ) && ( unsplitBlock->second != blockOffset ) )
        {
            if ( const auto chunkData = BaseType::cache().get( unsplitBlock->second ); chunkData ) {
                /* This will get the first split subchunk but this is fine because we only need the
                 * decodedOffsetInBytes from this query. Normally, this should always return a valid optional! */
                auto unsplitBlockInfo = m_blockMap->getEncodedOffset( ( *chunkData )->encodedOffsetInBits );
                if ( unsplitBlockInfo
                     /* Test whether we got the unsplit block or the first split subchunk from the cache. */
                     && ( blockOffset >= ( *chunkData )->encodedOffsetInBits )
                     && ( blockOffset < ( *chunkData )->encodedOffsetInBits + ( *chunkData )->encodedSizeInBits ) )
                {
                    if ( ( *chunkData )->containsMarkers() ) {
                        std::stringstream message;
                        message << "[GzipChunkFetcher] Did not expect to get results with markers! "
                                << "Requested offset: " << formatBits( offset ) << " found to belong to chunk at: "
                                << formatBits( blockOffset ) << ", found matching unsplit block with range ["
                                << formatBits( ( *chunkData )->encodedOffsetInBits ) << ", "
                                << formatBits( ( *chunkData )->encodedOffsetInBits
                                               + ( *chunkData )->encodedSizeInBits ) << "] in the list of "
                                << m_unsplitBlocks.size() << " unsplit blocks.";
                        throw std::logic_error( std::move( message ).str() );
                    }
                    return std::make_pair( unsplitBlockInfo->decodedOffsetInBytes, *chunkData );
                }
            }
        }

        /* Get block normally */
        auto chunkData = getBlock( blockInfo.encodedOffsetInBits, blockInfo.blockIndex );
        if ( chunkData && chunkData->containsMarkers() ) {
            auto lastWindow = m_windowMap->get( chunkData->encodedOffsetInBits );
            std::stringstream message;
            message << "[GzipChunkFetcher] Did not expect to get results with markers because the offset already "
                    << "exists in the block map!\n"
                    << "    Requested decompressed offset: " << formatBytes( offset ) << " found to belong to chunk at: "
                    << formatBits( blockOffset ) << " with range ["
                    << formatBits( chunkData->encodedOffsetInBits ) << ", "
                    << formatBits( chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits ) << "].\n"
                    << "    Window size for the chunk offset: "
                    << ( lastWindow ? std::to_string( lastWindow->decompressedSize() ) : "no window" ) << ".";
            throw std::logic_error( std::move( message ).str() );
        }

        return std::make_pair( blockInfo.decodedOffsetInBytes, std::move( chunkData ) );
    }

    [[nodiscard]] std::shared_ptr<ChunkData>
    processNextChunk()
    {
        if ( m_blockMap->finalized() ) {
            return {};
        }

        const auto nextBlockOffset = m_blockFinder->get( m_nextUnprocessedBlockIndex );

        if ( const auto inputFileSize = m_sharedFileReader->size();
             !nextBlockOffset ||
             ( inputFileSize && ( *inputFileSize > 0 ) && ( *nextBlockOffset >= *inputFileSize * BYTE_SIZE ) ) )
        {
            m_blockMap->finalize();
            m_blockFinder->finalize();
            return {};
        }

        auto chunkData = getBlock( *nextBlockOffset, m_nextUnprocessedBlockIndex );

        /* Because this is a new block, it might contain markers that we have to replace with the window
         * of the last block. The very first block should not contain any markers, ensuring that we
         * can successively propagate the window through all blocks. */
        auto sharedLastWindow = m_windowMap->get( *nextBlockOffset );
        if ( !sharedLastWindow ) {
            std::stringstream message;
            message << "The window of the last block at " << formatBits( *nextBlockOffset )
                    << " should exist at this point!";
            throw std::logic_error( std::move( message ).str() );
        }
        const auto lastWindow = sharedLastWindow->decompress();

        postProcessChunk( chunkData, lastWindow );

        /* Care has to be taken that we store the correct block offset not the speculative possible range!
         * This call corrects encodedSizeInBits, which only contains a guess from finalize().
         * This should only be called after post-processing has finished because encodedSizeInBits is also
         * used in windowCompressionType() during post-processing to compress the windows. */
        chunkData->setEncodedOffset( *nextBlockOffset );
        /* Should only happen when encountering EOF during decodeBlock call. */
        if ( chunkData->encodedSizeInBits == 0 ) {
            m_blockMap->finalize();
            m_blockFinder->finalize();
            return {};
        }

        appendSubchunksToIndexes( chunkData, chunkData->subchunks, *lastWindow );

        m_statistics.merge( *chunkData );

        return chunkData;
    }

    void
    appendSubchunksToIndexes( const std::shared_ptr<ChunkData>&                chunkData,
                              const std::vector<typename ChunkData::Subchunk>& subchunks,
                              const FasterVector<uint8_t>&                     lastWindow )
    {
        for ( const auto& boundary : subchunks ) {
            m_blockMap->push( boundary.encodedOffset, boundary.encodedSize, boundary.decodedSize );
            m_blockFinder->insert( boundary.encodedOffset + boundary.encodedSize );
        }

        if ( subchunks.size() > 1 ) {
            BaseType::m_fetchingStrategy.splitIndex( m_nextUnprocessedBlockIndex, subchunks.size() );

            /* Get actual key in cache, which might be the partition offset! */
            const auto chunkOffset = chunkData->encodedOffsetInBits;
            const auto partitionOffset = m_blockFinder->partitionOffsetContainingOffset( chunkOffset );
            const auto lookupKey = !BaseType::test( chunkOffset ) && BaseType::test( partitionOffset )
                                   ? partitionOffset
                                   : chunkOffset;
            for ( const auto& boundary : subchunks ) {
                /* This condition could be removed but makes the map slightly smaller. */
                if ( boundary.encodedOffset != chunkOffset ) {
                    m_unsplitBlocks.emplace( boundary.encodedOffset, lookupKey );
                }
            }
        }

        /* This should also work for multi-stream gzip files because encodedSizeInBits is such that it
         * points across the gzip footer and next header to the next deflate block. */
        const auto blockOffsetAfterNext = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;

        if ( const auto inputFileSize = m_sharedFileReader->size();
             inputFileSize && ( *inputFileSize > 0 ) && ( blockOffsetAfterNext >= *inputFileSize * BYTE_SIZE ) )
        {
            m_blockMap->finalize();
            m_blockFinder->finalize();
        }

        m_nextUnprocessedBlockIndex += subchunks.size();
        if ( const auto insertedNextBlockOffset = m_blockFinder->get( m_nextUnprocessedBlockIndex );
             !m_blockFinder->finalized()
             && ( !insertedNextBlockOffset.has_value() || ( *insertedNextBlockOffset != blockOffsetAfterNext ) ) )
        {
            /* We could also keep track of the next block offset instead of the block index but then we would
             * have to do a bisection for each block to find the block index from the offset. */
            std::stringstream message;
            message << "Next block offset index is out of sync! Requested offset to index "
                    << m_nextUnprocessedBlockIndex;
            if ( insertedNextBlockOffset.has_value() ) {
                message << " and got " << *insertedNextBlockOffset;
            } else {
                message << " and did not get a value";
            }
            message << " but expected " << blockOffsetAfterNext;
            throw std::logic_error( std::move( message ).str() );
        }

        const auto t0 = now();

        size_t decodedOffsetInBlock{ 0 };
        for ( const auto& subchunk : subchunks ) {
            decodedOffsetInBlock += subchunk.decodedSize;
            const auto windowOffset = subchunk.encodedOffset + subchunk.encodedSize;
            /* Avoid recalculating what we already emplaced in waitForReplacedMarkers when calling getLastWindow. */
            if ( !m_windowMap->get( windowOffset ) ) {
                if ( subchunk.window ) {
                    m_windowMap->emplaceShared( windowOffset, subchunk.window );
                } else {
                    m_windowMap->emplace( windowOffset, chunkData->getWindowAt( lastWindow, decodedOffsetInBlock ),
                                          chunkData->windowCompressionType() );
                    std::cerr << "[Info] The subchunk window for offset " << windowOffset << " is not compressed yet. "
                              << "Compressing it now might slow down the program.\n";
                }
            }
        }

        m_statistics.queuePostProcessingDuration += duration( t0 );
    }

    void
    postProcessChunk( const std::shared_ptr<ChunkData>& chunkData,
                      const SharedDecompressedWindow&   lastWindow )
    {
        if constexpr ( REPLACE_MARKERS_IN_PARALLEL ) {
            waitForReplacedMarkers( chunkData, lastWindow );
        } else {
            replaceMarkers( chunkData, *lastWindow, chunkData->windowCompressionType() );
        }
    }

    void
    waitForReplacedMarkers( const std::shared_ptr<ChunkData>& chunkData,
                            const SharedDecompressedWindow&   lastWindow )
    {
        using namespace std::chrono_literals;

        auto markerReplaceFuture = m_markersBeingReplaced.find( chunkData->encodedOffsetInBits );
        if ( ( markerReplaceFuture == m_markersBeingReplaced.end() ) && chunkData->hasBeenPostProcessed() ) {
            return;
        }

        const auto t0 = now();

        /* Not ready or not yet queued, so queue it and use the wait time to queue more marker replacements. */
        std::optional<std::future<void> > queuedFuture;
        if ( markerReplaceFuture == m_markersBeingReplaced.end() ) {
            /* First, we need to emplace the last window or else we cannot queue further blocks. */
            markerReplaceFuture = queueChunkForPostProcessing( chunkData, lastWindow );
        }

        /* Check other enqueued marker replacements whether they are finished. */
        for ( auto it = m_markersBeingReplaced.begin(); it != m_markersBeingReplaced.end(); ) {
            if ( it == markerReplaceFuture ) {
                ++it;
                continue;
            }

            auto& future = it->second;
            if ( !future.valid() || ( future.wait_for( 0s ) == std::future_status::ready ) ) {
                future.get();
                it = m_markersBeingReplaced.erase( it );
            } else {
                ++it;
            }
        }

        queuePrefetchedChunkPostProcessing();
        m_statistics.queuePostProcessingDuration += duration( t0 );

        markerReplaceFuture->second.get();
        m_markersBeingReplaced.erase( markerReplaceFuture );
    }

    void
    queuePrefetchedChunkPostProcessing()
    {
        /* Trigger jobs for ready block data to replace markers. */
        const auto& cacheElements = this->prefetchCache().contents();
        std::vector<size_t> sortedOffsets( cacheElements.size() );
        std::transform( cacheElements.begin(), cacheElements.end(), sortedOffsets.begin(),
                        [] ( const auto& keyValue ) { return keyValue.first; } );
        std::sort( sortedOffsets.begin(), sortedOffsets.end() );
        for ( const auto triedStartOffset : sortedOffsets ) {
            const auto chunkData = cacheElements.at( triedStartOffset );

            /* Ignore blocks already enqueued for marker replacement. */
            if ( m_markersBeingReplaced.find( chunkData->encodedOffsetInBits ) != m_markersBeingReplaced.end() ) {
                continue;
            }

            /* Ignore ready blocks. Do this check after the enqueued check above to avoid race conditions when
             * checking for markers while replacing markers in another thread. */
            if ( chunkData->hasBeenPostProcessed() ) {
                continue;
            }

            /* Check for previous window. */
            const auto sharedPreviousWindow = m_windowMap->get( chunkData->encodedOffsetInBits );
            if ( !sharedPreviousWindow ) {
                continue;
            }

            queueChunkForPostProcessing( chunkData, sharedPreviousWindow->decompress() );
        }
    }

    PostProcessingFutures::iterator
    queueChunkForPostProcessing( const std::shared_ptr<ChunkData>& chunkData,
                                 SharedDecompressedWindow          previousWindow )
    {
        const auto windowOffset = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;
        if ( !m_windowMap->get( windowOffset ) ) {
            /* The last window is always inserted into the window map by the main thread because else
             * it wouldn't be able queue the next chunk for post-processing in parallel. This is the critical
             * path that cannot be parallelized. Therefore, do not compress the last window to save time. */
            m_windowMap->emplace( windowOffset, chunkData->getLastWindow( *previousWindow ), CompressionType::NONE );
        }

        return m_markersBeingReplaced.emplace(
            chunkData->encodedOffsetInBits,
            this->submitTaskWithHighPriority(
                [chunkData, window = std::move( previousWindow )] () {
                    replaceMarkers( chunkData, *window, chunkData->windowCompressionType() );
                } ) ).first;
    }

    /**
     * Must be thread-safe because it is submitted to the thread pool.
     */
    static void
    replaceMarkers( const std::shared_ptr<ChunkData>& chunkData,
                    const WindowView                  previousWindow,
                    const CompressionType             windowCompressionType )
    {
        chunkData->applyWindow( previousWindow );

        const auto t0 = now();
        size_t decodedOffsetInBlock{ 0 };
        for ( auto& subchunk : chunkData->subchunks ) {
            decodedOffsetInBlock += subchunk.decodedSize;
            subchunk.window = std::make_shared<WindowMap::Window>(
                chunkData->getWindowAt( previousWindow, decodedOffsetInBlock ), windowCompressionType );
        }
        chunkData->statistics.compressWindowDuration += duration( t0 );
    }

    /**
     * First, tries to look up the given block offset by its partition offset and then by its real offset.
     *
     * @param blockOffset The real block offset, not a guessed one, i.e., also no partition offset!
     *        This is important because this offset is stored in the returned ChunkData as the real one.
     */
    [[nodiscard]] std::shared_ptr<ChunkData>
    getBlock( const size_t blockOffset,
              const size_t blockIndex )
    {
        const auto getPartitionOffsetFromOffset =
            [this] ( auto offset ) { return m_blockFinder->partitionOffsetContainingOffset( offset ); };
        const auto partitionOffset = getPartitionOffsetFromOffset( blockOffset );

        std::shared_ptr<ChunkData> chunkData;
        try {
            if ( BaseType::test( partitionOffset ) ) {
                chunkData = BaseType::get( partitionOffset, blockIndex, getPartitionOffsetFromOffset );
            }
        } catch ( const NoBlockInRange& ) {
            /* Trying to get the next block based on the partition offset is only a performance optimization.
             * It should succeed most of the time for good performance but is not required to and also might
             * sometimes not, e.g., when the deflate block finder failed to find any valid block inside the
             * partition, e.g., because it only contains fixed Huffman blocks. */
        }

        /* If we got a chunk matching the partition offset but the chunk does not match the actual desired offset,
         * then give a warning. No error, because below we simply request the actual offset directly in that case.
         * This warning will also appear when a chunk has preemptively quit decompressing, e.g., because the
         * compression ratio was too large. In that case, requests for the offset where the chunk has stopped,
         * will return the partition offset of the previous chunk and therefore will return a mismatching chunk.
         * Suppress this relative benign case.
         * @todo Get rid of the partition offset altogether and "simply" look in the chunk cache for ones where
         *       matchesEncodedOffset returns true. Note that this has problems when the chunk to test for has not
         *       yet found a viable start position. Therefore, it requires some locking and in the worst-case
         *       waiting or if we don't wait, it might result in the same chunk being decompressed twice, once
         *       as a prefetch starting from a guessed position and once as an on-demand fetch given the exact
         *       position. */
        if ( BaseType::statisticsEnabled() ) {
            if ( chunkData && !chunkData->matchesEncodedOffset( blockOffset ) && ( partitionOffset != blockOffset )
                 && ( m_statistics.preemptiveStopCount == 0 ) )
            {
                std::cerr << "[Info] Detected a performance problem. Decoding might take longer than necessary. "
                          << "Please consider opening a performance bug report with "
                          << "a reproducing compressed file. Detailed information:\n"
                          << "[Info] Found mismatching block. Need offset " << formatBits( blockOffset )
                          << ". Look in partition offset: " << formatBits( partitionOffset )
                          << ". Found possible range: ["
                          << formatBits( chunkData->encodedOffsetInBits ) << ", "
                          << formatBits( chunkData->maxEncodedOffsetInBits ) << "]\n";
            }
        }

        /* If we got no block or one with the wrong data, then try again with the real offset, not the
         * speculatively prefetched one. */
        if ( !chunkData
             || ( !chunkData->matchesEncodedOffset( blockOffset )
                  && ( partitionOffset != blockOffset ) ) )
        {
            try
            {
                /* This call given the exact block offset must always yield the correct data and should be equivalent
                 * to directly call @ref decodeBlock with that offset. */
                chunkData = BaseType::get( blockOffset, blockIndex, getPartitionOffsetFromOffset );
            }
            catch ( const rapidgzip::BitReader::EndOfFileReached& exception )
            {
                std::cerr << "Unexpected end of file when getting block at " << formatBits( blockOffset )
                          << " (block index: " << blockIndex << ") on demand\n";
                throw exception;
            }
        }

        if ( !chunkData || ( chunkData->encodedOffsetInBits == std::numeric_limits<size_t>::max() ) ) {
            std::stringstream message;
            message << "Decoding failed at block offset " << formatBits( blockOffset ) << "!";
            throw std::domain_error( std::move( message ).str() );
        }

        if ( !chunkData->matchesEncodedOffset( blockOffset ) ) {
            /* This error should be equivalent to trying to start to decode from the requested blockOffset
             * and failing to do so. It should only happen when a previous decodeBlock call did not stop
             * on a deflate block boundary. */
            std::stringstream message;
            message << "Got wrong block to searched offset! Looked for " << blockOffset
                    << " and looked up cache successively for estimated offset "
                    << partitionOffset << " but got block with actual offset ";
            if ( chunkData->encodedOffsetInBits == chunkData->maxEncodedOffsetInBits ) {
                message << chunkData->encodedOffsetInBits;
            } else {
                message << "[" << chunkData->encodedOffsetInBits << ", " << chunkData->maxEncodedOffsetInBits << "]";
            }
            throw std::logic_error( std::move( message ).str() );
        }

        return chunkData;
    }

    [[nodiscard]] ChunkData
    decodeBlock( size_t blockOffset,
                 size_t nextBlockOffset ) const override
    {
        /* The decoded size of the block is only for optimization purposes. Therefore, we do not have to take care
         * of the correct ordering between BlockMap accesses and modifications (the BlockMap is still thread-safe). */
        const auto blockInfo = m_blockMap->getEncodedOffset( blockOffset );

        ChunkConfiguration chunkDataConfiguration;
        chunkDataConfiguration.crc32Enabled = m_crc32Enabled;
        chunkDataConfiguration.fileType = m_blockFinder->fileType();
        chunkDataConfiguration.splitChunkSize = m_blockFinder->spacingInBits() / 8U;
        chunkDataConfiguration.windowCompressionType = m_windowCompressionType;

        /* If we are a BGZF file and we have not imported an index, then we can assume the
         * window to be empty because we should only get offsets at gzip stream starts.
         * If we have imported an index, then the block finder will be finalized, and it might be
         * possible that offsets were chosen in the middle of gzip streams, which would require windows. */
        auto sharedWindow = m_windowMap->get( blockOffset );
        if ( !sharedWindow && m_isBgzfFile && !m_blockFinder->finalized() ) {
            sharedWindow = std::make_shared<WindowMap::Window>();
        }

        return decodeBlock(
            m_sharedFileReader->clone(),
            blockOffset,
            /* untilOffset */
            ( blockInfo
              ? blockInfo->encodedOffsetInBits + blockInfo->encodedSizeInBits
              : nextBlockOffset ),
            std::move( sharedWindow ),
            /* decodedSize */ blockInfo ? blockInfo->decodedSizeInBytes : std::optional<size_t>{},
            m_cancelThreads,
            chunkDataConfiguration,
            m_maxDecompressedChunkSize,
            /* untilOffsetIsExact */ m_isBgzfFile || blockInfo );
    }

public:
    [[nodiscard]] static ChunkData
    decodeUnknownBzip2Chunk( bzip2::BitReader*     const bitReader,
                             size_t                const untilOffset,
                             std::optional<size_t> const decodedSize,
                             ChunkConfiguration   const& chunkDataConfiguration,
                             size_t                const maxDecompressedChunkSize )
    {
        if ( bitReader == nullptr ) {
            throw std::invalid_argument( "BitReader must be non-null!" );
        }

        ChunkData result{ chunkDataConfiguration };
        result.encodedOffsetInBits = bitReader->tell();

        /* If true, then read the gzip header. We cannot simply check the gzipHeader optional because we might
         * start reading in the middle of a gzip stream and will not meet the gzip header for a while or never. */
        bool isAtStreamEnd = false;
        size_t totalBytesRead = 0;

        /* Loop over possibly gzip streams and deflate blocks. We cannot use GzipReader even though it does
         * something very similar because GzipReader only works with fully decodable streams but we
         * might want to return buffer with placeholders in case we don't know the initial window, yet! */
        size_t nextBlockOffset{ 0 };
        while ( true )
        {
            if ( isAtStreamEnd ) {
                bzip2::readBzip2Header( *bitReader );
                isAtStreamEnd = false;
            }

            nextBlockOffset = bitReader->tell();

            /** @todo does this work when quitting on an empty block, i.e., if the next block is an end-of-stream one?
             *        test decodeUnknownBzip2Chunk with all block offsets */
            if ( totalBytesRead >= maxDecompressedChunkSize ) {
                result.stoppedPreemptively = true;
                break;
            }

            /* This also reads the header and will throw on errors. */
            std::optional<bzip2::Block> block;
            try {
                block.emplace( *bitReader );
                if ( !block->eos() ) {
                    block->readBlockData();
                }
            } catch ( const bzip2::BitReader::EndOfFileReached& exception ) {
                /* Encountering EOF while reading the (first bit for the) deflate block header is only
                 * valid if it is the very first deflate block given to us. Else, it should not happen
                 * because the final block bit should be set in the previous deflate block. */
                if ( bitReader->tell() == result.encodedOffsetInBits ) {
                    break;
                }
                throw exception;
            }

            /* Preemptive Stop Condition. */
            if ( ( ( nextBlockOffset >= untilOffset ) && !block->eos() ) || ( nextBlockOffset == untilOffset ) ) {
                break;
            }

            /* Do not push back the first boundary because it is redundant as it should contain the same encoded
             * offset as @ref result and it also would have the same problem that the real offset is ambiguous
             * for non-compressed blocks. */
            if ( totalBytesRead > 0 ) {
                result.appendDeflateBlockBoundary( nextBlockOffset, totalBytesRead );
            }

            /* In contrast to deflate, bzip2 has dedicated end-of-stream blocks, which do not contain any data.
             * Therefore, we need to check for it before calling block.decodeBlock. */
            if ( block->eos() ) {
                const auto footerOffset = bitReader->tell();
                typename ChunkData::Footer footer;
                footer.blockBoundary = { footerOffset, totalBytesRead };
                result.appendFooter( std::move( footer ) );

                isAtStreamEnd = true;

                if ( bitReader->eof() ) {
                    nextBlockOffset = bitReader->tell();
                    break;
                }
                continue;
            }

            /* Loop until we have read the full contents of the current block. */
            size_t blockBytesRead{ 0 };
            while ( true )
            {
                const auto suggestedDecodeSize = decodedSize.value_or( ALLOCATION_CHUNK_SIZE );
                deflate::DecodedVector subchunk(
                    suggestedDecodeSize > totalBytesRead
                    ? std::min( ALLOCATION_CHUNK_SIZE, suggestedDecodeSize - totalBytesRead )
                    : ALLOCATION_CHUNK_SIZE );

                size_t nBytesRead = 0;
                while ( nBytesRead < subchunk.size() ) {
                    const auto nBytesReadPerCall = block->read(
                        subchunk.size() - nBytesRead,
                        reinterpret_cast<char*>( subchunk.data() ) + nBytesRead );
                    if ( nBytesReadPerCall == 0 ) {
                        break;
                    }
                    nBytesRead += nBytesReadPerCall;
                }

                subchunk.resize( nBytesRead );
                subchunk.shrink_to_fit();
                result.append( std::move( subchunk ) );

                blockBytesRead += nBytesRead;
                totalBytesRead += nBytesRead;

                if ( nBytesRead == 0 ) {
                    break;
                }

                /**
                 * > Because of the first-stage RLE compression (see above), the maximum length of plaintext
                 * > that a single 900 kB bzip2 block can contain is around 46 MB (45,899,236 bytes).
                 * @see https://en.wikipedia.org/wiki/Bzip2
                 * Note that @ref maxDecompressedChunkSize is still necessary because this only limits the
                 * decoded size of a single bzip2 block, while a chunk can contain multiple such blocks.
                 * > An even smaller file of 40 bytes can be achieved by using an input containing entirely
                 * > values of 251, an apparent compression ratio of 1147480.9:1.
                 * This makes chunk splitting and @ref maxDecompressedChunkSize still a requirement.
                 */
                if ( blockBytesRead > 64_Mi ) {
                    throw std::runtime_error( "A single bzip2 block that decompresses to more than 64 MiB was "
                                              "encountered. This is not supported to avoid out-of-memory errors." );
                }
            }
        }

        result.finalize( nextBlockOffset );
        return result;
    }


    [[nodiscard]] static ChunkData
    decodeBzip2Chunk( UniqueFileReader       && sharedFileReader,
                      size_t              const chunkOffset,
                      size_t              const untilOffset,
                      std::atomic<bool>  const& cancelThreads,
                      ChunkConfiguration const& chunkDataConfiguration,
                      size_t              const maxDecompressedChunkSize )
    {
        bzip2::BitReader bitReader( sharedFileReader->clone() );

        const auto tryToDecode =
            [&] ( const size_t offset ) -> std::optional<ChunkData>
            {
                try {
                    bitReader.seek( offset );
                    auto result = decodeUnknownBzip2Chunk( &bitReader, untilOffset, /* decodedSize */ std::nullopt,
                                                           chunkDataConfiguration, maxDecompressedChunkSize );
                    result.encodedOffsetInBits = offset;
                    result.maxEncodedOffsetInBits = offset;
                    result.encodedSizeInBits = result.encodedEndOffsetInBits - result.encodedOffsetInBits;
                    return result;
                } catch ( const std::exception& ) {
                    /* Ignore errors and try next block candidate. This is very likely to happen if @ref blockOffset
                     * is only an estimated offset! */
                }
                return std::nullopt;
            };

        if ( auto result = tryToDecode( chunkOffset ); result ) {
            return *std::move( result );
        }

        const auto blockFinderOffsetInBytes = chunkOffset / BYTE_SIZE;
        sharedFileReader->seek( blockFinderOffsetInBytes );
        BitStringFinder<bzip2::MAGIC_BITS_SIZE> blockFinder{
            std::move( sharedFileReader ), bzip2::MAGIC_BITS_BLOCK, /* fileBufferSizeBytes */ 64_Ki };
        while ( !cancelThreads ) {
            const auto foundRelativeOffset = blockFinder.find();
            if ( foundRelativeOffset == std::numeric_limits<size_t>::max() ) {
                break;
            }

            const auto blockOffset = blockFinderOffsetInBytes * BYTE_SIZE + foundRelativeOffset;
            if ( blockOffset >= untilOffset ) {
                break;
            }

            if ( blockOffset >= chunkOffset ) {
                auto result = tryToDecode( blockOffset );
                if ( result ) {
                    return *std::move( result );
                }
            }
        }

        std::stringstream message;
        message << "Failed to find any valid bzip2 block in [" << formatBits( chunkOffset )
                << ", " << formatBits( untilOffset ) << ")";
        throw NoBlockInRange( std::move( message ).str() );
    }

    /**
     * @param untilOffset Decode to excluding at least this compressed offset. It can be the offset of the next
     *                    deflate block or next gzip stream but it can also be the starting guess for the block finder
     *                    to find the next deflate block or gzip stream.
     * @param initialWindow Required to resume decoding. Can be empty if, e.g., the blockOffset is at the gzip stream
     *                      start.
     */
    [[nodiscard]] static ChunkData
    decodeBlock( UniqueFileReader             && sharedFileReader,
                 size_t                    const blockOffset,
                 size_t                    const untilOffset,
                 SharedWindow              const initialWindow,
                 std::optional<size_t>     const decodedSize,
                 std::atomic<bool>        const& cancelThreads,
                 ChunkConfiguration       const& chunkDataConfiguration,
                 size_t                    const maxDecompressedChunkSize = std::numeric_limits<size_t>::max(),
                 bool                      const untilOffsetIsExact = false )
    {
        if ( chunkDataConfiguration.fileType == FileType::BZIP2 ) {
            return decodeBzip2Chunk( std::move( sharedFileReader ), blockOffset, untilOffset, cancelThreads,
                                     chunkDataConfiguration, maxDecompressedChunkSize );
        }

        if ( initialWindow && untilOffsetIsExact ) {
        #ifdef WITH_ISAL
            using InflateWrapper = IsalInflateWrapper;
        #else
            using InflateWrapper = ZlibInflateWrapper;
        #endif

            const auto fileSize = sharedFileReader->size();
            const auto window = initialWindow->decompress();

            auto configuration = chunkDataConfiguration;
            configuration.encodedOffsetInBits = blockOffset;
            auto result = decodeBlockWithInflateWrapper<InflateWrapper>(
                std::move( sharedFileReader ),
                fileSize ? std::min( untilOffset, *fileSize * BYTE_SIZE ) : untilOffset,
                *window,
                decodedSize,
                configuration );

            if ( decodedSize && ( result.decodedSizeInBytes != *decodedSize ) ) {
                std::stringstream message;
                message << "Decoded chunk size does not match the requested decoded size!\n"
                        << "  Block offset          : " << blockOffset << " b\n"
                        << "  Until offset          : " << untilOffset << " b\n"
                        << "  Encoded size          : " << ( untilOffset - blockOffset ) << " b\n"
                        << "  Actual encoded size   : " << result.encodedSizeInBits << " b\n"
                        << "  Decoded size          : " << result.decodedSizeInBytes << " B\n"
                        << "  Expected decoded size : " << *decodedSize << " B\n"
                        << "  Until offset is exact : " << untilOffsetIsExact << "\n"
                        << "  Initial Window        : " << std::to_string( window->size() ) << "\n";
                throw std::runtime_error( std::move( message ).str() );
            }

            return result;
        }

        BitReader bitReader( std::move( sharedFileReader ) );
        if ( initialWindow ) {
            bitReader.seek( blockOffset );
            const auto window = initialWindow->decompress();
            return decodeBlockWithRapidgzip( &bitReader, untilOffset, *window, maxDecompressedChunkSize,
                                              chunkDataConfiguration );
        }

        const auto tryToDecode =
            [&] ( const std::pair<size_t, size_t>& offset ) -> std::optional<ChunkData>
            {
                try {
                    /* For decoding, it does not matter whether we seek to offset.first or offset.second but it did
                     * matter a lot for interpreting and correcting the encodedSizeInBits in GzipBlockFetcer::get! */
                    bitReader.seek( offset.second );
                    auto result = decodeBlockWithRapidgzip(
                        &bitReader, untilOffset, /* initialWindow */ std::nullopt,
                        maxDecompressedChunkSize, chunkDataConfiguration );
                    result.encodedOffsetInBits = offset.first;
                    result.maxEncodedOffsetInBits = offset.second;
                    result.encodedSizeInBits = result.encodedEndOffsetInBits - result.encodedOffsetInBits;
                    /** @todo Avoid out of memory issues for very large compression ratios by using a simple runtime
                     *        length encoding or by only undoing the Huffman coding in parallel and the LZ77 serially,
                     *        or by stopping decoding at a threshold and fall back to serial decoding in that case? */
                    return result;
                } catch ( const std::exception& exception ) {
                    /* Ignore errors and try next block candidate. This is very likely to happen if @ref blockOffset
                     * is only an estimated offset! If it happens because decodeBlockWithRapidgzip has a bug, then it
                     * might indirectly trigger an exception when the next required block offset cannot be found. */
                }
                return std::nullopt;
            };

        /* First simply try to decode at the current position to avoid expensive block finders in the case
         * that for some reason the @ref blockOffset guess was perfect. Note that this has to be added as
         * a separate stop condition for decoding the previous block! */
        if ( auto result = tryToDecode( { blockOffset, blockOffset } ); result ) {
            return *std::move( result );
        }

        /**
         * Performance when looking for dynamic and uncompressed blocks:
         * @verbatim
         * m rapidgzip && time src/tools/rapidgzip -P 0 -d -c 4GiB-base64.gz | wc -c
         *    read in total 5.25998e+09 B out of 3263906195 B, i.e., read the file 1.61156 times
         *    Decompressed in total 4294967296 B in 4.35718 s -> 985.722 MB/s
         *
         * m rapidgzip && time src/tools/rapidgzip -P 0 -d -c random.bin.gz | wc -c
         *    read in total 2.41669e+09 B out of 2148139037 B, i.e., read the file 1.12502 times
         *    Decompressed in total 2147483648 B in 1.40283 s -> 1530.82 MB/s
         * @endverbatim
         *
         * Performance when looking only for dynamic blocks:
         * @verbatim
         * m rapidgzip && time src/tools/rapidgzip -P 0 -d -c 4GiB-base64.gz | wc -c
         *    read in total 3.67191e+09 B out of 3263906195 B, i.e., read the file 1.12501 times
         *    Decompressed in total 4294967296 B in 3.0489 s -> 1408.69 MB/s
         *  -> Almost 50% faster! And only 12% file read overhead instead of 61%!
         *
         * m rapidgzip && time src/tools/rapidgzip -P 0 -d -c random.bin.gz | wc -c
         *   -> LONGER THAN 3 MIN!
         * @endverbatim
         *
         * Performance after implementing the chunked alternating search:
         * @verbatim
         * m rapidgzip && time src/tools/rapidgzip -P 0 -d -c 4GiB-base64.gz | wc -c
         *    read in total 3.68686e+09 B out of 3263906195 B, i.e., read the file 1.12958 times
         *    Decompressed in total 4294967296 B in 3.06287 s -> 1402.27 MB/s
         *
         * m rapidgzip && time src/tools/rapidgzip -P 0 -d -c random.bin.gz | wc -c
         *    read in total 2.41669e+09 B out of 2148139037 B, i.e., read the file 1.12502 times
         *    Decompressed in total 2147483648 B in 1.31924 s -> 1627.82 MB/s
         * @endverbatim
         */

        const auto findNextDynamic =
            [&] ( size_t beginOffset, size_t endOffset ) {
                if ( beginOffset >= endOffset ) {
                    return std::numeric_limits<size_t>::max();
                }
                bitReader.seek( beginOffset );
                return blockfinder::seekToNonFinalDynamicDeflateBlock( bitReader, endOffset );
            };

        const auto findNextUncompressed =
            [&] ( size_t beginOffset, size_t endOffset ) {
                if ( beginOffset >= endOffset ) {
                    return std::make_pair( std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() );
                }
                bitReader.seek( beginOffset );
                return blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader, endOffset );
            };

        /**
         * 1. Repeat for each chunk:
         *    1. Initialize both offsets with possible matches inside the current chunk.
         *    2. Repeat until both offsets are invalid because no further matches were found in the chunk:
         *       1. Try decoding the earlier offset.
         *       2. Update the used offset by searching from last position + 1 until the chunk end.
         */
        const auto tBlockFinderStart = now();
        static constexpr auto CHUNK_SIZE = 8_Ki * BYTE_SIZE;
        size_t falsePositiveCount{ 0 };
        for ( auto chunkBegin = blockOffset; chunkBegin < untilOffset; chunkBegin += CHUNK_SIZE ) {
            /* Only look in the first 512 KiB of data. If nothing can be found there, then something is likely
             * to be wrong with the file and looking in the rest will also likely fail. And looking in the whole
             * range to be decompressed is multiple times slower than decompression because of the slower
             * block finder and because it requires intensive seeking for false non-compressed block positives. */
            if ( cancelThreads || ( chunkBegin - blockOffset >= 512_Ki * BYTE_SIZE ) ) {
                break;
            }

            const auto chunkEnd = std::min( static_cast<size_t>( chunkBegin + CHUNK_SIZE ), untilOffset );

            auto uncompressedOffsetRange = findNextUncompressed( chunkBegin, chunkEnd );
            auto dynamicHuffmanOffset = findNextDynamic( chunkBegin, chunkEnd );

            while ( ( uncompressedOffsetRange.first < chunkEnd ) || ( dynamicHuffmanOffset < chunkEnd ) ) {
                if ( cancelThreads ) {
                    break;
                }

                /* Choose the lower offset to test next. */
                std::pair<size_t, size_t> offsetToTest;
                if ( dynamicHuffmanOffset < uncompressedOffsetRange.first ) {
                    offsetToTest = { dynamicHuffmanOffset, dynamicHuffmanOffset };
                    dynamicHuffmanOffset = findNextDynamic( dynamicHuffmanOffset + 1, chunkEnd );
                } else {
                    offsetToTest = uncompressedOffsetRange;
                    uncompressedOffsetRange = findNextUncompressed( uncompressedOffsetRange.second + 1, chunkEnd );
                }

                /* Try decoding and measure the time. */
                const auto tBlockFinderStop = now();
                if ( auto result = tryToDecode( offsetToTest ); result ) {
                    result->statistics.blockFinderDuration = duration( tBlockFinderStart, tBlockFinderStop );
                    result->statistics.decodeDuration = duration( tBlockFinderStop );
                    result->statistics.falsePositiveCount = falsePositiveCount;
                    return *std::move( result );
                }

                falsePositiveCount++;
            }
        }

        std::stringstream message;
        message << "Failed to find any valid deflate block in [" << formatBits( blockOffset )
                << ", " << formatBits( untilOffset ) << ")";
        throw NoBlockInRange( std::move( message ).str() );
    }


    /**
     * @param decodedSize If given, it is used to avoid overallocations. It is NOT used as a stop condition.
     * @param exactUntilOffset Decompress until this known bit offset in the encoded stream. It must lie on
     *                         a deflate block boundary.
     */
    template<typename InflateWrapper>
    [[nodiscard]] static ChunkData
    decodeBlockWithInflateWrapper( UniqueFileReader         && sharedFileReader,
                                   size_t                const exactUntilOffset,
                                   WindowView            const initialWindow,
                                   std::optional<size_t> const decodedSize,
                                   ChunkConfiguration   const& chunkDataConfiguration )
    {
        const auto tStart = now();

        ChunkData result{ chunkDataConfiguration };

        BitReader bitReader( std::move( sharedFileReader ) );
        bitReader.seek( result.encodedOffsetInBits );
        InflateWrapper inflateWrapper( std::move( bitReader ), exactUntilOffset );
        inflateWrapper.setWindow( initialWindow );
        inflateWrapper.setFileType( result.fileType );

        const auto appendFooter =
            [&] ( size_t encodedOffset,
                  size_t decodedOffset,
                  const typename InflateWrapper::Footer& footer )
            {
                result.appendFooter( encodedOffset, decodedOffset, footer.gzipFooter );
                return;
                switch ( result.fileType )
                {
                case FileType::BGZF:
                case FileType::GZIP:
                    result.appendFooter( encodedOffset, decodedOffset, footer.gzipFooter );
                    break;

                case FileType::ZLIB:
                    result.appendFooter( encodedOffset, decodedOffset, footer.zlibFooter );
                    break;

                case FileType::NONE:
                case FileType::DEFLATE:
                    result.appendFooter( encodedOffset, decodedOffset );
                    break;

                case FileType::BZIP2:
                    throw std::logic_error( "[GzipChunkFetcher::decodeBlockWithInflateWrapper] Invalid file type!" );
                }
            };

        size_t alreadyDecoded{ 0 };
        while( true ) {
            const auto suggestedDecodeSize = decodedSize.value_or( ALLOCATION_CHUNK_SIZE );
            deflate::DecodedVector subchunk( suggestedDecodeSize > alreadyDecoded
                                             ? std::min( ALLOCATION_CHUNK_SIZE, suggestedDecodeSize - alreadyDecoded )
                                             : ALLOCATION_CHUNK_SIZE );
            std::optional<typename InflateWrapper::Footer> footer;

            /* In order for CRC32 verification to work, we have to append at most one gzip stream per subchunk
             * because the CRC32 calculator is swapped inside ChunkData::append. That's why the stop condition
             * tests for footer.has_value(). */
            size_t nBytesRead = 0;
            size_t nBytesReadPerCall{ 0 };
            for ( ; ( nBytesRead < subchunk.size() ) && !footer; nBytesRead += nBytesReadPerCall ) {
                std::tie( nBytesReadPerCall, footer ) = inflateWrapper.readStream( subchunk.data() + nBytesRead,
                                                                                   subchunk.size() - nBytesRead );
                if ( ( nBytesReadPerCall == 0 ) && !footer ) {
                    break;
                }
            }

            alreadyDecoded += nBytesRead;

            subchunk.resize( nBytesRead );
            subchunk.shrink_to_fit();
            result.append( std::move( subchunk ) );
            if ( footer ) {
                appendFooter( footer->footerEndEncodedOffset, alreadyDecoded, *footer );
            }

            if ( ( nBytesReadPerCall == 0 ) && !footer ) {
                break;
            }
        }

        uint8_t dummy{ 0 };
        const auto [nBytesReadPerCall, footer] = inflateWrapper.readStream( &dummy, 1 );
        if ( ( nBytesReadPerCall == 0 ) && footer ) {
            appendFooter( footer->footerEndEncodedOffset, alreadyDecoded, *footer );
        }

        if ( exactUntilOffset != inflateWrapper.tellCompressed() ) {
            std::stringstream message;
            message << "The inflate wrapper offset (" << inflateWrapper.tellCompressed() << ") "
                    << "does not match the requested exact stop offset: " << exactUntilOffset << ". "
                    << "The archive or the index may be corrupted or the stop condition might contain a bug. "
                    << "Decoded: " << alreadyDecoded << " B";
            if ( decodedSize ) {
                 message << " out of requested " << *decodedSize << " B";
            }
            message << ", started at offset: " << result.encodedOffsetInBits << ".";
            throw std::runtime_error( std::move( message ).str() );
        }

        result.finalize( exactUntilOffset );
        result.statistics.decodeDurationInflateWrapper = duration( tStart );
        return result;
    }


#ifdef WITH_ISAL
    /**
     * This is called from @ref decodeBlockWithRapidgzip in case the window has been fully resolved so that
     * normal decompression instead of two-staged one becomes possible.
     *
     * @param untilOffset In contrast to @ref decodeBlockWithInflateWrapper, this may be an inexact guess
     *                    from which another thread starts decoding!
     * @note This code is copy-pasted from decodeBlockWithInflateWrapper and adjusted to use the stopping
     *       points and deflate block properties as stop criterion.
     */
    [[nodiscard]] static ChunkData
    finishDecodeBlockWithIsal( BitReader* const bitReader,
                               size_t     const untilOffset,
                               WindowView const initialWindow,
                               size_t     const maxDecompressedChunkSize,
                               ChunkData&&      result )
    {
        if ( bitReader == nullptr ) {
            throw std::invalid_argument( "BitReader may not be nullptr!" );
        }

        const auto tStart = now();
        auto nextBlockOffset = bitReader->tell();
        bool stoppingPointReached{ false };
        auto alreadyDecoded = result.size();

        if ( ( alreadyDecoded > 0 ) && !bitReader->eof() ) {
            result.appendDeflateBlockBoundary( nextBlockOffset, alreadyDecoded );
        }

        IsalInflateWrapper inflateWrapper{ BitReader( *bitReader ) };
        inflateWrapper.setFileType( result.fileType );
        inflateWrapper.setWindow( initialWindow );
        inflateWrapper.setStoppingPoints( static_cast<StoppingPoint>( StoppingPoint::END_OF_BLOCK |
                                                                      StoppingPoint::END_OF_BLOCK_HEADER |
                                                                      StoppingPoint::END_OF_STREAM_HEADER ) );

        const auto appendFooter =
            [&] ( size_t encodedOffset,
                  size_t decodedOffset,
                  const IsalInflateWrapper::Footer& footer )
            {
                switch ( result.fileType )
                {
                case FileType::BGZF:
                case FileType::GZIP:
                    result.appendFooter( encodedOffset, decodedOffset, footer.gzipFooter );
                    break;

                case FileType::ZLIB:
                    result.appendFooter( encodedOffset, decodedOffset, footer.zlibFooter );
                    break;

                case FileType::NONE:
                case FileType::DEFLATE:
                    result.appendFooter( encodedOffset, decodedOffset );
                    break;

                case FileType::BZIP2:
                    throw std::logic_error( "[GzipChunkFetcher::finishDecodeBlockWithIsal] Invalid file type!" );
                }
            };

        while( !stoppingPointReached ) {
            deflate::DecodedVector subchunk( ALLOCATION_CHUNK_SIZE );
            std::optional<IsalInflateWrapper::Footer> footer;

            /* In order for CRC32 verification to work, we have to append at most one gzip stream per subchunk
             * because the CRC32 calculator is swapped inside ChunkData::append. */
            size_t nBytesRead = 0;
            size_t nBytesReadPerCall{ 0 };
            while ( ( nBytesRead < subchunk.size() ) && !footer && !stoppingPointReached ) {
                std::tie( nBytesReadPerCall, footer ) = inflateWrapper.readStream( subchunk.data() + nBytesRead,
                                                                                   subchunk.size() - nBytesRead );
                nBytesRead += nBytesReadPerCall;

                /* We cannot stop decoding after a final block because the following decoder does not
                 * expect to start a gzip footer. Put another way, we are interested in START_OF_BLOCK
                 * not END_OF_BLOCK and therefore we have to infer one from the other. */
                bool isBlockStart{ false };

                switch ( inflateWrapper.stoppedAt() )
                {
                case StoppingPoint::END_OF_STREAM_HEADER:
                    isBlockStart = true;
                    break;

                case StoppingPoint::END_OF_BLOCK:
                    isBlockStart = !inflateWrapper.isFinalBlock();
                    break;

                case StoppingPoint::END_OF_BLOCK_HEADER:
                    if ( ( ( nextBlockOffset >= untilOffset )
                           && !inflateWrapper.isFinalBlock()
                           && ( inflateWrapper.compressionType() != deflate::CompressionType::FIXED_HUFFMAN ) )
                         || ( nextBlockOffset == untilOffset ) ) {
                        stoppingPointReached = true;
                    }
                    break;

                case StoppingPoint::NONE:
                    if ( ( nBytesReadPerCall == 0 ) && !footer ) {
                        stoppingPointReached = true;
                    }
                    break;

                default:
                    throw std::logic_error( "Got stopping point of a type that was not requested!" );
                }

                if ( isBlockStart ) {
                    nextBlockOffset = inflateWrapper.tellCompressed();

                    /* Do not push back the first boundary because it is redundant as it should contain the same encoded
                     * offset as @ref result and it also would have the same problem that the real offset is ambiguous
                     * for non-compressed blocks. */
                    if ( alreadyDecoded + nBytesRead > 0 ) {
                        result.appendDeflateBlockBoundary( nextBlockOffset, alreadyDecoded + nBytesRead );
                    }

                    if ( alreadyDecoded >= maxDecompressedChunkSize ) {
                        stoppingPointReached = true;
                        result.stoppedPreemptively = true;
                        break;
                    }
                }
            }

            alreadyDecoded += nBytesRead;

            subchunk.resize( nBytesRead );
            subchunk.shrink_to_fit();
            result.append( std::move( subchunk ) );
            if ( footer ) {
                nextBlockOffset = inflateWrapper.tellCompressed();
                appendFooter( footer->footerEndEncodedOffset, alreadyDecoded, *footer );
            }

            if ( ( inflateWrapper.stoppedAt() == StoppingPoint::NONE )
                 && ( nBytesReadPerCall == 0 ) && !footer ) {
                break;
            }
        }

        uint8_t dummy{ 0 };
        const auto [nBytesReadPerCall, footer] = inflateWrapper.readStream( &dummy, 1 );
        if ( ( inflateWrapper.stoppedAt() == StoppingPoint::NONE ) && ( nBytesReadPerCall == 0 ) && footer ) {
            nextBlockOffset = inflateWrapper.tellCompressed();
            appendFooter( footer->footerEndEncodedOffset, alreadyDecoded, *footer );
        }

        result.finalize( nextBlockOffset );
        result.statistics.decodeDurationIsal = duration( tStart );
        /**
         * Without the std::move, performance is halved! It seems like copy elision on return does not work
         * with function arguments! @see https://en.cppreference.com/w/cpp/language/copy_elision
         * > In a return statement, when the operand is the name of a non-volatile object with automatic
         * > storage duration, **which isn't a function parameter** [...]
         * And that is only the non-mandatory copy elision, which isn't even guaranteed in C++17!
         */
        return std::move( result );
    }
#endif  // ifdef WITH_ISAL


    [[nodiscard]] static ChunkData
    decodeBlockWithRapidgzip( BitReader*                const bitReader,
                              size_t                    const untilOffset,
                              std::optional<WindowView> const initialWindow,
                              size_t                    const maxDecompressedChunkSize,
                              ChunkConfiguration       const& chunkDataConfiguration )
    {
        if ( bitReader == nullptr ) {
            throw std::invalid_argument( "BitReader must be non-null!" );
        }

        ChunkData result{ chunkDataConfiguration };
        result.encodedOffsetInBits = bitReader->tell();

    #ifdef WITH_ISAL
        if ( initialWindow ) {
            return finishDecodeBlockWithIsal( bitReader, untilOffset, *initialWindow, maxDecompressedChunkSize,
                                              std::move( result ) );
        }
    #endif

        /* If true, then read the gzip header. We cannot simply check the gzipHeader optional because we might
         * start reading in the middle of a gzip stream and will not meet the gzip header for a while or never. */
        bool isAtStreamEnd = false;
        size_t streamBytesRead = 0;
        size_t totalBytesRead = 0;
        bool didReadHeader{ false };

        std::optional<deflate::Block<> > block;
        block.emplace();
        if ( initialWindow ) {
            block->setInitialWindow( *initialWindow );
        }

        /* Loop over possibly gzip streams and deflate blocks. We cannot use GzipReader even though it does
         * something very similar because GzipReader only works with fully decodable streams but we
         * might want to return buffer with placeholders in case we don't know the initial window, yet! */
        size_t nextBlockOffset{ 0 };
    #ifdef WITH_ISAL
        size_t cleanDataCount{ 0 };
    #endif
        while ( true )
        {
            if ( isAtStreamEnd ) {
                const auto headerOffset = bitReader->tell();
                auto error = Error::NONE;

                switch ( result.fileType )
                {
                case FileType::NONE:
                case FileType::BZIP2:
                    throw std::logic_error( "[GzipChunkFetcher::decodeBlockWithRapidgzip] Invalid file type!" );
                case FileType::BGZF:
                case FileType::GZIP:
                    error = gzip::readHeader( *bitReader ).second;
                    break;
                case FileType::ZLIB:
                    error = zlib::readHeader( *bitReader ).second;
                    break;
                case FileType::DEFLATE:
                    error = Error::NONE;
                    break;
                }

                if ( error != Error::NONE ) {
                    if ( error == Error::END_OF_FILE ) {
                        break;
                    }
                    std::stringstream message;
                    message << "Failed to read gzip/zlib header at offset " << formatBits( headerOffset )
                            << " because of error: " << toString( error );
                    throw std::domain_error( std::move( message ).str() );
                }

            #ifdef WITH_ISAL
                return finishDecodeBlockWithIsal( bitReader, untilOffset, /* initialWindow */ {},
                                                  maxDecompressedChunkSize, std::move( result ) );
            #endif

                didReadHeader = true;
                block.emplace();
                block->setInitialWindow();

                isAtStreamEnd = false;
            }

            nextBlockOffset = bitReader->tell();

            if ( totalBytesRead >= maxDecompressedChunkSize ) {
                result.stoppedPreemptively = true;
                break;
            }

        #ifdef WITH_ISAL
            if ( cleanDataCount >= deflate::MAX_WINDOW_SIZE ) {
                return finishDecodeBlockWithIsal( bitReader, untilOffset, result.getLastWindow( {} ),
                                                  maxDecompressedChunkSize, std::move( result ) );
            }
        #endif

            if ( auto error = block->readHeader( *bitReader ); error != Error::NONE ) {
                /* Encountering EOF while reading the (first bit for the) deflate block header is only
                 * valid if it is the very first deflate block given to us. Else, it should not happen
                 * because the final block bit should be set in the previous deflate block. */
                if ( ( error == Error::END_OF_FILE ) && ( bitReader->tell() == result.encodedOffsetInBits ) ) {
                    break;
                }

                std::stringstream message;
                message << "Failed to read deflate block header at offset " << formatBits( result.encodedOffsetInBits )
                        << " (position after trying: " << formatBits( bitReader->tell() ) << ": "
                        << toString( error );
                throw std::domain_error( std::move( message ).str() );
            }

            /**
             * Preemptive Stop Condition.
             * @note It is only important for performance that the deflate blocks we are matching here are the same
             *       as the block finder will find.
             * @note We do not have to check for an uncompressed block padding of zero because the deflate decoder
             *       counts that as an error anyway!
             */
            if ( ( ( nextBlockOffset >= untilOffset )
                   && !block->isLastBlock()
                   && ( block->compressionType() != deflate::CompressionType::FIXED_HUFFMAN ) )
                 || ( nextBlockOffset == untilOffset ) ) {
                break;
            }

            /* Do not push back the first boundary because it is redundant as it should contain the same encoded
             * offset as @ref result and it also would have the same problem that the real offset is ambiguous
             * for non-compressed blocks. */
            if ( totalBytesRead > 0 ) {
                result.appendDeflateBlockBoundary( nextBlockOffset, totalBytesRead );
            }

            /* Loop until we have read the full contents of the current deflate block-> */
            size_t blockBytesRead{ 0 };
            while ( !block->eob() )
            {
                const auto [bufferViews, error] = block->read( *bitReader, std::numeric_limits<size_t>::max() );
                if ( error != Error::NONE ) {
                    std::stringstream message;
                    message << "Failed to decode deflate block at " << formatBits( result.encodedOffsetInBits )
                            << " because of: " << toString( error );
                    throw std::domain_error( std::move( message ).str() );
                }

            #ifdef WITH_ISAL
                cleanDataCount += bufferViews.dataSize();
            #endif

                result.append( bufferViews );
                blockBytesRead += bufferViews.size();

                /* Non-compressed deflate blocks are limited to 64 KiB and the largest Dynamic Huffman Coding
                 * deflate blocks I have seen were 128 KiB in compressed size. With a maximum compression
                 * ratio of 1032, this would result in ~128 MiB. Fortunately, simple runs of zeros did compress
                 * to only 8 KiB blocks, i.e., ~8 MiB decompressed.
                 * However, igzip -0 can compress the whole file in a single deflate block.
                 * Decompressing such a file is not supported (yet). It would require some heavy
                 * refactoring of the ChunkData class to support resuming the decompression so that
                 * we can simply break and return here instead of throwing an exception. This would basically
                 * require putting a whole GzipReader in the ChunkData so that even random access is supported
                 * in an emulated manner. */
                if ( blockBytesRead > 256_Mi ) {
                    throw std::runtime_error( "A single deflate block that decompresses to more than 256 MiB was "
                                              "encountered. This is not supported to avoid out-of-memory errors." );
                }
            }
            streamBytesRead += blockBytesRead;
            totalBytesRead += blockBytesRead;

            if ( block->isLastBlock() ) {
                switch ( result.fileType )
                {
                case FileType::NONE:
                case FileType::BZIP2:
                    throw std::logic_error( "Cannot decode stream if the file type is not specified!" );

                case FileType::DEFLATE:
                    if ( bitReader->tell() % BYTE_SIZE != 0 ) {
                        bitReader->read( BYTE_SIZE - bitReader->tell() % BYTE_SIZE );
                    }
                    result.appendFooter( bitReader->tell(), totalBytesRead );
                    break;

                case FileType::ZLIB:
                {
                    const auto footer = zlib::readFooter( *bitReader );
                    const auto footerOffset = bitReader->tell();
                    result.appendFooter( footerOffset, totalBytesRead, footer );
                    break;
                }

                case FileType::BGZF:
                case FileType::GZIP:
                {
                    const auto footer = gzip::readFooter( *bitReader );
                    const auto footerOffset = bitReader->tell();

                    /* We only check for the stream size and CRC32 if we have read the whole stream including the header! */
                    if ( didReadHeader ) {
                        if ( streamBytesRead != footer.uncompressedSize ) {
                            std::stringstream message;
                            message << "Mismatching size (" << streamBytesRead << " <-> footer: "
                                    << footer.uncompressedSize << ") for gzip stream!";
                            throw std::runtime_error( std::move( message ).str() );
                        }
                    }

                    result.appendFooter( footerOffset, totalBytesRead, footer );
                    break;
                }
                }

                isAtStreamEnd = true;
                didReadHeader = false;
                streamBytesRead = 0;

                if ( bitReader->eof() ) {
                    nextBlockOffset = bitReader->tell();
                    break;
                }
            }
        }

        result.finalize( nextBlockOffset );
        return result;
    }

private:
    mutable Statistics m_statistics;

    std::atomic<bool> m_cancelThreads{ false };
    std::atomic<bool> m_crc32Enabled{ true };

    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const UniqueSharedFileReader m_sharedFileReader;
    std::shared_ptr<BlockFinder> const m_blockFinder;
    std::shared_ptr<BlockMap> const m_blockMap;
    std::shared_ptr<WindowMap> const m_windowMap;

    const bool m_isBgzfFile;
    std::atomic<size_t> m_maxDecompressedChunkSize{ std::numeric_limits<size_t>::max() };

    /* This is the highest found block inside BlockFinder we ever processed and put into the BlockMap.
     * After the BlockMap has been finalized, this isn't needed anymore. */
    size_t m_nextUnprocessedBlockIndex{ 0 };

    /* This is necessary when blocks have been split in order to find and reuse cached unsplit chunks. */
    std::unordered_map</* block offset */ size_t, /* block offset of unsplit "parent" chunk */ size_t> m_unsplitBlocks;

    PostProcessingFutures m_markersBeingReplaced;
    std::optional<CompressionType> m_windowCompressionType;
};
}  // namespace rapidgzip
