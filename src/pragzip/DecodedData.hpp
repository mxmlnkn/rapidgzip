#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include <VectorView.hpp>

#include "definitions.hpp"


namespace pragzip::deflate
{
template<bool FULL_WINDOW>
struct MapMarkers
{
    MapMarkers( VectorView<uint8_t> const& window ) :
        m_window( window )
    {
        assert( ( m_window.size() >= MAX_WINDOW_SIZE ) == FULL_WINDOW );
    }

    [[nodiscard]] constexpr uint8_t
    operator()( uint16_t value ) const
    {
        if ( value <= std::numeric_limits<uint8_t>::max() ) {
            return static_cast<uint8_t>( value );
        }

        if ( value < MAX_WINDOW_SIZE ) {
            throw std::invalid_argument( "Cannot replace unknown 2 B code!" );
        }

        if constexpr ( !FULL_WINDOW  ) {
            if ( value - MAX_WINDOW_SIZE >= m_window.size() ) {
                throw std::invalid_argument( "Window too small!" );
            }
        }

        return m_window[value - MAX_WINDOW_SIZE];
    }

private:
    const VectorView<uint8_t> m_window;
};


void
replaceMarkerBytes( WeakVector<std::uint16_t>  buffer,
                    VectorView<uint8_t> const& window )
{
    /* For maximum size windows, we can skip one check because even UINT16_MAX is valid. */
    if ( window.size() >= MAX_WINDOW_SIZE ) {
        std::transform( buffer.begin(), buffer.end(), buffer.begin(), MapMarkers<true>( window ) );
    } else {
        std::transform( buffer.begin(), buffer.end(), buffer.begin(), MapMarkers<false>( window ) );
    }
}


/**
 * Only one of the two will contain non-empty VectorViews depending on whether marker bytes might appear.
 * @ref dataWithMarkers will be empty when @ref setInitialWindow has been called.
 */
struct DecodedDataView
{
public:
    [[nodiscard]] constexpr size_t
    size() const noexcept
    {
        return dataWithMarkers[0].size() + dataWithMarkers[1].size() + data[0].size() + data[1].size();
    }

    [[nodiscard]] size_t
    dataSize() const
    {
        return data[0].size() + data[1].size();
    }

    [[nodiscard]] size_t
    dataWithMarkersSize() const
    {
        return dataWithMarkers[0].size() + dataWithMarkers[1].size();
    }

    [[nodiscard]] constexpr bool
    containsMarkers() const noexcept
    {
        return !dataWithMarkers[0].empty() || !dataWithMarkers[1].empty();
    }

public:
    std::array<VectorView<uint16_t>, 2> dataWithMarkers;
    std::array<VectorView<uint8_t>, 2> data;
};


struct DecodedData
{
public:
    using WindowView = VectorView<uint8_t>;

    class Iterator
    {
    public:
        explicit
        Iterator( const DecodedData& decodedData,
                  const size_t       offset = 0,
                  const size_t       size = std::numeric_limits<size_t>::max() ) :
            m_data( decodedData ),
            m_size( size )
        {
            m_offsetInChunk = offset;
            for ( m_currentChunk = 0; m_currentChunk < m_data.data.size(); ++m_currentChunk ) {
                const auto& chunk = m_data.data[m_currentChunk];
                if ( ( m_offsetInChunk < chunk.size() ) && !chunk.empty() ) {
                    m_sizeInChunk = std::min( chunk.size() - m_offsetInChunk, m_size );
                    break;
                }
                m_offsetInChunk -= chunk.size();
            }
        }

        [[nodiscard]] operator bool() const noexcept
        {
            return ( m_currentChunk < m_data.data.size() ) && ( m_processedSize < m_size );
        }

        [[nodiscard]] std::pair<const void*, uint64_t>
        operator*() const
        {
            const auto& chunk = m_data.data[m_currentChunk];
            return { chunk.data() + m_offsetInChunk, m_sizeInChunk };
        }

        void
        operator++()
        {
            m_processedSize += m_sizeInChunk;
            m_offsetInChunk = 0;
            m_sizeInChunk = 0;

            if ( m_processedSize > m_size ) {
                throw std::logic_error( "Iterated over mroe bytes than was requested!" );
            }

            if ( !static_cast<bool>( *this ) ) {
                return;
            }

            ++m_currentChunk;
            for ( ; m_currentChunk < m_data.data.size(); ++m_currentChunk ) {
                const auto& chunk = m_data.data[m_currentChunk];
                if ( !chunk.empty() ) {
                    m_sizeInChunk = std::min( chunk.size(), m_size - m_processedSize );
                    break;
                }
            }
        }

