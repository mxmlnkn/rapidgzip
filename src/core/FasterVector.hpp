#pragma once

#include <vector>


#ifdef WITH_RPMALLOC
    #include <rpmalloc.h>


class RpmallocInit
{
public:
    RpmallocInit()
    {
        rpmalloc_initialize();
    }

    ~RpmallocInit()
    {
        rpmalloc_finalize();
    }
};


/* It must be the very first static variable so that rpmalloc is initialized before any usage of malloc
 * when overriding operator new (when including rpnew.h). And, this only works if everything is header-only
 * because else the static variable initialization order becomes undefined across different compile units.
 * That's why we avoid overriding operators new and delete and simply use it as a custom allocator in the
 * few places we know to be performance-critical */
inline static const RpmallocInit rpmallocInit{};


template<typename ElementType>
class RpmallocAllocator
{
public:
    using value_type = ElementType;

    using is_always_equal = std::true_type;

public:
    [[nodiscard]] constexpr ElementType*
    allocate( std::size_t nElementsToAllocate )
    {
        if ( nElementsToAllocate > std::numeric_limits<std::size_t>::max() / sizeof( ElementType ) ) {
            throw std::bad_array_new_length();
        }

        auto const nBytesToAllocate = nElementsToAllocate * sizeof( ElementType );
        return reinterpret_cast<ElementType*>( rpmalloc( nBytesToAllocate ) );
    }

    constexpr void
    deallocate( ElementType*                 allocatedPointer,
                [[maybe_unused]] std::size_t nElementsAllocated )
    {
        rpfree( allocatedPointer );
    }

    /* I don't understand why this is still necessary even with is_always_equal = true_type.
     * Defining it to true type should be necessary because the default implementation of
     * is_always_equal == is_empty should also be true because this class does not have any members. */
    [[nodiscard]] bool
    operator==( const RpmallocAllocator& ) const
    {
        return true;
    }

    [[nodiscard]] bool
    operator!=( const RpmallocAllocator& ) const
    {
        return false;
    }
};


static_assert( std::is_empty_v<RpmallocAllocator<char> > );


template<typename T>
using FasterVector = std::vector<T, RpmallocAllocator<T> >;

#else

template<typename T>
using FasterVector = std::vector<T>;

#endif
