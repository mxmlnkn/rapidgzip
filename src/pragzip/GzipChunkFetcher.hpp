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


namespace pragzip
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
    using BitReader = pragzip::BitReader;
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
            out << "    Time spent in block finder          : " << m_blockFinderTime << " s\n";
            out << "    Time spent decoding                 : " << m_decodeTime << " s\n";
            out << "    Time spent allocating and copying   : " << m_appendTime << " s\n";
            out << "    Time spent applying the last window : " << m_applyWindowTime << " s\n";
            out << "    Replaced marker bytes               : " << formatBytes( m_markerCount ) << "\n";
            std::cerr << std::move( out ).str();
        }
    }

    /**
     * @param offset The current offset in the decoded data. (Does not have to be a block offset!)
     */
    [[nodiscard]] std::optional<std::pair<BlockMap::BlockInfo, std::shared_ptr<ChunkData> > >
    get( size_t offset )
    {
        /* In case we already have decoded the block once, we can simply query it from the block map and the fetcher. */
        auto blockInfo = m_blockMap->findDataOffset( offset );
        if ( blockInfo.contains( offset ) ) {
            return std::make_pair( blockInfo, getBlock( blockInfo.encodedOffsetInBits, blockInfo.blockIndex ) );
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

            const auto subblocks = chunkData->split( m_blockFinder->spacingInBits() / 8U );
            for ( const auto boundary : subblocks ) {
                m_blockMap->push( boundary.encodedOffset, boundary.encodedSize, boundary.decodedSize );
            }

            if constexpr ( ENABLE_STATISTICS || SHOW_PROFILE ) {
                std::scoped_lock lock( m_statisticsMutex );
                m_blockFinderTime += chunkData->blockFinderDuration;
                m_decodeTime += chunkData->decodeDuration;
                m_appendTime += chunkData->appendDuration;
            }

            /* This should also work for multi-stream gzip files because encodedSizeInBits is such that it
             * points across the gzip footer and next header to the next deflate block. */
            const auto blockOffsetAfterNext = chunkData->encodedOffsetInBits + chunkData->encodedSizeInBits;
            m_blockFinder->insert( blockOffsetAfterNext );
            if ( blockOffsetAfterNext >= m_bitReader.size() ) {
                m_blockMap->finalize();
                m_blockFinder->finalize();
            }

            ++m_nextUnprocessedBlockIndex;
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

        return std::make_pair( blockInfo, chunkData );
    }

    void
    setCRC32Enabled( bool enabled )
    {
        m_crc32Enabled = enabled;
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
            if ( !chunkData->containsMarkers() )  {
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

        /* If we got no block or one with the wrong data, then try again with the real offset, not the
         * speculatively prefetched one. */
        if ( !chunkData
             || ( !chunkData->matchesEncodedOffset( blockOffset )
                  && ( partitionOffset != blockOffset ) ) ) {
            if ( chunkData ) {
                std::cerr << "[Info] Detected a performance problem. Decoding might take longer than necessary. "
                          << "Please consider opening a performance bug report with "
                          << "a reproducing compressed file. Detailed information:\n"
                          << "[Info] Found mismatching block. Need offset " << formatBits( blockOffset )
                          << ". Look in partition offset: " << formatBits( partitionOffset )
                          << ". Found possible range: ["
                          << formatBits( chunkData->encodedOffsetInBits ) << ", "
                          << formatBits( chunkData->maxEncodedOffsetInBits ) << "]\n";
            }
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
                            m_isBgzfFile ? std::make_optional( WindowView{} ) : m_windowMap->get( blockOffset ),
                            blockInfo ? blockInfo->decodedSizeInBytes : std::optional<size_t>{},
                            m_cancelThreads, m_crc32Enabled );
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
                 bool                      const crc32Enabled = false )
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
            return decodeBlockWithPragzip( &bitReader, untilOffset, initialWindow, crc32Enabled );
        }

        const auto tryToDecode =
            [&] ( const std::pair<size_t, size_t>& offset ) -> std::optional<ChunkData>
            {
                try {
                    /* For decoding, it does not matter whether we seek to offset.first or offset.second but it DOES
                     * matter a lot for interpreting and correcting the encodedSizeInBits in GzipBlockFetcer::get! */
                    bitReader.seek( offset.second );
                    auto result = decodeBlockWithPragzip( &bitReader, untilOffset, initialWindow, crc32Enabled );
                    result.encodedOffsetInBits = offset.first;
                    result.maxEncodedOffsetInBits = offset.second;
                    /** @todo Avoid out of memory issues for very large compression ratios by using a simple runtime
                     *        length encoding or by only undoing the Huffman coding in parallel and the LZ77 serially,
                     *        or by stopping decoding at a threshold and fall back to serial decoding in that case? */
                    return result;
                } catch ( const std::exception& exception ) {
                    /* Ignore errors and try next block candidate. This is very likely to happen if @ref blockOffset
                     * is only an estimated offset! If it happens because decodeBlockWithPragzip has a bug, then it
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
         * m pragzip && time src/tools/pragzip -P 0 -d -c 4GiB-base64.gz | wc -c
         *    read in total 5.25998e+09 B out of 3263906195 B, i.e., read the file 1.61156 times
         *    Decompressed in total 4294967296 B in 4.35718 s -> 985.722 MB/s
         *
         * m pragzip && time src/tools/pragzip -P 0 -d -c random.bin.gz | wc -c
         *    read in total 2.41669e+09 B out of 2148139037 B, i.e., read the file 1.12502 times
         *    Decompressed in total 2147483648 B in 1.40283 s -> 1530.82 MB/s
         * @endverbatim
         *
         * Performance when looking only for dynamic blocks:
         * @verbatim
         * m pragzip && time src/tools/pragzip -P 0 -d -c 4GiB-base64.gz | wc -c
         *    read in total 3.67191e+09 B out of 3263906195 B, i.e., read the file 1.12501 times
         *    Decompressed in total 4294967296 B in 3.0489 s -> 1408.69 MB/s
         *  -> Almost 50% faster! And only 12% file read overhead instead of 61%!
         *
         * m pragzip && time src/tools/pragzip -P 0 -d -c random.bin.gz | wc -c
         *   -> LONGER THAN 3 MIN!
         * @endverbatim
         *
         * Performance after implementing the chunked alternating search:
         * @verbatim
         * m pragzip && time src/tools/pragzip -P 0 -d -c 4GiB-base64.gz | wc -c
         *    read in total 3.68686e+09 B out of 3263906195 B, i.e., read the file 1.12958 times
         *    Decompressed in total 4294967296 B in 3.06287 s -> 1402.27 MB/s
         *
         * m pragzip && time src/tools/pragzip -P 0 -d -c random.bin.gz | wc -c
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
    /**
     * This is a small wrapper around zlib. It is able to:
     *  - work on BitReader as input
     *  - start at deflate block offset as opposed to gzip start
     */
    class ZlibDeflateWrapper
    {
    public:
        explicit
        ZlibDeflateWrapper( BitReader bitReader ) :
            m_bitReader( std::move( bitReader ) )
        {
            initStream();
            /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode. */
            m_windowFlags = -15;
            if ( inflateInit2( &m_stream, m_windowFlags ) != Z_OK ) {
                throw std::runtime_error( "Probably encountered invalid deflate data!" );
            }
        }

        ~ZlibDeflateWrapper()
        {
            inflateEnd( &m_stream );
        }

        void
        initStream()
        {
            m_stream = {};

            m_stream.zalloc = Z_NULL;     /* used to allocate the internal state */
            m_stream.zfree = Z_NULL;      /* used to free the internal state */
            m_stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

            m_stream.avail_in = 0;        /* number of bytes available at next_in */
            m_stream.next_in = Z_NULL;    /* next input byte */

            m_stream.avail_out = 0;       /* remaining free space at next_out */
            m_stream.next_out = Z_NULL;   /* next output byte will go here */
            m_stream.total_out = 0;       /* total amount of bytes read */

            m_stream.msg = nullptr;
        }

        void
        refillBuffer()
        {
            if ( m_stream.avail_in > 0 ) {
                return;
            }

            if ( m_bitReader.tell() % BYTE_SIZE != 0 ) {
                const auto nBitsToPrime = BYTE_SIZE - ( m_bitReader.tell() % BYTE_SIZE );
                if ( inflatePrime( &m_stream, nBitsToPrime, m_bitReader.read( nBitsToPrime ) ) != Z_OK ) {
                    throw std::runtime_error( "InflatePrime failed!" );
                }
                assert( m_bitReader.tell() % BYTE_SIZE == 0 );
            }

            m_stream.avail_in = m_bitReader.read(
                m_buffer.data(), std::min( ( m_bitReader.size() - m_bitReader.tell() ) / BYTE_SIZE, m_buffer.size() ) );
            m_stream.next_in = reinterpret_cast<unsigned char*>( m_buffer.data() );
        }

        void
        setWindow( WindowView const& window )
        {
            if ( inflateSetDictionary( &m_stream, window.data(), window.size() ) != Z_OK ) {
                throw std::runtime_error( "Failed to set back-reference window in zlib!" );
            }
        }

        [[nodiscard]] size_t
        read( uint8_t* const output,
              size_t   const outputSize )
        {
            m_stream.next_out = output;
            m_stream.avail_out = outputSize;
            m_stream.total_out = 0;

            size_t decodedSize{ 0 };
            while ( decodedSize + m_stream.total_out < outputSize ) {
                refillBuffer();
                if ( m_stream.avail_in == 0 ) {
                    throw std::runtime_error( "Not enough input for requested output!" );
                }

                const auto errorCode = inflate( &m_stream, Z_BLOCK );
                if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
                    std::stringstream message;
                    message << "[" << std::this_thread::get_id() << "] "
                            << "Decoding failed with error code " << errorCode << " "
                            << ( m_stream.msg == nullptr ? "" : m_stream.msg ) << "! "
                            << "Already decoded " << m_stream.total_out << " B.";
                    throw std::runtime_error( std::move( message ).str() );
                }

                if ( decodedSize + m_stream.total_out > outputSize ) {
                    throw std::logic_error( "Decoded more than fits into output buffer!" );
                }
                if ( decodedSize + m_stream.total_out == outputSize ) {
                    return outputSize;
                }

                if ( errorCode == Z_STREAM_END ) {
                    decodedSize += m_stream.total_out;

                    const auto oldStream = m_stream;
                    inflateEnd( &m_stream );  // All dynamically allocated data structures for this stream are freed.
                    initStream();
                    m_stream.avail_in = oldStream.avail_in;
                    m_stream.next_in = oldStream.next_in;
                    m_stream.total_out = oldStream.total_out;

                    /* If we started with raw deflate, then we also have to skip other the gzip footer.
                     * Assuming we are decoding gzip and not zlib or multiple raw deflate streams. */
                    if ( m_windowFlags < 0 ) {
                        for ( auto stillToRemove = 8U; stillToRemove > 0; ) {
                            if ( m_stream.avail_in >= stillToRemove ) {
                                m_stream.avail_in -= stillToRemove;
                                m_stream.next_in += stillToRemove;
                                stillToRemove = 0;
                            } else {
                                stillToRemove -= m_stream.avail_in;
                                m_stream.avail_in = 0;
                                refillBuffer();
                            }
                        }
                    }

                    /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode.
                     * > The current implementation of inflateInit2() does not process any header information --
                     * > that is deferred until inflate() is called.
                     * Because of this, we don't have to ensure that enough data is available and/or calling it a
                     * second time to read the rest of the header. */
                    m_windowFlags = /* decode gzip */ 16 + /* 2^15 buffer */ 15;
                    if ( inflateInit2( &m_stream, m_windowFlags ) != Z_OK ) {
                        throw std::runtime_error( "Probably encountered invalid gzip header!" );
                    }

                    m_stream.next_out = output + decodedSize;
                    m_stream.avail_out = outputSize - decodedSize;
                }

                if ( m_stream.avail_out == 0 ) {
                    return outputSize;
                }
            }

            return decodedSize;
        }

    private:
        BitReader m_bitReader;
        int m_windowFlags{ 0 };
        z_stream m_stream{};
        /* Loading the whole encoded data (multiple MiB) into memory first and then
         * decoding it in one go is 4x slower than processing it in chunks of 128 KiB! */
        std::array<char, 128_Ki> m_buffer;
    };

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

        FasterVector<uint8_t> decoded( decodedSize );
        if ( deflateWrapper.read( decoded.data(), decoded.size() ) != decoded.size() ) {
            throw std::runtime_error( "Could not decode as much as requested!" );
        }
        result.append( std::move( decoded ) );

        /* We cannot arbitarily use bitReader.tell here, because the zlib wrapper buffers input read from BitReader.
         * If untilOffset is nullopt, then we are to read to the end of the file. */
        result.finalize( untilOffset );
        return result;
    }

    [[nodiscard]] static ChunkData
    decodeBlockWithPragzip( BitReader*                      bitReader,
                            size_t                          untilOffset,
                            std::optional<WindowView> const initialWindow,
                            bool                            crc32Enabled )
    {
        if ( bitReader == nullptr ) {
            throw std::invalid_argument( "BitReader must be non-null!" );
        }

        const auto blockOffset = bitReader->tell();

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

            if ( auto error = block->readHeader( *bitReader ); error != Error::NONE ) {
                std::stringstream message;
                message << "Failed to read deflate block header at offset " << formatBits( blockOffset )
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
            while ( !block->eob() )
            {
                const auto [bufferViews, error] = block->read( *bitReader, std::numeric_limits<size_t>::max() );
                if ( error != Error::NONE ) {
                    std::stringstream message;
                    message << "Failed to decode deflate block at " << formatBits( blockOffset )
                            << " because of: " << toString( error );
                    throw std::domain_error( std::move( message ).str() );
                }

                const auto tAppendStart = now();
                result.append( bufferViews );
                result.appendDuration += duration( tAppendStart );
                streamBytesRead += bufferViews.size();
                totalBytesRead += bufferViews.size();
            }

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
    mutable std::mutex m_statisticsMutex;

    std::atomic<bool> m_cancelThreads{ false };
    std::atomic<bool> m_crc32Enabled{ true };

    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    std::shared_ptr<BlockFinder> const m_blockFinder;
    std::shared_ptr<BlockMap> const m_blockMap;
    std::shared_ptr<WindowMap> const m_windowMap;

    const bool m_isBgzfFile;

    /* This is the highest found block inside BlockFinder we ever processed and put into the BlockMap.
     * After the BlockMap has been finalized, this isn't needed anymore. */
    size_t m_nextUnprocessedBlockIndex{ 0 };

    std::map</* block offset */ size_t, std::future<void> > m_markersBeingReplaced;
};
}  // namespace pragzip
