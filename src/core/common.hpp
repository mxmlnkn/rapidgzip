#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef WITH_PYTHON_SUPPORT
    #include <Python.h>
#endif

#include "FileUtils.hpp"
#include "VectorView.hpp"


/* Platform dependent stuff */

#ifdef _MSC_VER
    #include <io.h>

    /* MSVC still has a fileno alias even though it is deprecated. Using define is not a good idea because some
     * methods are named fileno! Instead, disable the deprecation warning for this and if a more recent compiler
     * removes the alias, then use: const auto fileno = _fileno; */
    #pragma warning(disable : 4996)

    #include <sys/stat.h>

    #define S_IFIFO _S_IFIFO
    #define S_IFMT _S_IFMT

    template<typename FileMode, typename FileType>
    [[nodiscard]] bool
    testFileType( FileMode fileMode,
                  FileType fileType )
    {
        return ( fileMode & S_IFMT ) == fileType;
    }

    #define S_ISFIFO(m) testFileType( m, S_IFIFO )
#else
    #include <unistd.h>
#endif


#if defined( _MSC_VER )
    #define forceinline __forceinline
#elif defined(__clang__) || defined(__GNUC__)
    /* https://stackoverflow.com/questions/38499462/how-to-tell-clang-to-stop-pretending-to-be-other-compilers */
    #define forceinline __attribute__((always_inline))
#endif

template<typename I1,
         typename I2,
         typename Enable = typename std::enable_if<
             std::is_integral<I1>::value &&
             std::is_integral<I2>::value
         >::type>
[[nodiscard]] constexpr I1
ceilDiv( I1 dividend,
         I2 divisor ) noexcept
{
    return ( dividend + divisor - 1 ) / divisor;
}


template<typename S, typename T>
std::ostream&
operator<<( std::ostream&  out,
            std::pair<S,T> pair )
{
    out << "(" << pair.first << "," << pair.second << ")";
    return out;
}


template<typename T>
std::ostream&
operator<<( std::ostream&  out,
            std::vector<T> vector )
{
    if ( vector.empty() ) {
        out << "{}";
        return out;
    }

    out << "{ " << vector.front();
    for ( auto value = std::next( vector.begin() ); value != vector.end(); ++value ) {
        out << ", " << *value;
    }
    out << " }";

    return out;
}


template <typename S, typename T>
[[nodiscard]] constexpr bool
startsWith( const S& fullString,
            const T& prefix,
            bool     caseSensitive = true ) noexcept
{
    if ( fullString.size() < prefix.size() ) {
        return false;
    }

    if ( caseSensitive ) {
        return std::equal( prefix.begin(), prefix.end(), fullString.begin() );
    }

    return std::equal( prefix.begin(), prefix.end(), fullString.begin(),
                       [] ( auto a, auto b ) { return std::tolower( a ) == std::tolower( b ); } );
}


template <typename S, typename T>
[[nodiscard]] constexpr bool
endsWith( const S& fullString,
          const T& suffix,
          bool     caseSensitive = true ) noexcept
{
    if ( fullString.size() < suffix.size() ) {
        return false;
    }

    if ( caseSensitive ) {
        return std::equal( suffix.rbegin(), suffix.rend(), fullString.rbegin() );
    }

    return std::equal( suffix.rbegin(), suffix.rend(), fullString.rbegin(),
                       [] ( auto a, auto b ) { return std::tolower( a ) == std::tolower( b ); } );
}


[[nodiscard]] std::string
formatBits( const uint64_t value )
{
    return std::to_string( value / 8 ) + " B " + std::to_string( value % 8 ) + " b";
}


[[nodiscard]] std::chrono::time_point<std::chrono::high_resolution_clock>
now() noexcept
{
    return std::chrono::high_resolution_clock::now();
}


/**
 * @return duration in seconds
 */
template<typename T>
[[nodiscard]] double
duration( const T& t0,
          const T& t1 = now() ) noexcept
{
    return std::chrono::duration<double>( t1 - t0 ).count();
}


[[nodiscard]] uint64_t
unixTime() noexcept
{
    const auto currentTime = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>( std::chrono::duration_cast<std::chrono::seconds>( currentTime ).count() );
}


/**
 * Use like this: std::cerr << ( ThreadSafeOutput() << "Hello" << i << "there" ).str();
 */
