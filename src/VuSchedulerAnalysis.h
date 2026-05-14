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

enum VuBasicBlockTerminatorKind
{
	VU_BASIC_BLOCK_TERMINATOR_NONE,
	VU_BASIC_BLOCK_TERMINATOR_BRANCH,
	VU_BASIC_BLOCK_TERMINATOR_XGKICK,
	VU_BASIC_BLOCK_TERMINATOR_BOUNDARY,
	VU_BASIC_BLOCK_TERMINATOR_PREORDERED
};

struct VuBasicBlock
{
	VuBasicBlock();

	unsigned int firstTokenIndex;
	std::vector<const Token*> tokens;
	bool terminatedByBarrier;
	VuBasicBlockTerminatorKind terminatorKind;
	const Token* terminator;
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

struct VuScheduledIssueSlot
{
	VuScheduledIssueSlot();

	const Token* firstToken;
	const Token* secondToken;
	const Token* upperToken;
	const Token* lowerToken;
};

struct VuLoopCandidate
{
	VuLoopCandidate();

	std::string label;
	unsigned int labelTokenIndex;
	unsigned int branchTokenIndex;
	unsigned int firstBodyTokenIndex;
	unsigned int lastBodyTokenIndex;
	bool hasLoopDirective;
	bool simpleCountedLoop;
	unsigned int memoryLoadCount;
	unsigned int memoryStoreCount;
	bool hasMemoryPreOrPostIncrement;
	bool hasXgkick;
	std::list<std::string> inductionRegisters;
	std::list<std::string> loopReadWriteRegisters;
	std::vector<const Token*> bodyTokens;
	const Token* branchToken;
};

struct VuLoopPipelineOpportunity
{
	VuLoopPipelineOpportunity();

	std::string label;
	unsigned int branchTokenIndex;
	unsigned int qProducerTokenIndex;
	unsigned int firstQConsumerTokenIndex;
	unsigned int qProducerLatency;
	unsigned int sourcePrefixCycles;
	unsigned int sourceSuffixCycles;
	unsigned int branchDelaySlots;
	bool simpleCountedLoop;
	bool hasSingleQProducer;
	bool requiresPrologEpilog;
	bool requiresLoopCarriedRegisters;
	bool eligibleSingleQSoftwarePipeline;
	std::vector<unsigned int> qConsumerTokenIndices;
	std::list<std::string> carriedQInputRegisters;
	std::list<std::string> carriedQOutputRegisters;
	unsigned int memoryLoadCount;
	unsigned int memoryStoreCount;
	bool hasMemoryPreOrPostIncrement;
	bool hasXgkick;
	std::list<std::string> inductionRegisters;
	std::list<std::string> loopReadWriteRegisters;
};

std::vector<VuBasicBlock> buildVuBasicBlocks( const std::list<Token>& tokens );
std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block,
                                                      unsigned int ignoredImplicitWawResources = 0 );
std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlots( const VuBasicBlock& block,
                                                                       unsigned int ignoredImplicitWawResources = 0 );
std::vector<VuLoopCandidate> findVuLoopCandidates( const std::list<Token>& tokens );
std::vector<VuLoopPipelineOpportunity> findVuLoopPipelineOpportunities( const std::list<Token>& tokens );
std::list<Token> scheduleVuTokensPreservingOrder( const std::list<Token>& tokens );
std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens,
                                           unsigned int ignoredImplicitWawResources = 0 );
std::list<Token> scheduleVuTokensReadySetWithFlagLiveness( const std::list<Token>& tokens );

}

#endif
