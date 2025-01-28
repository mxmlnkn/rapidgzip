#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

#include <common.hpp>
#include <ThreadPool.hpp>


using namespace rapidgzip;


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
    std::vector<std::future<unsigned int> > checksums;
    for ( unsigned int i = 0; i < nTasks; ++i ) {
        checksums.emplace_back(
            threadPool.submit(
                [i, secondsToWait] () {
                    std::this_thread::sleep_for( std::chrono::milliseconds( int( secondsToWait * 1000 ) ) );
                    return 1U << i;
                }
        ) );
    }

    for ( auto& checksum : checksums ) {
        std::cout << "Checksum: " << checksum.get() << "\n";
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto durationMeasured = std::chrono::duration<double>( t1 - t0 ).count();
    const auto durationPredicted = secondsToWait * ceilDiv( nTasks, nThreads );

    std::cerr << "Checksums with thread pool took " << durationMeasured << "s "
              << "(predicted: " << durationPredicted << "s)\n";
    /* This timing test is too unstable as it fails when running with TSAN or valgrind, which slow down execution! */
    //assert( ( durationMeasured - durationPredicted ) / durationPredicted < 1.0 );
}


int
main()
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
