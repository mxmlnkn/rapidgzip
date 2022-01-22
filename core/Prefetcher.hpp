#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <deque>
#include <iterator>
#include <numeric>
#include <optional>
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
    prefetch( size_t maxAmountToPrefetch ) const = 0;
};


/**
 * Simply prefetches the next n indexes after the last access.
 */
class FetchNext :
    public FetchingStrategy
{
public:
    void
    fetch( size_t index ) override
    {
        m_lastFetched = index;
    }

    [[nodiscard]] std::vector<size_t>
    prefetch( size_t maxAmountToPrefetch ) const override
    {
        if ( !m_lastFetched ) {
            return {};
        }

        std::vector<size_t> toPrefetch( maxAmountToPrefetch );
        std::iota( toPrefetch.begin(), toPrefetch.end(), *m_lastFetched + 1 );
        return toPrefetch;
    }

private:
    static constexpr size_t MEMORY_SIZE = 3;
    std::optional<size_t> m_lastFetched;
};


/**
 * Similar to @ref FetchNext but the amount of returned subsequent indexes is relative to the amount of
 * consecutive accesses in the memory.
 * If all are consecutive, returns the specified amount to prefetch.
 * Else, if there are only random access in memory, then it will return nothing to prefetch to avoid wasted computation.
 * Inbetween, interpolate exponentially, i.e., for a memory size of 3 and 4 requested prefetch indexes:
 *   1 consecutive pair -> 1
 *   2 consecutive pair -> 2
 *   3 consecutive pair -> 4
 */
class FetchNextSmart :
    public FetchingStrategy
{
public:
    void
    fetch( size_t index ) override
    {
        /* Ignore duplicate accesses, which in the case of bzip2 blocks most likely means
         * that the caller reads only small parts from the block per call. */
        if ( !m_previousIndexes.empty() && ( m_previousIndexes.front() == index ) ) {
            return;
        }

        m_previousIndexes.push_front( index );
        while ( m_previousIndexes.size() > MEMORY_SIZE ) {
            m_previousIndexes.pop_back();
        }
    }

    [[nodiscard]] std::vector<size_t>
    prefetch( size_t maxAmountToPrefetch ) const override
    {
        if ( m_previousIndexes.empty() || ( maxAmountToPrefetch == 0 ) ) {
            return {};
        }

        /** This avoids division by 0 further down below! */
        if ( m_previousIndexes.size() == 1 ) {
            std::vector<size_t> toPrefetch( maxAmountToPrefetch );
            std::iota( toPrefetch.begin(), toPrefetch.end(), m_previousIndexes.front() + 1 );
            return toPrefetch;
        }

        size_t consecutiveCount = 0; /**< can be at most m_previousIndexes.size() - 1 */
        for ( auto it = m_previousIndexes.begin(), nit = std::next( it );
              nit != m_previousIndexes.end(); ++it, ++nit )
        {
            if ( *it == *nit + 1 ) {
                ++consecutiveCount;
            }
        }

        /* Handle special case of only random accesses. */
        if ( consecutiveCount == 0 ) {
            return {};
        }

        size_t lastConsecutiveCount = 0;
        for ( auto it = m_previousIndexes.begin(), nit = std::next( it );
              nit != m_previousIndexes.end(); ++it, ++nit )
        {
            if ( *it == *nit + 1 ) {
                ++lastConsecutiveCount;
            } else {
                break;
            }
        }

        /** 0 <= consecutiveRatio <= 1 */
        const auto consecutiveRatio = static_cast<double>( lastConsecutiveCount ) / ( m_previousIndexes.size() - 1 );
        /** 1 <= maxAmountToPrefetch +- floating point errors */
        const auto amountToPrefetch = std::round( std::exp2( consecutiveRatio * std::log2( maxAmountToPrefetch ) ) );

        assert( amountToPrefetch >= 0 );
        assert( static_cast<size_t>( amountToPrefetch ) <= maxAmountToPrefetch );

        std::vector<size_t> toPrefetch( static_cast<size_t>( amountToPrefetch ) );
        std::iota( toPrefetch.begin(), toPrefetch.end(), m_previousIndexes.front() + 1 );
        return toPrefetch;
    }

private:
    static constexpr size_t MEMORY_SIZE = 3;
    std::deque<size_t> m_previousIndexes;
};


/** @todo A prefetcher that can detect multiple interleaved consecutive patterns by sorting all last indexes and
 *        then search for consecutive (|diff neighbors| == 1) and return predictions for all of them at the same
 *        time relative to their length. Well, for bzip2, it sounds like the possibility of this happening is low.
 *        Even consecutive backward seeking should be a low frequency use case. */
}
