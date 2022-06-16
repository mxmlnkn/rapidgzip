/**
 * @file
 *
 * - Note that this implementation avoids C++ exceptions because invalid data is assumed happen rather often,
 *   which is the case when searching for deflate blocks without knowing the exact offsets! Exceptions are too
 *   slow for that!
 * - In the same manner as exceptions, it turns out that using std::array with a maximum possible size instead of
 *   dynamically sized std::vectors improves speed for checking and decoding a lot by avoiding heap allocations.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <BitReader.hpp>

#include <HuffmanCodingDoubleLiteralCached.hpp>
#include <HuffmanCodingSymbolsPerLength.hpp>
#include <HuffmanCodingReversedBitsCached.hpp>

#include "crc32.hpp"
#include "definitions.hpp"
#include "Error.hpp"
#include "gzip.hpp"


namespace pragzip
{
namespace deflate
{
/** For this namespace, refer to @see RFC 1951 "DEFLATE Compressed Data Format Specification version 1.3" */
constexpr size_t MAX_WINDOW_SIZE = 32*1024;
constexpr size_t MAX_UNCOMPRESSED_SIZE = std::numeric_limits<uint16_t>::max();
/* This is because the code length alphabet can't encode any higher value and because length 0 is ignored! */
constexpr uint8_t MAX_CODE_LENGTH = 15;

constexpr size_t MAX_LITERAL_OR_LENGTH_SYMBOLS = 286;
constexpr uint8_t MAX_DISTANCE_SYMBOL_COUNT = 30;
/* next power of two (because binary tree) of MAX_LITERAL_OR_LENGTH_SYMBOLS. This is assuming that all symbols
 * are equally likely to appear, i.e., all codes would be encoded with the same number of bits (9). */
constexpr size_t MAX_LITERAL_HUFFMAN_CODE_COUNT = 512;
constexpr size_t MAX_RUN_LENGTH = 258;


using LiteralOrLengthHuffmanCoding =
    HuffmanCodingDoubleLiteralCached<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_LITERAL_HUFFMAN_CODE_COUNT>;

/* Use the same parameters, even when we know the symbol count for the FixedHuffmanCoding, in order to use these
 * templated types interchangibly without polymorphism.
 * @todo Make it such that the implementations can handle the case that the construction might result in
 *       larger symbol values than are allowed to appear in the first place! I.e., cut-off construction there.
 *       Note that changing this from 286 to 512, lead to an increase of the runtime! We need to reduce it again! */
using FixedHuffmanCoding = LiteralOrLengthHuffmanCoding;
//using FixedHuffmanCoding =
//    HuffmanCodingReversedBitsCached<uint16_t, MAX_CODE_LENGTH, uint16_t, MAX_LITERAL_OR_LENGTH_SYMBOLS + 2>;


[[nodiscard]] FixedHuffmanCoding
createFixedHC()
{
    std::vector<uint8_t> encodedFixedHuffmanTree( MAX_LITERAL_OR_LENGTH_SYMBOLS + 2, 8 );
    std::fill( encodedFixedHuffmanTree.begin() + 144, encodedFixedHuffmanTree.begin() + 256, 9 );
    std::fill( encodedFixedHuffmanTree.begin() + 256, encodedFixedHuffmanTree.begin() + 280, 7 );

    FixedHuffmanCoding result;
    const auto error = result.initializeFromLengths( encodedFixedHuffmanTree );
    if ( error != Error::NONE ) {
        throw std::logic_error( "Fixed Huffman Tree could not be created!" );
    }

    return result;
}


[[nodiscard]] constexpr uint16_t
calculateDistance( uint16_t distance,
                   uint8_t  extraBitsCount,
                   uint16_t extraBits )
{
    assert( distance >= 4 );
    return 1U + ( 1U << ( extraBitsCount + 1U ) ) + ( ( distance % 2U ) << extraBitsCount ) + extraBits;
};


[[nodiscard]] inline uint16_t
calculateDistance( uint16_t distance )
{
    assert( distance >= 4 );
    const auto extraBitsCount = ( distance - 2U ) / 2U;
    return 1U + ( 1U << ( extraBitsCount + 1U ) ) + ( ( distance % 2U ) << extraBitsCount );
};


using DistanceLUT = std::array<uint16_t, 30>;

