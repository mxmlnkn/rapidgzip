#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sstream>
#include <utility>
#include <vector>

#include <BitReader.hpp>
#include <filereader/FileReader.hpp>
#include <Statistics.hpp>

#ifdef WITH_PYTHON_SUPPORT
    #include <filereader/Python.hpp>
#endif

#include "crc32.hpp"
#include "deflate.hpp"
#include "Error.hpp"


namespace rapidgzip::deflate
{
void
analyzeExtraString( std::string_view extra,
                    std::string_view prefix = "" )
{
    if ( extra.empty() ) {
        return;
    }

    if ( ( extra.size() == 6 )
         && ( extra[0] == 'B' )   // BGZF subfield ID "BC"
         && ( extra[1] == 'C' )
         && ( extra[2] == 0x02 )  // subfield length is 2 B
         && ( extra[3] == 0x00 ) )
    {
        const auto blockSize = ( static_cast<uint16_t>( static_cast<uint8_t>( extra[4] ) ) << 8U )
                                 + static_cast<uint8_t>( extra[5] ) + 1U;
        std::cout << prefix << "BGZF Metadata: Compressed Block Size: " << blockSize << "\n";
    }

    if ( ( extra.size() == 8 )
         && ( extra[0] == 'I' )   // "Indexed Gzip" subfield ID "IG"
         && ( extra[1] == 'G' )
         && ( extra[2] == 0x04 )  // subfield length is 4 B
         && ( extra[3] == 0x00 ) )
    {
        uint32_t blockSize{ 0 };
        for ( size_t i = 0; i < 4; ++i ) {
            blockSize |= static_cast<uint32_t>( static_cast<uint8_t>( extra[4 + i] ) ) << ( i * 8U );
        }
        std::cout << prefix << "Indexed Gzip (pgzip, mgzip) Metadata: Compressed Block Size: " << blockSize << "\n";
    }

    /**
     * @verbatim
     * mzip --help
     * > Compresses data from stdin and outputs the GZip-compressed bytes to stdout.
     * > Compressed data may be decompressed with any GZip utility single-threaded, or use MiGz to decompress it
     *   using multiple threads
     * > Optional arguments:
     * >     -t [thread count] : sets the number of threads to use (default = 2 * number of logical cores)
     * >     -b : sets the block size, in bytes (default = 512KB)
     * >     -0, -1, -2...-#...-9 : sets the compression level (0 = no compression, 1 = fastest compression,
     * >                                                        9 = best compression; default = 9)
     * > Compressing stdin using 48 threads, blocks of size 524288, and compression level 9
     * @endverbatim
     * -> The default block size 512 KB is very usable for rapidgzip!
     */
    if ( ( extra.size() == 8 )
         && ( extra[0] == 'M' )   // "MiGz" subfield ID "IG"
         && ( extra[1] == 'Z' )
         && ( extra[2] == 0x04 )  // subfield length is 4 B
         && ( extra[3] == 0x00 ) )
    {
        uint32_t blockSize{ 0 };
        for ( size_t i = 0; i < 4; ++i ) {
            blockSize |= static_cast<uint32_t>( static_cast<uint8_t>( extra[4 + i] ) ) << ( i * 8U );
        }
        /* The size is the deflate stream size (excluding the size for the gzip header and footer). */
        std::cout << prefix << "MiGz Metadata: Compressed Deflate Stream Size: " << blockSize << "\n";
    }

    if ( ( extra.size() == 12 )
         && ( extra[0] == 'Q' )   // "QATzip" subfield ID "QZ"
         && ( extra[1] == 'Z' )
         && ( extra[2] == 0x08 )  // subfield length is 8 B
         && ( extra[3] == 0x00 ) )
    {
        uint32_t chunkSize{ 0 };
        for ( size_t i = 0; i < 4U; ++i ) {
            chunkSize |= static_cast<uint32_t>( static_cast<uint8_t>( extra[4 + i] ) ) << ( i * 8U );
        }
        uint32_t blockSize{ 0 };
        for ( size_t i = 0; i < 4U; ++i ) {
            blockSize |= static_cast<uint32_t>( static_cast<uint8_t>( extra[8 + i] ) ) << ( i * 8U );
        }
        std::cout << prefix << "QATzip Metadata: Compressed Deflate Stream Size: " << blockSize
                  << ", Decompressed Stream Size: " << chunkSize << "\n";
        /* Based on further --analyze output, the "chunk size" seems to be the decompressed deflate / gzip stream size,
         * while the block size seems to be the compressed deflate stream size, i.e., without gzip header and footer. */
    }

    /**
     * @verbatim
     * pgzf -h
     * > PGZF: Parallel blocked gzip file IO
     * > Author: Jue Ruan <ruanjue@caas.cn>
     * > Version: 1.0
     * > [...]
     * >  -b <int>    Block size in MB, 1 ~ 256 [1]
     * >              '-b 1,8000' means 1 MB block size + 8000 blocks per group
     * >              that is one indexed group contains 8000 * 1MB bytes original data
     * @endverbatim
     * -> 1 MiB default block size is also very usable for rapidgzip. And first-class support for this file
     *    type would make much sense because in contrast to 32 KB blocks, it might take up to 25 % of the
     *    chunk size to arrive at a gzip stream boundary, which enables the ISA-L fastpath.
     */
    if ( ( extra.size() >= 8 )
         && ( extra[0] == 'Z' )   // "PGZF" subfield ID "ZC"
         && ( extra[1] == 'C' )
         && ( extra[2] == 0x04 )  // subfield length is 4 B
         && ( extra[3] == 0x00 ) )
    {
        uint32_t blockSize{ 0 };
        for ( size_t i = 0; i < 4; ++i ) {
            blockSize |= static_cast<uint32_t>( static_cast<uint8_t>( extra[4 + i] ) ) << ( i * 8U );
        }
        /* The size is the deflate stream size (excluding the size for the gzip header and footer). */
        std::cout << prefix << "PGZF Metadata: Compressed Deflate Stream Size: " << blockSize;

        if ( ( extra.size() == 20 )
             && ( extra[8] == 'G' )   // "PGZF" "group compressed" subfield ID "GC"
             && ( extra[9] == 'C' )
             && ( extra[10] == 0x08 )  // subfield length is 8 B
             && ( extra[11] == 0x00 ) )
        {
            uint64_t compressedGroupSize{ 0 };
            for ( size_t i = 0; i < 8; ++i ) {
                compressedGroupSize |= static_cast<uint64_t>( static_cast<uint8_t>( extra[12 + i] ) ) << ( i * 8U );
            }
            std::cout << ", Compressed Group Size: " << compressedGroupSize;
        }

        if ( ( extra.size() >= 20 )
             && ( extra[8] == 'I' )   // "PGZF" "index" subfield ID "GC"
             && ( extra[9] == 'X' )
             && ( extra[10] == 0x08 )  // subfield length is 8 B
             && ( extra[11] == 0x00 ) )
        {
            /* Index stores: nbin * {bzsize:u4i, busize:u4i}
             * @see https://github.com/ruanjue/pgzf/blob/d88a2730d1767b5f0e9ce86f7b2fa698335eb7dc/pgzf.h#L150 */
            std::cout << ", Index Data";
        }

        std::cout << "\n";
    }

    /**
     * Extra Field
     * @verbatim
     * +---+---+---+---+==================================+
     * |SI1|SI2|  LEN  |... LEN bytes of subfield data ...|
     * +---+---+---+---+==================================+
     * @endverbatim
     * subfieldID1 = 'R';
     * subfieldID2 = 'A';
     * subfieldLength =  6 + (int) tmpCount * 2;
     *
     * @verbatim
     * Random Access Field
     * +---+---+---+---+---+---+===============================+
     * |  VER  | CHLEN | CHCNT |  ... CHCNT words of data ...  |
     * +---+---+---+---+---+---+===============================+
     * @endverbatim
     * subfieldVersion = 1;
     * chunkLength = bufferSize;
     * chunkCount = (int) tmpCount;
     * chunks = new int[chunkCount];
     * // Calculate total length
     * extraLength = subfieldLength + 4;
     * headerLength = GZIP_HEADER_LEN + extraLength;
     * filename = null;
     * comment = null;
     *
     * @see https://codeberg.org/miurahr/dictzip-java/src/commit/25bb56c6b2215a1ebfd5689dbc444e276edc166c/dictzip-lib/
     *      src/main/java/org/dict/zip/DictZipHeader.java#L115-L140
     * @note Unfortunately "gradle build" fails and the CLI tool is not on the releases page.
     */
    if ( ( extra.size() >= 10 )
         && ( extra[0] == 'R' )   // "dictzip" subfield ID "RA" (random access)
         && ( extra[1] == 'A' ) )
    {
        std::cout << prefix << "Dictzip Metadata\n";
    }
}


[[nodiscard]] rapidgzip::Error
analyze( UniqueFileReader inputFile )
{
    using namespace rapidgzip;
    using Block = rapidgzip::deflate::Block</* Statistics */ true>;

    rapidgzip::BitReader bitReader{ std::move( inputFile ) };

    std::optional<gzip::Header> gzipHeader;
    Block block;

    size_t totalBytesRead = 0;
    size_t streamBytesRead = 0;

    size_t totalBlockCount = 0;
    size_t streamBlockCount = 0;
    size_t streamCount = 0;

    size_t headerOffset = 0;

    std::vector<size_t> precodeCodeLengths;
    std::vector<size_t> distanceCodeLengths;
    std::vector<size_t> literalCodeLengths;

    std::vector<size_t> encodedStreamSizes;
    std::vector<size_t> decodedStreamSizes;

    std::vector<size_t> encodedBlockSizes;
    std::vector<size_t> decodedBlockSizes;
    std::vector<double> compressionRatios;
    std::map<deflate::CompressionType, size_t> compressionTypes;

    std::map<std::vector<uint8_t>, size_t> precodeCodings;
    std::map<std::vector<uint8_t>, size_t> distanceCodings;
    std::map<std::vector<uint8_t>, size_t> literalCodings;

    CRC32Calculator crc32Calculator;

    while ( true ) {
        #ifdef WITH_PYTHON_SUPPORT
        checkPythonSignalHandlers();
        #endif

        if ( !gzipHeader ) {
            headerOffset = bitReader.tell();

            const auto [header, error] = gzip::readHeader( bitReader );
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while trying to read gzip header!\n";
                return error;
            }

            crc32Calculator.reset();
            gzipHeader = header;
            block.setInitialWindow();

            /* Analysis Information */

            streamCount += 1;
            streamBlockCount = 0;
            streamBytesRead = 0;

            std::cout << "Gzip header:\n";
            std::cout << "    Gzip Stream Count   : " << streamCount << "\n";
            std::cout << "    Compressed Offset   : " << formatBits( headerOffset ) << "\n";
            std::cout << "    Uncompressed Offset : " << totalBytesRead << " B\n";
            if ( header.fileName ) {
                std::cout << "    File Name           : " << *header.fileName << "\n";
            }
            std::cout << "    Modification Time   : " << header.modificationTime << "\n";
            std::cout << "    OS                  : " << gzip::getOperatingSystemName( header.operatingSystem ) << "\n";
            std::cout << "    Flags               : " << gzip::getExtraFlagsDescription( header.extraFlags ) << "\n";
            if ( header.comment ) {
                std::cout << "    Comment             : " << *header.comment << "\n";
            }
            if ( header.extra ) {
                std::stringstream extraString;
                extraString << header.extra->size() << " B: ";
                for ( const auto value : *header.extra ) {
                    if ( static_cast<bool>( std::isprint( value ) ) ) {
                        extraString << value;
                    } else {
                        std::stringstream hexCode;
                        hexCode << std::hex << std::setw( 2 ) << std::setfill( '0' ) << static_cast<int>( value );
                        extraString << '\\' << 'x' << hexCode.str();
                    }
                }
                std::cout << "    Extra               : " << extraString.str() << "\n";
                analyzeExtraString( { reinterpret_cast<const char*>( header.extra->data() ),
                                      header.extra->size() },
                                    "        " );
            }
            if ( header.crc16 ) {
                std::stringstream crc16String;
                crc16String << std::hex << std::setw( 16 ) << std::setfill( '0' ) << *header.crc16;
                std::cout << "    CRC16               : 0x" << crc16String.str() << "\n";
            }
            std::cout << "\n";
        }

        const auto blockOffset = bitReader.tell();
        {
            const auto error = block.readHeader( bitReader );
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while trying to read deflate header!\n";
                return error;
            }
        }
        const auto blockDataOffset = bitReader.tell();

