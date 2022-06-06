
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <archive.h>
#include <zlib.h>

#include <BitReader.hpp>
#include <FileUtils.hpp>
#include <MemoryFileReader.hpp>
#include <StandardFileReader.hpp>
#include <common.hpp>
#include <pragzip.hpp>


class GzipWrapper
{
public:
    static constexpr int WINDOW_SIZE = 32*1024;

    enum class Format
    {
        AUTO,
        RAW,
        GZIP,
    };

public:
    explicit
    GzipWrapper( Format format = Format::AUTO )
    {
        m_stream.zalloc = Z_NULL;     /* used to allocate the internal state */
        m_stream.zfree = Z_NULL;      /* used to free the internal state */
        m_stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

        m_stream.avail_in = 0;        /* number of bytes available at next_in */
        m_stream.next_in = Z_NULL;    /* next input byte */

        m_stream.avail_out = 0;       /* remaining free space at next_out */
        m_stream.next_out = Z_NULL;   /* next output byte will go here */

        m_stream.msg = nullptr;

        int windowBits = 15;  // maximum value corresponding to 32kiB;
        switch ( format )
        {
        case Format::AUTO:
            windowBits += 32;
            break;

        case Format::RAW:
            windowBits *= -1;
            break;

        case Format::GZIP:
            windowBits += 16;
            break;
        }

        auto ret = inflateInit2( &m_stream, windowBits );
        if ( ret != Z_OK ) {
            throw std::runtime_error( "inflateInit2 returned error code: " + std::to_string( ret ) );
        }
    }

    ~GzipWrapper()
    {
        inflateEnd( &m_stream );
    }

    std::optional<VectorView<unsigned char> >
    inflate( unsigned char const** compressedData,
             size_t*               compressedSize )
    {
        m_stream.avail_in = *compressedSize;
        /* const_cast should be safe because zlib presumably only uses this in a const manner.
         * I'll probably have to roll out my own deflate decoder anyway so I might be able
         * to change this bothersome interface. */
        m_stream.next_in = const_cast<unsigned char*>( *compressedData );

        m_stream.avail_out = m_outputBuffer.size();
        m_stream.next_out = m_outputBuffer.data();

        const auto errorCode = ::inflate( &m_stream, Z_NO_FLUSH );
        *compressedData = m_stream.next_in;
        *compressedSize = m_stream.avail_in;
        if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
            return {};
        }

        if ( m_stream.avail_out > m_outputBuffer.size() ) {
            throw std::logic_error( "Zlib returned and invalid value!" );
        }

        const auto nBytesDecoded = m_outputBuffer.size() - m_stream.avail_out;
        if ( nBytesDecoded == 0 ) {
            return {};
        }

        return VectorView<unsigned char>( m_outputBuffer.data(), nBytesDecoded );
    }

private:
    z_stream m_stream{};
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32UL * 1024UL, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64UL * 1024UL * 1024UL );
};


template<typename Functor>
[[nodiscard]] std::pair<size_t, std::vector<double> >
benchmarkFunction( Functor functor )
{
    decltype(functor()) result{};
    std::vector<double> durations;
    for ( size_t i = 0; i < 3; ++i ) {
        const auto t0 = now();
        result = functor();
        const auto t1 = now();
        durations.push_back( duration( t0, t1 ) );
    }

    return { result, durations };
}


[[nodiscard]] std::vector<uint8_t>
readFile( const std::string& fileName )
{
    const auto file = throwingOpen( fileName, "rb" );
    const auto success = std::fseek( file.get(), 0, SEEK_END );
    if ( success != 0 ) {
        throw std::runtime_error( "Could not seek in given file!" );
    }

    const auto fileSize = std::ftell( file.get() );
    std::vector<uint8_t> contents( fileSize );
    std::fseek( file.get(), 0, SEEK_SET );
    const auto nBytesRead = std::fread( contents.data(), sizeof( contents[0] ), contents.size(), file.get() );

    if ( nBytesRead != contents.size() ) {
        throw std::logic_error( "Did read less bytes than file is large!" );
    }

    return contents;
}


[[nodiscard]] size_t
decompressWithZlib( const std::vector<uint8_t>& compressedData )
{
    GzipWrapper gzip;
    const auto* pCompressedData = compressedData.data();
    auto compressedSize = compressedData.size();
    size_t totalDecodedBytes = 0;

    while ( true )
    {
        const auto decodedBytes = gzip.inflate( &pCompressedData, &compressedSize );
        if ( !decodedBytes || decodedBytes->empty() ) {
            break;
        }
        totalDecodedBytes += decodedBytes->size();
    }

    return totalDecodedBytes;
}


