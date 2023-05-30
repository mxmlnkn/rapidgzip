#pragma once

#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <BlockFetcher.hpp>
#include <BlockFinder.hpp>
#include <ParallelBitStringFinder.hpp>

#include "bzip2.hpp"


struct BlockHeaderData
{
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };  /**< When calling readBlockheader, only contains valid data if EOS block. */

    uint32_t expectedCRC{ 0 };  /**< if isEndOfStreamBlock == true, then this is the stream CRC. */
    bool isEndOfStreamBlock{ false };
    bool isEndOfFile{ false };
};


struct BlockData :
    public BlockHeaderData
{
    std::vector<uint8_t> data;
    uint32_t calculatedCRC{ 0xFFFFFFFFL };
};


template<typename FetchingStrategy = FetchingStrategy::FetchNextAdaptive>
class BZ2BlockFetcher :
    public BlockFetcher<
        ::BlockFinder<ParallelBitStringFinder<bzip2::MAGIC_BITS_SIZE> >,
        BlockData,
        FetchingStrategy>
{
public:
    using BaseType = BlockFetcher<::BlockFinder<ParallelBitStringFinder<bzip2::MAGIC_BITS_SIZE> >,
                                  BlockData,
                                  FetchingStrategy>;
    using BitReader = bzip2::BitReader;

public:
    BZ2BlockFetcher( BitReader                                       bitReader,
                     std::shared_ptr<typename BaseType::BlockFinder> blockFinder,
                     size_t                                          parallelization ) :
        BaseType( std::move( blockFinder ), parallelization ),
        m_bitReader( bitReader ),
        m_blockSize100k( bzip2::readBzip2Header( bitReader ) )
    {}

    virtual
    ~BZ2BlockFetcher()
    {
        this->stopThreadPool();
    }

    [[nodiscard]] BlockHeaderData
    readBlockHeader( size_t blockOffset ) const
    {
        BitReader bitReader( m_bitReader );
        bitReader.seek( blockOffset );
        bzip2::Block block( bitReader );

        BlockHeaderData result;
        result.encodedOffsetInBits = blockOffset;
        result.isEndOfStreamBlock = block.eos();
        result.isEndOfFile = block.eof();
        result.expectedCRC = block.bwdata.headerCRC;

        if ( block.eos() ) {
            result.encodedSizeInBits = block.encodedSizeInBits;
        }

        return result;
    }

private:
    [[nodiscard]] BlockData
    decodeBlock( size_t                  blockOffset,
                 [[maybe_unused]] size_t nextBlockOffset ) const override
    {
        BitReader bitReader( m_bitReader );
        bitReader.seek( blockOffset );
        bzip2::Block block( bitReader );

        BlockData result;
        result.encodedOffsetInBits = blockOffset;
        result.isEndOfStreamBlock = block.eos();
        result.isEndOfFile = block.eof();
        result.expectedCRC = block.bwdata.headerCRC;

        /* Actually, this should never happen with the current implementation because only blocks found by the
         * block finder will be handled here and the block finder does not search for EOS magic bits. */
        if ( block.eos() ) {
            result.encodedSizeInBits = block.encodedSizeInBits;
            return result;
        }


        const auto t0 = std::chrono::high_resolution_clock::now();
        block.readBlockData();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto dt = std::chrono::duration<double>( t1 - t0 ).count();
        {
            std::scoped_lock lock( this->m_analyticsMutex );
            this->m_readBlockDataTotalTime += dt;
        }

        size_t decodedDataSize = 0;
        do
        {
            /* Increase buffer for next batch. Unfortunately we can't find the perfect size beforehand because
             * we don't know the amount of decoded bytes in the block. */
            /** @todo We do have that information after the block index has been built! */
            if ( result.data.empty() ) {
                /* Just a guess to avoid reallocations at smaller sizes. Must be >= 255 though because the decodeBlock
                 * method might return up to 255 copies caused by the runtime length decoding! */
                result.data.resize( m_blockSize100k * 100'000 + 255 );
            } else {
                result.data.resize( result.data.size() * 2 );
            }

            decodedDataSize += block.bwdata.decodeBlock(
                result.data.size() - 255U - decodedDataSize,
                reinterpret_cast<char*>( result.data.data() ) + decodedDataSize
            );
        }
        while ( block.bwdata.writeCount > 0 );

        result.data.resize( decodedDataSize );
        result.encodedSizeInBits = block.encodedSizeInBits;
        result.calculatedCRC = block.bwdata.dataCRC;

        /* The next block offset might be a farther out in case this is the last valid block in the stream! */
        assert( ( nextBlockOffset == std::numeric_limits<size_t>::max() )
                || ( blockOffset + result.encodedSizeInBits <= nextBlockOffset ) );

        return result;
    }

private:
    /* Variables required by decodeBlock and which therefore should be either const or locked. */
    const BitReader m_bitReader;
    uint8_t m_blockSize100k;
};
