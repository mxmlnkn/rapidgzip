#pragma once

#include <cstddef>
#include <limits>
#include <optional>


class BlockFinderInterface
{
public:
    [[nodiscard]] virtual size_t
    size() const = 0;

    [[nodiscard]] virtual bool
    finalized() const = 0;

    [[nodiscard]] virtual std::optional<size_t>
    get( size_t blockIndex,
         double timeoutInSeconds = std::numeric_limits<double>::infinity() ) = 0;

    [[nodiscard]] virtual size_t
    find( size_t encodedBlockOffsetInBits ) const = 0;
};
