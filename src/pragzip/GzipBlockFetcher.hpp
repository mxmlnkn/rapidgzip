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

#include "BlockFinder.hpp"
#include "blockfinder/Interface.hpp"
#include "BlockMap.hpp"
#include "DecodedData.hpp"
#include "deflate.hpp"
#include "gzip.hpp"
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
    public BlockFetcher<BlockFinder<blockfinder::Interface>, BlockData, FetchingStrategy>
{
public:
    using BaseType = BlockFetcher<::BlockFinder<blockfinder::Interface>, BlockData, FetchingStrategy>;
    using BitReader = pragzip::BitReader;
    using WindowView = VectorView<uint8_t>;

public:
    GzipBlockFetcher( BitReader                                       bitReader,
                      std::shared_ptr<typename BaseType::BlockFinder> blockFinder,
                      std::shared_ptr<BlockMap>                       blockMap,
                      std::shared_ptr<WindowMap>                      windowMap,
                      bool                                            isBgzfFile,
                      size_t                                          parallelization ) :
        BaseType( std::move( blockFinder ), parallelization ),
        m_bitReader( bitReader ),
        m_isBgzfFile( isBgzfFile ),
        m_blockMap( std::move( blockMap ) ),
        m_windowMap( std::move( windowMap ) )
    {
        if ( !m_blockMap ) {
            throw std::invalid_argument( "Block map must be valid!" );
        }
        if ( !m_windowMap ) {
            throw std::invalid_argument( "Window map must be valid!" );
        }

        if ( m_windowMap->empty() ) {
            BitReader gzipBitReader{ m_bitReader };
            gzipBitReader.seek( 0 );
            const auto [header, error] = gzip::readHeader( gzipBitReader );
            if ( error != Error::NONE ) {
                throw std::invalid_argument( "Encountered error while reading gzip header: " + toString( error ) );
            }
            m_windowMap->emplace( gzipBitReader.tell(), {} );
        }
    }

    virtual
    ~GzipBlockFetcher()
    {
        this->stopThreadPool();
    }

    [[nodiscard]] virtual std::shared_ptr<BlockData>
    get( size_t                blockOffset,
         std::optional<size_t> dataBlockIndex = {} ) override
    {
        return BaseType::get( blockOffset, dataBlockIndex );
    }

private:
    [[nodiscard]] BlockData
    decodeBlock( size_t blockIndex,
                 size_t blockOffset ) const override
    {
        /* The decoded size of the block is only for optimization purposes. Therefore, we do not have to take care
         * about the correct ordering between BlockMap accesses and mofications (the BlockMap is still thread-safe). */
        const auto blockInfo = m_blockMap->getEncodedOffset( blockOffset );
        return decodeBlock( m_bitReader, blockOffset, this->m_blockFinder->get( blockIndex + 1 ),
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
    decodeBlock( const BitReader&          originalBitReader,
                 size_t                    blockOffset,
                 std::optional<size_t>     untilOffset,
                 std::optional<WindowView> initialWindow,
                 std::optional<size_t>     decodedSize )
    {
        if ( initialWindow && decodedSize && ( *decodedSize > 0 ) ) {
            return decodeBlockWithZlib( originalBitReader,
                                        blockOffset,
                                        untilOffset ? *untilOffset : originalBitReader.size(),
                                        *initialWindow,
                                        *decodedSize );
        }

        BitReader bitReader( originalBitReader );
        bitReader.seek( blockOffset );

        BlockData result;
        result.encodedOffsetInBits = blockOffset;

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

        /* Loop over possibly gzip streams and deflate blocks. We cannot use GzipReader even though it does
         * something very similar because GzipReader only works with fully decodable streams but we
         * might want to return buffer with placeholders in case we don't know the initial window, yet! */
        while ( true )
        {
            if ( untilOffset && ( bitReader.tell() >= untilOffset ) ) {
                break;
            }

            if ( isAtStreamEnd ) {
                const auto [header, error] = gzip::readHeader( bitReader );
                if ( error != Error::NONE ) {
                    std::cerr << "Encountered error while trying to read gzip header: " << toString( error ) << "\n";
                    break;
                }

                gzipHeader = std::move( header );
                block.emplace();
                block->setInitialWindow();

                if ( untilOffset && ( bitReader.tell() >= untilOffset ) ) {
                    break;
                }
            }

            if ( auto error = block->readHeader( bitReader ); error != Error::NONE ) {
                std::cerr << "Erroneous block header at offset " << blockOffset << " b (after read: "
                          << bitReader.tell() << " b): " << toString( error ) << "\n";
                return {};
            }

            /* Loop until we have read the full contents of the current deflate block-> */
            while ( !block->eob() )
            {
                const auto [bufferViews, error] = block->read( bitReader, std::numeric_limits<size_t>::max() );
                if ( error != Error::NONE ) {
                    std::cerr << "Erroneous block at offset " << blockOffset << " b: " << toString( error ) << "\n";
                    return {};
                }

                result.append( bufferViews );
                streamBytesRead += bufferViews.size();
            }

            if ( block->isLastBlock() ) {
                const auto footer = gzip::readFooter( bitReader );

                /* We only check for the stream size and CRC32 if we have read the whole stream including the header! */
                if ( gzipHeader ) {
                    if ( streamBytesRead != footer.uncompressedSize ) {
                        std::stringstream message;
                        message << "Mismatching size (" << streamBytesRead << " <-> footer: "
                                << footer.uncompressedSize << ") for gzip stream!";
                        throw std::runtime_error( message.str() );
                    }

                    if ( ( block->crc32() != 0 ) && ( block->crc32() != footer.crc32 ) ) {
                        std::stringstream message;
                        message << "Mismatching CRC32 (0x" << std::hex << block->crc32() << " <-> stored: 0x"
                                << footer.crc32 << ") for gzip stream!";
                        throw std::runtime_error( message.str() );
                    }
                }

                isAtStreamEnd = true;
                gzipHeader = {};
                streamBytesRead = 0;

                if ( bitReader.eof() ) {
                    break;
                }
            }
        }

        result.cleanUnmarkedData();

        /**
         * @todo write window back if we somehow got fully-decoded?
         * @todo Propagate that window through ready prefetched block results?
         * @note The idea here is that the more windows we have, the less extra work (marker replacement) we have to do.
         * @todo reduce buffer size because it is possible now with the automatic marker resolution, I think.
         * @todo add empty pigz block in the middle somewhere, e.g., by concatenating empty.pgz!
         *       This might trip up the ParallelGzipReader!
         */

        result.encodedSizeInBits = bitReader.tell() - blockOffset;
        return result;
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
                    throw std::runtime_error( message.str() );
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

private:
    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    const bool m_isBgzfFile;
    std::shared_ptr<BlockMap> const m_blockMap;
    std::shared_ptr<WindowMap> const m_windowMap;
};
}  // namespace pragzip
