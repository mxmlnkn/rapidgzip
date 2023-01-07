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

#include "blockfinder/DynamicHuffman.hpp"
#include "blockfinder/Uncompressed.hpp"
#include "common.hpp"
#include "DecodedData.hpp"
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


struct BlockData :
    public deflate::DecodedData
{
public:
    [[nodiscard]] bool
    matchesEncodedOffset( size_t offset )
    {
        if ( maxEncodedOffsetInBits == std::numeric_limits<size_t>::max() ) {
            return offset == encodedOffsetInBits;
        }
        return ( encodedOffsetInBits <= offset ) && ( offset <= maxEncodedOffsetInBits );
    }

    void
    setEncodedOffset( size_t offset )
    {
        if ( !matchesEncodedOffset( offset ) ) {
            throw std::invalid_argument( "The real offset to correct to should lie inside the offset range!" );
        }

        if ( maxEncodedOffsetInBits == std::numeric_limits<size_t>::max() ) {
            maxEncodedOffsetInBits = encodedOffsetInBits;
        }

        /* Correct the encoded size "assuming" (it must be ensured!) that it was calculated from
         * maxEncodedOffsetInBits. */
        encodedSizeInBits += maxEncodedOffsetInBits - offset;

        encodedOffsetInBits = offset;
        maxEncodedOffsetInBits = offset;
    }

public:
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    /* This should only be evaluated when it is unequal std::numeric_limits<size_t>::max() and unequal
     * encodedOffsetInBits. Then, [encodedOffsetInBits, maxEncodedOffsetInBits] specifies a valid range for the
     * block offset. Such a range might happen for finding uncompressed deflate blocks because of the byte-padding. */
    size_t maxEncodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };

    /* Benchmark results */
    double blockFinderDuration{ 0 };
    double decodeDuration{ 0 };
};


/**
 * Tries to use writeAllSpliceUnsafe and, if successful, also extends lifetime by adding the block data
 * shared_ptr into a list.
 *
 * @note Limitations:
 *  - To avoid querying the pipe buffer size, it is only done once. This might introduce subtle errors when it is
 *    dynamically changed after this point.
 *  - The lifetime can only be extended on block granularity even though chunks would be more suited.
 *    This results in larger peak memory than strictly necessary.
 *  - In the worst case we would read only 1B out of each block, which would extend the lifetime
 *    of thousands of large blocks resulting in an out of memory issue.
 *    - This would only be triggerable by using the API. The current CLI and not even the Python
 *      interface would trigger this because either they don't splice to a pipe or only read
 *      sequentially.
 * @note It *does* account for pages to be spliced into yet another pipe buffer. This is exactly what the
 *       SPLICE_F_GIFT flag is for. Without that being set, pages will not be spliced but copied into further
 *       pipe buffers. So, without this flag, there is no danger of extending the lifetime of those pages
 *       arbitarily.
 */
[[nodiscard]] bool
writeAllSplice( const int                         outputFileDescriptor,
                const void* const                 dataToWrite,
                size_t const                      dataToWriteSize,
                const std::shared_ptr<BlockData>& blockData )
{
#if defined( HAVE_VMSPLICE )
    return SpliceVault::getInstance( outputFileDescriptor ).first->splice( dataToWrite, dataToWriteSize, blockData );
#else
    return false;
#endif
}


#if defined( HAVE_VMSPLICE )
[[nodiscard]] bool
writeAllSplice( [[maybe_unused]] const int                         outputFileDescriptor,
                [[maybe_unused]] const std::shared_ptr<BlockData>& blockData,
                [[maybe_unused]] const std::vector<::iovec>&       buffersToWrite )
{
    return SpliceVault::getInstance( outputFileDescriptor ).first->splice( buffersToWrite, blockData );
}
#endif  // HAVE_VMSPLICE


