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
#include <sstream>
#include <utility>
#include <vector>

#include <BitReader.hpp>
#include <FileReader.hpp>
#include <Statistics.hpp>

#include "deflate.hpp"
#include "Error.hpp"


namespace pragzip::deflate
{
[[nodiscard]] pragzip::Error
analyze( std::unique_ptr<FileReader> inputFile )
{
    using namespace pragzip;
    using Block = pragzip::deflate::Block</* CRC32 */ false, /* Statistics */ true>;

    pragzip::BitReader bitReader{ std::move( inputFile ) };

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

    std::vector<size_t> encodedBlockSizes;
    std::vector<size_t> decodedBlockSizes;
    std::vector<double> compressionRatios;
    std::map<deflate::CompressionType, size_t> compressionTypes;

    std::map<std::vector<uint8_t>, size_t> precodeCodings;
    std::map<std::vector<uint8_t>, size_t> distanceCodings;
    std::map<std::vector<uint8_t>, size_t> literalCodings;

    while ( true ) {
        if ( !gzipHeader ) {
            headerOffset = bitReader.tell();

            const auto [header, error] = gzip::readHeader( bitReader );
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while trying to read gzip header!\n";
                return error;
            }

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

        size_t uncompressedBlockSize = 0;
        size_t uncompressedBlockOffset = totalBytesRead;
        size_t uncompressedBlockOffsetInStream = streamBytesRead;

        block.symbolTypes.literal = 0;
        block.symbolTypes.backreference = 0;

        while ( !block.eob() ) {
            const auto [buffers, error] = block.read( bitReader, std::numeric_limits<size_t>::max() );
            const auto nBytesRead = buffers.size();
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error )
                          << " while decompressing deflate block.\n";
            }
            totalBytesRead += nBytesRead;
            streamBytesRead += nBytesRead;

            /* No output necessary for analysis. */

            uncompressedBlockSize += nBytesRead;
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
            << "    Final Block             : " << ( block.isLastBlock() ? "True" : "False" ) << "\n"
            << "    Compression Type        : " << toString( block.compressionType() ) << "\n"
            << "    File Statistics:\n"
            << "        Total Block Count   : " << totalBlockCount << "\n"
            << "        Compressed Offset   : " << formatBits( blockOffset ) << "\n"
            << "        Uncompressed Offset : " << uncompressedBlockOffset << " B\n"
            << "    Gzip Stream Statistics:\n"
            << "        Block Count         : " << streamBlockCount << "\n"
            << "        Compressed Offset   : " << formatBits( blockOffset - headerOffset ) << "\n"
            << "        Uncompressed Offset : " << uncompressedBlockOffsetInStream << " B\n"
            << "    Compressed Size         : " << formatBits( compressedSizeInBits ) << "\n"
            << "    Uncompressed Size       : " << uncompressedBlockSize << " B\n"
            << "    Compression Ratio       : " << compressionRatio << "\n";
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
                << "        Back-References : " << formatSymbolType( block.symbolTypes.backreference ) << "\n"
                << "\n";
        }

        if ( block.isLastBlock() ) {
            const auto footer = gzip::readFooter( bitReader );

            if ( static_cast<uint32_t>( streamBytesRead ) != footer.uncompressedSize ) {
                std::stringstream message;
                message << "Mismatching size (" << static_cast<uint32_t>( streamBytesRead )
                        << " <-> footer: " << footer.uncompressedSize << ") for gzip stream!";
                throw std::runtime_error( std::move( message ).str() );
            }

            if ( ( block.crc32() != 0 ) && ( block.crc32() != footer.crc32 ) ) {
                std::stringstream message;
                message << "Mismatching CRC32 (0x" << std::hex << block.crc32() << " <-> stored: 0x" << footer.crc32
                        << ") for gzip stream!";
            }

            if ( block.crc32() != 0 ) {
                std::stringstream message;
                message << "Validated CRC32 0x" << std::hex << block.crc32() << " for gzip stream!\n";
                std::cerr << message.str();
            }

            gzipHeader = {};
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
        << "\n"
        << "== Deflate Block Compression Types ==\n"
        << "\n";

    for ( const auto& [compressionType, count] : compressionTypes ) {
        std::cout << std::setw( 10 ) << toString( compressionType ) << " : " << count << "\n";
    }

    std::cout << std::endl;

    return Error::NONE;
}
}  // namespace pragzip::deflate
