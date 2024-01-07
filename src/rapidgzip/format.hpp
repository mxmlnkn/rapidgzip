#pragma once

#include <blockfinder/Bgzf.hpp>
#include <filereader/Shared.hpp>

#include "gzip.hpp"


namespace rapidgzip
{
[[nodiscard]] FileType
determineFileType( UniqueFileReader file )
{
    const auto sharedFile = ensureSharedFileReader( std::move( file ) );

    /* The first deflate block offset is easily found by reading over the gzip header.
     * The correctness and existence of this first block is a required initial condition for the algorithm. */
    BitReader bitReader{ sharedFile->clone() };
    const auto [gzipHeader, gzipError] = gzip::readHeader( bitReader );
    if ( gzipError == Error::NONE ) {
        return blockfinder::Bgzf::isBgzfFile( sharedFile->clone() ) ? FileType::BGZF : FileType::GZIP;
    }

    /** Try reading zlib header */
    bitReader.seek( 0 );
    const auto [zlibHeader, zlibError] = zlib::readHeader( bitReader );
    if ( zlibError == Error::NONE ) {
        return FileType::ZLIB;
    }

    /** Try reading deflate "header" */
    bitReader.seek( 0 );
    deflate::Block block;
    return block.readHeader( bitReader ) == Error::NONE ? FileType::DEFLATE : FileType::NONE;
}


#ifdef WITH_PYTHON_SUPPORT
[[nodiscard]] std::string
determineFileTypeAsString( PyObject* pythonObject )
{
    return toString( determineFileType( std::make_unique<PythonFileReader>( pythonObject ) ) );
}
#endif
}  // namespace rapidgzip
