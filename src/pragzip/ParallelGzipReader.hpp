#pragma once

#include <algorithm>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <BlockMap.hpp>
#include <common.hpp>
#include <filereader/FileReader.hpp>

#ifdef WITH_PYTHON_SUPPORT
    #include <filereader/Python.hpp>
#endif

#include "GzipBlockFetcher.hpp"
#include "GzipBlockFinder.hpp"
#include "gzip.hpp"
#include "IndexFileFormat.hpp"


/**
 * @note Calls to this class are not thread-safe! Even though they use threads to evaluate them in parallel.
 */
class ParallelGzipReader final :
    public FileReader
{
public:
    /**
     * The fetching strategy should support parallelization via prefetching for sequential accesses while
     * avoiding a lot of useless prefetches for random or multi-stream sequential accesses like those occuring
     * via ratarmount.
     * The fetching strategy does not have to and also should not account for backward and strided accesses
     * because the prefetch and cache units are very large and striding or backward accessing over multiple
     * megabytes should be extremely rare.
     */
    using BlockFetcher = pragzip::GzipBlockFetcher<FetchingStrategy::FetchNextMulti>;
    using BlockFinder = typename BlockFetcher::BlockFinder;
    using BitReader = pragzip::BitReader;

public:
    explicit
    ParallelGzipReader( std::unique_ptr<FileReader> fileReader,
                        size_t                      parallelization = 0 ) :
        m_bitReader( std::move( fileReader ) ),
        m_fetcherParallelization(
            parallelization == 0
            ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
            : parallelization ),
        m_startBlockFinder(
            [&] () {
                return std::make_unique<BlockFinder>( m_bitReader.cloneSharedFileReader(),
                                                     /* spacing */ 1024 * 1024 );
            }
        )
    {
        if ( !m_bitReader.seekable() ) {
            throw std::invalid_argument( "Parallel BZ2 Reader will not work on non-seekable input like stdin (yet)!" );
        }
    }

#ifdef BENCHMARK_CHUNKING
    explicit
    ParallelGzipReader( std::unique_ptr<FileReader> fileReader,
                        size_t                      parallelization,
                        size_t                      nBlocksToSkip ) :
        m_bitReader( std::move( fileReader ) ),
        m_fetcherParallelization(
            parallelization == 0
            ? std::max<size_t>( 1U, std::thread::hardware_concurrency() )
            : parallelization ),
        m_startBlockFinder(
            [this, nBlocksToSkip] () {
                return std::make_unique<BlockFinder>( m_bitReader.cloneSharedFileReader(),
                                                      ( nBlocksToSkip + 1 ) * 32 * 1024 );
            }
        )
    {
        if ( !m_bitReader.seekable() ) {
            throw std::invalid_argument( "Parallel BZ2 Reader will not work on non-seekable input like stdin (yet)!" );
        }
    }
#endif

#ifdef WITH_PYTHON_SUPPORT
    /* These constructor overloads are for easier construction in the Cython-interface.
     * For C++, the FileReader constructor would have been sufficient. */

    explicit
    ParallelGzipReader( int    fileDescriptor,
                        size_t parallelization = 0 ) :
        ParallelGzipReader( std::make_unique<StandardFileReader>( fileDescriptor ), parallelization )
    {}

    explicit
    ParallelGzipReader( const std::string& filePath,
                        size_t             parallelization = 0 ) :
        ParallelGzipReader( std::make_unique<StandardFileReader>( filePath ), parallelization )
    {}

    explicit
    ParallelGzipReader( PyObject* pythonObject,
                        size_t    parallelization = 0 ) :
        ParallelGzipReader( std::make_unique<PythonFileReader>( pythonObject ), parallelization )
    {}
#endif

    /* FileReader overrides */

    [[nodiscard]] FileReader*
    clone() const override
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] int
    fileno() const override
    {
        return m_bitReader.fileno();
    }

    [[nodiscard]] bool
    seekable() const override
    {
        return m_bitReader.seekable();
    }

    void
    close() override
    {
        m_blockFetcher = {};
        m_blockFinder = {};
        m_bitReader.close();
    }

    [[nodiscard]] bool
    closed() const override
    {
        return m_bitReader.closed();
    }

    [[nodiscard]] bool
    eof() const override
    {
        return m_atEndOfFile;
    }

    [[nodiscard]] bool
    fail() const override
    {
        throw std::logic_error( "Not implemented!" );
    }

    [[nodiscard]] size_t
    tell() const override
    {
        if ( m_atEndOfFile ) {
            return size();
        }
        return m_currentPosition;
    }

    [[nodiscard]] size_t
    size() const override
    {
        if ( !m_blockMap->finalized() ) {
            throw std::invalid_argument( "Can't get stream size in BZ2 when not finished reading at least once!" );
        }
        return m_blockMap->back().second;
    }

    void
    clearerr() override
    {
        m_bitReader.clearerr();
        m_atEndOfFile = false;
        throw std::invalid_argument( "Not fully tested!" );
    }

    [[nodiscard]] size_t
    read( char*  outputBuffer,
          size_t nBytesToRead ) override
    {
        return read( -1,  outputBuffer, nBytesToRead );
    }

    /* Simpler file reader interface for Python-interfacing */

    size_t
    read( const int    outputFileDescriptor = -1,
          char* const  outputBuffer = nullptr,
          const size_t nBytesToRead = std::numeric_limits<size_t>::max() )
    {
        if ( closed() ) {
            throw std::invalid_argument( "You may not call read on closed ParallelGzipReader!" );
        }

        if ( eof() || ( nBytesToRead == 0 ) ) {
            return 0;
        }

        size_t nBytesDecoded = 0;
        while ( ( nBytesDecoded < nBytesToRead ) && !eof() ) {
            const auto blockResult = blockFetcher().get( m_currentPosition );
            if ( !blockResult ) {
                m_atEndOfFile = true;
                break;
            }
            const auto& [blockInfo, blockData] = *blockResult;

            if ( !blockData->dataWithMarkers.empty() ) {
                throw std::logic_error( "Did not expect to get results with markers!" );
            }

            /* Copy data from fetched block to output. */

            const auto offsetInBlock = m_currentPosition - blockInfo.decodedOffsetInBytes;
            if ( offsetInBlock >= blockData->size() ) {
                throw std::logic_error( "Block does not contain the requested offset even though it "
                                        "shouldn't be according to block map!" );
            }

            if ( blockData->data.empty() ) {
                throw std::logic_error( "Did not expect empty block. Cannot proceed!" );
            }

            /* Iterate over chunks, first to find offset, then to copy data to output. */
            size_t offsetInChunk{ offsetInBlock };
            for ( const auto& chunk : blockData->data ) {
                if ( nBytesDecoded >= nBytesToRead ) {
                    break;
                }

                if ( offsetInChunk > chunk.size() ) {
                    offsetInChunk -= chunk.size();
                    continue;
                }

                const auto nBytesToDecode = std::min( chunk.size() - offsetInChunk, nBytesToRead - nBytesDecoded );
                const auto nBytesWritten = writeResult(
                    outputFileDescriptor,
                    outputBuffer == nullptr ? nullptr : outputBuffer + nBytesDecoded,
                    reinterpret_cast<const char*>( chunk.data() + offsetInChunk ),
                    nBytesToDecode
                );

                if ( nBytesWritten != nBytesToDecode ) {
                    std::stringstream msg;
                    msg << "Less (" << nBytesWritten << ") than the requested number of bytes (" << nBytesToDecode
                        << ") were written to the output!";
                    throw std::logic_error( std::move( msg ).str() );
                }

                nBytesDecoded += nBytesToDecode;
                m_currentPosition += nBytesToDecode;
                offsetInChunk = 0;
            }
        }

        return nBytesDecoded;
    }

    size_t
    seek( long long int offset,
          int           origin = SEEK_SET ) override
    {
        if ( closed() ) {
            throw std::invalid_argument( "You may not call seek on closed ParallelGzipReader!" );
        }

        switch ( origin )
        {
        case SEEK_CUR:
            offset = tell() + offset;
            break;
        case SEEK_SET:
            break;
        case SEEK_END:
            /* size() requires the block offsets to be available! */
            if ( !m_blockMap->finalized() ) {
                read();
            }
            offset = size() + offset;
            break;
        }

        const auto positiveOffset = static_cast<size_t>( std::max<decltype( offset )>( 0, offset ) );

        if ( positiveOffset == tell() ) {
            return positiveOffset;
        }

        /* Backward seeking is no problem at all! 'tell' may only return <= size()
         * as value meaning we are now < size() and therefore EOF can be cleared! */
        if ( positiveOffset < tell() ) {
            m_atEndOfFile = false;
            m_currentPosition = positiveOffset;
            return positiveOffset;
        }

        /* m_blockMap is only accessed by read and seek, which are not to be called from different threads,
         * so we do not have to lock it. */
        const auto blockInfo = m_blockMap->findDataOffset( positiveOffset );
        if ( positiveOffset < blockInfo.decodedOffsetInBytes ) {
            throw std::logic_error( "Block map returned unwanted block!" );
        }

        if ( blockInfo.contains( positiveOffset ) ) {
            m_atEndOfFile = false;
            m_currentPosition = positiveOffset;
            return tell();
        }

        assert( positiveOffset - blockInfo.decodedOffsetInBytes > blockInfo.decodedSizeInBytes );
        if ( m_blockMap->finalized() ) {
            m_atEndOfFile = true;
            m_currentPosition = size();
            return tell();
        }

        /* Jump to furthest known point as performance optimization. Note that even if that is right after
         * the last byte, i.e., offset == size(), then no eofbit is set even in ifstream! In ifstream you
         * can even seek to after the file end with no fail bits being set in my tests! */
        m_atEndOfFile = false;
        m_currentPosition = blockInfo.decodedOffsetInBytes + blockInfo.decodedSizeInBytes;
        read( -1, nullptr, positiveOffset - tell() );
        return tell();
    }

    /* Block compression specific methods */

    [[nodiscard]] bool
    blockOffsetsComplete() const
    {
        return m_blockMap->finalized();
    }

    /**
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    [[nodiscard]] std::map<size_t, size_t>
    blockOffsets()
    {
        if ( !m_blockMap->finalized() ) {
            read();
            if ( !m_blockMap->finalized() || !blockFinder().finalized() ) {
                throw std::logic_error( "Reading everything should have finalized the block map!" );
            }
        }

        return m_blockMap->blockOffsets();
    }

    [[nodiscard]] GzipIndex
    gzipIndex()
    {
        const auto offsets = blockOffsets();  // Also finalizes reading implicitly.
        if ( offsets.empty() ) {
            return {};
        }

        GzipIndex index;
        index.compressedSizeInBytes = ceilDiv( offsets.rbegin()->first, 8U );
        index.uncompressedSizeInBytes = offsets.rbegin()->second;
        index.windowSizeInBytes = 32ULL * 1024ULL;

        /* Heuristically determine a checkpoint spacing from the existing checkpoints. */
        std::vector<size_t> uncompressedSpacings;
        for ( auto it = offsets.begin(), nit = std::next( offsets.begin() ); nit != offsets.end(); ++it, ++nit ) {
            uncompressedSpacings.push_back( nit->second - it->second );
        }
        index.checkpointSpacing = ceilDiv(
            *std::max_element( uncompressedSpacings.begin(), uncompressedSpacings.end() ),
            32ULL * 1024ULL ) * 32ULL * 1024ULL;

        for ( const auto& [compressedOffsetInBits, uncompressedOffsetInBytes] : offsets ) {
            Checkpoint checkpoint;
            checkpoint.compressedOffsetInBits = compressedOffsetInBits;
            checkpoint.uncompressedOffsetInBytes = uncompressedOffsetInBytes;

            const auto window = m_windowMap->get( compressedOffsetInBits );
            if ( window ) {
                checkpoint.window.assign( window->begin(), window->end() );
            }

            index.checkpoints.emplace_back( std::move( checkpoint ) );
        }

        return index;
    }

    /**
     * Same as @ref blockOffsets but it won't force calculation of all blocks and simply returns
     * what is availabe at call time.
     * @return vectors of block data: offset in file, offset in decoded data
     *         (cumulative size of all prior decoded blocks).
     */
    [[nodiscard]] std::map<size_t, size_t>
    availableBlockOffsets() const
    {
        return m_blockMap->blockOffsets();
    }