[[nodiscard]] size_t
decompressWithLibArchive( const std::vector<uint8_t>& compressedData )
{
    auto* const pArchive = archive_read_new();
    archive_read_support_filter_gzip( pArchive );
    archive_read_support_format_raw( pArchive );
    auto errorCode = archive_read_open_memory( pArchive, compressedData.data(), compressedData.size() );
    if ( errorCode != ARCHIVE_OK ) {
        throw std::runtime_error( "Could not initialize libarchive!" );
    }

    struct archive_entry *entry{ nullptr };
    errorCode = archive_read_next_header( pArchive, &entry );
    if ( errorCode != ARCHIVE_OK ) {
        throw std::runtime_error( "Could not read header with libarchive!" );
    }

    std::vector<uint8_t> outputBuffer( 64UL * 1024UL * 1024UL );
    size_t totalDecodedBytes = 0;
    while ( true )
    {
        const auto nBytesDecoded = archive_read_data( pArchive, outputBuffer.data(), outputBuffer.size() );
        if ( nBytesDecoded < 0 ) {
            throw std::runtime_error( "Reading with libarchive failed!" );
        }
        if ( nBytesDecoded == 0 ) {
            break;
        }
        totalDecodedBytes += nBytesDecoded;
    }

    archive_read_free( pArchive );
    return totalDecodedBytes;
}


[[nodiscard]] size_t
decompressWithPragzip( const std::string& fileName )
{
    using namespace pragzip;

    size_t totalDecodedBytes = 0;
    size_t blockCount = 0;

    GzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
    std::vector<uint8_t> outputBuffer( 64UL * 1024UL * 1024UL );
    while ( true ) {
        const auto nBytesRead = gzipReader.read( -1,
                                                 reinterpret_cast<char*>( outputBuffer.data() ),
                                                 outputBuffer.size(),
                                                 GzipReader::StoppingPoint::END_OF_BLOCK_HEADER );
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        const auto currentPoint = gzipReader.currentPoint();
        if ( currentPoint == GzipReader::StoppingPoint::END_OF_BLOCK_HEADER ) {
            blockCount++;
        }
        totalDecodedBytes += nBytesRead;
    }

    std::cerr << "Decoded " << blockCount << " deflate blocks\n";

    return totalDecodedBytes;
}


void
benchmarkDecompression( const std::string& fileName )
{
    const auto fileContents = readFile( fileName );

    [[maybe_unused]] const auto decompressedZlibSize = decompressWithZlib( fileContents );

    const auto printDurations =
        [nBytesEncoded = fileContents.size()]
        ( const std::vector<double>& durations )
        {
            const auto min = std::min_element( durations.begin(), durations.end() );
            const auto max = std::max_element( durations.begin(), durations.end() );
            std::cout << " min: " << *min << "s max: " << *max << " s, ";
            std::cout << " min: " << ( static_cast<double>( nBytesEncoded ) / 1e6 / *max ) << " MB/s"
                      << " max: " << ( static_cast<double>( nBytesEncoded ) / 1e6 / *min ) << " MB/s";
            std::cout << "\n";
        };

    const auto [sizeZlib, durationsZlib] = benchmarkFunction( [&fileContents] () { return decompressWithZlib( fileContents ); } );
    assert( sizeZlib == decompressedZlibSize );
    std::cout << "Decompressed " << fileContents.size() << " B to " << sizeZlib << " B with zlib:";
    printDurations( durationsZlib );

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:";
    printDurations( durationsLibArchive );

    const auto [sizePragzip, durationsPragzip] = benchmarkFunction(
        [&fileName] () { return decompressWithPragzip( fileName ); } );
    std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzip << " B "
              << "with custom decoder (pragzip):";
    printDurations( durationsPragzip );
}


int
main( int    argc,
      char** argv )
{
    if ( argc != 2 ) {
        std::cerr << "Please specify a gzip-compressed test file!\n";
        return 1;
    }

    if ( !fileExists( argv[1] ) ) {
        std::cerr << "Could not find specified file: " << argv[1] << "\n";
        return 2;
    }
    const std::string fileName = argv[1];

    benchmarkDecompression( fileName );

    return 0;
}


/**
base64 /dev/urandom | head -c $(( 512*1024*1024 )) > small
gzip -k small

make benchmarkGzip && taskset 0x01 tests/benchmarkGzip small.gz
    Decompressed 416689498 B to 536870912 B with zlib: min: 2.19021s max: 2.1974 s,  min: 189.628 MB/s max: 190.251 MB/s
    Decompressed 416689498 B to 536870912 B with libarchive: min: 1.97245s max: 1.97384 s,  min: 211.106 MB/s max: 211.254 MB/s
    Decompressed 416689498 B to 536870912 B with custom decoder (pragzip): min: 4.5473s max: 4.5918 s,  min: 90.7464 MB/s max: 91.6345 MB/s
        ->  pragzip is more than twice as slow :/

time gzip -d -k -c small.gz | wc -c
    real  0m3.542s
  -> pragzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?
*/

