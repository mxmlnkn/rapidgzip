#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>          // posix_fallocate
#include <sys/mman.h>

#include <AlignedAllocator.hpp>
#include <common.hpp>
#include <FileUtils.hpp>
#include <Statistics.hpp>
#include <ThreadPool.hpp>


using namespace rapidgzip;


/* Alignment to the filesystem block size is necessary for direct I/O. */
using DataBuffer = std::vector<char, AlignedAllocator<char, 4096> >;

constexpr uint64_t FILE_SIZE_TO_BENCHMARK = 1_Gi;
constexpr size_t REPEAT_COUNT = 10;


enum class FileInitialization
{
    EMPTY,
    ALLOCATE,
    TRUNCATE,
};


void
pwriteAllToFdVector( const int                   outputFileDescriptor,
                     const std::vector<::iovec>& dataToWrite,
                     size_t                      fileOffset )
{
    for ( size_t i = 0; i < dataToWrite.size(); ) {
        const auto segmentCount = std::min( static_cast<size_t>( IOV_MAX ), dataToWrite.size() - i );
        auto nBytesWritten = ::pwritev( outputFileDescriptor, &dataToWrite[i], segmentCount, fileOffset );

        if ( nBytesWritten < 0 ) {
            std::stringstream message;
            message << "Failed to write all bytes because of: " << strerror( errno ) << " (" << errno << ")";
            throw std::runtime_error( std::move( message ).str() );
        }

        fileOffset += nBytesWritten;

        /* Skip over buffers that were written fully. */
        for ( ; ( i < dataToWrite.size() ) && ( dataToWrite[i].iov_len <= static_cast<size_t>( nBytesWritten ) );
              ++i ) {
            nBytesWritten -= dataToWrite[i].iov_len;
        }

        /* Write out last partially written buffer if necessary so that we can resume full vectorized writing
         * from the next iovec buffer. */
        if ( ( i < dataToWrite.size() ) && ( nBytesWritten > 0 ) ) {
            const auto& iovBuffer = dataToWrite[i];

            assert( iovBuffer.iov_len < static_cast<size_t>( nBytesWritten ) );
            const auto remainingSize = iovBuffer.iov_len - nBytesWritten;
            const auto remainingData = reinterpret_cast<char*>( iovBuffer.iov_base ) + nBytesWritten;
            pwriteAllToFd( outputFileDescriptor, remainingData, remainingSize, fileOffset );
            fileOffset += remainingSize;

            ++i;
        }
    }
}


[[nodiscard]] const char*
toStringFile( const FileInitialization fileInitialization )
{
    switch ( fileInitialization )
    {
    case FileInitialization::EMPTY:
        return "an emptied file";
    case FileInitialization::ALLOCATE:
        return "a preallocated file";
    case FileInitialization::TRUNCATE:
        return "a sparsely allocated file";
    }

    throw std::invalid_argument( "Unknown enum value!" );
}


void
checkedFtruncate( int    fd,
                  size_t size )
{
    if ( ::ftruncate( fd, size ) == -1 ) {
        std::stringstream message;
        message << "Encountered error while truncating file: " << std::strerror( errno )
                << " (" << errno << ")\n";
        throw std::runtime_error( std::move( message ).str() );
    }
}


[[nodiscard]] unique_file_descriptor
openFile( const std::string&       filePath,
          const size_t             size,
          const FileInitialization fileInitialization )
{
    /* ftruncate( fd, 0 ) is not sufficient!!! At least not without closing and reopening the file it seems!
     * It will still yield the same results as a preallocated file! */
    if ( fileExists( filePath ) ) {
        std::filesystem::remove( filePath );
    }

    const auto fd = ::open( filePath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR );

    switch ( fileInitialization )
    {
    case FileInitialization::EMPTY:
        break;

    case FileInitialization::ALLOCATE:
        /* This preceded ftruncate is only in the hope to make posix_fallocate faster than 100 MB/s on BeeGFS. */
        checkedFtruncate( fd, size );
        if ( posix_fallocate( fd, /* offset */ 0, size ) == -1 ) {
            std::cerr << "Encountered error while truncating file: " << std::strerror( errno )
                      << " (" << errno << ")\n";
        }
        break;

    case FileInitialization::TRUNCATE:
        checkedFtruncate( fd, size );
        break;
    }

    return unique_file_descriptor( fd );
}