private:
    void
    setBlockOffsets( std::map<size_t, size_t> offsets )
    {
        if ( offsets.empty() ) {
            if ( m_blockMap->dataBlockCount() == 0 ) {
                return;
            }
            throw std::invalid_argument( "May not clear offsets. Construct a new ParallelGzipReader instead!" );
        }

        setBlockFinderOffsets( offsets );

        if ( offsets.size() < 2 ) {
            throw std::invalid_argument( "Block offset map must contain at least one valid block and one EOS block!" );
        }
        m_blockMap->setBlockOffsets( std::move( offsets ) );
    }

public:
    void
    setBlockOffsets( const GzipIndex& index )
    {
        if ( index.checkpoints.empty() ) {
            return;
        }

        /* Generate simple compressed to uncompressed offset map from index. */
        std::map<size_t, size_t> newBlockOffsets;
        for ( const auto& checkpoint : index.checkpoints ) {
            newBlockOffsets.emplace( checkpoint.compressedOffsetInBits, checkpoint.uncompressedOffsetInBytes );
        }

        /* Input file-end offset if not included in checkpoints. */
        if ( const auto fileEndOffset = newBlockOffsets.find( index.compressedSizeInBytes * 8 );
             fileEndOffset == newBlockOffsets.end() )
        {
            newBlockOffsets.emplace( index.compressedSizeInBytes * 8, index.uncompressedSizeInBytes );
        } else if ( fileEndOffset->second != index.uncompressedSizeInBytes ) {
            throw std::invalid_argument( "Index has contradicting information for the file end information!" );
        }

        setBlockOffsets( std::move( newBlockOffsets ) );

        /* Copy window data. */
        for ( const auto& checkpoint : index.checkpoints ) {
            /* For some reason, indexed_gzip also stores windows for the very last checkpoint at the end of the file,
             * which is useless because there is nothing thereafter. But, don't filter it here so that exportIndex
             * mirrors importIndex better. */
            m_windowMap->emplace( checkpoint.compressedOffsetInBits, checkpoint.window );
        }
        blockFetcher().clearCache();
    }

