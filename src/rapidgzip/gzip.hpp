#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "definitions.hpp"
#include "Error.hpp"


namespace rapidgzip
{
namespace gzip
{
/** For this namespace, refer to @see RFC 1952 "GZIP File Format Specification" */

constexpr auto MAGIC_ID1 = 0x1FU;
constexpr auto MAGIC_ID2 = 0x8BU;
constexpr auto MAGIC_COMPRESSION = 0x08U;

/* Note that the byte order is reversed because of the LSB BitReader. */
constexpr auto MAGIC_BYTES_GZIP = 0x08'8B'1FU;

/* This is not a gzip-specific constant. It's such so that the decoder will not try to
 * read the whole file to memory for invalid data. */
constexpr auto MAX_ALLOWED_FIELD_SIZE = 1024 * 1024;


[[nodiscard]] inline std::string
getOperatingSystemName( uint8_t code ) noexcept
{
    switch ( code )
    {
    case   0: return "FAT filesystem (MS-DOS, OS/2, NT/Win32)";
    case   1: return "Amiga";
    case   2: return "VMS (or OpenVMS)";
    case   3: return "Unix";
    case   4: return "VM/CMS";
    case   5: return "Atari TOS";
    case   6: return "HPFS filesystem (OS/2, NT)";
    case   7: return "Macintosh";
    case   8: return "Z-System";
    case   9: return "CP/M";
    case  10: return "TOPS-20";
    case  11: return "NTFS filesystem (NT)";
    case  12: return "QDOS";
    case  13: return "Acorn RISCOS";
    case 255: return "unknown";
    }
    return std::string( "undefined (" ) + std::to_string( code ) + ")";
}


[[nodiscard]] inline std::string
getExtraFlagsDescription( uint8_t code ) noexcept
{
    switch ( code )
    {
    case   0: return "none";
    case   2: return "compressor used maximum compression, slowest algorithm";
    case   4: return "compressor used fastest algorithm";
    }
    return std::string( "undefined (" ) + std::to_string( code ) + ")";
}


struct Header
{
    uint32_t modificationTime{ 0 };
    uint8_t operatingSystem{ 0 };
    /**
     * 2: compressor used maximum compression, slowest algorithm
     * 4: compressor used fastest algorithm
     */
    uint8_t extraFlags{ 0 };

    bool isLikelyASCII{ false };
    std::optional<std::vector<uint8_t> > extra;
    std::optional<std::string> fileName;
    std::optional<std::string> comment;
    std::optional<uint16_t> crc16;
};


struct Footer
{
    uint32_t crc32{ 0 };
    uint32_t uncompressedSize{ 0 };  // If larger than UINT32_MAX, then contains the modulo.
};


inline std::pair<Header, Error>
readHeader( BitReader& bitReader )
{
    Header header;

    try {
        bitReader.peek<1>();
    } catch ( const BitReader::EndOfFileReached& ) {
        return { header, Error::END_OF_FILE };
    }

    try {
        const auto readBytes = bitReader.read<3 * BYTE_SIZE>();
        if ( readBytes != MAGIC_BYTES_GZIP ) {
            return { header, Error::INVALID_GZIP_HEADER };
        }

        const auto flags = bitReader.read<BYTE_SIZE>();
        header.modificationTime = static_cast<uint32_t>( bitReader.read<4 * BYTE_SIZE>() );
        header.extraFlags = static_cast<uint8_t>( bitReader.read<BYTE_SIZE>() );
        header.operatingSystem = static_cast<uint8_t>( bitReader.read<BYTE_SIZE>() );

        header.isLikelyASCII = ( flags & 1 ) != 0;

        const auto readZeroTerminatedString =
            [&bitReader] () -> std::pair<std::string, Error>
            {
                std::string result;
                for ( size_t i = 0; i < MAX_ALLOWED_FIELD_SIZE; ++i ) {
                    if ( bitReader.eof() ) {
                        return { result, Error::EOF_ZERO_STRING };
                    }

                    const auto toAppend = static_cast<char>( bitReader.read<BYTE_SIZE>() );
                    if ( toAppend == 0 ) {
                        break;
                    }

                    result.push_back( toAppend );
                }
                return { result, Error::NONE };
            };

        if ( ( flags & ( 1U << 2U ) ) != 0 ) {
            const auto length = bitReader.read<16>();
            std::vector<uint8_t> extraData( static_cast<size_t>( length ) );
            for ( auto& extraByte : extraData ) {
                extraByte = static_cast<uint8_t>( bitReader.read<BYTE_SIZE>() );
            }
            header.extra = std::move( extraData );
        }

        Error error = Error::NONE;

        if ( ( flags & ( 1U << 3U ) ) != 0 ) {
            std::tie( header.fileName, error ) = readZeroTerminatedString();
            if ( error != Error::NONE ) {
                return { header, error };
            }
        }

        if ( ( flags & ( 1U << 4U ) ) != 0 ) {
            std::tie( header.comment, error ) = readZeroTerminatedString();
            if ( error != Error::NONE ) {
                return { header, error };
            }
        }

        if ( ( flags & ( 1U << 1U ) ) != 0 ) {
            header.crc16 = bitReader.read<16>();
        }
    } catch ( const BitReader::EndOfFileReached& ) {
        return { header, Error::INCOMPLETE_GZIP_HEADER };
    }

    return { header, Error::NONE };
}


inline Error
checkHeader( BitReader& bitReader )
{
    const auto readBytes = bitReader.read<3 * BYTE_SIZE>();
    if ( readBytes != MAGIC_BYTES_GZIP ) {
        return Error::INVALID_GZIP_HEADER;
    }

    const auto flags = bitReader.read<BYTE_SIZE>();
    bitReader.read<4 * BYTE_SIZE>();  // modification time
    bitReader.read<BYTE_SIZE>();  // extra flags
    bitReader.read<BYTE_SIZE>();  // OS identifier

    const auto skipZeroTerminatedString =
        [&bitReader] () -> Error
        {
            for ( size_t i = 0; i < MAX_ALLOWED_FIELD_SIZE; ++i ) {
                if ( bitReader.eof() ) {
                    return Error::EOF_ZERO_STRING;
                }

                const auto toAppend = bitReader.read<BYTE_SIZE>();
                if ( toAppend == 0 ) {
                    break;
                }
            }
            return Error::NONE;
        };

    if ( ( flags & ( 1U << 2U ) ) != 0 ) {
        const auto length = bitReader.read<16>();
        bitReader.seek( length * BYTE_SIZE, SEEK_CUR );
    }

    Error error = Error::NONE;

    if ( ( flags & ( 1U << 3U ) ) != 0 ) {
        error = skipZeroTerminatedString();
        if ( error != Error::NONE ) {
            return error;
        }
    }

    if ( ( flags & ( 1U << 4U ) ) != 0 ) {
        error = skipZeroTerminatedString();
        if ( error != Error::NONE ) {
            return error;
        }
    }

    if ( ( flags & ( 1U << 1U ) ) != 0 ) {
        bitReader.read<16>();  // CRC16
    }

    return Error::NONE;
}


inline Footer
readFooter( BitReader& bitReader )
{
    if ( bitReader.tell() % BYTE_SIZE != 0 ) {
        bitReader.read( BYTE_SIZE - ( bitReader.tell() % BYTE_SIZE ) );
    }

    Footer footer;
    footer.crc32 = static_cast<uint32_t>( bitReader.read<32>() );
    footer.uncompressedSize = static_cast<uint32_t>( bitReader.read<32>() );
    return footer;
}
}  // namespace gzip
}  // namespace rapidgzip
