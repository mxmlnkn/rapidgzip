#pragma once

#include <iterator>
#include <map>
#include <optional>
#include <utility>


namespace CacheStrategy
{
template<typename Index>
class CacheStrategy
{
public:
    virtual ~CacheStrategy() = default;
    virtual void touch( Index index ) = 0;
    [[nodiscard]] virtual std::optional<Index> evict() = 0;
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
    evict() override
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

        auto indexToEvict = lowest->first;
        m_lastUsage.erase( lowest );
        return indexToEvict;
    }

private:
    /* With this, inserting will be relatively fast but eviction make take longer
     * because we have to go over all elements. */
    std::map<Index, size_t> m_lastUsage;
    size_t usageNonce{ 0 };
};
}


template<
    typename Key,
    typename Value,
    typename CacheStrategy = CacheStrategy::LeastRecentlyUsed<Key>
>
class Cache
{
public:
    explicit
    Cache( size_t maxCacheSize ) :
        m_maxCacheSize( maxCacheSize )
    {}

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

    [[nodiscard]] std::optional<Value>
    get( const Key& key )
    {
        if ( const auto match = m_cache.find( key ); match != m_cache.end() ) {
            ++m_hits;
            m_cacheStrategy.touch( key );
            return match->second;
        }

        ++m_misses;
        return std::nullopt;
    }

    void
    insert( Key   key,
            Value value )
    {
        while ( m_cache.size() >= m_maxCacheSize ) {
            if ( const auto toEvict = m_cacheStrategy.evict(); toEvict ) {
                m_cache.erase( *toEvict );
            } else {
                m_cache.erase( m_cache.begin() );
            }
        }

        const auto [match, wasInserted] = m_cache.try_emplace( std::move( key ), std::move( value ) );
        if ( !wasInserted ) {
            match->second = std::move( value );
        }

        m_cacheStrategy.touch( key );
    }

    [[nodiscard]] size_t
    hits() const
    {
        return m_hits;
    }

    [[nodiscard]] size_t
    misses() const
    {
        return m_misses;
    }

    void
    resetStatistics()
    {
        m_hits = 0;
        m_misses = 0;
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

private:
    CacheStrategy m_cacheStrategy;
    size_t const m_maxCacheSize;
    std::map<Key, Value > m_cache;

    /* Analytics */
    size_t m_hits = 0;
    size_t m_misses = 0;
};