[[nodiscard]] std::string
formatBandwidth( const std::vector<double>& times,
                 const size_t               nBytes )
{
    std::vector<double> bandwidths( times.size() );
    std::transform( times.begin(), times.end(), bandwidths.begin(),
                    [nBytes] ( double time ) { return static_cast<double>( nBytes ) / time / 1e9; } );
    Statistics<double> bandwidthStats{ bandwidths };

    /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
     * physically possible but many noisy influences can slow things down, i.e., the minimum time is
     * the value closest to be free of noise. */
    std::stringstream result;
    if ( times.size() == 1 ) {
        /* std::fixed shows trailing 0, e.g., 0.500. */
        result << std::setprecision( 3 ) << std::fixed << bandwidthStats.min << " GB/s";
    } else {
        result << "( min: " << bandwidthStats.min << ", " << bandwidthStats.formatAverageWithUncertainty()
               << ", max: " << bandwidthStats.max << " ) GB/s";
    }
    return std::move( result ).str();
}


using BenchmarkFunction = std::function</* duration */ double()>;

[[nodiscard]] std::vector<double>
repeatBenchmarks( const BenchmarkFunction& toMeasure,
                  const size_t             repeatCount = REPEAT_COUNT )
{
    std::vector<double> times( repeatCount );
    for ( auto& time : times ) {
        time = toMeasure();
    }
    return times;
}


void
checkFileSize( const std::string& filePath,
               const size_t       size )
{
    if ( fileSize( filePath ) != size ) {
        std::stringstream message;
        message << "File has different size than expected! Got file size: " << formatBytes( fileSize( filePath ) )
                << ", expected: " << formatBytes( size )  << "\n";
        throw std::runtime_error( std::move( message ).str() );
    }
}


/* File Creation Benchmarks */


template<typename Functor>
void
benchmarkFileCreation( const std::string& filePath,
                       const Functor&     createFile,
                       const std::string& name )
{
    for ( const auto size : { 128_Mi, 512_Mi, 1_Gi, 2_Gi, 4_Gi } ) {
        const auto times = repeatBenchmarks(
            [&filePath, size, &createFile, &name] {
                if ( fileExists( filePath ) ) {
                    std::filesystem::remove( filePath );
                }

                const auto t0 = now();
                auto file = throwingOpen( filePath, "wb" );
                if ( createFile( ::fileno( file.get() ), size ) == -1 ) {
                    std::cerr << "Encountered error while calling " << name << " on file: " << std::strerror( errno )
                              << " (" << errno << ")\n";
                }
                file.reset();

                return duration( t0 );
            },
            REPEAT_COUNT );

        std::cout << "    " << name << " file sized " << formatBytes( size ) << ": " << formatBandwidth( times, size )
                  << "\n";

        if ( times.back() > 1 /* second */ ) {
            /* 1s for 128 MiB would be ~134 MB/s */
            break;
        }
    }

    std::cout << std::endl;
}


void
benchmarkTruncating( const std::string& filePath )
{
    benchmarkFileCreation( filePath, ::ftruncate, "ftruncate" );
}


void
benchmarkAllocating( const std::string& filePath )
{
    const auto allocate = [] ( int fd, size_t size ) { return ::posix_fallocate( fd, /* offset */ 0, size ); };
    benchmarkFileCreation( filePath, allocate, "posix_fallocate" );
}


void
benchmarkFAllocating( const std::string& filePath )
{
    const auto allocate =
        [] ( int fd, size_t size ) {
            return ::fallocate( fd, /* mode */ 0, /* offset */ 0, size );
        };
    benchmarkFileCreation( filePath, allocate, "fallocate" );
}


