#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/FileReader.hpp>
#include <gzip.hpp>

#include "Interface.hpp"


namespace pragzip::blockfinder
{
/**
 * @note Tops out at 1-1.5 GiB/s, which might bottleneck decompression with ~12 cores for
 *       my decoder (~90 MB/s) and ~6 cores for zlib decompression (~200 MB/s).
 * @deprecated Use blockfinder::PigzStringView instead because it achieves more than 8 GB/s!
 */
class PigzNaive :
    public Interface
{
public:
    /**
     * Should probably be larger than the I/O block size of 4096 B and smaller than most L1 cache sizes.
     * Not fitting into L1 cache isn't as bad as thought but increasing the size past 16 kiB also does not improve
     * the timings anymore on my Ryzen 3900X.
     */
    static constexpr size_t BUFFER_SIZE = 16*1024;
    static constexpr uint8_t MAGIC_BIT_STRING_SIZE = 35;

public:
    explicit
    PigzNaive( std::unique_ptr<FileReader> fileReader ) :
        m_fileReader( std::move( fileReader ) )
    {}

    void
    refillBuffer()
    {
        m_blockCandidate = ceilDiv( MAGIC_BIT_STRING_SIZE, CHAR_BIT );

        if ( m_fileReader->eof() ) {
            m_bufferSize = 0;
            return;
        }

        if ( m_bufferSize == 0 ) {
            m_bufferSize = m_fileReader->read( m_buffer.data(), BUFFER_SIZE );
            return;
        }

        /* We need to retain one more byte because we are searching from the point of view
         * of the block offset after the magic bit string. */
        const auto nBytesToRetain = static_cast<size_t>( ceilDiv( MAGIC_BIT_STRING_SIZE + CHAR_BIT, CHAR_BIT ) - 1 );
        if ( m_bufferSize <= nBytesToRetain ) {
            throw std::logic_error( "Buffer should always contain more contents than the search length or be empty!" );
        }

        /* Move bytes to front to account for string matches over buffer boundaries. */
        for ( size_t i = 0; i < nBytesToRetain; ++i ) {
            m_buffer[i] = m_buffer[i + ( m_bufferSize - nBytesToRetain )];
        }

        const auto nBytesRead = m_fileReader->read( m_buffer.data() + nBytesToRetain,
                                                    BUFFER_SIZE - nBytesToRetain );
        m_bufferSize = nBytesRead + nBytesToRetain;
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
            pragzip::BitReader bitReader( m_fileReader->clone() );

            #else

            refillBuffer();
            pragzip::BitReader bitReader(
                std::make_unique<BufferedFileReader>(
                    BufferedFileReader::AlignedBuffer( m_buffer.data(), m_buffer.data() + m_bufferSize )
                )
            );
            #endif

            pragzip::gzip::checkHeader( bitReader );
            m_lastBlockOffsetReturned = bitReader.tell();
            m_blockCandidate = m_lastBlockOffsetReturned / CHAR_BIT;
            return m_lastBlockOffsetReturned;
        }

        /** @note This method would be much easier with C++20 co_yield but not so sure about the overhead. */
        while ( ( m_bufferSize > 0 ) || !m_fileReader->eof() ) {
            /* "i" is basically represents the offset of the candidate match.
             * Of course it should not be after the buffer end! */
            for ( ; m_blockCandidate < m_bufferSize; ++m_blockCandidate ) {
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
                if (    ( m_buffer[m_blockCandidate-1] == (char)0xFF )
                     && ( m_buffer[m_blockCandidate-2] == (char)0xFF )
                     && ( m_buffer[m_blockCandidate-3] == 0 )
                     && ( m_buffer[m_blockCandidate-4] == 0 )
                     /* Note that this check only works if the padding is filled with zeros. */
                     && ( ( static_cast<uint8_t>( m_buffer[m_blockCandidate-5] ) & 0b1110'0000 ) == 0 ) ) {
                    const auto offset = ( m_fileReader->tell() - m_bufferSize + m_blockCandidate ) * CHAR_BIT;
                    if ( offset == m_lastBlockOffsetReturned ) {
                        continue;
                    }
                    m_lastBlockOffsetReturned = offset;
                    return offset;

                 #if 0
                    /* In my 512MiB test with only compressed blocks, this did not filter any false positives because
                     * there were none. Plus, for uncompressed blocks containing gzip files, it might also not filter
                     * false positives because it is a valid gzip file meaning the next block will likely also be valid.
                     * The only way to detect these is by checking against "overlapping" found blocks while decoding.
                     * This additional check reduces speed from ~1350 MiB/s down to ~750 MiB/s. It seems not worth it! */
                    m_bitReader.seek( offset );
                    try
                    {
                        auto error = block.readHeader( m_bitReader );
                        if ( error == pragzip::Error::NONE ) {
                            blockOffsets.push_back( offset );
                        }
                    } catch ( const std::exception& exception ) {
                        /* Should only happen when we reach the end of the file! */
                        std::cerr << "Caught exception: " << exception.what() << "\n";
                    }
                #endif
                }
            }

            refillBuffer();
        }

        m_lastBlockOffsetReturned = std::numeric_limits<size_t>::max();
        return std::numeric_limits<size_t>::max();
    }

private:
    const std::unique_ptr<FileReader> m_fileReader;
    alignas(64) std::array<char, BUFFER_SIZE> m_buffer;
    size_t m_bufferSize{ 0 };
    size_t m_lastBlockOffsetReturned{ 0 };  /**< absolute offset in bits */
    size_t m_blockCandidate{ 0 };  /**< relative offset in bytes */
};
}  // pragzip::blockfinder
