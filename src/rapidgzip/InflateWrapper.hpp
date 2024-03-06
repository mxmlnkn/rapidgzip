#pragma once

#include <memory>
#include <optional>
#include <utility>

#include <filereader/BufferView.hpp>
#include <VectorView.hpp>

#include "definitions.hpp"
#include "gzip.hpp"
#ifdef WITH_ISAL
    #include "isal.hpp"
#endif


template<typename InflateWrapper,
         typename Container>
[[nodiscard]] Container
inflateWithWrapper( const Container&            toDecompress,
                    const std::optional<size_t> decompressedSize,
                    const VectorView<uint8_t>   dictionary = {} )
{
    if ( ( decompressedSize && ( *decompressedSize == 0 ) ) || toDecompress.empty() ) {
        return {};
    }

#ifdef WITH_ISAL
    if constexpr ( std::is_same_v<InflateWrapper, rapidgzip::IsalInflateWrapper> ) {
        if ( decompressedSize && dictionary.empty() ) {
            return rapidgzip::inflateWithIsal( toDecompress, *decompressedSize );
        }
    }
#endif

    rapidgzip::BitReader bitReader(
        std::make_unique<BufferViewFileReader>( toDecompress.data(), toDecompress.size() ) );
    /**
     * @todo offer to different modes: read gzip header, zlib header, no header, automatic detection.
     *       Need to add this feature anyway so that rapidgzip can handle multiple files! -> offer dedicated function.
     */
    rapidgzip::gzip::readHeader( bitReader );
    InflateWrapper inflateWrapper( std::move( bitReader ) );
    if ( !dictionary.empty() ) {
        inflateWrapper.setWindow( dictionary );
    }

    static constexpr auto CHUNK_SIZE = 4_Ki;
    Container result;
    while( true ) {
        const auto oldSize = result.size();
        if ( ( oldSize == 0 ) && decompressedSize && ( *decompressedSize > 0 ) ) {
            result.resize( *decompressedSize );
        } else {
            result.resize( oldSize + CHUNK_SIZE );
        }

        const auto [nBytesRead, footer] = inflateWrapper.readStream( result.data() + oldSize, result.size() - oldSize );
        result.resize( oldSize + nBytesRead );
        if ( ( nBytesRead == 0 ) && !footer ) {
            break;
        }
    }
    return result;
}
