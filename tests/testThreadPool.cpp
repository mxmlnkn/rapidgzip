#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

#include <ThreadPool.hpp>


/**
 * Starts a thread pool with @p nThreads and submits @p nTasks tasks waiting for a fixed time.
 * The total time to finish is then compared to a prediction.
 * Because the threads do non-busy wait, the hardware concurrency is not a limiting factor for this test!
 */
void
testThreadPool( unsigned int nThreads,
                unsigned int nTasks )
{
    ThreadPool threadPool( nThreads );

    const auto t0 = std::chrono::high_resolution_clock::now();

    const auto secondsToWait = 0.01;
    std::vector<std::future<int> > checksums;
    for ( unsigned int i = 0; i < nTasks; ++i ) {
        checksums.emplace_back( threadPool.submitTask(
            [i, secondsToWait] () {
                std::this_thread::sleep_for( std::chrono::milliseconds( int( secondsToWait * 1000 ) ) );
                return 1 << i;
            }
        ) );
    }

    for ( auto& checksum : checksums ) {
        std::cout << "Checksum: " << checksum.get() << "\n";
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration<double>( t1 - t0 ).count();
    const auto durationPredicted = secondsToWait * ( ( nTasks + nThreads - 1 ) / nThreads );

    std::cerr << "Checksums with thread pool took " << duration << "s (predicted: " << durationPredicted << "s)\n";
    assert( ( duration - durationPredicted ) / durationPredicted < 0.1 );
}


int main( void )
{
    testThreadPool( 1, 1 );
    testThreadPool( 1, 2 );
    testThreadPool( 2, 1 );
    testThreadPool( 2, 2 );
    testThreadPool( 2, 3 );
    testThreadPool( 2, 6 );
    testThreadPool( 16, 16 );
    testThreadPool( 16, 17 );

    return 0;
}
