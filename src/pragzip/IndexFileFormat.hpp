#pragma once

#include <algorithm>
#include <cstdio>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <filereader/FileReader.hpp>
#include <FileUtils.hpp>


/**
 * File Format:
 * @see zran_export_index and zran_import_index functions in indexed_gzip https://github.com/pauldmccarthy/indexed_gzip
 *
 * @verbatim
 * 00  GZIDX      # Index File ID
 * 05  \x01       # File Version
 * 06  \x00       # Flags (Unused)
 * 07  <8B>       # Compressed Size (uint64_t)
 * 15  <8B>       # Uncompressed Size (uint64_t)
 * 23  <4B>       # Spacing (uint32_t)
 * 27  <4B>       # Window Size (uint32_t), Expected to be 32768, indexed_gzip checks that it is >= 32768.
 * 31  <4B>       # Number of Checkpoints (uint32_t)
 * 35
 * <Checkpoint Data> (Repeated Number of Checkpoints Times)
 * > 00  <8B>       # Compressed Offset in Rounded Down Bytes (uint64_t)
 * > 08  <8B>       # Uncompressed Offset (uint64_t)
 * > 16  <1B>       # Bits (uint8_t), Possible Values: 0-7
 * >                # "this is the number of bits in the compressed data, before the [byte offset]"
 * > 17  <1B>       # Data Flag (uint8_t), 1 if this checkpoint has window data, else 0.
 * > 18             # For format version 0, this flag did not exist and all but the first checkpoint had windows!
 * <Window Data> (Might be fewer than checkpoints because no data is written for e.g. stream boundaries)
 * > 00  <Window Size Bytes>  # Window Data, i.e., uncompressed buffer before the checkpoint's offset.
 * @endverbatim
 *
 * @note The checkpoint and window data have fixed length, so theoretically, the data could be read
 *       on-demand from the file by seeking to the required position.
 */


struct Checkpoint
{
    uint64_t compressedOffsetInBits{ 0 };
    uint64_t uncompressedOffsetInBytes{ 0 };
    /** The window may be empty for the first deflate block in each gzip stream. */
    std::vector<uint8_t> window;

    [[nodiscard]] constexpr bool
    operator==( const Checkpoint& other ) const noexcept
    {
        return ( compressedOffsetInBits    == other.compressedOffsetInBits    ) &&
               ( uncompressedOffsetInBytes == other.uncompressedOffsetInBytes ) &&
               ( window                    == other.window                    );
    }
};


struct GzipIndex
{
    uint64_t compressedSizeInBytes{ 0 };
    uint64_t uncompressedSizeInBytes{ 0 };
    /**
     * This is a kind of guidance for spacing between checkpoints in the uncompressed data!
     * If the compression ratio is very high, it could mean that the checkpoint sizes can be larger
     * than the compressed file even for very large spacings.
     */
    uint32_t checkpointSpacing{ 0 };
    uint32_t windowSizeInBytes{ 0 };
    std::vector<Checkpoint> checkpoints;

    [[nodiscard]] constexpr bool
    operator==( const GzipIndex& other ) const noexcept
    {
        return ( compressedSizeInBytes   == other.compressedSizeInBytes   ) &&
               ( uncompressedSizeInBytes == other.uncompressedSizeInBytes ) &&
               ( checkpointSpacing       == other.checkpointSpacing       ) &&
               ( windowSizeInBytes       == other.windowSizeInBytes       ) &&
               ( checkpoints             == other.checkpoints             );
    }
};


template<typename T>
[[nodiscard]] T
readValue( FileReader* file )
{
    /* Note that indexed_gzip itself does no endiannes check or conversion during writing,
     * so this system-specific reading is as portable as it gets assuming that the indexes are
     * read on the same system they are written. */
    T value;
    if ( file->read( reinterpret_cast<char*>( &value ), sizeof( value ) ) != sizeof( value ) ) {
        throw std::invalid_argument( "Premature end of file!" );
    }
    return value;
}


