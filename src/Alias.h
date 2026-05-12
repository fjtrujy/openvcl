#ifndef __OPENVCL_ALIAS_H__
#define __OPENVCL_ALIAS_H__

/*
 * Alias.h
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Register.h"

#include <list>
#include <iostream>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Alias
{

public:

	enum Type
	{
		FLOAT,
		INTEGER
	};

	struct Range
	{
		unsigned int m_start;
		unsigned int m_stop;
	};

	Alias( Type type );

	void setAllocatedRegister( const Register* allocated );
	const Register* allocatedRegister() const;

	Type type() const;

	void addRange( unsigned int start, unsigned int stop );
	void merge( Alias* alias );
	bool intersects( Alias* alias );
	bool hasRangeOverlapping( unsigned int start, unsigned int stop ) const;
	bool hasRangeStartingBefore( unsigned int line ) const;
	void printRanges( std::ostream& os ) const;

	// Two-address hint.  When the parser emits an instruction like
	// `isubiu x, x, 1`, openvcl creates two Alias objects for `x` — the
	// read picks up the existing alias, the write spawns a fresh one.
	// Without coordination the allocator can put them on different VI
	// regs, so the counter is decremented in a stale register, the
	// loop never reaches zero, and xgkick is never fired.  We record
	// the prior (read) alias on the new (write) alias so the allocator
	// can prefer its allocated register first.
	void setSameNamePredecessor( Alias* predecessor );
	Alias* sameNamePredecessor() const;

private:

	Type m_type;

	const Register* m_allocatedRegister;
	Alias* m_sameNamePredecessor;
	std::list<Range> m_ranges;

};

#include "Alias.inl"

}

#endif
