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
void
replaceMarkerBytes( WeakVector<std::uint16_t>  buffer,
                    VectorView<uint8_t> const& window )
{
    const auto mapMarker =
        [&window] ( uint16_t value )
        {
            if ( ( value > std::numeric_limits<uint8_t>::max() )
                 && ( value < MAX_WINDOW_SIZE ) )
            {
                throw std::invalid_argument( "Cannot replace unknown 2 B code!" );
            }

            if ( ( value >= MAX_WINDOW_SIZE ) && ( value - MAX_WINDOW_SIZE >= window.size() ) ) {
                throw std::invalid_argument( "Window too small!" );
            }

            return value >= MAX_WINDOW_SIZE ? window[value - MAX_WINDOW_SIZE] : static_cast<uint8_t>( value );
        };

    std::transform( buffer.begin(), buffer.end(), buffer.begin(), mapMarker );
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

public:
    void
    append( std::vector<uint8_t>&& toAppend )
    {
        if ( !toAppend.empty() ) {
            data.emplace_back( std::move( toAppend ) );
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

    void
    applyWindow( WindowView const& window );

    /**
     * Returns the last 32 KiB decoded bytes. This can be called after decoding a block has finished
     * and then can be used to store and load it with deflate::Block::setInitialWindow to restart decoding
     * with the next block. Because this is not supposed to be called very often, it returns a copy of
     * the data instead of views.
     */
    [[nodiscard]] std::array<std::uint8_t, MAX_WINDOW_SIZE>
    getLastWindow( WindowView const& previousWindow ) const;

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

        auto& copied = dataWithMarkers.empty() ? dataWithMarkers.emplace_back() : dataWithMarkers.back();
        for ( const auto& buffer : buffers.dataWithMarkers ) {
            copied.insert( copied.end(), buffer.begin(), buffer.end() );
        }
    }

    if ( buffers.dataSize() > 0 ) {
        auto& copied = data.empty() ? data.emplace_back() : data.back();
        for ( const auto& buffer : buffers.data ) {
            copied.insert( copied.end(), buffer.begin(), buffer.end() );
        }
    }
}


inline void
DecodedData::applyWindow( WindowView const& window )
{
    if ( dataWithMarkersSize() == 0 ) {
        dataWithMarkers.clear();
        return;
    }

    std::vector<uint8_t> downcasted( dataWithMarkersSize() );
    size_t offset{ 0 };
    for ( auto& chunk : dataWithMarkers ) {
        replaceMarkerBytes( &chunk, window );
        std::transform( chunk.begin(), chunk.end(), downcasted.begin() + offset,
                        [] ( const auto symbol ) { return static_cast<uint8_t>( symbol ); } );
        offset += chunk.size();
    }
    data.insert( data.begin(), std::move( downcasted ) );
    dataWithMarkers.clear();
}


[[nodiscard]] inline std::array<std::uint8_t, MAX_WINDOW_SIZE>
DecodedData::getLastWindow( WindowView const& previousWindow ) const
{
    if ( dataWithMarkersSize() > 0 ) {
        throw std::invalid_argument( "No valid window available. Please call applyWindow first!" );
    }

    std::array<std::uint8_t, MAX_WINDOW_SIZE> window{};
    size_t nBytesWritten{ 0 };

    /* Fill the result from the back with data from our buffer. */
    for ( auto chunk = data.rbegin(); ( chunk != data.rend() ) && ( nBytesWritten < window.size() ); ++chunk ) {
        for ( auto symbol = chunk->rbegin(); ( symbol != chunk->rend() ) && ( nBytesWritten < window.size() );
              ++symbol, ++nBytesWritten )
        {
            window[window.size() - 1 - nBytesWritten] = *symbol;
        }
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
}
}  // namespace pragzip::deflate
