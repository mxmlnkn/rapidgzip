#pragma once

#include <vector>
#include <stdexcept>


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

    VectorView( const std::vector<T>& vector ) noexcept :
        m_data( vector.data() ),
        m_size( vector.size() )
    {}

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

    [[nodiscard]] const T*
    begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] const T*
    end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] const T*
    data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] size_t
    size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] bool
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
    const T* m_data{ nullptr };
    size_t   m_size{ 0 };
};


template<typename T,
         size_t   T_size>
class ArrayView
{
public:
    using value_type = T;

public:
    ArrayView( const T* data ) noexcept :
        m_data( data )
    {}

    [[nodiscard]] T
    front() const
    {
        return *m_data;
    }

    [[nodiscard]] const T*
    begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] const T*
    end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] const T*
    data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr size_t
    size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] bool
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
