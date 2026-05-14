#ifndef __OPENVCL_VUSCHEDULERANALYSIS_H__
#define __OPENVCL_VUSCHEDULERANALYSIS_H__

/*
 * VuSchedulerAnalysis.h
 *
 * Basic-block and dependency scaffolding for the future VU scheduler.
 */

#include "Token.h"
#include "VuInstructionInfo.h"

#include <list>
#include <vector>

namespace vcl
{

class VuLatencyTracker;

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

struct VuLoopInductionUpdate
{
	std::string registerName;
	std::string mnemonic;
	std::string immediate;
	long step;
	bool stepKnown;
	unsigned int tokenIndex;
};

enum VuLoopQSchedulingStrategy
{
	VU_LOOP_Q_SCHEDULE_LOCAL,
	VU_LOOP_Q_SCHEDULE_LOOP_CARRIED,
	VU_LOOP_Q_SCHEDULE_INSUFFICIENT
};

struct VuLoopQStage
{
	VuLoopQStage();

	unsigned int qProducerTokenIndex;
	std::vector<unsigned int> qConsumerTokenIndices;
	unsigned int qProducerLatency;
	unsigned int qProducerConsumerGapCycles;
	unsigned int qProducerConsumerGapDeficitCycles;
	unsigned int loopCarriedQGapCycles;
	unsigned int qProducerInsertionGapCycles;
	unsigned int qProducerInsertionGapDeficitCycles;
	VuLoopQSchedulingStrategy qSchedulingStrategy;
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

enum VuScheduledPaddingKind
{
	VU_SCHEDULED_PADDING_NONE,
	VU_SCHEDULED_PADDING_NOP,
	VU_SCHEDULED_PADDING_WAITQ,
	VU_SCHEDULED_PADDING_WAITP
};

struct VuScheduledIssueSlot
{
	VuScheduledIssueSlot();

	const Token* firstToken;
	const Token* secondToken;
	const Token* upperToken;
	const Token* lowerToken;
	bool padding;
	VuScheduledPaddingKind paddingKind;
	unsigned int ignoredImplicitWawResources;
	unsigned int issueCycle;
	unsigned int cycleCount;
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
	std::vector<VuLoopInductionUpdate> inductionUpdates;
	std::list<std::string> loopReadWriteRegisters;
	std::vector<const Token*> bodyTokens;
	const Token* branchToken;
};

struct VuSoftwarePipelineRotation
{
	std::string registerBase;
	std::list<std::string> inputFields;
	std::list<std::string> outputFields;
	bool hasScratchRegister;
	std::string scratchRegister;
};

struct VuSoftwarePipelinePrefetch
{
	unsigned int tokenIndex;
	std::string mnemonic;
	VuMemoryKind memoryKind;
	unsigned int memoryFlags;
	bool hasMemoryBase;
	std::string memoryBaseRegister;
	bool hasMemoryOffset;
	long memoryOffset;
	bool readsInductionRegister;
	std::string inductionRegister;
	bool hasNextIterationOffset;
	long nextIterationOffset;
};

struct VuLoopPipelineOpportunity
{
	VuLoopPipelineOpportunity();

