#pragma once

#include <array>
#include <string>

#include "deflate.hpp"
#include "gzip.hpp"
#include "GzipReader.hpp"
#include "IndexFileFormat.hpp"
#include "ParallelGzipReader.hpp"


#define RAPIDGZIP_VERSION_MAJOR 0
#define RAPIDGZIP_VERSION_MINOR 12
#define RAPIDGZIP_VERSION_PATCH 0
#define RAPIDGZIP_VERSION_FROM_SEMVER( a, b, c ) ( a * 0x10000 + b * 0x100 + c )
#define RAPIDGZIP_VERSION \
RAPIDGZIP_VERSION_FROM_SEMVER( RAPIDGZIP_VERSION_MAJOR, RAPIDGZIP_VERSION_MINOR, RAPIDGZIP_VERSION_PATCH )


namespace rapidgzip
{
static constexpr std::array<uint8_t, 3> VERSION = {
    RAPIDGZIP_VERSION_MAJOR,
    RAPIDGZIP_VERSION_MINOR,
    RAPIDGZIP_VERSION_PATCH,
};


static const std::string VERSION_STRING{
    std::to_string( RAPIDGZIP_VERSION_MAJOR ) + '.' +
    std::to_string( RAPIDGZIP_VERSION_MINOR ) + '.' +
    std::to_string( RAPIDGZIP_VERSION_PATCH )
};
}  // namespace rapidgzip