/* File Writing Benchmarks */


[[nodiscard]] double
benchmarkFwrite( const std::string&       filePath,
                 const std::vector<char>& data,
                 const size_t             chunkSize,
                 const FileInitialization fileInitialization )
{
    auto ufd = openFile( filePath, data.size(), fileInitialization );
    auto file = make_unique_file_ptr( fdopen( *ufd, "wb" ) );

    const auto t0 = now();
    for ( size_t i = 0; i < data.size(); i += chunkSize ) {
        const auto sizeToWrite = std::min( chunkSize, data.size() - i );
        const auto result = std::fwrite( &data[i], 1, sizeToWrite, file.get() );
        if ( result != sizeToWrite ) {
            std::cerr << "std::fwrite returned " << result << ", failed with: " << strerror( errno ) << " ("
                      << errno << ")\n";
            throw std::runtime_error( "Was not able to write out all of the data!" );
        }
    }

    file.reset();  // use fclose instead of close to flush buffers! Else the last 4 B were cut off!
    ufd.release();  // has been closed by fclose
    return duration( t0 );
}


void
benchmarkFwrite( const std::string&       filePath,
                 const FileInitialization fileInitialization )
{
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );
    for ( const auto chunkSize : { 1_Ki, 4_Ki, 8_Ki, 16_Ki, 64_Ki, 1_Mi, 16_Mi, 64_Mi, 512_Mi, 1_Gi } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkFwrite( filePath, data, chunkSize, fileInitialization ); },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    fwrite " << formatBytes( data.size() ) << " into " << toStringFile( fileInitialization )
                  << " in " << std::setw( 7 ) << formatBytes( chunkSize )
                  << " chunks: " << formatBandwidth( times, data.size() ) << "\n";
    }

    std::cout << std::endl;
}


[[nodiscard]] double
benchmarkWrite( const std::string&       filePath,
                const std::vector<char>& data,
                const size_t             chunkSize,
                const FileInitialization fileInitialization )
{
    auto ufd = openFile( filePath, data.size(), fileInitialization );

    const auto t0 = now();
    for ( size_t i = 0; i < data.size(); i += chunkSize ) {
        const auto sizeToWrite = std::min( chunkSize, data.size() - i );
        const auto result = ::write( *ufd, &data[i], sizeToWrite );
        if ( ( result < 0 ) || ( static_cast<size_t>( result ) != sizeToWrite ) ) {
            std::cerr << "Write returned " << result << ", failed with: " << strerror( errno ) << " ("
                      << errno << ")\n";
            throw std::runtime_error( "Was not able to write out all of the data!" );
        }
    }

    ufd.close();
    return duration( t0 );
}


void
benchmarkWrite( const std::string&       filePath,
                const FileInitialization fileInitialization )
{
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );
    for ( const auto chunkSize : { 1_Ki, 4_Ki, 8_Ki, 16_Ki, 64_Ki, 1_Mi, 16_Mi, 64_Mi, 512_Mi, 1_Gi } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkWrite( filePath, data, chunkSize, fileInitialization ); },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    write " << formatBytes( data.size() ) << " into " << toStringFile( fileInitialization )
                  << " in " << std::setw( 7 ) << formatBytes( chunkSize ) << " chunks: "
                  << formatBandwidth( times, data.size() ) << "\n";
    }
    std::cout << std::endl;
}


/* Vectorized Write */


