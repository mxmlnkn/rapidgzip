#pragma once

#include <memory>
#include <utility>

#include "Bgzf.hpp"
#include "Interface.hpp"
#include "PigzStringView.hpp"


namespace pragzip::blockfinder
{
class Combined :
    public Interface
{
public:
    explicit
    Combined( std::unique_ptr<FileReader> fileReader ) :
        m_blockFinder(
            Bgzf::isBgzfFile( fileReader )
            ? static_cast<Interface*>( new Bgzf( std::move( fileReader ) ) )
            : static_cast<Interface*>( new PigzStringView( std::move( fileReader ) ) )
        )
    {}

    /**
     * @return offset of deflate block in bits (not the gzip stream offset!).
     */
    [[nodiscard]] size_t
    find() override
    {
        return m_blockFinder->find();
    }

private:
    const std::unique_ptr<Interface> m_blockFinder;
};
}  // pragzip::blockfinder
