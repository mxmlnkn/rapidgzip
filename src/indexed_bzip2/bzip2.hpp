/**
 * Modified version of bzcat.c part of toybox commit 7bf68329eb3b
 * by Rob Landley released under SPDX-0BSD license.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <BitReader.hpp>


namespace bzip2
{
static constexpr int CRC32_LOOKUP_TABLE_SIZE = 256;

inline std::array<uint32_t, CRC32_LOOKUP_TABLE_SIZE>
createCRC32LookupTable( bool littleEndian = false )
{
    std::array<uint32_t, CRC32_LOOKUP_TABLE_SIZE> table;
    for ( uint32_t i = 0; i < table.size(); ++i ) {
        uint32_t c = littleEndian ? i : i << 24U;
        for ( int j = 0; j < 8; ++j ) {
            if ( littleEndian ) {
                c = ( c & 1 ) ? ( c >> 1U ) ^ 0xEDB88320 : c >> 1U;
            } else {
                c = ( c & 0x80000000 ) ? ( c << 1U ) ^ 0x04c11db7 : ( c << 1U );
            }
        }
        table[i] = c;
    }
    return table;
}


/* a small lookup table: raw data -> CRC32 value to speed up CRC calculation */
static const std::array<uint32_t, CRC32_LOOKUP_TABLE_SIZE> CRC32_TABLE = createCRC32LookupTable();


/* Constants for huffman coding */
static constexpr int MAX_GROUPS = 6;
static constexpr int GROUP_SIZE = 50;       /* 64 would have been more efficient */
static constexpr int MAX_HUFCODE_BITS = 20; /* Longest huffman code allowed */
static constexpr int MAX_SYMBOLS = 258;     /* 256 literals + RUNA + RUNB */
static constexpr int SYMBOL_RUNA = 0;
static constexpr int SYMBOL_RUNB = 1;


constexpr auto MAGIC_BITS_BLOCK = 0x314159265359ULL;  /* bcd(pi) */
constexpr auto MAGIC_BITS_EOS = 0x177245385090ULL;  /* bcd(sqrt(pi)) */
constexpr auto MAGIC_BITS_SIZE = 48;
constexpr std::string_view MAGIC_BYTES_BZ2 = "BZh";

using BitReader = ::BitReader<true, uint64_t>;


/**
 * @return 1..9 representing the bzip2 block size of 100k to 900k
 */
inline uint8_t
readBzip2Header( BitReader& bitReader )
{
    for ( const auto magicByte : MAGIC_BYTES_BZ2 ) {
        const auto readByte = static_cast<char>( static_cast<uint8_t>( bitReader.read<8>() ) );
        if ( readByte != magicByte ) {
            std::stringstream msg;
            msg << "Input header is not BZip2 magic string 'BZh' (0x"
                << std::hex << int('B') << int('Z') << int('h') << std::dec << "). Mismatch at bit position "
                << bitReader.tell() - 8 << " with " << readByte << " (0x" << std::hex
                /* first cast to unsigned of same length so that the hex representation makes sense. */
                << static_cast<int>( static_cast<uint8_t>( readByte ) )
                << ") should be " << magicByte;
            throw std::domain_error( std::move( msg ).str() );
        }
    }

    // Next byte ASCII '1'-'9', indicates block size in units of 100k of
    // uncompressed data. Allocate intermediate buffer for block.
    const auto i = static_cast<char>( static_cast<uint8_t>( bitReader.read<8>() ) );
    if ( ( i < '1' ) || ( i > '9' ) ) {
        std::stringstream msg;
        msg << "Blocksize must be one of '0' (" << std::hex << static_cast<int>( '0' ) << ") ... '9' ("
            << static_cast<int>( '9' ) << ") but is " << i << " (" << static_cast<int>( i ) << ")";
        throw std::domain_error( std::move( msg ).str() );
    }

    return static_cast<uint8_t>( i - '0' );
}


