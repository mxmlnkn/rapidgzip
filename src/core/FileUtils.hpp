#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <sys/stat.h>

#ifdef _MSC_VER
    #define NOMINMAX
    #include <Windows.h>
#else
    #ifndef _GNU_SOURCE
        #define _GNU_SOURCE
    #endif

    #include <errno.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/poll.h>
    #include <unistd.h>

    #ifndef HAVE_VMSPLICE
        #define HAVE_VMSPLICE __linux__
    #endif
#endif


#ifdef _MSC_VER
[[nodiscard]] bool
stdinHasInput()
{
    const auto handle = GetStdHandle( STD_INPUT_HANDLE );
    DWORD bytesAvailable{ 0 };
    const auto success = PeekNamedPipe( handle, nullptr, 0, nullptr, &bytesAvailable, nullptr );
    return ( success == 0 ) && ( bytesAvailable > 0 );
}


[[nodiscard]] bool
stdoutIsDevNull()
{
    /**
     * @todo Figure this out on Windows in a reasonable readable manner:
     * @see https://stackoverflow.com/a/21070689/2191065
     */
    return false;
}

#else

[[nodiscard]] bool
stdinHasInput()
{
    pollfd fds{};
    fds.fd = STDIN_FILENO;
    fds.events = POLLIN;
    return poll( &fds, 1, /* timeout in ms */ 0 ) == 1;
}


[[nodiscard]] bool
stdoutIsDevNull()
{
    struct stat devNull{};
    struct stat stdOut{};
    return ( fstat( STDOUT_FILENO, &stdOut ) == 0 ) &&
           ( stat( "/dev/null", &devNull ) == 0 ) &&
           S_ISCHR( stdOut.st_mode ) &&  // NOLINT
           ( devNull.st_dev == stdOut.st_dev ) &&
           ( devNull.st_ino == stdOut.st_ino );
}
#endif


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


using unique_file_ptr = std::unique_ptr<std::FILE, std::function<void ( std::FILE* )> >;

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
        throw std::invalid_argument( std::move( msg ).str() );
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
        throw std::invalid_argument( std::move( msg ).str() );
    }

    return file;
}


/** dup is not strong enough to be able to independently seek in the old and the dup'ed fd! */
[[nodiscard]] std::string
fdFilePath( int fileDescriptor )
{
    std::stringstream filename;
    filename << "/dev/fd/" << fileDescriptor;
    return filename.str();
}


#ifndef __APPLE_CC__  // Missing std::filesytem::path support in wheels
[[nodiscard]] std::string
findParentFolderContaining( const std::string& folder,
                            const std::string& relativeFilePath )
{
    auto parentFolder = std::filesystem::absolute( folder );
    while ( !parentFolder.empty() )
    {
        const auto filePath = parentFolder / relativeFilePath;
        if ( std::filesystem::exists( filePath ) ) {
            return parentFolder.string();
        }

        if ( parentFolder.parent_path() == parentFolder ) {
            break;
        }
        parentFolder = parentFolder.parent_path();
    }

    return {};
}
#endif


/**
 * @note Throws if some splice calls were successful followed by an unsucessful one before finishing.
 * @return true if successful and false if it could not be spliced from the beginning, e.g., because the file
 *         descriptor is not a pipe.
 */
[[nodiscard]] bool
writeAllSplice( const int         outputFileDescriptor,
                const void* const dataToWrite,
                const size_t      dataToWriteSize )
{
#if HAVE_VMSPLICE
    /**
     * Short overview of syscalls that optimize copies by instead copying full page pointers into the
     * pipe buffers inside the kernel:
     * - splice: <fd (pipe or not)> <-> <pipe>
     * - vmsplice: memory -> <pipe>
     * - mmap: <fd> -> memory
     * - sendfile: <fd that supports mmap> -> <fd (before Linux 2.6.33 (2010-02-24) it had to be a socket fd)>
     */
    ::iovec dataToSplice{};
    dataToSplice.iov_base = const_cast<void*>( reinterpret_cast<const void*>( dataToWrite ) );
    dataToSplice.iov_len = dataToWriteSize;
    while ( dataToSplice.iov_len > 0 ) {
        /* The const_cast should be safe because vmsplice should not modify the input data. */
        const auto nBytesWritten = ::vmsplice( outputFileDescriptor, &dataToSplice, 1, /* flags */ 0 );
        if ( nBytesWritten < 0 ) {
            if ( dataToSplice.iov_len == dataToWriteSize ) {
                return false;
            }
            std::cerr << "error: " << errno << "\n";
            throw std::runtime_error( "Failed to write to pipe" );
        }
        dataToSplice.iov_base = reinterpret_cast<char*>( dataToSplice.iov_base ) + nBytesWritten;
        dataToSplice.iov_len -= nBytesWritten;
    }
    return true;
#else
    return false;
#endif
}


/**
 * Posix write is not guaranteed to write everything and in fact was encountered to not write more than
 * 0x7ffff000 (2'147'479'552) B. To avoid this, it has to be looped over.
 */
void
writeAllToFd( const int         outputFileDescriptor,
              const void* const dataToWrite,
              const size_t      dataToWriteSize )
{
    for ( uint64_t nTotalWritten = 0; nTotalWritten < dataToWriteSize; ) {
        const auto currentBufferPosition =
            reinterpret_cast<const void*>( reinterpret_cast<uintptr_t>( dataToWrite ) + nTotalWritten );
        const auto nBytesWritten = ::write( outputFileDescriptor,
                                            currentBufferPosition,
                                            dataToWriteSize - nTotalWritten );
        if ( nBytesWritten <= 0 ) {
            std::stringstream message;
            message << "Unable to write all data to the given file descriptor. Wrote " << nTotalWritten << " out of "
                    << dataToWriteSize << ".";
            throw std::runtime_error( std::move( message ).str() );
        }
        nTotalWritten += static_cast<size_t>( nBytesWritten );
    }
}


void
writeAll( const int         outputFileDescriptor,
          void* const       outputBuffer,
          const void* const dataToWrite,
          const size_t      dataToWriteSize )
{
    if ( dataToWriteSize == 0 ) {
        return;
    }

    if ( outputFileDescriptor >= 0 ) {
        if ( !writeAllSplice( outputFileDescriptor, dataToWrite, dataToWriteSize ) ) {
            writeAllToFd( outputFileDescriptor, dataToWrite, dataToWriteSize );
        }
    }

    if ( outputBuffer != nullptr ) {
        std::memcpy( outputBuffer, dataToWrite, dataToWriteSize );
    }
}
