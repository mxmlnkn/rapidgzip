#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <common.hpp>


using namespace rapidgzip;


void
replaceInPlace( std::vector<std::uint16_t>&      buffer,
                const std::vector<std::uint8_t>& window )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );
    //#pragma omp parallel for simd  // triggers the domain_error probably because it is in-place!
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        if ( buffer[i] < 256 ) {
            replaced[i] = buffer[i];
        } else if ( buffer[i] >= 32_Ki ) {
            replaced[i] = window[buffer[i] - 32_Ki];
        } else {
            throw std::domain_error( "Illegal marker byte!" );
        }
    }
}


void
replaceInPlace2( std::vector<std::uint16_t>&      buffer,
                 const std::vector<std::uint8_t>& window )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );
    //#pragma omp parallel for simd  // triggers the domain_error probably because it is in-place!
    for ( int i = 0; i < static_cast<int>( buffer.size() ); ++i ) {
        if ( ( buffer[i] >= 256 ) && ( buffer[i] < 32_Ki ) ) {
            throw std::domain_error( "Illegal marker byte!" );
        }
        replaced[i] = buffer[i] < 256 ? buffer[i] : window[buffer[i] - 32_Ki];
    }
}


void
replaceInPlaceAlternativeFormat( std::vector<std::uint16_t>&      buffer,
                                 const std::vector<std::uint8_t>& window )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );
    //#pragma omp parallel for simd  // triggers the domain_error probably because it is in-place!
    for ( int i = 0; i < static_cast<int>( buffer.size() ); ++i ) {
        if ( buffer[i] >= 32_Ki + 256 ) {
            throw std::domain_error( "Illegal marker byte!" );
        }
        replaced[i] = buffer[i] < 256 ? buffer[i] : window[buffer[i] - 256];
    }
}


void
replaceInPlaceTransformAlternativeFormat( std::vector<std::uint16_t>&      buffer,
                                          const std::vector<std::uint8_t>& window )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );
    std::transform( buffer.begin(), buffer.end(), replaced,
                    [&window] ( const auto c )
                    {
                        if ( c >= 32_Ki + 256 ) {
                            throw std::domain_error( "Illegal marker byte!" );
                        }
                        return c < 256 ? c : window[c - 256];
                    } );
}


/* This version tries to profit from SIMD even though it is in-place by doing the replacement and
 * compacting in two steps. This *does* lead to improved performance when SIMD is turned but it still
 * is worse than a simple combined loop without SIMD. */
void
replaceInPlaceHalfWindowAlternativeFormat( std::vector<std::uint16_t>&      buffer,
                                           const std::vector<std::uint8_t>& window )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );

    #pragma omp parallel for simd
    for ( int i = 0; i < static_cast<int>( buffer.size() ); ++i ) {  // NOLINT
        if ( buffer[i] >= 32_Ki + 256 ) {
            throw std::domain_error( "Illegal marker byte!" );
        }

        if ( ( buffer[i] >= 256 ) && ( buffer[i] < 16 * 1024 ) ) {
            buffer[i] = window[buffer[i] - 256];
        }
    }

    //#pragma omp parallel for simd  // triggers segfault probably because it is in-place!
    for ( int i = 0; i < static_cast<int>( buffer.size() ); ++i ) {
        replaced[i] = buffer[i] < 256 ? buffer[i] : window[buffer[i] - 256];
    }
}


/* Appending the identity bytes to the window probably not only is able to get rid of the branching
 * inside the replacement loop, it also leads to the whole window being loaded into cache beforehand. */
