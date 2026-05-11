/*
 * Alias.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Alias.h"

#include <assert.h>

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::addRange( unsigned int start, unsigned int stop )
{
	assert( start <= stop );

	// find range intersections or immediately adjacent ranges
	// Adjacent ranges (where one ends exactly where another starts)
	// should be merged to handle loop-carried dependencies

	for( std::list<Range>::iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		Range r = *i;

		// Check for overlap
		bool overlaps = (((start >= r.m_start) && (start <= r.m_stop)) || ((stop >= r.m_start) && (stop <= r.m_stop))) ||
		                (((r.m_start >= start) && (r.m_start <= stop)) || ((r.m_stop >= start) && (r.m_stop <= stop)));

		// Check for immediate adjacency (r ends where we start, or we end where r starts)
		bool adjacent = (r.m_stop + 1 == start) || (stop + 1 == r.m_start);

		if( overlaps || adjacent )
		{
			// range intersects or is immediately adjacent, merge

			m_ranges.erase(i);
			addRange( min(start,r.m_start), max(stop, r.m_stop) );

			return;
		}
	}

	// no previous range intersected or was adjacent, insert into list

	Range newRange;
	newRange.m_start = start;
	newRange.m_stop = stop;

	m_ranges.push_back( newRange );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::merge( Alias* alias )
{
	if( alias == this )
		return;

	assert( !(alias->allocatedRegister() && allocatedRegister()) || (alias->allocatedRegister() == allocatedRegister()) );

	if( alias->allocatedRegister() )
		setAllocatedRegister( alias->allocatedRegister() );	

	for( std::list<Range>::iterator i = alias->m_ranges.begin(); i != alias->m_ranges.end(); ++i )
		addRange( (*i).m_start, (*i).m_stop );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::intersects( Alias* alias )
{
	// TODO: if we sort the ranges before intersecting, this can get a lot faster

	for( std::list<Range>::iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		Range& r1 = *i;

		for( std::list<Range>::iterator j = alias->m_ranges.begin(); j != alias->m_ranges.end(); ++j )
		{
			Range& r2 = *j;

			// Check for actual overlap
			bool overlaps = (((r1.m_start >= r2.m_start) && (r1.m_start <= r2.m_stop)) || ((r1.m_stop >= r2.m_start) && (r1.m_stop <= r2.m_stop))) ||
			                (((r2.m_start >= r1.m_start) && (r2.m_start <= r1.m_stop)) || ((r2.m_stop >= r1.m_start) && (r2.m_stop <= r1.m_stop)));

			if( overlaps )
			{
				return true;
			}
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::hasRangeOverlapping( unsigned int start, unsigned int stop ) const
{
	// Check if any of this alias's ranges overlap with [start, stop]
	for( std::list<Range>::const_iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		const Range& r = *i;
		// Check if [r.m_start, r.m_stop] overlaps with [start, stop]
		if(
			(((start >= r.m_start) && (start <= r.m_stop)) || ((stop >= r.m_start) && (stop <= r.m_stop))) ||
			(((r.m_start >= start) && (r.m_start <= stop)) || ((r.m_stop >= start) && (r.m_stop <= stop)))
		)
		{
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::hasRangeStartingBefore( unsigned int line ) const
{
	// Check if any of this alias's ranges start before the given line
	for( std::list<Range>::const_iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		if( i->m_start < line )
		{
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::printRanges( std::ostream& os ) const
{
	os << "ranges: ";
	for( std::list<Range>::const_iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		if( i != m_ranges.begin() )
			os << ", ";
		os << "[" << i->m_start << "-" << i->m_stop << "]";
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