template<typename Functor>
[[nodiscard]] double
benchmarkVectorizedWrite( const std::string&       filePath,
                          const std::vector<char>& data,
                          const size_t             chunkSize,
                          const size_t             chunkCount,
                          const FileInitialization fileInitialization,
                          const Functor&           vectorizedWrite )
{
    auto ufd = openFile( filePath, data.size(), fileInitialization );
    const auto fd = *ufd;

    [[maybe_unused]] size_t nBytesWritten{ 0 };

    const auto t0 = now();
    for ( size_t i = 0; i < data.size(); ) {
        const auto fileOffset = i;
        std::vector<::iovec> dataToWrite;
        dataToWrite.reserve( chunkCount );
        for ( ; ( dataToWrite.size() < chunkCount ) && ( i < data.size() ); i += chunkSize ) {
            auto& back = dataToWrite.emplace_back();
            const auto sizeToWrite = std::min( chunkSize, data.size() - i );
            back.iov_base = const_cast<char*>( &data[i] );
            back.iov_len = sizeToWrite;
            nBytesWritten += sizeToWrite;
        }
        vectorizedWrite( fd, fileOffset, dataToWrite );
    }

    ufd.close();
    return duration( t0 );
}


[[nodiscard]] double
benchmarkWritev( const std::string&       filePath,
                 const std::vector<char>& data,
                 const size_t             chunkSize,
                 const size_t             chunkCount,
                 const FileInitialization fileInitialization )
{
    return benchmarkVectorizedWrite( filePath, data, chunkSize, chunkCount, fileInitialization,
                                     [] ( int fd, size_t /* offset */, const std::vector<::iovec>& iov ) {
                                         return writeAllToFdVector( fd, iov );
                                     } );
}


void
benchmarkWritev( const std::string&       filePath,
                 const FileInitialization fileInitialization )
{
    constexpr size_t chunkCount = 128;
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );
    for ( const auto chunkSize : { 1_Ki, 4_Ki, 16_Ki, 64_Ki, 1_Mi, 8_Mi } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkWritev( filePath, data, chunkSize, chunkCount, fileInitialization ); },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    writev " << formatBytes( data.size() ) << " into " << toStringFile( fileInitialization )
                  << " in " << std::setw( 6 ) << formatBytes( chunkSize ) << " chunks (x" << chunkCount << "): "
                  << formatBandwidth( times, data.size() ) << "\n";
    }
    std::cout << std::endl;
}


[[nodiscard]] double
benchmarkPwritev( const std::string&       filePath,
                  const std::vector<char>& data,
                  const size_t             chunkSize,
                  const size_t             chunkCount,
                  const FileInitialization fileInitialization )
{
    return benchmarkVectorizedWrite( filePath, data, chunkSize, chunkCount, fileInitialization,
                                     [] ( int fd, size_t offset, const std::vector<::iovec>& iov ) {
                                         return pwriteAllToFdVector( fd, iov, offset );
                                     } );
}


void
benchmarkPwritev( const std::string&       filePath,
                  const FileInitialization fileInitialization )
{
    constexpr size_t chunkCount = 128;
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );
    for ( const auto chunkSize : { 1_Ki, 4_Ki, 16_Ki, 64_Ki, 1_Mi, 8_Mi } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkPwritev( filePath, data, chunkSize, chunkCount, fileInitialization ); },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    pwritev " << formatBytes( data.size() ) << " into " << toStringFile( fileInitialization )
                  << " in " << std::setw( 6 ) << formatBytes( chunkSize ) << " chunks (x" << chunkCount << "): "
                  << formatBandwidth( times, data.size() ) << "\n";
    }
    std::cout << std::endl;
}


/* Mmap Write */


