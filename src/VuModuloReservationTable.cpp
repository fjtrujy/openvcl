#include "VuModuloReservationTable.h"

namespace vcl
{

ModuloReservationTable::ModuloReservationTable( unsigned int initiationInterval )
    : m_ii( initiationInterval == 0 ? 1 : initiationInterval ),
      m_upper( m_ii, static_cast< unsigned int >( SLOT_FREE ) ),
      m_lower( m_ii, static_cast< unsigned int >( SLOT_FREE ) ),
      m_fdiv ( m_ii, static_cast< unsigned int >( SLOT_FREE ) ),
      m_efu  ( m_ii, static_cast< unsigned int >( SLOT_FREE ) ),
      m_upperOccupancy( 0 ),
      m_lowerOccupancy( 0 ),
      m_fdivOccupancy( 0 ),
      m_efuOccupancy( 0 )
{
}

bool ModuloReservationTable::laneIsClear( const std::vector< unsigned int >& lane,
                                          unsigned int cycle,
                                          unsigned int duration ) const
{
    if( duration == 0 )
        return true;

    // A reservation longer than II would self-collide; cap at II so the
    // caller's intent ("this lane is fully busy") is detectable as a single
    // sweep failure rather than an arithmetic surprise.
    if( duration > m_ii )
        duration = m_ii;

    for( unsigned int k = 0; k < duration; ++k )
    {
        const unsigned int slot = ( cycle + k ) % m_ii;
        if( lane[ slot ] != static_cast< unsigned int >( SLOT_FREE ) )
            return false;
    }
    return true;
}

void ModuloReservationTable::markLane( std::vector< unsigned int >& lane,
                                       unsigned int cycle,
                                       unsigned int duration,
                                       unsigned int tokenIndex,
                                       unsigned int& occupancyCounter )
{
    if( duration == 0 )
        return;
    if( duration > m_ii )
        duration = m_ii;

    for( unsigned int k = 0; k < duration; ++k )
    {
        const unsigned int slot = ( cycle + k ) % m_ii;
        lane[ slot ] = tokenIndex;
        ++occupancyCounter;
    }
}

bool ModuloReservationTable::canReserveUpper( unsigned int cycle ) const
{
    return laneIsClear( m_upper, cycle, 1 );
}

bool ModuloReservationTable::reserveUpper( unsigned int cycle, unsigned int tokenIndex )
{
    if( !canReserveUpper( cycle ) )
        return false;
    markLane( m_upper, cycle, 1, tokenIndex, m_upperOccupancy );
    return true;
}

bool ModuloReservationTable::canReserveLower( unsigned int cycle ) const
{
    return laneIsClear( m_lower, cycle, 1 );
}

bool ModuloReservationTable::reserveLower( unsigned int cycle, unsigned int tokenIndex )
{
    if( !canReserveLower( cycle ) )
        return false;
    markLane( m_lower, cycle, 1, tokenIndex, m_lowerOccupancy );
    return true;
}

bool ModuloReservationTable::canReserveFdiv( unsigned int cycle, unsigned int duration ) const
{
    return laneIsClear( m_fdiv, cycle, duration );
}

bool ModuloReservationTable::reserveFdiv( unsigned int cycle, unsigned int duration, unsigned int tokenIndex )
{
    if( !canReserveFdiv( cycle, duration ) )
        return false;
    markLane( m_fdiv, cycle, duration, tokenIndex, m_fdivOccupancy );
    return true;
}

bool ModuloReservationTable::canReserveEfu( unsigned int cycle, unsigned int duration ) const
{
    return laneIsClear( m_efu, cycle, duration );
}

bool ModuloReservationTable::reserveEfu( unsigned int cycle, unsigned int duration, unsigned int tokenIndex )
{
    if( !canReserveEfu( cycle, duration ) )
        return false;
    markLane( m_efu, cycle, duration, tokenIndex, m_efuOccupancy );
    return true;
}

unsigned int ModuloReservationTable::upperAt( unsigned int cycle ) const
{
    return m_upper[ cycle % m_ii ];
}

unsigned int ModuloReservationTable::lowerAt( unsigned int cycle ) const
{
    return m_lower[ cycle % m_ii ];
}

unsigned int ModuloReservationTable::fdivAt( unsigned int cycle ) const
{
    return m_fdiv[ cycle % m_ii ];
}

unsigned int ModuloReservationTable::efuAt( unsigned int cycle ) const
{
    return m_efu[ cycle % m_ii ];
}

} // namespace vcl
