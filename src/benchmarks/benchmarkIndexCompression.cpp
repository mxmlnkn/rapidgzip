#include <cstdint>
#include <memory>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#include <core/common.hpp>
#include <filereader/Standard.hpp>
#include <rapidgzip/gzip/deflate.hpp>
#ifdef LIBRAPIDARCHIVE_WITH_ISAL
    #include <rapidgzip/gzip/isal.hpp>
#endif
#include <rapidgzip/IndexFileFormat.hpp>


using namespace rapidgzip;


template<typename ResultContainer = std::vector<uint8_t> >
[[nodiscard]] ResultContainer
compress( const VectorView<uint8_t> toCompress )
{
#ifdef LIBRAPIDARCHIVE_WITH_ISAL
    return rapidgzip::compressWithIsal( toCompress );
#else
    return rapidgzip::compressWithZlib( toCompress );
#endif
}


int
main( int    argc,
      char** argv )
{
    if ( argc < 3 ) {
        std::cout << "Requires two arguments: <path to gzip file> <path to indexed_gzip-compatible index>.\n";
        return 1;
    }

    const std::string inputFilePath( argv[1] );
    const std::string indexFilePath( argv[2] );

    const auto tStartReadIndex = now();
    const auto file = ensureSharedFileReader( openFileOrStdin( inputFilePath ) );
    auto indexFile = std::make_unique<StandardFileReader>( indexFilePath );
    const auto index = readGzipIndex( std::move( indexFile ), file->clone() );

    std::cerr << "Successfully read " << index.checkpoints.size() << " checkpoints in " << duration( tStartReadIndex )
              << " s.\n";

    if ( !index.windows ) {
        throw std::logic_error( "There should be a valid window map!" );
    }

    /** @todo also add time measurements. Might require the different implementations to be split into separate
     *        loops. */

    size_t windowSizeDecompressed{ 0 };
    size_t windowSizeUsedSymbols{ 0 };
    size_t windowSizeCompressed{ 0 };
    size_t windowSize2{ 0 };
    size_t windowSize3{ 0 };
    size_t windowSize4{ 0 };
    /* compress windows in batches. */
    std::vector<uint8_t> allWindows;
    std::vector<uint8_t> allWindows4;
    size_t windowBatchCount{ 0 };
    size_t windowCount{ 0 };

    std::array<uint8_t, 64_Ki> windowPatches{};

    gzip::BitReader bitReader( file->clone() );
    WindowMap windows;
    for ( const auto& checkpoint : index.checkpoints ) {
        windowCount++;
        if ( windowCount % 10'000 == 0 ) {
            std::cerr << "Processing " << windowCount << "-th window\n";
        }

        const auto fullWindow = index.windows->get( checkpoint.compressedOffsetInBits );
        if ( !fullWindow ) {
            throw std::logic_error( "Windows to all checkpoints should exist!" );
        }

        windowSizeCompressed += fullWindow->compressedSize();
        windowSizeDecompressed += fullWindow->decompressedSize();

        if ( fullWindow->empty() ) {
            windows.emplace( checkpoint.compressedOffsetInBits, {}, CompressionType::NONE );
            continue;
        }


        try {
            bitReader.seek( checkpoint.compressedOffsetInBits );
            const auto usedSymbols = rapidgzip::deflate::getUsedWindowSymbols( bitReader );
            windowSizeUsedSymbols += std::count_if( usedSymbols.begin(), usedSymbols.end(),
                                                    [] ( bool x ) { return x; } );

            bitReader.seek( checkpoint.compressedOffsetInBits );
            const auto decompressedWindow = fullWindow->decompress();
            const auto sparseWindow = rapidgzip::deflate::getSparseWindow( bitReader, *decompressedWindow );
            windows.emplace( checkpoint.compressedOffsetInBits, sparseWindow, CompressionType::GZIP );

            allWindows.insert( allWindows.end(), sparseWindow.begin(), sparseWindow.end() );
            if ( ++windowBatchCount >= 16 ) {
                windowSize2 += compress( allWindows ).size();
                allWindows.clear();
            }

            /** @todo this only works for the .json file, else we need to adjust getSparseWindow.
             * Format: <length zeros (may be 0)> <length data> <data> <length zeros> ...
             */
            size_t targetSize{ 0 };
            bool lookingForZeros{ true };
            size_t length{ 0 };
            for ( size_t i = 0; i < sparseWindow.size(); ++i ) {
                const auto isZero = sparseWindow[i] == 0;

                if ( isZero != lookingForZeros ) {
                    windowPatches[targetSize++] = static_cast<uint8_t>( length );
                    lookingForZeros = false;
                    length = 0;
                }

                if ( !isZero ) {
                    windowPatches[targetSize++] = sparseWindow[i];
                }
            }

            windowSize3 += compress( { windowPatches.data(), targetSize } ).size();

            allWindows4.insert( allWindows4.end(), windowPatches.begin(), windowPatches.begin() + targetSize );
            if ( ++windowBatchCount >= 16 ) {
                windowSize4 += compress( allWindows4 ).size();
                allWindows4.clear();
            }
        } catch ( const std::exception& exception ) {
            std::cerr << "Failed to get sparse window for " << checkpoint.compressedOffsetInBits << " with error: "
                      << exception.what() << ". Will ignore it.\n";
        }
    }

    /* Analyze the windows. */
    const auto [lock, windowMap] = windows.data();
    const auto windowSizeSparseRepresentationAndCompressed =
        std::accumulate( windowMap->begin(), windowMap->end(), size_t( 0 ),
                         [] ( size_t sum, const auto& kv ) { return sum + kv.second->compressedSize(); } );
    std::cerr << "    Window Count: " << windowMap->size() << "\n"
              << "    Total Window Size Decompressed: " << formatBytes( windowSizeDecompressed ) << "\n"
              << "    Total Window Size Compressed: " << formatBytes( windowSizeCompressed ) << "\n"
              << "    Total Window Size Used Symbols: " << formatBytes( windowSizeUsedSymbols ) << "\n"
              << "    Total Window Size Unused Symbols Zeroed + Compressed: "
              << formatBytes( windowSizeSparseRepresentationAndCompressed ) << "\n"
              << "    Total Window Size Unused Symbols Zeroed + Batch-Compressed: "
              << formatBytes( windowSize2 ) << "\n"
              << "    Total Window Size Without Zeros + Compressed: " << formatBytes( windowSize3 ) << "\n"
              << "    Total Window Size Without Zeros + Batch-Compressed: " << formatBytes( windowSize4 ) << "\n";

    return 0;
}