[[nodiscard]] std::vector<std::pair<unique_file_descriptor, void*> >
mmapFile( const std::string& filePath,
          const size_t       size,
          const size_t       subdivisions,
          const bool         dedicatedFd )
{
    if ( fileExists( filePath ) ) {
        std::filesystem::remove( filePath );
    }

    const auto fd = ::open( filePath.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR );
    if ( fd == -1 ) {
        std::cerr << "Failed to open file " << filePath << " with errno: " << errno << ": "
                  << std::strerror( errno ) << "\n";
        return {};
    }

    unique_file_descriptor ufd( fd );
    if ( ::ftruncate( fd, size ) == -1 ) {
        std::cerr << "Encountered error while truncating file: " << std::strerror( errno )
                  << " (" << errno << ")\n";
        return {};
    }

    if ( size % subdivisions != 0 ) {
        throw std::invalid_argument( "File size should be divisible the number of mmaps!" );
    }

    std::vector<std::pair<unique_file_descriptor, void*> > result;
    const auto chunkSize = size / subdivisions;
    for ( size_t i = 0; i < subdivisions; ++i ) {
        const auto chunkFd = dedicatedFd ? ::open( filePath.c_str(), O_RDWR, S_IRUSR | S_IWUSR ) : fd;
        const auto offset = i * chunkSize;
        auto* const map = mmap( nullptr, chunkSize, PROT_WRITE, MAP_PRIVATE /* MAP_SHARED */, chunkFd, offset );

        /* Emplace especially the fd for RAII even before checking the mmap result. */
        result.emplace_back( chunkFd, map );

        if ( map == (void*)-1 ) {
            std::cerr << "Failed to mmap file with errno: " << errno << ": " << std::strerror( errno ) << "\n";
            return {};
        }
        result.emplace_back( chunkFd, map );
    }

    return result;
}


[[nodiscard]] double
benchmarkMmapWrite( const std::string&       filePath,
                    const std::vector<char>& data )
{
    auto maps = mmapFile( filePath, data.size(), /* mmap count */ 1, /* dedicated fd */ false );
    if ( maps.empty() ) {
        return 0;
    }
    auto* const map = maps.front().second;

    const auto t0 = now();

    memcpy( map, data.data(), data.size() );
    msync( map, data.size(), MS_SYNC );
    munmap( map, data.size() );

    return duration( t0 );
}


void
benchmarkMmapWrite( const std::string& filePath )
{
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK );

    const auto times = repeatBenchmarks( [&] () { return benchmarkMmapWrite( filePath, data ); }, REPEAT_COUNT );
    checkFileSize( filePath, data.size() );
    std::cout << "    ftruncate + mmap write " << formatBytes( data.size() ) << ": "
              << formatBandwidth( times, data.size() ) << "\n";
}


enum class MmapStrategy
{
    SINGLE_MAP,
    DEDICATED_MAPS,
    DEDICATED_MAPS_AND_FDS,
};


[[nodiscard]] double
benchmarkMmapWriteParallel( const std::string&       filePath,
                            const std::vector<char>& data,
                            const size_t             threadCount,
                            const MmapStrategy       mmapStrategy )
{
    ThreadPool threadPool( threadCount );

    auto maps = mmapFile( filePath, data.size(),
                          /* mmap count */ mmapStrategy == MmapStrategy::SINGLE_MAP ? 1 : threadCount,
                          /* dedicated fd */ mmapStrategy == MmapStrategy::DEDICATED_MAPS_AND_FDS );
    if ( maps.empty() ) {
        return 0;
    }

    const auto t0 = now();

    const auto chunkSize = data.size() / threadPool.capacity();
    std::vector<std::future<void> > futures;
    for ( size_t i = 0; i < threadCount; ++i ) {
        const auto offset = i * chunkSize;
        auto* const map = mmapStrategy == MmapStrategy::SINGLE_MAP
                          ? (char*)maps.front().second + offset
                          : (char*)maps[i].second;
        futures.emplace_back(
            threadPool.submit(
                [offset, chunkSize, &data, map] () {
                    memcpy( map, data.data() + offset, std::min( data.size() - offset, chunkSize ) );
                } ) );
    }
    for ( auto& future : futures ) {
        future.get();
    }

    for ( const auto& [_, map] : maps ) {
        const auto mmapSize = mmapStrategy == MmapStrategy::SINGLE_MAP ? data.size() : chunkSize;
        msync( map, mmapSize, MS_SYNC );
        munmap( map, mmapSize );
    }

    return duration( t0 );
}


void
benchmarkMmapWriteParallel( const std::string& filePath )
{
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );

    for ( const auto threadCount : { 1, 2, 4, 8, 16 } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkMmapWriteParallel( filePath, data, threadCount, MmapStrategy::SINGLE_MAP ); },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    ftruncate + mmap write " << formatBytes( data.size() ) << " using "
                  << std::setw( 2 ) << threadCount
                  << " threads: " << formatBandwidth( times, data.size() ) << "\n";
    }

    std::cout << std::endl;
}


