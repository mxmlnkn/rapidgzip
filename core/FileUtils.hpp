#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>


inline bool
fileExists( const std::string& filePath )
{
    return std::ifstream( filePath ).good();
}


inline size_t
fileSize( const std::string& filePath )
{
    std::ifstream file( filePath );
    file.seekg( 0, std::ios_base::end );
    return file.tellg();
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