    private:
        const DecodedData& m_data;
        const size_t m_size;

        size_t m_currentChunk{ 0 };
        size_t m_offsetInChunk{ 0 };
        size_t m_sizeInChunk{ 0 };
        size_t m_processedSize{ 0 };
    };

public:
    void
    append( std::vector<uint8_t>&& toAppend )
    {
        if ( !toAppend.empty() ) {
            data.emplace_back( std::move( toAppend ) );
            data.back().shrink_to_fit();
        }
    }

    void
    append( DecodedDataView const& buffers );

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

    [[nodiscard]] size_t
    sizeInBytes() const noexcept
    {
        return dataSize() * sizeof( uint8_t ) + dataWithMarkersSize() * sizeof( uint16_t );
    }

    void
    applyWindow( WindowView const& window );

    /**
     * Returns the last 32 KiB decoded bytes. This can be called after decoding a block has finished
     * and then can be used to store and load it with deflate::Block::setInitialWindow to restart decoding
     * with the next block. Because this is not supposed to be called very often, it returns a copy of
     * the data instead of views.
     */
    [[nodiscard]] std::vector<std::uint8_t>
    getLastWindow( WindowView const& previousWindow ) const;

    /**
     * @param skipBytes The number of bits to shift the previous window and fill it with new data.
     *        A value of 0 would simply return @p previousWindow while a value equal to size() would return
     *        the window as it would be after this whole block.
     * @note Should only be called after @ref applyWindow because @p skipBytes larger than @ref dataSize will throw.
     */
    [[nodiscard]] std::vector<std::uint8_t>
    getWindowAt( WindowView const& previousWindow,
                 size_t            skipBytes ) const;

    void
    shrinkToFit()
    {
        for ( auto& container : data ) {
            container.shrink_to_fit();
        }
        for ( auto& container : dataWithMarkers ) {
            container.shrink_to_fit();
        }
    }

    /**
     * Check decoded blocks that account for possible markers whether they actually contain markers and if not so
     * convert and move them to actual decoded data.
     */
    void
    cleanUnmarkedData();

public:
    size_t encodedOffsetInBits{ std::numeric_limits<size_t>::max() };
    size_t encodedSizeInBits{ 0 };

