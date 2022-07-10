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

#include <BlockFetcher.hpp>

#include "BlockFinder.hpp"
#include "blockfinder/Interface.hpp"
#include "deflate.hpp"
#include "gzip.hpp"


namespace pragzip
{
struct BlockData
{
public:
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
            throw std::invalid_argument( "No valid window available!" );
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

public:
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };

    /* Use vectors of vectors to avoid reallocations. */
    std::vector<std::vector<uint8_t> > data;
    std::vector<std::vector<uint16_t> > dataWithMarkers;
};


template<typename FetchingStrategy>
class GzipBlockFetcher :
    public BlockFetcher<BlockFinder<blockfinder::Interface>, BlockData, FetchingStrategy>
{
public:
    using BaseType = BlockFetcher<::BlockFinder<blockfinder::Interface>, BlockData, FetchingStrategy>;
    using BitReader = pragzip::BitReader;
    using Windows = std::unordered_map</* block offet */ size_t, deflate::Window>;

public:
    GzipBlockFetcher( BitReader                                       bitReader,
                      std::shared_ptr<typename BaseType::BlockFinder> blockFinder,
                      bool                                            isBgzfFile,
                      size_t                                          parallelization ) :
        BaseType( std::move( blockFinder ), parallelization ),
        m_bitReader( bitReader ),
        m_isBgzfFile( isBgzfFile )
    {}

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
        std::optional<deflate::WindowView> window;
        {
            std::scoped_lock lock( windowsMutex );
            if ( const auto match = windows.find( blockOffset ); match != windows.end() ) {
                window.emplace( match->second );
            }
        }
        return decodeBlock( m_bitReader, blockOffset, this->m_blockFinder->get( blockIndex + 1 ),
                            /* needsNoInitialWindow */ ( blockIndex == 0 ) || m_isBgzfFile,
                            std::move( window ) );
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
                 std::optional<deflate::WindowView> initialWindow )
    {
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

        /* Try to check previous blocks whether they might be free of markers when we ended up with a fully-decoded
         * window. */
        if ( !result.dataWithMarkers.empty() && !result.data.empty() ) {
            std::vector<std::vector<uint8_t> > downcastedVectors;
            while ( !result.dataWithMarkers.empty()
                    && !deflate::containsMarkers( result.dataWithMarkers.back() ) )
            {
                const auto& toDowncast = result.dataWithMarkers.back();
                auto& downcasted = downcastedVectors.emplace_back( toDowncast.size() );
                std::transform( toDowncast.begin(), toDowncast.end(), downcasted.begin(),
                                [] ( auto symbol ) { return static_cast<uint8_t>( symbol ); } );
                result.dataWithMarkers.pop_back();
            }

            /* The above loop iterates starting from the back and also insert at the back in the result,
             * therefore, we need to reverse the temporary container again to get the correct order. */
            result.data.insert( result.data.begin(),
                                std::move_iterator( downcastedVectors.rbegin() ),
                                std::move_iterator( downcastedVectors.rend() ) );
        }

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

public:
    Windows windows;
    mutable std::mutex windowsMutex;

private:
    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    const bool m_isBgzfFile;
};
}  // namespace pragzip