// This is what we know about each huffman coding group
struct GroupData
{
    std::array<int, MAX_HUFCODE_BITS + 1> limit;
    std::array<int, MAX_HUFCODE_BITS> base;
    std::array<uint16_t, MAX_SYMBOLS> permute;
    uint8_t minLen;
    uint8_t maxLen;
};


struct Block
{
public:
    Block() = default;

    /** Better don't allow copies because the bitreader would be shared, which might be problematic */
    Block( const Block& ) = delete;

    Block&
    operator=( const Block& ) = delete;

    Block( Block&& ) = default;

    Block&
    operator=( Block&& ) = default;

    explicit
    Block( BitReader& bitReader ) :
        m_bitReader( &bitReader )
    {
        readBlockHeader();
    }

    /**
     * First pass, read block's symbols into dbuf[dbufCount].
     *
     * This undoes three types of compression: huffman coding, run length encoding,
     * and move to front encoding.  We have to undo all those to know when we've
     * read enough input.
     */
    void
    readBlockData();

    [[nodiscard]] bool
    eos() const
    {
        return m_atEndOfStream;
    }

    [[nodiscard]] bool
    eof() const
    {
        return m_atEndOfFile;
    }

    [[nodiscard]] BitReader&
    bitReader()
    {
        if ( m_bitReader != nullptr ) {
            return *m_bitReader;
        }
        throw std::invalid_argument( "Block has not been initialized yet!" );
    }

private:
    template<uint8_t nBits>
    [[nodiscard]] uint32_t
    getBits()
    {
        return static_cast<uint32_t>( bitReader().read<nBits>() );
    }

    [[nodiscard]] uint32_t
    getBits( uint8_t nBits )
    {
        return static_cast<uint32_t>( bitReader().read( nBits ) );
    }

    void
    readBlockHeader();

public:
    struct BurrowsWheelerTransformData
    {
    public:
        void
        prepare();

        /**
         * Currently, the logic is limited and might write up to nMaxBytesToDecode + 255 characters
         * to the output buffer! Currently, the caller has to ensure that the output buffer is large enough.
         */
        [[nodiscard]] size_t
        decodeBlock( const size_t nMaxBytesToDecode,
                     char*        outputBuffer );

    public:
        uint32_t origPtr = 0;
        std::array<int, 256> byteCount;

        /* These variables are saved when interrupting decode and are required for resuming */
        int writePos = 0;
        int writeRun = 0;
        int writeCount = 0;
        int writeCurrent = 0;

        uint32_t dataCRC = 0xFFFFFFFFL;  /* CRC of block as calculated by us */
        uint32_t headerCRC = 0;  /* what the block data CRC should be */

        /* simply allocate the maximum of 900kB for the internal block size so we won't run into problem when
         * block sizes changes (e.g. in pbzip2 file). 900kB is nothing in today's age anyways. */
        std::vector<uint32_t> dbuf = std::vector<uint32_t>( 900000, 0 );
    };

public:
    uint64_t magicBytes;
    bool isRandomized = false;

    /* First pass decompression data (Huffman and MTF decoding) */

    /**
     * mapping table: if some byte values are never used (encoding things
     * like ascii text), the compression code removes the gaps to have fewer
     * symbols to deal with, and writes a sparse bitfield indicating which
     * values were present.  We make a translation table to convert the symbols
     * back to the corresponding bytes.
     */
    std::array<uint8_t, 256> symbolToByte;
    std::array<uint8_t, 256> mtfSymbol;
    unsigned int symbolCount;
    /**
     * Every GROUP_SIZE many symbols we switch huffman coding tables.
     * Each group has a selector, which is an index into the huffman coding table arrays.
     *
     * Read in the group selector array, which is stored as MTF encoded
     * bit runs.  (MTF = Move To Front.  Every time a symbol occurs it's moved
     * to the front of the table, so it has a shorter encoding next time.)
     */
    uint16_t selectors_used;

