#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

#define TEST_DECODED_DATA

#include <DecodedData.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


struct ChunkRange
{
    size_t chunk;
    size_t offset;
    size_t size;
};


std::ostream&
operator<<( std::ostream&                                       out,
            const std::vector<std::pair<const void*, size_t> >& views )
{
    out << "{";
    for ( const auto& [data, size] : views ) {
        out << " (" << data << ", " << size << ")";
    }
    out << "}";
    return out;
}


void
testIterator( const std::vector<size_t>&     chunkSizes,
              const size_t                   offset,
              const size_t                   size,
              const std::vector<ChunkRange>& expected )
{
    using namespace rapidgzip::deflate;

    DecodedData decodedData;
    for ( const auto chunkSize : chunkSizes ) {
        std::vector<uint8_t> buffer( chunkSize, 0 );

        DecodedDataView toAppend;
        toAppend.data[0] = VectorView<uint8_t>( { buffer.data(), buffer.size() } );
        decodedData.append( toAppend );
    }

    std::vector<std::pair<const void*, size_t> > iteratedViews;
    for ( auto it = DecodedData::Iterator( decodedData, offset, size ); static_cast<bool>( it ); ++it ) {
        iteratedViews.emplace_back( *it );
    }

    std::vector<std::pair<const void*, size_t> > expectedViews;
    expectedViews.reserve( expected.size() );
    for ( const auto& chunkRange : expected ) {
        expectedViews.emplace_back( decodedData.getData().at( chunkRange.chunk ).data() + chunkRange.offset,
                                    chunkRange.size );
    }

    REQUIRE_EQUAL( iteratedViews, expectedViews );
}


void
testIterator()
{
    testIterator( {}, 0, 0, {} );
    testIterator( {}, 0, 1, {} );
    testIterator( {}, 1, 10, {} );

    testIterator( { 0 }, 0, 0, {} );
    testIterator( { 0 }, 0, 1, {} );
    testIterator( { 0 }, 1, 10, {} );

    testIterator( { 0, 0 }, 0, 0, {} );
    testIterator( { 0, 0 }, 0, 1, {} );
    testIterator( { 0, 0 }, 1, 10, {} );

    testIterator( { 7 }, 0, 0, {} );
    testIterator( { 7 }, 0, 1, { { .chunk = 0, .offset = 0, .size = 1 } } );
    testIterator( { 7 }, 0, 10, { { .chunk = 0, .offset = 0, .size = 7 } } );
    testIterator( { 7 }, 1, 1, { { .chunk = 0, .offset = 1, .size = 1 } } );
    testIterator( { 7 }, 1, 10, { { .chunk = 0, .offset = 1, .size = 6 } } );

    testIterator( { 3, 7 }, 0, 0, {} );
    testIterator( { 3, 7 }, 0, 1, { { .chunk = 0, .offset = 0, .size = 1 } } );
    testIterator( { 3, 7 }, 0, 10, { { .chunk = 0, .offset = 0, .size = 3 },
                                     { .chunk = 1, .offset = 0, .size = 7 } } );
    testIterator( { 3, 7 }, 1, 1, { { .chunk = 0, .offset = 1, .size = 1 } } );
    testIterator( { 3, 7 }, 1, 10, { { .chunk = 0, .offset = 1, .size = 2 },
                                     { .chunk = 1, .offset = 0, .size = 7 } } );
    testIterator( { 3, 7 }, 2, 10, { { .chunk = 0, .offset = 2, .size = 1 },
                                     { .chunk = 1, .offset = 0, .size = 7 } } );
    testIterator( { 3, 7 }, 3, 10, { { .chunk = 1, .offset = 0, .size = 7 } } );
    testIterator( { 3, 7 }, 4, 10, { { .chunk = 1, .offset = 1, .size = 6 } } );
}


template<typename T>
[[nodiscard]] std::vector<T>
createVector( const std::vector<std::pair<size_t, T> >& ranges )
{
    std::vector<T> result;
    for ( const auto& [size, symbol] : ranges ) {
        result.resize( result.size() + size, symbol );
    }
    return result;
}


template<typename T>
[[nodiscard]] std::vector<T>
resizeRight( const std::vector<T>& container,
             size_t                size,
             T                     fill )
{
    std::vector<T> result( size, fill );
    if ( container.size() >= size ) {
        std::copy( container.begin() + ( container.size() - size ), container.end(), result.begin() );
    } else {
        std::copy( container.begin(), container.end(), result.begin() + ( size - container.size() ) );
    }
    return result;
}


