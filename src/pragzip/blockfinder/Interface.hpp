#pragma once


namespace pragzip::blockfinder
{
class Interface
{
public:
    virtual ~Interface() = default;

    [[nodiscard]] virtual size_t
    find() = 0;
};
}  // pragzip::blockfinder