	std::string label;
	unsigned int labelTokenIndex;
	unsigned int branchTokenIndex;
	unsigned int qProducerTokenIndex;
	std::vector<unsigned int> qProducerTokenIndices;
	std::vector<VuLoopQStage> qStages;
	unsigned int firstQConsumerTokenIndex;
	unsigned int lastQConsumerTokenIndex;
	unsigned int qProducerLatency;
	unsigned int qProducerConsumerGapCycles;
	unsigned int qProducerConsumerGapDeficitCycles;
	unsigned int loopCarriedQGapCycles;
	unsigned int qProducerInsertionGapCycles;
	unsigned int qProducerInsertionGapDeficitCycles;
	VuLoopQSchedulingStrategy qSchedulingStrategy;
	unsigned int sourcePrefixCycles;
	unsigned int sourceSuffixCycles;
	unsigned int branchDelaySlots;
	bool simpleCountedLoop;
	bool hasSingleQProducer;
	bool requiresPrologEpilog;
	bool requiresLoopCarriedRegisters;
	bool qLiveOut;
	bool eligibleSingleQSoftwarePipeline;
	bool hasSoftwarePipelinePlan;
	bool canEmitSoftwarePipeline;
	std::vector<unsigned int> qConsumerTokenIndices;
	std::vector<unsigned int> prologTokenIndices;
	std::vector<unsigned int> mainTokenIndices;
	std::vector<unsigned int> drainTokenIndices;
	std::list<std::string> softwarePipelineBlockers;
	std::list<std::string> softwarePipelineRotatedRegisters;
	std::vector<VuSoftwarePipelineRotation> softwarePipelineRotations;
	std::vector<VuSoftwarePipelinePrefetch> softwarePipelinePrefetches;
	std::list<std::string> carriedQInputRegisters;
	std::list<std::string> carriedQOutputRegisters;
	unsigned int memoryLoadCount;
	unsigned int memoryStoreCount;
	bool hasMemoryPreOrPostIncrement;
	bool hasXgkick;
	std::list<std::string> inductionRegisters;
	std::vector<VuLoopInductionUpdate> inductionUpdates;
	std::list<std::string> loopReadWriteRegisters;
};

struct VuSoftwarePipelineRewritePlan
{
	VuSoftwarePipelineRewritePlan();

	std::string label;
	std::string prologLabel;
	std::string mainLabel;
	std::string drainLabel;
	unsigned int labelTokenIndex;
	unsigned int branchTokenIndex;
	unsigned int qProducerTokenIndex;
	unsigned int prefetchInsertAfterTokenIndex;
	unsigned int qProducerInsertAfterTokenIndex;
	bool qProducerInBranchDelaySlot;
	bool qProducerBranchDelayBlockedBySuffixFiller;
	unsigned int qProducerBranchDelaySuffixFillerTokenIndex;
	bool emitsDrain;
	std::vector<unsigned int> prefetchTokenIndices;
	std::vector<VuSoftwarePipelinePrefetch> prefetches;
	std::vector<VuSoftwarePipelineRotation> rotations;
	std::vector<unsigned int> prologTokenIndices;
	std::vector<unsigned int> mainTokenIndices;
	std::vector<unsigned int> drainTokenIndices;
	std::vector<unsigned int> qConsumerTokenIndices;
};

std::vector<VuBasicBlock> buildVuBasicBlocks( const std::list<Token>& tokens );
std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block,
                                                      unsigned int ignoredImplicitWawResources = 0 );
std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlots( const VuBasicBlock& block,
                                                                       unsigned int ignoredImplicitWawResources = 0 );
std::vector< std::vector<VuScheduledIssueSlot> > scheduleVuBasicBlocksReadyIssueSlotsWithFlagLiveness(
    const std::list<Token>& tokens );
unsigned int vuIgnoredFlagWawResourcesForRemaining( std::list<Token>::const_iterator begin,
                                                    std::list<Token>::const_iterator end );
VuScheduledPaddingKind vuScheduledPaddingKindForReadHazard( const Token& token,
                                                            const Token* partner,
                                                            const VuLatencyTracker& latencyTracker,
                                                            int currentCycle );
std::vector<VuLoopCandidate> findVuLoopCandidates( const std::list<Token>& tokens );
std::vector<VuLoopPipelineOpportunity> findVuLoopPipelineOpportunities( const std::list<Token>& tokens );
std::vector<VuSoftwarePipelineRewritePlan> buildVuSoftwarePipelineRewritePlans( const std::list<Token>& tokens );
std::list<Token> applyVuSoftwarePipelinePlans( const std::list<Token>& tokens );
std::list<Token> scheduleVuTokensPreservingOrder( const std::list<Token>& tokens );
std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens,
                                           unsigned int ignoredImplicitWawResources = 0 );
std::list<Token> scheduleVuTokensReadySetWithFlagLiveness( const std::list<Token>& tokens );

}

#endif