/**
sudo apt install gzip tabix pigz libarchive-dev zlib1g-dev isal
python3 -m pip install --user pgzip indexed_gzip

base64 /dev/urandom | head -c $(( 512*1024*1024 )) > small
gzip -k small
tar -cf small.tar small
gzip small.tar

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 4.594 s

python3 -c 'import gzip; import time; t0 = time.time(); gzip.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 3.069 s

time python3 -m pgzip -o - small.gz | wc -c
    416755811

    real	0m3.289s
    user	0m10.282s
    sys	0m0.633s

time archivemount small.tar.gz mountpoint/
    real	0m1.657s
    user	0m1.563s
    sys	0m0.070s

time ( fuse-archive small.tar.gz mountpoint && stat mountpoint &>/dev/null; )
    real	0m1.697s
    user	0m0.002s
    sys	0m0.015s

time gzip -d -k -c small.gz | wc -c
    536870912

    real	0m3.510s
    user	0m3.395s
    sys	0m0.283s

time pigz -d -k -c small.gz | wc -c
    536870912

    real	0m2.225s
    user	0m2.797s
    sys	0m0.524s

time bgzip -d -c small.gz | wc -c
    536870912

    real	0m2.548s
    user	0m2.411s
    sys	0m0.253s

time igzip -d -c small.gz | wc -c
    536870912

    real	0m1.523s
    user	0m1.307s
    sys	0m0.344s

time pugz small.gz | wc -c
    536870912

    real	0m2.372s
    user	0m2.273s
    sys	0m0.262s

time pugz -t 4 small.gz | wc -c
    using 4 threads for decompression (experimental)
    536870912

    real	0m0.975s
    user	0m2.345s
    sys	0m0.318s

time pugz -t $( nproc ) small.gz | wc -c
    using 24 threads for decompression (experimental)
    536870912

    real	0m0.985s
    user	0m2.828s
    sys	0m6.894s

cd zlib-ng/ && mkdir build && cd $_ && cmake .. && cmake --build . --config Release
cp minigzip ~/bin/minigzip-zlib-ng
time minigzip-zlib-ng -d -k -c small.gz | wc -c
    536870912

    real	0m1.903s
    user	0m1.743s
    sys	0m0.283s

cd libdeflate && make && cp gunzio ~/bin/gunzip-libdeflate
time gunzip-libdeflate -d -k -c small.gz | wc -c
    536870912

    real	0m1.841s
    user	0m1.508s
    sys	0m0.460s

time crc32 small
    474e5ffd

    real	0m0.510s
    user	0m0.421s
    sys	0m0.069s
*/


/**
Repeat benchmarks with tarred and gzipped Silesia corpus.
http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 1.249 s

python3 -c 'import gzip; import time; t0 = time.time(); gzip.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 0.908 s

time python3 -m pgzip -o - small.gz | wc -c
    67997404

    real	0m1.837s
    user	0m1.719s
    sys	0m0.128s

time archivemount small.tar.gz mountpoint/
    real	0m1.663s
    user	0m1.580s
    sys	0m0.053s

time ( fuse-archive small.tar.gz mountpoint && stat mountpoint &>/dev/null; )
    real	0m1.629s
    user	0m0.007s
    sys	0m0.008s

time gzip -d -k -c small.gz | wc -c
    211957760

    real	0m1.000s
    user	0m0.958s
    sys	0m0.102s

time pigz -d -k -c small.gz | wc -c
    211957760

    real	0m0.622s
    user	0m0.860s
    sys	0m0.176s

time bgzip -d -c small.gz | wc -c
    211957760

    real	0m0.700s
    user	0m0.667s
    sys	0m0.068s

time igzip -d -c small.gz | wc -c
    211957760

    real	0m0.357s
    user	0m0.299s
    sys	0m0.109s

time pugz small.gz | wc -c
    terminate called after throwing an instance of 'gzip_error'
      what():  INVALID_LITERAL
    0

    real	0m0.078s
    user	0m0.005s
    sys	0m0.006s

time minigzip-zlib-ng -d -k -c small.gz | wc -c
    211957760

    real	0m0.443s
    user	0m0.405s
    sys	0m0.081s

time gunzip-libdeflate -d -k -c small.gz | wc -c
    211957760

    real	0m0.403s
    user	0m0.276s
    sys	0m0.184s

time crc32 silesia.tar
    78e42bf0

    real	0m0.236s
    user	0m0.174s
    sys	0m0.043s


Rebenchmark different versions and options of indexed_gzip

python3 -m pip install --user pgzip indexed_gzip
python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 4.666 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.IndexedGzipFile("small.gz", spacing=16*1024**2).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 6.403 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.IndexedGzipFile("small.gz", spacing=2**30, readbuf_size=2**30, buffer_size=2**30).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Floating point exception

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz", spacing=int(1*1024**2), readbuf_size=int(1*1024**2), buffer_size=int(1*1024**2)).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 4.286 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz", spacing=int(32*1024**2), readbuf_size=int(1*1024**2), buffer_size=int(1*1024**2)).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 7.407 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz", spacing=int(128*1024**2), readbuf_size=int(1024**2), buffer_size=int(128*1024**2)).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 13.290 s
*/