    std::array<char, 32768> selectors;        // nSelectors=15 bits
    std::array<GroupData, MAX_GROUPS> groups; // huffman coding tables
    int groupCount = 0;

    /* Second pass decompression data (burrows-wheeler transform) */
    BurrowsWheelerTransformData bwdata;

    size_t encodedOffsetInBits = 0;
    size_t encodedSizeInBits = 0;

private:
    BitReader* m_bitReader = nullptr;
    bool m_atEndOfStream = false;
    bool m_atEndOfFile = false;
};


/* Read block header at start of a new compressed data block.  Consists of:
 *
 * 48 bits : Block signature, either pi (data block) or e (EOF block).
 * 32 bits : bw->headerCRC
 * 1  bit  : obsolete feature flag.
 * 24 bits : origPtr (Burrows-wheeler unwind index, only 20 bits ever used)
 * 16 bits : Mapping table index.
 *[16 bits]: symToByte[symTotal] (Mapping table.  For each bit set in mapping
 *           table index above, read another 16 bits of mapping table data.
 *           If correspondig bit is unset, all bits in that mapping table
 *           section are 0.)
 *  3 bits : groupCount (how many huffman tables used to encode, anywhere
 *           from 2 to MAX_GROUPS)
 * variable: hufGroup[groupCount] (MTF encoded huffman table data.)
 */