class ThreadSafeOutput
{
public:
    ThreadSafeOutput()
    {
        using namespace std::chrono;
        const auto time = system_clock::now();
        const auto timePoint = system_clock::to_time_t( time );
        const auto subseconds = duration_cast<milliseconds>( time.time_since_epoch() ).count() % 1000;
        m_out << "[" << std::put_time( std::localtime( &timePoint ), "%H:%M:%S" ) << "." << subseconds << "]"
              << "[" << std::this_thread::get_id() << "]";
    }

    template<typename T>
    ThreadSafeOutput&
    operator<<( const T& value )
    {
        m_out << " " << value;
        return *this;
    }

    operator std::string() const
    {
        return m_out.str() + "\n";
    }

    [[nodiscard]] std::string
    str() const
    {
        return m_out.str() + "\n";
    }

private:
    std::stringstream m_out;
};


std::ostream&
operator<<( std::ostream&           out,
            const ThreadSafeOutput& output )
{
    out << output.str();
    return out;
}


[[nodiscard]] std::string
toString( std::future_status status ) noexcept
{
    switch ( status )
    {
    case std::future_status::ready:
        return "ready";
    case std::future_status::deferred:
        return "deferred";
    case std::future_status::timeout:
        return "timeout";
    }
    return "unknown future states";
}


/**
 * RAII based notify at the end of a scope, which will also be triggered e.g. when throwing exceptions!
 * std::notify_all_at_thread_exit is no alternative to this because the thread does not exit because we are using
 * a thread pool but we might still want to notify someone when the packaged task throws.
 */
class FinallyNotify
{
public:
    explicit
    FinallyNotify( std::condition_variable& toNotify ) :
        m_toNotify( toNotify )
    {}

    ~FinallyNotify()
    {
        m_toNotify.notify_all();
    }

private:
    std::condition_variable& m_toNotify;
};


/* Minimal Testing Helpers. */

int gnTests = 0;  // NOLINT
int gnTestErrors = 0;  // NOLINT


template<typename T>
void
requireEqual( const T& a, const T& b, int line )
{
    ++gnTests;
    if ( a != b ) {
        ++gnTestErrors;
        std::cerr << "[FAIL on line " << line << "] " << a << " != " << b << "\n";
    }
}


void
require( bool               condition,
         std::string const& conditionString,
         int                line )
{
    ++gnTests;
    if ( !condition ) {
        ++gnTestErrors;
        std::cerr << "[FAIL on line " << line << "] " << conditionString << "\n";
    }
}


#define REQUIRE_EQUAL( a, b ) requireEqual( a, b, __LINE__ ) // NOLINT
#define REQUIRE( condition ) require( condition, #condition, __LINE__ ) // NOLINT


template<typename T>
[[nodiscard]] constexpr typename T::value_type
getMinPositive( const T& container )
{
    if ( container.empty() ) {
        throw std::invalid_argument( "Container must not be empty!" );
    }

    auto result = container.front();
    for ( const auto value : container ) {
        if ( value > 0 ) {
            if ( result > 0 ) {
                result = std::min( result, value );
            } else {
                result = value;
            }
        }
    }
    return result;
}


template<typename T>
[[nodiscard]] constexpr typename T::value_type
getMax( const T& container )
{
    const auto match = std::max_element( container.begin(), container.end() );
    if ( match == container.end() ) {
        throw std::invalid_argument( "Container must not be empty!" );
    }
    return *match;
}


template<typename Container,
         typename Value>
[[nodiscard]] constexpr bool
contains( const Container& container,
          Value            value )
{
    return std::find( container.begin(), container.end(), value ) != container.end();
}


template<typename Iterator,
         typename T = const decltype( *std::declval<Iterator>() )&>
[[nodiscard]] constexpr size_t
countAdjacentIf( const Iterator rangeBegin,
                 const Iterator rangeEnd,
                 const std::function<bool( const decltype( *std::declval<Iterator>() )&,
                                           const decltype( *std::declval<Iterator>() )& )>& equal )
{
    size_t result{ 0 };
    if ( rangeBegin != rangeEnd ) {
        for ( auto it = rangeBegin, nit = std::next( it ); nit != rangeEnd; ++it, ++nit ) {
            if ( equal( *it, *nit ) ) {
                ++result;
            }
        }
    }
    return result;
}


