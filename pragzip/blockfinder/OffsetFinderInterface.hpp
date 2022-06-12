#pragma once


class OffsetFinderInterface
{
public:
    virtual ~OffsetFinderInterface() = default;

    [[nodiscard]] virtual size_t
    find() = 0;
};