void
replaceInPlaceExtendedWindowAlternativeFormat( std::vector<std::uint16_t>&      buffer,
                                               const std::vector<std::uint8_t>& window )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );

    std::array<std::uint8_t, 32_Ki + 256> extendedWindow{};
    #pragma omp parallel for simd
    for ( size_t i = 0; i < 256; ++i ) {  // NOLINT
        extendedWindow[i] = i;  // NOLINT
    }
    std::copy( window.begin(), window.end(), extendedWindow.begin() + 256 );

    //#pragma omp parallel for simd  // triggers segfault probably because it is in-place!
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        if ( buffer[i] >= 32_Ki + 256 ) {
            throw std::domain_error( "Illegal marker byte!" );
        }

        replaced[i] = window[buffer[i]];
    }
}


void
onlyCompactBufferInPlace( std::vector<std::uint16_t>&      buffer,
                          const std::vector<std::uint8_t>& /* window */ )
{
    auto* const replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );

    /**
     * A non-optimized example compiles to vpunpcklqdq xmm0, xmm1, xmm1 on godbolt with -mavx2.
     *
     * @see Intel 64 and IA-32 Architectures Software Developer’s Manual Volume 2: Instruction Set Reference, A-Z
     * > Interleave low-order quadword from xmm2 and xmm3/m128 into xmm1 register.
     * -> this is only an AVX instruction and there seems to exist an AVX2 one with ymm registers, so not even optimal.
     * I would have expected: VPUNPCKLQDQ __m512i _mm512_unpacklo_epi64(__m512i a, __m512i b);
     *
     * @see http://const.me/articles/simd/simd.pdf
     * > _mm_packus_epi16 does the same but it assumes the input data contains 16-bit unsigned integer
     * > lanes, that one packs each lane into 8-bit unsigned integer using saturation (values that are greater
     * > than 255 are clipped to 255), and returns a value with all 16 values.
     *
     * Note that lookup might be parallelizable with VGATHERDPS (AVX2) but I need 8-bit integer values instead
     * of single-precision (32-bit) floats. That might complicate everything too much for a speed improvement :/
     * I could convert the LUT to contain 32-bit elements but that would be a huge waste of space and would not fit
     * in L1-cache as it increases the 32 KiB buffer to 128 KiB.
     * @see https://stackoverflow.com/a/61703013/2191065
     */
    #pragma omp parallel for simd
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        replaced[i] = buffer[i];
    }
}


void
onlyCompactBufferWithIntermediary( std::vector<std::uint16_t>&      buffer,
                                   const std::vector<std::uint8_t>& /* window */ )
{
    std::vector<std::uint8_t> result( buffer.size() );
    #pragma omp parallel for simd
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        result[i] = buffer[i];
    }
    std::memcpy( buffer.data(), result.data(), result.size() * sizeof( result[0] ) );
}


void
onlyCompactBufferWithIntermediary2( std::vector<std::uint16_t>&      buffer,
                                    const std::vector<std::uint8_t>& /* window */ )
{
    std::vector<std::uint8_t> result( buffer.size() );

    #pragma omp parallel for simd
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        result[i] = buffer[i];
    }

    auto* const __restrict__ replaced = reinterpret_cast<std::uint8_t*>( buffer.data() );
    #pragma omp parallel for simd
    for ( size_t i = 0; i < result.size(); ++i ) {
        replaced[i] = result[i];
    }
}


void
onlyCompactBufferWithIntermediarySwap( std::vector<std::uint16_t>&      buffer,
                                       const std::vector<std::uint8_t>& /* window */ )
{
    std::vector<std::uint16_t> result( buffer.size() );
    auto* const __restrict__ replaced = reinterpret_cast<std::uint8_t*>( result.data() );
    #pragma omp parallel for simd
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        replaced[i] = buffer[i];
    }
    std::swap( buffer, result );
}


void
onlyCompactBufferInPlaceCopy( std::vector<std::uint16_t>&      buffer,
                              const std::vector<std::uint8_t>& /* window */ )
{
    std::copy( buffer.begin(), buffer.end(), reinterpret_cast<std::uint8_t*>( buffer.data() ) );
}


