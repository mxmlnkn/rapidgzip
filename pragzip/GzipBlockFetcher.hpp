#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <BlockFetcher.hpp>

#include "BlockFinder.hpp"
#include "blockfinder/Combined.hpp"
#include "deflate.hpp"
#include "gzip.hpp"


namespace pragzip
{
struct BlockData
{
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };
    std::vector<uint8_t> data;
};


template<typename FetchingStrategy = FetchingStrategy::FetchNextSmart>
class GzipBlockFetcher :
    public BlockFetcher<BlockFinder<Combined>, BlockData, FetchingStrategy>
{
public:
    using BaseType = BlockFetcher<::BlockFinder<Combined>, BlockData, FetchingStrategy>;
    using BitReader = pragzip::BitReader;

public:
    GzipBlockFetcher( BitReader                                       bitReader,
                      std::shared_ptr<typename BaseType::BlockFinder> blockFinder,
                      size_t                                          parallelization ) :
        BaseType( std::move( blockFinder ), parallelization ),
        m_bitReader( bitReader )
    {}

    virtual
    ~GzipBlockFetcher()
    {
        this->stopThreadPool();
    }

    /**
     * Notes about prefetchNewBlocks optimization.
     * @todo Beware! The prefetch count is more unstable than I thought it to be! Simply increasing it to
     *       m_parallelization * 4 leads to a slow down of 4x when increasing parallelism from 12 (physical cores)
     *       to 24 (logical cores). I thought that the check on m_prefetching.size() would be enough as a hard
     *       cut off but something else seems to go wrong here!
     *          -> The m_prefetching.size() check is definitely not enough because it might get emptied
     *             after each call! The argument to prefetch should not be changed because it is a maxToPrefetch
     *             limit. This limit is important because else it could basically lead to n blocks being
     *             prefetched only for them to get evicted from the cache right after they were inserted.
     *             The cache has to have space to hold the maximum prefetch count!
     *
     * m && time tools/pragzip -c -P 24 -v -o /dev/shm/small.bgz.decoded -f /dev/shm/small.bgz
     *
     * m_fetchingStrategy.prefetch( m_parallelization )
     *
     * m_fetchingStrategy.prefetch( m_parallelization * 4 )
     * @verbatim
     * [12:17:17.422][140010449418048] [BlockFetcher::~BlockFetcher]
     *     Cache hits                 : 5019
     *     misses                     : 41
     *     prefetched blocks          : 30036
     *     direct prefetch queue hits : 3166
     *     hit rate                   : 0.995016
     *     time spent in:
     *         bzip2::readBlockData          : 0 s
     *         time spent in decodeBlock     : 27.2933 s
     *         time spent waiting on futures : 0.747354 s
     *
     * real   0m1.630s
     * user   0m17.495s
     * sys    0m1.588s
     * @endverbatim
     *   -> This is pure sequential decoding, so I would never even have expected cache misses!
     *      And the number of prefetched blocks is much higher than actual cache hits !!!
     *      This seems to indicate that we somehow seem to prefetch some blocks multiple times without ever
     *      using them...
     *
     * @verbatim
     * [BlockFetcher::~BlockFetcher]
     *     Cache hits                 : 7709
     *     misses                     : 1
     *     prefetched blocks          : 8228
     *     direct prefetch queue hits : 516
     *     hit rate                   : 0.999878
     *     time spent in:
     *         bzip2::readBlockData          : 0 s
     *         time spent in decodeBlock     : 6.00846 s
     *         time spent waiting on futures : 0.0694636 s
     * real   0m0.486s
     * user   0m4.923s
     * sys    0m0.585s
     * @endverbatim
     *
     * The one cache miss is expected for sequential decoding because it happens on the very first access.
     * The number of prefetched blocks is identical to the sum of total cache hits, i.e., direct cache hits +
     * cache hits in the prefetch queue! The file contains 8227 gzip streams, which is weirdly off by 1 but it
     * should be close enough for performance.
     */

    [[nodiscard]] BlockData
    decodeBlock( size_t blockIndex,
                 size_t blockOffset ) const override
    {
        std::optional<ArrayView<std::uint8_t, pragzip::deflate::MAX_WINDOW_SIZE> > window;
        if ( const auto match = windows.find( blockOffset ); match != windows.end() ) {
            window.emplace( match->second );
        }
        return decodeBlock( blockOffset, this->m_blockFinder->get( blockIndex + 1 ), window );
    }

    /**
     * @param untilOffset Decode to excluding at least this compressed offset. It can be the offset of the next
     *                    deflate block or next gzip stream but it can also be the starting guess for the block finder
     *                    to find the next deflate block or gzip stream.
     * @param initialWindow Required to resume decoding. Can be empty if, e.g., the blockOffset is at the gzip stream
     *                      start.
     * @todo Make it automatically decompress to replacement markers if no valid window was given but needed anyway.
     */
    [[nodiscard]] BlockData
    decodeBlock( size_t                blockOffset,
                 std::optional<size_t> untilOffset,
                 std::optional<ArrayView<std::uint8_t, pragzip::deflate::MAX_WINDOW_SIZE> > initialWindow ) const
    {
        BitReader bitReader( m_bitReader );
        bitReader.seek( blockOffset );

        BlockData result;
        result.encodedOffsetInBits = blockOffset;

        /* If true, then read the gzip header. We cannot simply check the gzipHeader optional because we might
         * start reading in the middle of a gzip stream and will not meet the gzip header for a while or never. */
        bool isAtStreamEnd = false;
        size_t streamBytesRead = 0;
        std::optional<pragzip::gzip::Header> gzipHeader;

        pragzip::deflate::Block block;
        if ( initialWindow ) {
            block.setInitialWindow( *initialWindow );
        } else {
            block.setInitialWindow();
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
                const auto [header, error] = pragzip::gzip::readHeader( bitReader );
                if ( error != pragzip::Error::NONE ) {
                    std::cerr << "Encountered error while trying to read gzip header: "
                              << pragzip::toString( error ) << "\n";
                    break;
                }

                gzipHeader = std::move( header );
                block.setInitialWindow();

                if ( untilOffset && ( bitReader.tell() >= untilOffset ) ) {
                    break;
                }
            }

            auto error = block.readHeader( bitReader );
            if ( error != pragzip::Error::NONE ) {
                std::cerr << "Erroneous block header at offset " << blockOffset << " b (after read: "
                          << bitReader.tell() << " b): " << pragzip::toString( error ) << "\n";
                return {};
            }

            /* Loop until we have read the full contents of the current deflate block. */
            while ( !block.eob() )
            {
                error = block.read( bitReader, std::numeric_limits<size_t>::max() ).second;
                if ( error != pragzip::Error::NONE ) {
                    std::cerr << "Erroneous block at offset " << blockOffset
                              << " b: " << pragzip::toString( error ) << "\n";
                    return {};
                }

                for ( const auto& buffer : block.lastBuffers() ) {
                    if ( buffer.empty() ) {
                        continue;
                    }

                    const auto oldSize = result.data.size();
                    result.data.resize( oldSize + buffer.size() );
                    std::memcpy( result.data.data() + oldSize, buffer.data(), buffer.size() );
                    streamBytesRead += buffer.size();
                }
            }

            if ( block.isLastBlock() ) {
                const auto footer = pragzip::gzip::readFooter( bitReader );

                /* We only check for the stream size and CRC32 if we have read the whole stream including the header! */
                if ( gzipHeader ) {
                    if ( streamBytesRead != footer.uncompressedSize ) {
                        std::stringstream message;
                        message << "Mismatching size (" << streamBytesRead << " <-> footer: " << footer.uncompressedSize << ") for gzip stream!";
                        throw std::runtime_error( message.str() );
                    }

                    if ( ( block.crc32() != 0 ) && ( block.crc32() != footer.crc32 ) ) {
                        std::stringstream message;
                        message << "Mismatching CRC32 (0x" << std::hex << block.crc32() << " <-> stored: 0x"
                                << footer.crc32 << ") for gzip stream!";
                        throw std::runtime_error( message.str() );
                    }

                    if ( block.crc32() != 0 ) {
                        std::stringstream message;
                        message << "Validated CRC32 0x" << std::hex << block.crc32() << " for gzip stream!\n";
                        std::cerr << message.str();
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

        result.encodedSizeInBits = bitReader.tell() - blockOffset;
        return result;
    }

public:
    /** @todo put into BlockMap and use that here instead? */
    std::unordered_map<size_t, std::array<std::uint8_t, pragzip::deflate::MAX_WINDOW_SIZE> > windows;

private:
    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
};
}  // namespace pragzip