[[nodiscard]] DistanceLUT
createDistanceLUT()
{
    DistanceLUT result;
    for ( uint16_t i = 0; i < 4; ++i ) {
        result[i] = i + 1;
    }
    for ( uint16_t i = 4; i < result.size(); ++i ) {
        result[i] = calculateDistance( i );
    }
    return result;
}


alignas(8) static inline const DistanceLUT
distanceLUT = createDistanceLUT();


[[nodiscard]] inline uint16_t
calculateLength( uint16_t code )
{
    assert( code < 285 - 261 );
    const auto extraBits = code / 4U;
    return 3U + ( 1U << ( extraBits + 2U ) ) + ( ( code % 4U ) << extraBits );
};


using LengthLUT = std::array<uint16_t, 285 - 261>;

[[nodiscard]] LengthLUT
createLengthLUT()
{
    LengthLUT result;
    for ( uint16_t i = 0; i < result.size(); ++i ) {
        result[i] = calculateLength( i );
    }
    return result;
}


alignas(8) static constexpr LengthLUT
lengthLUT = createLengthLUT();


/**
 * @todo Silesia is ~70% slower when writing back and calculating CRC32.
 * When only only writing the result and not calculating CRC32, then it is ~60% slower.
 * Both, LZ77 back-references and CRC32 calculation can still be improved upon by a lot, I think.
 * Silesia contains a lot of 258 length back-references with distance 1, which could be replaced with memset
 * with the last byte.
 */
template<bool CALCULATE_CRC32 = false>
class Block
{
public:
    enum class CompressionType : uint8_t
    {
        UNCOMPRESSED    = 0b00,
        FIXED_HUFFMAN   = 0b01,
        DYNAMIC_HUFFMAN = 0b10,
        RESERVED        = 0b11,
    };

    [[nodiscard]] static std::string
    toString( CompressionType compressionType )
    {
        switch ( compressionType )
        {
        case CompressionType::UNCOMPRESSED:
            return "Uncompressed";
        case CompressionType::FIXED_HUFFMAN:
            return "Fixed Huffman";
        case CompressionType::DYNAMIC_HUFFMAN:
            return "Dynamic Huffman";
        case CompressionType::RESERVED:
            return "Reserved";
        }
        return "Unknown";
    }

    /**
     * Only one of the two will contain non-empty VectorViews depending on whether marker bytes might appear.
     * @ref dataWithMarkers will be empty when @ref setInitialWindow has been called.
     */
    struct BufferViews
    {
    public:
        [[nodiscard]] size_t
        size() const
        {
            return dataWithMarkers[0].size() + dataWithMarkers[1].size() + data[0].size() + data[1].size();
        }

        [[nodiscard]] bool
        containsMarkers() const
        {
            return !dataWithMarkers[0].empty() || !dataWithMarkers[1].empty();
        }

    public:
        std::array<VectorView<uint16_t>, 2> dataWithMarkers;
        std::array<VectorView<uint8_t>, 2> data;
    };

public:
    [[nodiscard]] bool
    eob() const
    {
        return m_atEndOfBlock;
    }

    [[nodiscard]] bool
    eos() const
    {
        return m_atEndOfBlock && m_isLastBlock;
    }

    [[nodiscard]] bool
    eof() const
    {
        return m_atEndOfFile;
    }

    [[nodiscard]] bool
    isLastBlock() const
    {
        return m_isLastBlock;
    }

    [[nodiscard]] CompressionType
    compressionType() const
    {
        return m_compressionType;
    }

    /**
     * @param nMaxToDecode Maximum bytes to decode. It might decode less even when there is enough data.
     *                     It will only decode as much as fits into the internal buffer.
     *                     It might decode more when it is an uncompressed block.
     *                     Check for @ref eob to test for the end of the block instead of testing the read byte count.
     */
    [[nodiscard]] std::pair<BufferViews, Error>
    read( BitReader& bitReader,
          size_t     nMaxToDecode = std::numeric_limits<size_t>::max() );

    /**
     * @tparam treatLastBlockAsError This parameter is intended when using readHeader for finding valid headers.
     *         Ignoring last headers, filters candidates by 25% and filtering them sooner avoids reading the
     *         Huffman codings, which saves almost 50% of time!
     */
    template<bool treatLastBlockAsError = false>
    [[nodiscard]] Error
    readHeader( BitReader& bitReader );

    [[nodiscard]] const auto&
    window() const
    {
        return m_window;
    }

    [[nodiscard]] uint32_t
    crc32() const
    {
        return ~m_crc32;
    }