/*
m benchmarkIndexCompression && src/benchmarks/benchmarkIndexCompression /media/e/wikidata-20220103-all.json.gz{,.index}

    Read 340425 checkpoints
    Window Count: 340425
    Total Window Size Decompressed: 10 GiB 398 MiB 256 KiB

    ISA-L:
        Total Window Size Compressed: 1 GiB 339 MiB 14 KiB 529 B
        Total Window Size Used Symbols: 1 GiB 890 MiB 756 KiB 898 B
        Total Window Size Unused Symbols Zeroed + Compressed: 635 MiB 372 KiB 827 B
        Total Window Size Unused Symbols Zeroed + Batch-Compressed: 635 MiB 371 KiB 15 B
        Total Window Size Without Zeros + Compressed: 624 MiB 802 KiB 824 B
        Total Window Size Without Zeros + Batch-Compressed: 624 MiB 801 KiB 71 B
    Zlib:
        Total Window Size Compressed: 1 GiB 169 MiB 582 KiB 744 B
        Total Window Size Used Symbols: 1 GiB 890 MiB 756 KiB 898 B
        Total Window Size Unused Symbols Zeroed + Compressed: 539 MiB 730 KiB 96 B
        Total Window Size Unused Symbols Zeroed + Batch-Compressed: 539 MiB 727 KiB 748 B
        Total Window Size Without Zeros + Compressed: 532 MiB 772 KiB 273 B
        Total Window Size Without Zeros + Batch-Compressed: 532 MiB 770 KiB 61 B

 -> Batch-compression helps almost nothing and would only introduce further complexities! Discard this idea.
 -> Zlib has slightly higher (~15%) compression but takes much much longer (see below for time measurements)!


m benchmarkIndexCompression && src/benchmarks/benchmarkIndexCompression 4GiB-base64.gz{,.index}

    Read 780 checkpoints
    Window Count: 780
    Total Window Size Decompressed: 24 MiB 352 KiB

    ISA-L:
        Total Window Size Compressed: 24 MiB 352 KiB
        Total Window Size Used Symbols: 149 KiB 781 B
        Total Window Size Unused Symbols Zeroed + Compressed: 428 KiB 617 B
        Total Window Size Unused Symbols Zeroed + Batch-Compressed: 428 KiB 442 B
        Total Window Size Without Zeros + Compressed: 393 KiB 282 B
        Total Window Size Without Zeros + Batch-Compressed: 393 KiB 281 B

    Zlib:
        Total Window Size Compressed: 24 MiB 352 KiB
        Total Window Size Used Symbols: 149 KiB 781 B
        Total Window Size Unused Symbols Zeroed + Compressed: 333 KiB 131 B
        Total Window Size Unused Symbols Zeroed + Batch-Compressed: 332 KiB 637 B
        Total Window Size Without Zeros + Compressed: 324 KiB 353 B
        Total Window Size Without Zeros + Batch-Compressed: 323 KiB 941 B

 -> As expected, compression doesn't really matter in this case.
 -> Zlib has slightly higher (~20%) compression but takes much much longer!
 -> Sparsity already reduces the window size by 75x!
    This is a rare case because there are almost not back-references in this data.


Old benchmarks:

Read 340425 checkpoints
    Window Count: 340425
    Total Window Size: 1 GiB 652 MiB 217 KiB 518 B

ISA-L (level 0)

    Total Window Size: 747 MiB 794 KiB 512 B
    real	6m22.796s
    user	1m6.975s
    sys	0m33.980s

    -> Not as large a reduction as hoped for but 2x still is nice

ISA-L (level 1)

    Total Window Size: 609 MiB 568 KiB 423 B
    Total Window Size Batch-Compressed: 609 MiB 566 KiB 856 B
    Total Window Size Without Zeros: 608 MiB 212 KiB 519 B

    real	6m29.877s
    user	1m14.430s
    sys	0m33.450s

 -> Increasing the level, really is worth it at this point!

ISA-L (level 2)

    Total Window Size: 608 MiB 126 KiB 654 B

 -> level 2 instead of level 0. Not worth it

zlib (default level (6))

    Total Window Size: 528 MiB 323 KiB 15 B
    Total Window Size Batch-Compressed: 528 MiB 320 KiB 519 B
    Total Window Size Without Zeros: 527 MiB 507 KiB 103 B

    real	11m30.027s
    user	7m36.295s
    sys	0m34.736s

  -> twice as slow as ISA-L!
*/
