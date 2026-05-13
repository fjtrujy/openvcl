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

}