    [[nodiscard]] size_t
    uncompressedSize() const
    {
        return m_compressionType == CompressionType::UNCOMPRESSED ? m_uncompressedSize : 0;
    }

    /**
     * Returns the last 32 KiB decoded bytes. This can be called after decoding a block has finished
     * and then can be used to store and load it with setInitialWindow to restart decoding with the next block.
     * Because this is not supposed to be called very often, it returns a copy of the data instead of views.
     */
    [[nodiscard]] std::array<uint8_t, MAX_WINDOW_SIZE>
    lastWindow() const
    {
        if ( m_containsMarkerBytes ) {
            throw std::invalid_argument( "No valid window available!" );
        }

        std::array<uint8_t, MAX_WINDOW_SIZE> result;

        const auto nBytesToCopy = std::min( m_decodedBytes, MAX_WINDOW_SIZE );
        if ( m_windowPosition >= nBytesToCopy ) {
            std::memcpy( result.data() + result.size() - nBytesToCopy,
                         m_window.data() + ( m_windowPosition - nBytesToCopy ),
                         nBytesToCopy );
        } else {
            const auto nToCopyFromStart = m_windowPosition;
            const auto nToCopyFromEnd = nBytesToCopy - nToCopyFromStart;

            /* Copy wrapped-around oldest data from end of m_window to "start" of result. */
            std::memcpy( result.data() + ( result.size() - nToCopyFromStart - nToCopyFromEnd ),
                         m_window.data() + ( m_window.size() - nToCopyFromEnd ),
                         nToCopyFromStart );
            /* Copy most recent decoded data from beginning of m_window to end of result. */
            std::memcpy( result.data() + ( result.size() - nToCopyFromStart ),
                         m_window.data(),
                         nToCopyFromStart );
        }

        return result;
    }

    static void
    checkMarkerBytes( WeakVector<std::uint16_t> buffer )
    {
        for ( size_t i = 0; i < buffer.size(); ++i ) {
            const auto code = buffer[i];
            if ( ( code > std::numeric_limits<uint8_t>::max() )
                 && ( code < MAX_WINDOW_SIZE ) )
            {
                std::stringstream message;
                message << "Found unknown 2 B code (" << (int)buffer[i] << ") in buffer at position " << i << "!";
                throw std::invalid_argument( message.str() );
            }
        }
    }

    static void
    replaceMarkerBytes( WeakVector<std::uint16_t>                      buffer,
                        ArrayView<std::uint8_t, MAX_WINDOW_SIZE> const initialWindow )
    {
        checkMarkerBytes( buffer );

        const auto mapMarker =
            [&initialWindow] ( uint16_t value )
            {
                if ( ( value > std::numeric_limits<uint8_t>::max() )
                     && ( value < MAX_WINDOW_SIZE ) )
                {
                    throw std::invalid_argument( "Cannot replace unknown 2 B code!" );
                }

                return value >= MAX_WINDOW_SIZE
                       ? initialWindow[value - MAX_WINDOW_SIZE]
                       : static_cast<uint8_t>( value );
            };

        std::transform( buffer.begin(), buffer.end(), buffer.begin(), mapMarker );
    }

    /**
     * Should be called if this is the first block, i.e., if there is now winow buffer to initialize.
     * @todo I don't like that the API requires to call this for normal blocks :/. Maybe automatically
     *       detect and set m_containsMarkerBytes.
     */
    void
    setInitialWindow()
    {
        m_containsMarkerBytes = false;
        m_decodedBytes = 0;
    }

