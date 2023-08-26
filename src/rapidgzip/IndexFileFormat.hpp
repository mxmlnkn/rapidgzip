#pragma once

#include <algorithm>
#include <cstdio>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <blockfinder/Bgzf.hpp>
#include <common.hpp>                   // _Ki literals
#include <filereader/FileReader.hpp>
#include <FileUtils.hpp>
#ifdef WITH_ISAL
    #include <isal.hpp>
#endif
#include <VectorView.hpp>
#include <zlib.hpp>


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
        // *INDENT-OFF*
        return ( compressedOffsetInBits    == other.compressedOffsetInBits    ) &&
               ( uncompressedOffsetInBytes == other.uncompressedOffsetInBytes ) &&
               ( window                    == other.window                    );
        // *INDENT-ON*
    }
};


struct GzipIndex
{
    uint64_t compressedSizeInBytes{ std::numeric_limits<uint64_t>::max() };
    uint64_t uncompressedSizeInBytes{ std::numeric_limits<uint64_t>::max() };
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
        // *INDENT-OFF*
        return ( compressedSizeInBytes   == other.compressedSizeInBytes   ) &&
               ( uncompressedSizeInBytes == other.uncompressedSizeInBytes ) &&
               ( checkpointSpacing       == other.checkpointSpacing       ) &&
               ( windowSizeInBytes       == other.windowSizeInBytes       ) &&
               ( checkpoints             == other.checkpoints             );
        // *INDENT-ON*
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


[[nodiscard]] size_t
countDecompressedBytes( rapidgzip::BitReader           bitReader,
                        VectorView<std::uint8_t> const initialWindow )
{
    #ifdef WITH_ISAL
        using InflateWrapper = rapidgzip::IsalInflateWrapper;
    #else
        using InflateWrapper = rapidgzip::ZlibInflateWrapper;
    #endif

    InflateWrapper inflateWrapper( std::move( bitReader ), std::numeric_limits<size_t>::max() );
    inflateWrapper.setWindow( initialWindow );

    size_t alreadyDecoded{ 0 };
    std::vector<uint8_t> subchunk( 128_Ki );
    while ( true ) {
        std::optional<InflateWrapper::Footer> footer;
        size_t nBytesReadPerCall{ 0 };
        while ( !footer ) {
            std::tie( nBytesReadPerCall, footer ) = inflateWrapper.readStream( subchunk.data(), subchunk.size() );
            if ( nBytesReadPerCall == 0 ) {
                break;
            }
            alreadyDecoded += nBytesReadPerCall;
        }

        if ( ( nBytesReadPerCall == 0 ) && !footer ) {
            break;
        }
    }

    return alreadyDecoded;
}


[[nodiscard]] inline GzipIndex
readGzipIndex( UniqueFileReader        indexFile,
               const UniqueFileReader& archiveFile = {} )
{
    GzipIndex index;

    const auto checkedRead =
        [&indexFile] ( void* buffer, size_t size )
        {
            const auto nBytesRead = indexFile->read( reinterpret_cast<char*>( buffer ), size );
            if ( nBytesRead != size ) {
                throw std::runtime_error( "Premature end of index file! Got only " + std::to_string( nBytesRead )
                                          + " out of " + std::to_string( size ) + " requested bytes." );
            }
        };

    const auto loadValue =
        [&checkedRead] ( auto& destination ) { checkedRead( &destination, sizeof( destination ) ); };

    std::vector<char> formatId( 5, 0 );
    checkedRead( formatId.data(), formatId.size() );
    if ( formatId != std::vector<char>( { 'G', 'Z', 'I', 'D', 'X' } ) ) {
        /* We need a seekable archive to add the very first and very last offset pairs.
         * If the archive is not seekable, loading the index makes not much sense anyways. */
        if ( archiveFile && !archiveFile->seekable() ) {
            return index;
        }

        /**
         * Try to interpret it as BGZF index, which is simply a list of 64-bit values stored in little endian:
         * uint64_t number_entries
         * [Repated number_entries times]:
         *     uint64_t compressed_offset
         *     uint64_t uncompressed_offset
         * Such an index can be created with: bgzip -c file > file.bgz; bgzip --reindex file.bgz
         * @see http://www.htslib.org/doc/bgzip.html#GZI_FORMAT
         * @note by reusing the already read 5 bytes we can avoid any seek, making it possible to work
         *       with a non-seekable input although I doubt it will be used.
         */
        uint64_t numberOfEntries{ 0 };
        std::memcpy( &numberOfEntries, formatId.data(), formatId.size() );
        checkedRead( reinterpret_cast<char*>( &numberOfEntries ) + formatId.size(),
                     sizeof( uint64_t ) - formatId.size() /* 3 */ );

        /* I don't understand why bgzip writes out 0xFFFF'FFFF'FFFF'FFFFULL in case of an empty file
         * instead of simply 0, but it does. */
        if ( numberOfEntries == std::numeric_limits<uint64_t>::max() ) {
            numberOfEntries = 0;  // Set it to a sane value which also will make the file size check work.
            index.compressedSizeInBytes = 0;
            index.uncompressedSizeInBytes = 0;
        }

        const auto expectedFileSize = ( 2U * numberOfEntries + 1U ) * sizeof( uint64_t );
        if ( ( indexFile->size() > 0 ) && ( indexFile->size() != expectedFileSize ) ) {
            throw std::invalid_argument( "Invalid magic bytes!" );
        }
        index.compressedSizeInBytes = archiveFile->size();

        index.checkpoints.reserve( numberOfEntries + 1 );

        try {
            rapidgzip::blockfinder::Bgzf blockfinder( archiveFile->clone() );
            const auto firstBlockOffset = blockfinder.find();
            if ( firstBlockOffset == std::numeric_limits<size_t>::max() ) {
                throw std::invalid_argument( "" );
            }

            auto& firstCheckPoint = index.checkpoints.emplace_back();
            firstCheckPoint.compressedOffsetInBits = firstBlockOffset;
            firstCheckPoint.uncompressedOffsetInBytes = 0;
        } catch ( const std::invalid_argument& ) {
            throw std::invalid_argument( "Trying to load a BGZF index for a non-BGZF file!" );
        }

        for ( uint64_t i = 1; i < numberOfEntries; ++i ) {
            auto& checkpoint = index.checkpoints.emplace_back();
            loadValue( checkpoint.compressedOffsetInBits );
            loadValue( checkpoint.uncompressedOffsetInBytes );
            checkpoint.compressedOffsetInBits += 18U;  // Jump over gzip header
            checkpoint.compressedOffsetInBits *= 8U;

            const auto& lastCheckPoint = *( index.checkpoints.rbegin() + 1 );

            if ( checkpoint.compressedOffsetInBits > index.compressedSizeInBytes * 8U ) {
                std::stringstream message;
                message << "Compressed bit offset (" << checkpoint.compressedOffsetInBits
                        << ") should be smaller or equal than the file size ("
                        << index.compressedSizeInBytes * 8U << ")!";
                throw std::invalid_argument( std::move( message ).str() );
            }

            if ( checkpoint.compressedOffsetInBits <= lastCheckPoint.compressedOffsetInBits ) {
                std::stringstream message;
                message << "Compressed bit offset (" << checkpoint.compressedOffsetInBits
                        << ") should be greater than predecessor ("
                        << lastCheckPoint.compressedOffsetInBits << ")!";
                throw std::invalid_argument( std::move( message ).str() );
            }

            if ( checkpoint.uncompressedOffsetInBytes < lastCheckPoint.uncompressedOffsetInBytes ) {
                std::stringstream message;
                message << "Uncompressed offset (" << checkpoint.uncompressedOffsetInBytes
                        << ") should be greater or equal than predecessor ("
                        << lastCheckPoint.uncompressedOffsetInBytes << ")!";
                throw std::invalid_argument( std::move( message ).str() );
            }
        }

        try {
            rapidgzip::BitReader bitReader( archiveFile->clone() );
            bitReader.seek( index.checkpoints.back().compressedOffsetInBits );
            index.uncompressedSizeInBytes = index.checkpoints.back().uncompressedOffsetInBytes
                                            + countDecompressedBytes( std::move( bitReader ), {} );
        } catch ( const std::invalid_argument& ) {
            throw std::invalid_argument( "Unable to read from the last given offset in the index!" );
        }

        return index;
    }

    const auto formatVersion = readValue<uint8_t>( indexFile.get() );
    if ( formatVersion > 1 ) {
        throw std::invalid_argument( "Index was written with a newer indexed_gzip version than supported!" );
    }

    indexFile->seek( 1, SEEK_CUR );  // Skip reserved flags 1B

    loadValue( index.compressedSizeInBytes );
    loadValue( index.uncompressedSizeInBytes );
    loadValue( index.checkpointSpacing );
    loadValue( index.windowSizeInBytes );

    if ( archiveFile
         && ( archiveFile->size() > 0 )
         && ( archiveFile->size() != index.compressedSizeInBytes ) )
    {
        std::stringstream message;
        message << "File size for the compressed file (" << archiveFile->size()
                << ") does not fit the size stored in the given index (" << index.compressedSizeInBytes << ")!";
        throw std::invalid_argument( std::move( message ).str() );
    }

    /* However, a window size larger than 32 KiB makes no sense bacause the Lempel-Ziv back-references
     * in the deflate format are limited to 32 KiB! Smaller values might, however, be enforced by especially
     * memory-constrained encoders.
     * This basically means that we either check for this to be exactly 32 KiB or we simply throw away all
     * other data and only load the last 32 KiB of the window buffer. */
    if ( index.windowSizeInBytes != 32_Ki ) {
        throw std::invalid_argument( "Only a window size of 32 KiB makes sense because indexed_gzip supports "
                                     "no smaller ones and gzip does support any larger one." );
    }
    const auto checkpointCount = readValue<uint32_t>( indexFile.get() );

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

        const auto bits = readValue<uint8_t>( indexFile.get() );
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
            if ( /* data flag */ readValue<uint8_t>( indexFile.get() ) != 0 ) {
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


inline void
writeGzipIndex( const GzipIndex&                                              index,
                const std::function<void( const void* buffer, size_t size )>& checkedWrite )
{
    const auto writeValue = [&checkedWrite] ( auto value ) { checkedWrite( &value, sizeof( value ) ); };

    const auto& checkpoints = index.checkpoints;
    const uint32_t windowSizeInBytes = static_cast<uint32_t>( 32_Ki );

    if ( !std::all_of( checkpoints.begin(), checkpoints.end(), [windowSizeInBytes] ( const auto& checkpoint ) {
                           return checkpoint.window.empty() || ( checkpoint.window.size() >= windowSizeInBytes );
                       } ) )
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
