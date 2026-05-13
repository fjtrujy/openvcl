#include "VuSchedulerAnalysis.h"

#include "VuTokenResourceAccess.h"

#include <string>

namespace vcl
{

namespace
{
	bool containsKey( const std::list<std::string>& keys, const std::string& key )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			if( *i == key )
				return true;
		}
		return false;
	}

	bool intersects( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( containsKey( b, *i ) )
				return true;
		}
		return false;
	}

	bool isBoundaryOperand( const Token& token )
	{
		if( !token.operand() )
			return false;

		const std::string& name = token.operand()->name();
		return name == "--barrier"
		    || name == "--cont"
		    || name == "--enter"
		    || name == "--endenter"
		    || name == "--exit"
		    || name == "--endexit";
	}

	bool isMemoryOrderingAccess( const VuTokenResourceAccess& access )
	{
		return access.memoryKind == VU_MEMORY_STORE
		    || access.memoryKind == VU_MEMORY_XGKICK
		    || access.memoryFlags != VU_MEMORY_FLAG_NONE;
	}

	bool isReadyScheduleCandidate( const Token& token )
	{
		if( token.label().length() != 0 )
			return false;
		if( !token.operand() )
			return false;
		if( token.operand()->isPreprocessor() )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::IGNORED | Token::E | Token::D | Token::T) )
			return false;
		if( !token.operand()->isUpperExecutionPath() && !token.operand()->isLowerExecutionPath() )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.branchDelaySlots > 0 )
			return false;
		if( access.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P | VU_INSTR_BRANCH) )
			return false;
		if( access.memoryKind != VU_MEMORY_NONE || access.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;
		if( access.implicitReads & (VU_RESOURCE_MAC | VU_RESOURCE_CLIP) )
			return false;

		return true;
	}

	bool isBlockBarrier( const Token& token )
	{
		if( token.flags() & Token::PREORDERED )
			return true;
		if( isBoundaryOperand( token ) )
			return true;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;

		return access.branchDelaySlots > 0
		    || access.memoryKind == VU_MEMORY_XGKICK;
	}

	void addEdge( std::vector<VuDependencyEdge>& edges, unsigned int before, unsigned int after, VuDependencyKind kind )
	{
		edges.push_back( VuDependencyEdge( before, after, kind ) );
	}

	bool isLowerPipe( const Token& token )
	{
		return token.operand() && token.operand()->isLowerExecutionPath();
	}

	bool isLongLatencyProducer( const VuTokenResourceAccess& access )
	{
		return (access.instructionFlags & (VU_INSTR_WRITES_Q | VU_INSTR_WRITES_P)) != 0;
	}

	int readyCandidateScore( unsigned int candidate,
	                         bool haveLastPipe,
	                         bool lastWasLower,
	                         const VuBasicBlock& block,
	                         const std::vector<VuTokenResourceAccess>& accesses )
	{
		int score = static_cast<int>( candidate );

		if( isLongLatencyProducer( accesses[candidate] ) )
			score -= 500;

		if( haveLastPipe && isLowerPipe( *block.tokens[candidate] ) != lastWasLower )
			score -= 100;

		return score;
	}

	void appendReadyScheduledSegment( const std::vector<const Token*>& segment, std::list<Token>& scheduled )
	{
		if( segment.size() < 2 )
		{
			for( std::vector<const Token*>::const_iterator i = segment.begin(); i != segment.end(); ++i )
				scheduled.push_back( **i );
			return;
		}

		VuBasicBlock block;
		block.tokens = segment;

		std::vector<VuTokenResourceAccess> accesses;
		for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
		{
			VuTokenResourceAccess access;
			buildVuTokenResourceAccess( **i, access );
			accesses.push_back( access );
		}

		std::vector<VuDependencyEdge> edges = buildVuDependencyGraph( block );
		std::vector<unsigned int> incoming( block.tokens.size(), 0 );
		std::vector< std::vector<unsigned int> > outgoing( block.tokens.size() );

		for( std::vector<VuDependencyEdge>::const_iterator i = edges.begin(); i != edges.end(); ++i )
		{
			if( i->before >= block.tokens.size() || i->after >= block.tokens.size() )
				continue;
			++incoming[i->after];
			outgoing[i->before].push_back( i->after );
		}

		std::vector<bool> emitted( block.tokens.size(), false );
		unsigned int emittedCount = 0;
		bool haveLastPipe = false;
		bool lastWasLower = false;

		while( emittedCount < block.tokens.size() )
		{
			unsigned int best = static_cast<unsigned int>( block.tokens.size() );
			int bestScore = 0;

			for( unsigned int i = 0; i < block.tokens.size(); ++i )
			{
				if( emitted[i] || incoming[i] != 0 )
					continue;

				const int score = readyCandidateScore( i, haveLastPipe, lastWasLower, block, accesses );
				if( best == block.tokens.size() || score < bestScore )
				{
					best = i;
					bestScore = score;
				}
			}

			if( best == block.tokens.size() )
			{
				for( unsigned int i = 0; i < block.tokens.size(); ++i )
				{
					if( !emitted[i] )
						scheduled.push_back( *block.tokens[i] );
				}
				return;
			}

			scheduled.push_back( *block.tokens[best] );
			emitted[best] = true;
			++emittedCount;
			haveLastPipe = true;
			lastWasLower = isLowerPipe( *block.tokens[best] );

			for( std::vector<unsigned int>::const_iterator i = outgoing[best].begin(); i != outgoing[best].end(); ++i )
			{
				if( incoming[*i] > 0 )
					--incoming[*i];
			}
		}
	}

	void appendReadyScheduledBlock( const VuBasicBlock& block, std::list<Token>& scheduled )
	{
		std::vector<const Token*> segment;

		for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
		{
			if( isReadyScheduleCandidate( **i ) )
			{
				segment.push_back( *i );
				continue;
			}

			appendReadyScheduledSegment( segment, scheduled );
			segment.clear();
			scheduled.push_back( **i );
		}

		appendReadyScheduledSegment( segment, scheduled );
	}
}