    /**
     * Primes the deflate decoder with a window to be used for the LZ77 backreferences.
     * There are two use cases for this function:
     *  - To set a window before decoding in order to resume decoding and for seeking in the gzip stream.
     *  - To replace marker bytes with real data in post.
     * The only real use case for the latter would be huge deflate blocks. In practice, all gzip implementations
     * I encountered produced deflate blocks not larger than 64 KiB. In that case, it would be simpler to create
     * a new deflate::Block object on the next block and then set the initial window before decoding with the
     * data from the last read calls whose markers will have to be replaced using @ref replaceMarkerBytes.
     * This method does not much more but has to account for wrap-around, too.
     */
    std::array<VectorView<uint8_t>, 2>
    setInitialWindow( ArrayView<std::uint8_t, MAX_WINDOW_SIZE> const initialWindow )
    {
        if ( !m_containsMarkerBytes ) {
            return lastBuffers( m_window, m_windowPosition, std::min( m_window.size(), m_decodedBytes ) );
        }

        /* Set an initial window before decoding has started. */
        if ( ( m_decodedBytes == 0 ) && ( m_windowPosition == 0 ) ) {
            std::memcpy( m_window.data(), initialWindow.data(), initialWindow.size() );
            m_windowPosition = initialWindow.size();
            m_decodedBytes = initialWindow.size();
            m_containsMarkerBytes = false;
            return lastBuffers( m_window, m_windowPosition, std::min( m_window.size(), m_decodedBytes ) );
        }

        replaceMarkerBytes( { m_window16.data(), m_window16.size() }, initialWindow );

        /* We cannot simply move each byte to m_window because it has twice as many elements as m_windows16
         * and simply filling it from left to right will result in wrapping not working because the right half
         * is empty. It would only work if there is no wrapping necessary because it is a contiguous block!
         * To achieve that, map i -> i' such that m_windowPosition is m_window.size() - 1.
         * This way all back-references will not wrap around on the left border. */
        std::array<uint8_t, decltype( m_window16 )().size()> conflatedBuffer{};

        for ( size_t i = 0; i < m_window16.size(); ++i ) {
            conflatedBuffer[i] = m_window16[( i + m_windowPosition ) % m_window16.size()];
        }

        std::memcpy( m_window.data() + ( m_window.size() - conflatedBuffer.size() ),
                     conflatedBuffer.data(),
                     conflatedBuffer.size() );

        m_windowPosition = 0;

        m_containsMarkerBytes = false;
        return lastBuffers( m_window, m_windowPosition, std::min( m_window16.size(), m_decodedBytes ) );
    }

    [[nodiscard]] bool
    isValid() const
    {
        switch ( m_compressionType )
        {
        case CompressionType::RESERVED:
            return false;

        case CompressionType::UNCOMPRESSED:
            return true;

        case CompressionType::FIXED_HUFFMAN:
            return m_fixedHC.isValid();

        case CompressionType::DYNAMIC_HUFFMAN:
            return m_literalHC.isValid();
        }

        return false;
    }

private:
    template<typename Window>
    [[nodiscard]] std::pair<size_t, Error>
    readInternal( BitReader& bitReader,
                  size_t     nMaxToDecode,
                  Window&    window );

    [[nodiscard]] static uint16_t
    getLength( uint16_t   code,
               BitReader& bitReader );

    [[nodiscard]] std::pair<uint16_t, Error>
    getDistance( BitReader& bitReader ) const;

    /**
     * @param position The position in the window where the next byte would be appended. Similar to std::end();
     * @param size How many of the bits before @ref position are requested. @ref position - @ref size is begin.
     * @return the areas last written in the circular window buffer. Because of the circularity, two VectorViews
     *         are returned and both are non-empty in the case of the last written data wrapping around.
     */
    template<typename Window,
             typename Symbol = typename Window::value_type>
    [[nodiscard]] static std::array<VectorView<Symbol>, 2>
    lastBuffers( const Window& window,
                 size_t        position,
                 size_t        size )
    {
        if ( size > window.size() ) {
            throw std::invalid_argument( "Requested more bytes than fit in the buffer. Data is missing!" );
        }

        std::array<VectorView<Symbol>, 2> result;
        if ( size == 0 ) {
            return result;
        }

        /* Calculate wrapped around begin without unsigned underflow during the difference. */
        const auto begin = ( position + window.size() - ( size % window.size() ) ) % window.size();
        if ( begin < position ) {
            result[0] = VectorView<Symbol>( window.data() + begin, position - begin );
            return result;
        }

        result[0] = VectorView<Symbol>( window.data() + begin, window.size() - begin );  // up to end of window
        result[1] = VectorView<Symbol>( window.data(), position );  // wrapped around part at start of window
        return result;
    }

private:
    /**
     * Size is max back-reference distance + max back-reference length to avoid the case of "in-place" updating
     * (overlapping input and output). Round up to power of two in the hope of making modulo faster...
     * Note that this buffer may be used for 16-bit half-decompressed data for when the initial window buffer is
     * unknown as well as for the case of the window buffer being known which only requires uint8_t.
     * For the former we need twice the size!
     */
    using PreDecodedBuffer = std::array<uint16_t, 2 * MAX_WINDOW_SIZE>;
    using DecodedBuffer = WeakArray<std::uint8_t, PreDecodedBuffer().size() * sizeof( uint16_t ) / sizeof( uint8_t )>;
    static_assert( std::min( PreDecodedBuffer().size(),
                             PreDecodedBuffer().size() * sizeof( uint16_t ) / sizeof( uint8_t ) ) >= 64 * 1024,
                   "Buffers should at least be able to fit one uncompressed block." );

private:
    [[nodiscard]] Error
    readDynamicHuffmanCoding( BitReader& bitReader );