inline void
Block::readBlockHeader()
{
    encodedOffsetInBits = bitReader().tell();
    encodedSizeInBits = 0;

    magicBytes = ( (uint64_t)getBits<24>() << 24U ) | (uint64_t)getBits<24>();
    bwdata.headerCRC = getBits( 32 );
    m_atEndOfStream = magicBytes == MAGIC_BITS_EOS;
    if ( m_atEndOfStream ) {
        /* read byte padding bits */
        const auto nBitsInByte = static_cast<uint8_t>( bitReader().tell() & 7LLU );
        if ( nBitsInByte > 0 ) {
            bitReader().read( uint8_t( 8 ) - nBitsInByte );
        }

        encodedSizeInBits = bitReader().tell() - encodedOffsetInBits;
        m_atEndOfFile = bitReader().eof();
        return;
    }

    if ( magicBytes != MAGIC_BITS_BLOCK ) {
        std::stringstream msg;
        msg << "[BZip2 block header] invalid compressed magic 0x" << std::hex << magicBytes
            << " at offset " << formatBits( encodedOffsetInBits );
        throw std::domain_error( std::move( msg ).str() );
    }

    isRandomized = getBits<1>();
    if ( isRandomized ) {
        throw std::domain_error( "[BZip2 block header] deprecated isRandomized bit is not supported" );
    }

    if ( ( bwdata.origPtr = getBits<24>() ) > bwdata.dbuf.size() ) {
        std::stringstream msg;
        msg << "[BZip2 block header] origPtr " << bwdata.origPtr << " is larger than buffer size: "
            << bwdata.dbuf.size();
        throw std::logic_error( std::move( msg ).str() );
    }

    /**
     * The Mapping table itself is compressed in two parts:
     * huffmanUsedMap: each bit indicates whether the corresponding range [0...15], [16...31] is present.
     * huffman_used_bitmaps: 0-16 16-bit bitmaps
     * The Huffman map gives 0, 10, 11, 100, 101, ... (8-bit) symbols
     * Instead of storing 2 * 256 bytes ( 0b : A, 10b : B, .. ) for the table, the first part is left out.
     * And for short maps, only the first n-th are actually stored.
     * The second half is also assumed to be ordered, so that we only need to store which symbols are actually
     * present.
     * This however means that the Huffmann table can't be frequency sorted, therefore this is done in a
     * second step / table, the mtfSymbol (move to front) map.
     * This would need 256 bits to store the table in huffman_used_bitmaps.
     * These bits are split in groups of 16 and the presence of each group is encoded in huffmanUsedMap
     * to save even more bytes.
     * @verbatim
     *  10001000 00000000     # huffmanUsedMap (bit map)
     *  ^   ^
     *  |   [64,95]
     *  [0...15]
     *  00000000 00100000     # huffman_used_bitmaps[0]
     *  ^          ^    ^
     *  0          10   15
     *          (newline)
     *  00000100 10001001     # huffman_used_bitmaps[1]
     *  ^    ^   ^   ^  ^
     *  64   69  72  76 95
     *       E   H   L  O
     * @endverbatim
     */
    // mapping table: if some byte values are never used (encoding things
    // like ascii text), the compression code removes the gaps to have fewer
    // symbols to deal with, and writes a sparse bitfield indicating which
    // values were present.  We make a translation table to convert the symbols
    // back to the corresponding bytes.
    {
        const uint16_t huffmanUsedMap = getBits<16>();
        symbolCount = 0;
        for ( int i = 0; i < 16; i++ ) {
            if ( huffmanUsedMap & ( 1 << ( 15 - i ) ) ) {
                const auto bitmap = getBits<16>();
                for ( int j = 0; j < 16; j++ ) {
                    if ( bitmap & ( 1 << ( 15 - j ) ) ) {
                        symbolToByte[symbolCount++] = ( 16 * i ) + j;
                    }
                }
            }
        }
    }

    // How many different huffman coding groups does this block use?
    groupCount = getBits<3>();
    if ( ( groupCount < 2 ) || ( groupCount > MAX_GROUPS ) ) {
        std::stringstream msg;
        msg << "[BZip2 block header] Invalid Huffman coding group count " << groupCount;
        throw std::logic_error( std::move( msg ).str() );
    }

    // nSelectors: Every GROUP_SIZE many symbols we switch huffman coding
    // tables.  Each group has a selector, which is an index into the huffman
    // coding table arrays.
    //
    // Read in the group selector array, which is stored as MTF encoded
    // bit runs.  (MTF = Move To Front.  Every time a symbol occurs it's moved
    // to the front of the table, so it has a shorter encoding next time.)
    if ( !( selectors_used = getBits<15>() ) ) {
        std::stringstream msg;
        msg << "[BZip2 block header] selectors_used " << selectors_used << " is invalid";
        throw std::logic_error( std::move( msg ).str() );
    }
    for ( int i = 0; i < groupCount; i++ ) {
        mtfSymbol[i] = i;
    }
    for ( int i = 0; i < selectors_used; i++ ) {
        int j = 0;
        for ( ; getBits<1>(); j++ ) {
            if ( j >= groupCount ) {
                std::stringstream msg;
                msg << "[BZip2 block header] Could not find zero termination after " << groupCount << " bits";
                throw std::domain_error( std::move( msg ).str() );
            }
        }

        // Decode MTF to get the next selector, and move it to the front.
        const auto uc = mtfSymbol[j];
        memmove( mtfSymbol.data() + 1, mtfSymbol.data(), j );
        mtfSymbol[0] = selectors[i] = uc;
    }

    // Read the huffman coding tables for each group, which code for symTotal
    // literal symbols, plus two run symbols (RUNA, RUNB)
    const auto symCount = symbolCount + 2;
    for ( int j = 0; j < groupCount; j++ ) {
        // Read lengths
        std::array<uint8_t, MAX_SYMBOLS> length;
        unsigned int hh = getBits<5>();
        for ( unsigned int i = 0; i < symCount; i++ ) {
            while ( true ) {
                // !hh || hh > MAX_HUFCODE_BITS in one test.
                if ( MAX_HUFCODE_BITS - 1 < hh - 1 ) {
                    std::stringstream msg;
                    msg << "[BZip2 block header]  start_huffman_length " << hh
                        << " is larger than " << MAX_HUFCODE_BITS << " or zero\n";
                    throw std::logic_error( std::move( msg ).str() );
                }

                // Stop if first bit is 0, otherwise second bit says whether to
                // increment or decrement.
                if ( getBits<1>() != 0 ) {
                    hh += 1 - ( getBits<1>() << 1U );
                } else {
                    break;
                }
            }
            if ( hh > std::numeric_limits<uint8_t>::max() ) {
                std::stringstream msg;
                msg << "[BZip2 block header] The read length is unexpectedly large: " << hh;
                throw std::logic_error( std::move( msg ).str() );
            }
            length[i] = static_cast<uint8_t>( hh );
        }

        /* Calculate permute[], base[], and limit[] tables from length[].
         *
         * permute[] is the lookup table for converting huffman coded symbols
         * into decoded symbols.  It contains symbol values sorted by length.
         *
         * base[] is the amount to subtract from the value of a huffman symbol
         * of a given length when using permute[].
         *
         * limit[] indicates the largest numerical value a symbol with a given
         * number of bits can have.  It lets us know when to stop reading.
         *
         * To use these, keep reading bits until value <= limit[bitcount] or
         * you've read over 20 bits (error).  Then the decoded symbol
         * equals permute[hufcode_value - base[hufcode_bitcount]].
         */
        const auto hufGroup = &groups[j];
        hufGroup->minLen = *std::min_element( length.begin(), length.begin() + symCount );
        hufGroup->maxLen = *std::max_element( length.begin(), length.begin() + symCount );

        // Note that minLen can't be smaller than 1, so we adjust the base
        // and limit array pointers so we're not always wasting the first
        // entry.  We do this again when using them (during symbol decoding).
        const auto base = hufGroup->base.data() - 1;
        const auto limit = hufGroup->limit.data() - 1;

        // zero temp[] and limit[], and calculate permute[]
        int pp = 0;
        std::array<unsigned int, MAX_HUFCODE_BITS + 1> temp;
        for ( int i = hufGroup->minLen; i <= hufGroup->maxLen; i++ ) {
            temp[i] = limit[i] = 0;
            for ( hh = 0; hh < symCount; hh++ ) {
                if ( length[hh] == i ) {
                    hufGroup->permute[pp++] = hh;
                }
            }
        }

        // Count symbols coded for at each bit length
        for ( unsigned int i = 0; i < symCount; i++ ) {
            temp[length[i]]++;
        }

        /* Calculate limit[] (the largest symbol-coding value at each bit
         * length, which is (previous limit<<1)+symbols at this level), and
         * base[] (number of symbols to ignore at each bit length, which is
         * limit minus the cumulative count of symbols coded for already). */
        pp = hh = 0;
        for ( int i = hufGroup->minLen; i < hufGroup->maxLen; i++ ) {
            pp += temp[i];
            limit[i] = pp - 1;
            pp <<= 1;
            base[i + 1] = pp - ( hh += temp[i] );
        }
        limit[hufGroup->maxLen] = pp + temp[hufGroup->maxLen] - 1;
        limit[hufGroup->maxLen + 1] = std::numeric_limits<int>::max();
        base[hufGroup->minLen] = 0;
    }
}


