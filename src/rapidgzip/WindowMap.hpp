#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <DecodedData.hpp>
#include <VectorView.hpp>


class WindowMap
{
public:
    using Window = rapidgzip::deflate::DecodedVector;
    using WindowView = VectorView<std::uint8_t>;
    using Windows = std::map</* encoded block offset */ size_t, Window>;

public:
    WindowMap() = default;

    explicit
    WindowMap( const WindowMap& other )
    {
        const auto [lock, windows] = other.data();
        m_windows = *windows;
    }

    void
    emplace( size_t encodedBlockOffset,
             Window window )
    {
        std::scoped_lock lock( m_mutex );
        if ( m_windows.empty() ) {
            m_windows.emplace( encodedBlockOffset, std::move( window ) );
        } else if ( m_windows.rbegin()->first < encodedBlockOffset ) {
            /* Last value is smaller, so it is given that there is no collision and we can "append"
             * the new value with a hint in constant time. This should be the common case as windows
             * should be inserted in order of the offset! */
            m_windows.emplace_hint( m_windows.end(), encodedBlockOffset, std::move( window ) );
        } else {
            const auto match = m_windows.find( encodedBlockOffset );
            if ( ( match != m_windows.end() ) && ( match->second != window ) ) {
                throw std::invalid_argument( "Window data to insert is inconsistent and may not be changed!" );
            }
            m_windows.emplace( encodedBlockOffset, std::move( window ) );
        }
    }

    [[nodiscard]] std::optional<WindowView>
    get( size_t encodedOffsetInBits ) const
    {
        /* Note that insertions might invalidate iterators but not references to values and especially not the
         * internal pointers of the vectors we are storing in the values. Meaning, it is safe to simply return
         * a WindowView without a corresponding lock. */
        std::scoped_lock lock( m_mutex );
        if ( const auto match = m_windows.find( encodedOffsetInBits ); match != m_windows.end() ) {
            return WindowView( match->second.data(), match->second.size() );
        }
        return std::nullopt;
    }

    [[nodiscard]] bool
    empty() const
    {
        std::scoped_lock lock( m_mutex );
        return m_windows.empty();
    }

    void
    releaseUpTo( size_t encodedOffset )
    {
        std::scoped_lock lock( m_mutex );
        auto start = m_windows.begin();
        auto end = start;
        while ( ( end != m_windows.end() ) && ( end->first < encodedOffset ) ) {
            ++end;
        }
        m_windows.erase( start, end );
    }

    [[nodiscard]] std::pair<std::unique_lock<std::mutex>, Windows*>
    data()
    {
        return { std::unique_lock( m_mutex ), &m_windows };
    }

    [[nodiscard]] std::pair<std::unique_lock<std::mutex>, const Windows*>
    data() const
    {
        return { std::unique_lock( m_mutex ), &m_windows };
    }

    [[nodiscard]] size_t
    size() const
    {
        std::scoped_lock lock( m_mutex );
        return m_windows.size();
    }

    [[nodiscard]] bool
    operator==( const WindowMap& other ) const
    {
        std::scoped_lock lock( m_mutex );
        return m_windows == other.m_windows;
    }

private:
    mutable std::mutex m_mutex;

    /**
     * As soon as a window for an encoded block offset has been inserted it must contain valid data, i.e.,
     * actual data, often exactly deflate::MAX_WINDOW_SIZE, or either it is empty because no window is required
     * because we are at the start of a gzip stream!
     * Initially, this was std::unordered_map to ensure O(1) insertion speed.
     * However, this makes releaseUpTo take a possibly very long time after an index has been imported.
     * Using std::map with insertion/emplace hint also can achieve O(1) and according to benchmarks can even
     * be ~20% faster than unordered_map when all those emplace hints are perfect.
     * This should normally be the case because windows should be inserted in order as are the offsets,
     * i.e., the hint can always be end():
     */
    Windows m_windows;
};