        size_t uncompressedBlockSize = 0;
        size_t uncompressedBlockOffset = totalBytesRead;
        size_t uncompressedBlockOffsetInStream = streamBytesRead;

        block.symbolTypes.literal = 0;
        block.symbolTypes.backreference = 0;

        while ( !block.eob() ) {
            const auto [buffers, error] = block.read( bitReader, std::numeric_limits<size_t>::max() );
            const auto nBytesRead = buffers.size();
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error ) << " while decompressing deflate block.\n";
            }
            totalBytesRead += nBytesRead;
            streamBytesRead += nBytesRead;

            /* No output necessary for analysis. */

            uncompressedBlockSize += nBytesRead;

            for ( const auto& buffer : buffers.data ) {
                crc32Calculator.update( reinterpret_cast<const char*>( buffer.data() ), buffer.size() );
            }
        }

        /* Analysis Information */

        encodedBlockSizes.emplace_back( bitReader.tell() - blockOffset );
        decodedBlockSizes.emplace_back( uncompressedBlockSize );

        streamBlockCount += 1;
        totalBlockCount += 1;

        const auto compressedSizeInBits = bitReader.tell() - blockOffset;
        const auto compressionRatio = static_cast<double>( uncompressedBlockSize ) /
                                      static_cast<double>( compressedSizeInBits ) * BYTE_SIZE;
        compressionRatios.emplace_back( compressionRatio );

        const auto [compressionTypeCount, wasInserted] = compressionTypes.try_emplace( block.compressionType(), 1 );
        if ( !wasInserted ) {
            compressionTypeCount->second++;
        }

        const auto printCodeLengthStatistics =
            [] ( const auto&  codeLengths,
                 const size_t codeLengthCountRead )
            {
                auto min = std::numeric_limits<uint32_t>::max();
                auto max = std::numeric_limits<uint32_t>::min();
                size_t nonZeroCount{ 0 };

                std::array<size_t, 128> lengthCounts{};

                for ( const auto codeLength : codeLengths ) {
                    if ( codeLength > 0 ) {
                        min = std::min( min, static_cast<uint32_t>( codeLength ) );
                        max = std::max( max, static_cast<uint32_t>( codeLength ) );
                        nonZeroCount++;
                    }
                    lengthCounts.at( codeLength )++;
                }

                std::stringstream result;
                result << nonZeroCount << " CLs in [" << min << ", " << max << "] out of " << codeLengthCountRead
                       << ": CL:Count, ";
                bool requiresComma{ false };
                for ( size_t codeLength = 0; codeLength < lengthCounts.size(); ++codeLength ) {
                    if ( requiresComma ) {
                        result << ", ";
                        requiresComma = false;
                    }

                    const auto count = lengthCounts[codeLength];
                    if ( count > 0 ) {
                        result << codeLength << ":" << count;
                        requiresComma = true;
                    }
                }

                return std::move( result ).str();
            };

        const auto formatSymbolType =
            [total = block.symbolTypes.literal + block.symbolTypes.backreference] ( const auto count )
            {
                std::stringstream result;
                result << count << " (" << static_cast<double>( count ) * 100.0 / static_cast<double>( total ) << " %)";
                return std::move( result ).str();
            };

        std::cout
            << "Deflate block:\n"
            << "    Final Block                : " << ( block.isLastBlock() ? "True" : "False" ) << "\n"
            << "    Compression Type           : " << toString( block.compressionType() ) << "\n"
            << "    File Statistics:\n"
            << "        Total Block Count      : " << totalBlockCount << "\n"
            << "        Compressed Offset      : " << formatBits( blockOffset ) << "\n"
            << "        Uncompressed Offset    : " << uncompressedBlockOffset << " B\n"
            << "        Compressed Data Offset : " << formatBits( blockDataOffset ) << "\n"
            << "    Gzip Stream Statistics:\n"
            << "        Block Count            : " << streamBlockCount << "\n"
            << "        Compressed Offset      : " << formatBits( blockOffset - headerOffset ) << "\n"
            << "        Uncompressed Offset    : " << uncompressedBlockOffsetInStream << " B\n"
            << "    Compressed Size            : " << formatBits( compressedSizeInBits ) << "\n"
            << "    Uncompressed Size          : " << uncompressedBlockSize << " B\n"
            << "    Compression Ratio          : " << compressionRatio << "\n";
        if ( block.compressionType() == deflate::CompressionType::DYNAMIC_HUFFMAN ) {
            const VectorView<uint8_t> precodeCL{ block.precodeCL().data(), block.precodeCL().size() };
            const VectorView<uint8_t> distanceCL{ block.distanceAndLiteralCL().data() + block.codeCounts.literal,
                                                  block.codeCounts.distance };
            const VectorView<uint8_t> literalCL{ block.distanceAndLiteralCL().data(), block.codeCounts.literal };

            precodeCodings[static_cast<std::vector<uint8_t> >( precodeCL )]++;
            distanceCodings[static_cast<std::vector<uint8_t> >( distanceCL )]++;
            literalCodings[static_cast<std::vector<uint8_t> >( literalCL )]++;

            precodeCodeLengths.emplace_back( block.codeCounts.precode );
            distanceCodeLengths.emplace_back( block.codeCounts.distance );
            literalCodeLengths.emplace_back( block.codeCounts.literal );

            std::cout
                << "    Huffman Alphabets:\n"
                << "        Precode  : " << printCodeLengthStatistics( precodeCL, block.codeCounts.precode ) << "\n"
                << "        Distance : " << printCodeLengthStatistics( distanceCL, block.codeCounts.distance ) << "\n"
                << "        Literals : " << printCodeLengthStatistics( literalCL, block.codeCounts.literal ) << "\n";
        }
        if ( block.compressionType() != deflate::CompressionType::UNCOMPRESSED ) {
            std::cout
                << "    Symbol Types:\n"
                << "        Literal         : " << formatSymbolType( block.symbolTypes.literal ) << "\n"
                << "        Back-References : " << formatSymbolType( block.symbolTypes.backreference ) << "\n";
        }
        std::cout << "\n";

        if ( block.isLastBlock() ) {
            const auto footer = gzip::readFooter( bitReader );

            std::stringstream crcAsString;
            crcAsString << "0x" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << footer.crc32;
            std::cout << "Gzip footer:\n";
            std::cout << "    Decompressed Size % 2^32  : " << footer.uncompressedSize << "\n";
            std::cout << "    CRC32                     : " << std::move( crcAsString ).str() << "\n";

            if ( static_cast<uint32_t>( streamBytesRead ) != footer.uncompressedSize ) {
                std::stringstream message;
                message << "Mismatching size (" << static_cast<uint32_t>( streamBytesRead )
                        << " <-> footer: " << footer.uncompressedSize << ") for gzip stream!";
                throw std::runtime_error( std::move( message ).str() );
            }

            if ( crc32Calculator.verify( footer.crc32 ) ) {
                std::cerr << "Validated CRC32 0x" << std::hex << crc32Calculator.crc32() << std::dec
                          << " for gzip stream!\n";
            }

            gzipHeader = {};

            encodedStreamSizes.emplace_back( bitReader.tell() - headerOffset );
            decodedStreamSizes.emplace_back( streamBytesRead );
        }

        if ( bitReader.eof() ) {
            std::cout << "Bit reader EOF reached at " << formatBits( bitReader.tell() ) << "\n";
            break;
        }
    }

    const auto printCategorizedDuration =
        [totalDuration = block.durations.readDynamicHeader + block.durations.readData] ( const double duration )
        {
            std::stringstream result;
            result << duration << " s (" << duration / totalDuration * 100 << " %)";
            return std::move( result ).str();
        };

    const auto printHeaderDuration =
        [totalDuration = block.durations.readDynamicHeader] ( const double duration )
        {
            std::stringstream result;
            result << duration << " s (" << duration / totalDuration * 100 << " %)";
            return std::move( result ).str();
        };

    const auto printAlphabetStatistics =
        [] ( const auto& counts )
        {
            size_t total{ 0 };
            size_t duplicates{ 0 };
            for ( const auto& [_, count] : counts ) {
                if ( count > 1 ) {
                    duplicates += count - 1;
                }
                total += count;
            }

            std::stringstream result;
            result << duplicates << " duplicates out of " << total << " ("
                   << static_cast<double>( duplicates ) * 100. / static_cast<double>( total ) << " %)";
            return std::move( result ).str();
        };

    std::cout
        << "\n\n== Benchmark Profile (Cumulative Times) ==\n"
        << "\n"
        << "readDynamicHuffmanCoding : " << printCategorizedDuration( block.durations.readDynamicHeader ) << "\n"
        << "readData                 : " << printCategorizedDuration( block.durations.readData ) << "\n"
        << "Dynamic Huffman Initialization in Detail:\n"
        << "    Read precode       : " << printHeaderDuration( block.durations.readPrecode      ) << "\n"
        << "    Create precode HC  : " << printHeaderDuration( block.durations.createPrecodeHC  ) << "\n"
        << "    Apply precode HC   : " << printHeaderDuration( block.durations.applyPrecodeHC   ) << "\n"
        << "    Create distance HC : " << printHeaderDuration( block.durations.createDistanceHC ) << "\n"
        << "    Create literal HC  : " << printHeaderDuration( block.durations.createLiteralHC  ) << "\n"
        << "\n"
        << "\n"
        << "== Alphabet Statistics ==\n"
        << "\n"
        << "Precode  : " << printAlphabetStatistics( precodeCodings ) << "\n"
        << "Distance : " << printAlphabetStatistics( distanceCodings ) << "\n"
        << "Literals : " << printAlphabetStatistics( literalCodings ) << "\n"
        << "\n"
        << "== Precode Code Length Count Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ precodeCodeLengths, /* bin count */ 8 }.plot()
        << "\n"
        << "== Distance Code Length Count Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ distanceCodeLengths, /* bin count */ 8 }.plot()
        << "\n"
        << "== Literal Code Length Count Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ literalCodeLengths, /* bin count */ 8 }.plot()
        << "\n"
        << "\n"
        << "== Encoded Block Size Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ encodedBlockSizes, 8, "bits" }.plot()
        << "\n"
        << "\n"
        << "== Decoded Block Size Distribution ==\n"
        << "\n"
        << Histogram<size_t>{ decodedBlockSizes, 8, "Bytes" }.plot()
        << "\n"
        << "\n== Compression Ratio Distribution ==\n"
        << "\n"
        << Histogram<double>{ compressionRatios, 8, "Bytes" }.plot()
        << "\n";
    if ( streamCount > 1 ) {
        std::cout
        << "\n== Compressed Stream Sizes for " << encodedStreamSizes.size() << " streams ==\n"
        << "\n"
        << Histogram<size_t>{ encodedStreamSizes, 8, "Bytes" }.plot()
        << "\n"
        << "\n== Decompressed Stream Sizes for " << decodedStreamSizes.size() << " streams ==\n"
        << "\n"
        << Histogram<size_t>{ decodedStreamSizes, 8, "Bytes" }.plot()
        << "\n";
    }

    std::cout << "== Deflate Block Compression Types ==\n\n";
    for ( const auto& [compressionType, count] : compressionTypes ) {
        std::cout << std::setw( 10 ) << toString( compressionType ) << " : " << count << "\n";
    }

    std::cout << std::endl;

    return Error::NONE;
}
}  // namespace rapidgzip::deflate
