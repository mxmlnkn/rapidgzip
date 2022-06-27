#pragma once

#include <memory>
#include <utility>

#include "Bgzf.hpp"
#include "OffsetFinderInterface.hpp"
#include "PigzStringView.hpp"


class Combined :
    public OffsetFinderInterface
{
public:
    Combined( std::unique_ptr<FileReader> fileReader ) :
        m_blockFinder(
            BgzfBlockFinder::isBgzfFile( fileReader )
            ? static_cast<OffsetFinderInterface*>( new BgzfBlockFinder( std::move( fileReader ) ) )
            : static_cast<OffsetFinderInterface*>( new PigzBlockFinderStringView( std::move( fileReader ) ) )
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
    const std::unique_ptr<OffsetFinderInterface> m_blockFinder;
};