    /**
     * Use vectors of vectors to avoid reallocations. The order of this data is:
     * - @ref dataWithMarkers (front to back)
     * - @ref data (front to back)
     * This order is fixed because there should be no reason for markers after we got enough data without markers!
     * There is no append( DecodedData ) method because this property might not be retained after using
     * @ref cleanUnmarkedData.
     */
    std::vector<std::vector<uint16_t> > dataWithMarkers;
    std::vector<std::vector<uint8_t> > data;
};


inline void
DecodedData::append( DecodedDataView const& buffers )
{
    if ( buffers.dataWithMarkersSize() > 0 ) {
        if ( !data.empty() ) {
            throw std::invalid_argument( "It is not allowed to append data with markers when fully decoded data "
                                         "has already been appended because the ordering will be wrong!" );
        }

        auto& copied = dataWithMarkers.emplace_back();
        copied.reserve( buffers.dataWithMarkersSize() );
        for ( const auto& buffer : buffers.dataWithMarkers ) {
            copied.insert( copied.end(), buffer.begin(), buffer.end() );
        }
    }

    if ( buffers.dataSize() > 0 ) {
        auto& copied = data.emplace_back();
        copied.reserve( buffers.dataSize() );
        for ( const auto& buffer : buffers.data ) {
            copied.insert( copied.end(), buffer.begin(), buffer.end() );
        }
    }
}


inline void
DecodedData::applyWindow( WindowView const& window )
{
    const auto markerCount = dataWithMarkersSize();
    if ( markerCount == 0 ) {
        dataWithMarkers.clear();
        return;
    }

    /* Because of the overhead of copying the window, avoid it for small replacements. */
    if ( markerCount >= 128_Ki ) {
        const std::array<uint8_t, 64_Ki> fullWindow =
            [&window] () noexcept
            {
                std::array<uint8_t, 64_Ki> result{};
                std::iota( result.begin(), result.begin() + 256, 0 );
                std::copy( window.begin(), window.end(), result.begin() + MAX_WINDOW_SIZE );
                return result;
            }();

        std::vector<uint8_t> downcasted( markerCount );
        size_t offset{ 0 };
        for ( auto& chunk : dataWithMarkers ) {
            std::transform( chunk.begin(), chunk.end(), downcasted.begin() + offset,
                            [&fullWindow] ( const auto value ) constexpr noexcept { return fullWindow[value]; } );
            offset += chunk.size();
        }

        data.insert( data.begin(), std::move( downcasted ) );
        dataWithMarkers.clear();
        return;
    }

    std::vector<uint8_t> downcasted( markerCount );
    size_t offset{ 0 };

    /* For maximum size windows, we can skip one check because even UINT16_MAX is valid. */
    static_assert( std::numeric_limits<uint16_t>::max() - MAX_WINDOW_SIZE + 1U == MAX_WINDOW_SIZE );
    if ( window.size() >= MAX_WINDOW_SIZE ) {
        const MapMarkers<true> mapMarkers( window );
        for ( auto& chunk : dataWithMarkers ) {
            std::transform( chunk.begin(), chunk.end(), downcasted.begin() + offset, mapMarkers );
            offset += chunk.size();
        }
    } else {
        const MapMarkers<false> mapMarkers( window );
        for ( auto& chunk : dataWithMarkers ) {
            std::transform( chunk.begin(), chunk.end(), chunk.begin(), mapMarkers );
            std::copy( chunk.begin(), chunk.end(), reinterpret_cast<std::uint8_t*>( downcasted.data() + offset ) );
            offset += chunk.size();
        }
    }

    data.insert( data.begin(), std::move( downcasted ) );
    dataWithMarkers.clear();
}


[[nodiscard]] inline std::vector<std::uint8_t>
DecodedData::getLastWindow( WindowView const& previousWindow ) const
{
    std::vector<std::uint8_t> window( MAX_WINDOW_SIZE, 0 );
    size_t nBytesWritten{ 0 };

    /* Fill the result from the back with data from our buffer. */
    for ( auto chunk = data.rbegin(); ( chunk != data.rend() ) && ( nBytesWritten < window.size() ); ++chunk ) {
        for ( auto symbol = chunk->rbegin(); ( symbol != chunk->rend() ) && ( nBytesWritten < window.size() );
              ++symbol, ++nBytesWritten )
        {
            window[window.size() - 1 - nBytesWritten] = *symbol;
        }
    }

    /* Fill the result from the back with data from our unresolved buffers. */
    const auto copyFromDataWithMarkers =
        [this, &window, &nBytesWritten] ( const auto& mapMarker )
        {
            for ( auto chunk = dataWithMarkers.rbegin();
                  ( chunk != dataWithMarkers.rend() ) && ( nBytesWritten < window.size() ); ++chunk )
            {
                for ( auto symbol = chunk->rbegin(); ( symbol != chunk->rend() ) && ( nBytesWritten < window.size() );
                      ++symbol, ++nBytesWritten )
                {
                    window[window.size() - 1 - nBytesWritten] = mapMarker( *symbol );
                }
            }
        };

    if ( previousWindow.size() >= MAX_WINDOW_SIZE ) {
        copyFromDataWithMarkers( MapMarkers</* full window */ true>( previousWindow ) );
    } else {
        copyFromDataWithMarkers( MapMarkers</* full window */ false>( previousWindow ) );
    }

    /* Fill the remaining part with the given window. This should only happen for very small DecodedData sizes. */
    if ( nBytesWritten < MAX_WINDOW_SIZE ) {
        const auto remainingBytes = MAX_WINDOW_SIZE - nBytesWritten;
        std::copy( std::reverse_iterator( previousWindow.end() ),
                   std::reverse_iterator( previousWindow.end() )
                   + std::min( remainingBytes, previousWindow.size() ),
                   window.rbegin() + nBytesWritten );
    }

    return window;
}


[[nodiscard]] inline std::vector<std::uint8_t>
DecodedData::getWindowAt( WindowView const& previousWindow,
                          size_t const      skipBytes) const
{
    if ( skipBytes > size() ) {
        throw std::invalid_argument( "Amount of bytes to skip is larger than this block!" );
    }

    std::vector<std::uint8_t> window( MAX_WINDOW_SIZE );
    size_t prefilled{ 0 };
    if ( skipBytes < MAX_WINDOW_SIZE ) {
        const auto lastBytesToCopyFromPrevious = MAX_WINDOW_SIZE - skipBytes;
        if ( lastBytesToCopyFromPrevious <= previousWindow.size() ) {
            for ( size_t j = previousWindow.size() - lastBytesToCopyFromPrevious; j < previousWindow.size();
                  ++j, ++prefilled )
            {
                window[prefilled] = previousWindow[j];
            }
            // prefilled = lastBytesToCopyFromPrevious = MAX_WINDOW_SIZE - skipBytes
        } else {
            /* If previousWindow.size() < MAX_WINDOW_SIZE, which might happen at the start of streams,
             * then behave as if previousWindow was padded with leading zeros. */
            const auto zerosToFill = lastBytesToCopyFromPrevious - previousWindow.size();
            for ( ; prefilled < zerosToFill; ++prefilled ) {
                window[prefilled] = 0;
            }

            for ( size_t j = 0; j < previousWindow.size(); ++j, ++prefilled ) {
                window[prefilled] = previousWindow[j];
            }
            // prefilled = lastBytesToCopyFromPrevious - previousWindow.size() + previousWindow.size()
        }
        assert( prefilled == MAX_WINDOW_SIZE - skipBytes );
    }

    const auto remainingBytes = window.size() - prefilled;

    /* Skip over skipBytes in data and then copy the last remainingBytes before it. */
    auto offset = skipBytes - remainingBytes;
    /* if skipBytes < MAX_WINDOW_SIZE
     *     offset = skipBytes - ( window.size() - ( MAX_WINDOW_SIZE - skipBytes ) ) = 0
     * if skipBytes >= MAX_WINDOW_SIZE
     *     offset = skipBytes - ( window.size() - 0 ) */

    const auto copyFromDataWithMarkers =
        [this, &offset, &prefilled, &window] ( const auto& mapMarker )
        {
            for ( auto& chunk : dataWithMarkers ) {
                if ( prefilled >= window.size() ) {
                    break;
                }

                if ( offset >= chunk.size() ) {
                    offset -= chunk.size();
                    continue;
                }

                for ( size_t i = offset; ( i < chunk.size() ) && ( prefilled < window.size() ); ++i, ++prefilled ) {
                    window[prefilled] = mapMarker( chunk[i] );
                }
                offset = 0;
            }
        };

    if ( previousWindow.size() >= MAX_WINDOW_SIZE ) {
        copyFromDataWithMarkers( MapMarkers</* full window */ true>( previousWindow ) );
    } else {
        copyFromDataWithMarkers( MapMarkers</* full window */ false>( previousWindow ) );
    }

    for ( auto& chunk : data ) {
        if ( prefilled >= window.size() ) {
            break;
        }

        if ( offset >= chunk.size() ) {
            offset -= chunk.size();
            continue;
        }

        for ( size_t i = offset; ( i < chunk.size() ) && ( prefilled < window.size() ); ++i, ++prefilled ) {
            window[prefilled] = chunk[i];
        }
        offset = 0;
    }

    return window;
}


inline void
DecodedData::cleanUnmarkedData()
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

    shrinkToFit();
}


#ifdef HAVE_IOVEC
[[nodiscard]] std::vector<::iovec>
toIoVec( const DecodedData& decodedData,
         const size_t       offsetInBlock,
         const size_t       dataToWriteSize )
{
    std::vector<::iovec> buffersToWrite;
    for ( auto it = pragzip::deflate::DecodedData::Iterator( decodedData, offsetInBlock, dataToWriteSize );
          static_cast<bool>( it ); ++it )
    {
        const auto& [data, size] = *it;
        ::iovec buffer;
        /* The const_cast should be safe because vmsplice and writev should not modify the input data. */
        buffer.iov_base = const_cast<void*>( reinterpret_cast<const void*>( data ) );;
        buffer.iov_len = size;
        buffersToWrite.emplace_back( buffer );
    }
    return buffersToWrite;
}
#endif  // HAVE_IOVEC
}  // namespace pragzip::deflate