#ifdef WITH_PYTHON_SUPPORT
    void
    importIndex( PyObject* pythonObject )
    {
        setBlockOffsets( readGzipIndex( std::make_unique<PythonFileReader>( pythonObject ) ) );
    }

    void
    exportIndex( PyObject* pythonObject )
    {
        const auto file = std::make_unique<PythonFileReader>( pythonObject );
        const auto checkedWrite =
            [&file] ( const void* buffer, size_t size )
            {
                if ( file->write( reinterpret_cast<const char*>( buffer ), size ) != size ) {
                    throw std::runtime_error( "Failed to write data to index!" );
                }
            };

        writeGzipIndex( gzipIndex(), checkedWrite );
    }
#endif

    /**
     * @return number of processed bits of compressed bzip2 input file stream
     * @note Bzip2 is block based and blocks are currently read fully, meaning that the granularity
     *       of the returned position is ~100-900kB. It's only useful for a rough estimate.
     */
    [[nodiscard]] size_t
    tellCompressed() const
    {

        const auto blockInfo = m_blockMap->findDataOffset( m_currentPosition );
        if ( blockInfo.contains( m_currentPosition ) ) {
            return blockInfo.encodedOffsetInBits;
        }
        return 0;
    }


    /**
     * Closes all threads and saves the work. They will be restarted when needed again, e.g., on seek or read.
     * This is intended for use with fusepy. You can start a ParallelGzipReader use it to create the block map
     * and print out user output and then you join all threads before FUSE forks the process. FUSE requires
     * threads to be created after it forks, it seems:
     * @see https://github.com/libfuse/libfuse/wiki/FAQ#how-should-threads-be-started
     * Personally, the only problem I observed was background process not finishing even after unmounting,
     * however, contrary to the FAQ it seems that threads were not joined because the file system seemed to work.
     */
    void
    joinThreads()
    {
        m_blockFetcher.reset();
        m_blockFinder.reset();
    }

