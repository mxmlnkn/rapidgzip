#include <iostream>

#include <rapidgzip/rapidgzip.hpp>


int
main()
{
    using namespace rapidgzip::blockfinder;
    constexpr auto lutSize = 1U << OPTIMAL_NEXT_DEFLATE_LUT_SIZE;
    for ( uint32_t i = 0; i < lutSize; ++i ) {
        std::cout << std::setw( 2 )
                  << static_cast<unsigned int>( nextDeflateCandidate<OPTIMAL_NEXT_DEFLATE_LUT_SIZE>( i ) ) << ',';
        if ( ( i + 1 ) % 32 == 0 ) {
            std::cout << '\n';
        }
    }
    return 0;
}