void
testGetWindow()
{
    /* getLastWindow( window ) should be identical to getWindowAt( window, decodedData.size() ).
     * getWindowAt( window, 0 ) == window(truncated to MAX_WINDOW_SIZE) should always hold. */

    using namespace rapidgzip::deflate;

    /* dataWithMarkers.size() == 0 */
    {
        const auto initDecodedData =
            [] ( size_t  size,
                 uint8_t value )
            {
                DecodedData decodedData;

                std::vector<uint8_t> buffer( size, value );

                DecodedDataView toAppend;
                toAppend.data[0] = VectorView<uint8_t>( { buffer.data(), buffer.size() } );
                decodedData.append( toAppend );

                return decodedData;
            };

        /* data.size() == MAX_WINDOW_SIZE */
        {
            const auto decodedData = initDecodedData( MAX_WINDOW_SIZE, 3 );
            const std::vector<uint8_t> window( MAX_WINDOW_SIZE, 1 );
            const std::vector<uint8_t> expected( MAX_WINDOW_SIZE, 3 );

            REQUIRE( decodedData.getLastWindow( {} ) == expected );
            REQUIRE( decodedData.getLastWindow( window ) == expected );

            REQUIRE( decodedData.getWindowAt( {}, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
        }

        /* data.size() > MAX_WINDOW_SIZE */
        {
            const auto decodedData = initDecodedData( MAX_WINDOW_SIZE + 10000, 3 );
            const std::vector<uint8_t> window( MAX_WINDOW_SIZE, 1 );
            const std::vector<uint8_t> expected( MAX_WINDOW_SIZE, 3 );

            REQUIRE( decodedData.getLastWindow( {} ) == expected );
            REQUIRE( decodedData.getLastWindow( window ) == expected );

            REQUIRE( decodedData.getWindowAt( {}, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
        }

        /* data.size() < MAX_WINDOW_SIZE */
        {
            const auto decodedData = initDecodedData( 100, 3 );

            /* window.size() == 0 */
            {
                const std::vector<uint8_t> window;
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 100, 0 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() < MAX_WINDOW_SIZE - data.size() */
            {
                const std::vector<uint8_t> window( 200, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 300, 0 }, { 200, 1 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() < MAX_WINDOW_SIZE */
            {
                const std::vector<uint8_t> window( MAX_WINDOW_SIZE - 100, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 100, 1 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() == MAX_WINDOW_SIZE */
            {
                const std::vector<uint8_t> window( MAX_WINDOW_SIZE, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 100, 1 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }


            /* window.size() > MAX_WINDOW_SIZE */
            {
                const std::vector<uint8_t> window( MAX_WINDOW_SIZE + 1000, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 100, 1 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }
        }
    }

    /* dataWithMarkers.size() > 0 */
    {
        const auto initDecodedDataWithMarkers =
            [] ( size_t   size,
                 uint8_t  value )
            {
                DecodedData decodedData;

                std::vector<uint16_t> markersBuffer( 300, 5 );
                std::vector<uint8_t> buffer( size, value );

                DecodedDataView toAppend;
                toAppend.dataWithMarkers[0] = VectorView<uint16_t>( { markersBuffer.data(), markersBuffer.size() } );
                toAppend.data[0] = VectorView<uint8_t>( { buffer.data(), buffer.size() } );
                decodedData.append( toAppend );

                return decodedData;
            };

        /* data.size() == MAX_WINDOW_SIZE */
        {
            const auto decodedData = initDecodedDataWithMarkers( MAX_WINDOW_SIZE, 3 );
            const std::vector<uint8_t> window( MAX_WINDOW_SIZE, 1 );
            const std::vector<uint8_t> expected( MAX_WINDOW_SIZE, 3 );

            REQUIRE( decodedData.getLastWindow( {} ) == expected );
            REQUIRE( decodedData.getLastWindow( window ) == expected );

            REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
        }

        /* data.size() > MAX_WINDOW_SIZE */
        {
            const auto decodedData = initDecodedDataWithMarkers( MAX_WINDOW_SIZE + 10000, 3 );
            const std::vector<uint8_t> window( MAX_WINDOW_SIZE, 1 );
            const std::vector<uint8_t> expected( MAX_WINDOW_SIZE, 3 );

            REQUIRE( decodedData.getLastWindow( {} ) == expected );
            REQUIRE( decodedData.getLastWindow( window ) == expected );

            REQUIRE( decodedData.getWindowAt( {}, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
            REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
        }

        /* MAX_WINDOW_SIZE - dataWithMarkers.size() <= data.size() < MAX_WINDOW_SIZE */

        /* data.size() + dataWithMarkers.size() < MAX_WINDOW_SIZE */
        {
            const auto decodedData = initDecodedDataWithMarkers( 100, 3 );

            /* window.size() == 0 */
            {
                const std::vector<uint8_t> window;
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 400, 0 }, { 300, 5 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() < MAX_WINDOW_SIZE - data.size() */
            {
                const std::vector<uint8_t> window( 200, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 600, 0 },
                                                               { 200, 1 }, { 300, 5 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() < MAX_WINDOW_SIZE */
            {
                const std::vector<uint8_t> window( MAX_WINDOW_SIZE - 100, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 400, 1 }, { 300, 5 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() == MAX_WINDOW_SIZE */
            {
                const std::vector<uint8_t> window( MAX_WINDOW_SIZE, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 400, 1 }, { 300, 5 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );
            }

            /* window.size() > MAX_WINDOW_SIZE */
            {
                const std::vector<uint8_t> window( MAX_WINDOW_SIZE + 1000, 1 );
                const auto expected = createVector<uint8_t>( { { MAX_WINDOW_SIZE - 400, 1 }, { 300, 5 }, { 100, 3 } } );

                REQUIRE( decodedData.getLastWindow( window ) == expected );
                REQUIRE( decodedData.getWindowAt( window, decodedData.size() ) == expected );
                REQUIRE( decodedData.getWindowAt( window, 0 ) == resizeRight<uint8_t>( window, MAX_WINDOW_SIZE, 0 ) );

                REQUIRE( decodedData.getWindowAt( window, 50 )
                         == createVector<uint8_t>( { { MAX_WINDOW_SIZE - 50, 1 }, { 50, 5 } } ) );
                REQUIRE( decodedData.getWindowAt( window, 300 )
                         == createVector<uint8_t>( { { MAX_WINDOW_SIZE - 300, 1 }, { 300, 5 } } ) );
                REQUIRE( decodedData.getWindowAt( window, 301 )
                         == createVector<uint8_t>( { { MAX_WINDOW_SIZE - 301, 1 }, { 300, 5 }, { 1, 3 } } ) );
            }
        }
    }
}


int
main()
{
    testIterator();
    testGetWindow();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