    /* Note that making this constexpr or an immediately evaluated lambda expression to initialize the buffer,
     * increases compile time from 14s to 64s with GCC 11! */
    [[nodiscard]] static PreDecodedBuffer
    initializeMarkedWindowBuffer()
    {
        PreDecodedBuffer result{};
        for ( size_t i = 0; i < MAX_WINDOW_SIZE; ++i ) {
            result[result.size() - MAX_WINDOW_SIZE + i] = i + MAX_WINDOW_SIZE;
        }
        return result;
    }

private:
    uint32_t m_crc32 = ~uint32_t( 0 );
    uint16_t m_uncompressedSize = 0;

private:
    /* These flags might get triggered by the read function. */
    mutable bool m_atEndOfBlock{ false };
    mutable bool m_atEndOfFile{ false };

    bool m_isLastBlock{ false };
    CompressionType m_compressionType{ CompressionType::RESERVED };

    FixedHuffmanCoding m_fixedHC;
    LiteralOrLengthHuffmanCoding m_literalHC;

    /* HuffmanCodingReversedBitsCached is definitely faster for siles.tar.gz which has more back-references than
     * base64.gz for which the difference in changing this Huffman coding is negligible. Note that we can't use
     * double caching for this because that would mean merging the cache with ne next literal/length Huffman code! */
    HuffmanCodingReversedBitsCached<uint16_t, MAX_CODE_LENGTH, uint8_t, MAX_DISTANCE_SYMBOL_COUNT> m_distanceHC;

    alignas(64) PreDecodedBuffer m_window16{ initializeMarkedWindowBuffer() };

    DecodedBuffer m_window{ reinterpret_cast<std::uint8_t*>( m_window16.data() ) };

    /**
     * Points to the index of the next code to be written in @ref m_window. I.e., can also be interpreted as
     * the size of m_window (in the beginning as long as it does not wrap).
     */
    size_t m_windowPosition{ 0 };
    /**
     * @todo Instead of this bool, keep track of the largest backreference and dynamically switch to 16-bit?
     */
    bool m_containsMarkerBytes{ true };
    /**
     * Sum of decoded bytes over all read calls. Also will be set when calling setInitialWindow.
     * It is used to determine whether a backreference references valid data.
     */
    size_t m_decodedBytes{ 0 };
};
} // namespace deflate


template<bool CALCULATE_CRC32>
template<bool treatLastBlockAsError>
Error
deflate::Block<CALCULATE_CRC32>::readHeader( BitReader& bitReader )
{
    m_isLastBlock = bitReader.read<1>();
    if constexpr ( treatLastBlockAsError ) {
        if ( m_isLastBlock ) {
            return Error::UNEXPECTED_LAST_BLOCK;
        }
    }
    m_compressionType = static_cast<CompressionType>( bitReader.read<2>() );

    Error error = Error::NONE;

    switch ( m_compressionType )
    {
    case CompressionType::UNCOMPRESSED:
    {
        /* @todo There is no mention what the padding is. But there is mention for the flags, that the reserved ones
         *       ones should be zero. Could I also check for the padding to be zero? I just don't want to believe,
         *       that anyone would store random data here ... Although it might be good for stenography :D */
        if ( bitReader.tell() % BYTE_SIZE != 0 ) {
            const auto padding = bitReader.read( BYTE_SIZE - ( bitReader.tell() % BYTE_SIZE ) );
            if ( padding != 0 ) {
                return Error::NON_ZERO_PADDING;
            }
        }

        m_uncompressedSize = bitReader.read<2 * BYTE_SIZE>();
        const auto negatedLength = bitReader.read<2 * BYTE_SIZE>();
        if ( m_uncompressedSize != static_cast<uint16_t>( ~negatedLength ) ) {
            return Error::LENGTH_CHECKSUM_MISMATCH;
        }
        break;
    }

    case CompressionType::FIXED_HUFFMAN:
        /* Initializing m_fixedHC statically leads to very weird problems when compiling with ASAN.
         * The code might be too complex and run into the static initialization order fiasco. */
        m_fixedHC = createFixedHC();
        break;

    case CompressionType::DYNAMIC_HUFFMAN:
        error = readDynamicHuffmanCoding( bitReader );
        break;

    case CompressionType::RESERVED:
        return Error::INVALID_COMPRESSION;
    };

    m_atEndOfBlock = false;

    return error;
}