void
writeAll( const std::shared_ptr<BlockData>& blockData,
          const int                         outputFileDescriptor,
          const size_t                      offsetInBlock,
          const size_t                      dataToWriteSize )
{
    if ( ( outputFileDescriptor < 0 ) || ( dataToWriteSize == 0 ) ) {
        return;
    }

#ifdef HAVE_IOVEC
    const auto buffersToWrite = toIoVec( *blockData, offsetInBlock, dataToWriteSize );
    if ( !writeAllSplice( outputFileDescriptor, blockData, buffersToWrite ) ) {
        writeAllToFdVector( outputFileDescriptor, buffersToWrite );
    }
#else
    using pragzip::deflate::DecodedData;

    bool splicable = true;
    for ( auto it = DecodedData::Iterator( *blockData, offsetInBlock, dataToWriteSize );
          static_cast<bool>( it ); ++it )
    {
        const auto& [buffer, size] = *it;
        if ( splicable ) {
            splicable = writeAllSplice( outputFileDescriptor, buffer, size, blockData );
        }
        if ( !splicable ) {
            writeAllToFd( outputFileDescriptor, buffer, size);
        }
    }
#endif
}


template<typename FetchingStrategy,
         bool     ENABLE_STATISTICS = false,
         bool     SHOW_PROFILE = false>
