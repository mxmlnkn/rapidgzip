#pragma once

#include <array>
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
         typename T_ChunkData = ChunkData,
         bool     ENABLE_STATISTICS = false,
         bool     SHOW_PROFILE = false>
class GzipChunkFetcher :
    public BlockFetcher<GzipBlockFinder, T_ChunkData, T_FetchingStrategy, ENABLE_STATISTICS, SHOW_PROFILE>
{
public:
    using FetchingStrategy = T_FetchingStrategy;
    using ChunkData = T_ChunkData;
    using BaseType = BlockFetcher<GzipBlockFinder, ChunkData, FetchingStrategy, ENABLE_STATISTICS, SHOW_PROFILE>;
    using BitReader = rapidgzip::BitReader;
    using WindowView = VectorView<uint8_t>;
    using BlockFinder = typename BaseType::BlockFinder;

    static constexpr bool REPLACE_MARKERS_IN_PARALLEL = true;

public:
    GzipChunkFetcher( BitReader                    bitReader,
                      std::shared_ptr<BlockFinder> blockFinder,
                      std::shared_ptr<BlockMap>    blockMap,
                      std::shared_ptr<WindowMap>   windowMap,
                      size_t                       parallelization ) :
        BaseType( blockFinder, parallelization ),
        m_bitReader( bitReader ),
        m_blockFinder( std::move( blockFinder ) ),
        m_blockMap( std::move( blockMap ) ),
        m_windowMap( std::move( windowMap ) ),
        m_isBgzfFile( m_blockFinder->isBgzfFile() )
    {
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
            m_windowMap->emplace( *firstBlockInStream, {} );
        }
    }

    virtual
    ~GzipChunkFetcher()
    {
        m_cancelThreads = true;
        this->stopThreadPool();

        if constexpr ( SHOW_PROFILE ) {
            std::stringstream out;
            out << "[GzipChunkFetcher::GzipChunkFetcher] First block access statistics:\n";
            out << "    Time spent in block finder              : " << m_blockFinderTime << " s\n";
            out << "    Time spent decoding                     : " << m_decodeTime << " s\n";
            out << "    Time spent allocating and copying       : " << m_appendTime << " s\n";
            out << "    Time spent applying the last window     : " << m_applyWindowTime << " s\n";
            out << "    Replaced marker bytes                   : " << formatBytes( m_markerCount ) << "\n";
            out << "    Chunks exceeding max. compression ratio : " << m_preemptiveStopCount << "\n";
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
            const auto blockOffset = blockInfo.encodedOffsetInBits;
            /* Try to look up the offset based on an offset of for the unsplit block.
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
                         /* Test whether we got the unsplit block or the first split subblock from the cache. */
                         && ( blockOffset >= ( *chunkData )->encodedOffsetInBits )
                         && ( blockOffset < ( *chunkData )->encodedOffsetInBits + ( *chunkData )->encodedSizeInBits ) )
                    {
                        return std::make_pair( unsplitBlockInfo->decodedOffsetInBytes, *chunkData );
                    }
                }
            }

            /* Get block normally */
            return std::make_pair( blockInfo.decodedOffsetInBytes,
                                   getBlock( blockInfo.encodedOffsetInBits, blockInfo.blockIndex ) );
        }

        /* If the requested offset lies outside the last known block, then we need to keep fetching the next blocks
         * and filling the block- and window map until the end of the file is reached or we found the correct block. */
        std::shared_ptr<ChunkData> chunkData;
        for ( ; !blockInfo.contains( offset ); blockInfo = m_blockMap->findDataOffset( offset ) ) {
            if ( m_blockMap->finalized() ) {
                return std::nullopt;
            }

            const auto nextBlockOffset = m_blockFinder->get( m_nextUnprocessedBlockIndex );
            if ( !nextBlockOffset ) {
                m_blockMap->finalize();
                m_blockFinder->finalize();
                return std::nullopt;
            }

            chunkData = getBlock( *nextBlockOffset, m_nextUnprocessedBlockIndex );

            /* This should also work for multi-stream gzip files because encodedSizeInBits is such that it
             * points across the gzip footer and next header to the next deflate block. */
            const auto blockOffsetAfterNext = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;

            m_preemptiveStopCount += chunkData->stoppedPreemptively ? 1 : 0;

            const auto subblocks = chunkData->split( m_blockFinder->spacingInBits() / 8U );
            for ( const auto boundary : subblocks ) {
                m_blockMap->push( boundary.encodedOffset, boundary.encodedSize, boundary.decodedSize );
                m_blockFinder->insert( boundary.encodedOffset + boundary.encodedSize );
            }

            if ( subblocks.size() > 1 ) {
                BaseType::m_fetchingStrategy.splitIndex( m_nextUnprocessedBlockIndex, subblocks.size() );

                /* Get actual key in cache, which might be the partition offset! */
                const auto chunkOffset = chunkData->encodedOffsetInBits;
                const auto partitionOffset = m_blockFinder->partitionOffsetContainingOffset( chunkOffset );
                const auto lookupKey = !BaseType::test( chunkOffset ) && BaseType::test( partitionOffset )
                                       ? partitionOffset
                                       : chunkOffset;
                for ( const auto boundary : subblocks ) {
                    /* This condition could be removed but makes the map slightly smaller. */
                    if ( boundary.encodedOffset != chunkOffset ) {
                        m_unsplitBlocks.emplace( boundary.encodedOffset, lookupKey );
                    }
                }
            }
            m_nextUnprocessedBlockIndex += subblocks.size();

            if constexpr ( ENABLE_STATISTICS || SHOW_PROFILE ) {
                std::scoped_lock lock( m_statisticsMutex );
                m_blockFinderTime += chunkData->blockFinderDuration;
                m_decodeTime += chunkData->decodeDuration;
                m_appendTime += chunkData->appendDuration;
            }

            if ( blockOffsetAfterNext >= m_bitReader.size() ) {
                m_blockMap->finalize();
                m_blockFinder->finalize();
            }

            if ( const auto insertedNextBlockOffset = m_blockFinder->get( m_nextUnprocessedBlockIndex );
                 !m_blockFinder->finalized()
                 && ( !insertedNextBlockOffset.has_value() || ( *insertedNextBlockOffset != blockOffsetAfterNext ) ) )
            {
                /* We could also keep track of the next block offset instead of the block index but then we would
                 * have to do a bisection for each block to find the block index from the offset. */
                throw std::logic_error( "Next block offset index is out of sync!" );
            }

            /* Because this is a new block, it might contain markers that we have to replace with the window
             * of the last block. The very first block should not contain any markers, ensuring that we
             * can successively propagate the window through all blocks. */
            auto lastWindow = m_windowMap->get( chunkData->encodedOffsetInBits );
            if ( !lastWindow ) {
                std::stringstream message;
                message << "The window of the last block at " << formatBits( chunkData->encodedOffsetInBits )
                        << " should exist at this point!";
                throw std::logic_error( std::move( message ).str() );
            }

            if constexpr ( REPLACE_MARKERS_IN_PARALLEL ) {
                waitForReplacedMarkers( chunkData, *lastWindow );
            } else {
                replaceMarkers( chunkData, *lastWindow );
            }

            size_t decodedOffsetInBlock{ 0 };
            for ( const auto& subblock : subblocks ) {
                decodedOffsetInBlock += subblock.decodedSize;
                const auto windowOffset = subblock.encodedOffset + subblock.encodedSize;
                /* Avoid recalculating what we already emplaced in waitForReplacedMarkers when calling getLastWindow. */
                if ( !m_windowMap->get( windowOffset ) ) {
                    m_windowMap->emplace( windowOffset, chunkData->getWindowAt( *lastWindow, decodedOffsetInBlock ) );
                }
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
    maxDecompressedChunkSize( size_t maxDecompressedChunkSize ) const
    {
        return m_maxDecompressedChunkSize;
    }

private:
    void
    waitForReplacedMarkers( const std::shared_ptr<ChunkData>& chunkData,
                            const WindowView                  lastWindow )
    {
        using namespace std::chrono_literals;

        auto markerReplaceFuture = m_markersBeingReplaced.find( chunkData->encodedOffsetInBits );
        if ( ( markerReplaceFuture == m_markersBeingReplaced.end() ) && !chunkData->containsMarkers() ) {
            return;
        }

        /* Not ready or not yet queued, so queue it and use the wait time to queue more marker replacements. */
        std::optional<std::future<void> > queuedFuture;
        if ( markerReplaceFuture == m_markersBeingReplaced.end() ) {
            /* First, we need to emplace the last window or else we cannot queue further blocks. */
            const auto windowOffset = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;
            if ( !m_windowMap->get( windowOffset ) ) {
                m_windowMap->emplace( windowOffset, chunkData->getLastWindow( lastWindow ) );
            }

            markerReplaceFuture = m_markersBeingReplaced.emplace(
                chunkData->encodedOffsetInBits,
                this->submitTaskWithHighPriority(
                    [this, chunkData, lastWindow] () { replaceMarkers( chunkData, lastWindow ); }
                )
            ).first;
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

        replaceMarkersInPrefetched();

        markerReplaceFuture->second.get();
        m_markersBeingReplaced.erase( markerReplaceFuture );
    }

    void
    replaceMarkersInPrefetched()
    {
        /* Trigger jobs for ready block data to replace markers. */
        const auto& cacheElements = this->prefetchCache().contents();
        std::vector<size_t> sortedOffsets( cacheElements.size() );
        std::transform( cacheElements.begin(), cacheElements.end(), sortedOffsets.begin(),
                        [] ( const auto& keyValue ) { return keyValue.first; } );
        std::sort( sortedOffsets.begin(), sortedOffsets.end() );
        for ( const auto triedStartOffset : sortedOffsets ) {
            const auto chunkData = cacheElements.at( triedStartOffset );

            /* Ignore ready blocks. */
            if ( !chunkData->containsMarkers() ) {
                continue;
            }

            /* Ignore blocks already enqueued for marker replacement. */
            if ( m_markersBeingReplaced.find( chunkData->encodedOffsetInBits ) != m_markersBeingReplaced.end() ) {
                continue;
            }

            /* Check for previous window. */
            const auto previousWindow = m_windowMap->get( chunkData->encodedOffsetInBits );
            if ( !previousWindow ) {
                continue;
            }

            const auto windowOffset = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;
            if ( !m_windowMap->get( windowOffset ) ) {
                m_windowMap->emplace( windowOffset, chunkData->getLastWindow( *previousWindow ) );
            }

            m_markersBeingReplaced.emplace(
                chunkData->encodedOffsetInBits,
                this->submitTaskWithHighPriority(
                    [this, chunkData, previousWindow] () { replaceMarkers( chunkData, *previousWindow ); }
                )
            );
        }
    }

    /**
     * Must be thread-safe because it is submitted to the thread pool.
     */
    void
    replaceMarkers( const std::shared_ptr<ChunkData>& chunkData,
                    const WindowView                  previousWindow )
    {
        [[maybe_unused]] const auto markerCount = chunkData->dataWithMarkersSize();
        [[maybe_unused]] const auto tApplyStart = now();
        chunkData->applyWindow( previousWindow );
        if constexpr ( ENABLE_STATISTICS || SHOW_PROFILE ) {
            std::scoped_lock lock( m_statisticsMutex );
            if ( markerCount > 0 ) {
                m_applyWindowTime += duration( tApplyStart );
            }
            m_markerCount += markerCount;
        }
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
        if ( chunkData && !chunkData->matchesEncodedOffset( blockOffset ) && ( partitionOffset != blockOffset )
             && ( m_preemptiveStopCount == 0 ) ) {
            std::cerr << "[Info] Detected a performance problem. Decoding might take longer than necessary. "
                      << "Please consider opening a performance bug report with "
                      << "a reproducing compressed file. Detailed information:\n"
                      << "[Info] Found mismatching block. Need offset " << formatBits( blockOffset )
                      << ". Look in partition offset: " << formatBits( partitionOffset )
                      << ". Found possible range: ["
                      << formatBits( chunkData->encodedOffsetInBits ) << ", "
                      << formatBits( chunkData->maxEncodedOffsetInBits ) << "]\n";
        }

        /* If we got no block or one with the wrong data, then try again with the real offset, not the
         * speculatively prefetched one. */
        if ( !chunkData
             || ( !chunkData->matchesEncodedOffset( blockOffset )
                  && ( partitionOffset != blockOffset ) ) ) {
            /* This call given the exact block offset must always yield the correct data and should be equivalent
             * to directly call @ref decodeBlock with that offset. */
            chunkData = BaseType::get( blockOffset, blockIndex, getPartitionOffsetFromOffset );
        }

        if ( !chunkData || ( chunkData->encodedOffsetInBits == std::numeric_limits<size_t>::max() ) ) {
            std::stringstream message;
            message << "Decoding failed at block offset " << formatBits( blockOffset ) << "!";
            throw std::domain_error( std::move( message ).str() );
        }

        if ( !chunkData->matchesEncodedOffset( blockOffset ) ) {
            std::stringstream message;
            message << "Got wrong block to searched offset! Looked for " << std::to_string( blockOffset )
                    << " and looked up cache successively for estimated offset "
                    << std::to_string( partitionOffset ) << " but got block with actual offset "
                    << std::to_string( blockOffset );
            throw std::logic_error( std::move( message ).str() );
        }

        /* Care has to be taken that we store the correct block offset not the speculative possible range! */
        chunkData->setEncodedOffset( blockOffset );
        return chunkData;
    }

    [[nodiscard]] ChunkData
    decodeBlock( size_t blockOffset,
                 size_t nextBlockOffset ) const override
    {
        /* The decoded size of the block is only for optimization purposes. Therefore, we do not have to take care
         * of the correct ordering between BlockMap accesses and modifications (the BlockMap is still thread-safe). */
        const auto blockInfo = m_blockMap->getEncodedOffset( blockOffset );
        return decodeBlock( m_bitReader, blockOffset, nextBlockOffset,
                            /**
                             * If we are a BGZF file and we have not imported an index, then we can assume the
                             * window to be empty because we should only get offsets at gzip stream starts.
                             * @note This is brittle in case of supporting partial imports of indexes, which would
                             *       not finalize the block finder. Instead it might be better to create the index,
                             *       including windows and decompressed sizes, by the BGZF "block finder" itself.
                             *       It has all data necessary for it including the decompressed size in the gzip
                             *       stream footer!
                             * @note BGZF index offsets should only begin at gzip stream offsets because those
                             *       do not require any window! However, even if this is implemented we cannot
                             *       remove the check against imported indexes because we cannot be sure that the
                             *       indexes were created by us and follow that scheme.
                             */
                            m_isBgzfFile && !m_blockFinder->finalized()
                            ? std::make_optional( WindowView{} )
                            : m_windowMap->get( blockOffset ),
                            blockInfo ? blockInfo->decodedSizeInBytes : std::optional<size_t>{},
                            m_cancelThreads, m_crc32Enabled, m_maxDecompressedChunkSize );
    }

public:
    /**
     * @param untilOffset Decode to excluding at least this compressed offset. It can be the offset of the next
     *                    deflate block or next gzip stream but it can also be the starting guess for the block finder
     *                    to find the next deflate block or gzip stream.
     * @param initialWindow Required to resume decoding. Can be empty if, e.g., the blockOffset is at the gzip stream
     *                      start.
     */
    [[nodiscard]] static ChunkData
    decodeBlock( BitReader                const& originalBitReader,
                 size_t                    const blockOffset,
                 size_t                    const untilOffset,
                 std::optional<WindowView> const initialWindow,
                 std::optional<size_t>     const decodedSize,
                 std::atomic<bool>        const& cancelThreads,
                 bool                      const crc32Enabled = false,
                 size_t                    const maxDecompressedChunkSize = std::numeric_limits<size_t>::max() )
    {
        if ( initialWindow && decodedSize && ( *decodedSize > 0 ) ) {
            return decodeBlockWithZlib( originalBitReader,
                                        blockOffset,
                                        std::min( untilOffset, originalBitReader.size() ),
                                        *initialWindow,
                                        *decodedSize,
                                        crc32Enabled );
        }

        BitReader bitReader( originalBitReader );
        if ( initialWindow ) {
            bitReader.seek( blockOffset );
            return decodeBlockWithRapidgzip( &bitReader, untilOffset, initialWindow, crc32Enabled,
                                             maxDecompressedChunkSize );
        }

        const auto tryToDecode =
            [&] ( const std::pair<size_t, size_t>& offset ) -> std::optional<ChunkData>
            {
                try {
                    /* For decoding, it does not matter whether we seek to offset.first or offset.second but it DOES
                     * matter a lot for interpreting and correcting the encodedSizeInBits in GzipBlockFetcer::get! */
                    bitReader.seek( offset.second );
                    auto result = decodeBlockWithRapidgzip( &bitReader, untilOffset, initialWindow, crc32Enabled,
                                                            maxDecompressedChunkSize );
                    result.encodedOffsetInBits = offset.first;
                    result.maxEncodedOffsetInBits = offset.second;
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
                    result->blockFinderDuration = duration( tBlockFinderStart, tBlockFinderStop );
                    result->decodeDuration = duration( tBlockFinderStop );
                    return *std::move( result );
                }
            }
        }

        std::stringstream message;
        message << "Failed to find any valid deflate block in [" << formatBits( blockOffset )
                << ", " << formatBits( untilOffset ) << ")";
        throw NoBlockInRange( std::move( message ).str() );
    }

private:
    [[nodiscard]] static ChunkData
    decodeBlockWithZlib( const BitReader& originalBitReader,
                         size_t           blockOffset,
                         size_t           untilOffset,
                         WindowView       initialWindow,
                         size_t           decodedSize,
                         bool             crc32Enabled )
    {
        BitReader bitReader( originalBitReader );
        bitReader.seek( blockOffset );
        ZlibDeflateWrapper deflateWrapper( std::move( bitReader ) );
        deflateWrapper.setWindow( initialWindow );

        ChunkData result;
        result.setCRC32Enabled( crc32Enabled );
        result.encodedOffsetInBits = blockOffset;

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
        constexpr size_t ALLOCATION_CHUNK_SIZE = 128_Ki;
        for ( size_t alreadyDecoded = 0; alreadyDecoded < decodedSize; ) {
            deflate::DecodedVector subchunk( std::min( ALLOCATION_CHUNK_SIZE, decodedSize - alreadyDecoded ) );
            std::optional<gzip::Footer> footer;

            /* In order for CRC32 verification to work, we have append at most one gzip stream per subchunk
             * because the CRC32 calculator is swapped inside ChunkData::append. */
            size_t nBytesRead = 0;
            size_t nBytesReadPerCall{ 0 };
            for ( ; ( nBytesRead < subchunk.size() ) && !footer; nBytesRead += nBytesReadPerCall ) {
                std::tie( nBytesReadPerCall, footer ) = deflateWrapper.readStream( subchunk.data() + nBytesRead,
                                                                                   subchunk.size() - nBytesRead );
                if ( nBytesReadPerCall == 0 ) {
                    throw std::runtime_error( "Could not decode as much as requested!" );
                }
            }

            alreadyDecoded += nBytesRead;

            subchunk.resize( nBytesRead );
            subchunk.shrink_to_fit();
            result.append( std::move( subchunk ) );
            if ( footer ) {
                result.appendFooter( deflateWrapper.tellEncoded(), alreadyDecoded, *footer );
            }
        }

        uint8_t dummy{ 0 };
        const auto [nBytesReadPerCall, footer] = deflateWrapper.readStream( &dummy, 1 );
        if ( ( nBytesReadPerCall == 0 ) && footer ) {
            result.appendFooter( deflateWrapper.tellEncoded(), decodedSize, *footer );
        }

        /* We cannot arbitarily use bitReader.tell here, because the zlib wrapper buffers input read from BitReader.
         * If untilOffset is nullopt, then we are to read to the end of the file. */
        result.finalize( untilOffset );
        return result;
    }

    [[nodiscard]] static ChunkData
    decodeBlockWithRapidgzip( BitReader*                      bitReader,
                              size_t                          untilOffset,
                              std::optional<WindowView> const initialWindow,
                              bool                            crc32Enabled,
                              size_t                          maxDecompressedChunkSize )
    {
        if ( bitReader == nullptr ) {
            throw std::invalid_argument( "BitReader must be non-null!" );
        }

        const auto chunkOffset = bitReader->tell();

        /* If true, then read the gzip header. We cannot simply check the gzipHeader optional because we might
         * start reading in the middle of a gzip stream and will not meet the gzip header for a while or never. */
        bool isAtStreamEnd = false;
        size_t streamBytesRead = 0;
        size_t totalBytesRead = 0;
        std::optional<gzip::Header> gzipHeader;

        std::optional<deflate::Block<> > block;
        block.emplace();
        if ( initialWindow ) {
            block->setInitialWindow( *initialWindow );
        }

        ChunkData result;
        result.setCRC32Enabled( crc32Enabled );
        result.encodedOffsetInBits = bitReader->tell();

        /* Loop over possibly gzip streams and deflate blocks. We cannot use GzipReader even though it does
         * something very similar because GzipReader only works with fully decodable streams but we
         * might want to return buffer with placeholders in case we don't know the initial window, yet! */
        size_t nextBlockOffset{ 0 };
        while ( true )
        {
            if ( isAtStreamEnd ) {
                const auto headerOffset = bitReader->tell();
                const auto [header, error] = gzip::readHeader( *bitReader );
                if ( error != Error::NONE ) {
                    std::stringstream message;
                    message << "Failed to read gzip header at offset " << formatBits( headerOffset )
                            << " because of error: " << toString( error );
                    throw std::domain_error( std::move( message ).str() );
                }

                gzipHeader = std::move( header );
                block.emplace();
                block->setInitialWindow();

                nextBlockOffset = bitReader->tell();
                if ( nextBlockOffset >= untilOffset ) {
                    break;
                }

                isAtStreamEnd = false;
            }

            nextBlockOffset = bitReader->tell();

            if ( totalBytesRead >= maxDecompressedChunkSize ) {
                result.stoppedPreemptively = true;
                break;
            }

            if ( auto error = block->readHeader( *bitReader ); error != Error::NONE ) {
                std::stringstream message;
                message << "Failed to read deflate block header at offset " << formatBits( chunkOffset )
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
                    message << "Failed to decode deflate block at " << formatBits( chunkOffset )
                            << " because of: " << toString( error );
                    throw std::domain_error( std::move( message ).str() );
                }

                const auto tAppendStart = now();
                result.append( bufferViews );
                result.appendDuration += duration( tAppendStart );
                blockBytesRead += bufferViews.size();

                /* Non-compressed deflate blocks are limited to 64 KiB and the largest Dynamic Huffman Coding
                 * deflate blocks I have seen were 128 KiB in compressed size. With a maximum compression
                 * ratio of 1032, this would result in ~128 MiB. Fortunately, simple runs of zeros did compress
                 * to only 8 KiB blocks, i.e., ~8 MiB decompressed.
                 * However, igzip -0 can compress the whole file in a single deflate block.
                 * Decompressing such a file is not supported (yet). It would require some heavy
                 * refactoring of the ChunkData class to support resuming the decompression so that
                 * we can simply break and return here insteda of throwing an exception. This would basically
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
                const auto footerOffset = bitReader->tell();
                const auto footer = gzip::readFooter( *bitReader );

                /* We only check for the stream size and CRC32 if we have read the whole stream including the header! */
                if ( gzipHeader ) {
                    if ( streamBytesRead != footer.uncompressedSize ) {
                        std::stringstream message;
                        message << "Mismatching size (" << streamBytesRead << " <-> footer: "
                                << footer.uncompressedSize << ") for gzip stream!";
                        throw std::runtime_error( std::move( message ).str() );
                    }
                }

                result.appendFooter( footerOffset, totalBytesRead, footer );

                isAtStreamEnd = true;
                gzipHeader = {};
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
    /* Members for benchmark statistics */
    double m_applyWindowTime{ 0 };
    double m_blockFinderTime{ 0 };
    double m_decodeTime{ 0 };
    double m_appendTime{ 0 };
    uint64_t m_markerCount{ 0 };
    uint64_t m_preemptiveStopCount{ 0 };
    mutable std::mutex m_statisticsMutex;

    std::atomic<bool> m_cancelThreads{ false };
    std::atomic<bool> m_crc32Enabled{ true };

    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
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

    std::map</* block offset */ size_t, std::future<void> > m_markersBeingReplaced;
};
}  // namespace rapidgzip