template<bool CALCULATE_CRC32>
Error
deflate::Block<CALCULATE_CRC32>::readDynamicHuffmanCoding( BitReader& bitReader )
{
    /**
     * Huffman codings map variable length (bit) codes to symbols.
     * Huffman codings are given a as a tuple of code lengths, i.e., number of bits for Huffman code to use.
     * The elements of the tuple correspond to the elements of the ordered set of symbols, i.e., the alphabet.
     * For reading the block header it is important to understand that there are three different Huffmann condings
     * and also alphabets:
     *  - Alphabet L: the mixed alphabet containing 286 literals and lengths / instructions.
     *  - Alphabet D: contains distances in 30 different symbols / instructions.
     *  - Alphabet C: contains 19 different symbols / instructions for reconstructing the code length tuples
     *                Is used to encode L and D! It itself is encoded a sequence of 3-bit numbers for the bit lengths.
     *                This means, there can be no longer Huffman code than 7 for this, i.e., fits into a char.
     */

    const auto literalCodeCount = 257 + bitReader.read<5>();
    if ( literalCodeCount > MAX_LITERAL_OR_LENGTH_SYMBOLS ) {
        return Error::EXCEEDED_LITERAL_RANGE;
    }
    const auto distanceCodeCount = 1 + bitReader.read<5>();
    if ( distanceCodeCount > MAX_DISTANCE_SYMBOL_COUNT ) {
        return Error::EXCEEDED_DISTANCE_RANGE;
    }
    const auto codeLengthCount = 4 + bitReader.read<4>();

    /* Get code lengths (CL) for alphabet C. */
    constexpr auto MAX_CL_SYMBOL_COUNT = 19;
    constexpr auto CL_CODE_LENGTH_BIT_COUNT = 3;
    constexpr auto MAX_CL_CODE_LENGTH = 1U << CL_CODE_LENGTH_BIT_COUNT;
    static constexpr std::array<uint8_t, MAX_CL_SYMBOL_COUNT> alphabetOrderC =
        { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    std::array<uint8_t, MAX_CL_SYMBOL_COUNT> codeLengthCL = {};
    for ( size_t i = 0; i < codeLengthCount; ++i ) {
        codeLengthCL[alphabetOrderC[i]] = bitReader.read<CL_CODE_LENGTH_BIT_COUNT>();
    }

    Error error = Error::NONE;
    HuffmanCodingSymbolsPerLength<uint8_t, MAX_CL_CODE_LENGTH, uint8_t, MAX_CL_SYMBOL_COUNT> codeLengthHC;
    error = codeLengthHC.initializeFromLengths( VectorView<uint8_t>( codeLengthCL.data(), codeLengthCL.size() ) );
    if ( error != Error::NONE ) {
        return error;
    }

    /* Decode the code lengths for the literal/length and distance alphabets. */
    std::array<uint8_t, MAX_LITERAL_OR_LENGTH_SYMBOLS + MAX_DISTANCE_SYMBOL_COUNT> literalCL = {};
    const auto literalCLSize = literalCodeCount + distanceCodeCount;
    for ( size_t i = 0; i < literalCLSize; ) {
        const auto decoded = codeLengthHC.decode( bitReader );
        if ( !decoded ) {
            return Error::INVALID_HUFFMAN_CODE;
        }
        const auto code = *decoded;

        /* Note that this interpretation of the alphabet results in the maximum code length being 15! */
        if ( code <= 15 ) {
            literalCL[i] = code;
             ++i;
        } else if ( code == 16 ) {
            if ( i == 0 ) {
                return Error::INVALID_CL_BACKREFERENCE;
            }
            const auto lastValue = literalCL[i-1];
            const auto repeatCount = bitReader.read<2>() + 3;
            if ( i + repeatCount > literalCLSize ) {
                return Error::EXCEEDED_LITERAL_RANGE;
            }
            for ( uint32_t j = 0; j < repeatCount; ++j, ++i ) {
                literalCL[i] = lastValue;
            }
        } else if ( code == 17 ) {
            /* Decode fixed number of zeros. The vector is initialized to zeros, so we can simply skip these. */
            i += bitReader.read<3>() + 3;
        } else if ( code == 18 ) {
            /* Decode fixed number of zeros. The vector is initialized to zeros, so we can simply skip these. */
            i += bitReader.read<7>() + 11;
        } else {
            throw std::logic_error( "No such value should have been in the alphabet!" );
        }

        if ( i > literalCLSize ) {
            return Error::EXCEEDED_LITERAL_RANGE;
        }
    }

    /* When encoding base64-encoded random-data, I encountered a length of 9, so uint16_t is necessary! */
    error = m_distanceHC.initializeFromLengths(
        VectorView<uint8_t>( literalCL.data() + literalCodeCount, distanceCodeCount ) );
    if ( error != Error::NONE ) {
        return error;
    }

    error = m_literalHC.initializeFromLengths( VectorView<uint8_t>( literalCL.data(), literalCodeCount ) );
    return error;
}


template<bool CALCULATE_CRC32>
uint16_t
deflate::Block<CALCULATE_CRC32>::getLength( uint16_t   code,
                                            BitReader& bitReader )
{
    if ( code <= 264 ) {
        return code - 257U + 3U;
    } else if ( code < 285 ) {
        code -= 261;
        const auto extraBits = code / 4;
        return calculateLength( code ) + bitReader.read( extraBits );
    } else if ( code == 285 ) {
        return 258;
    }

    throw std::invalid_argument( "Invalid Code!" );
}


template<bool CALCULATE_CRC32>
std::pair<uint16_t, Error>
deflate::Block<CALCULATE_CRC32>::getDistance( BitReader& bitReader ) const
{
    uint16_t distance = 0;
    if ( m_compressionType == CompressionType::FIXED_HUFFMAN ) {
        distance = reverseBits( static_cast<uint8_t>( bitReader.read<5>() ) ) >> 3;
        if ( distance >= MAX_DISTANCE_SYMBOL_COUNT ) {
            return { 0, Error::EXCEEDED_DISTANCE_RANGE };
        }
    } else {
        const auto decodedDistance = m_distanceHC.decode( bitReader );
        if ( !decodedDistance ) {
            return { 0, Error::INVALID_HUFFMAN_CODE };
        }
        distance = static_cast<uint16_t>( *decodedDistance );
    }

    if ( distance <= 3U ) {
        distance += 1U;
    } else if ( distance <= 29U ) {
        const auto extraBitsCount = ( distance - 2U ) / 2U;
        const auto extraBits = bitReader.read( extraBitsCount );
        distance = calculateDistance( distance ) + extraBits;
    } else {
        throw std::logic_error( "Invalid distance codes encountered!" );
    }

    return { distance, Error::NONE };
}


template<bool CALCULATE_CRC32>
std::pair<typename deflate::Block<CALCULATE_CRC32>::BufferViews, Error>
deflate::Block<CALCULATE_CRC32>::read( BitReader& bitReader,
                                       size_t     nMaxToDecode )
{
    if ( eob() ) {
        return { {}, Error::NONE };
    }

    if ( m_compressionType == CompressionType::RESERVED ) {
        throw std::domain_error( "Invalid deflate compression type!" );
    }

    BufferViews result;

    /**
     * Special case for uncompressed blocks larger or equal than the window size.
     * Because, in this case, we can simply copy the uncompressed block to the beginning of the window
     * without worrying about wrap-around and also now we know that there are no markers remaining!
     * @todo use memcpy / large read even when smaller than MAX_WINDOW_SIZE to improve speed
     */
    if ( ( m_compressionType == CompressionType::UNCOMPRESSED ) && ( m_uncompressedSize >= MAX_WINDOW_SIZE ) ) {
        m_containsMarkerBytes = false;
        m_windowPosition = m_uncompressedSize;
        m_atEndOfBlock = true;
        m_decodedBytes += m_uncompressedSize;

        const auto nBytesRead = bitReader.read( reinterpret_cast<char*>( m_window.data() ), m_uncompressedSize );

        if constexpr ( CALCULATE_CRC32 ) {
            for ( size_t i = 0; i < nBytesRead; ++i ) {
                m_crc32 = updateCRC32( m_crc32, m_window[i] );
            }
        }

        result.data = lastBuffers( m_window, m_windowPosition, nBytesRead );
        return { result, nBytesRead == m_uncompressedSize ? Error::NONE : Error::EOF_UNCOMPRESSED };
    }

    if ( m_containsMarkerBytes ) {
        const auto [nBytesRead, error] = readInternal( bitReader, nMaxToDecode, m_window16 );
        result.dataWithMarkers = lastBuffers( m_window16, m_windowPosition, nBytesRead );
        return { result, error };
    }

    const auto [nBytesRead, error] = readInternal( bitReader, nMaxToDecode, m_window );
    result.data = lastBuffers( m_window, m_windowPosition, nBytesRead );
    return { result, error };
}


template<bool CALCULATE_CRC32>
template<typename Window>
std::pair<size_t, Error>
deflate::Block<CALCULATE_CRC32>::readInternal( BitReader& bitReader,
                                               size_t     nMaxToDecode,
                                               Window&    window )
{
    const auto appendToWindow =
        [this, &window] ( auto decodedSymbol )
        {
            constexpr bool containsMarkerBytes = std::is_same_v<std::decay_t<decltype( *window.data() ) >, uint16_t>;

            if constexpr ( CALCULATE_CRC32 && !containsMarkerBytes ) {
                m_crc32 = updateCRC32( m_crc32, decodedSymbol );
            }

            window[m_windowPosition] = decodedSymbol;
            m_windowPosition++;
            m_windowPosition %= window.size();
        };

    constexpr bool containsMarkerBytes = std::is_same_v<std::decay_t<decltype( *window.data() ) >, uint16_t>;

    if ( m_compressionType == CompressionType::UNCOMPRESSED ) {
        /**
         * Because the non-compressed deflate block size is 16-bit, the uncompressed data is limited to 65535 B!
         * The buffer can hold MAX_WINDOW_SIZE 16-bit values (for markers) or twice the amount of decoded bytes.
         * Therefore, this routine is safe to call.
         */
        for ( uint16_t i = 0; i < m_uncompressedSize; ++i ) {
            const auto literal = bitReader.read<BYTE_SIZE>();
            appendToWindow( literal );
        }
        m_atEndOfBlock = true;
        m_decodedBytes += m_uncompressedSize;
        return { m_uncompressedSize, Error::NONE };
    }

    const auto& coding = m_compressionType == CompressionType::FIXED_HUFFMAN ? m_fixedHC : m_literalHC;
    if ( !coding.isValid() ) {
        throw std::invalid_argument( "No Huffman coding loaded! Call readHeader first!" );
    }

    nMaxToDecode = std::min( nMaxToDecode, window.size() - MAX_RUN_LENGTH );

    size_t nBytesRead{ 0 };
    for ( nBytesRead = 0; nBytesRead < nMaxToDecode; )
    {
        const auto decoded = coding.decode( bitReader );
        if ( !decoded ) {
            return { nBytesRead, Error::INVALID_HUFFMAN_CODE };
        }
        auto code = *decoded;

        if ( code <= 255 ) {
            appendToWindow( code );
             ++nBytesRead;
            continue;
        }

        if ( code == 256 ) {
            m_atEndOfBlock = true;
            break;
        }

        if ( code > 285 ) {
            return { nBytesRead, Error::INVALID_HUFFMAN_CODE };
        }

        const auto length = getLength( code, bitReader );
        if ( length != 0 ) {
            const auto [distance, error] = getDistance( bitReader );
            if ( error != Error::NONE ) {
                return { nBytesRead, error };
            }

            if constexpr ( !containsMarkerBytes ) {
                if ( distance > m_decodedBytes + nBytesRead ) {
                    return { nBytesRead, Error::EXCEEDED_WINDOW_RANGE };
                }
            }

            /**
             * @todo use memcpy when it is not wrapping around! Note that we might be able to use lastBuffers
             *       and write to those views to determine where it wraps around!
             * @todo There are two kinds of wrap around! the actual buffer and when length > distance!
             */
            const auto offset = ( m_windowPosition + window.size() - distance ) % window.size();
            const auto nToCopyPerRepeat = std::min( static_cast<uint16_t>( distance ), length );
            assert( nToCopyPerRepeat != 0 );

            for ( size_t nCopied = 0; nCopied < length; ) {
                for ( size_t position = offset;
                     ( position < offset + nToCopyPerRepeat ) && ( nCopied < length );
                     ++position, ++nCopied )
                {
                    const auto copiedSymbol = window[position % window.size()];
                    appendToWindow( copiedSymbol );
                    nBytesRead++;
                }
            }
        }
    }

    m_decodedBytes += nBytesRead;
    return { nBytesRead, Error::NONE };
}
}  // namespace pragzip
