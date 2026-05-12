/*
 * RegisterAllocator.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */


#include "RegisterAllocator.h"
#include "BranchState.h"
#include "Error.h"

#include <iostream>
#include <iomanip>
#include <set>
#include <vector>
#include <stdlib.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RegisterAllocator::RegisterAllocator()
{
	unsigned int i;

	for( i = 0; i < sizeof(m_floats)/sizeof(Register); i++ )
	{
		std::stringstream s;
		s << "VF" << std::setw(2) << std::setfill('0') << i;
		m_floats[i].setName( s.str() );
	}

	for( i = 0; i < sizeof(m_integers)/sizeof(Register); i++ )
	{
		std::stringstream s;
		s << "VI" << std::setw(2) << std::setfill('0') << i;
		m_integers[i].setName( s.str() );
	}

	m_dynamicThreshold = 16;
	m_showRegisterInfo = false;
	m_currState = OUTSIDE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RegisterAllocator::~RegisterAllocator()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::setAvailableFloats( unsigned int floats )
{
	for( unsigned int i = 1; i < 32; i++ )
		m_floats[i].setAvailable( ( floats & (1<<i) ) != 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::setAvailableIntegers( unsigned int integers  )
{
	for( unsigned int i = 1; i < 16; i++ )
		m_integers[i].setAvailable( ( integers & (1<<i) ) != 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::process( std::list<Token>& tokens )
{
	BranchState* branchState = NULL;

	if( !collectLabels( tokens.begin(), tokens.end() ) )
		return false;

	for( std::list<Token>::iterator i = tokens.begin(), next = tokens.begin(); i != tokens.end(); i = next )
	{
		next++;

		switch( state() )
		{
			case OUTSIDE:
			{
				if( !(*i).operand() )
					continue;

				if( !((*i).operand()->flags() & Operand::PREPROCESSOR) )
				{
					Error::Display( Error( "Code not allowed outside entry point", *i ) );
					return false;
				}

				if( "--enter" == (*i).operand()->name() )
				{
					next = i;

					branchState = new BranchState( *this );
					assert( branchState );

					setState( ENTER );
				}
				if( ("--exit" == (*i).operand()->name()) || ("--exitm" == (*i).operand()->name()) )
				{
					next = i;
					setState( EXIT );
				}
				else
				{
				}
			}
			break;

			case ENTER:
			{
				if( !(*i).operand() )
					continue;

				if( (*i).operand()->unit() != Operand::ENTER )
				{
					Error::Display( Error( "Invalid operand inside --enter/--endenter block", *i ) );
					delete branchState;
					return false;
				}

				(*i).setFlags( (*i).flags() | Token::IGNORED );

				if( "in_vf" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to in_vf", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					branchState->setFloatInput( arg.immediate(), arg.regNumber() );
				}
				else if( "in_vi" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to in_vi", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					branchState->setIntegerInput( arg.immediate(), arg.regNumber() );
				}
				else if( "in_hw_acc" == (*i).operand()->name() )
					branchState->writeAccumulator( Token::X|Token::Y|Token::Z|Token::W );
				else if( "in_hw_i" == (*i).operand()->name() )
					branchState->writeI();
				else if( "in_hw_p" == (*i).operand()->name() )
					branchState->writeP();
				else if( "in_hw_q" == (*i).operand()->name() )
					branchState->writeQ();
				else if( "in_hw_r" == (*i).operand()->name() )
					branchState->writeR();
				else if( "--endenter" == (*i).operand()->name() )
				{
					m_states.push_back( branchState );
					setState( CODE );
				}
			}
			break;

			case CODE:
			{
				if( !branchState )
				{
					Error::Display( Error( "Internal error: missing branch state before CODE block" ) );
					return false;
				}
				// locate end of codeblock
				for( ; next != tokens.end(); next++ )
				{
					if( !(*next).operand() )
						continue;

					if( !((*next).operand()->flags() & Operand::PREPROCESSOR) )
						continue;

					if( ((*next).operand()->unit() == Operand::EXIT) || ((*next).operand()->unit() == Operand::ENTER) )
						break;

					if( !processCommonDirective( (*next) ) )
						return false;
				}

				branchState->setCurrent( i );
				branchState->setExitPoint( next );
				branchState->pushTraces( &*i, true );

				setState( EXIT );
			}
			break;

			case EXIT:
			{
				if( !(*i).operand() )
					continue;

				if( (*i).operand()->unit() != Operand::EXIT )
				{
					Error::Display( Error( "Invalid operand inside --exit/--endexit block", *i ) );
					return false;
				}

				(*i).setFlags( (*i).flags() | Token::IGNORED );

				if( "out_vf" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to out_vf", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());

					if( branchState )
						branchState->setFloatOutput( arg.immediate(), arg.regNumber() );
				}
				else if( "out_vi" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to out_vi", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					if( branchState )
						branchState->setIntegerOutput( arg.immediate(), arg.regNumber() );
				}
				else if( "--endexit" == (*i).operand()->name() )
				{
					setState( OUTSIDE );
				}
			}
			break;
		}

		if( !processCommonDirective( (*i) ) )
			return false;
	}

	std::list<BranchState*>::iterator j;
	for( j = m_states.begin(); j != m_states.end(); j++ )
	{
		if( !(*j) )
		{
			Error::Display( Error( "Internal error: null branch state before processing" ) );
			return false;
		}
		if( !processBranchState( *j, tokens.end() ) )
			return false;
	}

	collectLiteralRegisterUsage( tokens );
	extendContinuationLiveRanges( tokens );

	if( m_aliases.size() > 0 )
	{
		if( !processAliases() )
		{
			Error::Display( Error( "Register allocation ran out of registers" ) );
			return false;
		}
	}

	while( !m_states.empty() )
	{
		delete m_states.back();
		m_states.pop_back();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processBranchState( BranchState* state, std::list<Token>::iterator end )
{
	std::list<Token>::iterator curr, next;
	for( ; state->current() != end; state->setCurrent( curr ) )
	{
		curr = state->current();
		Token& token = *curr;
		curr++;

		if( !token.operand() )
			continue;

		// if we've reached a new entry or exit-point, abort branch
		if( (token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER) )
			break;

		token.setFlags( token.flags() | Token::PROCESSED );

		for( std::list<Token::Argument>::reverse_iterator i = token.arguments().rbegin(); i != token.arguments().rend(); i++ )
		{
			switch( (*i).type() )
			{
				case Token::Argument::FLOAT_REGISTER:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeFloat( (*i) );
					else
					{
						if( !state->readFloat( (*i) ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized float register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::INTEGER_REGISTER:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeInteger( (*i) );
					else
					{
						if( !state->readInteger( (*i) ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized integer register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::ACCUMULATOR:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeAccumulator( (*i).fields() );
					else
					{
						if( !state->readAccumulator( (*i).fields() ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized accumulator", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::Q:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeQ();
					else
					{
						if( !state->readQ() )
						{
							Error::Display( Error( "Read-attempt from uninitialized Q register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::P:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeP();
					else
					{
						if( !state->readP() )
						{
							Error::Display( Error( "Read-attempt from uninitialized P register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::R:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeR();
					else
					{
						if( !state->readR() )
						{
							Error::Display( Error( "Read-attempt from uninitialized R register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::I:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeI();
					else
					{
						if( !state->readI() )
						{
							Error::Display( Error( "Read-attempt from uninitialized I register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::IMMEDIATE:
				{
					if( token.operand()->flags() & Operand::IWRITE )
					{
						// special case for LOI
						state->writeI();
					}
				}
				break;

				default: break;
			}
		}

		if( token.operand()->unit() == Operand::BRU )
		{
			// branch instruction

			std::list<Token::Argument>::const_iterator branchDest = token.arguments().end();
			std::list<Token::Argument>::const_iterator branchStore = token.arguments().end();

			std::list<Token::Argument>::const_iterator i;
			for( i = token.arguments().begin(); i != token.arguments().end(); i++ )
			{
				if( (*i).flags() & Token::Argument::BRANCH )
					branchDest = i;

				if( (*i).flags() & Token::Argument::ADDRESS )
					branchStore = i;
			}

			if( token.arguments().end() == branchDest )
			{
				Error::Display( Error( "Invalid branch", token ) );
				return false;
			}

			if( token.arguments().end() != branchStore )
				state->storeAddress( *branchStore );

			std::list<Token>::iterator target;

			if( token.operand()->flags() & Operand::DYNAMIC )
			{
				if( !state->address( *branchDest, target, m_labels ) )
					continue;

				unsigned int targetLine = (*target).lineNumber();
				unsigned int currentLine = (*curr).lineNumber();
				bool isBackwardBranch = targetLine < currentLine;

				if( state->isBranchTaken( &*target ) )
				{
					if( isBackwardBranch )
					{
						// Backward branch (loop back-edge) already processed - extend ranges again and stop
						state->extendLiveRanges( targetLine, currentLine );
					}
					break;
				}

				// First time seeing this branch - extend ranges now if it's a backward branch
				if( isBackwardBranch )
				{
					state->extendLiveRanges( targetLine, currentLine );
				}

				if( !updateDynamicTracker( &*state->current() ) )
					break;

				state->storeBranch( &*target );

				BranchState* newState = new BranchState( *state );
				assert( newState );

				newState->pushTraces( &*curr, false );
				newState->pushTraces( &*target, true );
				newState->setCurrent( target );

				m_states.push_back( newState );
			}
			else
			{
				if( !state->address( *branchDest, target, m_labels ) )
					break;

				if( state->isBranchTaken( &*target ) )
				{
					// Backward branch (loop back-edge) - extend live ranges to cover loop body
					unsigned int targetLine = (*target).lineNumber();
					unsigned int currentLine = (*curr).lineNumber();
					if( targetLine < currentLine )
						state->extendLiveRanges( targetLine, currentLine );
					break;
				}

				state->storeBranch( &*target );

				state->pushTraces( &*curr, false );
				state->pushTraces( &*target, true );

				curr = target;
			}
		}
	}

	if( state->current() == state->exitPoint() )
	{
		if( !state->applyRegisterOutputs() )
		{
			Error::Display( Error( "Output register conflict", *(state->exitPoint()) ) );
			return false;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::collectLabels( std::list<Token>::iterator start, std::list<Token>::iterator end )
{
	m_labels.clear();

	for( std::list<Token>::iterator i = start; i != end; i++ )
	{
		if( (*i).label().length() )
		{
			std::map<std::string,std::list<Token>::iterator>::iterator j = m_labels.find( (*i).label() );

			if( j != m_labels.end() )
			{
				Error::Display( Error( "Duplicate label '" + (*i).label() + "'", *i ) );
				return false;
			}

			m_labels[ (*i).label() ] = i;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processAliases()
{
	// allocate registers that do not overlap usage

	// Count total aliases by type
	int totalFloatAliases = 0;
	int totalIntAliases = 0;
	int preallocatedFloats = 0;
	int preallocatedInts = 0;

	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		if( i->first->type() == Alias::FLOAT )
		{
			totalFloatAliases++;
			if( i->first->allocatedRegister() )
				preallocatedFloats++;
		}
		else
		{
			totalIntAliases++;
			if( i->first->allocatedRegister() )
				preallocatedInts++;
		}
	}

	// Count available registers
	int availableFloats = 0;
	int availableInts = 0;
	for( int i = 0; i < 32; i++ )
		if( m_floats[i].available() )
			availableFloats++;
	for( int i = 0; i < 16; i++ )
		if( m_integers[i].available() )
			availableInts++;

	if( m_showRegisterInfo )
	{
		std::cerr << "\n=== Register Allocation Debug ===" << std::endl;
		std::cerr << "Float registers: " << availableFloats << " available, "
		          << (totalFloatAliases - preallocatedFloats) << " needed (total aliases: "
		          << totalFloatAliases << ", preallocated: " << preallocatedFloats << ")" << std::endl;
		std::cerr << "Integer registers: " << availableInts << " available, "
		          << (totalIntAliases - preallocatedInts) << " needed (total aliases: "
		          << totalIntAliases << ", preallocated: " << preallocatedInts << ")" << std::endl;

		// Print all alias ranges for debugging
		std::cerr << "\n=== Alias Ranges ===" << std::endl;
		for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
		{
			Alias* alias = i->first;
			std::cerr << (alias->type() == Alias::FLOAT ? "FLOAT" : "INT") << ": ";
			if( alias->allocatedRegister() )
				std::cerr << "[prealloc: " << alias->allocatedRegister()->name() << "] ";
			alias->printRanges( std::cerr );
			std::cerr << std::endl;
		}
		std::cerr << "====================\n" << std::endl;
	}

	// Pre-pass: allocate every two-address chain ATOMICALLY (root +
	// all its known successors) before any singleton aliases run the
	// main loop.  Treating chain members one-at-a-time isn't enough,
	// because an unrelated singleton alias whose live range fits in
	// the same register can snipe it before the successor reaches the
	// main loop — leaving the successor with no choice but a different
	// register, which is exactly the bug we're trying to prevent (the
	// `isubiu` dest then disagrees with its source, the loop counter
	// never decrements, and xgkick never fires).
	//
	// For each unallocated root (no sameNamePredecessor), find every
	// alias whose chain root is this root, then pick a register that
	// no non-chain allocated alias intersects on ANY chain member's
	// range.  Assign that register to all chain members at once.
	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* root = i->first;
		if( root->allocatedRegister() )
			continue;
		if( root->sameNamePredecessor() )
			continue;

		// Collect chain members: walk every alias's predecessor chain
		// (with a hard hop cap as a cycle defence) and gather the ones
		// that terminate at `root`.  This is O(N²) but N is small.
		std::vector<Alias*> chain;
		chain.push_back( root );
		for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
		{
			Alias* other = k->first;
			if( other == root ) continue;
			if( other->type() != root->type() ) continue;
			Alias* p = other;
			for( int hop = 0; hop < 32 && p->sameNamePredecessor(); ++hop )
				p = p->sameNamePredecessor();
			if( p == root )
				chain.push_back( other );
		}

		unsigned int maxLimit = root->type() == Alias::FLOAT ? 32 : 16;
		for( unsigned int j = 0; j < maxLimit; ++j )
		{
			const Register* candidate = (root->type() == Alias::FLOAT) ? &m_floats[j] : &m_integers[j];
			if( !candidate->available() )
				continue;

			// `candidate` must not collide with any already-allocated
			// non-chain alias on ANY chain member's live range.
			bool conflict = false;
			for( AliasMap::iterator k = m_aliases.begin(); !conflict && k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;
				if( !src->allocatedRegister() ) continue;
				if( src->type() != root->type() ) continue;
				if( src->allocatedRegister() != candidate ) continue;
				// Chain members don't conflict with each other on the
				// shared register — that's the whole point.
				bool inChain = false;
				for( unsigned int m = 0; m < chain.size(); ++m )
					if( chain[m] == src ) { inChain = true; break; }
				if( inChain ) continue;
				for( unsigned int m = 0; m < chain.size(); ++m )
				{
					if( chain[m]->intersects( src ) )
					{
						conflict = true;
						break;
					}
				}
			}

			if( !conflict )
			{
				for( unsigned int m = 0; m < chain.size(); ++m )
					chain[m]->setAllocatedRegister( candidate );
				break;
			}
		}
	}

	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* dest = i->first;

		// preallocated register
		if( dest->allocatedRegister() )
			continue;

		unsigned int maxLimit = dest->type() == Alias::FLOAT ? 32 : 16;

		// Two-address coalescing: if `dest` was created by a write that
		// reuses an existing source-level alias name (e.g. the dest of
		// `isubiu x, x, 1`), try the predecessor's register FIRST.  The
		// predecessor may itself be unallocated yet; walk the chain to
		// find the earliest allocated ancestor.  Falls through to the
		// regular j=0..maxLimit scan if no predecessor / preferred reg
		// is available or it conflicts.
		const Register* preferred = NULL;
		// Walk to the root, with a hard cap to defeat any residual cycle
		// the branch-state analysis might have introduced via loop re-
		// processing.  16 is well above any plausible chain depth in
		// real shaders.
		Alias* chainRoot = dest;
		for( int hop = 0; hop < 16 && chainRoot->sameNamePredecessor(); ++hop )
			chainRoot = chainRoot->sameNamePredecessor();
		if( chainRoot != dest && chainRoot->allocatedRegister() )
			preferred = chainRoot->allocatedRegister();
		if( preferred )
		{
			// Run the normal conflict check against `preferred`.  If it
			// holds, assign immediately and skip the j-loop.
			bool conflict = false;
			for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;
				if( !src->allocatedRegister() ) continue;
				if( src->type() != dest->type() ) continue;
				if( src->allocatedRegister() != preferred ) continue;
				if( src == dest ) continue;
				// Same-name predecessor on the chain doesn't count as a
				// conflict: that's the whole point of coalescing.
				bool isAncestor = false;
				for( Alias* p = dest->sameNamePredecessor(); p; p = p->sameNamePredecessor() )
				{
					if( p == src ) { isAncestor = true; break; }
				}
				if( isAncestor ) continue;
				if( !dest->intersects( src ) ) continue;
				conflict = true;
				break;
			}
			if( !conflict )
			{
				dest->setAllocatedRegister( preferred );
				continue;
			}
		}

		for( unsigned int j = 0; j < maxLimit; ++j )
		{
			const Register* candidate = (dest->type() == Alias::FLOAT) ? &m_floats[j] : &m_integers[j];

			if( !candidate->available() )
				continue;

			for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;

				if( !src->allocatedRegister() )
					continue;

				if( src->type() != dest->type() )
					continue;

				if( src->allocatedRegister() != candidate )
					continue;

				if( !dest->intersects( src ) )
					continue;

				candidate = NULL;
				break;
			}

			if( candidate )
			{
				dest->setAllocatedRegister( candidate );
				break;
			}
		}

		if( !dest->allocatedRegister() )
		{
			if( m_showRegisterInfo )
			{
				std::cerr << "Failed to allocate " << (dest->type() == Alias::FLOAT ? "FLOAT" : "INTEGER")
				          << " register for alias" << std::endl;
			}
			return false;
		}
		//assert( dest->allocatedRegister() );
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	void extendContinuationBlockLiveRanges( std::list<Token>::iterator blockStart, std::list<Token>::iterator blockEnd )
	{
		for( std::list<Token>::iterator cont = blockStart; cont != blockEnd; ++cont )
		{
			if( !cont->operand() || cont->operand()->name() != "--cont" )
				continue;

			const unsigned int contLine = cont->lineNumber();
			unsigned int blockEndLine = contLine;
			std::set<Alias*> writtenBeforeCont;
			std::set<Alias*> liveAcrossCont;

			for( std::list<Token>::iterator t = blockStart; t != blockEnd; ++t )
			{
				if( t->lineNumber() > blockEndLine )
					blockEndLine = t->lineNumber();

				for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
				{
					if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
						continue;

					Alias* alias = a->dependency()->alias();
					if( t->lineNumber() < contLine && (a->flags() & Token::Argument::WRITE) )
						writtenBeforeCont.insert( alias );
					else if( t->lineNumber() > contLine
					         && !(a->flags() & Token::Argument::WRITE)
					         && writtenBeforeCont.find( alias ) != writtenBeforeCont.end() )
						liveAcrossCont.insert( alias );
				}
			}

			for( std::set<Alias*>::iterator a = liveAcrossCont.begin(); a != liveAcrossCont.end(); ++a )
				(*a)->addRange( contLine, blockEndLine );
		}
	}
}

void RegisterAllocator::extendContinuationLiveRanges( std::list<Token>& tokens )
{
	std::list<Token>::iterator blockStart = tokens.end();
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( !i->operand() )
			continue;

		if( i->operand()->name() == "--endenter" )
		{
			blockStart = i;
			++blockStart;
			continue;
		}

		const bool startsNewBlock = (i->operand()->unit() == Operand::ENTER);
		const bool startsExitBlock = (i->operand()->unit() == Operand::EXIT);
		if( blockStart == tokens.end() || (!startsNewBlock && !startsExitBlock) )
			continue;

		std::list<Token>::iterator blockEnd = i;
		extendContinuationBlockLiveRanges( blockStart, blockEnd );

		blockStart = tokens.end();
		if( startsNewBlock )
		{
			blockStart = i;
			++blockStart;
		}
	}

	if( blockStart != tokens.end() )
		extendContinuationBlockLiveRanges( blockStart, tokens.end() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::collectLiteralRegisterUsage( std::list<Token>& tokens )
{
	// One synthetic alias per (type, register) pair, with one range per
	// usage line.  Inserted into m_aliases pre-allocated to its physical
	// register so the existing conflict check in processAliases naturally
	// keeps user aliases off it.
	Alias* floats[32];
	Alias* integers[16];
	for( unsigned int i = 0; i < 32; i++ ) floats[i]   = NULL;
	for( unsigned int i = 0; i < 16; i++ ) integers[i] = NULL;

	for( std::list<Token>::iterator it = tokens.begin(); it != tokens.end(); ++it )
	{
		if( !it->operand() )
			continue;
		// Preprocessor directives (--enter, in_vf, .name, etc.) don't emit
		// hardware ops — their register references shouldn't pin physical
		// registers across the whole program.
		if( it->operand()->flags() & Operand::PREPROCESSOR )
			continue;
		if( it->flags() & Token::IGNORED )
			continue;

		const unsigned int line = it->lineNumber();

		for( std::list<Token::Argument>::const_iterator a = it->arguments().begin(); a != it->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::REGISTER )
				continue;

			if( a->type() == Token::Argument::FLOAT_REGISTER )
			{
				// Float side intentionally disabled — adding synthetic
				// VF aliases (even just for vf00, the zero register)
				// shifts the allocator's choices and ends up splitting
				// `vert_xform` between two VF sets across the [E]
				// halt boundary (load into VF05..VF08, use in
				// xform_loop as VF01..VF04 = garbage).  The integer-
				// side fix below is the one that matters for the
				// originally-motivating `do_clipping` / VI01 bug.
				(void)a;
				continue;
			}
			else if( a->type() == Token::Argument::INTEGER_REGISTER )
			{
				int r = a->regNumber();
				if( r < 0 || r >= 16 )
					continue;
				if( !integers[r] )
				{
					integers[r] = new Alias( Alias::INTEGER );
					integers[r]->setAllocatedRegister( &m_integers[r] );
					m_aliases[ integers[r] ] = integers[r];
				}
				integers[r]->addRange( line, line );
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Alias* RegisterAllocator::obtainAlias( Alias::Type type )
{
	Alias* alias = new Alias( type );

	m_aliases[ alias ] = alias;

	return alias;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::releaseAlias( Alias* alias )
{
	AliasMap::iterator i = m_aliases.find( alias );
	assert( i != m_aliases.end() );

	// Any alias whose same-name predecessor chain points at `alias` is
	// about to hold a dangling pointer; clear those edges so processAliases
	// doesn't walk into freed memory.  (Without this guard openvcl segfaults
	// when branch-state merges release one half of a previously-recorded
	// two-address pair.)
	for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
	{
		Alias* other = k->first;
		if( other == alias )
			continue;
		if( other->sameNamePredecessor() == alias )
			other->setSameNamePredecessor( NULL );
	}

	m_aliases.erase( i );

	delete alias;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processCommonDirective( Token& token )
{
	if( !token.operand() )
		return true;

	if( token.operand()->name() == ".name" )
	{
		m_name = token.arguments().begin()->immediate();
		token.setFlags( token.flags() | Token::IGNORED );
		return true;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::updateDynamicTracker( const Token* src )
{
	std::map< const Token*, unsigned int >::iterator i = m_dynamicTracker.find( src );

	if( m_dynamicTracker.end() == i )
	{
		m_dynamicTracker[ src ] = 1;
		return true;
	}

	if( i->second > m_dynamicThreshold )
		return false;

	m_dynamicTracker[ src ] = i->second+1;
	return true;
}


}