class GzipBlockFetcher :
    public BlockFetcher<GzipBlockFinder, BlockData, FetchingStrategy, ENABLE_STATISTICS, SHOW_PROFILE>
{
public:
    using BaseType = BlockFetcher<GzipBlockFinder, BlockData, FetchingStrategy, ENABLE_STATISTICS, SHOW_PROFILE>;
    using BitReader = pragzip::BitReader;
    using WindowView = VectorView<uint8_t>;
    using BlockFinder = typename BaseType::BlockFinder;

public:
    GzipBlockFetcher( BitReader                    bitReader,
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
    ~GzipBlockFetcher()
    {
        m_cancelThreads = true;
        this->stopThreadPool();

        if constexpr ( SHOW_PROFILE ) {
            std::stringstream out;
            out << "[GzipBlockFetcher::GzipBlockFetcher] First block access statistics:\n";
            out << "    Time spent in block finder          : " << m_blockFinderTime << " s\n";
            out << "    Time spent decoding                 : " << m_decodeTime << " s\n";
            out << "    Time spent applying the last window : " << m_applyWindowTime << " s\n";
            out << "    Replaced marker bytes               : " << formatBytes( m_markerCount ) << "\n";
            std::cerr << std::move( out ).str();
        }
    }

    /**
     * @param offset The current offset in the decoded data. (Does not have to be a block offset!)
     */
    [[nodiscard]] std::optional<std::pair<BlockMap::BlockInfo, std::shared_ptr<BlockData> > >
    get( size_t offset )
    {
        /* In case we already have decoded the block once, we can simply query it from the block map and the fetcher. */
        auto blockInfo = m_blockMap->findDataOffset( offset );
        if ( blockInfo.contains( offset ) ) {
            return std::make_pair( blockInfo, getBlock( blockInfo.encodedOffsetInBits, blockInfo.blockIndex ) );
        }

        if ( m_blockMap->finalized() ) {
            return std::nullopt;
        }

        /* If the requested offset lies outside the last known block, then we need to keep fetching the next blocks
         * and filling the block- and window map until the end of the file is reached or we found the correct block. */
        std::shared_ptr<BlockData> blockData;
        for ( ; !blockInfo.contains( offset ); blockInfo = m_blockMap->findDataOffset( offset ) ) {
            const auto nextBlockOffset = m_blockFinder->get( m_nextUnprocessedBlockIndex );
            if ( !nextBlockOffset ) {
                m_blockMap->finalize();
                m_blockFinder->finalize();
                return std::nullopt;
            }

            blockData = getBlock( *nextBlockOffset, m_nextUnprocessedBlockIndex );

            m_blockMap->push( *nextBlockOffset, blockData->encodedSizeInBits, blockData->size() );
            const auto blockOffsetAfterNext = *nextBlockOffset + blockData->encodedSizeInBits;
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
            auto lastWindow = m_windowMap->get( *nextBlockOffset );
            if ( !lastWindow ) {
                std::stringstream message;
                message << "The window of the last block at " << formatBits( *nextBlockOffset )
                        << " should exist at this point!";
                throw std::logic_error( std::move( message ).str() );
            }

            [[maybe_unused]] const auto markerCount = blockData->dataWithMarkersSize();
            [[maybe_unused]] const auto tApplyStart = now();
            blockData->applyWindow( *lastWindow );
            if constexpr ( ENABLE_STATISTICS || SHOW_PROFILE ) {
                if ( markerCount > 0 ) {
                    m_applyWindowTime += duration( tApplyStart );
                }
                m_markerCount += markerCount;
                m_blockFinderTime += blockData->blockFinderDuration;
                m_decodeTime += blockData->decodeDuration;
            }

            const auto nextWindow = blockData->getLastWindow( *lastWindow );
            m_windowMap->emplace( blockOffsetAfterNext, { nextWindow.begin(), nextWindow.end() } );
        }

        return std::make_pair( blockInfo, blockData );
    }

private:
    /**
     * First, tries to look up the given block offset by its partition offset and then by its real offset.
     *
     * @param blockOffset The real block offset, not a guessed one, i.e., also no partition offset!
     *        This is important because this offset is stored in the returned BlockData as the real one.
     */
    [[nodiscard]] std::shared_ptr<BlockData>
    getBlock( const size_t blockOffset,
              const size_t blockIndex )
    {
        const auto getPartitionOffsetFromOffset =
            [this] ( auto offset ) { return m_blockFinder->partitionOffsetContainingOffset( offset ); };
        const auto partitionOffset = getPartitionOffsetFromOffset( blockOffset );

        std::shared_ptr<BlockData> blockData;
        try {
            blockData = BaseType::get( partitionOffset, blockIndex, /* only check caches */ true,
                                       getPartitionOffsetFromOffset );
        } catch ( const NoBlockInRange& ) {
            /* Trying to get the next block based on the partition offset is only a performance optimization.
             * It should succeed most of the time for good performance but is not required to and also might
             * sometimes not, e.g., when the deflate block finder failed to find any valid block inside the
             * partition, e.g., because it only contains fixed Huffman blocks. */
        }

        /* If we got no block or one with the wrong data, then try again with the real offset, not the
         * speculatively prefetched one. */
        if ( !blockData
             || ( !blockData->matchesEncodedOffset( blockOffset )
                  && ( partitionOffset != blockOffset ) ) ) {
            if ( blockData ) {
                std::cerr << "[Info] Detected a performance problem. Decoding might take longer than necessary. "
                          << "Please consider opening a performance bug report with "
                          << "a reproducing compressed file. Detailed information:\n"
                          << "[Info] Found mismatching block. Need offset " << formatBits( blockOffset )
                          << ". Look in partition offset: " << formatBits( partitionOffset )
                          << ". Found possible range: ["
                          << formatBits( blockData->encodedOffsetInBits ) << ", "
                          << formatBits( blockData->maxEncodedOffsetInBits ) << "]\n";
            }
            /* This call given the exact block offset must always yield the correct data and should be equivalent
             * to directly call @ref decodeBlock with that offset. */
            blockData = BaseType::get( blockOffset, blockIndex, /* only check caches */ false,
                                       getPartitionOffsetFromOffset );
        }

        if ( !blockData || ( blockData->encodedOffsetInBits == std::numeric_limits<size_t>::max() ) ) {
            std::stringstream message;
            message << "Decoding failed at block offset " << formatBits( blockOffset ) << "!";
            throw std::domain_error( std::move( message ).str() );
        }

        if ( !blockData->matchesEncodedOffset( blockOffset ) ) {
            std::stringstream message;
            message << "Got wrong block to searched offset! Looked for " << std::to_string( blockOffset )
                    << " and looked up cache successively for estimated offset "
                    << std::to_string( partitionOffset ) << " but got block with actual offset "
                    << std::to_string( blockOffset );
            throw std::logic_error( std::move( message ).str() );
        }

        /* Care has to be taken that we store the correct block offset not the speculative possible range! */
        blockData->setEncodedOffset( blockOffset );
        return blockData;
    }

    [[nodiscard]] BlockData
    decodeBlock( size_t blockOffset,
                 size_t nextBlockOffset ) const override
    {
        /* The decoded size of the block is only for optimization purposes. Therefore, we do not have to take care
         * about the correct ordering between BlockMap accesses and mofications (the BlockMap is still thread-safe). */
        const auto blockInfo = m_blockMap->getEncodedOffset( blockOffset );
        return decodeBlock( m_bitReader, blockOffset, nextBlockOffset,
                            m_isBgzfFile ? std::make_optional( WindowView{} ) : m_windowMap->get( blockOffset ),
                            blockInfo ? blockInfo->decodedSizeInBytes : std::optional<size_t>{},
                            m_cancelThreads );
    }

public:
    /**
     * @param untilOffset Decode to excluding at least this compressed offset. It can be the offset of the next
     *                    deflate block or next gzip stream but it can also be the starting guess for the block finder
     *                    to find the next deflate block or gzip stream.
     * @param initialWindow Required to resume decoding. Can be empty if, e.g., the blockOffset is at the gzip stream
     *                      start.
     */
    [[nodiscard]] static BlockData
    decodeBlock( BitReader                const& originalBitReader,
                 size_t                    const blockOffset,
                 size_t                    const untilOffset,
                 std::optional<WindowView> const initialWindow,
                 std::optional<size_t>     const decodedSize,
                 std::atomic<bool>        const& cancelThreads )
    {
        if ( initialWindow && decodedSize && ( *decodedSize > 0 ) ) {
            return decodeBlockWithZlib( originalBitReader,
                                        blockOffset,
                                        std::min( untilOffset, originalBitReader.size() ),
                                        *initialWindow,
                                        *decodedSize );
        }

        BitReader bitReader( originalBitReader );
        if ( initialWindow ) {
            bitReader.seek( blockOffset );
            return decodeBlockWithPragzip( &bitReader, untilOffset, initialWindow );
        }

        const auto tryToDecode =
            [&] ( const std::pair<size_t, size_t>& offset ) -> std::optional<BlockData>
            {
                try {
                    /* For decoding, it does not matter whether we seek to offset.first or offset.second but it DOES
                     * matter a lot for interpreting and correcting the encodedSizeInBits in GzipBlockFetcer::get! */
                    bitReader.seek( offset.second );
                    auto result = decodeBlockWithPragzip( &bitReader, untilOffset, initialWindow );
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
            if ( cancelThreads ) {
                break;
            }

            const auto chunkEnd = std::min( static_cast<size_t>( chunkBegin + CHUNK_SIZE ), untilOffset );

            auto uncompressedOffsetRange = findNextUncompressed( chunkBegin, chunkEnd );
            auto dynamicHuffmanOffset = findNextDynamic( chunkBegin, chunkEnd );

            while ( ( uncompressedOffsetRange.first < chunkEnd ) || ( dynamicHuffmanOffset < chunkEnd ) ) {
                if ( cancelThreads ) {
                    break;
                }

                std::pair<size_t, size_t> offsetToTest;
                if ( dynamicHuffmanOffset < uncompressedOffsetRange.first ) {
                    offsetToTest = { dynamicHuffmanOffset, dynamicHuffmanOffset };
                    dynamicHuffmanOffset = findNextDynamic( dynamicHuffmanOffset + 1, chunkEnd );
                } else {
                    offsetToTest = uncompressedOffsetRange;
                    uncompressedOffsetRange = findNextUncompressed( uncompressedOffsetRange.second + 1, chunkEnd );
                }

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
    [[nodiscard]] static BlockData
    decodeBlockWithZlib( const BitReader& originalBitReader,
                         size_t           blockOffset,
                         size_t           untilOffset,
                         WindowView       initialWindow,
                         size_t           decodedSize )
    {
        BitReader bitReader( originalBitReader );
        bitReader.seek( blockOffset );
        ZlibDeflateWrapper deflateWrapper( std::move( bitReader ) );
        deflateWrapper.setWindow( initialWindow );

        BlockData result;
        result.encodedOffsetInBits = blockOffset;

        std::vector<uint8_t> decoded( decodedSize );
        if ( deflateWrapper.read( decoded.data(), decoded.size() ) != decoded.size() ) {
            throw std::runtime_error( "Could not decode as much as requested!" );
        }
        result.append( std::move( decoded ) );

        /* We cannot arbitarily use bitReader.tell here, because the zlib wrapper buffers input read from BitReader.
         * If untilOffset is nullopt, then we are to read to the end of the file. */
        result.encodedSizeInBits = untilOffset - blockOffset;
        return result;
    }

    [[nodiscard]] static BlockData
    decodeBlockWithPragzip( BitReader*                      bitReader,
                            size_t                          untilOffset,
                            std::optional<WindowView> const initialWindow )
    {
        if ( bitReader == nullptr ) {
            throw std::invalid_argument( "BitReader must be non-null!" );
        }

        const auto blockOffset = bitReader->tell();

        /* If true, then read the gzip header. We cannot simply check the gzipHeader optional because we might
         * start reading in the middle of a gzip stream and will not meet the gzip header for a while or never. */
        bool isAtStreamEnd = false;
        size_t streamBytesRead = 0;
        std::optional<gzip::Header> gzipHeader;

        std::optional<deflate::Block</* CRC32 */ false> > block;
        block.emplace();
        if ( initialWindow ) {
            block->setInitialWindow( *initialWindow );
        }

        BlockData result;
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

            /* It is only important for performance that the deflate blocks we are matching here are the same
             * the block finder is will find. Note that we do not have to check for a uncompressed block padding
             * of zero because the deflate decoder counts that as an error anyway! */
            if ( ( ( nextBlockOffset >= untilOffset )
                   && !block->isLastBlock()
                   && ( block->compressionType() != deflate::CompressionType::FIXED_HUFFMAN ) )
                 || ( nextBlockOffset == untilOffset ) ) {
                break;
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

                result.append( bufferViews );
                streamBytesRead += bufferViews.size();
            }

            if ( block->isLastBlock() ) {
                const auto footer = gzip::readFooter( *bitReader );

                /* We only check for the stream size and CRC32 if we have read the whole stream including the header! */
                if ( gzipHeader ) {
                    if ( streamBytesRead != footer.uncompressedSize ) {
                        std::stringstream message;
                        message << "Mismatching size (" << streamBytesRead << " <-> footer: "
                                << footer.uncompressedSize << ") for gzip stream!";
                        throw std::runtime_error( std::move( message ).str() );
                    }

                    if ( ( block->crc32() != 0 ) && ( block->crc32() != footer.crc32 ) ) {
                        std::stringstream message;
                        message << "Mismatching CRC32 (0x" << std::hex << block->crc32() << " <-> stored: 0x"
                                << footer.crc32 << ") for gzip stream!";
                        throw std::runtime_error( std::move( message ).str() );
                    }
                }

                isAtStreamEnd = true;
                gzipHeader = {};
                streamBytesRead = 0;

                if ( bitReader->eof() ) {
                    nextBlockOffset = bitReader->tell();
                    break;
                }
            }
        }

        result.cleanUnmarkedData();
        result.encodedSizeInBits = nextBlockOffset - result.encodedOffsetInBits;
        return result;
    }

private:
    /* Members for benchmark statistics */
    double m_applyWindowTime{ 0 };
    double m_blockFinderTime{ 0 };
    double m_decodeTime{ 0 };
    uint64_t m_markerCount{ 0 };

    std::atomic<bool> m_cancelThreads{ false };

    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    std::shared_ptr<BlockFinder> const m_blockFinder;
    std::shared_ptr<BlockMap> const m_blockMap;
    std::shared_ptr<WindowMap> const m_windowMap;

    const bool m_isBgzfFile;

    /* This is the highest found block inside BlockFinder we ever processed and put into the BlockMap.
     * After the BlockMap has been finalized, this isn't needed anymore. */
    size_t m_nextUnprocessedBlockIndex{ 0 };
};
}  // namespace pragzip
