#pragma once

#include <vector>
#include <stdexcept>


/**
 * Views are by their name read-only. This represents a read-only non-owned memory chunk.
 */
template<typename T>
class VectorView
{
public:
    using value_type = T;

public:
    VectorView() = default;
    VectorView( const VectorView& ) = default;
    VectorView( VectorView&& ) = default;
    VectorView& operator=( const VectorView& ) = default;
    VectorView& operator=( VectorView&& ) = default;

    constexpr
    VectorView( const std::vector<T>& vector ) noexcept :
        m_data( vector.data() ),
        m_size( vector.size() )
    {}

    constexpr
    VectorView( const T* data,
                size_t   size ) noexcept :
        m_data( data ),
        m_size( size )
    {}

    [[nodiscard]] T
    front() const
    {
        return *m_data;
    }

    [[nodiscard]] constexpr const T*
    begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr const T*
    end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr const T*
    data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr size_t
    size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr bool
    empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] T
    operator[]( size_t i ) const
    {
        return m_data[i];
    }

    [[nodiscard]] constexpr T
    at( size_t i ) const
    {
        if ( i >= m_size ) {
            throw std::out_of_range( "VectorView index larger than size!" );
        }
        return m_data[i];
    }

private:
    const T* m_data{ nullptr };
    size_t   m_size{ 0 };
};


/**
 * This is in basically a modifiable VectorView. The vector cannot be resized but
 * it is a simple pointer and length tuple to a read-write memory chunk.
 */
template<typename T>
class WeakVector
{
public:
    using value_type = T;

public:
    WeakVector() = default;
    WeakVector( const WeakVector& ) = default;
    WeakVector( WeakVector&& ) = default;
    WeakVector& operator=( const WeakVector& ) = default;
    WeakVector& operator=( WeakVector&& ) = default;

    constexpr
    WeakVector( const std::vector<T>& vector ) noexcept :
        m_data( vector.data() ),
        m_size( vector.size() )
    {}

    constexpr
    WeakVector( T*     data,
                size_t size ) noexcept :
        m_data( data ),
        m_size( size )
    {}

    [[nodiscard]] T
    front() const
    {
        return *m_data;
    }

    [[nodiscard]] constexpr const T*
    begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr T*
    begin() noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr const T*
    end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr T*
    end() noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr T*
    data() noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr const T*
    data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr size_t
    size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr bool
    empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] const T&
    operator[]( size_t i ) const
    {
        return m_data[i];
    }

    [[nodiscard]] T&
    operator[]( size_t i )
    {
        return m_data[i];
    }

    [[nodiscard]] constexpr T
    at( size_t i ) const
    {
        if ( i >= m_size ) {
            throw std::out_of_range( "VectorView index larger than size!" );
        }
        return m_data[i];
    }

private:
    T* m_data{ nullptr };
    size_t m_size{ 0 };
};


template<typename T,
         size_t   T_size>
class ArrayView
{
public:
    using value_type = T;

public:
    constexpr
    ArrayView( const std::array<T, T_size>& array ) noexcept :
        m_data( array.data() )
    {}

    constexpr
    ArrayView( const T* data ) noexcept :
        m_data( data )
    {}

    [[nodiscard]] T
    front() const
    {
        return *m_data;
    }

    [[nodiscard]] constexpr const T*
    begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr const T*
    end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr const T*
    data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr size_t
    size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr bool
    empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] T
    operator[]( size_t i ) const
    {
        return m_data[i];
    }

    [[nodiscard]] T
    at( size_t i ) const
    {
        if ( i >= m_size ) {
            throw std::out_of_range( "VectorView index larger than size!" );
        }
        return m_data[i];
    }

private:
    const T* const m_data;
    constexpr static size_t m_size = T_size;
};


template<typename T,
         size_t   T_size>
class WeakArray
{
public:
    using value_type = T;

public:
    constexpr
    WeakArray( T* data ) noexcept :
        m_data( data )
    {}

    [[nodiscard]] T
    front() const
    {
        return *m_data;
    }

    [[nodiscard]] constexpr const T*
    begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr const T*
    end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr const T*
    data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr T*
    data() noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr size_t
    size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr bool
    empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] const T&
    operator[]( size_t i ) const
    {
        return m_data[i];
    }

    [[nodiscard]] T&
    operator[]( size_t i )
    {
        return m_data[i];
    }

    [[nodiscard]] constexpr T
    at( size_t i ) const
    {
        if ( i >= m_size ) {
            throw std::out_of_range( "VectorView index larger than size!" );
        }
        return m_data[i];
    }

private:
    T* const m_data;
    constexpr static size_t m_size = T_size;
};
