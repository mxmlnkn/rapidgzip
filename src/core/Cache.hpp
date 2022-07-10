#pragma once

#include <iterator>
#include <optional>
#include <unordered_map>
#include <utility>


namespace CacheStrategy
{
template<typename Index>
class CacheStrategy
{
public:
    virtual
    ~CacheStrategy() = default;

    virtual void
    touch( Index index ) = 0;

    [[nodiscard]] virtual std::optional<Index>
    nextEviction() const = 0;

    /**
     * @param indexToEvict If an index is given, that index will be removed if it exists instead of using
     *                     the cache strategy.
     */
    virtual std::optional<Index>
    evict( std::optional<Index> indexToEvict = {} ) = 0;
};


template<typename Index>
class LeastRecentlyUsed :
    public CacheStrategy<Index>
{
public:
    LeastRecentlyUsed() = default;

    void
    touch( Index index ) override
    {
        ++usageNonce;
        auto [match, wasInserted] = m_lastUsage.try_emplace( std::move( index ), usageNonce );
        if ( !wasInserted ) {
            match->second = usageNonce;
        }
    }

    [[nodiscard]] std::optional<Index>
    nextEviction() const override
    {
        if ( m_lastUsage.empty() ) {
            return std::nullopt;
        }

        auto lowest = m_lastUsage.begin();
        for ( auto it = std::next( lowest ); it != m_lastUsage.end(); ++it ) {
            if ( it->second < lowest->second ) {
                lowest = it;
            }
        }

        return lowest->first;
    }

    std::optional<Index>
    evict( std::optional<Index> indexToEvict = {} ) override
    {
        if ( indexToEvict && ( m_lastUsage.find( *indexToEvict ) != m_lastUsage.end() ) ) {
            m_lastUsage.erase( *indexToEvict );
            return indexToEvict;
        }

        auto evictedIndex = nextEviction();
        if ( evictedIndex ) {
            m_lastUsage.erase( *evictedIndex );
        }
        return evictedIndex;
    }

private:
    /* With this, inserting will be relatively fast but eviction make take longer
     * because we have to go over all elements. */
    std::unordered_map<Index, size_t> m_lastUsage;
    size_t usageNonce{ 0 };
};
}


/**
 * @ref get and @ref insert should be sufficient for simple cache usages.
 * For advanced control, there are also @ref touch, @ref clear, @ref evict, and @ref test available.
 */
template<
    typename Key,
    typename Value,
    typename CacheStrategy = CacheStrategy::LeastRecentlyUsed<Key>
>
class Cache
{
public:
    struct Statistics
    {
        size_t hits{ 0 };
        size_t misses{ 0 };
        size_t unusedEntries{ 0 };
        size_t capacity{ 0 };
    };

public:
    explicit
    Cache( size_t maxCacheSize ) :
        m_maxCacheSize( maxCacheSize )
    {}

    [[nodiscard]] std::optional<Value>
    get( const Key& key )
    {
        if ( const auto match = m_cache.find( key ); match != m_cache.end() ) {
            ++m_statistics.hits;
            ++m_accesses[key];
            m_cacheStrategy.touch( key );
            return match->second;
        }

        ++m_statistics.misses;
        return std::nullopt;
    }

    void
    insert( Key   key,
            Value value )
    {
        if ( capacity() == 0 ) {
            return;
        }

        while ( m_cache.size() >= m_maxCacheSize ) {
            const auto toEvict = m_cacheStrategy.evict();
            assert( toEvict );
            const auto keyToEvict = toEvict ? *toEvict : m_cache.begin()->first;
            m_cache.erase( keyToEvict );

            if ( const auto match = m_accesses.find( keyToEvict ); match != m_accesses.end() ) {
                if ( match->second == 0 ) {
                    m_statistics.unusedEntries++;
                }
                m_accesses.erase( match );
            }
        }

        if ( const auto match = m_accesses.find( key ); match == m_accesses.end() ) {
            m_accesses[key] = 0;
        }

        const auto [match, wasInserted] = m_cache.try_emplace( key, std::move( value ) );
        if ( !wasInserted ) {
            match->second = std::move( value );
        }

        m_cacheStrategy.touch( key );
    }

    /* Advanced Control and Usage */

    void
    touch( const Key& key )
    {
        m_cacheStrategy.touch( key );
    }

    [[nodiscard]] bool
    test( const Key& key ) const
    {
        return m_cache.find( key ) != m_cache.end();
    }

    void
    clear()
    {
        m_cache.clear();
    }

    void
    evict( const Key& key )
    {
        m_cacheStrategy.evict( key );
        m_cache.erase( key );
    }

    /* Analytics */

    [[nodiscard]] Statistics
    statistics() const
    {
        auto result = m_statistics;
        result.capacity = capacity();
        return result;
    }

    void
    resetStatistics()
    {
        m_statistics = Statistics{};
        m_accesses.clear();
    }

    [[nodiscard]] size_t
    capacity() const
    {
        return m_maxCacheSize;
    }

    [[nodiscard]] size_t
    size() const
    {
        return m_cache.size();
    }

    [[nodiscard]] const CacheStrategy&
    cacheStrategy() const
    {
        return m_cacheStrategy;
    }

private:
    CacheStrategy m_cacheStrategy;
    size_t const m_maxCacheSize;
    std::unordered_map<Key, Value > m_cache;

    /* Analytics */
    Statistics m_statistics;
    std::unordered_map<Key, size_t> m_accesses;
};
