#pragma once

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
#include "common.hpp"
#include "DecodedData.hpp"
#include "deflate.hpp"
#include "gzip.hpp"
#include "GzipBlockFinder.hpp"
#include "WindowMap.hpp"


namespace pragzip
{
struct BlockData :
    public deflate::DecodedData
{
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };
};


template<typename FetchingStrategy>
class GzipBlockFetcher :
    public BlockFetcher<GzipBlockFinder, BlockData, FetchingStrategy>
{
public:
    using BaseType = BlockFetcher<GzipBlockFinder, BlockData, FetchingStrategy>;
    using BitReader = pragzip::BitReader;
    using WindowView = VectorView<uint8_t>;
    using BlockFinder = typename BaseType::BlockFinder;

public:
    class DecompressionError :
        public std::exception
    {};

    class NoBlockInRange :
        public DecompressionError
    {};

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
        this->stopThreadPool();
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
            return std::make_pair( blockInfo, BaseType::get( blockInfo.encodedOffsetInBits ) );
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

            const auto partitionOffset = m_blockFinder->partitionOffsetContaining( *nextBlockOffset );
            try {
                blockData = BaseType::get( partitionOffset, m_nextUnprocessedBlockIndex, /* only check caches */ true );
            } catch ( const NoBlockInRange& ) {
                /* Trying to get the next block based on the partition offset is only a performance optimization.
                 * It should succeed most of the time for good performance but is not required to and also might
                 * sometimes not, e.g., when the deflate block finder failed to find any valid block inside the
                 * partition, e.g., because it only contains fixed Huffman blocks. */
            }
            if ( !blockData ) {
                blockData = BaseType::get( *nextBlockOffset, m_nextUnprocessedBlockIndex );
            } else if ( blockData->encodedOffsetInBits != *nextBlockOffset ) {
                std::stringstream message;
                message << "Got wrong block to searched offset! Looked for " << std::to_string( *nextBlockOffset )
                        << " and looked up cache successively for estimated offset "
                        << std::to_string( partitionOffset ) << " but got block with actual offset "
                        << std::to_string( blockData->encodedOffsetInBits );
                throw std::logic_error( std::move( message ).str() );
            }
            if ( blockData->encodedOffsetInBits == std::numeric_limits<size_t>::max() ) {
                std::stringstream message;
                message << "Decoding failed at block offset " << formatBits( *nextBlockOffset ) << "!";
                throw std::domain_error( std::move( message ).str() );
            }

            m_blockMap->push( blockData->encodedOffsetInBits, blockData->encodedSizeInBits, blockData->size() );
            m_blockFinder->insert( blockData->encodedOffsetInBits + blockData->encodedSizeInBits );
            if ( blockData->encodedOffsetInBits + blockData->encodedSizeInBits >= m_bitReader.size() ) {
                m_blockMap->finalize();
                m_blockFinder->finalize();
            }
            ++m_nextUnprocessedBlockIndex;

            /* Because this is a new block, it might contain markers that we have to replace with the window
             * of the last block. The very first block should not contain any markers, ensuring that we
             * can successively propagate the window through all blocks. */
            auto lastWindow = m_windowMap->get( blockData->encodedOffsetInBits );
            if ( !lastWindow ) {
                std::stringstream message;
                message << "The window of the last block at " << formatBits( blockData->encodedOffsetInBits )
                        << " should exist at this point!";
                throw std::logic_error( std::move( message ).str() );
            }

            blockData->applyWindow( *lastWindow );
            const auto nextWindow = blockData->getLastWindow( *lastWindow );
            m_windowMap->emplace( blockData->encodedOffsetInBits + blockData->encodedSizeInBits,
                                  { nextWindow.begin(), nextWindow.end() } );
        }

        return std::make_pair( blockInfo, blockData );
    }

private:
    [[nodiscard]] BlockData
    decodeBlock( size_t blockOffset,
                 size_t nextBlockOffset ) const override
    {
        /* The decoded size of the block is only for optimization purposes. Therefore, we do not have to take care
         * about the correct ordering between BlockMap accesses and mofications (the BlockMap is still thread-safe). */
        const auto blockInfo = m_blockMap->getEncodedOffset( blockOffset );
        return decodeBlock( m_bitReader, blockOffset, nextBlockOffset,
                            m_isBgzfFile ? std::make_optional( WindowView{} ) : m_windowMap->get( blockOffset ),
                            blockInfo ? blockInfo->decodedSizeInBytes : std::optional<size_t>{} );
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
                 std::optional<size_t>     const decodedSize )
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

        /*
         * @todo Use faster pigz block finder to possibly speed up block finding. But, if I then had to recheck
         *       in all offsets before that for a gzip block for correct boundary conditions, it wouldn't mean
         *       anything. Therefore, I would need a complete special case for pigz files similarly to bgzf files.
         *       But, it might not even amount to much because on average it should only seek block size / 2
         *       which can be 8-32 KiB from what I found, except if there are many uncompressed blocks!
         *       Large uncompressed chunks will not profit from parallel gzip decoding but might not be necessary
         *       in the first place because it only seeks anyway but they would be nice to create index points!
         *       When applying multiple block finders (pigz, uncompressed, dynamic) it would be best to avoid
         *       BitReader buffer refills. Maybe only test up to BitReader::IOBUF_SIZE bytes?
         */
        for ( size_t offset = blockOffset; offset < untilOffset; ) {
            try {
                bitReader.seek( offset );
                auto result = decodeBlockWithPragzip( &bitReader, untilOffset, initialWindow );
                /** @todo Avoid out of memory issues for very large compression ratios by using a simple runtime
                 *        length encoding or by only undoing the Huffman coding in parallel and the LZ77 serially,
                 *        or by stopping decoding at a threshold and fall back to serial decoding in that case? */
                return result;
            } catch ( const std::exception& exception ) {
                /* Ignore errors and try next block candidate. This is very likely to happen if @ref blockOffset
                 * is only an estimated offset! If it happens because decodeBlockWithPragzip has a bug, then it
                 * might indirectly trigger an exception when the next required block offset cannot be found. */
                bitReader.seek( offset + 1 );
                offset = blockfinder::seekToNonFinalDynamicDeflateBlock<14>( bitReader );
            }
        }

        throw NoBlockInRange();
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
            if ( inflateInit2( &m_stream, -15 ) != Z_OK ) {
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
            if ( m_bitReader.tell() % BYTE_SIZE != 0 ) {
                const auto nBitsToPrime = BYTE_SIZE - ( m_bitReader.tell() % BYTE_SIZE );
                if ( inflatePrime( &m_stream, nBitsToPrime, m_bitReader.read( nBitsToPrime ) ) != Z_OK ) {
                    throw std::runtime_error( "InflatePrime failed!" );
                }
                assert( m_bitReader.tell() % BYTE_SIZE == 0 );
            }

            if ( m_stream.avail_in > 0 ) {
                return;
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

                    inflateEnd( &m_stream );
                    initStream();
                    /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode. */
                    if ( inflateInit2( &m_stream, /* decode gzip */ 16 + /* 2^15 buffer */ 15 ) != Z_OK ) {
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
        z_stream m_stream{};
        /* Loading the whole encoded data (multiple MiB) into memory first and then
         * decoding it in one go is 4x slower than processing it in chunks of 128 KiB! */
        std::array<char, 128 * 1024> m_buffer;
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

            if ( ( ( nextBlockOffset >= untilOffset )
                   && !block->isLastBlock()
                   && ( block->compressionType() == deflate::CompressionType::DYNAMIC_HUFFMAN ) )
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
