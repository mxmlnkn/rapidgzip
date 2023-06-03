#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/FileReader.hpp>
#include <ThreadPool.hpp>

#include "../rapidgzip.hpp"
#include "Interface.hpp"


namespace rapidgzip::blockfinder
{
/**
 * @note Does not even give a speedup of two! ~2300 MB/s vs. 1450 MB/s ... even when using /dev/shm!?
 *       I also tried varying the buffer size but to no avail.
 * @deprecated Use blockfinder::PigzStringView instead because it achieves more than 8 GB/s!
 *
 * 12 threads:
 *     [findPigzBlocks] Trying to find block bit offsets in 389 MiB of data took 0.170229 s => 2399.3 MB/s
 *     [BlockFetcher::~BlockFetcher]
 *        Time spent in:
 *            refillBuffer                   : 0.0969651 s
 *            time spent waiting for futures : 0.0607085 s
 *     Found 2115 pigz block candidates!
 *        128 2393184 5584424 7977624 8775336 17550072 23932064 24729848 26325312 29516064 ...
 *
 * 2 threads:
 *     [findPigzBlocks] Trying to find block bit offsets in 389 MiB of data took 0.283223 s => 1442.08 MB/s
 *        Time spent in:
 *            refillBuffer                   : 0.117154 s
 *            time spent waiting for futures : 0.153178 s
 *
 * 1 threads:
 *     [findPigzBlocks] Trying to find block bit offsets in 389 MiB of data took 0.318255 s => 1283.34 MB/s
 *     [BlockFetcher::~BlockFetcher]
 *        Time spent in:
 *            refillBuffer                   : 0.109558 s
 *            time spent waiting for futures : 0.196396 s
 *
 * -> For 12 threads, more than half of the time is spend in refilling the buffer!
 *    I might be able to squeeze out that last bit of performance by overlapping refilling with searching
 *    in a kind of double-buffering. But the critical path would still be the refilling, i.e., the 0.097s, i.e.,
 *    even with this the expected bandwidth would "only" be ~4200 MB/s. This does not even sound like full memcpy
 *    speed (which I would have expected to be ~10 GB/s).
 *    I might have to parallelize the call to std::fread basically :/... ... Normally, that should speed up things
 *    much but on /dev/shm it might work. Plus, it might improve locality because each thread reads their own data
 *    which might move them into L1 cache of that particular core!
 *  -> Maybe that buffer -> subBuffer partitioning approach is just wrong. Just use a SharedFileReader and let
 *     each thread load its own buffer. Of course the shared file reader might serialize the reading anyway...
 *     I would need to clone/reopen the FileReader for each thread if it is supported.
 *
 *  m && time tools/rapidgzip -c -P 24 -v -o /dev/shm/small.bgz.decoded -f /dev/shm/small.pigz
 *
 *      Finalized block map with 2116 blocks
 *      BlockFinder found 2115 block candidates
 *      [23:53:13.804][140501916813120] [BlockFetcher::~BlockFetcher]
 *         Cache hits                 : 1857
 *         misses                     : 3
 *         prefetched blocks          : 2125
 *         direct prefetch queue hits : 255
 *         hit rate                   : 0.998582
 *         time spent in:
 *             bzip2::readBlockData          : 0 s
 *             time spent in decodeBlock     : 9.71707 s
 *             time spent waiting on futures : 0.678193 s
 *         prefetch fail because of BlockFinder : 300
 *      [BlockFetcher::~BlockFetcher]
 *         Time spent in:
 *             refillBuffer                   : 0.114353 s      -> almost 90% spent in refilling. WTF!? on /dev/shm
 *             time spent waiting for futures : 0.0148126 s     -> probably only so much lower because some of the
 *                                                                 missing time is spent waiting for decoding futures!
 *      real	0m1.020s                                        -> If the refilling was in the background, then it could
 *      user	0m7.970s                                           also overlap decompression and could effectively
 *      sys	0m0.627s                                               speed up a lot beause it is not the critical path!
 *
 */
class PigzParallel final :
    public Interface
{
public:
    using DeflateBlock = rapidgzip::deflate::Block<>;

    /**
     * Should probably be larger than the I/O block size of 4096 B and smaller than most L1 cache sizes.
     * Not fitting into L1 cache isn't as bad as thought but increasing the size past 16 kiB also does not improve
     * the timings anymore on my Ryzen 3900X.
     */
    static constexpr size_t BUFFER_SIZE = 16_Mi;
    static constexpr uint8_t MAGIC_BIT_STRING_SIZE = 35;
    static constexpr uint8_t MAGIC_BYTE_STRING_SIZE = 5;

public:
    explicit
    PigzParallel( UniqueFileReader fileReader ) :
        m_fileReader( std::move( fileReader ) )
    {}

    #if 1
    ~PigzParallel()
    {
        std::cerr << "[BlockFetcher::~BlockFetcher]"
                  << "\n   Time spent in:"
                  << "\n       refillBuffer                   : " << m_refillDuration << " s"
                  << "\n       time spent waiting for futures : " << m_futureWaitDuration << " s"
                  << "\n";
    }
    #endif

    void
    refillBuffer()
    {
        struct MeasureTime
        {
            explicit MeasureTime( double& result ) : m_result( result ) {}
            ~MeasureTime() { m_result += duration( t0, now() ); }
            const decltype( now() ) t0 = now();
            double& m_result;
        } measureTime{ m_refillDuration };

        if ( m_fileReader->eof() ) {
            m_buffer.clear();
            return;
        }

        if ( m_buffer.empty() ) {
            m_buffer.resize( BUFFER_SIZE );
            m_buffer.resize( m_fileReader->read( m_buffer.data(), m_buffer.size() ) );
            return;
        }

        /* We need to retain one more byte because we are searching from the point of view
         * of the block offset after the magic bit string. Normally, it would be enough to retain one byte less
         * the number of bytes we search through. */
        const size_t nBytesToRetain{ MAGIC_BYTE_STRING_SIZE };
        if ( m_buffer.size() <= nBytesToRetain ) {
            throw std::logic_error( "Buffer should always contain more contents than the search length or be empty!" );
        }

        /* Move bytes to front to account for string matches over buffer boundaries. */
        for ( size_t i = 0; i < nBytesToRetain; ++i ) {
            m_buffer[i] = m_buffer[i + ( m_buffer.size() - nBytesToRetain )];
        }

        m_buffer.resize( BUFFER_SIZE );
        const auto nBytesRead = m_fileReader->read( m_buffer.data() + nBytesToRetain,
                                                    m_buffer.size() - nBytesToRetain );
        m_buffer.resize( nBytesRead + nBytesToRetain );
    }

    /**
     * @return offset of deflate block in bits (not the gzip stream offset!).
     */
    [[nodiscard]] size_t
    find() override
    {
        /* The flush markers will be AFTER deflate blocks, meaning the very first deflate block needs special
         * treatment to not be ignored. */
        if ( m_lastBlockOffsetReturned == 0 ) {
            #if 0
            /**
             * @todo This requires the buffer to be larger than the first gzip header may be.
             * Theoretically, the user could store arbitrary amount of data in the zero-terminated file name
             * and file comment ... */
            rapidgzip::BitReader bitReader( m_fileReader->clone() );

            #else

            refillBuffer();
            distributeWork();
            rapidgzip::BitReader bitReader( std::make_unique<BufferedFileReader>( m_buffer ) );
            #endif

            auto error = rapidgzip::gzip::checkHeader( bitReader );
            if ( error != rapidgzip::Error::NONE ) {
                throw std::invalid_argument( "Corrupted deflate stream in gzip file!" );
            }
            m_lastBlockOffsetReturned = bitReader.tell();

            DeflateBlock block;
            error = block.readHeader( bitReader );
            if ( error != rapidgzip::Error::NONE ) {
                throw std::invalid_argument( "Corrupted deflate stream in gzip file!" );
            }

            if ( ( block.compressionType() != DeflateBlock::CompressionType::UNCOMPRESSED )
                 || block.isLastBlock()
                 || ( block.uncompressedSize() > 0 ) )
            {
                return m_lastBlockOffsetReturned;
            }
        }

        while ( !m_blockOffsets.empty() || !m_threadResults.empty() || !m_fileReader->eof() ) {
            /* Start new futures if we are out of results. */
            if ( m_threadResults.empty() ) {
                refillBuffer();
                distributeWork();
            }

            /* Wait on futures until one returns with a result. */
            while ( m_blockOffsets.empty() && !m_threadResults.empty() ) {
                const auto t0 = now();

                m_blockOffsets = m_threadResults.front().get();
                m_threadResults.pop_front();

                m_futureWaitDuration += duration( t0, now() );
            }

            /* Try to return the next offset but check against duplicates. */
            if ( !m_blockOffsets.empty() ) {
                const auto offset = m_blockOffsets.front();
                m_blockOffsets.pop_front();
                if ( offset != m_lastBlockOffsetReturned ) {
                    m_lastBlockOffsetReturned = offset;
                    return offset;
                }
            }
        }

        m_lastBlockOffsetReturned = std::numeric_limits<size_t>::max();
        return std::numeric_limits<size_t>::max();
    }

    void
    distributeWork()
    {
        /* We need to retain one more byte because we are searching from the point of view
         * of the block offset after the magic bit string. Normally, it would be enough to retain one byte less
         * the number of bytes we search through. */
        const size_t nBytesToRetain{ MAGIC_BYTE_STRING_SIZE };

        /** @todo splitting a buffer into sub-buffers with certain amount of halos really is something
         *        which could be generalized and tested more rigorously. */

        const auto minSubBufferSizeInBytes = std::max<size_t>( nBytesToRetain, 4096 );
        size_t subBufferStrideInBytes = m_buffer.size();
        for ( size_t i = 2; i <= m_threadPool.capacity(); ++i ) {
            const auto candidateSubBufferStride = ceilDiv( m_buffer.size(), i );
            if ( candidateSubBufferStride >= minSubBufferSizeInBytes ) {
                subBufferStrideInBytes = candidateSubBufferStride;
            } else {
                break;
            }
        }

        for ( size_t offset = 0; offset < m_buffer.size(); offset += subBufferStrideInBytes ) {
            VectorView<char> subBuffer( m_buffer.data() + offset,
                                        std::min( subBufferStrideInBytes + nBytesToRetain,
                                                  m_buffer.size() - offset ) );
            const auto byteOffset = m_fileReader->tell() - m_buffer.size() + offset;

            assert( subBuffer.size() > nBytesToRetain );
            m_threadResults.emplace_back(
                m_threadPool.submit(
                    [=] () { return workerMain( subBuffer, byteOffset ); } ) );
        }
    }

private:
    static std::deque<size_t>
    workerMain( VectorView<char> buffer,
                size_t           byteOffset )
    {
        std::deque<size_t> blockOffsets;

        for ( size_t blockCandidate = MAGIC_BYTE_STRING_SIZE; blockCandidate < buffer.size(); ++blockCandidate ) {
            /**
             * Pigz produces stored blocks of size 0 maybe because it uses zlib stream flush or something like that.
             * The stored deflate block consists of:
             *  - 3 zero bits to indicate non-final and non-compressed 0b00 blocks
             *  - 0-7 bits for padding to byte boundaries
             *  - 2x 16-bit numbers for the size and bit-complement / bit-negated size, which here is 0 and 0xFFFF.
             * This gives a 35 bit-string to search for and one which furthermore has rather low entropy and
             * therefore is unlikely to appear in gzip-compressed data!
             * In random data, the 2^35 bits would result in one match / false positive every 32GiB.
             */
            if (    ( buffer[blockCandidate-1] == (char)0xFF )
                 && ( buffer[blockCandidate-2] == (char)0xFF )
                 && ( buffer[blockCandidate-3] == 0 )
                 && ( buffer[blockCandidate-4] == 0 )
                 /* Note that this check only works if the padding is filled with zeros. */
                 && ( ( static_cast<uint8_t>( buffer[blockCandidate-5] ) & 0b1110'0000 ) == 0 ) ) {
                blockOffsets.push_back( ( byteOffset + blockCandidate ) * CHAR_BIT );
            }
        }

        return blockOffsets;
    }

private:
    const UniqueFileReader m_fileReader;
    BufferedFileReader::AlignedBuffer m_buffer;
    size_t m_lastBlockOffsetReturned{ 0 };  /**< absolute offset in bits */

    ThreadPool m_threadPool{ 12 };  /**< @todo expose this configuration parameter */
    std::deque<std::future<std::deque<size_t> > > m_threadResults;
    std::deque<size_t> m_blockOffsets;

    mutable double m_refillDuration{ 0 };
    mutable double m_futureWaitDuration{ 0 };
};
}  // rapidgzip::blockfinder