private:
    BlockFinder&
    blockFinder()
    {
        if ( m_blockFinder ) {
            return *m_blockFinder;
        }

        if ( !m_startBlockFinder ) {
            throw std::logic_error( "Block finder creator was not initialized correctly!" );
        }

        m_blockFinder = m_startBlockFinder();
        if ( !m_blockFinder ) {
            throw std::logic_error( "Block finder creator failed to create new block finder!" );
        }

        if ( m_blockMap->finalized() ) {
            setBlockFinderOffsets( m_blockMap->blockOffsets() );
        }

        return *m_blockFinder;
    }


    BlockFetcher&
    blockFetcher()
    {
        if ( m_blockFetcher ) {
            return *m_blockFetcher;
        }

        /* As a side effect, blockFinder() creates m_blockFinder if not already initialized! */
        blockFinder();

        m_blockFetcher = std::make_unique<BlockFetcher>( m_bitReader, m_blockFinder, m_blockMap, m_windowMap,
                                                         m_fetcherParallelization );

        if ( !m_blockFetcher ) {
            throw std::logic_error( "Block fetcher should have been initialized!" );
        }

        return *m_blockFetcher;
    }

    void
    setBlockFinderOffsets( const std::map<size_t, size_t>& offsets )
    {
        if ( offsets.empty() ) {
            throw std::invalid_argument( "A non-empty list of block offsets is required!" );
        }

        BlockFinder::BlockOffsets encodedBlockOffsets;
        for ( auto it = offsets.begin(), nit = std::next( offsets.begin() ); nit != offsets.end(); ++it, ++nit )
        {
            /* Ignore blocks with no data, i.e., EOS blocks. */
            if ( it->second != nit->second ) {
                encodedBlockOffsets.push_back( it->first );
            }
        }
        /* The last block is not pushed because "std::next( it )" is end but last block must be EOS anyways
         * or else BlockMap will not work correctly because the implied size of that last block is 0! */

        blockFinder().setBlockOffsets( std::move( encodedBlockOffsets ) );
    }

    size_t
    writeResult( int         const outputFileDescriptor,
                 char*       const outputBuffer,
                 char const* const dataToWrite,
                 size_t      const dataToWriteSize )
    {
        size_t nBytesFlushed = dataToWriteSize; // default then there is neither output buffer nor file device given

        if ( outputFileDescriptor >= 0 ) {
            const auto nBytesWritten = write( outputFileDescriptor, dataToWrite, dataToWriteSize );
            nBytesFlushed = std::max<decltype( nBytesWritten )>( 0, nBytesWritten );
        }

        if ( outputBuffer != nullptr ) {
            std::memcpy( outputBuffer, dataToWrite, nBytesFlushed );
        }

        return nBytesFlushed;
    }

