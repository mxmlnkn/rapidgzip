#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
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


/* Platform dependent stuff */

#ifdef _MSC_VER
    #include <io.h>

    #define fileno _fileno
    #define dup _dup
    #define fdopen _fdopen

    #include <sys/stat.h>

    #define S_IFIFO _S_IFIFO
    #define S_IFMT _S_IFMT

    template<typename FileMode, typename FileType>
    bool
    testFileType( FileMode fileMode,
                  FileType fileType )
    {
        return ( fileMode & S_IFMT ) == fileType;
    }

    #define S_ISFIFO(m) testFileType( m, S_IFIFO )

#else
    #include <unistd.h>
#endif


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


using unique_file_ptr = std::unique_ptr<std::FILE, std::function<void( std::FILE* )> >;

inline unique_file_ptr
make_unique_file_ptr( std::FILE* file )
{
    return unique_file_ptr( file, []( auto* ownedFile ){
        if ( ownedFile != nullptr ) {
            std::fclose( ownedFile );
        } } );
}

inline unique_file_ptr
make_unique_file_ptr( char const* const filePath,
                      char const* const mode )
{
    return make_unique_file_ptr( std::fopen( filePath, mode ) );
}

inline unique_file_ptr
make_unique_file_ptr( int         fileDescriptor,
                      char const* mode )
{
    return make_unique_file_ptr( fdopen( fileDescriptor, mode ) );
}



inline unique_file_ptr
throwingOpen( const std::string& filePath,
              const char*        mode )
{
    if ( mode == nullptr ) {
        throw std::invalid_argument( "Mode must be a C-String and not null!" );
    }

    auto file = make_unique_file_ptr( filePath.c_str(), mode );
    if ( file == nullptr ) {
        std::stringstream msg;
        msg << "Opening file '" << filePath << "' with mode '" << mode << "' failed!";
        throw std::invalid_argument( msg.str() );
    }

    return file;
}


inline unique_file_ptr
throwingOpen( int         fileDescriptor,
              const char* mode )
{
    if ( mode == nullptr ) {
        throw std::invalid_argument( "Mode must be a C-String and not null!" );
    }

    auto file = make_unique_file_ptr( fileDescriptor, mode );
    if ( file == nullptr ) {
        std::stringstream msg;
        msg << "Opening file descriptor " << fileDescriptor << " with mode '" << mode << "' failed!";
        throw std::invalid_argument( msg.str() );
    }

    return file;
}


/** dup is not strong enough to be able to independently seek in the old and the dup'ed fd! */
std::string
fdFilePath( int fileDescriptor )
{
    std::stringstream filename;
    filename << "/dev/fd/" << fileDescriptor;
    return filename.str();
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