void
benchmarkMmapWriteParallelMaps( const std::string& filePath )
{
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );

    for ( const auto threadCount : { 1, 2, 4, 8, 16 } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkMmapWriteParallel( filePath, data, threadCount, MmapStrategy::DEDICATED_MAPS ); },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    ftruncate + mmap write " << formatBytes( data.size() ) << " using "
                  << std::setw( 2 ) << threadCount
                  << " threads and maps: " << formatBandwidth( times, data.size() ) << "\n";
    }

    std::cout << std::endl;
}


void
benchmarkMmapWriteParallelMapsAndFds( const std::string& filePath )
{
    const std::vector<char> data( FILE_SIZE_TO_BENCHMARK, 1 );

    for ( const auto threadCount : { 1, 2, 4, 8, 16 } ) {
        const auto times = repeatBenchmarks(
            [&] () {
                return benchmarkMmapWriteParallel( filePath, data, threadCount,
                                                   MmapStrategy::DEDICATED_MAPS_AND_FDS );
            },
            REPEAT_COUNT );

        checkFileSize( filePath, data.size() );
        std::cout << "    ftruncate + mmap write " << formatBytes( data.size() ) << " using "
                  << std::setw( 2 ) << threadCount
                  << " threads and maps and fds: " << formatBandwidth( times, data.size() ) << "\n";
    }

    std::cout << std::endl;
}


/* Pwrite */


[[nodiscard]] double
benchmarkPwriteParallel( const std::string&       filePath,
                         const DataBuffer&        data,
                         const size_t             threadCount,
                         const FileInitialization fileInitialization )
{
    auto ufd = openFile( filePath, data.size(), fileInitialization );

    ThreadPool threadPool( threadCount );

    const auto t0 = now();

    const auto chunkSize = data.size() / threadPool.capacity();
    std::vector<std::future<void> > futures;
    for ( size_t offset = 0; offset < data.size(); offset += chunkSize ) {
        futures.emplace_back(
            threadPool.submit(
                [offset, chunkSize, &data, fd = *ufd] ()
                {
                    const auto sizeToWrite = std::min( data.size() - offset, chunkSize );
                    const auto result = pwrite( fd, data.data() + offset, sizeToWrite, offset );
                    if ( ( result < 0 ) || ( static_cast<size_t>( result ) != sizeToWrite ) ) {
                        std::cerr << "Pwrite returned " << result << ", failed with: " << strerror( errno ) << " ("
                                  << errno << ")\n";
                        throw std::runtime_error( "Was not able to write out all of the data!" );
                    }
                } ) );
    }
    for ( auto& future : futures ) {
        future.get();
    }

    ufd.close();
    return duration( t0 );
}


void
benchmarkPwriteParallel( const std::string&       filePath,
                         const FileInitialization fileInitialization )
{
    const DataBuffer data( FILE_SIZE_TO_BENCHMARK, 1 );
    for ( const auto threadCount : { 1, 2, 4, 8, 16 } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkPwriteParallel( filePath, data, threadCount, fileInitialization ); },
            REPEAT_COUNT );
        checkFileSize( filePath, data.size() );

        std::cout << "    Use pwrite to write " << formatBytes( data.size() ) << " into "
                  << toStringFile( fileInitialization ) << " using " << std::setw( 2 ) << threadCount << " threads: "
                  << formatBandwidth( times, data.size() ) << "\n";
    }
    std::cout << std::endl;
}


/* Write into multiples files in parallel */


