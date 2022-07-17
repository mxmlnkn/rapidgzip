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
#include "deflate.hpp"
#include "gzip.hpp"


namespace pragzip
{
struct BlockData
{
public:
    void
    append( std::vector<uint8_t>&& toAppend )
    {
        if ( !toAppend.empty() ) {
            data.emplace_back( std::move( toAppend ) );
        }
    }

    void
    append( const deflate::Block</* CRC32 */ false>::BufferViews& buffers )
    {
        if ( buffers.dataWithMarkersSize() > 0 ) {
            if ( !data.empty() ) {
                throw std::invalid_argument( "It is not allowed to append data with markers when fully decoded data "
                                             "has already been appended because the ordering will be wrong!" );
            }

            auto& copied = dataWithMarkers.emplace_back( buffers.dataWithMarkersSize() );
            size_t offset{ 0 };
            for ( const auto& buffer : buffers.dataWithMarkers ) {
                std::memcpy( copied.data() + offset, buffer.data(), buffer.size() * sizeof( buffer[0] ) );
                offset += buffer.size();
            }
        }

        if ( buffers.dataSize() > 0 ) {
            auto& copied = data.emplace_back( buffers.dataSize() );
            size_t offset{ 0 };
            for ( const auto& buffer : buffers.data ) {
                std::memcpy( copied.data() + offset, buffer.data(), buffer.size() );
                offset += buffer.size();
            }
        }
    }

    [[nodiscard]] size_t
    dataSize() const noexcept
    {
        const auto addSize = [] ( const size_t size, const auto& container ) { return size + container.size(); };
        return std::accumulate( data.begin(), data.end(), size_t( 0 ), addSize );
    }

    [[nodiscard]] size_t
    dataWithMarkersSize() const noexcept
    {
        const auto addSize = [] ( const size_t size, const auto& container ) { return size + container.size(); };
        return std::accumulate( dataWithMarkers.begin(), dataWithMarkers.end(), size_t( 0 ), addSize );
    }

    [[nodiscard]] size_t
    size() const noexcept
    {
        return dataSize() + dataWithMarkersSize();
    }

    void
    applyWindow( const deflate::WindowView& window )
    {
        if ( dataWithMarkersSize() == 0 ) {
            dataWithMarkers.clear();
            return;
        }

        std::vector<uint8_t> downcasted( dataWithMarkersSize() );
        size_t offset{ 0 };
        for ( auto& chunk : dataWithMarkers ) {
            deflate::Block<>::replaceMarkerBytes( &chunk, window );
            std::transform( chunk.begin(), chunk.end(), downcasted.begin() + offset,
                            [] ( const auto symbol ) { return static_cast<uint8_t>( symbol ); } );
            offset += chunk.size();
        }
        data.insert( data.begin(), std::move( downcasted ) );
        dataWithMarkers.clear();
    }

    /**
     * Returns the last 32 KiB decoded bytes. This can be called after decoding a block has finished
     * and then can be used to store and load it with deflate::Block::setInitialWindow to restart decoding
     * with the next block. Because this is not supposed to be called very often, it returns a copy of
     * the data instead of views.
     */
    [[nodiscard]] deflate::Window
    getLastWindow( const deflate::WindowView& previousWindow ) const
    {
        if ( dataWithMarkersSize() > 0 ) {
            throw std::invalid_argument( "No valid window available. Please call applyWindow first!" );
        }

        deflate::Window window;
        size_t nBytesWritten{ 0 };

        /* Fill the result from the back with data from our buffer. */
        for ( auto chunk = data.rbegin(); ( chunk != data.rend() ) && ( nBytesWritten < window.size() ); ++chunk ) {
            for ( auto symbol = chunk->rbegin(); ( symbol != chunk->rend() ) && ( nBytesWritten < window.size() );
                  ++symbol, ++nBytesWritten )
            {
                window[window.size() - 1 - nBytesWritten] = *symbol;
            }
        }

        /* Fill the remaining part with the given window. This should only happen for very small BlockData sizes. */
        if ( nBytesWritten < deflate::MAX_WINDOW_SIZE ) {
            const auto remainingBytes = deflate::MAX_WINDOW_SIZE - nBytesWritten;
            assert( remainingBytes <= previousWindow.size() );
            std::copy( std::reverse_iterator( previousWindow.end() ),
                       std::reverse_iterator( previousWindow.end() ) + remainingBytes,
                       window.rbegin() + nBytesWritten );
        }

        return window;
    }