[[nodiscard]] inline GzipIndex
readGzipIndex( std::unique_ptr<FileReader> file )
{
    GzipIndex index;

    const auto checkedRead =
        [&file] ( void* buffer, size_t size )
        {
            const auto nBytesRead = file->read( reinterpret_cast<char*>( buffer ), size );
            if ( nBytesRead != size ) {
                throw std::runtime_error( "Premature end of index file! Got only " + std::to_string( nBytesRead )
                                          + " out of " + std::to_string( size ) + " requested bytes." );
            }
        };

    const auto loadValue = [&checkedRead] ( auto& destination ) { checkedRead( &destination, sizeof( destination ) ); };

    std::vector<char> formatId( 5, 0 );
    checkedRead( formatId.data(), formatId.size() );
    if ( formatId != std::vector<char>( { 'G', 'Z', 'I', 'D', 'X' } ) ) {
        throw std::invalid_argument( "Invalid magic bytes!" );
    }

    const auto formatVersion = readValue<uint8_t>( file.get() );
    if ( formatVersion > 1 ) {
        throw std::invalid_argument( "Index was written with a newer indexed_gzip version than supported!" );
    }

    file->seek( 1, SEEK_CUR );  // Skip reserved flags 1B

    loadValue( index.compressedSizeInBytes );
    loadValue( index.uncompressedSizeInBytes );
    loadValue( index.checkpointSpacing );
    loadValue( index.windowSizeInBytes );

    /* However, a window size larger than 32*1024 makes no sense bacause the Lempel-Ziv back-references
     * in the deflate format are limited to 32*1024! Smaller values might, however, be enforced by especially
     * memory-constrained encoders.
     * This basically means that we either check for this to be exactly 32*1024 or we simply throw away all
     * other data and only load the last 32*1024 of the window buffer. */
    if ( index.windowSizeInBytes != 32 * 1024 ) {
        throw std::invalid_argument( "Only a window size of 32 KiB makes sense because indexed_gzip supports "
                                     "no smaller ones and gzip does supprt any larger one." );
    }
    const auto checkpointCount = readValue<uint32_t>( file.get() );

    index.checkpoints.resize( checkpointCount );
    for ( uint32_t i = 0; i < checkpointCount; ++i ) {
        auto& checkpoint = index.checkpoints[i];

        /* First load only compressed offset rounded down in bytes, the bits are loaded down below! */
        loadValue( checkpoint.compressedOffsetInBits );
        if ( checkpoint.compressedOffsetInBits > index.compressedSizeInBytes ) {
            throw std::invalid_argument( "Checkpoint compressed offset is after the file end!" );
        }
        checkpoint.compressedOffsetInBits *= 8;

        loadValue( checkpoint.uncompressedOffsetInBytes );
        if ( checkpoint.uncompressedOffsetInBytes > index.uncompressedSizeInBytes ) {
            throw std::invalid_argument( "Checkpoint uncompressed offset is after the file end!" );
        }

        const auto bits = readValue<uint8_t>( file.get() );
        if ( bits >= 8 ) {
            throw std::invalid_argument( "Denormal compressed offset for checkpoint. Bit offset >= 8!" );
        }
        if ( bits > 0 ) {
            if ( checkpoint.compressedOffsetInBits == 0 ) {
                throw std::invalid_argument( "Denormal bits for checkpoint. Effectively negative offset!" );
            }
            checkpoint.compressedOffsetInBits -= bits;
        }

        if ( formatVersion == 0 ) {
            if ( i != 0 ) {
                checkpoint.window.resize( index.windowSizeInBytes );
            }
        } else {
            if ( /* data flag */ readValue<uint8_t>( file.get() ) != 0 ) {
                checkpoint.window.resize( index.windowSizeInBytes );
            }
        }
    }

    for ( auto& checkpoint : index.checkpoints ) {
        if ( !checkpoint.window.empty() ) {
            checkedRead( checkpoint.window.data(), checkpoint.window.size() );
        }
    }

    return index;
}


void
writeGzipIndex( const GzipIndex&                                              index,
                const std::function<void( const void* buffer, size_t size )>& checkedWrite )
{
    const auto writeValue = [&checkedWrite] ( auto value ) { checkedWrite( &value, sizeof( value ) ); };

    const auto& checkpoints = index.checkpoints;
    const uint32_t windowSizeInBytes = 32 * 1024;

    if ( !std::all_of( checkpoints.begin(), checkpoints.end(), [windowSizeInBytes] ( const auto& checkpoint ) {
        return checkpoint.window.empty() || ( checkpoint.window.size() >= windowSizeInBytes ); } ) )
    {
        throw std::invalid_argument( "All window sizes must be at least 32 KiB!" );
    }

    checkedWrite( "GZIDX", 5 );
    checkedWrite( /* format version */ "\x01", 1 );
    checkedWrite( /* reserved flags */ "\x00", 1 );

    /* The spacing is only used for decompression, so after reading a >full< index file, it should be irrelevant! */
    uint32_t checkpointSpacing = index.checkpointSpacing;

    if ( !checkpoints.empty() && ( checkpointSpacing < windowSizeInBytes ) ) {
        std::vector<uint64_t> uncompressedOffsets( checkpoints.size() );
        std::transform( checkpoints.begin(), checkpoints.end(), uncompressedOffsets.begin(),
                        [] ( const auto& checkpoint ) { return checkpoint.uncompressedOffsetInBytes; } );
        std::adjacent_difference( uncompressedOffsets.begin(), uncompressedOffsets.end(), uncompressedOffsets.begin() );
        const auto minSpacing = std::accumulate( uncompressedOffsets.begin() + 1, uncompressedOffsets.end(),
                                                 uint64_t( 0 ), [] ( auto a, auto b ) { return std::min( a, b ); } );
        checkpointSpacing = std::max( windowSizeInBytes, static_cast<uint32_t>( minSpacing ) );
    }

    writeValue( index.compressedSizeInBytes );
    writeValue( index.uncompressedSizeInBytes );
    writeValue( checkpointSpacing );
    writeValue( windowSizeInBytes );
    writeValue( static_cast<uint32_t>( checkpoints.size() ) );

    for ( const auto& checkpoint : checkpoints ) {
        const auto bits = checkpoint.compressedOffsetInBits % 8;
        writeValue( checkpoint.compressedOffsetInBits / 8 + ( bits == 0 ? 0 : 1 ) );
        writeValue( checkpoint.uncompressedOffsetInBytes );
        writeValue( static_cast<uint8_t>( bits == 0 ? 0 : 8 - bits ) );
        writeValue( static_cast<uint8_t>( checkpoint.window.empty() ? 0 : 1 ) );
    }

    for ( const auto& checkpoint : checkpoints ) {
        const auto& window = checkpoint.window;
        if ( window.empty() ) {
            continue;
        }

        if ( window.size() == windowSizeInBytes ) {
            checkedWrite( window.data(), window.size() );
        } else if ( window.size() > windowSizeInBytes ) {
            checkedWrite( window.data() + window.size() - windowSizeInBytes, windowSizeInBytes );
        } else if ( window.size() < windowSizeInBytes ) {
            const std::vector<char> zeros( windowSizeInBytes - window.size(), 0 );
            checkedWrite( zeros.data(), zeros.size() );
            checkedWrite( window.data(), window.size() );
        }
    }
}