[[nodiscard]] double
benchmarkWriteParallelFiles( const std::string&       filePath,
                             const DataBuffer&        data,
                             const size_t             threadCount,
                             const FileInitialization fileInitialization )
{
    const auto chunkSize = data.size() / threadCount;

    std::vector<unique_file_descriptor> ufds;
    for ( size_t i = 0; i < threadCount; ++i ) {
        ufds.emplace_back( openFile( filePath + "." + std::to_string( i ), chunkSize, fileInitialization ) );
    }

    ThreadPool threadPool( threadCount );

    const auto t0 = now();

    std::vector<std::future<void> > futures;
    for ( size_t i = 0; i < threadCount; ++i ) {
        const auto offset = i * chunkSize;
        futures.emplace_back(
            threadPool.submit(
                [offset, chunkSize, &data, fd = *ufds[i]] ()
                {
                    const auto sizeToWrite = std::min( data.size() - offset, chunkSize );
                    const auto result = pwrite( fd, data.data() + offset, sizeToWrite, offset );
                    if ( ( result < 0 ) || ( static_cast<size_t>( result ) != sizeToWrite ) ) {
                        std::cerr << "Pwrite returned " << result << ", failed with: " << strerror( errno ) << " ("
                                  << errno << ")\n";
                        throw std::runtime_error( "Was not able to write out all of the data!" );
                    }
                } ) );
    }
    for ( auto& future : futures ) {
        future.get();
    }

    for ( auto& ufd : ufds ) {
        ufd.close();
    }
    return duration( t0 );
}


void
benchmarkWriteParallelFiles( const std::string&       filePath,
                             const FileInitialization fileInitialization )
{
    const DataBuffer data( FILE_SIZE_TO_BENCHMARK, 1 );
    for ( const auto threadCount : { 1U, 2U, 4U, 8U, 16U } ) {
        const auto times = repeatBenchmarks(
            [&] () { return benchmarkWriteParallelFiles( filePath, data, threadCount, fileInitialization ); },
            REPEAT_COUNT );

        for ( size_t i = 0; i < threadCount; ++i ) {
            const auto subFilePath = filePath + "." + std::to_string( i );
            if ( fileExists( subFilePath ) ) {
                std::filesystem::remove( subFilePath );
            }
        }

        std::cout << "    Write " << formatBytes( data.size() ) << " into one file per thread using "
                  << std::setw( 2 ) << threadCount << " threads: "
                  << formatBandwidth( times, data.size() ) << "\n";
    }
    std::cout << std::endl;
}


int
main( int    argc,
      char** argv )
{
    const std::string filePath{ argc > 1 ? argv[1] : "/dev/shm/rapidgzip-write-test" };

    /* Note that truncate will create a sparse file, i.e., "stat" will show 0 blocks for it while "fallocate"
     * will also allocate blocks for the file. */
    std::cout << "# File Creation\n\n";
    benchmarkAllocating( filePath );  // Super slow on BEEGFS!!!
    benchmarkFAllocating( filePath );
    benchmarkTruncating( filePath );

    std::cout << "# Mmap Write\n\n";

    benchmarkMmapWrite( filePath );
    benchmarkMmapWriteParallelMaps( filePath );
    benchmarkMmapWriteParallelMapsAndFds( filePath );
    benchmarkMmapWriteParallel( filePath );

    const std::vector<FileInitialization> fileInitializations = {
        FileInitialization::EMPTY,
        FileInitialization::TRUNCATE,
        FileInitialization::ALLOCATE
    };
    for ( const auto fileInitialization : fileInitializations )
    {
        std::cout << "# Write into " << toStringFile( fileInitialization ) << "\n\n";

        std::cout << "## Vectorized Writing\n\n";
        benchmarkWritev( filePath, fileInitialization );
        benchmarkPwritev( filePath, fileInitialization );

        std::cout << "## Parallel Writing\n\n";
        benchmarkPwriteParallel( filePath, fileInitialization );
        benchmarkWriteParallelFiles( filePath, fileInitialization );

        std::cout << "## Simple Writing\n\n";
        benchmarkFwrite( filePath, fileInitialization );
        benchmarkWrite( filePath, fileInitialization );
    }

    std::filesystem::remove( filePath );

    return 0;
}
