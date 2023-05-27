#pragma once

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "common.hpp"  // duration


int gnTests = 0;  // NOLINT
int gnTestErrors = 0;  // NOLINT


template<typename A,
         typename B>
void
requireEqual( const A&  a,
              const B&  b,
              const int line )
{
    ++gnTests;
    if ( a != b ) {
        ++gnTestErrors;
        std::cerr << "[FAIL on line " << line << "] " << a << " != " << b << "\n";
    }
}


void
require( bool               condition,
         std::string const& conditionString,
         int                line )
{
    ++gnTests;
    if ( !condition ) {
        ++gnTestErrors;
        std::cerr << "[FAIL on line " << line << "] " << conditionString << "\n";
    }
}


#define REQUIRE_EQUAL( a, b ) requireEqual( a, b, __LINE__ )  // NOLINT
#define REQUIRE( condition ) require( condition, #condition, __LINE__ )  // NOLINT


template<
    size_t   REPETITIONS,
    typename Functor,
    typename FunctorResult = std::invoke_result_t<Functor>
>
[[nodiscard]] std::pair<FunctorResult, std::vector<double> >
benchmarkFunction( Functor functor )
{
    std::optional<FunctorResult> result;
    std::vector<double> durations;
    for ( size_t i = 0; i < REPETITIONS; ++i ) {
        const auto t0 = now();
        const auto currentResult = functor();
        const auto t1 = now();
        durations.push_back( duration( t0, t1 ) );

        if ( !result ) {
            result = std::move( currentResult );
        } else if ( *result != currentResult ) {
            throw std::logic_error( "Function to benchmark returns indeterministic results!" );
        }
    }

    return { *result, durations };
}


template<
    size_t   REPETITIONS,
    typename Functor,
    typename SetupFunctor,
    typename FunctorResult = std::invoke_result_t<Functor, std::invoke_result_t<SetupFunctor> >
>
[[nodiscard]] std::pair<FunctorResult, std::vector<double> >
benchmarkFunction( SetupFunctor setup,
                   Functor      functor )
{
    decltype( setup() ) setupResult;
    try {
        setupResult = setup();
    } catch ( const std::exception& e ) {
        std::cerr << "Failed to run setup with exception: " << e.what() << "\n";
        return {};
    }

    std::optional<FunctorResult> result;
    std::vector<double> durations;
    for ( size_t i = 0; i < REPETITIONS; ++i ) {
        const auto t0 = now();
        auto currentResult = functor( setupResult );
        const auto t1 = now();
        durations.push_back( duration( t0, t1 ) );

        if ( !result ) {
            result = std::move( currentResult );
        } else if ( *result != currentResult ) {
            throw std::logic_error( "Function to benchmark returns indeterministic results!" );
        }
    }

    return { *result, durations };
}
