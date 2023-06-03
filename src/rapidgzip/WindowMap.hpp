#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <DecodedData.hpp>
#include <VectorView.hpp>


class WindowMap
{
public:
    using Window = rapidgzip::deflate::DecodedVector;
    using WindowView = VectorView<std::uint8_t>;

public:
    WindowMap() = default;

    void
    emplace( size_t encodedBlockOffset,
             Window window )
    {
        std::scoped_lock lock( m_mutex );
        const auto [match, wasInserted] = m_windows.try_emplace( encodedBlockOffset, std::move( window ) );
        if ( !wasInserted && ( match->second != window ) ) {
            throw std::invalid_argument( "Window data to insert is inconsistent and may not be changed!" );
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

private:
    mutable std::mutex m_mutex;

    /**
     * As soon as a window for an encoded block offset has been inserted it must contain valid data, i.e.,
     * actual data, often exactly deflate::MAX_WINDOW_SIZE, or either it is empty because now indow is required
     * because we are at the start of a gzip stream!
     */
    std::unordered_map</* encoded block offset */ size_t, Window> m_windows;
};