private:
    BitReader m_bitReader;

    size_t m_currentPosition = 0; /**< the current position as can only be modified with read or seek calls. */
    bool m_atEndOfFile = false;

private:
    size_t const m_fetcherParallelization;
    /** The block finder is much faster than the fetcher and therefore does not require es much parallelization! */
    size_t const m_finderParallelization{ ceilDiv( m_fetcherParallelization, 8U ) };

    std::function<std::shared_ptr<BlockFinder>( void )> const m_startBlockFinder;

    /** Necessary for prefetching decoded blocks in parallel. */
    std::shared_ptr<BlockFinder>     m_blockFinder;
    std::shared_ptr<BlockMap>  const m_blockMap{ std::make_shared<BlockMap>() };
    /**
     * The window map should contain windows to all encoded block offsets inside @ref m_blockMap.
     * The windows are stored in a separate map even though all keys should be identical because BlockMap is
     * too "finished". I don't see how to generically and readably add generic user data / windows to it.
     * Furthermore, the windows might potentially be written out-of-order while block offsets should be inserted
     * in order into @ref m_blockMap.
     */
    std::shared_ptr<WindowMap> const m_windowMap{ std::make_shared<WindowMap>() };
    std::unique_ptr<BlockFetcher>    m_blockFetcher;
};