/**
 * Returns iterators to the first sequence of elements that are increasing by one each.
 */
template<typename Iterator>
[[nodiscard]] constexpr std::pair<Iterator, Iterator>
findAdjacentIf( const Iterator rangeBegin,
                const Iterator rangeEnd,
                const std::function<bool( const decltype( *std::declval<Iterator>() )&,
                                          const decltype( *std::declval<Iterator>() )& )>& equal  )
{
    auto sequenceBegin = rangeEnd;
    if ( rangeBegin != rangeEnd ) {
        for ( auto it = rangeBegin, nit = std::next( it ); nit != rangeEnd; ++it, ++nit ) {
            if ( equal( *it, *nit ) ) {
                if ( sequenceBegin == rangeEnd ) {
                    sequenceBegin = it;
                }
            } else if ( sequenceBegin != rangeEnd ) {
                return { sequenceBegin, nit };
            }
        }
    }
    return { sequenceBegin, rangeEnd };
}


template<typename Container>
[[nodiscard]] Container
interleave( const std::vector<Container>& values )
{
    const auto size = std::accumulate( values.begin(), values.end(), size_t{ 0 },
                                       [] ( size_t sum, const auto& container ) { return sum + container.size(); } );
    Container result;
    result.reserve( size );
    for ( size_t i = 0; i < size; ++i ) {
        for ( const auto& container : values ) {
            if ( i < container.size() ) {
                result.emplace_back( container[i] );
            }
        }
    }
    return result;
}


[[nodiscard]] inline bool
testFlags( const uint64_t value,
           const uint64_t flags )
{
    return ( value & flags ) != 0;
}


/* error: 'std::filesystem::path' is unavailable: introduced in macOS 10.15.
 * Fortunately, this is only needed for the tests, so the incomplete std::filesystem support
 * is not a problem for building the manylinux wheels on the pre 10.15 macos kernel. */
#ifndef __APPLE_CC__
void
createRandomTextFile( std::filesystem::path path,
                      size_t                size )
{
    std::ofstream textFile( path );
    for ( size_t i = 0; i < size; ++i ) {
        const auto c = i % 80 == 0 ? '\n' : 'A' + ( rand() % ( 'Z' - 'A' ) );
        textFile << static_cast<char>( c );
    }
}


class TemporaryDirectory
{
public:
    explicit
    TemporaryDirectory( std::filesystem::path path ) :
        m_path( std::move( path ) )
    {}

    TemporaryDirectory( TemporaryDirectory&& ) = default;
    TemporaryDirectory( const TemporaryDirectory& ) = delete;
    TemporaryDirectory& operator=( TemporaryDirectory&& ) = default;
    TemporaryDirectory& operator=( const TemporaryDirectory& ) = delete;

    ~TemporaryDirectory()
    {
        if ( !m_path.empty() ) {
            std::filesystem::remove_all( m_path );
        }
    }

    operator std::filesystem::path() const
    {
        return m_path;
    }

    const std::filesystem::path&
    path() const
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};
#endif


#if defined(__GNUC__)
    #define LIKELY(x) (__builtin_expect(static_cast<bool>(x),1))
    #define UNLIKELY(x) (__builtin_expect(static_cast<bool>(x),0))
#else
    #define LIKELY(x) (x)
    #define UNLIKELY(x) (x)
#endif


enum class Endian
{
    LITTLE,
    BIG,
    UNKNOWN,
};


/**
 * g++-dM -E -x c++ /dev/null | grep -i endian
 * > #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
 * clang++ -dM -E -x c++ /dev/null | grep -i little
 * > #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
 */
constexpr Endian ENDIAN =
#if defined(__BYTE_ORDER__) && defined( __ORDER_LITTLE_ENDIAN__ ) && ( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )
    Endian::LITTLE
#else
    Endian::UNKNOWN
#endif
;


/**
 * This should compile to a single load on modern compilers instead of a function call. Test e.g. on godbolt.org.
 * Note that we cannot use reinterpret_cast from char* to uint64_t* because it would result in unedefined behavior
 * because of strict-aliasing rules! @see https://en.cppreference.com/w/cpp/language/reinterpret_cast#Type_aliasing
 */
template<typename T>
[[nodiscard]] constexpr T
loadUnaligned( const void* data )
{
	T result{ 0 };
	std::memcpy( &result, data, sizeof( result ) );
	return result;
}
