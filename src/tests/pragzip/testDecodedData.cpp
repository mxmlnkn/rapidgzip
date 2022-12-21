#include <DecodedData.hpp>
#include <TestHelpers.hpp>


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
    using namespace pragzip::deflate;

    DecodedData decodedData;
    decodedData.data.reserve( chunkSizes.size() );
    for ( const auto chunkSize : chunkSizes ) {
        decodedData.data.emplace_back( chunkSize, 0 );
    }

    std::vector<std::pair<const void*, size_t> > iteratedViews;
    for ( auto it = DecodedData::Iterator( decodedData, offset, size ); static_cast<bool>( it ); ++it ) {
        iteratedViews.emplace_back( *it );
    }

    std::vector<std::pair<const void*, size_t> > expectedViews;
    for ( const auto& chunkRange : expected ) {
        expectedViews.emplace_back( decodedData.data.at( chunkRange.chunk ).data() + chunkRange.offset,
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


int
main()
{
    testIterator();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