template<typename Transform>
void
measureByteComparison( const std::vector<std::uint16_t>& buffer,
                       const std::vector<std::uint8_t>&  window,
                       Transform                         transform )
{
    double minTime = std::numeric_limits<double>::infinity();
    size_t nReplaced = 0;
    const auto nRepetitions = 5;
    for ( int iRepetition = 0; iRepetition < nRepetitions; ++iRepetition ) {
        auto copied = buffer;

        const auto t0 = now();
        transform( copied, window );
        minTime = std::min( minTime, duration( t0, now() ) );

        for ( const auto& c : buffer ) {
            if ( c > 128 ) {
                nReplaced++;
            }
        }
    }

    std::cout << "Processed " << buffer.size() * 2 << " B in " << minTime << " s -> "
              << static_cast<double>( buffer.size() ) * 2 / 1e6 / minTime << " MB/s and replaced "
              << nReplaced / nRepetitions << " markers.\n";
}


[[nodiscard]] std::vector<std::uint16_t>
createRandomBuffer( std::size_t bufferSize )
{
    /* Create half marker bytes / match lengths, half random bytes.  */
    std::vector<std::uint16_t> buffer( bufferSize );
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        const size_t matchLength = 3 + ( rand() % ( 64 - 3 ) );
        const size_t offset = rand() % 32_Ki;
        for ( size_t j = 0; ( j < matchLength ) && ( i < buffer.size() ); ++i, ++j ) {
            buffer[i] = 32_Ki + ( ( offset + j ) % 32_Ki );
        }
        for ( size_t j = 0; ( j < matchLength ) && ( i < buffer.size() ); ++i, ++j ) {
            buffer[i] = rand() % 128;
        }
    }
    return buffer;
}


[[nodiscard]] std::vector<std::uint16_t>
createRandomBufferAlternativeFormat( std::size_t bufferSize )
{
    /* Create half marker bytes / match lengths, half random bytes.  */
    std::vector<std::uint16_t> buffer( bufferSize );
    for ( size_t i = 0; i < buffer.size(); ++i ) {
        const size_t matchLength = 3 + ( rand() % ( 64 - 3 ) );
        const size_t offset = rand() % 32_Ki;
        for ( size_t j = 0; ( j < matchLength ) && ( i < buffer.size() ); ++i, ++j ) {
            buffer[i] = 256 + ( ( offset + j ) % 32_Ki );
        }
        for ( size_t j = 0; ( j < matchLength ) && ( i < buffer.size() ); ++i, ++j ) {
            buffer[i] = rand() % 128;
        }
    }
    return buffer;
}


[[nodiscard]] std::vector<std::uint8_t>
createRandomWindow( std::size_t bufferSize )
{
    /* Create half marker bytes / match lengths, half random bytes.  */
    std::vector<std::uint8_t> buffer( bufferSize );
    for ( auto& c : buffer ) {
        c = 128 + ( rand() % 128 );
    }
    return buffer;
}


