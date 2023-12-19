#pragma once

#include <string>

#include <cxxopts.hpp>


std::string
getFilePath( cxxopts::ParseResult const& parsedArgs,
             std::string          const& argument )
{
    if ( parsedArgs.count( argument ) > 0 ) {
        auto path = parsedArgs[argument].as<std::string>();
        if ( path != "-" ) {
            return path;
        }
    }
    return {};
}
