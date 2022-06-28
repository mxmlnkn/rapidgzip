#pragma once

#include <memory>
#include <utility>

#include "Interface.hpp"


namespace pragzip::blockfinder
{
/**
 * Returns the first find, then skips n finds and returns the next find, and so on.
 */
class Skipping :
    public Interface
{
public:
    Skipping( std::unique_ptr<Interface> blockFinder,
              size_t                     nToSkip ) :
        m_blockFinder( std::move( blockFinder ) ),
        m_nToSkip( nToSkip )
    {}

    [[nodiscard]] size_t
    find() override
    {
        if ( !m_firstFound ) {
            m_firstFound = true;
            return m_blockFinder->find();
        }

        for ( size_t i = 0; i < m_nToSkip; ++i ) {
            [[maybe_unused]] const auto _ = m_blockFinder->find();
        }
        return m_blockFinder->find();
    }

private:
    const std::unique_ptr<Interface> m_blockFinder;
    const size_t m_nToSkip;
    bool m_firstFound{ false };
};
}  // pragzip::blockfinder
