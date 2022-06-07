#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>


template<typename T>
struct Statistics
{
    template<typename Container>
    explicit
    Statistics( const Container& values )
    {
        for ( const auto value : values ) {
            merge( static_cast<T>( value ) );
        }
    }

    template<typename Iterator>
    Statistics( const Iterator& begin,
                const Iterator& end )
    {
        for ( auto it = begin; it != end; ++it ) {
            merge( static_cast<T>( *it ) );
        }
    }

    [[nodiscard]] double
    average() const
    {
        return sum / count;
    }

    [[nodiscard]] double
    variance() const
    {
        /* Calculation makes use of expanded expression for the variance to avoid requiring there
         * average beforehand: Var(x) = < (x - <x>)^2 > = <x^2> - <x>^2
         * Furthermore, it is the sample variance, therefore divide by one count less because that
         * degree of freedom has been used up for the sample average calculation. */
        return sum2 / ( count - 1 ) - average() * average();
    }

    [[nodiscard]] double
    standardDeviation() const
    {
        return std::sqrt( variance() );
    }

    void
    merge( T value )
    {
        min = std::min( min, value );
        max = std::max( max, value );
        sum += value;
        sum2 += std::pow( static_cast<double>( value ), 2 );
        ++count;
    }

public:
    T min{ std::numeric_limits<T>::infinity() };
    T max{ -std::numeric_limits<T>::infinity() };

    double sum{ 0 };
    double sum2{ 0 };
    uint64_t count{ 0 };
};


template<typename T>
class Histogram
{
public:
    template<typename Container>
    explicit
    Histogram( const Container& container,
               uint16_t         binCount,
               std::string      unit = {} ) :
        m_statistics( container ),
        m_bins( binCount, 0 ),
        m_unit( std::move( unit ) )
    {
        if ( !std::isfinite( m_statistics.min )
            || !std::isfinite( m_statistics.max )
            || ( m_statistics.min == m_statistics.max ) )
        {
            return;
        }

        for ( const auto value : container ) {
            merge( static_cast<T>( value ) );
        }
    }

    bool
    merge( T value )
    {
        if ( !std::isfinite( value )
             || ( value < m_statistics.min )
             || ( value > m_statistics.max )
             || m_bins.empty() )
        {
            return false;
        }

        const auto unitValue = static_cast<double>( value - m_statistics.min )
                               / static_cast<double>( m_statistics.max - m_statistics.min );
        const auto index =
            value == m_statistics.max
            ? m_bins.size() - 1U
            : static_cast<size_t>( std::floor( unitValue * m_bins.size() ) );

        m_bins.at( index ) += 1;
        return true;
    }

    [[nodiscard]] const auto&
    statistics() const
    {
        return m_statistics;
    }

    [[nodiscard]] double
    binStart( size_t binNumber ) const
    {
        return m_statistics.min + ( m_statistics.max - m_statistics.min ) / m_bins.size() * binNumber;
    }

    [[nodiscard]] double
    binCenter( size_t binNumber ) const
    {
        return m_statistics.min + ( m_statistics.max - m_statistics.min ) / m_bins.size() * ( binNumber + 0.5 );
    }

    [[nodiscard]] double
    binEnd( size_t binNumber ) const
    {
        return m_statistics.min + ( m_statistics.max - m_statistics.min ) / m_bins.size() * ( binNumber + 1 );
    }

    [[nodiscard]] const auto&
    bins() const
    {
        return m_bins;
    }

    [[nodiscard]] std::string
    plot() const
    {
        if ( m_bins.size() <= 1 ) {
            return {};
        }

        std::stringstream result;
        const auto maxBin = std::max_element( m_bins.begin(), m_bins.end() );

        std::vector<std::string> binLabels{ m_bins.size() };
        binLabels.back() = formatLabel( m_statistics.max );
        binLabels.front() = formatLabel( m_statistics.min );
        for ( size_t i = 1; i < m_bins.size() - 1; ++i ) {
            if ( i == static_cast<size_t>( std::distance( m_bins.begin(), maxBin ) ) ) {
                binLabels[i] = formatLabel( binCenter( i ) );
            }
        }

        const auto maxLabelLength = std::max_element(
            binLabels.begin(), binLabels.end(),
            [] ( const auto& a, const auto& b ) { return a.size() < b.size(); } )->size();

        for ( size_t i = 0; i < m_bins.size(); ++i ) {
            const auto bin = m_bins[i];

            std::stringstream binLabel;
            binLabel << std::setw( maxLabelLength ) << std::right << binLabels[i];

            const auto binVisualSize = static_cast<size_t>( static_cast<double>( bin ) / *maxBin * m_barWidth );
            std::stringstream histogramBar;
            histogramBar << std::setw( m_barWidth ) << std::left << std::string( binVisualSize, '=' );

            const auto countLabel = bin > 0 ? "(" + std::to_string( bin ) + ")" : std::string();

            result << binLabel.str() << " |" << histogramBar.str() << " " << countLabel << "\n";
        }

        return result.str();
    }

private:
    [[nodiscard]] std::string
    formatLabel( double value ) const
    {
        std::stringstream result;
        if ( std::round( value ) != value ) {
            result << std::scientific;
        }
        result << value;

        if ( !m_unit.empty() ) {
            result << " " << m_unit;
        }

        return result.str();
    }

private:
    const Statistics<T> m_statistics;
    std::vector<uint64_t> m_bins;

    const std::string m_unit;
    const uint16_t m_barWidth{ 20 };
};