inline void
Block::readBlockData()
{
    const GroupData* hufGroup = nullptr;

    // We've finished reading and digesting the block.  Now read this
    // block's huffman coded symbols from the file and undo the huffman coding
    // and run length encoding, saving the result into dbuf[dbufCount++] = uc

    bwdata.byteCount.fill( 0 );
    std::iota( mtfSymbol.begin(), mtfSymbol.end(), 0 );

    // Loop through compressed symbols.  This is the first "tight inner loop"
    // that needs to be micro-optimized for speed.  (This one fills out dbuf[]
    // linearly, staying in cache more, so isn't as limited by DRAM access.)
    const int* base = nullptr;
    const int* limit = nullptr;
    uint32_t dbufCount = 0;
    for ( int ii, jj, hh = 0, runPos = 0, symCount = 0, selector = 0; ; ) {
        // Have we reached the end of this huffman group?
        if ( !( symCount-- ) ) {
            // Determine which huffman coding group to use.
            symCount = GROUP_SIZE - 1;
            if ( selector >= selectors_used ) {
                std::stringstream msg;
                msg << "[BZip2 block data] selector " << selector << " out of maximum range " << selectors_used;
                throw std::domain_error( std::move( msg ).str() );
            }
            hufGroup = &groups[selectors[selector++]];
            base  = hufGroup->base.data() - 1;
            limit = hufGroup->limit.data() - 1;
        }

        // Read next huffman-coded symbol (into jj).
        ii = hufGroup->minLen;
        jj = getBits( ii );
        while ( ( ii <= hufGroup->maxLen ) && ( jj > limit[ii] ) ) {
            ii++;
            jj = ( jj << 1U ) | getBits<1>();
        }

        if ( ii > hufGroup->maxLen ) {
            std::stringstream msg;
            msg << "[BZip2 block data] " << ii << " bigger than max length " << hufGroup->maxLen;
            throw std::domain_error( std::move( msg ).str() );
        }

        // Huffman decode jj into nextSym (with bounds checking)
        jj -= base[ii];

        if ( (unsigned int)jj >= MAX_SYMBOLS ) {
            std::stringstream msg;
            msg << "[BZip2 block data] " << jj << " larger than max symbols " << MAX_SYMBOLS;
            throw std::domain_error( std::move( msg ).str() );
        }

        const auto nextSym = hufGroup->permute[jj];

        // If this is a repeated run, loop collecting data
        if ( (unsigned int)nextSym <= SYMBOL_RUNB ) {
            // If this is the start of a new run, zero out counter
            if ( !runPos ) {
                runPos = 1;
                hh = 0;
            }

            /* Neat trick that saves 1 symbol: instead of or-ing 0 or 1 at
             * each bit position, add 1 or 2 instead. For example,
             * 1011 is 1<<0 + 1<<1 + 2<<2. 1010 is 2<<0 + 2<<1 + 1<<2.
             * You can make any bit pattern that way using 1 less symbol than
             * the basic or 0/1 method (except all bits 0, which would use no
             * symbols, but a run of length 0 doesn't mean anything in this
             * context). Thus space is saved. */
            hh += ( runPos << nextSym );  // +runPos if RUNA; +2*runPos if RUNB
            runPos <<= 1;
            continue;
        }

        /* When we hit the first non-run symbol after a run, we now know
         * how many times to repeat the last literal, so append that many
         * copies to our buffer of decoded symbols (dbuf) now. (The last
         * literal used is the one at the head of the mtfSymbol array.) */
        if ( runPos ) {
            runPos = 0;
            if ( dbufCount + hh > bwdata.dbuf.size() ) {
                std::stringstream msg;
                msg << "[BZip2 block data] dbufCount + hh " << dbufCount + hh
                    << " > " << bwdata.dbuf.size() << " dbufSize";
                throw std::domain_error( std::move( msg ).str() );
            }

            const auto uc = symbolToByte[mtfSymbol[0]];
            bwdata.byteCount[uc] += hh;
            while ( hh-- ) {
                bwdata.dbuf[dbufCount++] = uc;
            }
        }

        // Is this the terminating symbol?
        if ( nextSym > symbolCount ) {
            break;
        }

        /* At this point, the symbol we just decoded indicates a new literal
         * character. Subtract one to get the position in the MTF array
         * at which this literal is currently to be found. (Note that the
         * result can't be -1 or 0, because 0 and 1 are RUNA and RUNB.
         * Another instance of the first symbol in the mtf array, position 0,
         * would have been handled as part of a run.) */
        if ( dbufCount >= bwdata.dbuf.size() ) {
            std::stringstream msg;
            msg << "[BZip2 block data] dbufCount " << dbufCount << " > " << bwdata.dbuf.size() << " dbufSize";
            throw std::domain_error( std::move( msg ).str() );
        }
        ii = nextSym - 1;
        auto uc = mtfSymbol[ii];
        // On my laptop, unrolling this memmove() into a loop shaves 3.5% off
        // the total running time.
        while ( ii-- ) {
            mtfSymbol[ii + 1] = mtfSymbol[ii];
        }
        mtfSymbol[0] = uc;
        uc = symbolToByte[uc];

        // We have our literal byte.  Save it into dbuf.
        bwdata.byteCount[uc]++;
        bwdata.dbuf[dbufCount++] = uc;
    }

    // Now we know what dbufCount is, do a better sanity check on origPtr.
    bwdata.writeCount = dbufCount;
    if ( bwdata.origPtr >= dbufCount ) {
        std::stringstream msg;
        msg << "[BZip2 block data] origPtr error " << bwdata.origPtr;
        throw std::domain_error( std::move( msg ).str() );
    }

    bwdata.prepare();

    encodedSizeInBits = bitReader().tell() - encodedOffsetInBits;
}