VuBasicBlock::VuBasicBlock()
{
	firstTokenIndex = 0;
	terminatedByBarrier = false;
}

VuDependencyEdge::VuDependencyEdge()
{
	before = 0;
	after = 0;
	kind = VU_DEPENDENCY_REGISTER_RAW;
}

VuDependencyEdge::VuDependencyEdge( unsigned int beforeToken, unsigned int afterToken, VuDependencyKind dependencyKind )
{
	before = beforeToken;
	after = afterToken;
	kind = dependencyKind;
}

std::vector<VuBasicBlock> buildVuBasicBlocks( const std::list<Token>& tokens )
{
	std::vector<VuBasicBlock> blocks;
	VuBasicBlock current;
	bool hasCurrent = false;
	unsigned int index = 0;

	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
	{
		if( (*i).label().length() != 0 && hasCurrent )
		{
			blocks.push_back( current );
			current = VuBasicBlock();
			hasCurrent = false;
		}

		if( !hasCurrent )
		{
			current = VuBasicBlock();
			current.firstTokenIndex = index;
			hasCurrent = true;
		}

		current.tokens.push_back( &*i );

		if( isBlockBarrier( *i ) )
		{
			current.terminatedByBarrier = true;
			blocks.push_back( current );
			current = VuBasicBlock();
			hasCurrent = false;
		}
	}

	if( hasCurrent )
		blocks.push_back( current );

	return blocks;
}

std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block )
{
	std::vector<VuDependencyEdge> edges;
	std::vector<VuTokenResourceAccess> accesses;
	for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
	{
		VuTokenResourceAccess access;
		buildVuTokenResourceAccess( **i, access );
		accesses.push_back( access );
	}

	for( unsigned int before = 0; before < accesses.size(); ++before )
	{
		for( unsigned int after = before + 1; after < accesses.size(); ++after )
		{
			const VuTokenResourceAccess& a = accesses[before];
			const VuTokenResourceAccess& b = accesses[after];

			if( intersects( a.registerWrites, b.registerReads ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_RAW );
			if( intersects( a.registerReads, b.registerWrites ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_WAR );
			if( intersects( a.registerWrites, b.registerWrites ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_WAW );

			if( a.implicitWrites & b.implicitReads )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_RAW );
			if( a.implicitReads & b.implicitWrites )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAR );
			if( a.implicitWrites & b.implicitWrites )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAW );

			if( a.memoryKind != VU_MEMORY_NONE && b.memoryKind != VU_MEMORY_NONE
			    && (isMemoryOrderingAccess( a ) || isMemoryOrderingAccess( b )) )
				addEdge( edges, before, after, VU_DEPENDENCY_MEMORY );
		}
	}

	return edges;
}

std::list<Token> scheduleVuTokensPreservingOrder( const std::list<Token>& tokens )
{
	std::list<Token> scheduled;
	std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );

	for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
	{
		for( std::vector<const Token*>::const_iterator token = block->tokens.begin(); token != block->tokens.end(); ++token )
			scheduled.push_back( **token );
	}

	return scheduled;
}

std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens )
{
	std::list<Token> scheduled;
	std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );

	for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
		appendReadyScheduledBlock( *block, scheduled );

	return scheduled;
}

}