    /**
     * Check decoded blocks that account for possible markers whether they actually contain markers and if not so
     * convert and move them to actual decoded data.
     */
    void
    cleanUnmarkedData()
    {
        while ( !dataWithMarkers.empty() ) {
            const auto& toDowncast = dataWithMarkers.back();
            /* Try to not only downcast whole chunks of data but also as many bytes as possible for the last chunk. */
            const auto marker = std::find_if(
                toDowncast.rbegin(), toDowncast.rend(),
                [] ( auto value ) { return value > std::numeric_limits<uint8_t>::max(); } );

            const auto sizeWithoutMarkers = static_cast<size_t>( std::distance( toDowncast.rbegin(), marker ) );
            auto downcasted = data.emplace( data.begin(), sizeWithoutMarkers );
            std::transform( marker.base(), toDowncast.end(), downcasted->begin(),
                            [] ( auto symbol ) { return static_cast<uint8_t>( symbol ); } );

            if ( marker == toDowncast.rend() ) {
                dataWithMarkers.pop_back();
            } else {
                dataWithMarkers.back().resize( dataWithMarkers.back().size() - sizeWithoutMarkers );
                break;
            }
        }
    }

public:
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };

    /**
     * Use vectors of vectors to avoid reallocations. The order of this data is:
     * - @ref dataWithMarkers (front to back)
     * - @ref data (front to back)
     * This order is fixed because there should be no reason for markers after we got enough data without markers!
     * There is no append( BlockData ) method because this property might not be retained after using
     * @ref cleanUnmarkedData.
     */
    std::vector<std::vector<uint16_t> > dataWithMarkers;
    std::vector<std::vector<uint8_t> > data;
};


template<typename FetchingStrategy>
class GzipBlockFetcher :
    public BlockFetcher<BlockFinder<blockfinder::Interface>, BlockData, FetchingStrategy>
{
public:
    using BaseType = BlockFetcher<::BlockFinder<blockfinder::Interface>, BlockData, FetchingStrategy>;
    using BitReader = pragzip::BitReader;

    struct BlockInfo
    {
        deflate::Window window;
    };

    using WindowMap = std::unordered_map</* encoded block offset */ size_t, BlockInfo>;

public:
    GzipBlockFetcher( BitReader                                       bitReader,
                      std::shared_ptr<typename BaseType::BlockFinder> blockFinder,
                      std::shared_ptr<BlockMap>                       blockMap,
                      bool                                            isBgzfFile,
                      size_t                                          parallelization ) :
        BaseType( std::move( blockFinder ), parallelization ),
        m_bitReader( bitReader ),
        m_isBgzfFile( isBgzfFile ),
        m_blockMap( std::move( blockMap ) )
    {
        if ( !m_blockMap ) {
            throw std::invalid_argument( "BlockMap must be valid!" );
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
        std::optional<size_t> decodedSize;
        if ( blockInfo ) {
            decodedSize = blockInfo->decodedSizeInBytes;
        }

        std::optional<deflate::WindowView> window;
        {
            std::scoped_lock lock( blockMapMutex );
            if ( const auto match = blockMap.find( blockOffset ); match != blockMap.end() ) {
                window.emplace( match->second.window );
            }
        }
        return decodeBlock( m_bitReader, blockOffset, this->m_blockFinder->get( blockIndex + 1 ),
                            /* needsNoInitialWindow */ ( blockIndex == 0 ) || m_isBgzfFile,
                            std::move( window ), decodedSize );
    }

public:
    /**
     * @param untilOffset Decode to excluding at least this compressed offset. It can be the offset of the next
     *                    deflate block or next gzip stream but it can also be the starting guess for the block finder
     *                    to find the next deflate block or gzip stream.
     * @param initialWindow Required to resume decoding. Can be empty if, e.g., the blockOffset is at the gzip stream
     *                      start.
     * @todo Make it automatically decompress to replacement markers if no valid window was given but needed anyway.
     */
    [[nodiscard]] static BlockData
    decodeBlock( const BitReader&                   originalBitReader,
                 size_t                             blockOffset,
                 std::optional<size_t>              untilOffset,
                 bool                               needsNoInitialWindow,
                 std::optional<deflate::WindowView> initialWindow,
                 std::optional<size_t>              decodedSize )
    {
        if ( ( needsNoInitialWindow || initialWindow ) && decodedSize && ( *decodedSize > 0 ) ) {
            return decodeBlockWithZlib( originalBitReader,
                                        blockOffset,
                                        untilOffset ? *untilOffset : originalBitReader.size(),
                                        needsNoInitialWindow ? std::nullopt : initialWindow,
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
        if ( needsNoInitialWindow ) {
            block->setInitialWindow();
        } else if ( initialWindow ) {
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
        setWindow( deflate::WindowView window )
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
    decodeBlockWithZlib( const BitReader&                   originalBitReader,
                         size_t                             blockOffset,
                         size_t                             untilOffset,
                         std::optional<deflate::WindowView> initialWindow,
                         size_t                             decodedSize )
    {
        BitReader bitReader( originalBitReader );
        bitReader.seek( blockOffset );
        ZlibDeflateWrapper deflateWrapper( std::move( bitReader ) );

        if ( initialWindow ) {
            deflateWrapper.setWindow( *initialWindow );
        }

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

public:
    WindowMap blockMap;
    mutable std::mutex blockMapMutex;

private:
    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    const bool m_isBgzfFile;
    std::shared_ptr<BlockMap> const m_blockMap;
};
}  // namespace pragzip
