#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <future>
#include <memory>
#include <ostream>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>


template<typename I1,
         typename I2,
         typename Enable = typename std::enable_if<
            std::is_integral<I1>::value &&
            std::is_integral<I2>::value
         >::type>
I1
ceilDiv( I1 dividend,
         I2 divisor )
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
    out << "{ ";
    for ( const auto value : vector ) {
        out << value << ", ";
    }
    out << " }";
    return out;
}


template <typename S, typename T>
inline bool
startsWith( const S& fullString,
            const T& prefix,
            bool     caseSensitive )
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
inline bool
endsWith( const S& fullString,
          const T& suffix,
          bool     caseSensitive )
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


inline bool
fileExists( const char* filePath )
{
    return std::ifstream( filePath ).good();
}


using unique_file_ptr = std::unique_ptr<FILE, std::function<void( FILE* )> >;

inline unique_file_ptr
make_unique_file_ptr( char const* const filePath,
                      char const* const mode )
{
    return unique_file_ptr( fopen( filePath, mode ), []( FILE* file ){ fclose( file ); } ); // NOLINT
}


inline std::chrono::time_point<std::chrono::high_resolution_clock>
now()
{
    return std::chrono::high_resolution_clock::now();
}


/**
 * @return duration in seconds
 */
template<typename T0, typename T1>
double
duration( const T0& t0,
          const T1& t1 )
{
    return std::chrono::duration<double>( t1 - t0 ).count();
}


/**
 * Use like this: std::cerr << ( ThreadSafeOutput() << "Hello" << i << "there" ).str();
 */
class ThreadSafeOutput
{
public:
    ThreadSafeOutput()
    {
        m_out << "[" << std::this_thread::get_id() << "]";
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

    std::string
    str() const
    {
        return m_out.str() + "\n";
    }

private:
    std::stringstream m_out;
};


inline std::string
toString( std::future_status status )
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