inline void
Block::BurrowsWheelerTransformData::prepare()
{
    // Turn byteCount into cumulative occurrence counts of 0 to n-1.
    for ( size_t i = 0, j = 0; i < byteCount.size(); ++i ) {
        const auto kk = j + byteCount[i];
        byteCount[i] = j;
        j = kk;
    }

    // Use occurrence counts to quickly figure out what order dbuf would be in
    // if we sorted it.
    // Using i as position, j as previous character, hh as current character,
    // and uc as run count.
    for ( int i = 0; i < writeCount; i++ ) {
        const auto uc = static_cast<uint8_t>( dbuf[i] );
        dbuf[byteCount[uc]] |= ( i << 8U );
        byteCount[uc]++;
    }

    dataCRC = 0xFFFFFFFFL;

    /* Decode first byte by hand to initialize "previous" byte. Note that it
     * doesn't get output, and if the first three characters are identical
     * it doesn't qualify as a run (hence uc=255, which will either wrap
     * to 1 or get reset). */
    if ( writeCount > 0 ) {
        writePos = dbuf[origPtr];
        writeCurrent = (unsigned char)writePos;
        writePos >>= 8;
        writeRun = -1;
    }
}


inline size_t
Block::BurrowsWheelerTransformData::decodeBlock( const size_t nMaxBytesToDecode,
                                                 char*        outputBuffer )
{
    assert( outputBuffer != nullptr );
    size_t nBytesDecoded = 0;

    while ( ( writeCount > 0 ) && ( nBytesDecoded < nMaxBytesToDecode ) ) {
        writeCount--;

        // Follow sequence vector to undo Burrows-Wheeler transform.
        const auto previous = writeCurrent;
        writePos = dbuf[writePos];
        writeCurrent = writePos & 0xff;
        writePos >>= 8;

        /* Whenever we see 3 consecutive copies of the same byte, the 4th is a repeat count */
        if ( writeRun < 3 ) {
            outputBuffer[nBytesDecoded++] = writeCurrent;
            dataCRC = ( dataCRC << 8U ) ^ CRC32_TABLE[( dataCRC >> 24U ) ^ writeCurrent];
            if ( writeCurrent != previous ) {
                writeRun = 0;
            } else {
                ++writeRun;
            }
        } else {
            int copies = writeCurrent;
            while ( copies-- ) {
                outputBuffer[nBytesDecoded++] = previous;
                dataCRC = ( dataCRC << 8U ) ^ CRC32_TABLE[( dataCRC >> 24U ) ^ previous];
            }
            writeCurrent = -1;
            writeRun = 0;
        }
    }

    /* decompression of this block completed successfully */
    if ( writeCount == 0 ) {
        dataCRC = ~dataCRC;
        if ( dataCRC != headerCRC ) {
            std::stringstream msg;
            msg << "Calculated CRC " << std::hex << dataCRC << " for block mismatches " << headerCRC;
            throw std::runtime_error( std::move( msg ).str() );
        }
    }

    return nBytesDecoded;
}
}  // namespace bzip2
