#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <crc32.hpp>
#include <DecodedData.hpp>
#include <gzip.hpp>


namespace rapidgzip
{
/**
 * This class adds higher-level capabilities onto @ref deflate::DecodedData, which was only intended for
 * returning decompression results and aggregating them during decompression of a single deflate block.
 * This class instead is intended to aggregate results from multiple deflate blocks, possibly even multiple
 * gzip streams. It is used to hold the chunk data for parallel decompression.
 * It also adds some further metadata like deflate block and stream boundaries and helpers for creating
 * evenly distributed checkpoints for a gzip seek index.
 *
 * Specialized use cases can optimize memory usage or add post-processing steps by implementing the two
 * @ref append methods, @ref applyWindow, and @ref finalize. The shadowed methods in the base class
 * should be called from the reimplemented methods in order to keep default functionality. This call
 * can also be knowingly omitted, e.g., for only counting bytes instead of appending them.
 *
 * - @ref append is called by @ref GzipChunkFetcher after each deflate::Block call back, which could be
 *   every block or up to maximum 32 KiB of decompressed data.
 * - @ref finalize is called after the first stage of decompression has finished.
 *   At this point, the number of elements in the chunk is finalized. Elements can be 16-bit wide markers.
 * - @ref applyWindow is called during the second decompression stage and the ChunkData will hold the fully
 *   decompressed data after this call.
 */
struct ChunkData :
    public deflate::DecodedData
{
    using BaseType = deflate::DecodedData;

    struct BlockBoundary
    {
        size_t encodedOffset;
        size_t decodedOffset;

        [[nodiscard]] bool
        operator==( const BlockBoundary& other ) const
        {
            return ( encodedOffset == other.encodedOffset ) && ( decodedOffset == other.decodedOffset );
        }
    };

    struct Footer
    {
        /**
         * The blockBoundary is currently (2023-08-26) unused. It is intended to aid block splitting in order
         * to split after a gzip footer because then the window is known to be empty, which would save space
         * and time.
         * The uncompressed block boundary offset is unambiguous.
         * The compressed block boundary is more ambiguous. There are three possibilities:
         *  - The end of the preceding deflate block. The footer start is then the next byte-aligned boundary.
         *  - The byte-aligned footer start.
         *  - The byte-aligned footer end, which is the file end or the next gzip stream start.
         *    For gzip, it is exactly FOOTER_SIZE bytes after the footer start.
         * Thoughts about the choice:
         *  - The offset after the footer is more relevant to the intended block splitting improvement.
         *  - The previous deflate block end contains the most information because the other two possible
         *    choices can be derived from it by rounding up and adding FOOTER_SIZE. The inverse is not true.
         *  - The previous block end might be the most stable choice because stopping at that boundary is
         *    already a requirement for using ISA-l without an exact untilOffset. Stopping at the footer end
         *    might not work perfectly and might already have read some of the next block.
         * Currently, the unit tests, test that all possibilities to derive the footer offsets: GzipReader, decodeBlock,
         * decodeBlockWithInflateWrapper with ISA-L or zlib, return the same value.
         * That value is currently the footer end because it seemed easier to implement. This might be subjecft to
         * change until it is actually used for something (e.g. smarter block splitting).
         * The most complicated to implement but least ambiguous solution would be to add all three boundaries to
         * this struct.
         */
        BlockBoundary blockBoundary;
        gzip::Footer gzipFooter;
    };

    struct Subblock
    {
        size_t encodedOffset{ 0 };
        size_t encodedSize{ 0 };
        size_t decodedSize{ 0 };

        [[nodiscard]] bool
        operator==( const Subblock& other ) const
        {
            return ( encodedOffset == other.encodedOffset )
                   && ( encodedSize == other.encodedSize )
                   && ( decodedSize == other.decodedSize );
        }
    };

public:
    void
    append( deflate::DecodedVector&& toAppend )
    {
        crc32s.back().update( toAppend.data(), toAppend.size() );

        BaseType::append( std::move( toAppend ) );
    }

    void
    append( deflate::DecodedDataView const& toAppend )
    {
        /* Ignore data with markers. Those will be CRC32 computed inside @ref applyWindow. */
        for ( const auto& buffer : toAppend.data ) {
            crc32s.back().update( buffer.data(), buffer.size() );
        }

        BaseType::append( toAppend );
    }

    void
    applyWindow( WindowView const& window )
    {
        BaseType::applyWindow( window );

        const auto alreadyProcessedSize = std::accumulate(
            crc32s.begin(), crc32s.end(), size_t( 0 ),
            [] ( const auto sum, const auto& crc32 ) { return sum + crc32.streamSize(); } );
        if ( crc32s.front().enabled() && ( alreadyProcessedSize < BaseType::dataSize() ) ) {
            /* Markers should only appear up to the first gzip footer because otherwise a new gzip stream
             * would have started. A new gzip stream must not contain markers because there are no unresolvable
             * back-references! Because of this, it is safe to only update the first CRC32.
             * Beware that we do not only have to compute the CRC32 of markers but also for data that has been
             * been converted from dataWithMarkers inside DecodedData::cleanUnmarkedData. */
            const auto toProcessSize = BaseType::dataSize() - alreadyProcessedSize;
            CRC32Calculator crc32;
            for ( auto it = DecodedData::Iterator( *this, 0, toProcessSize ); static_cast<bool>( it ); ++it ) {
                const auto [buffer, size] = *it;
                crc32.update( buffer, size );
            }
            crc32s.front().prepend( crc32 );
        }
    }

    [[nodiscard]] bool
    matchesEncodedOffset( size_t offset ) const noexcept
    {
        if ( maxEncodedOffsetInBits == std::numeric_limits<size_t>::max() ) {
            return offset == encodedOffsetInBits;
        }
        return ( encodedOffsetInBits <= offset ) && ( offset <= maxEncodedOffsetInBits );
    }

    void
    setEncodedOffset( size_t offset );

    [[nodiscard]] std::vector<Subblock>
    split( [[maybe_unused]] const size_t spacing ) const;

    /**
     * @note Probably should not be called internally because it is allowed to be shadowed by a child class method.
     */
    void
    finalize( size_t blockEndOffsetInBits )
    {
        const auto oldMarkerSize = BaseType::dataWithMarkersSize();
        cleanUnmarkedData();
        const auto toProcessSize = oldMarkerSize - BaseType::dataWithMarkersSize();
        if ( toProcessSize > 0 ) {
            CRC32Calculator crc32;
            /* Iterate over contiguous chunks of memory. */
            for ( auto it = DecodedData::Iterator( *this, 0, toProcessSize ); static_cast<bool>( it ); ++it ) {
                const auto [buffer, size] = *it;
                crc32.update( buffer, size );
            }
            /* Note that the data with markers ought not cross footer boundaries because after a footer,
             * a new gzip stream begins, which should be known to not contain any unresolvable backreferences.
             * That's why we can simply merge the CRC32 for the cleaned data with the first CRC32. */
            crc32s.front().prepend( crc32 );
        }

        encodedSizeInBits = blockEndOffsetInBits - encodedOffsetInBits;
        decodedSizeInBytes = BaseType::size();
    }

    /**
     * Appends a deflate block boundary.
     */
    void
    appendDeflateBlockBoundary( const size_t encodedOffset,
                                const size_t decodedOffset )
    {
        if ( blockBoundaries.empty()
             || ( blockBoundaries.back().encodedOffset != encodedOffset )
             || ( blockBoundaries.back().decodedOffset != decodedOffset ) )
        {
            blockBoundaries.emplace_back( BlockBoundary{ encodedOffset, decodedOffset } );
        }
    }

    /**
     * Appends gzip footer information at the given offset.
     */
    void
    appendFooter( const size_t encodedOffset,
                  const size_t decodedOffset,
                  gzip::Footer footer )
    {
        typename ChunkData::Footer footerResult;
        footerResult.blockBoundary = { encodedOffset, decodedOffset };
        footerResult.gzipFooter = footer;
        footers.emplace_back( footerResult );

        const auto wasEnabled = crc32s.back().enabled();
        crc32s.emplace_back();
        crc32s.back().setEnabled( wasEnabled );
    }

    void
    setCRC32Enabled( bool enabled )
    {
        for ( auto& calculator : crc32s ) {
            calculator.setEnabled( enabled );
        }
    }

public:
    /* This should only be evaluated when it is unequal std::numeric_limits<size_t>::max() and unequal
     * Base::encodedOffsetInBits. Then, [Base::encodedOffsetInBits, maxEncodedOffsetInBits] specifies a valid range
     * for the block offset. Such a range might happen for finding uncompressed deflate blocks because of the
     * byte-padding. */
    size_t maxEncodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    /* Initialized with size() after thread has finished writing into ChunkData. Redundant but avoids a lock
     * because the marker replacement will momentarily lead to different results returned by size! */
    size_t decodedSizeInBytes{ 0 };

    /* Decoded offsets are relative to the decoded offset of this ChunkData because that might not be known
     * during first-pass decompression. */
    std::vector<BlockBoundary> blockBoundaries;
    std::vector<Footer> footers;
    /* There will be ( footers.size() + 1 ) CRC32 calculators. */
    std::vector<CRC32Calculator> crc32s{ std::vector<CRC32Calculator>( 1 ) };

    /* Benchmark results */
    size_t falsePositiveCount{ 0 };
    double blockFinderDuration{ 0 };
    double decodeDuration{ 0 };
    double decodeDurationInflateWrapper{ 0 };
    double decodeDurationIsal{ 0 };
    double appendDuration{ 0 };

    bool stoppedPreemptively{ false };
};


inline void
ChunkData::setEncodedOffset( size_t offset )
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


[[nodiscard]] inline std::vector<ChunkData::Subblock>
ChunkData::split( [[maybe_unused]] const size_t spacing ) const
{
    if ( encodedOffsetInBits != maxEncodedOffsetInBits ) {
        throw std::invalid_argument( "ChunkData::split may only be called after setEncodedOffset!" );
    }

    if ( spacing == 0 ) {
        throw std::invalid_argument( "Spacing must be a positive number of bytes." );
    }

    const auto decompressedSize = decodedSizeInBytes;
    if ( ( encodedSizeInBits == 0 ) && ( decompressedSize == 0 ) ) {
        return {};
    }

    const auto nBlocks = static_cast<size_t>( std::round( static_cast<double>( decompressedSize )
                                                          / static_cast<double>( spacing ) ) );
    Subblock wholeChunkAsSubblock;
    wholeChunkAsSubblock.encodedOffset = encodedOffsetInBits;
    wholeChunkAsSubblock.encodedSize = encodedSizeInBits;
    wholeChunkAsSubblock.decodedSize = decompressedSize;
    /* blockBoundaries does not contain the first block begin but all thereafter including the boundary after
     * the last block, i.e., the begin of the next deflate block not belonging to this ChunkData. */
    if ( ( nBlocks <= 1 ) || blockBoundaries.empty() ) {
        return { wholeChunkAsSubblock };
    }

    /* The idea for partitioning is: Divide the size evenly and into subblocks and then choose the block boundary
     * that is closest to that value. */
    const auto perfectSpacing = static_cast<double>( decompressedSize ) / static_cast<double>( nBlocks );

    std::vector<Subblock> subblocks;
    subblocks.reserve( nBlocks + 1 );

    BlockBoundary lastBoundary{ encodedOffsetInBits, 0 };
    /* The first and last boundaries are static, so we only need to find nBlocks - 1 further boundaries. */
    for ( size_t iSubblock = 1; iSubblock < nBlocks; ++iSubblock ) {
        const auto perfectDecompressedOffset = static_cast<size_t>( iSubblock * perfectSpacing );

        const auto isCloser =
            [perfectDecompressedOffset] ( const auto& b1, const auto& b2 )
            {
                return absDiff( b1.decodedOffset, perfectDecompressedOffset )
                       < absDiff( b2.decodedOffset, perfectDecompressedOffset );
            };
        auto closest = std::min_element( blockBoundaries.begin(), blockBoundaries.end(), isCloser );

        /* If there are duplicate decodedOffset, min_element returns the first, so skip over empty blocks (pigz).
         * Using the last block with the same decodedOffset makes handling the last block after this for-loop easier. */
        while ( ( std::next( closest ) != blockBoundaries.end() )
                && ( closest->decodedOffset == std::next( closest )->decodedOffset ) )
        {
            ++closest;
        }

        /* For very small spacings, the same boundary might be found twice. Avoid empty subblocks because of that. */
        if ( closest->decodedOffset <= lastBoundary.decodedOffset ) {
            continue;
        }

        if ( closest->encodedOffset <= lastBoundary.encodedOffset ) {
            throw std::logic_error( "If the decoded offset is strictly larger than so must be the encoded one!" );
        }

        Subblock subblock;
        subblock.encodedOffset = lastBoundary.encodedOffset;
        subblock.encodedSize = closest->encodedOffset - lastBoundary.encodedOffset;
        subblock.decodedSize = closest->decodedOffset - lastBoundary.decodedOffset;
        subblocks.emplace_back( subblock );
        lastBoundary = *closest;
    }

    if ( lastBoundary.decodedOffset > decompressedSize ) {
        throw std::logic_error( "There should be no boundary outside of the chunk range!" );
    }
    if ( ( lastBoundary.decodedOffset < decompressedSize ) || subblocks.empty() ) {
        /* Create the last subblock from lastBoundary and the chunk end. */
        Subblock subblock;
        subblock.encodedOffset = lastBoundary.encodedOffset,
        subblock.encodedSize = encodedOffsetInBits + encodedSizeInBits - lastBoundary.encodedOffset,
        subblock.decodedSize = decompressedSize - lastBoundary.decodedOffset,
        subblocks.emplace_back( subblock );
    } else if ( lastBoundary.decodedOffset == decompressedSize ) {
        /* Enlarge the last subblock encoded size to also encompass the empty blocks before the chunk end.
         * Assuming that blockBoundaries contain the boundary at the chunk end and knowing that the loop
         * above always searches for the last boundary with the same decodedOffset, this branch shouldn't happen. */
        subblocks.back().encodedSize = encodedOffsetInBits + encodedSizeInBits - subblocks.back().encodedOffset;
    }

    const auto subblockEncodedSizeSum =
        std::accumulate( subblocks.begin(), subblocks.end(), size_t( 0 ),
                         [] ( size_t sum, const auto& block ) { return sum + block.encodedSize; } );
    const auto subblockDecodedSizeSum =
        std::accumulate( subblocks.begin(), subblocks.end(), size_t( 0 ),
                         [] ( size_t sum, const auto& block ) { return sum + block.decodedSize; } );
    if ( ( subblockEncodedSizeSum != encodedSizeInBits ) || ( subblockDecodedSizeSum != decodedSizeInBytes ) ) {
        std::stringstream message;
        message << "[Warning] Block splitting was unsuccessful. This might result in higher memory usage but is "
                << "otherwise harmless. Please report this performance bug with a reproducing example.\n"
                << "  subblockEncodedSizeSum: " << subblockEncodedSizeSum << "\n"
                << "  encodedSizeInBits     : " << encodedSizeInBits      << "\n"
                << "  subblockDecodedSizeSum: " << subblockDecodedSizeSum << "\n"
                << "  decodedSizeInBytes    : " << decodedSizeInBytes     << "\n";
        std::cerr << std::move( message ).str();
        return { wholeChunkAsSubblock };  // fallback without any splitting done at all
    }

    return subblocks;
}


/**
 * m rapidgzip && src/tools/rapidgzip -v -d -c -P 0 4GiB-base64.gz | wc -c
 * Non-polymorphic: Decompressed in total 4294967296 B in 1.49444 s -> 2873.96 MB/s
 * With virtual ~DecodedData() = default: Decompressed in total 4294967296 B in 3.58325 s -> 1198.62 MB/s
 * I don't know why it happens. Maybe it affects inline of function calls or moves of instances.
 */
static_assert( !std::is_polymorphic_v<ChunkData>, "Simply making it polymorphic halves performance!" );


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
[[nodiscard]] inline bool
writeAllSplice( const int                         outputFileDescriptor,
                const void* const                 dataToWrite,
                size_t const                      dataToWriteSize,
                const std::shared_ptr<ChunkData>& chunkData )
{
#if defined( HAVE_VMSPLICE )
    return SpliceVault::getInstance( outputFileDescriptor ).first->splice( dataToWrite, dataToWriteSize, chunkData );
#else
    return false;
#endif
}


#if defined( HAVE_VMSPLICE )
[[nodiscard]] inline bool
writeAllSplice( [[maybe_unused]] const int                         outputFileDescriptor,
                [[maybe_unused]] const std::shared_ptr<ChunkData>& chunkData,
                [[maybe_unused]] const std::vector<::iovec>&       buffersToWrite )
{
    return SpliceVault::getInstance( outputFileDescriptor ).first->splice( buffersToWrite, chunkData );
}
#endif  // HAVE_VMSPLICE


inline void
writeAll( const std::shared_ptr<ChunkData>& chunkData,
          const int                         outputFileDescriptor,
          const size_t                      offsetInBlock,
          const size_t                      dataToWriteSize )
{
    if ( ( outputFileDescriptor < 0 ) || ( dataToWriteSize == 0 ) ) {
        return;
    }

#ifdef HAVE_IOVEC
    const auto buffersToWrite = toIoVec( *chunkData, offsetInBlock, dataToWriteSize );
    if ( !writeAllSplice( outputFileDescriptor, chunkData, buffersToWrite ) ) {
        writeAllToFdVector( outputFileDescriptor, buffersToWrite );
    }
#else
    using rapidgzip::deflate::DecodedData;

    bool splicable = true;
    for ( auto it = DecodedData::Iterator( *chunkData, offsetInBlock, dataToWriteSize );
          static_cast<bool>( it ); ++it )
    {
        const auto& [buffer, size] = *it;
        if ( splicable ) {
            splicable = writeAllSplice( outputFileDescriptor, buffer, size, chunkData );
        }
        if ( !splicable ) {
            writeAllToFd( outputFileDescriptor, buffer, size );
        }
    }
#endif
}


/**
 * This subclass of @ref ChunkData only counts the decompressed bytes and does not store them.
 */
struct ChunkDataCounter final :
    public ChunkData
{
    void
    append( deflate::DecodedVector&& toAppend )
    {
        decodedSizeInBytes += toAppend.size();
    }

    void
    append( deflate::DecodedDataView const& toAppend )
    {
        decodedSizeInBytes += toAppend.size();
    }

    void
    finalize( size_t blockEndOffsetInBits )
    {
        encodedSizeInBits = blockEndOffsetInBits - encodedOffsetInBits;
        /* Do not overwrite decodedSizeInBytes like is done in the base class
         * because DecodedData::size() would return 0! Instead, it is updated inside append. */
    }

    /**
     * The internal index will only contain the offsets and empty windows but that is fine because
     * this subclass does never require windows. The index should not be exported when this is used.
     */
    [[nodiscard]] deflate::DecodedVector
    getWindowAt( WindowView const& /* previousWindow */,
                 size_t            /* skipBytes */ ) const
    {
        return {};
    }
};
}  // namespace rapidgzip
