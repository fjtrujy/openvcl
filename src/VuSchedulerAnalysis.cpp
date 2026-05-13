#include "VuSchedulerAnalysis.h"

#include "VuSchedulingRules.h"
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

	void addEdge( std::vector<VuDependencyEdge>& edges, unsigned int before, unsigned int after, VuDependencyKind kind )
	{
		edges.push_back( VuDependencyEdge( before, after, kind ) );
	}

	bool memoryOrderRequiresDependency( const VuTokenResourceAccess& beforeAccess,
	                                    const VuTokenResourceAccess& afterAccess,
	                                    const Token& before,
	                                    const Token& after )
	{
		if( beforeAccess.memoryKind == VU_MEMORY_NONE || afterAccess.memoryKind == VU_MEMORY_NONE )
			return false;
		if( !isVuMemoryOrderingAccess( before ) && !isVuMemoryOrderingAccess( after ) )
			return false;
		return !vuTokenCanMoveBefore( after, before );
	}

	int readyCandidateScore( unsigned int candidate,
	                         bool haveLastPipe,
	                         bool lastWasLower,
	                         const VuBasicBlock& block,
	                         const std::vector<unsigned int>& priority )
	{
		int score = static_cast<int>( candidate );

		if( isVuLongLatencyProducer( *block.tokens[candidate] ) )
			score -= 500;
		else if( isVuLatencyLoad( *block.tokens[candidate] ) )
			score -= 300;

		if( haveLastPipe && isVuLowerPipe( *block.tokens[candidate] ) != lastWasLower )
			score -= 100;

		if( candidate < priority.size() )
			score -= static_cast<int>( priority[candidate] * 20 );

		return score;
	}

	std::vector<unsigned int> buildDependencyPriorities( const VuBasicBlock& block,
	                                                    const std::vector< std::vector<unsigned int> >& outgoing )
	{
		std::vector<unsigned int> priority( block.tokens.size(), 1 );

		for( unsigned int reverse = block.tokens.size(); reverse > 0; --reverse )
		{
			const unsigned int i = reverse - 1;
			unsigned int cost = 1;
			if( block.tokens[i]->operand() && block.tokens[i]->operand()->latency() > cost )
				cost = block.tokens[i]->operand()->latency();
			unsigned int best = cost;
			for( std::vector<unsigned int>::const_iterator edge = outgoing[i].begin(); edge != outgoing[i].end(); ++edge )
			{
				if( *edge < priority.size() && priority[*edge] + cost > best )
					best = priority[*edge] + cost;
			}
			priority[i] = best;
		}

		return priority;
	}

	void appendReadyScheduledSegment( const std::vector<const Token*>& segment,
	                                  std::list<Token>& scheduled,
	                                  unsigned int ignoredImplicitWawResources )
	{
		if( segment.size() < 2 )
		{
			for( std::vector<const Token*>::const_iterator i = segment.begin(); i != segment.end(); ++i )
				scheduled.push_back( **i );
			return;
		}

		VuBasicBlock block;
		block.tokens = segment;

		std::vector<VuDependencyEdge> edges = buildVuDependencyGraph( block, ignoredImplicitWawResources );
		std::vector<unsigned int> incoming( block.tokens.size(), 0 );
		std::vector< std::vector<unsigned int> > outgoing( block.tokens.size() );

		for( std::vector<VuDependencyEdge>::const_iterator i = edges.begin(); i != edges.end(); ++i )
		{
			if( i->before >= block.tokens.size() || i->after >= block.tokens.size() )
				continue;
			++incoming[i->after];
			outgoing[i->before].push_back( i->after );
		}

		const std::vector<unsigned int> priority = buildDependencyPriorities( block, outgoing );
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

				const int score = readyCandidateScore( i, haveLastPipe, lastWasLower, block, priority );
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
			lastWasLower = isVuLowerPipe( *block.tokens[best] );

			for( std::vector<unsigned int>::const_iterator i = outgoing[best].begin(); i != outgoing[best].end(); ++i )
			{
				if( incoming[*i] > 0 )
					--incoming[*i];
			}
		}
	}

	void appendReadyScheduledBlock( const VuBasicBlock& block,
	                                std::list<Token>& scheduled,
	                                unsigned int ignoredImplicitWawResources )
	{
		std::vector<const Token*> segment;

		for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
		{
			if( isVuReadyScheduleCandidate( **i ) )
			{
				segment.push_back( *i );
				continue;
			}

			appendReadyScheduledSegment( segment, scheduled, ignoredImplicitWawResources );
			segment.clear();
			scheduled.push_back( **i );
		}

		appendReadyScheduledSegment( segment, scheduled, ignoredImplicitWawResources );
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

		if( isVuSchedulingBarrier( *i ) )
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

std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block,
                                                      unsigned int ignoredImplicitWawResources )
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
			if( (a.implicitWrites & b.implicitWrites & ~ignoredImplicitWawResources) != 0 )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAW );

			if( memoryOrderRequiresDependency( a,
			                                  b,
			                                  *block.tokens[before],
			                                  *block.tokens[after] ) )
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

std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens,
                                           unsigned int ignoredImplicitWawResources )
{
	std::list<Token> scheduled;
	std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );

	for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
		appendReadyScheduledBlock( *block, scheduled, ignoredImplicitWawResources );

	return scheduled;
}

}