int
main()
{
    const auto buffer = createRandomBuffer( 128_Mi );
    const auto bufferAlternativeFormat = createRandomBufferAlternativeFormat( 128_Mi );
    const auto window = createRandomWindow( 32_Ki );

    std::cout << "[replaceInPlace] ";
    measureByteComparison( buffer, window, replaceInPlace );
    std::cout << "[replaceInPlace2] ";
    measureByteComparison( buffer, window, replaceInPlace2 );
    std::cout << "[replaceInPlaceAlternativeFormat] ";
    measureByteComparison( bufferAlternativeFormat, window, replaceInPlaceAlternativeFormat );
    std::cout << "[replaceInPlaceTransformAlternativeFormat] ";
    measureByteComparison( bufferAlternativeFormat, window, replaceInPlaceTransformAlternativeFormat );
    std::cout << "[replaceInPlaceHalfWindowAlternativeFormat] ";
    measureByteComparison( bufferAlternativeFormat, window, replaceInPlaceHalfWindowAlternativeFormat );
    std::cout << "[replaceInPlaceExtendedWindowAlternativeFormat] ";
    measureByteComparison( bufferAlternativeFormat, window, replaceInPlaceExtendedWindowAlternativeFormat );

    std::cout << "\n";
    std::cout << "[onlyCompactBufferInPlace] ";
    measureByteComparison( bufferAlternativeFormat, window, onlyCompactBufferInPlace );
    std::cout << "[onlyCompactBufferWithIntermediary] ";
    measureByteComparison( bufferAlternativeFormat, window, onlyCompactBufferWithIntermediary );
    std::cout << "[onlyCompactBufferWithIntermediary2] ";
    measureByteComparison( bufferAlternativeFormat, window, onlyCompactBufferWithIntermediary2 );
    std::cout << "[onlyCompactBufferWithIntermediarySwap] ";
    measureByteComparison( bufferAlternativeFormat, window, onlyCompactBufferWithIntermediarySwap );
    std::cout << "[onlyCompactBufferInPlaceCopy] ";
    measureByteComparison( bufferAlternativeFormat, window, onlyCompactBufferInPlaceCopy );

    return 0;
}


/**
[replaceInPlace                               ] Processed 268435456 B in 0.16079 s   -> 1669.48 MB/s and replaced 66106853 markers.
[replaceInPlace2                              ] Processed 268435456 B in 0.128343 s  -> 2091.54 MB/s and replaced 66106853 markers.
[replaceInPlaceAlternativeFormat              ] Processed 268435456 B in 0.129165 s  -> 2078.24 MB/s and replaced 66106882 markers.
[replaceInPlaceTransformAlternativeFormat     ] Processed 268435456 B in 0.117 s     -> 2294.33 MB/s and replaced 66106882 markers.
    -> Nice! std::transform seems to be the fastest if only by a few percent.
[replaceInPlaceHalfWindowAlternativeFormat    ] Processed 268435456 B in 0.221808 s  -> 1210.22 MB/s and replaced 66106882 markers.
[replaceInPlaceExtendedWindowAlternativeFormat] Processed 268435456 B in 0.106715 s  -> 2515.45 MB/s and replaced 66106882 markers.

[onlyCompactBufferInPlace                 ] Processed 268435456 B in 0.0724053 s -> 3707.4 MB/s and replaced 66106882 markers.
[onlyCompactBufferWithIntermediary        ] Processed 268435456 B in 0.0732716 s -> 3663.57 MB/s and replaced 66106882 markers.
[onlyCompactBufferWithIntermediarySwap    ] Processed 268435456 B in 0.110629 s  -> 2426.45 MB/s and replaced 66106882 markers.
[onlyCompactBufferInPlaceCopy             ] Processed 268435456 B in 0.066874 s  -> 4014.05 MB/s and replaced 66106882 markers.

With -mavx -fopenmp and #pragma omp parallel for simd where possible without crashing or throws:

[replaceInPlaceHalfWindowAlternativeFormat] Processed 268435456 B in 0.145903 s  -> 1839.82 MB/s and replaced 66106882 markers.
[onlyCompactBufferInPlace                 ] Processed 268435456 B in 0.0218482 s -> 12286.4 MB/s and replaced 66106882 markers.
    -> THESE are the kind of speeds I wanna see! Only question would be, does it even work correctly?
[onlyCompactBufferWithIntermediary        ] Processed 268435456 B in 0.0745457 s -> 3600.95 MB/s and replaced 66106882 markers.
    -> Why isn't this as fast? Is the memcpy the bottleneck?
[onlyCompactBufferWithIntermediarySwap    ] Processed 268435456 B in 0.0969545 s -> 2768.67 MB/s and replaced 66106882 markers.
[onlyCompactBufferInPlaceCopy             ] Processed 268435456 B in 0.0663725 s -> 4044.38 MB/s and replaced 66106882 markers.
    -> commenting out throws does not change anything measurable
*/
