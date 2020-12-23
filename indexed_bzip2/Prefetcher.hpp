#pragma once

#include <cstddef>
#include <deque>
#include <numeric>
#include <thread>
#include <vector>


namespace FetchingStrategy
{
class FetchingStrategy
{
public:
    virtual ~FetchingStrategy() = default;

    virtual void
    fetch( size_t index ) = 0;

    [[nodiscard]] virtual std::vector<size_t>
    prefetch() const = 0;
};


class FetchNext :
    public FetchingStrategy
{
public:
    FetchNext( size_t maxPrefetchCount = std::thread::hardware_concurrency() ) :
        m_maxPrefetchCount( maxPrefetchCount )
    {}

    void
    fetch( size_t index ) override
    {
        previousIndexes.push_back( index );
        while ( previousIndexes.size() > 5 ) {
            previousIndexes.pop_front();
        }
    }

    [[nodiscard]] std::vector<size_t>
    prefetch() const override
    {
        if ( previousIndexes.empty() ) {
            return {};
        }

        std::vector<size_t> toPrefetch( m_maxPrefetchCount );
        std::iota( toPrefetch.begin(), toPrefetch.end(), previousIndexes.back() + 1 );
        return toPrefetch;
    }

private:
    size_t m_maxPrefetchCount;
    std::deque<size_t> previousIndexes;
};


/** @todo Add a fetching strategy which avoid full CPU load during frequent seeks (and small reads) by e.g.
 *        not suggesting anything to prefetch after a sudden jump back. */
}
