#ifndef __OPENVCL_VUSCHEDULERANALYSIS_H__
#define __OPENVCL_VUSCHEDULERANALYSIS_H__

/*
 * VuSchedulerAnalysis.h
 *
 * Basic-block and dependency scaffolding for the future VU scheduler.
 */

#include "Token.h"

#include <list>
#include <vector>

namespace vcl
{

struct VuBasicBlock
{
	VuBasicBlock();

	unsigned int firstTokenIndex;
	std::vector<const Token*> tokens;
	bool terminatedByBarrier;
};

enum VuDependencyKind
{
	VU_DEPENDENCY_REGISTER_RAW,
	VU_DEPENDENCY_REGISTER_WAR,
	VU_DEPENDENCY_REGISTER_WAW,
	VU_DEPENDENCY_RESOURCE_RAW,
	VU_DEPENDENCY_RESOURCE_WAR,
	VU_DEPENDENCY_RESOURCE_WAW,
	VU_DEPENDENCY_MEMORY
};

struct VuDependencyEdge
{
	VuDependencyEdge();
	VuDependencyEdge( unsigned int beforeToken, unsigned int afterToken, VuDependencyKind dependencyKind );

	unsigned int before;
	unsigned int after;
	VuDependencyKind kind;
};

std::vector<VuBasicBlock> buildVuBasicBlocks( const std::list<Token>& tokens );
std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block,
                                                      unsigned int ignoredImplicitWawResources = 0 );
std::list<Token> scheduleVuTokensPreservingOrder( const std::list<Token>& tokens );
std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens,
                                           unsigned int ignoredImplicitWawResources = 0 );

}

#endif
