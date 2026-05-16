#include "VuSchedulerAnalysis.h"

#include "VuLatencyTracker.h"
#include "VuModuloReservationTable.h"
#include "VuKernelLayout.h"
#include "VuSchedulingRules.h"
#include "VuTokenResourceAccess.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace vcl
{

extern const unsigned int VU_SCHEDULED_TOKEN_INDEX_NONE = ~0u;

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

	void addImplicitFlagWriterBarrierEdges( std::vector<VuDependencyEdge>& edges,
	                                        const std::vector<unsigned int>& writers,
	                                        bool preserveFinalWriter )
	{
		if( !preserveFinalWriter || writers.size() < 2 )
			return;

		const unsigned int finalWriter = writers.back();
		for( std::vector<unsigned int>::const_iterator writer = writers.begin();
		     writer != writers.end(); ++writer )
		{
			if( *writer != finalWriter )
				addEdge( edges, *writer, finalWriter, VU_DEPENDENCY_RESOURCE_WAW );
		}
	}

	void addPreciseImplicitFlagDependencies( std::vector<VuDependencyEdge>& edges,
	                                         const std::vector<VuTokenResourceAccess>& accesses,
	                                         unsigned int resource,
	                                         unsigned int ignoredImplicitWawResources )
	{
		std::vector<unsigned int> pendingWriters;
		const bool preserveLiveOutWriter = (ignoredImplicitWawResources & resource) == 0;

		for( unsigned int index = 0; index < accesses.size(); ++index )
		{
			const bool readsResource = (accesses[index].implicitReads & resource) != 0;
			const bool writesResource = (accesses[index].implicitWrites & resource) != 0;

			if( readsResource )
			{
				if( !pendingWriters.empty() )
				{
					addImplicitFlagWriterBarrierEdges( edges, pendingWriters, true );
					addEdge( edges, pendingWriters.back(), index, VU_DEPENDENCY_RESOURCE_RAW );
				}
				pendingWriters.clear();
			}

			if( writesResource )
				pendingWriters.push_back( index );
		}

		addImplicitFlagWriterBarrierEdges( edges, pendingWriters, preserveLiveOutWriter );
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

	VuBasicBlockTerminatorKind vuBlockTerminatorKind( const Token& token )
	{
		if( isVuBoundaryOperand( token ) )
			return VU_BASIC_BLOCK_TERMINATOR_BOUNDARY;
		if( isVuXgkick( token ) )
			return VU_BASIC_BLOCK_TERMINATOR_XGKICK;
		if( vuTokenBranchDelaySlots( token ) > 0 )
			return VU_BASIC_BLOCK_TERMINATOR_BRANCH;
		if( token.flags() & Token::PREORDERED )
			return VU_BASIC_BLOCK_TERMINATOR_PREORDERED;
		return VU_BASIC_BLOCK_TERMINATOR_NONE;
	}

	int readyCandidateScore( unsigned int candidate,
	                         bool haveLastPipe,
	                         bool lastWasLower,
	                         const VuBasicBlock& block,
	                         const std::vector<unsigned int>& priority,
	                         const VuLatencyTracker& latencyTracker,
	                         unsigned int currentCycle )
	{
		int score = static_cast<int>( candidate );
		const int delay =
		    latencyTracker.readHazardDelay( *block.tokens[candidate], NULL, static_cast<int>( currentCycle ) );
		score += delay * 1000;

		if( isVuLongLatencyProducer( *block.tokens[candidate] ) )
			score -= 500;
		else if( isVuLatencyLoad( *block.tokens[candidate] ) )
			score -= 300;

		if( haveLastPipe && isVuLowerPipe( *block.tokens[candidate] ) != lastWasLower )
			score -= 100;

		if( delay == 0 && candidate < priority.size() )
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

	bool tokenWritesMacForPair( const Token& token )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		return (access.implicitWrites & VU_RESOURCE_MAC) != 0;
	}

	bool tokenSchedulesAsUpper( const Token& token, unsigned int ignoredImplicitWawResources )
	{
		if( (ignoredImplicitWawResources & VU_RESOURCE_MAC) && isVuMoveAsUpperMaxCandidate( token ) )
			return true;
		return !isVuLowerPipe( token );
	}

	bool tokenSchedulesAsLower( const Token& token, unsigned int ignoredImplicitWawResources )
	{
		return !tokenSchedulesAsUpper( token, ignoredImplicitWawResources ) && isVuLowerPipe( token );
	}

	bool tokenWritesMacForPair( const Token& token, unsigned int ignoredImplicitWawResources )
	{
		if( tokenSchedulesAsUpper( token, ignoredImplicitWawResources ) && isVuMoveAsUpperMaxCandidate( token ) )
			return true;
		return tokenWritesMacForPair( token );
	}

	unsigned int chooseReadyPairPartner( unsigned int primary,
	                                     const VuBasicBlock& block,
	                                     const std::vector<unsigned int>& incoming,
	                                     const std::vector<bool>& emitted,
	                                     const std::vector<unsigned int>& priority,
	                                     const VuLatencyTracker& latencyTracker,
	                                     unsigned int ignoredImplicitWawResources,
	                                     unsigned int currentCycle )
	{
		unsigned int best = static_cast<unsigned int>( block.tokens.size() );
		int bestScore = 0;
		const bool primaryIsLower = tokenSchedulesAsLower( *block.tokens[primary],
		                                                   ignoredImplicitWawResources );
		const bool primaryWritesMac = tokenWritesMacForPair( *block.tokens[primary],
		                                                     ignoredImplicitWawResources );
		const int primaryDelay =
		    latencyTracker.readHazardDelay( *block.tokens[primary], NULL, static_cast<int>( currentCycle ) );

		for( unsigned int i = 0; i < block.tokens.size(); ++i )
		{
			if( i == primary || emitted[i] || incoming[i] != 0 )
				continue;
			if( tokenSchedulesAsLower( *block.tokens[i],
			                           ignoredImplicitWawResources ) == primaryIsLower )
				continue;
			if( !vuTokenPairResourcesAreIndependent( *block.tokens[primary],
			                                         *block.tokens[i],
			                                         primaryWritesMac,
			                                         tokenWritesMacForPair( *block.tokens[i],
			                                                               ignoredImplicitWawResources ) ) )
				continue;
			if( latencyTracker.readHazardDelay( *block.tokens[primary],
			                                    block.tokens[i],
			                                    static_cast<int>( currentCycle ) ) > primaryDelay )
				continue;

			const int score = readyCandidateScore( i,
			                                       false,
			                                       false,
			                                       block,
			                                       priority,
			                                       latencyTracker,
			                                       currentCycle );
			if( best == block.tokens.size() || score < bestScore )
			{
				best = i;
				bestScore = score;
			}
		}

		return best;
	}

	void markReadyTokenScheduled( unsigned int token,
	                              std::vector<unsigned int>& incoming,
	                              const std::vector< std::vector<unsigned int> >& outgoing,
	                              std::vector<bool>& emitted,
	                              unsigned int& emittedCount )
	{
		emitted[token] = true;
		++emittedCount;

		for( std::vector<unsigned int>::const_iterator i = outgoing[token].begin(); i != outgoing[token].end(); ++i )
		{
			if( incoming[*i] > 0 )
				--incoming[*i];
		}
	}

	VuScheduledIssueSlot makeIssueSlot( const Token* first,
	                                    const Token* second,
	                                    unsigned int issueCycle = 0,
	                                    unsigned int cycleCount = 1,
	                                    unsigned int ignoredImplicitWawResources = VU_RESOURCE_NONE )
	{
		VuScheduledIssueSlot slot;
		slot.firstToken = first;
		slot.secondToken = second;
		slot.padding = first == NULL && second == NULL;
		slot.paddingKind = slot.padding ? VU_SCHEDULED_PADDING_NOP : VU_SCHEDULED_PADDING_NONE;
		slot.issueCycle = issueCycle;
		slot.cycleCount = cycleCount;

		if( first )
		{
			if( tokenSchedulesAsLower( *first, ignoredImplicitWawResources ) )
				slot.lowerToken = first;
			else
				slot.upperToken = first;
		}

		if( second )
		{
			if( tokenSchedulesAsLower( *second, ignoredImplicitWawResources ) )
				slot.lowerToken = second;
			else
				slot.upperToken = second;
		}

		return slot;
	}

	VuScheduledIssueSlot makePaddingIssueSlot( VuScheduledPaddingKind paddingKind,
	                                           unsigned int issueCycle,
	                                           unsigned int cycleCount )
	{
		VuScheduledIssueSlot slot = makeIssueSlot( NULL, NULL, issueCycle, cycleCount );
		slot.paddingKind = paddingKind;
		return slot;
	}

	VuScheduledIssueSlot makeUpperWaitIssueSlot( const Token* upper,
	                                             VuScheduledPaddingKind paddingKind,
	                                             unsigned int issueCycle,
	                                             unsigned int cycleCount,
	                                             unsigned int ignoredImplicitWawResources )
	{
		VuScheduledIssueSlot slot =
			makeIssueSlot( upper, NULL, issueCycle, cycleCount, ignoredImplicitWawResources );
		slot.paddingKind = paddingKind;
		return slot;
	}

	unsigned int waitPaddingCycleCount( VuScheduledPaddingKind paddingKind,
	                                    const VuLatencyTracker& latencyTracker,
	                                    unsigned int currentCycle )
	{
		if( paddingKind == VU_SCHEDULED_PADDING_WAITQ )
		{
			const int readyCycle = latencyTracker.qReadyCycle();
			const unsigned int nextCycle = static_cast<unsigned int>(
				readyCycle > static_cast<int>( currentCycle + 1 )
				? readyCycle
				: static_cast<int>( currentCycle + 1 ) );
			return nextCycle - currentCycle;
		}
		if( paddingKind == VU_SCHEDULED_PADDING_WAITP )
		{
			const int readyCycle = latencyTracker.pReadyCycle();
			const unsigned int nextCycle = static_cast<unsigned int>(
				readyCycle > static_cast<int>( currentCycle + 1 )
				? readyCycle
				: static_cast<int>( currentCycle + 1 ) );
			return nextCycle - currentCycle;
		}
		return 1;
	}

	void appendReadHazardPaddingSlots( std::vector<VuScheduledIssueSlot>& slots,
	                                   const Token& token,
	                                   const Token* partner,
	                                   VuLatencyTracker& latencyTracker,
	                                   unsigned int blockStartCycle,
	                                   unsigned int& currentCycle )
	{
		int issueDelay = latencyTracker.readHazardDelay( token,
		                                                 partner,
		                                                 static_cast<int>( currentCycle ) );
		while( issueDelay > 0 )
		{
			const VuScheduledPaddingKind paddingKind =
				vuScheduledPaddingKindForReadHazard( token,
				                                     partner,
				                                     latencyTracker,
				                                     static_cast<int>( currentCycle ) );
			const unsigned int paddingCycleCount =
				waitPaddingCycleCount( paddingKind, latencyTracker, currentCycle );
			slots.push_back( makePaddingIssueSlot( paddingKind,
			                                       currentCycle - blockStartCycle,
			                                       paddingCycleCount ) );
			currentCycle += paddingCycleCount;
			issueDelay = latencyTracker.readHazardDelay( token,
			                                             partner,
			                                             static_cast<int>( currentCycle ) );
		}
	}

	void assignIgnoredImplicitWawResources( std::vector<VuScheduledIssueSlot>& slots,
	                                        unsigned int ignoredImplicitWawResources )
	{
		for( std::vector<VuScheduledIssueSlot>::iterator i = slots.begin(); i != slots.end(); ++i )
			i->ignoredImplicitWawResources = ignoredImplicitWawResources;
	}

	unsigned int tokenIndexInScheduledBlock( const VuBasicBlock& block, const Token* token )
	{
		if( !token )
			return VU_SCHEDULED_TOKEN_INDEX_NONE;

		for( unsigned int i = 0; i < block.tokens.size(); ++i )
		{
			if( block.tokens[i] == token )
				return block.firstTokenIndex + i;
		}

		return VU_SCHEDULED_TOKEN_INDEX_NONE;
	}

	void assignScheduledIssueSlotTokenIndices( const VuBasicBlock& block,
	                                           std::vector<VuScheduledIssueSlot>& slots )
	{
		for( std::vector<VuScheduledIssueSlot>::iterator slot = slots.begin(); slot != slots.end(); ++slot )
		{
			slot->firstTokenIndex = tokenIndexInScheduledBlock( block, slot->firstToken );
			slot->secondTokenIndex = tokenIndexInScheduledBlock( block, slot->secondToken );
			slot->upperTokenIndex = tokenIndexInScheduledBlock( block, slot->upperToken );
			slot->lowerTokenIndex = tokenIndexInScheduledBlock( block, slot->lowerToken );
		}
	}

	bool tokenCanEnterBarrierTailPair( const Token& token )
	{
		if( !token.operand() )
			return false;
		if( token.flags() & Token::IGNORED )
			return false;
		if( token.flags() & Token::BRANCH_DELAY_FILLER )
			return false;
		if( token.flags() & Token::PREORDERED )
			return false;
		if( token.operand()->flags() & Operand::PREPROCESSOR )
			return false;
		if( token.operand()->flags() & Operand::FILTERED )
			return false;
		if( token.operand()->unit() == Operand::ENTER || token.operand()->unit() == Operand::EXIT )
			return false;
		if( !token.operand()->isUpperExecutionPath() && !token.operand()->isLowerExecutionPath() )
			return false;
		return true;
	}

	bool tokenCanPairWithBarrierTail( const Token& previous,
	                                  const Token& barrier,
	                                  unsigned int ignoredImplicitWawResources )
	{
		if( !tokenCanEnterBarrierTailPair( previous ) || !tokenCanEnterBarrierTailPair( barrier ) )
			return false;
		if( barrier.label().length() != 0 )
			return false;
		if( !isVuXgkick( barrier ) && vuTokenBranchDelaySlots( barrier ) == 0 )
			return false;
		if( tokenSchedulesAsLower( previous, ignoredImplicitWawResources )
		    == tokenSchedulesAsLower( barrier, ignoredImplicitWawResources ) )
			return false;

		VuTokenResourceAccess previousAccess;
		VuTokenResourceAccess barrierAccess;
		if( !buildVuTokenResourceAccess( previous, previousAccess )
		    || !buildVuTokenResourceAccess( barrier, barrierAccess ) )
			return false;
		if( previousAccess.branchDelaySlots > 0 )
			return false;
		if( barrierAccess.branchDelaySlots > 0
		    && (barrierAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH)) )
			return false;
		if( (previousAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P))
		    || (barrierAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P)) )
			return false;

		const unsigned int previousReadsImplicit = previousAccess.implicitReads;
		const unsigned int previousWritesImplicit = previousAccess.implicitWrites;
		const unsigned int barrierReadsImplicit = barrierAccess.implicitReads;
		const unsigned int barrierWritesImplicit = barrierAccess.implicitWrites;
		if( previousWritesImplicit & (barrierReadsImplicit | barrierWritesImplicit) )
			return false;
		if( barrierWritesImplicit & (previousReadsImplicit | previousWritesImplicit) )
			return false;

		if( vuTokensHaveDataDependency( previous, barrier )
		    || vuTokensHaveDataDependency( barrier, previous ) )
			return false;

		return true;
	}

	bool tokenCanPairWithFlagReaderTail( const Token& previous,
	                                     const Token& tail,
	                                     unsigned int ignoredImplicitWawResources )
	{
		if( !tokenCanEnterBarrierTailPair( previous ) || !tokenCanEnterBarrierTailPair( tail ) )
			return false;
		if( tail.label().length() != 0 )
			return false;
		if( tokenSchedulesAsLower( previous, ignoredImplicitWawResources )
		    == tokenSchedulesAsLower( tail, ignoredImplicitWawResources ) )
			return false;

		VuTokenResourceAccess previousAccess;
		VuTokenResourceAccess tailAccess;
		if( !buildVuTokenResourceAccess( previous, previousAccess )
		    || !buildVuTokenResourceAccess( tail, tailAccess ) )
			return false;
		if( (tailAccess.implicitReads & (VU_RESOURCE_MAC | VU_RESOURCE_CLIP)) == 0 )
			return false;
		if( previousAccess.memoryKind != VU_MEMORY_NONE || tailAccess.memoryKind != VU_MEMORY_NONE )
			return false;
		if( previousAccess.branchDelaySlots > 0 || tailAccess.branchDelaySlots > 0 )
			return false;
		if( (previousAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P))
		    || (tailAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P)) )
			return false;

		const unsigned int previousReadsImplicit = previousAccess.implicitReads;
		const unsigned int previousWritesImplicit = previousAccess.implicitWrites;
		const unsigned int tailReadsImplicit = tailAccess.implicitReads;
		const unsigned int tailWritesImplicit = tailAccess.implicitWrites;
		if( previousWritesImplicit & (tailReadsImplicit | tailWritesImplicit) )
			return false;
		if( tailWritesImplicit & (previousReadsImplicit | previousWritesImplicit) )
			return false;

		if( vuTokensHaveDataDependency( previous, tail )
		    || vuTokensHaveDataDependency( tail, previous ) )
			return false;

		return true;
	}

	bool tokenCanPairWithPreviousTail( const Token& previous,
	                                   const Token& tail,
	                                   unsigned int ignoredImplicitWawResources )
	{
		return tokenCanPairWithBarrierTail( previous, tail, ignoredImplicitWawResources )
		    || tokenCanPairWithFlagReaderTail( previous, tail, ignoredImplicitWawResources );
	}

	bool tryPairTailWithPreviousSlot( std::vector<VuScheduledIssueSlot>& slots,
	                                  const Token& tail,
	                                  unsigned int ignoredImplicitWawResources )
	{
		if( slots.empty() )
			return false;

		VuScheduledIssueSlot& previousSlot = slots.back();
		if( previousSlot.padding || previousSlot.cycleCount != 1 )
			return false;
		if( !previousSlot.firstToken || previousSlot.secondToken )
			return false;
		if( !tokenCanPairWithPreviousTail( *previousSlot.firstToken,
		                                   tail,
		                                   ignoredImplicitWawResources ) )
			return false;

		previousSlot.secondToken = &tail;
		if( tokenSchedulesAsLower( tail, ignoredImplicitWawResources ) )
			previousSlot.lowerToken = &tail;
		else
			previousSlot.upperToken = &tail;
		return true;
	}

	bool selectUpperWaitFiller( const VuBasicBlock& block,
	                            const std::vector<unsigned int>& incoming,
	                            const std::vector<bool>& emitted,
	                            const std::vector<unsigned int>& priority,
	                            const VuLatencyTracker& latencyTracker,
	                            unsigned int ignoredImplicitWawResources,
	                            unsigned int currentCycle,
	                            unsigned int& waitToken,
	                            unsigned int& upperToken,
	                            VuScheduledPaddingKind& paddingKind )
	{
		waitToken = static_cast<unsigned int>( block.tokens.size() );
		upperToken = static_cast<unsigned int>( block.tokens.size() );
		paddingKind = VU_SCHEDULED_PADDING_NONE;
		int bestScore = 0;

		for( unsigned int wait = 0; wait < block.tokens.size(); ++wait )
		{
			if( emitted[wait] || incoming[wait] != 0 )
				continue;
			if( !tokenSchedulesAsLower( *block.tokens[wait], ignoredImplicitWawResources ) )
				continue;
			if( latencyTracker.readHazardDelay( *block.tokens[wait],
			                                    NULL,
			                                    static_cast<int>( currentCycle ) ) <= 0 )
				continue;

			const VuScheduledPaddingKind waitKind =
				vuScheduledPaddingKindForReadHazard( *block.tokens[wait],
				                                     NULL,
				                                     latencyTracker,
				                                     static_cast<int>( currentCycle ) );
			if( waitKind != VU_SCHEDULED_PADDING_WAITQ
			    && waitKind != VU_SCHEDULED_PADDING_WAITP )
				continue;

			for( unsigned int upper = 0; upper < block.tokens.size(); ++upper )
			{
				if( upper == wait || emitted[upper] || incoming[upper] != 0 )
					continue;
				if( !tokenSchedulesAsUpper( *block.tokens[upper], ignoredImplicitWawResources ) )
					continue;
				if( latencyTracker.readHazardDelay( *block.tokens[upper],
				                                    NULL,
				                                    static_cast<int>( currentCycle ) ) > 0 )
					continue;

				const int score = readyCandidateScore( upper,
				                                       false,
				                                       false,
				                                       block,
				                                       priority,
				                                       latencyTracker,
				                                       currentCycle );
				if( upperToken == block.tokens.size() || score < bestScore )
				{
					waitToken = wait;
					upperToken = upper;
					paddingKind = waitKind;
					bestScore = score;
				}
			}
		}

		return upperToken < block.tokens.size();
	}

	std::vector<VuScheduledIssueSlot> scheduleReadySegmentIssueSlots( const std::vector<const Token*>& segment,
	                                                                  unsigned int ignoredImplicitWawResources,
	                                                                  VuLatencyTracker& latencyTracker,
	                                                                  unsigned int blockStartCycle,
	                                                                  unsigned int& currentCycle )
	{
		std::vector<VuScheduledIssueSlot> slots;
		if( segment.size() < 2 )
		{
			for( std::vector<const Token*>::const_iterator i = segment.begin(); i != segment.end(); ++i )
			{
				appendReadHazardPaddingSlots( slots,
				                              **i,
				                              NULL,
				                              latencyTracker,
				                              blockStartCycle,
				                              currentCycle );
				slots.push_back( makeIssueSlot( *i,
				                                NULL,
				                                currentCycle - blockStartCycle,
				                                1,
				                                ignoredImplicitWawResources ) );
				latencyTracker.recordWrites( **i, static_cast<int>( currentCycle ) );
				++currentCycle;
			}
			assignIgnoredImplicitWawResources( slots, ignoredImplicitWawResources );
			return slots;
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
			unsigned int waitToken = static_cast<unsigned int>( block.tokens.size() );
			unsigned int waitUpper = static_cast<unsigned int>( block.tokens.size() );
			VuScheduledPaddingKind waitPaddingKind = VU_SCHEDULED_PADDING_NONE;
			if( selectUpperWaitFiller( block,
			                           incoming,
			                           emitted,
			                           priority,
			                           latencyTracker,
			                           ignoredImplicitWawResources,
			                           currentCycle,
			                           waitToken,
			                           waitUpper,
			                           waitPaddingKind ) )
			{
				(void)waitToken;
				const unsigned int waitCycles =
					waitPaddingCycleCount( waitPaddingKind, latencyTracker, currentCycle );
				slots.push_back( makeUpperWaitIssueSlot( block.tokens[waitUpper],
				                                         waitPaddingKind,
				                                         currentCycle - blockStartCycle,
				                                         waitCycles + 1,
				                                         ignoredImplicitWawResources ) );
				const unsigned int issueCycle = currentCycle + waitCycles;
				markReadyTokenScheduled( waitUpper, incoming, outgoing, emitted, emittedCount );
				latencyTracker.recordWrites( *block.tokens[waitUpper], static_cast<int>( issueCycle ) );
				currentCycle = issueCycle + 1;
				haveLastPipe = false;
				continue;
			}

			unsigned int best = static_cast<unsigned int>( block.tokens.size() );
			int bestScore = 0;

			for( unsigned int i = 0; i < block.tokens.size(); ++i )
			{
				if( emitted[i] || incoming[i] != 0 )
					continue;

				const int score = readyCandidateScore( i,
				                                       haveLastPipe,
				                                       lastWasLower,
				                                       block,
				                                       priority,
				                                       latencyTracker,
				                                       currentCycle );
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
						slots.push_back( makeIssueSlot( block.tokens[i],
						                                NULL,
						                                0,
						                                1,
						                                ignoredImplicitWawResources ) );
				}
				return slots;
			}

			const unsigned int partner = chooseReadyPairPartner( best,
			                                                     block,
			                                                     incoming,
			                                                     emitted,
			                                                     priority,
			                                                     latencyTracker,
			                                                     ignoredImplicitWawResources,
			                                                     currentCycle );
			const Token* partnerToken = partner < block.tokens.size() ? block.tokens[partner] : NULL;
			const int bestDelay =
				latencyTracker.readHazardDelay( *block.tokens[best],
				                                NULL,
				                                static_cast<int>( currentCycle ) );
			const VuScheduledPaddingKind bestPaddingKind =
				vuScheduledPaddingKindForReadHazard( *block.tokens[best],
				                                     NULL,
				                                     latencyTracker,
				                                     static_cast<int>( currentCycle ) );
			if( bestDelay > 0
			    && tokenSchedulesAsUpper( *block.tokens[best], ignoredImplicitWawResources )
			    && (bestPaddingKind == VU_SCHEDULED_PADDING_WAITQ
			        || bestPaddingKind == VU_SCHEDULED_PADDING_WAITP) )
			{
				const unsigned int waitCycles =
					waitPaddingCycleCount( bestPaddingKind, latencyTracker, currentCycle );
				slots.push_back( makeUpperWaitIssueSlot( block.tokens[best],
				                                         bestPaddingKind,
				                                         currentCycle - blockStartCycle,
				                                         waitCycles + 1,
				                                         ignoredImplicitWawResources ) );
				const unsigned int issueCycle = currentCycle + waitCycles;
				markReadyTokenScheduled( best, incoming, outgoing, emitted, emittedCount );
				latencyTracker.recordWrites( *block.tokens[best], static_cast<int>( issueCycle ) );
				currentCycle = issueCycle + 1;
				haveLastPipe = false;
				continue;
			}

			appendReadHazardPaddingSlots( slots,
			                              *block.tokens[best],
			                              partnerToken,
			                              latencyTracker,
			                              blockStartCycle,
			                              currentCycle );
			slots.push_back( makeIssueSlot( block.tokens[best],
			                                partnerToken,
			                                currentCycle - blockStartCycle,
			                                1,
			                                ignoredImplicitWawResources ) );

			markReadyTokenScheduled( best, incoming, outgoing, emitted, emittedCount );
			latencyTracker.recordWrites( *block.tokens[best], static_cast<int>( currentCycle ) );
			if( partner < block.tokens.size() )
			{
				markReadyTokenScheduled( partner, incoming, outgoing, emitted, emittedCount );
				latencyTracker.recordWrites( *block.tokens[partner], static_cast<int>( currentCycle ) );
				haveLastPipe = false;
			}
			else
			{
				haveLastPipe = true;
				lastWasLower = tokenSchedulesAsLower( *block.tokens[best],
				                                      ignoredImplicitWawResources );
			}
			++currentCycle;
		}

		assignIgnoredImplicitWawResources( slots, ignoredImplicitWawResources );
		return slots;
	}

	void appendIssueSlotsFlat( const std::vector<VuScheduledIssueSlot>& slots,
	                           std::list<Token>& scheduled )
	{
		for( std::vector<VuScheduledIssueSlot>::const_iterator i = slots.begin(); i != slots.end(); ++i )
		{
			if( i->firstToken )
			{
				Token token( *i->firstToken );
				token.setFlags( token.flags() & ~(Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) );
				if( i->secondToken )
					token.setFlags( token.flags() | Token::SCHEDULED_PAIR_FIRST );
				scheduled.push_back( token );
			}
			if( i->secondToken )
			{
				Token token( *i->secondToken );
				token.setFlags( token.flags() & ~(Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) );
				token.setFlags( token.flags() | Token::SCHEDULED_PAIR_SECOND );
				scheduled.push_back( token );
			}
		}
	}

	struct VuFlagLiveness
	{
		VuFlagLiveness()
		{
			lastMacReader = -1;
			lastClipReader = -1;
		}

		int lastMacReader;
		int lastClipReader;
	};

	bool tokenReadsMac( const Token& token )
	{
		return token.operand() && isVuMacReader( token.operand()->name() );
	}

	bool tokenReadsClip( const Token& token )
	{
		return token.operand() && isVuClipReader( token.operand()->name() );
	}

	unsigned int ignoredFlagWawMaskForIndex( unsigned int index,
	                                         int lastMacReader,
	                                         int lastClipReader )
	{
		unsigned int mask = VU_RESOURCE_NONE;
		if( lastMacReader < 0 || index > static_cast<unsigned int>( lastMacReader ) )
			mask |= VU_RESOURCE_MAC;
		if( lastClipReader < 0 || index > static_cast<unsigned int>( lastClipReader ) )
			mask |= VU_RESOURCE_CLIP;
		return mask;
	}

	VuFlagLiveness analyzeFlagLiveness( const std::list<Token>& tokens )
	{
		VuFlagLiveness liveness;
		unsigned int index = 0;
		for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
		{
			if( tokenReadsMac( *i ) )
				liveness.lastMacReader = static_cast<int>( index );
			if( tokenReadsClip( *i ) )
				liveness.lastClipReader = static_cast<int>( index );
		}
		return liveness;
	}

	void appendReadyScheduledSegmentSlots( const std::vector<const Token*>& segment,
	                                       std::vector<VuScheduledIssueSlot>& slots,
	                                       unsigned int ignoredImplicitWawResources,
	                                       VuLatencyTracker& latencyTracker,
	                                       unsigned int blockStartCycle,
	                                       unsigned int& currentCycle )
	{
		std::vector<VuScheduledIssueSlot> segmentSlots =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                latencyTracker,
			                                blockStartCycle,
			                                currentCycle );
		slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );
	}

	std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness(
	    const VuBasicBlock& block,
	    const VuFlagLiveness& liveness,
	    VuLatencyTracker& latencyTracker,
	    unsigned int blockStartCycle )
	{
		std::vector<VuScheduledIssueSlot> slots;
		std::vector<const Token*> segment;
		unsigned int segmentMask = VU_RESOURCE_NONE;
		bool haveSegment = false;
		unsigned int currentCycle = blockStartCycle;

		for( unsigned int offset = 0; offset < block.tokens.size(); ++offset )
		{
			const unsigned int tokenIndex = block.firstTokenIndex + offset;
			const Token& token = *block.tokens[offset];
			if( isVuReadyScheduleCandidate( token ) )
			{
				const unsigned int tokenMask =
					ignoredFlagWawMaskForIndex( tokenIndex,
					                            liveness.lastMacReader,
					                            liveness.lastClipReader );
				if( haveSegment && tokenMask != segmentMask )
				{
					appendReadyScheduledSegmentSlots( segment,
					                                 slots,
					                                 segmentMask,
					                                 latencyTracker,
					                                 blockStartCycle,
					                                 currentCycle );
					segment.clear();
					haveSegment = false;
				}

				if( !haveSegment )
				{
					segmentMask = tokenMask;
					haveSegment = true;
				}

				segment.push_back( &token );

				if( tokenIndex == static_cast<unsigned int>( liveness.lastMacReader )
				    || tokenIndex == static_cast<unsigned int>( liveness.lastClipReader ) )
				{
					appendReadyScheduledSegmentSlots( segment,
					                                 slots,
					                                 segmentMask,
					                                 latencyTracker,
					                                 blockStartCycle,
					                                 currentCycle );
					segment.clear();
					haveSegment = false;
				}
				continue;
			}

			appendReadyScheduledSegmentSlots( segment,
			                                 slots,
			                                 segmentMask,
			                                 latencyTracker,
			                                 blockStartCycle,
			                                 currentCycle );
			segment.clear();
			haveSegment = false;
			bool pairedWithPreviousSlot = false;
			if( !slots.empty() )
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				if( latencyTracker.readHazardDelay( token, NULL, static_cast<int>( pairedCycle ) ) <= 0 )
					pairedWithPreviousSlot =
						tryPairTailWithPreviousSlot( slots,
						                             token,
						                             slots.back().ignoredImplicitWawResources );
			}
			if( !pairedWithPreviousSlot )
			{
				appendReadHazardPaddingSlots( slots,
				                              token,
				                              NULL,
				                              latencyTracker,
				                              blockStartCycle,
				                              currentCycle );
				slots.push_back( makeIssueSlot( &token,
				                                NULL,
				                                currentCycle - blockStartCycle,
				                                1,
				                                VU_RESOURCE_NONE ) );
				latencyTracker.recordWrites( token, static_cast<int>( currentCycle ) );
				++currentCycle;
			}
			else
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				latencyTracker.recordWrites( token, static_cast<int>( pairedCycle ) );
			}
		}

		appendReadyScheduledSegmentSlots( segment,
		                                 slots,
		                                 segmentMask,
		                                 latencyTracker,
		                                 blockStartCycle,
		                                 currentCycle );

		assignScheduledIssueSlotTokenIndices( block, slots );
		return slots;
	}

	bool branchTargetLabel( const Token& token, std::string& label )
	{
		if( !token.operand() || token.operand()->unit() != Operand::BRU )
			return false;

		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin(); i != token.arguments().end(); ++i )
		{
			if( ((*i).flags() & Token::Argument::BRANCH)
			    && (*i).type() == Token::Argument::IMMEDIATE
			    && (*i).immediate().length() != 0 )
			{
				label = (*i).immediate();
				return true;
			}
		}

		return false;
	}

	bool tokenIsLoopDirective( const Token& token )
	{
		return token.operand() && token.operand()->name() == "--LoopCS";
	}

	bool tokenIsLoopExtraDirective( const Token& token )
	{
		return token.operand() && token.operand()->name() == "--LoopExtra";
	}

	bool tokenCanCarrySoftwarePipelineLabel( const Token& token )
	{
		return !token.operand() || tokenIsLoopDirective( token );
	}

	bool parseImmediateLong( const std::string& text, long& value )
	{
		char* end = NULL;
		value = std::strtol( text.c_str(), &end, 0 );
		return end && *end == '\0';
	}

	bool parseImmediateUnsigned( const std::string& text, unsigned int& value )
	{
		long parsed = 0;
		if( !parseImmediateLong( text, parsed ) || parsed < 0 )
			return false;
		value = static_cast<unsigned int>( parsed );
		return true;
	}

	bool describeLoopCsDirective( const Token& token,
	                              unsigned int& clid,
	                              unsigned int& mlid )
	{
		if( !tokenIsLoopDirective( token ) )
			return false;

		const std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 2 )
			return false;

		std::list<Token::Argument>::const_iterator first = args.begin();
		std::list<Token::Argument>::const_iterator second = first;
		++second;
		if( first->type() != Token::Argument::IMMEDIATE
		    || second->type() != Token::Argument::IMMEDIATE )
			return false;

		return parseImmediateUnsigned( first->immediate(), clid )
		    && parseImmediateUnsigned( second->immediate(), mlid );
	}

	bool describeLoopCsDirectiveAtLabel( const std::vector<const Token*>& tokens,
	                                     unsigned int labelIndex,
	                                     unsigned int& clid,
	                                     unsigned int& mlid )
	{
		if( labelIndex < tokens.size()
		    && describeLoopCsDirective( *tokens[labelIndex], clid, mlid ) )
			return true;
		const unsigned int next = labelIndex + 1;
		return next < tokens.size()
		    && describeLoopCsDirective( *tokens[next], clid, mlid );
	}

	bool describeSelfIntegerImmediateUpdate( const Token& token,
	                                         unsigned int tokenIndex,
	                                         VuLoopInductionUpdate& update )
	{
		if( !token.operand() )
			return false;

		const std::string name = lowerVuTokenName( token );
		if( name != "iaddiu" && name != "isubiu" )
			return false;

		const std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 3 )
			return false;

		std::list<Token::Argument>::const_iterator dst = args.begin();
		std::list<Token::Argument>::const_iterator src = dst;
		++src;
		std::list<Token::Argument>::const_iterator imm = src;
		++imm;

		if( (*dst).type() != Token::Argument::INTEGER_REGISTER
		    || (*src).type() != Token::Argument::INTEGER_REGISTER
		    || (*imm).type() != Token::Argument::IMMEDIATE )
			return false;
		if( !((*dst).flags() & Token::Argument::WRITE) )
			return false;

		std::string dstReg;
		std::string srcReg;
		if( !vuRegisterKey( *dst, dstReg ) || !vuRegisterKey( *src, srcReg ) )
			return false;
		if( dstReg != srcReg )
			return false;

		long step = 0;
		const bool stepKnown = parseImmediateLong( (*imm).immediate(), step );
		if( name == "isubiu" )
			step = -step;

		update.registerName = dstReg;
		update.mnemonic = name;
		update.immediate = (*imm).immediate();
		update.step = step;
		update.stepKnown = stepKnown;
		update.tokenIndex = tokenIndex;
		return true;
	}

	bool loopTargetHasDirective( const std::vector<const Token*>& tokens, unsigned int labelIndex )
	{
		if( labelIndex < tokens.size() && tokenIsLoopDirective( *tokens[labelIndex] ) )
			return true;
		const unsigned int next = labelIndex + 1;
		return next < tokens.size() && tokenIsLoopDirective( *tokens[next] );
	}

	void addUniqueString( std::list<std::string>& values, const std::string& value )
	{
		if( !containsKey( values, value ) )
			values.push_back( value );
	}

	void collectRegisterIntersection( const std::list<std::string>& reads,
	                                  const std::list<std::string>& writes,
	                                  std::list<std::string>& intersection )
	{
		for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
		{
			if( containsKey( writes, *i ) )
				addUniqueString( intersection, *i );
		}
	}

	void analyzeLoopCandidateResources( VuLoopCandidate& candidate )
	{
		std::list<std::string> reads;
		std::list<std::string> writes;

		for( unsigned int offset = 0; offset < candidate.bodyTokens.size(); ++offset )
		{
			const Token* token = candidate.bodyTokens[offset];
			VuTokenResourceAccess access;
			if( buildVuTokenResourceAccess( *token, access ) )
			{
				if( access.memoryKind == VU_MEMORY_LOAD )
					++candidate.memoryLoadCount;
				else if( access.memoryKind == VU_MEMORY_STORE )
					++candidate.memoryStoreCount;
				else if( access.memoryKind == VU_MEMORY_XGKICK )
					candidate.hasXgkick = true;

				if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
					candidate.hasMemoryPreOrPostIncrement = true;
			}

			VuLoopInductionUpdate inductionUpdate;
			if( describeSelfIntegerImmediateUpdate( *token,
			                                        candidate.firstBodyTokenIndex + offset,
			                                        inductionUpdate ) )
			{
				addUniqueString( candidate.inductionRegisters, inductionUpdate.registerName );
				candidate.inductionUpdates.push_back( inductionUpdate );
			}

			std::list<std::string> tokenReads;
			std::list<std::string> tokenWrites;
			collectVuRegisterReadKeys( *token, tokenReads );
			collectVuRegisterWriteKeys( *token, tokenWrites );
			for( std::list<std::string>::const_iterator read = tokenReads.begin(); read != tokenReads.end(); ++read )
				addUniqueString( reads, *read );
			for( std::list<std::string>::const_iterator write = tokenWrites.begin(); write != tokenWrites.end(); ++write )
				addUniqueString( writes, *write );
		}

		collectRegisterIntersection( reads, writes, candidate.loopReadWriteRegisters );
	}

	void collectLoopCarriedQInputs( const VuLoopCandidate& loop,
	                                unsigned int consumerOffset,
	                                std::list<std::string>& carriedInputs )
	{
		std::list<std::string> reads;
		collectVuRegisterReadKeys( *loop.bodyTokens[consumerOffset], reads );

		for( unsigned int before = 0; before < consumerOffset; ++before )
		{
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *loop.bodyTokens[before], writes );
			for( std::list<std::string>::const_iterator read = reads.begin(); read != reads.end(); ++read )
			{
				if( containsKey( writes, *read ) )
					addUniqueString( carriedInputs, *read );
			}
		}
	}

	void collectLoopCarriedQOutputs( const VuLoopCandidate& loop,
	                                 unsigned int consumerOffset,
	                                 std::list<std::string>& carriedOutputs )
	{
		std::list<std::string> writes;
		collectVuRegisterWriteKeys( *loop.bodyTokens[consumerOffset], writes );

		for( unsigned int after = consumerOffset + 1; after < loop.bodyTokens.size(); ++after )
		{
			std::list<std::string> reads;
			collectVuRegisterReadKeys( *loop.bodyTokens[after], reads );
			for( std::list<std::string>::const_iterator write = writes.begin(); write != writes.end(); ++write )
			{
				if( containsKey( reads, *write ) )
					addUniqueString( carriedOutputs, *write );
			}
		}
	}

	unsigned int countEmittableTokens( const VuLoopCandidate& loop,
	                                   unsigned int beginOffset,
	                                   unsigned int endOffset )
	{
		unsigned int count = 0;
		if( beginOffset > loop.bodyTokens.size() )
			return 0;
		if( endOffset > loop.bodyTokens.size() )
			endOffset = static_cast<unsigned int>( loop.bodyTokens.size() );
		for( unsigned int i = beginOffset; i < endOffset; ++i )
		{
			const Token& token = *loop.bodyTokens[i];
			if( token.operand()
			    && !token.operand()->isPreprocessor()
			    && !(token.flags() & Token::IGNORED)
			    && (token.operand()->isUpperExecutionPath() || token.operand()->isLowerExecutionPath()) )
				++count;
		}
		return count;
	}

	bool tokenIsPipelineInstruction( const Token& token )
	{
		return token.operand()
		    && !token.operand()->isPreprocessor()
		    && !(token.flags() & Token::IGNORED)
		    && (token.operand()->isUpperExecutionPath() || token.operand()->isLowerExecutionPath());
	}

	void appendPipelineInstructionIndices( const VuLoopCandidate& loop,
	                                       unsigned int beginOffset,
	                                       unsigned int endOffset,
	                                       std::vector<unsigned int>& indices )
	{
		if( beginOffset > loop.bodyTokens.size() )
			return;
		if( endOffset > loop.bodyTokens.size() )
			endOffset = static_cast<unsigned int>( loop.bodyTokens.size() );
		for( unsigned int i = beginOffset; i < endOffset; ++i )
		{
			if( tokenIsPipelineInstruction( *loop.bodyTokens[i] ) )
				indices.push_back( loop.firstBodyTokenIndex + i );
		}
	}

	void addPipelineBlocker( VuLoopPipelineOpportunity& opportunity, const std::string& blocker )
	{
		if( !containsKey( opportunity.softwarePipelineBlockers, blocker ) )
			opportunity.softwarePipelineBlockers.push_back( blocker );
	}

	void addMultiQPipelineBlocker( VuLoopPipelineOpportunity& opportunity, const std::string& blocker )
	{
		if( !containsKey( opportunity.multiQSoftwarePipelineBlockers, blocker ) )
			opportunity.multiQSoftwarePipelineBlockers.push_back( blocker );
	}

	void addSuffixStoreDrainBlocker( VuLoopPipelineOpportunity& opportunity, const std::string& blocker )
	{
		if( !containsKey( opportunity.suffixStoreDrainBlockers, blocker ) )
			opportunity.suffixStoreDrainBlockers.push_back( blocker );
	}

	std::string registerBaseKey( const std::string& key )
	{
		std::string::size_type field = key.find( '.' );
		if( field == std::string::npos )
			return key;
		return key.substr( 0, field );
	}

	// Track 9.G step 4a: shared MII helpers. Compute the recurrence-bound MII
	// (RecMII) and resource-bound MII (ResMII) for a simple-counted loop body.
	// Used by the OPENVCL_DUMP_LOOP_MII diagnostic and intended for reuse by
	// subsequent step-4 sub-tracks (priority list, modulo reservation table,
	// iterative scheduler). Diagnostic / analysis only.
	unsigned int computeLoopRecMII( const std::vector<unsigned int>& mt,
	                                const std::vector<const Token*>& indexedTokens )
	{
		const unsigned int n = static_cast<unsigned int>( mt.size() );
		if( n == 0 ) return 1;
		std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
		for( unsigned int k = 0; k < n; ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			VuTokenResourceAccess acc;
			if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
			for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
			     it != acc.registerWrites.end(); ++it )
				nodeWrites[k].push_back( registerBaseKey( *it ) );
			for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
			     it != acc.registerReads.end(); ++it )
				nodeReads[k].push_back( registerBaseKey( *it ) );
			const unsigned int iw = acc.implicitWrites;
			const unsigned int ir = acc.implicitReads;
			if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
			if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
			if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
			if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
			if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
			if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
			if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
			if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
			if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
			if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
			if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
			if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
			if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
			if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
		}
		std::vector<unsigned int> eFrom, eTo, eDist;
		std::vector<int> eLat;
		unsigned int carried = 0;
		for( unsigned int i = 0; i < n; ++i )
		{
			for( unsigned int j = 0; j < n; ++j )
			{
				if( i == j ) continue;
				const unsigned int dist = ( i < j ) ? 0u : 1u;
				std::string sharedRaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeReads[j][b] )
							sharedRaw = nodeWrites[i][a];
				if( !sharedRaw.empty()
				    && mt[i] < indexedTokens.size()
				    && mt[j] < indexedTokens.size() )
				{
					VuLatencyTracker tr;
					tr.reset();
					tr.recordWrites( *indexedTokens[mt[i]], 0 );
					const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
					const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
					eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
					eLat.push_back( static_cast<int>( lat ) );
					if( dist == 1 ) ++carried;
				}
				std::string sharedWaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeWrites[j][b] )
							sharedWaw = nodeWrites[i][a];
				if( !sharedWaw.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
					eLat.push_back( 1 );
					if( dist == 1 ) ++carried;
				}
				std::string sharedWar;
				for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
						if( nodeReads[i][a] == nodeWrites[j][b] )
							sharedWar = nodeReads[i][a];
				if( !sharedWar.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
					eLat.push_back( 1 );
					if( dist == 1 ) ++carried;
				}
			}
		}
		if( carried == 0 || eFrom.empty() ) return 1;
		const unsigned int E = static_cast<unsigned int>( eFrom.size() );
		double hi = 0.0;
		for( unsigned int e = 0; e < eLat.size(); ++e ) hi += (double)eLat[e];
		if( hi < 1.0 ) hi = 1.0;
		double lo = 0.0;
		std::vector<double> d( n, 0.0 );
		for( int iter = 0; iter < 60; ++iter )
		{
			const double mid = 0.5 * ( lo + hi );
			for( unsigned int v = 0; v < n; ++v ) d[v] = 0.0;
			for( unsigned int pass = 0; pass < n; ++pass )
			{
				bool changed = false;
				for( unsigned int e = 0; e < E; ++e )
				{
					const double w = (double)eLat[e] - mid * (double)eDist[e];
					const double nd = d[ eFrom[e] ] + w;
					if( nd > d[ eTo[e] ] + 1e-12 )
					{
						d[ eTo[e] ] = nd;
						changed = true;
					}
				}
				if( !changed ) break;
			}
			bool positive = false;
			for( unsigned int e = 0; e < E && !positive; ++e )
			{
				const double w = (double)eLat[e] - mid * (double)eDist[e];
				if( d[ eFrom[e] ] + w > d[ eTo[e] ] + 1e-9 )
					positive = true;
			}
			if( positive ) lo = mid;
			else           hi = mid;
		}
		const double recmiiFract = lo;
		unsigned int recmiiInt = static_cast<unsigned int>( recmiiFract );
		if( (double)recmiiInt + 1e-6 < recmiiFract ) ++recmiiInt;
		if( recmiiInt < 1 ) recmiiInt = 1;
		return recmiiInt;
	}

	unsigned int computeLoopResMII( const std::vector<unsigned int>& mt,
	                                const std::vector<const Token*>& indexedTokens )
	{
		unsigned int nUpper = 0, nLower = 0;
		unsigned int fdivBusy = 0, efuBusy = 0;
		for( unsigned int k = 0; k < mt.size(); ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			const Token& tk = *indexedTokens[mt[k]];
			if( !tk.operand() ) continue;
			const VuInstructionInfo* info =
			    findVuInstructionInfo( normalizeVuMnemonic( tk.operand()->name() ) );
			if( !info ) continue;
			if( info->pipe == VU_PIPE_NOP ) continue;
			if( info->pipe == VU_PIPE_UPPER ) ++nUpper;
			else if( info->pipe == VU_PIPE_LOWER ) ++nLower;
			if( info->unit == VU_EXEC_FDIV ) fdivBusy += info->throughput;
			else if( info->unit == VU_EXEC_EFU ) efuBusy += info->throughput;
		}
		unsigned int resmii = nUpper;
		if( nLower    > resmii ) resmii = nLower;
		if( fdivBusy  > resmii ) resmii = fdivBusy;
		if( efuBusy   > resmii ) resmii = efuBusy;
		if( resmii < 1 ) resmii = 1;
		return resmii;
	}

	// Track 9.G step 4c: per-node priority for iterative modulo scheduling.
	// Compute ASAP / ALAP / height / mobility over the intra-iteration DDG
	// (RAW via VuLatencyTracker, WAW/WAR with lat=1). The intra DDG is a DAG
	// because intra edges are emitted only for i<j; topo order = ascending
	// index. Loop-carried edges are intentionally excluded here: they are
	// already captured by RecMII (which bounds II), and ASAP/ALAP within a
	// single iteration are the inputs Lam's iterative modulo scheduler
	// consumes to choose insertion order. Diagnostic / analysis only.
	struct LoopPriorityResult
	{
		std::vector<unsigned int> asap;
		std::vector<unsigned int> alap;
		std::vector<unsigned int> height;
		std::vector<unsigned int> mobility;
		std::vector<unsigned int> order;     // descending priority
		unsigned int              scheduleLength;
	};

	void computeLoopPriority( const std::vector<unsigned int>& mt,
	                          const std::vector<const Token*>& indexedTokens,
	                          unsigned int II,
	                          LoopPriorityResult& out )
	{
		const unsigned int n = static_cast<unsigned int>( mt.size() );
		out.asap.assign( n, 0 );
		out.alap.assign( n, 0 );
		out.height.assign( n, 0 );
		out.mobility.assign( n, 0 );
		out.order.clear();
		out.scheduleLength = 0;
		if( n == 0 ) return;

		std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
		for( unsigned int k = 0; k < n; ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			VuTokenResourceAccess acc;
			if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
			for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
			     it != acc.registerWrites.end(); ++it )
				nodeWrites[k].push_back( registerBaseKey( *it ) );
			for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
			     it != acc.registerReads.end(); ++it )
				nodeReads[k].push_back( registerBaseKey( *it ) );
			const unsigned int iw = acc.implicitWrites;
			const unsigned int ir = acc.implicitReads;
			if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
			if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
			if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
			if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
			if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
			if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
			if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
			if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
			if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
			if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
			if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
			if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
			if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
			if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
		}

		std::vector<unsigned int> eFrom, eTo, eLat;
		for( unsigned int i = 0; i < n; ++i )
		{
			for( unsigned int j = i + 1; j < n; ++j )
			{
				std::string sharedRaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeReads[j][b] )
							sharedRaw = nodeWrites[i][a];
				if( !sharedRaw.empty()
				    && mt[i] < indexedTokens.size()
				    && mt[j] < indexedTokens.size() )
				{
					VuLatencyTracker tr;
					tr.reset();
					tr.recordWrites( *indexedTokens[mt[i]], 0 );
					const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
					const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
					eFrom.push_back( i ); eTo.push_back( j ); eLat.push_back( lat );
				}
				std::string sharedWaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeWrites[j][b] )
							sharedWaw = nodeWrites[i][a];
				if( !sharedWaw.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eLat.push_back( 1 );
				}
				std::string sharedWar;
				for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
						if( nodeReads[i][a] == nodeWrites[j][b] )
							sharedWar = nodeReads[i][a];
				if( !sharedWar.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eLat.push_back( 1 );
				}
			}
		}

		const unsigned int E = static_cast<unsigned int>( eFrom.size() );

		// ASAP via topological relaxation (i<j is a valid topo order).
		for( unsigned int e = 0; e < E; ++e )
		{
			const unsigned int cand = out.asap[ eFrom[e] ] + eLat[e];
			if( cand > out.asap[ eTo[e] ] )
				out.asap[ eTo[e] ] = cand;
		}

		// Height: longest distance from node to any sink. Reverse topo (i: n-1..0).
		for( unsigned int idx = n; idx-- > 0; )
		{
			unsigned int h = 0;
			for( unsigned int e = 0; e < E; ++e )
			{
				if( eFrom[e] == idx )
				{
					const unsigned int cand = eLat[e] + out.height[ eTo[e] ];
					if( cand > h ) h = cand;
				}
			}
			out.height[idx] = h;
		}

		// Schedule length L = max(asap[i] + height[i]) over all nodes; never
		// shorter than II-1 (a one-iteration window must hold at least II
		// slots). ALAP[i] = L - height[i].
		unsigned int L = 0;
		for( unsigned int i = 0; i < n; ++i )
		{
			const unsigned int reach = out.asap[i] + out.height[i];
			if( reach > L ) L = reach;
		}
		if( II > 0 && L + 1 < II ) L = II - 1;
		out.scheduleLength = L;

		for( unsigned int i = 0; i < n; ++i )
			out.alap[i] = ( out.height[i] > L ) ? 0u : ( L - out.height[i] );

		for( unsigned int i = 0; i < n; ++i )
			out.mobility[i] = ( out.alap[i] > out.asap[i] )
			                ? ( out.alap[i] - out.asap[i] ) : 0u;

		// Order by descending height; tie-break by ascending mobility, then
		// ascending node index. Selection sort (n is small, < 50 in practice).
		out.order.resize( n );
		for( unsigned int i = 0; i < n; ++i ) out.order[i] = i;
		for( unsigned int a = 0; a < n; ++a )
		{
			unsigned int best = a;
			for( unsigned int b = a + 1; b < n; ++b )
			{
				const unsigned int ib = out.order[b];
				const unsigned int ia = out.order[best];
				const bool hbBigger     = out.height[ib]   >  out.height[ia];
				const bool hEqual       = out.height[ib]   == out.height[ia];
				const bool mbSmaller    = out.mobility[ib] <  out.mobility[ia];
				const bool mEqual       = out.mobility[ib] == out.mobility[ia];
				const bool idxSmaller   = ib < ia;
				if( hbBigger
				    || ( hEqual && mbSmaller )
				    || ( hEqual && mEqual && idxSmaller ) )
					best = b;
			}
			if( best != a )
			{
				const unsigned int tmp = out.order[a];
				out.order[a] = out.order[best];
				out.order[best] = tmp;
			}
		}
	}

	std::string registerFieldKey( const std::string& key )
	{
		std::string::size_type field = key.find( '.' );
		if( field == std::string::npos || field + 1 >= key.size() )
			return "";
		return key.substr( field + 1 );
	}

	void appendFieldNames( unsigned int fieldMask, std::list<std::string>& fields )
	{
		if( fieldMask == 0 )
			fieldMask = Token::X | Token::Y | Token::Z | Token::W;
		if( fieldMask & Token::X ) addUniqueString( fields, "x" );
		if( fieldMask & Token::Y ) addUniqueString( fields, "y" );
		if( fieldMask & Token::Z ) addUniqueString( fields, "z" );
		if( fieldMask & Token::W ) addUniqueString( fields, "w" );
	}

	bool describeStoreValueRegister( const Token& token,
	                                 std::string& registerName,
	                                 std::list<std::string>& fields,
	                                 bool& isFloatRegister )
	{
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( (*i).flags() & (Token::Argument::INDIRECT | Token::Argument::WRITE) )
				continue;
			if( (*i).type() != Token::Argument::FLOAT_REGISTER
			    && (*i).type() != Token::Argument::INTEGER_REGISTER )
				continue;
			if( !vuRegisterKey( *i, registerName ) )
				return false;
			isFloatRegister = (*i).type() == Token::Argument::FLOAT_REGISTER;
			if( (*i).type() == Token::Argument::FLOAT_REGISTER )
				appendFieldNames( vuReadFieldMask( token, *i ), fields );
			return true;
		}
		return false;
	}

	void collectRotatedRegisterBaseKeys( const std::list<std::string>& keys,
	                                     std::list<std::string>& rotatedRegisters )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
			addUniqueString( rotatedRegisters, registerBaseKey( *i ) );
	}

	void addRotationField( std::vector<VuSoftwarePipelineRotation>& rotations,
	                       const std::string& key,
	                       bool input )
	{
		const std::string base = registerBaseKey( key );
		unsigned int index = static_cast<unsigned int>( rotations.size() );
		for( unsigned int i = 0; i < rotations.size(); ++i )
		{
			if( rotations[i].registerBase == base )
			{
				index = i;
				break;
			}
		}

		if( index == rotations.size() )
		{
			VuSoftwarePipelineRotation rotation;
			rotation.registerBase = base;
			rotation.hasScratchRegister = false;
			rotation.scratchRegister = "";
			rotations.push_back( rotation );
		}

		if( input )
			addUniqueString( rotations[index].inputFields, registerFieldKey( key ) );
		else
			addUniqueString( rotations[index].outputFields, registerFieldKey( key ) );
	}

	void collectRotationDescriptors( const std::list<std::string>& keys,
	                                 bool input,
	                                 std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
			addRotationField( rotations, *i, input );
	}

	void collectVfBaseKeys( const std::list<std::string>& keys, std::list<std::string>& bases )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			const std::string base = registerBaseKey( *i );
			if( base.size() >= 2 && base[0] == 'V' && base[1] == 'F' )
				addUniqueString( bases, base );
		}
	}

	void collectLoopVfBaseKeys( const VuLoopCandidate& loop, std::list<std::string>& bases )
	{
		for( std::vector<const Token*>::const_iterator i = loop.bodyTokens.begin(); i != loop.bodyTokens.end(); ++i )
		{
			std::list<std::string> reads;
			std::list<std::string> writes;
			collectVuRegisterReadKeys( **i, reads );
			collectVuRegisterWriteKeys( **i, writes );
			collectVfBaseKeys( reads, bases );
			collectVfBaseKeys( writes, bases );
		}
	}

	std::string vfRegisterName( unsigned int index )
	{
		std::stringstream s;
		s << "VF";
		if( index < 10 )
			s << "0";
		s << index;
		return s.str();
	}

	// Track 9.E step 3: K-deep rotation bank allocation.
	// depth==1 behaves identically to the original single-scratch implementation;
	// depth>1 fills rotationBank with up to `depth` scratch registers per carried
	// register drawn from the free VF pool.  hasScratchRegister / scratchRegister
	// are kept for backward compat (populated from bank[0] when available).
	void assignRotationScratchRegisters( const VuLoopCandidate& loop,
	                                     std::vector<VuSoftwarePipelineRotation>& rotations,
	                                     unsigned int depth = 1 )
	{
		std::list<std::string> used;
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
			addUniqueString( used, i->registerBase );

		for( std::vector<VuSoftwarePipelineRotation>::iterator rotation = rotations.begin();
		     rotation != rotations.end(); ++rotation )
		{
			rotation->rotationBank.clear();
			for( unsigned int k = 0; k < depth; ++k )
			{
				for( unsigned int reverse = 32; reverse > 1; --reverse )
				{
					const std::string scratch = vfRegisterName( reverse - 1 );
					if( containsKey( used, scratch ) )
						continue;
					rotation->rotationBank.push_back( scratch );
					addUniqueString( used, scratch );
					break;
				}
				if( rotation->rotationBank.size() <= k )
					break;  // free VF pool exhausted for this depth
			}
			if( !rotation->rotationBank.empty() )
			{
				rotation->hasScratchRegister = true;
				rotation->scratchRegister = rotation->rotationBank[0];
			}
		}
	}

	void collectSuffixStoreValueKeys( const VuSoftwarePipelineSuffixStore& store,
	                                  std::list<std::string>& keys )
	{
		if( !store.hasStoredValueRegister )
			return;
		if( !store.storedValueIsFloatRegister )
		{
			addUniqueString( keys, store.storedValueRegister );
			return;
		}
		if( store.storedValueFields.empty() )
		{
			addUniqueString( keys, store.storedValueRegister + ".x" );
			addUniqueString( keys, store.storedValueRegister + ".y" );
			addUniqueString( keys, store.storedValueRegister + ".z" );
			addUniqueString( keys, store.storedValueRegister + ".w" );
			return;
		}
		for( std::list<std::string>::const_iterator field = store.storedValueFields.begin();
		     field != store.storedValueFields.end(); ++field )
			addUniqueString( keys, store.storedValueRegister + "." + *field );
	}

	bool suffixStoreNeedsValueRotation( const VuSoftwarePipelineSuffixStore& store,
	                                    const std::list<std::string>& clobberedKeys )
	{
		std::list<std::string> valueKeys;
		collectSuffixStoreValueKeys( store, valueKeys );
		return intersects( valueKeys, clobberedKeys );
	}

	void collectSoftwarePipelinePrefetchWrites( const VuLoopPipelineOpportunity& opportunity,
	                                            const std::vector<const Token*>& indexedTokens,
	                                            bool rotationsWillUseScratch,
	                                            std::list<std::string>& prefetchWrites );
	bool branchCanInvertToDrain( const Token& token );
	bool tokenRangeHasMemoryDependencyWithStore( const VuSoftwarePipelineSuffixStore& store,
	                                             unsigned int beginIndex,
	                                             unsigned int endIndex,
	                                             const std::vector<const Token*>& indexedTokens,
	                                             const std::vector<VuSoftwarePipelineSuffixStore>& stores );
	Token adjustedMultiQCyclicPrefixToken( const Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                       const std::vector<VuLoopInductionUpdate>& inductionUpdates,
	                                       unsigned int insertionTokenIndex );

	bool tokenRangeWritesAny( unsigned int beginIndex,
	                          unsigned int endIndex,
	                          const std::vector<const Token*>& indexedTokens,
	                          const std::list<std::string>& keys )
	{
		for( unsigned int i = beginIndex; i <= endIndex && i < indexedTokens.size(); ++i )
		{
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *indexedTokens[i], writes );
			if( intersects( writes, keys ) )
				return true;
		}
		return false;
	}

	void assignSuffixStoreValueScratchRegisters( const VuLoopCandidate& loop,
	                                             const std::list<std::string>& clobberedKeys,
	                                             const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                             const std::vector<const Token*>& indexedTokens,
	                                             unsigned int insertionTokenIndex,
	                                             std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		std::list<std::string> used;
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin();
		     i != rotations.end(); ++i )
		{
			addUniqueString( used, i->registerBase );
			if( i->hasScratchRegister )
				addUniqueString( used, i->scratchRegister );
		}

		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store = stores.begin();
		     store != stores.end(); ++store )
		{
			store->requiresValueRotation = false;
			store->hasValueScratchRegister = false;
			store->valueScratchRegister = "";

			if( !store->hasStoredValueRegister
			    || !store->storedValueIsFloatRegister
			    || !suffixStoreNeedsValueRotation( *store, clobberedKeys ) )
				continue;

			std::list<std::string> valueKeys;
			collectSuffixStoreValueKeys( *store, valueKeys );
			if( insertionTokenIndex + 1 < store->tokenIndex
			    && tokenRangeWritesAny( insertionTokenIndex + 1,
			                            store->tokenIndex - 1,
			                            indexedTokens,
			                            valueKeys ) )
				continue;

			store->requiresValueRotation = true;
			store->rotateValueBeforePrefetch = true;
			for( unsigned int reverse = 32; reverse > 1; --reverse )
			{
				const std::string scratch = vfRegisterName( reverse - 1 );
				if( containsKey( used, scratch ) )
					continue;
				store->hasValueScratchRegister = true;
				store->valueScratchRegister = scratch;
				addUniqueString( used, scratch );
				break;
			}
		}
	}

	void collectUsedScratchVfRegisters( const VuLoopCandidate& loop,
	                                    const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                    const std::vector<VuSoftwarePipelineSuffixStore>& stores,
	                                    std::list<std::string>& used )
	{
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin();
		     i != rotations.end(); ++i )
		{
			addUniqueString( used, i->registerBase );
			if( i->hasScratchRegister )
				addUniqueString( used, i->scratchRegister );
		}
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->hasValueScratchRegister )
				addUniqueString( used, i->valueScratchRegister );
		}
	}

	bool assignOneSuffixStoreValueScratchRegister( VuSoftwarePipelineSuffixStore& store,
	                                               std::list<std::string>& used )
	{
		if( store.hasValueScratchRegister )
			return true;

		for( unsigned int reverse = 32; reverse > 1; --reverse )
		{
			const std::string scratch = vfRegisterName( reverse - 1 );
			if( containsKey( used, scratch ) )
				continue;
			store.hasValueScratchRegister = true;
			store.valueScratchRegister = scratch;
			addUniqueString( used, scratch );
			return true;
		}

		return false;
	}

	void classifySuffixStoreDrainOpportunity( VuLoopPipelineOpportunity& opportunity,
	                                          const VuLoopCandidate& loop,
	                                          bool rotationsWillUseScratch,
	                                          const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.softwarePipelineSuffixStores.empty() )
			return;

		opportunity.hasSuffixStoreDrainPlan = false;
		opportunity.canEmitSuffixStoreDrain = false;

		if( opportunity.branchTokenIndex >= indexedTokens.size()
		    || !branchCanInvertToDrain( *indexedTokens[opportunity.branchTokenIndex] ) )
			addSuffixStoreDrainBlocker( opportunity, "non_invertible_loop_branch" );
		if( opportunity.qLiveOut )
			addSuffixStoreDrainBlocker( opportunity, "q_live_out" );

		bool hasDrainCandidate = false;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = opportunity.softwarePipelineSuffixStores.begin();
		     i != opportunity.softwarePipelineSuffixStores.end(); ++i )
		{
			if( i->drainCandidate )
				hasDrainCandidate = true;
		}
		if( !hasDrainCandidate )
			return;

		opportunity.hasSuffixStoreDrainPlan = true;

		std::list<std::string> prefetchWrites;
		collectSoftwarePipelinePrefetchWrites( opportunity,
		                                       indexedTokens,
		                                       rotationsWillUseScratch,
		                                       prefetchWrites );
		std::list<std::string> used;
		collectUsedScratchVfRegisters( loop,
		                               opportunity.softwarePipelineRotations,
		                               opportunity.softwarePipelineSuffixStores,
		                               used );

		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( !store->drainCandidate )
				continue;
			if( !store->hasNextIterationOffset )
			{
				addSuffixStoreDrainBlocker( opportunity, "store_without_next_offset" );
				continue;
			}
			if( !store->hasStoredValueRegister )
			{
				addSuffixStoreDrainBlocker( opportunity, "store_without_value_register" );
				continue;
			}
			if( store->tokenIndex < indexedTokens.size()
			    && (indexedTokens[store->tokenIndex]->flags() & (Token::PREORDERED | Token::E | Token::D
			                                                   | Token::T | Token::BRANCH_DELAY_FILLER)) )
			{
				addSuffixStoreDrainBlocker( opportunity, "store_has_control_flag" );
				continue;
			}

			std::list<std::string> valueKeys;
			collectSuffixStoreValueKeys( *store, valueKeys );
			const bool valueWrittenAfterStore =
			    store->tokenIndex + 1 <= opportunity.branchTokenIndex
			    && tokenRangeWritesAny( store->tokenIndex + 1,
			                            opportunity.branchTokenIndex,
			                            indexedTokens,
			                            valueKeys );
			const bool prefetchClobbersValue =
			    suffixStoreNeedsValueRotation( *store, prefetchWrites );
			if( valueWrittenAfterStore || prefetchClobbersValue )
			{
				if( !store->storedValueIsFloatRegister )
				{
					addSuffixStoreDrainBlocker( opportunity, "non_vf_store_value_rotation" );
					continue;
				}
				store->requiresValueRotation = true;
				if( prefetchClobbersValue && store->tokenIndex > opportunity.qConsumerTokenIndices.back() )
					store->rotateValueBeforePrefetch = true;
				else
					store->rotateValueAtStore = true;
				if( !assignOneSuffixStoreValueScratchRegister( *store, used ) )
					addSuffixStoreDrainBlocker( opportunity, "missing_suffix_store_value_scratch" );
			}
			store->delayedDrain = true;
		}

		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( !store->delayedDrain )
				continue;
			if( tokenRangeHasMemoryDependencyWithStore( *store,
			                                           store->tokenIndex + 1,
			                                           opportunity.branchTokenIndex,
			                                           indexedTokens,
			                                           opportunity.softwarePipelineSuffixStores ) )
				addSuffixStoreDrainBlocker( opportunity, "delayed_store_memory_dependency" );
		}

		if( !opportunity.suffixStoreDrainBlockers.empty() )
		{
			for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
			         opportunity.softwarePipelineSuffixStores.begin();
			     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
			{
				store->delayedDrain = false;
				store->rotateValueBeforePrefetch = false;
				store->rotateValueAtStore = false;
			}
			return;
		}

		opportunity.canEmitSuffixStoreDrain = true;
	}

	const VuSoftwarePipelineRotation* findRotationForBase( const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                                       const std::string& base )
	{
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
		{
			if( i->registerBase == base )
				return &*i;
		}
		return NULL;
	}

	const VuLoopInductionUpdate* findInductionUpdate( const std::vector<VuLoopInductionUpdate>& updates,
	                                                  const std::string& registerName )
	{
		for( std::vector<VuLoopInductionUpdate>::const_iterator i = updates.begin(); i != updates.end(); ++i )
		{
			if( i->registerName == registerName )
				return &*i;
		}
		return NULL;
	}

	bool aggregateInductionStepAfterToken( const std::vector<VuLoopInductionUpdate>& updates,
	                                       const std::string& registerName,
	                                       unsigned int tokenIndex,
	                                       long& step )
	{
		bool found = false;
		step = 0;
		for( std::vector<VuLoopInductionUpdate>::const_iterator i = updates.begin();
		     i != updates.end(); ++i )
		{
			if( i->registerName != registerName || i->tokenIndex <= tokenIndex )
				continue;
			if( !i->stepKnown )
				return false;
			step += i->step;
			found = true;
		}
		return found;
	}

	void appendSoftwarePipelinePrefetchDescriptor( const Token& token,
	                                               unsigned int tokenIndex,
	                                               const VuLoopPipelineOpportunity& opportunity,
	                                               std::vector<VuSoftwarePipelinePrefetch>& prefetches )
	{
		VuSoftwarePipelinePrefetch prefetch;
		prefetch.tokenIndex = tokenIndex;
		prefetch.mnemonic = lowerVuTokenName( token );
		prefetch.memoryKind = VU_MEMORY_NONE;
		prefetch.memoryFlags = VU_MEMORY_FLAG_NONE;
		prefetch.hasMemoryBase = false;
		prefetch.memoryBaseRegister = "";
		prefetch.hasMemoryOffset = false;
		prefetch.memoryOffset = 0;
		prefetch.readsInductionRegister = false;
		prefetch.inductionRegister = "";
		prefetch.hasNextIterationOffset = false;
		prefetch.nextIterationOffset = 0;

		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access ) )
		{
			prefetch.memoryKind = access.memoryKind;
			prefetch.memoryFlags = access.memoryFlags;
			prefetch.hasMemoryBase = access.hasMemoryBase;
			prefetch.memoryBaseRegister = access.memoryBaseRegister;
			prefetch.hasMemoryOffset = access.hasMemoryOffset;
			prefetch.memoryOffset = access.memoryOffset;

			for( std::list<std::string>::const_iterator i = opportunity.inductionRegisters.begin();
			     i != opportunity.inductionRegisters.end(); ++i )
			{
				if( containsKey( access.registerReads, *i )
				    || (access.hasMemoryBase && access.memoryBaseRegister == *i) )
				{
					prefetch.readsInductionRegister = true;
					prefetch.inductionRegister = *i;
					break;
				}
			}

			const VuLoopInductionUpdate* update =
			    access.hasMemoryBase
			    ? findInductionUpdate( opportunity.inductionUpdates, access.memoryBaseRegister )
			    : NULL;
			if( update && update->stepKnown && access.hasMemoryOffset )
			{
				prefetch.hasNextIterationOffset = true;
				prefetch.nextIterationOffset = access.memoryOffset + update->step;
			}
		}

		prefetches.push_back( prefetch );
	}

	void appendSoftwarePipelineSuffixStoreDescriptor( const Token& token,
	                                                  unsigned int tokenIndex,
	                                                  const VuLoopPipelineOpportunity& opportunity,
	                                                  std::vector<VuSoftwarePipelineSuffixStore>& suffixStores )
	{
		VuSoftwarePipelineSuffixStore suffixStore;
		suffixStore.tokenIndex = tokenIndex;
		suffixStore.mnemonic = lowerVuTokenName( token );
		suffixStore.hasMemoryBase = false;
		suffixStore.memoryBaseRegister = "";
		suffixStore.hasMemoryOffset = false;
		suffixStore.memoryOffset = 0;
		suffixStore.usesInductionRegister = false;
		suffixStore.inductionRegister = "";
		suffixStore.hasNextIterationOffset = false;
		suffixStore.nextIterationOffset = 0;
		suffixStore.drainCandidate = false;
		suffixStore.hasStoredValueRegister = false;
		suffixStore.storedValueIsFloatRegister = false;
		suffixStore.storedValueRegister = "";
		suffixStore.requiresValueRotation = false;
		suffixStore.hasValueScratchRegister = false;
		suffixStore.valueScratchRegister = "";
		suffixStore.delayedDrain = false;
		suffixStore.rotateValueBeforePrefetch = false;
		suffixStore.rotateValueAtStore = false;

		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access )
		    && access.memoryKind == VU_MEMORY_STORE
		    && access.memoryFlags == VU_MEMORY_FLAG_NONE )
		{
			suffixStore.hasMemoryBase = access.hasMemoryBase;
			suffixStore.memoryBaseRegister = access.memoryBaseRegister;
			suffixStore.hasMemoryOffset = access.hasMemoryOffset;
			suffixStore.memoryOffset = access.memoryOffset;

			for( std::list<std::string>::const_iterator i = opportunity.inductionRegisters.begin();
			     i != opportunity.inductionRegisters.end(); ++i )
			{
				if( access.hasMemoryBase && access.memoryBaseRegister == *i )
				{
					suffixStore.usesInductionRegister = true;
					suffixStore.inductionRegister = *i;
					break;
				}
			}

			long stepAfterStore = 0;
			if( access.hasMemoryBase
			    && access.hasMemoryOffset
			    && aggregateInductionStepAfterToken( opportunity.inductionUpdates,
			                                         access.memoryBaseRegister,
			                                         tokenIndex,
			                                         stepAfterStore ) )
			{
				suffixStore.hasNextIterationOffset = true;
				suffixStore.nextIterationOffset = access.memoryOffset - stepAfterStore;
				suffixStore.drainCandidate = suffixStore.usesInductionRegister;
			}

			suffixStore.hasStoredValueRegister =
			    describeStoreValueRegister( token,
			                                suffixStore.storedValueRegister,
			                                suffixStore.storedValueFields,
			                                suffixStore.storedValueIsFloatRegister );
		}

		suffixStores.push_back( suffixStore );
	}

	bool isIbeqOrIbne( const Token& token )
	{
		const std::string name = lowerVuTokenName( token );
		return name == "ibeq" || name == "ibne";
	}

	bool branchCanInvertToDrain( const Token& token )
	{
		if( !isIbeqOrIbne( token ) )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( (access.instructionFlags & VU_INSTR_BRANCH) == 0 )
			return false;
		if( access.instructionFlags & (VU_INSTR_UNCONDITIONAL_BRANCH
		                             | VU_INSTR_LINK_BRANCH
		                             | VU_INSTR_REGISTER_BRANCH) )
			return false;

		std::string target;
		return branchTargetLabel( token, target );
	}

	const Operand* syntheticIbeqOperand()
	{
		static const Operand ibeqOperand( "IBEQ",
		                                  3,
		                                  Operand::LOWER | Operand::DYNAMIC,
		                                  "vi,vi,imm:branch",
		                                  Operand::BRU,
		                                  2,
		                                  2 );
		return &ibeqOperand;
	}

	const Operand* syntheticIbneOperand()
	{
		static const Operand ibneOperand( "IBNE",
		                                  3,
		                                  Operand::LOWER | Operand::DYNAMIC,
		                                  "vi,vi,imm:branch",
		                                  Operand::BRU,
		                                  2,
		                                  2 );
		return &ibneOperand;
	}

	Token invertedBranchToDrainToken( const Token& token, const std::string& drainLabel )
	{
		Token branch( token );
		branch.setLabel( "" );
		const std::string name = lowerVuTokenName( branch );
		if( name == "ibne" )
		{
			branch.setName( "ibeq" );
			branch.setOperand( syntheticIbeqOperand() );
		}
		else if( name == "ibeq" )
		{
			branch.setName( "ibne" );
			branch.setOperand( syntheticIbneOperand() );
		}

		for( std::list<Token::Argument>::iterator i = branch.arguments().begin();
		     i != branch.arguments().end(); ++i )
		{
			if( ((*i).flags() & Token::Argument::BRANCH)
			    && (*i).type() == Token::Argument::IMMEDIATE )
				i->setImmediate( drainLabel );
		}

		return branch;
	}

	bool delayedSuffixStoreForTokenIndex( const std::vector<VuSoftwarePipelineSuffixStore>& stores,
	                                      unsigned int tokenIndex )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->tokenIndex == tokenIndex )
				return i->delayedDrain;
		}
		return false;
	}

	bool tokenRangeHasMemoryDependencyWithStore( const VuSoftwarePipelineSuffixStore& store,
	                                             unsigned int beginIndex,
	                                             unsigned int endIndex,
	                                             const std::vector<const Token*>& indexedTokens,
	                                             const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		if( store.tokenIndex >= indexedTokens.size() )
			return true;

		VuTokenResourceAccess storeAccess;
		if( !buildVuTokenResourceAccess( *indexedTokens[store.tokenIndex], storeAccess ) )
			return true;

		for( unsigned int tokenIndex = beginIndex; tokenIndex <= endIndex && tokenIndex < indexedTokens.size(); ++tokenIndex )
		{
			if( tokenIndex == store.tokenIndex || delayedSuffixStoreForTokenIndex( stores, tokenIndex ) )
				continue;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[tokenIndex], access ) )
				continue;
			if( memoryOrderRequiresDependency( storeAccess,
			                                  access,
			                                  *indexedTokens[store.tokenIndex],
			                                  *indexedTokens[tokenIndex] )
			    || memoryOrderRequiresDependency( access,
			                                     storeAccess,
			                                     *indexedTokens[tokenIndex],
			                                     *indexedTokens[store.tokenIndex] ) )
				return true;
		}

		return false;
	}

	void collectTokenWriteKeys( const std::vector<unsigned int>& tokenIndices,
	                            const std::vector<const Token*>& indexedTokens,
	                            std::list<std::string>& writes )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;
			std::list<std::string> tokenWrites;
			collectVuRegisterWriteKeys( *indexedTokens[*i], tokenWrites );
			for( std::list<std::string>::const_iterator write = tokenWrites.begin(); write != tokenWrites.end(); ++write )
				addUniqueString( writes, *write );
		}
	}

	void collectTokenWriteBaseKeys( const std::vector<unsigned int>& tokenIndices,
	                                const std::vector<const Token*>& indexedTokens,
	                                std::list<std::string>& bases )
	{
		std::list<std::string> writes;
		collectTokenWriteKeys( tokenIndices, indexedTokens, writes );
		collectRotatedRegisterBaseKeys( writes, bases );
	}

	void retainPrefetchedRotations( VuLoopPipelineOpportunity& opportunity,
	                                const std::vector<const Token*>& indexedTokens )
	{
		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		std::list<std::string> prefetchWriteBases;
		collectTokenWriteBaseKeys( prefetchTokenIndices, indexedTokens, prefetchWriteBases );

		for( std::vector<VuSoftwarePipelineRotation>::iterator i = opportunity.softwarePipelineRotations.begin();
		     i != opportunity.softwarePipelineRotations.end(); )
		{
			if( containsKey( prefetchWriteBases, i->registerBase ) )
				++i;
			else
				i = opportunity.softwarePipelineRotations.erase( i );
		}

		for( std::list<std::string>::iterator i = opportunity.softwarePipelineRotatedRegisters.begin();
		     i != opportunity.softwarePipelineRotatedRegisters.end(); )
		{
			if( containsKey( prefetchWriteBases, *i ) )
				++i;
			else
				i = opportunity.softwarePipelineRotatedRegisters.erase( i );
		}
	}

	bool rotationBaseListContains( const std::list<std::string>& bases, const std::string& base )
	{
		return containsKey( bases, base );
	}

	bool scheduledBlockLabel( const VuScheduledBasicBlock& block, std::string& label )
	{
		for( std::vector<const Token*>::const_iterator i = block.block.tokens.begin();
		     i != block.block.tokens.end(); ++i )
		{
			if( (*i)->label().length() != 0 )
			{
				label = (*i)->label();
				return true;
			}
		}
		return false;
	}

	bool softwarePipelineRotationsCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                       const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.softwarePipelineRotations.empty() )
			return true;

		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		std::list<std::string> prefetchWrites;
		std::list<std::string> prefetchWriteBases;
		collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
		collectRotatedRegisterBaseKeys( prefetchWrites, prefetchWriteBases );

		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = opportunity.softwarePipelineRotations.begin();
		     i != opportunity.softwarePipelineRotations.end(); ++i )
		{
			if( !i->hasScratchRegister )
				return false;
			if( !rotationBaseListContains( prefetchWriteBases, i->registerBase ) )
				return false;
		}

		if( opportunity.qConsumerTokenIndices.empty()
		    || opportunity.qConsumerTokenIndices.back() + 1 > opportunity.branchTokenIndex )
			return true;

		std::list<std::string> rotatedBases;
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = opportunity.softwarePipelineRotations.begin();
		     i != opportunity.softwarePipelineRotations.end(); ++i )
			addUniqueString( rotatedBases, i->registerBase );

		for( unsigned int tokenIndex = opportunity.qConsumerTokenIndices.back() + 1;
		     tokenIndex <= opportunity.branchTokenIndex && tokenIndex < indexedTokens.size(); ++tokenIndex )
		{
			std::list<std::string> writes;
			std::list<std::string> writeBases;
			collectVuRegisterWriteKeys( *indexedTokens[tokenIndex], writes );
			collectRotatedRegisterBaseKeys( writes, writeBases );
			for( std::list<std::string>::const_iterator i = writeBases.begin(); i != writeBases.end(); ++i )
			{
				if( containsKey( rotatedBases, *i ) )
					return false;
			}
		}

		return true;
	}

	void removeRotationCoveredWrites( std::list<std::string>& writes,
	                                  const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<std::string>::iterator i = writes.begin(); i != writes.end(); )
		{
			if( findRotationForBase( rotations, registerBaseKey( *i ) ) )
				i = writes.erase( i );
			else
				++i;
		}
	}

	void collectSoftwarePipelinePrefetchWrites( const VuLoopPipelineOpportunity& opportunity,
	                                            const std::vector<const Token*>& indexedTokens,
	                                            bool rotationsWillUseScratch,
	                                            std::list<std::string>& prefetchWrites )
	{
		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
		if( rotationsWillUseScratch )
			removeRotationCoveredWrites( prefetchWrites, opportunity.softwarePipelineRotations );
	}

	const VuSoftwarePipelineSuffixStore* findSuffixStoreForTokenIndex(
	    const std::vector<VuSoftwarePipelineSuffixStore>& stores,
	    unsigned int tokenIndex )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->tokenIndex == tokenIndex )
				return &*i;
		}
		return NULL;
	}

	bool suffixStoreRotationCoversReads( const VuSoftwarePipelineSuffixStore& store,
	                                     const VuTokenResourceAccess& access,
	                                     const std::list<std::string>& clobberedKeys )
	{
		if( !store.requiresValueRotation || !store.hasValueScratchRegister )
			return false;
		if( intersects( access.registerWrites, clobberedKeys ) )
			return false;

		std::list<std::string> valueKeys;
		collectSuffixStoreValueKeys( store, valueKeys );
		for( std::list<std::string>::const_iterator read = access.registerReads.begin();
		     read != access.registerReads.end(); ++read )
		{
			if( containsKey( clobberedKeys, *read ) && !containsKey( valueKeys, *read ) )
				return false;
		}

		return true;
	}

	bool prefetchWritesClobberSuffix( const VuLoopPipelineOpportunity& opportunity,
	                                  const std::vector<const Token*>& indexedTokens,
	                                  const std::list<std::string>& prefetchWrites )
	{
		if( prefetchWrites.empty() )
			return false;

		const unsigned int insertAfter = opportunity.qConsumerTokenIndices.back();
		if( insertAfter + 1 > opportunity.branchTokenIndex )
			return false;

		for( unsigned int tokenIndex = insertAfter + 1;
		     tokenIndex <= opportunity.branchTokenIndex && tokenIndex < indexedTokens.size();
		     ++tokenIndex )
		{
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[tokenIndex], access ) )
				continue;
			if( !intersects( access.registerReads, prefetchWrites )
			    && !intersects( access.registerWrites, prefetchWrites ) )
				continue;

			const VuSoftwarePipelineSuffixStore* store =
			    findSuffixStoreForTokenIndex( opportunity.softwarePipelineSuffixStores, tokenIndex );
			if( store && suffixStoreRotationCoversReads( *store, access, prefetchWrites ) )
				continue;
			return true;
		}

		return false;
	}

	bool softwarePipelinePrefetchesCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                        const std::vector<const Token*>& indexedTokens,
	                                        bool rotationsWillUseScratch )
	{
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
		{
			if( i->memoryKind != VU_MEMORY_NONE && i->memoryKind != VU_MEMORY_LOAD )
				return false;
			if( i->memoryFlags != VU_MEMORY_FLAG_NONE )
				return false;
			if( i->readsInductionRegister && i->memoryKind != VU_MEMORY_LOAD )
				return false;
			if( i->readsInductionRegister && !i->hasNextIterationOffset )
				return false;
		}

		if( opportunity.qConsumerTokenIndices.empty() )
			return false;

		std::list<std::string> prefetchWrites;
		collectSoftwarePipelinePrefetchWrites( opportunity,
		                                       indexedTokens,
		                                       rotationsWillUseScratch,
		                                       prefetchWrites );
		return !prefetchWritesClobberSuffix( opportunity, indexedTokens, prefetchWrites );
	}

	bool softwarePipelineQDrainCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                    const VuLoopCandidate& loop,
	                                    unsigned int qProducerOffset )
	{
		if( qProducerOffset >= loop.bodyTokens.size() )
			return false;
		if( !opportunity.softwarePipelinePrefetches.empty()
		    || !opportunity.softwarePipelineRotations.empty() )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( *loop.bodyTokens[qProducerOffset], access ) )
			return false;
		if( access.memoryKind != VU_MEMORY_NONE )
			return false;
		if( intersects( access.registerReads, opportunity.inductionRegisters ) )
			return false;
		if( intersects( access.registerReads, opportunity.loopReadWriteRegisters ) )
			return false;

		return true;
	}

	bool qProducerCanMoveIntoBranchDelaySlot( const VuLoopPipelineOpportunity& opportunity,
	                                          const std::vector<const Token*>& indexedTokens,
	                                          unsigned int* suffixBlockerTokenIndex )
	{
		if( suffixBlockerTokenIndex )
			*suffixBlockerTokenIndex = 0;
		if( opportunity.qLiveOut
		    || opportunity.qConsumerTokenIndices.empty()
		    || opportunity.qProducerTokenIndex >= indexedTokens.size()
		    || opportunity.branchTokenIndex >= indexedTokens.size() )
			return false;

		const Token& qProducer = *indexedTokens[opportunity.qProducerTokenIndex];
		const Token& branch = *indexedTokens[opportunity.branchTokenIndex];
		if( vuTokenBranchDelaySlots( branch ) != 1 )
			return false;
		if( !qProducer.operand() || !qProducer.operand()->isLowerExecutionPath() )
			return false;

		VuTokenResourceAccess qProducerAccess;
		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( qProducer, qProducerAccess )
		    || !buildVuTokenResourceAccess( branch, branchAccess ) )
			return false;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			return false;
		if( qProducerAccess.memoryKind != VU_MEMORY_NONE
		    || qProducerAccess.memoryFlags != VU_MEMORY_FLAG_NONE
		    || qProducerAccess.branchDelaySlots > 0 )
			return false;
		if( qProducerAccess.instructionFlags & (VU_INSTR_BRANCH
		                                      | VU_INSTR_WAIT_Q
		                                      | VU_INSTR_WAIT_P
		                                      | VU_INSTR_XGKICK) )
			return false;
		if( (qProducerAccess.instructionFlags & (VU_INSTR_WRITES_Q | VU_INSTR_WRITES_P)) == 0 )
			return false;

		if( intersects( qProducerAccess.registerWrites, branchAccess.registerReads )
		    || intersects( qProducerAccess.registerWrites, branchAccess.registerWrites )
		    || intersects( qProducerAccess.registerReads, branchAccess.registerWrites ) )
			return false;
		if( qProducerAccess.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
			return false;
		if( branchAccess.implicitWrites & (qProducerAccess.implicitReads | qProducerAccess.implicitWrites) )
			return false;

		for( unsigned int tokenIndex = opportunity.qConsumerTokenIndices.back() + 1;
		     tokenIndex < opportunity.branchTokenIndex && tokenIndex < indexedTokens.size();
		     ++tokenIndex )
		{
			// Keep every suffix instruction before the branch unless it cannot safely
			// stay ahead of the cloned next-iteration Q producer.
			if( !vuTokenCanMoveBefore( *indexedTokens[tokenIndex], qProducer, VU_RESOURCE_NONE ) )
			{
				if( suffixBlockerTokenIndex )
					*suffixBlockerTokenIndex = tokenIndex;
				return false;
			}
		}

		return true;
	}

	void collectSoftwarePipelinePrefetchDescriptors( const std::vector<unsigned int>& prologTokenIndices,
	                                                unsigned int qProducerTokenIndex,
	                                                const std::vector<const Token*>& indexedTokens,
	                                                VuLoopPipelineOpportunity& opportunity )
	{
		for( std::vector<unsigned int>::const_iterator i = prologTokenIndices.begin();
		     i != prologTokenIndices.end(); ++i )
		{
			if( *i == qProducerTokenIndex || *i >= indexedTokens.size() )
				continue;
			appendSoftwarePipelinePrefetchDescriptor( *indexedTokens[*i],
			                                          *i,
			                                          opportunity,
			                                          opportunity.softwarePipelinePrefetches );
		}
	}

	void collectSoftwarePipelineSuffixStoreDescriptors( const std::vector<unsigned int>& tokenIndices,
	                                                   const std::vector<const Token*>& indexedTokens,
	                                                   VuLoopPipelineOpportunity& opportunity )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access )
			    || access.memoryKind != VU_MEMORY_STORE
			    || access.memoryFlags != VU_MEMORY_FLAG_NONE )
				continue;

			appendSoftwarePipelineSuffixStoreDescriptor( *indexedTokens[*i],
			                                             *i,
			                                             opportunity,
			                                             opportunity.softwarePipelineSuffixStores );
		}
	}

	void collectAdjustedCyclicPrefixWriteKeys( const VuLoopPipelineOpportunity& opportunity,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           std::list<std::string>& writes )
	{
		for( std::vector<unsigned int>::const_iterator i =
		         opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;
			const Token adjusted =
			    adjustedMultiQCyclicPrefixToken( *indexedTokens[*i],
			                                     opportunity.multiQCyclicPrefixRotations,
			                                     opportunity.inductionUpdates,
			                                     opportunity.multiQCyclicPrefixInsertBeforeTokenIndex );
			std::list<std::string> tokenWrites;
			collectVuRegisterWriteKeys( adjusted, tokenWrites );
			for( std::list<std::string>::const_iterator write = tokenWrites.begin();
			     write != tokenWrites.end(); ++write )
				addUniqueString( writes, *write );
		}
	}

	bool tokenIndicesLoadFromMemoryBase( const std::vector<unsigned int>& tokenIndices,
	                                     const std::vector<const Token*>& indexedTokens,
	                                     const std::string& memoryBaseRegister )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				continue;
			if( access.memoryKind == VU_MEMORY_LOAD
			    && access.hasMemoryBase
			    && access.memoryBaseRegister == memoryBaseRegister )
				return true;
		}
		return false;
	}

	bool suffixStoreHasReadWriteMemoryStreamConflict( const VuSoftwarePipelineSuffixStore& store,
	                                                  const VuLoopPipelineOpportunity& opportunity,
	                                                  const std::vector<const Token*>& indexedTokens )
	{
		if( !store.hasMemoryBase )
			return true;
		return tokenIndicesLoadFromMemoryBase( opportunity.multiQMainTokenIndices,
		                                       indexedTokens,
		                                       store.memoryBaseRegister )
		    || tokenIndicesLoadFromMemoryBase( opportunity.multiQCyclicPrefixTokenIndices,
		                                       indexedTokens,
		                                       store.memoryBaseRegister );
	}

	bool loadedMultiQDrainLeavesPartialStoreStream(
	    const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		std::list<std::string> delayedBases;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         stores.begin(); store != stores.end(); ++store )
		{
			if( store->delayedDrain && store->hasMemoryBase )
				addUniqueString( delayedBases, store->memoryBaseRegister );
		}
		if( delayedBases.empty() )
			return false;

		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         stores.begin(); store != stores.end(); ++store )
		{
			if( store->drainCandidate
			    && store->hasMemoryBase
			    && containsKey( delayedBases, store->memoryBaseRegister )
			    && !store->delayedDrain )
				return true;
		}

		return false;
	}

	void clearSuffixStoreDrainSelection( std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
		         stores.begin(); store != stores.end(); ++store )
		{
			store->requiresValueRotation = false;
			store->hasValueScratchRegister = false;
			store->valueScratchRegister = "";
			store->delayedDrain = false;
			store->rotateValueBeforePrefetch = false;
			store->rotateValueAtStore = false;
		}
	}

	void classifyMultiQCyclicPrefixSuffixStoreDrains( VuLoopPipelineOpportunity& opportunity,
	                                                  const VuLoopCandidate& loop,
	                                                  const std::vector<const Token*>& indexedTokens )
	{
		opportunity.hasSuffixStoreDrainPlan = false;
		opportunity.canEmitSuffixStoreDrain = false;
		opportunity.suffixStoreDrainBlockers.clear();
		opportunity.softwarePipelineSuffixStores.clear();

		collectSoftwarePipelineSuffixStoreDescriptors( opportunity.multiQMainTokenIndices,
		                                               indexedTokens,
		                                               opportunity );
		if( opportunity.softwarePipelineSuffixStores.empty() )
			return;

		bool hasDrainCandidate = false;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i =
		         opportunity.softwarePipelineSuffixStores.begin();
		     i != opportunity.softwarePipelineSuffixStores.end(); ++i )
		{
			if( i->drainCandidate )
				hasDrainCandidate = true;
		}
		if( !hasDrainCandidate )
			return;

		opportunity.hasSuffixStoreDrainPlan = true;
		if( opportunity.branchTokenIndex >= indexedTokens.size()
		    || !branchCanInvertToDrain( *indexedTokens[opportunity.branchTokenIndex] ) )
			addSuffixStoreDrainBlocker( opportunity, "non_invertible_loop_branch" );
		if( opportunity.qLiveOut )
			addSuffixStoreDrainBlocker( opportunity, "q_live_out" );
		if( !opportunity.suffixStoreDrainBlockers.empty() )
			return;

		std::list<std::string> cyclicPrefixWrites;
		collectAdjustedCyclicPrefixWriteKeys( opportunity, indexedTokens, cyclicPrefixWrites );
		std::list<std::string> used;
		collectUsedScratchVfRegisters( loop,
		                               opportunity.multiQCyclicPrefixRotations,
		                               opportunity.softwarePipelineSuffixStores,
		                               used );

		bool readWriteMemoryStreamConflict = false;
		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( !store->drainCandidate )
				continue;
			if( !store->hasNextIterationOffset )
				continue;
			if( !store->hasStoredValueRegister )
				continue;
			if( store->tokenIndex >= indexedTokens.size() )
				continue;
			if( suffixStoreHasReadWriteMemoryStreamConflict( *store,
			                                                 opportunity,
			                                                 indexedTokens ) )
			{
				readWriteMemoryStreamConflict = true;
				continue;
			}
			if( indexedTokens[store->tokenIndex]->flags() & (Token::PREORDERED | Token::E | Token::D
			                                                | Token::T | Token::BRANCH_DELAY_FILLER) )
				continue;

			if( tokenRangeHasMemoryDependencyWithStore( *store,
			                                           store->tokenIndex + 1,
			                                           opportunity.branchTokenIndex,
			                                           indexedTokens,
			                                           opportunity.softwarePipelineSuffixStores ) )
				continue;

			std::list<std::string> valueKeys;
			collectSuffixStoreValueKeys( *store, valueKeys );
			const bool valueWrittenAfterStore =
			    store->tokenIndex + 1 <= opportunity.branchTokenIndex
			    && tokenRangeWritesAny( store->tokenIndex + 1,
			                            opportunity.branchTokenIndex,
			                            indexedTokens,
			                            valueKeys );
			const bool cyclicPrefixClobbersValue =
			    suffixStoreNeedsValueRotation( *store, cyclicPrefixWrites );
			if( cyclicPrefixClobbersValue
			    && store->tokenIndex >= opportunity.multiQCyclicPrefixInsertBeforeTokenIndex )
				continue;
			if( valueWrittenAfterStore || cyclicPrefixClobbersValue )
			{
				if( !store->storedValueIsFloatRegister )
					continue;
				store->requiresValueRotation = true;
				store->rotateValueAtStore = true;
				if( !assignOneSuffixStoreValueScratchRegister( *store, used ) )
					continue;
			}
			store->delayedDrain = true;
		}

		bool hasDelayedStore = false;
		unsigned int delayedStoreCount = 0;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( store->delayedDrain )
			{
				hasDelayedStore = true;
				++delayedStoreCount;
			}
		}
		if( hasDelayedStore
		    && delayedStoreCount > 1
		    && opportunity.memoryLoadCount > 0
		    && opportunity.qProducerTokenIndices.size() > 1 )
		{
			addSuffixStoreDrainBlocker( opportunity, "multi_store_loaded_suffix_stream" );
			addMultiQPipelineBlocker( opportunity, "multi_store_loaded_suffix_stream" );
			opportunity.eligibleMultiQSoftwarePipeline = false;
			opportunity.canEmitMultiQSoftwarePipeline = false;
			clearSuffixStoreDrainSelection( opportunity.softwarePipelineSuffixStores );
			hasDelayedStore = false;
		}
		if( hasDelayedStore
		    && opportunity.memoryLoadCount > 0
		    && opportunity.qProducerTokenIndices.size() > 1
		    && loadedMultiQDrainLeavesPartialStoreStream( opportunity.softwarePipelineSuffixStores ) )
		{
			addSuffixStoreDrainBlocker( opportunity, "partial_loaded_suffix_store_stream" );
			addMultiQPipelineBlocker( opportunity, "partial_loaded_suffix_store_stream" );
			opportunity.eligibleMultiQSoftwarePipeline = false;
			opportunity.canEmitMultiQSoftwarePipeline = false;
			clearSuffixStoreDrainSelection( opportunity.softwarePipelineSuffixStores );
			hasDelayedStore = false;
		}
		if( !hasDelayedStore && readWriteMemoryStreamConflict )
		{
			addSuffixStoreDrainBlocker( opportunity, "read_write_memory_stream_conflict" );
			if( opportunity.memoryLoadCount > 0 && opportunity.qProducerTokenIndices.size() > 1 )
			{
				addMultiQPipelineBlocker( opportunity, "read_write_memory_stream_conflict" );
				opportunity.eligibleMultiQSoftwarePipeline = false;
				opportunity.canEmitMultiQSoftwarePipeline = false;
			}
		}
		if( hasDelayedStore && opportunity.suffixStoreDrainBlockers.empty() )
			opportunity.canEmitSuffixStoreDrain = true;
	}

	bool tokenHasGuardableMultiQCyclicPrefixSideEffect( const Token& token )
	{
		if( token.flags() & (Token::PREORDERED | Token::BRANCH_DELAY_FILLER
		                   | Token::E | Token::D | Token::T) )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.branchDelaySlots > 0 )
			return false;
		if( access.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;
		return access.memoryKind == VU_MEMORY_STORE;
	}

	bool tokenHasUnsafeMultiQCyclicPrefixSideEffect( const Token& token,
	                                                bool allowGuardedPlainStores )
	{
		if( token.flags() & (Token::PREORDERED | Token::BRANCH_DELAY_FILLER) )
			return true;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return true;
		if( access.branchDelaySlots > 0 )
			return true;
		if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
			return true;
		if( allowGuardedPlainStores
		    && access.memoryKind == VU_MEMORY_STORE
		    && tokenHasGuardableMultiQCyclicPrefixSideEffect( token ) )
			return false;
		return access.memoryKind == VU_MEMORY_STORE
		    || access.memoryKind == VU_MEMORY_XGKICK;
	}

	bool tokenIndicesHaveUnsafeMultiQCyclicPrefixSideEffects( const std::vector<unsigned int>& tokenIndices,
	                                                          const std::vector<const Token*>& indexedTokens,
	                                                          bool allowGuardedPlainStores = false )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size()
			    || tokenHasUnsafeMultiQCyclicPrefixSideEffect( *indexedTokens[*i],
			                                                  allowGuardedPlainStores ) )
				return true;
		}
		return false;
	}

	bool tokenIndicesHaveGuardableMultiQCyclicPrefixSideEffects( const std::vector<unsigned int>& tokenIndices,
	                                                             const std::vector<const Token*>& indexedTokens )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return false;
			if( tokenHasGuardableMultiQCyclicPrefixSideEffect( *indexedTokens[*i] ) )
				return true;
		}
		return false;
	}

	bool tokenIndicesContainVisibleLabel( const std::vector<unsigned int>& tokenIndices,
	                                      const std::vector<const Token*>& indexedTokens,
	                                      unsigned int allowedLabelTokenIndex )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			if( *i != allowedLabelTokenIndex && indexedTokens[*i]->label().length() != 0 )
				return true;
		}
		return false;
	}

	bool loopBodyContainsVisibleInternalLabel( const VuLoopPipelineOpportunity& opportunity,
	                                           const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.labelTokenIndex >= indexedTokens.size()
		    || opportunity.branchTokenIndex >= indexedTokens.size()
		    || opportunity.labelTokenIndex + 1 >= opportunity.branchTokenIndex )
			return false;
		for( unsigned int i = opportunity.labelTokenIndex + 1; i < opportunity.branchTokenIndex; ++i )
		{
			if( indexedTokens[i]->label().length() != 0 )
				return true;
		}
		return false;
	}

	bool cyclicPrefixClobbersBranch( const VuLoopPipelineOpportunity& opportunity,
	                                 const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.branchTokenIndex >= indexedTokens.size() )
			return true;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( *indexedTokens[opportunity.branchTokenIndex], branchAccess ) )
			return true;

		std::list<std::string> prefixWrites;
		unsigned int prefixImplicitWrites = VU_RESOURCE_NONE;
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return true;
			for( std::list<std::string>::const_iterator write = access.registerWrites.begin();
			     write != access.registerWrites.end(); ++write )
				addUniqueString( prefixWrites, *write );
			prefixImplicitWrites |= access.implicitWrites;
		}

		if( intersects( prefixWrites, branchAccess.registerReads )
		    || intersects( prefixWrites, branchAccess.registerWrites ) )
			return true;
		return (prefixImplicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites)) != 0;
	}

	bool cyclicPrefixReadsSuffixClobbers( const VuLoopPipelineOpportunity& opportunity,
	                                      const std::vector<const Token*>& indexedTokens );
	bool cyclicPrefixReadsSuffixClobbersBeforeInsertion( const VuLoopPipelineOpportunity& opportunity,
	                                                     const std::vector<const Token*>& indexedTokens );
	Token tokenWithoutLabel( const Token& token );
	bool setIndirectMemoryOffset( Token& token, const std::string& baseRegister, long offset );
	void rewriteRotatedRegistersToScratch( Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations );
	Token makeRotationMoveToken( const Token& donor,
	                             const VuSoftwarePipelineRotation& rotation );

	bool loopUsesLoopExtraDirective( const VuLoopCandidate& loop,
	                                 const std::vector<const Token*>& indexedTokens )
	{
		if( loop.labelTokenIndex >= indexedTokens.size()
		    || loop.branchTokenIndex >= indexedTokens.size()
		    || loop.labelTokenIndex + 1 >= loop.branchTokenIndex )
			return false;
		for( unsigned int i = loop.labelTokenIndex + 1; i < loop.branchTokenIndex; ++i )
		{
			if( i < indexedTokens.size() && tokenIsLoopExtraDirective( *indexedTokens[i] ) )
				return true;
		}
		return false;
	}

	void adjustMultiQCyclicPrefixInductionOffsets( Token& token,
	                                               const std::vector<VuLoopInductionUpdate>& inductionUpdates,
	                                               unsigned int insertionTokenIndex )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access )
		    || access.memoryKind == VU_MEMORY_NONE
		    || !access.hasMemoryBase
		    || !access.hasMemoryOffset )
			return;

		for( std::vector<VuLoopInductionUpdate>::const_iterator i = inductionUpdates.begin();
		     i != inductionUpdates.end(); ++i )
		{
			if( !i->stepKnown
			    || i->registerName != access.memoryBaseRegister
			    || i->tokenIndex < insertionTokenIndex )
				continue;
			setIndirectMemoryOffset( token,
			                         i->registerName,
			                         access.memoryOffset + i->step );
			return;
		}
	}

	Token adjustedMultiQCyclicPrefixToken( const Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                       const std::vector<VuLoopInductionUpdate>& inductionUpdates,
	                                       unsigned int insertionTokenIndex )
	{
		Token copy = tokenWithoutLabel( token );
		adjustMultiQCyclicPrefixInductionOffsets( copy,
		                                          inductionUpdates,
		                                          insertionTokenIndex );
		rewriteRotatedRegistersToScratch( copy, rotations );
		return copy;
	}

	Token adjustedMultiQCyclicPrefixToken( const Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		return adjustedMultiQCyclicPrefixToken( token,
		                                       rotations,
		                                       std::vector<VuLoopInductionUpdate>(),
		                                       VU_SCHEDULED_TOKEN_INDEX_NONE );
	}

	void collectFloatWriteRotationFields( const Token& token,
	                                      std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER
			    || !(i->flags() & Token::Argument::WRITE)
			    || (i->flags() & Token::Argument::INDIRECT) )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) )
				continue;
			const unsigned int fields = vuWriteFieldMask( token, *i );
			if( fields & Token::X ) addRotationField( rotations, registerBaseKey( key ) + ".x", false );
			if( fields & Token::Y ) addRotationField( rotations, registerBaseKey( key ) + ".y", false );
			if( fields & Token::Z ) addRotationField( rotations, registerBaseKey( key ) + ".z", false );
			if( fields & Token::W ) addRotationField( rotations, registerBaseKey( key ) + ".w", false );
		}
	}

	bool prefixRotationsHaveNoReadBeforeWrite( const std::vector<unsigned int>& tokenIndices,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		std::list<std::string> producedBases;
		for( std::vector<unsigned int>::const_iterator tokenIndex = tokenIndices.begin();
		     tokenIndex != tokenIndices.end(); ++tokenIndex )
		{
			if( *tokenIndex >= indexedTokens.size() )
				return false;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*tokenIndex], access ) )
				return false;

			for( std::list<std::string>::const_iterator read = access.registerReads.begin();
			     read != access.registerReads.end(); ++read )
			{
				const std::string base = registerBaseKey( *read );
				if( findRotationForBase( rotations, base )
				    && !containsKey( producedBases, base ) )
					return false;
			}

			for( std::list<std::string>::const_iterator write = access.registerWrites.begin();
			     write != access.registerWrites.end(); ++write )
			{
				const std::string base = registerBaseKey( *write );
				if( findRotationForBase( rotations, base ) )
					addUniqueString( producedBases, base );
			}
		}

		return true;
	}

	bool assignMultiQCyclicPrefixRotations( const VuLoopCandidate& loop,
	                                        const std::vector<const Token*>& indexedTokens,
	                                        VuLoopPipelineOpportunity& opportunity )
	{
		opportunity.multiQCyclicPrefixRotations.clear();
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				collectFloatWriteRotationFields( *indexedTokens[*i],
				                                  opportunity.multiQCyclicPrefixRotations );
		}

		if( opportunity.multiQCyclicPrefixRotations.empty() )
			return false;
		if( !prefixRotationsHaveNoReadBeforeWrite( opportunity.multiQCyclicPrefixTokenIndices,
		                                           indexedTokens,
		                                           opportunity.multiQCyclicPrefixRotations ) )
		{
			opportunity.multiQCyclicPrefixRotations.clear();
			return false;
		}

		assignRotationScratchRegisters( loop, opportunity.multiQCyclicPrefixRotations );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i =
		         opportunity.multiQCyclicPrefixRotations.begin();
		     i != opportunity.multiQCyclicPrefixRotations.end(); ++i )
		{
			if( !i->hasScratchRegister )
			{
				opportunity.multiQCyclicPrefixRotations.clear();
				return false;
			}
		}
		return true;
	}

	void assignMultiQPipelineCandidate( const VuLoopCandidate& loop,
	                                    unsigned int prefixEndOffset,
	                                    unsigned int mainBeginOffset,
	                                    unsigned int branchOffset,
	                                    VuLoopPipelineOpportunity& opportunity )
	{
		opportunity.multiQPrologTokenIndices.clear();
		opportunity.multiQMainTokenIndices.clear();
		opportunity.multiQCyclicPrefixTokenIndices.clear();
		opportunity.multiQCyclicPrefixRotations.clear();
		opportunity.multiQCyclicPrefixNeedsGuard = false;
		opportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
		appendPipelineInstructionIndices( loop,
		                                  0,
		                                  prefixEndOffset,
		                                  opportunity.multiQPrologTokenIndices );
		opportunity.multiQCyclicPrefixTokenIndices = opportunity.multiQPrologTokenIndices;
		appendPipelineInstructionIndices( loop,
		                                  mainBeginOffset,
		                                  branchOffset + 1,
		                                  opportunity.multiQMainTokenIndices );
		opportunity.multiQCyclicPrefixInsertBeforeTokenIndex =
		    loop.firstBodyTokenIndex + branchOffset;
	}

	bool multiQPipelineCandidateStructurallySafe( const VuLoopPipelineOpportunity& candidate,
	                                              const std::vector<const Token*>& indexedTokens,
	                                              bool allowGuardedPlainStores = false,
	                                              bool checkSuffixClobbers = true )
	{
		if( candidate.multiQPrologTokenIndices.empty()
		    || candidate.multiQMainTokenIndices.empty()
		    || candidate.multiQCyclicPrefixTokenIndices.empty() )
			return false;
		if( tokenIndicesHaveUnsafeMultiQCyclicPrefixSideEffects( candidate.multiQCyclicPrefixTokenIndices,
		                                                         indexedTokens,
		                                                         allowGuardedPlainStores ) )
			return false;
		if( tokenIndicesContainVisibleLabel( candidate.multiQCyclicPrefixTokenIndices,
		                                     indexedTokens,
		                                     candidate.labelTokenIndex )
		    || tokenIndicesContainVisibleLabel( candidate.multiQMainTokenIndices,
		                                        indexedTokens,
		                                        candidate.labelTokenIndex )
		    || loopBodyContainsVisibleInternalLabel( candidate, indexedTokens ) )
			return false;
		if( checkSuffixClobbers
		    && cyclicPrefixReadsSuffixClobbers( candidate, indexedTokens ) )
			return false;
		if( cyclicPrefixClobbersBranch( candidate, indexedTokens ) )
			return false;
		return true;
	}

	void appendUnlabeledTokenForScheduleCost( std::list<Token>& tokens, const Token& token )
	{
		Token copy( token );
		copy.setLabel( "" );
		tokens.push_back( copy );
	}

	void appendMultiQCyclicPrefixForScheduleCost( std::list<Token>& tokens,
	                                              const VuLoopPipelineOpportunity& candidate,
	                                              const std::vector<const Token*>& indexedTokens )
	{
		for( std::vector<unsigned int>::const_iterator i = candidate.multiQCyclicPrefixTokenIndices.begin();
		     i != candidate.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				tokens.push_back( adjustedMultiQCyclicPrefixToken( *indexedTokens[*i],
				                                                   candidate.multiQCyclicPrefixRotations,
				                                                   candidate.inductionUpdates,
				                                                   candidate.multiQCyclicPrefixInsertBeforeTokenIndex ) );
		}
	}

	void appendMultiQRotationMovesForScheduleCost( std::list<Token>& tokens,
	                                               const VuLoopPipelineOpportunity& candidate,
	                                               const std::vector<const Token*>& indexedTokens )
	{
		if( candidate.multiQCyclicPrefixRotations.empty() )
			return;
		const Token* donor = NULL;
		if( !candidate.multiQMainTokenIndices.empty()
		    && candidate.multiQMainTokenIndices.front() < indexedTokens.size() )
			donor = indexedTokens[candidate.multiQMainTokenIndices.front()];
		else if( !candidate.multiQCyclicPrefixTokenIndices.empty()
		         && candidate.multiQCyclicPrefixTokenIndices.front() < indexedTokens.size() )
			donor = indexedTokens[candidate.multiQCyclicPrefixTokenIndices.front()];
		if( !donor )
			return;
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i =
		         candidate.multiQCyclicPrefixRotations.begin();
		     i != candidate.multiQCyclicPrefixRotations.end(); ++i )
			tokens.push_back( makeRotationMoveToken( *donor, *i ) );
	}

	bool cyclicPrefixCanCrossAdjustedInductionUpdate( const Token& adjustedPrefix,
	                                                  const Token& crossed,
	                                                  unsigned int crossedTokenIndex,
	                                                  const std::vector<VuLoopInductionUpdate>& inductionUpdates )
	{
		const VuLoopInductionUpdate* crossedUpdate = NULL;
		for( std::vector<VuLoopInductionUpdate>::const_iterator i = inductionUpdates.begin();
		     i != inductionUpdates.end(); ++i )
		{
			if( i->tokenIndex == crossedTokenIndex && i->stepKnown )
			{
				crossedUpdate = &*i;
				break;
			}
		}
		if( !crossedUpdate )
			return false;

		VuTokenResourceAccess movedAccess;
		VuTokenResourceAccess crossedAccess;
		if( !buildVuTokenResourceAccess( adjustedPrefix, movedAccess )
		    || !buildVuTokenResourceAccess( crossed, crossedAccess ) )
			return false;
		if( movedAccess.memoryKind == VU_MEMORY_NONE
		    || movedAccess.memoryFlags != VU_MEMORY_FLAG_NONE
		    || !movedAccess.hasMemoryBase
		    || movedAccess.memoryBaseRegister != crossedUpdate->registerName )
			return false;
		if( crossedAccess.memoryKind != VU_MEMORY_NONE
		    || crossedAccess.implicitReads != VU_RESOURCE_NONE
		    || crossedAccess.implicitWrites != VU_RESOURCE_NONE )
			return false;
		if( !containsKey( crossedAccess.registerWrites, crossedUpdate->registerName ) )
			return false;
		if( containsKey( movedAccess.registerWrites, crossedUpdate->registerName ) )
			return false;

		std::list<std::string> movedReads = movedAccess.registerReads;
		std::list<std::string> crossedWrites = crossedAccess.registerWrites;
		movedReads.remove( crossedUpdate->registerName );
		crossedWrites.remove( crossedUpdate->registerName );

		if( intersects( movedAccess.registerWrites, crossedAccess.registerReads )
		    || intersects( movedAccess.registerWrites, crossedWrites ) )
			return false;
		if( intersects( crossedWrites, movedReads )
		    || intersects( crossedWrites, movedAccess.registerWrites ) )
			return false;
		return true;
	}

	bool cyclicPrefixTokenCanFillBranchDelaySlot( const Token& token, const Token& branch )
	{
		if( token.label().length() != 0 )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T
		                   | Token::BRANCH_DELAY_FILLER) )
			return false;
		if( !isVuEmittableInstruction( token ) )
			return false;
		if( vuTokenBranchDelaySlots( token ) > 0 )
			return false;
		if( vuTokenBranchDelaySlots( branch ) != 1 )
			return false;

		VuTokenResourceAccess tokenAccess;
		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( token, tokenAccess )
		    || !buildVuTokenResourceAccess( branch, branchAccess ) )
			return false;
		if( tokenAccess.branchDelaySlots > 0 )
			return false;
		if( tokenAccess.instructionFlags & (VU_INSTR_BRANCH
		                                  | VU_INSTR_WAIT_Q
		                                  | VU_INSTR_WAIT_P
		                                  | VU_INSTR_XGKICK) )
			return false;
		if( tokenAccess.implicitReads & (VU_RESOURCE_Q | VU_RESOURCE_P) )
			return false;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			return false;
		if( tokenAccess.memoryKind != VU_MEMORY_NONE
		    || tokenAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;

		if( intersects( tokenAccess.registerWrites, branchAccess.registerReads )
		    || intersects( tokenAccess.registerWrites, branchAccess.registerWrites )
		    || intersects( tokenAccess.registerReads, branchAccess.registerWrites ) )
			return false;
		if( tokenAccess.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
			return false;
		if( branchAccess.implicitWrites & (tokenAccess.implicitReads | tokenAccess.implicitWrites) )
			return false;
		return true;
	}

	bool cyclicPrefixLastTokenCanMoveToBranchDelaySlot( const VuLoopPipelineOpportunity& candidate,
	                                                    const std::vector<const Token*>& indexedTokens )
	{
		if( candidate.multiQCyclicPrefixNeedsGuard
		    || candidate.multiQCyclicPrefixTokenIndices.empty()
		    || candidate.branchTokenIndex >= indexedTokens.size() )
			return false;

		const unsigned int delayedTokenIndex = candidate.multiQCyclicPrefixTokenIndices.back();
		if( delayedTokenIndex >= indexedTokens.size() )
			return false;

		const Token delayedToken =
		    adjustedMultiQCyclicPrefixToken( *indexedTokens[delayedTokenIndex],
		                                     candidate.multiQCyclicPrefixRotations,
		                                     candidate.inductionUpdates,
		                                     candidate.multiQCyclicPrefixInsertBeforeTokenIndex );
		if( !cyclicPrefixTokenCanFillBranchDelaySlot( delayedToken,
		                                              *indexedTokens[candidate.branchTokenIndex] ) )
			return false;

		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( *i < candidate.multiQCyclicPrefixInsertBeforeTokenIndex
			    || *i >= candidate.branchTokenIndex
			    || *i >= indexedTokens.size() )
				continue;
			if( !vuTokenCanMoveBefore( *indexedTokens[*i], delayedToken ) )
				return false;
		}

		return true;
	}

	bool multiQPipelinePrefixCanMoveBeforeTail( const VuLoopPipelineOpportunity& candidate,
	                                            const std::vector<const Token*>& indexedTokens,
	                                            unsigned int insertionTokenIndex )
	{
		for( std::vector<unsigned int>::const_iterator crossed = candidate.multiQMainTokenIndices.begin();
		     crossed != candidate.multiQMainTokenIndices.end(); ++crossed )
		{
			if( *crossed < insertionTokenIndex || *crossed == candidate.branchTokenIndex )
				continue;
			if( *crossed >= indexedTokens.size() )
				return false;
			for( std::vector<unsigned int>::const_iterator prefix = candidate.multiQCyclicPrefixTokenIndices.begin();
			     prefix != candidate.multiQCyclicPrefixTokenIndices.end(); ++prefix )
			{
				if( *prefix >= indexedTokens.size() )
					return false;
				const Token adjustedPrefix =
				    adjustedMultiQCyclicPrefixToken( *indexedTokens[*prefix],
				                                     candidate.multiQCyclicPrefixRotations,
				                                     candidate.inductionUpdates,
				                                     insertionTokenIndex );
				if( !vuTokenCanMoveBefore( adjustedPrefix, *indexedTokens[*crossed] )
				    && !cyclicPrefixCanCrossAdjustedInductionUpdate( adjustedPrefix,
				                                                     *indexedTokens[*crossed],
				                                                     *crossed,
				                                                     candidate.inductionUpdates ) )
					return false;
			}
		}
		return true;
	}

	void setMultiQPipelineCandidateInsertionPoint( VuLoopPipelineOpportunity& candidate,
	                                              const std::vector<const Token*>& indexedTokens,
	                                              unsigned int insertionTokenIndex )
	{
		if( insertionTokenIndex == candidate.branchTokenIndex
		    || multiQPipelinePrefixCanMoveBeforeTail( candidate,
		                                             indexedTokens,
		                                             insertionTokenIndex ) )
			candidate.multiQCyclicPrefixInsertBeforeTokenIndex = insertionTokenIndex;
	}

	bool branchConditionReadyAtInsertionPoint( const VuLoopPipelineOpportunity& candidate,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           unsigned int insertionTokenIndex )
	{
		if( insertionTokenIndex == candidate.branchTokenIndex )
			return true;
		if( candidate.branchTokenIndex >= indexedTokens.size() )
			return false;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( *indexedTokens[candidate.branchTokenIndex], branchAccess ) )
			return false;

		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( *i < insertionTokenIndex
			    || *i >= candidate.branchTokenIndex
			    || *i >= indexedTokens.size() )
				continue;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return false;
			if( intersects( access.registerWrites, branchAccess.registerReads )
			    || intersects( access.registerWrites, branchAccess.registerWrites ) )
				return false;
			if( access.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
				return false;
		}
		return true;
	}

	void assignMultiQGuardDrainTokenIndices( VuLoopPipelineOpportunity& candidate )
	{
		candidate.drainTokenIndices.clear();
		if( !candidate.multiQCyclicPrefixNeedsGuard )
			return;
		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( *i >= candidate.multiQCyclicPrefixInsertBeforeTokenIndex
			    && *i < candidate.branchTokenIndex )
				candidate.drainTokenIndices.push_back( *i );
		}
	}

	bool tokenIndicesTouchImplicitResources( const std::vector<unsigned int>& tokenIndices,
	                                         const std::vector<const Token*>& indexedTokens,
	                                         unsigned int resources )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return true;
			if( (access.implicitReads | access.implicitWrites) & resources )
				return true;
		}
		return false;
	}

	unsigned int multiQPipelineCandidateMainCycles( const VuLoopPipelineOpportunity& candidate,
	                                                const std::vector<const Token*>& indexedTokens )
	{
		std::list<Token> mainTokens;
		appendMultiQRotationMovesForScheduleCost( mainTokens, candidate, indexedTokens );
		bool emittedPrefix = false;
		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( !emittedPrefix && *i == candidate.multiQCyclicPrefixInsertBeforeTokenIndex )
			{
				if( candidate.multiQCyclicPrefixNeedsGuard
				    && candidate.branchTokenIndex < indexedTokens.size() )
					mainTokens.push_back( invertedBranchToDrainToken( *indexedTokens[candidate.branchTokenIndex],
					                                                  candidate.label + "__DRAIN" ) );
				appendMultiQCyclicPrefixForScheduleCost( mainTokens, candidate, indexedTokens );
				emittedPrefix = true;
			}
			if( *i < indexedTokens.size() && *i != candidate.branchTokenIndex )
				appendUnlabeledTokenForScheduleCost( mainTokens, *indexedTokens[*i] );
		}
		if( !emittedPrefix )
			appendMultiQCyclicPrefixForScheduleCost( mainTokens, candidate, indexedTokens );
		if( candidate.branchTokenIndex < indexedTokens.size() )
			appendUnlabeledTokenForScheduleCost( mainTokens, *indexedTokens[candidate.branchTokenIndex] );
		const unsigned int cycles = scheduleVuProgramReadyIssueSlotsWithFlagLiveness( mainTokens ).cycleCount;
		return candidate.multiQCyclicPrefixLastTokenInBranchDelaySlot && cycles > 0
		     ? cycles - 1
		     : cycles;
	}

	bool considerMultiQPipelineCandidate( const VuLoopCandidate& loop,
	                                      const std::vector<const Token*>& indexedTokens,
	                                      const VuLoopPipelineOpportunity& baseOpportunity,
	                                      unsigned int prefixEndOffset,
	                                      unsigned int mainBeginOffset,
	                                      unsigned int branchOffset,
	                                      bool allowGuardedPlainStores,
	                                      unsigned int& bestMainCycles,
	                                      VuLoopPipelineOpportunity& bestOpportunity )
	{
		VuLoopPipelineOpportunity candidate = baseOpportunity;
		assignMultiQPipelineCandidate( loop,
		                               prefixEndOffset,
		                               mainBeginOffset,
		                               branchOffset,
		                               candidate );
		std::vector<VuLoopPipelineOpportunity> candidates;
		candidates.push_back( candidate );
		VuLoopPipelineOpportunity rotatedCandidate = candidate;
		if( assignMultiQCyclicPrefixRotations( loop, indexedTokens, rotatedCandidate ) )
			candidates.push_back( rotatedCandidate );

		bool found = false;
		for( std::vector<VuLoopPipelineOpportunity>::const_iterator candidateIt = candidates.begin();
		     candidateIt != candidates.end(); ++candidateIt )
		{
			const bool needsGuard =
			    allowGuardedPlainStores
			    && tokenIndicesHaveGuardableMultiQCyclicPrefixSideEffects( candidateIt->multiQCyclicPrefixTokenIndices,
			                                                               indexedTokens );
			if( needsGuard
			    && (candidateIt->branchTokenIndex >= indexedTokens.size()
			        || !branchCanInvertToDrain( *indexedTokens[candidateIt->branchTokenIndex] )) )
				continue;
			if( !multiQPipelineCandidateStructurallySafe( *candidateIt,
			                                              indexedTokens,
			                                              needsGuard,
			                                              false ) )
				continue;
			if( needsGuard )
			{
				const unsigned int kMaxGuardedSplitPrefixTokens = 32;
				const bool prefixTouchesLongLatencyResult =
				    tokenIndicesTouchImplicitResources( candidateIt->multiQCyclicPrefixTokenIndices,
				                                        indexedTokens,
				                                        VU_RESOURCE_Q | VU_RESOURCE_P );
				const unsigned int firstLegalInsertionTokenIndex =
				    prefixTouchesLongLatencyResult
				    ? candidateIt->lastQConsumerTokenIndex + 1
				    : (candidateIt->multiQMainTokenIndices.empty()
				       ? candidateIt->lastQConsumerTokenIndex + 1
				       : candidateIt->multiQMainTokenIndices.front());

				std::vector<unsigned int> insertionCandidates;
				if( candidateIt->multiQCyclicPrefixTokenIndices.size() <= kMaxGuardedSplitPrefixTokens )
				{
					for( std::vector<unsigned int>::const_iterator i = candidateIt->multiQMainTokenIndices.begin();
					     i != candidateIt->multiQMainTokenIndices.end(); ++i )
					{
						if( *i < firstLegalInsertionTokenIndex || *i > candidateIt->branchTokenIndex )
							continue;
						VuLoopPipelineOpportunity insertedCandidate = *candidateIt;
						insertedCandidate.multiQCyclicPrefixNeedsGuard = true;
						insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
						setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
						if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
							continue;
						if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate,
						                                                    indexedTokens ) )
							continue;
						if( !branchConditionReadyAtInsertionPoint( insertedCandidate, indexedTokens, *i ) )
							continue;
						insertionCandidates.push_back( *i );
						break;
					}
				}
				if( insertionCandidates.empty()
				    || insertionCandidates.back() != candidateIt->branchTokenIndex )
					insertionCandidates.push_back( candidateIt->branchTokenIndex );

				for( std::vector<unsigned int>::const_iterator i = insertionCandidates.begin();
				     i != insertionCandidates.end(); ++i )
				{
					VuLoopPipelineOpportunity insertedCandidate = *candidateIt;
					insertedCandidate.multiQCyclicPrefixNeedsGuard = true;
					insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
					setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
					if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
						continue;
					if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate,
					                                                    indexedTokens ) )
						continue;
					assignMultiQGuardDrainTokenIndices( insertedCandidate );
					const unsigned int cycles = multiQPipelineCandidateMainCycles( insertedCandidate, indexedTokens );
					const bool preferEarlyGuardedDrain =
					    !insertedCandidate.drainTokenIndices.empty()
					    && candidateIt->multiQMainTokenIndices.size() <= 32;
					const bool keepEarlyGuardedDrain =
					    insertedCandidate.drainTokenIndices.empty()
					    && bestOpportunity.multiQCyclicPrefixNeedsGuard
					    && !bestOpportunity.drainTokenIndices.empty()
					    && candidateIt->multiQMainTokenIndices.size() <= 32;
					if( (cycles < bestMainCycles && !keepEarlyGuardedDrain)
					    || preferEarlyGuardedDrain )
					{
						bestMainCycles = cycles;
						bestOpportunity = insertedCandidate;
					}
					found = true;
				}
				continue;
			}
			const bool prefixTouchesLongLatencyResult =
			    tokenIndicesTouchImplicitResources( candidateIt->multiQCyclicPrefixTokenIndices,
			                                        indexedTokens,
			                                        VU_RESOURCE_Q | VU_RESOURCE_P );
			const unsigned int firstLegalInsertionTokenIndex =
			    prefixTouchesLongLatencyResult
			    ? candidateIt->lastQConsumerTokenIndex + 1
			    : (candidateIt->multiQMainTokenIndices.empty()
			       ? candidateIt->lastQConsumerTokenIndex + 1
			       : candidateIt->multiQMainTokenIndices.front());
			for( std::vector<unsigned int>::const_iterator i = candidateIt->multiQMainTokenIndices.begin();
			     i != candidateIt->multiQMainTokenIndices.end(); ++i )
			{
				if( *i < firstLegalInsertionTokenIndex || *i > candidateIt->branchTokenIndex )
					continue;
				VuLoopPipelineOpportunity insertedCandidate = *candidateIt;
				setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
				if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
					continue;
				if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate,
				                                                    indexedTokens ) )
					continue;
				insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    cyclicPrefixLastTokenCanMoveToBranchDelaySlot( insertedCandidate, indexedTokens );
				const unsigned int cycles = multiQPipelineCandidateMainCycles( insertedCandidate, indexedTokens );
				if( cycles < bestMainCycles )
				{
					bestMainCycles = cycles;
					bestOpportunity = insertedCandidate;
				}
				found = true;
			}
		}
		return found;
	}

	unsigned int scheduledLoopBodyCycles( const std::vector<unsigned int>& tokenIndices,
	                                      const std::vector<const Token*>& indexedTokens )
	{
		std::list<Token> bodyTokens;
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( bodyTokens, *indexedTokens[*i] );
		}
		return scheduleVuProgramReadyIssueSlotsWithFlagLiveness( bodyTokens ).cycleCount;
	}

	// Diagnostic: schedule prolog ++ main as one segment, return cycles attributed to the
	// main portion (i.e. cycles spent on tokens after the prolog's last issue slot).
	// This approximates how the emitter sees the main body when long-latency results from
	// the prolog (Q, P, FMAC ACC, flag clauses, etc.) are still in flight at main entry.
	unsigned int scheduledLoopBodyCyclesInContext( const std::vector<unsigned int>& prologTokenIndices,
	                                               const std::vector<unsigned int>& mainTokenIndices,
	                                               const std::vector<const Token*>& indexedTokens )
	{
		std::list<Token> combinedTokens;
		for( std::vector<unsigned int>::const_iterator i = prologTokenIndices.begin();
		     i != prologTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( combinedTokens, *indexedTokens[*i] );
		}
		const unsigned int prologCycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( combinedTokens ).cycleCount;
		for( std::vector<unsigned int>::const_iterator i = mainTokenIndices.begin();
		     i != mainTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( combinedTokens, *indexedTokens[*i] );
		}
		const unsigned int totalCycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( combinedTokens ).cycleCount;
		return totalCycles >= prologCycles ? totalCycles - prologCycles : 0;
	}

	bool cyclicPrefixMainStartsWithStore( const VuLoopPipelineOpportunity& candidate,
	                                      const std::vector<const Token*>& indexedTokens )
	{
		if( candidate.multiQMainTokenIndices.empty() )
			return false;
		const unsigned int tokenIndex = candidate.multiQMainTokenIndices.front();
		if( tokenIndex >= indexedTokens.size() )
			return false;
		VuTokenResourceAccess access;
		return buildVuTokenResourceAccess( *indexedTokens[tokenIndex], access )
		    && access.memoryKind == VU_MEMORY_STORE;
	}

	// Track 9.E step 2 (diagnostic only): estimate the steady-state per-iteration
	// cycle count of a 2-stage software-pipelined kernel built from a single-Q
	// eligible opportunity. Builds a replicated kernel where iteration N+1 uses
	// scratch VF registers in place of the loop-carried Q-output VF registers,
	// breaking the iter->iter WAR/WAW on those carried regs. The Q register and
	// FMAC accumulator deps that the hardware serializes are preserved.
	//
	// Returns false if rotation allocation fails or the opportunity isn't
	// shaped like a single-Q SWP candidate. On success outKernelCycles is the
	// total scheduled cycles of (iter1 ++ iter2_renamed); per-iter cost is
	// outKernelCycles / 2.
	bool synthesizeTwoStageKernelCycles( const VuLoopCandidate& loop,
	                                     const std::vector<const Token*>& indexedTokens,
	                                     const VuLoopPipelineOpportunity& opportunity,
	                                     unsigned int& outKernelCycles,
	                                     std::vector<VuSoftwarePipelineRotation>& outRotations )
	{
		outKernelCycles = 0;
		outRotations.clear();
		if( !opportunity.eligibleSingleQSoftwarePipeline )
			return false;
		if( opportunity.mainTokenIndices.empty() )
			return false;
		if( opportunity.carriedQOutputRegisters.empty() )
			return false;

		std::vector<VuSoftwarePipelineRotation> rotations;
		for( std::list<std::string>::const_iterator reg =
		         opportunity.carriedQOutputRegisters.begin();
		     reg != opportunity.carriedQOutputRegisters.end(); ++reg )
		{
			VuSoftwarePipelineRotation rot;
			rot.registerBase = *reg;
			rot.hasScratchRegister = false;
			rotations.push_back( rot );
		}
		assignRotationScratchRegisters( loop, rotations );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator r = rotations.begin();
		     r != rotations.end(); ++r )
		{
			if( !r->hasScratchRegister )
				return false;
		}

		std::list<Token> kernelTokens;
		for( std::vector<unsigned int>::const_iterator i = opportunity.mainTokenIndices.begin();
		     i != opportunity.mainTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( kernelTokens, *indexedTokens[*i] );
		}
		for( std::vector<unsigned int>::const_iterator i = opportunity.mainTokenIndices.begin();
		     i != opportunity.mainTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				kernelTokens.push_back(
				    adjustedMultiQCyclicPrefixToken( *indexedTokens[*i], rotations ) );
		}

		outKernelCycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( kernelTokens ).cycleCount;
		outRotations.swap( rotations );
		return true;
	}

	// Track 9.E step 3b (diagnostic only): estimate the steady-state per-iter
	// cycle count of a 2-stage software-pipelined kernel that interleaves
	// stage1 of iter N+1 BEFORE stage2 of iter N at the source level, so the
	// downstream scheduler can issue the next div/mulax chain while iter N's
	// mulq->ftoi4 chain is still draining Q. Uses depth-2 rotation banks
	// from step 3a so two iterations can hold renamed VF carriers simultaneously.
	//
	// Source order emitted to the scheduler (3-iter window):
	//   stage1_A_b0, stage1_B_b1, stage2_A_b0, stage1_C_b0, stage2_B_b1, stage2_C_b0
	//                                          |                       |
	//                                      boundary1               boundary2
	// perIter = boundary2 - boundary1 = steady-state cost of one iteration
	// once both rotation banks are in flight.
	//
	// Returns false if the opportunity is not single-Q SWP shaped, if the q
	// producer doesn't live inside mainTokenIndices, or if depth-2 rotation
	// allocation fails. On success outPerIterCycles holds the per-iter estimate
	// and outRotations holds the depth-2 banks used.
	bool synthesizeQInterleavedKernelCycles( const VuLoopCandidate& loop,
	                                         const std::vector<const Token*>& indexedTokens,
	                                         const VuLoopPipelineOpportunity& opportunity,
	                                         unsigned int& outPerIterCycles,
	                                         unsigned int& outStage1Cycles,
	                                         unsigned int& outStage2Cycles,
	                                         std::vector<VuSoftwarePipelineRotation>& outRotations,
	                                         std::string& outFailReason )
	{
		outPerIterCycles = 0;
		outStage1Cycles = 0;
		outStage2Cycles = 0;
		outRotations.clear();
		outFailReason.clear();

		if( !opportunity.eligibleSingleQSoftwarePipeline )
		{
			outFailReason = "not_eligible_single_q_swp";
			return false;
		}
		if( opportunity.mainTokenIndices.empty() )
		{
			outFailReason = "empty_main";
			return false;
		}
		if( opportunity.carriedQOutputRegisters.empty() )
		{
			outFailReason = "no_carried_q_outputs";
			return false;
		}

		// Split mainTokenIndices at the LAST q consumer (mulq): stage1 = tokens
		// up to and including the last q consumer of iter N (the chain that
		// uses Q from iter N-1's hoisted div); stage2 = tokens after (post-mulq
		// chain, e.g. ftoi4/sq/induction). At source-level interleave we want
		// stage1 of iter N+1 (which will be using iter N's pending Q) to be
		// scheduled alongside stage2 of iter N. The hoisted q producer for
		// iter N+1 lives outside mainTokenIndices and so isn't part of either
		// stage here.
		if( opportunity.qConsumerTokenIndices.empty() )
		{
			outFailReason = "no_q_consumers";
			return false;
		}
		const unsigned int qConsumerBoundary = opportunity.qConsumerTokenIndices.back();
		std::vector<unsigned int> stage1;
		std::vector<unsigned int> stage2;
		bool sawBoundary = false;
		for( std::vector<unsigned int>::const_iterator i = opportunity.mainTokenIndices.begin();
		     i != opportunity.mainTokenIndices.end(); ++i )
		{
			if( !sawBoundary )
			{
				stage1.push_back( *i );
				if( *i == qConsumerBoundary )
					sawBoundary = true;
			}
			else
			{
				stage2.push_back( *i );
			}
		}
		if( !sawBoundary || stage1.empty() || stage2.empty() )
		{
			outFailReason = !sawBoundary ? "q_consumer_not_in_main"
			                             : ( stage1.empty() ? "empty_stage1" : "empty_stage2" );
			return false;
		}

		// Allocate depth-2 rotation banks. Collapse carriedQOutputRegisters to
		// their base register key first: rewriteRotatedRegistersToScratch
		// (line ~4288) looks up rotations via findRotationForBase against the
		// base register name (registerBaseKey strips .x/.y/.z). Field-keyed
		// rotations are silently ignored by the rewriter, so we MUST keep
		// registerBase as the bare VF name. This also reduces the depth-2
		// scratch VF demand from N_fields to N_bases (e.g. mulq.xyz VF8 uses
		// 1 base, not 3 fields).
		std::list<std::string> uniqueBases;
		for( std::list<std::string>::const_iterator reg =
		         opportunity.carriedQOutputRegisters.begin();
		     reg != opportunity.carriedQOutputRegisters.end(); ++reg )
			addUniqueString( uniqueBases, registerBaseKey( *reg ) );
		std::vector<VuSoftwarePipelineRotation> banks;
		for( std::list<std::string>::const_iterator base = uniqueBases.begin();
		     base != uniqueBases.end(); ++base )
		{
			VuSoftwarePipelineRotation rot;
			rot.registerBase = *base;
			rot.hasScratchRegister = false;
			banks.push_back( rot );
		}
		assignRotationScratchRegisters( loop, banks, 2 );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator r = banks.begin();
		     r != banks.end(); ++r )
		{
			if( r->rotationBank.size() < 2 )
			{
				outFailReason = "depth2_alloc_failed";
				return false;
			}
		}

		// Build per-bank rotation views. bank0 uses rotationBank[0] (same as
		// hasScratchRegister/scratchRegister); bank1 overrides scratchRegister
		// with rotationBank[1] so rewriteRotatedRegistersToScratch emits the
		// alternate scratch VF.
		std::vector<VuSoftwarePipelineRotation> bank0 = banks;
		std::vector<VuSoftwarePipelineRotation> bank1 = banks;
		for( std::size_t k = 0; k < bank0.size(); ++k )
		{
			bank0[k].scratchRegister = bank0[k].rotationBank[0];
			bank0[k].hasScratchRegister = true;
			bank1[k].scratchRegister = bank1[k].rotationBank[1];
			bank1[k].hasScratchRegister = true;
		}

		// Stage helpers: append the given index list as adjusted tokens under
		// the supplied bank. Source-order interleave is performed by the caller
		// via the order in which these helpers are invoked.
		struct Local
		{
			static void appendStage( std::list<Token>& dst,
			                         const std::vector<unsigned int>& indices,
			                         const std::vector<const Token*>& src,
			                         const std::vector<VuSoftwarePipelineRotation>& rotations )
			{
				for( std::vector<unsigned int>::const_iterator i = indices.begin();
				     i != indices.end(); ++i )
				{
					if( *i < src.size() )
						dst.push_back(
						    adjustedMultiQCyclicPrefixToken( *src[*i], rotations ) );
				}
			}
		};

		// Schedule three prefixes of the interleaved sequence and subtract to
		// extract per-stage and per-iter cycle costs.
		// Sequence: stage1_A_b0, stage1_B_b1, stage2_A_b0, stage1_C_b0, stage2_B_b1, stage2_C_b0
		std::list<Token> seq_b1;
		Local::appendStage( seq_b1, stage1, indexedTokens, bank0 );
		Local::appendStage( seq_b1, stage1, indexedTokens, bank1 );
		Local::appendStage( seq_b1, stage2, indexedTokens, bank0 );
		const unsigned int boundary1 =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_b1 ).cycleCount;

		std::list<Token> seq_b2;
		Local::appendStage( seq_b2, stage1, indexedTokens, bank0 );
		Local::appendStage( seq_b2, stage1, indexedTokens, bank1 );
		Local::appendStage( seq_b2, stage2, indexedTokens, bank0 );
		Local::appendStage( seq_b2, stage1, indexedTokens, bank0 );
		Local::appendStage( seq_b2, stage2, indexedTokens, bank1 );
		const unsigned int boundary2 =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_b2 ).cycleCount;

		// Also measure stage1 and stage2 in isolation (no warmup) for reporting.
		std::list<Token> seq_s1_only;
		Local::appendStage( seq_s1_only, stage1, indexedTokens, bank0 );
		outStage1Cycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_s1_only ).cycleCount;
		std::list<Token> seq_s2_only;
		Local::appendStage( seq_s2_only, stage2, indexedTokens, bank0 );
		outStage2Cycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_s2_only ).cycleCount;

		if( boundary2 <= boundary1 )
		{
			outFailReason = "boundary_non_positive";
			return false;
		}
		outPerIterCycles = boundary2 - boundary1;
		outRotations.swap( banks );
		return true;
	}

	bool loopBodyHasQProducer( const VuLoopCandidate& loop )
	{
		for( std::vector<const Token*>::const_iterator i = loop.bodyTokens.begin();
		     i != loop.bodyTokens.end(); ++i )
		{
			if( vuTokenWritesQ( **i ) )
				return true;
		}
		return false;
	}

	bool buildGenericCyclicPrefixRewritePlan( const VuLoopCandidate& loop,
	                                          const std::vector<const Token*>& indexedTokens,
	                                          VuSoftwarePipelineRewritePlan& plan )
	{
		const bool hasLoopExtra = loopUsesLoopExtraDirective( loop, indexedTokens );
		if( !loop.simpleCountedLoop
		    || loopBodyHasQProducer( loop )
		    || loop.hasMemoryPreOrPostIncrement
		    || loop.hasXgkick
		    || loop.inductionRegisters.empty()
		    || loop.branchTokenIndex >= indexedTokens.size()
		    || loop.firstBodyTokenIndex >= loop.branchTokenIndex )
			return false;
		if( hasLoopExtra && loop.memoryStoreCount > 1 )
			return false;

		VuLoopPipelineOpportunity base;
		base.label = loop.label;
		base.labelTokenIndex = loop.labelTokenIndex;
		base.branchTokenIndex = loop.branchTokenIndex;
		base.branchDelaySlots = loop.branchToken ? vuTokenBranchDelaySlots( *loop.branchToken ) : 0;
		base.loopCsClid = loop.loopCsClid;
		base.loopCsMlid = loop.loopCsMlid;
		base.simpleCountedLoop = loop.simpleCountedLoop;
		base.memoryLoadCount = loop.memoryLoadCount;
		base.memoryStoreCount = loop.memoryStoreCount;
		base.hasMemoryPreOrPostIncrement = loop.hasMemoryPreOrPostIncrement;
		base.hasXgkick = loop.hasXgkick;
		base.inductionRegisters = loop.inductionRegisters;
		base.inductionUpdates = loop.inductionUpdates;
		base.loopReadWriteRegisters = loop.loopReadWriteRegisters;

		if( base.branchDelaySlots == 0
		    || tokenIndicesContainVisibleLabel( std::vector<unsigned int>(),
		                                        indexedTokens,
		                                        base.labelTokenIndex )
		    || loopBodyContainsVisibleInternalLabel( base, indexedTokens ) )
			return false;

		const unsigned int branchOffset = loop.branchTokenIndex - loop.firstBodyTokenIndex;
		std::vector<unsigned int> originalTokenIndices;
		appendPipelineInstructionIndices( loop, 0, branchOffset + 1, originalTokenIndices );
		if( originalTokenIndices.empty() )
			return false;

		unsigned int bestCycles = scheduledLoopBodyCycles( originalTokenIndices, indexedTokens );
		VuLoopPipelineOpportunity bestCandidate;
		bool bestStartsWithStore = false;
		bool found = false;

		const bool dumpRejections =
		    std::getenv( "OPENVCL_DUMP_CYCLIC_PREFIX_REJECTIONS" ) != NULL;
		if( dumpRejections )
		{
			std::cerr << "[cyclic-prefix-trace] loop=" << loop.label
			          << " branchOffset=" << branchOffset
			          << " baselineCycles=" << bestCycles << "\n";
		}

		for( unsigned int splitOffset = 1; splitOffset < branchOffset; ++splitOffset )
		{
			VuLoopPipelineOpportunity candidate = base;
			assignMultiQPipelineCandidate( loop,
			                               splitOffset,
			                               splitOffset,
			                               branchOffset,
			                               candidate );
			if( candidate.multiQPrologTokenIndices.empty()
			    || candidate.multiQMainTokenIndices.empty() )
			{
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " rejected=empty_partition\n";
				continue;
			}
			if( !assignMultiQCyclicPrefixRotations( loop, indexedTokens, candidate ) )
			{
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " rejected=rotation_assign_failed\n";
				continue;
			}
			if( !multiQPipelineCandidateStructurallySafe( candidate, indexedTokens ) )
			{
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " rejected=structurally_unsafe"
					          << " (suffix_clobber_at_branch="
					          << ( cyclicPrefixReadsSuffixClobbers( candidate, indexedTokens ) ? 1 : 0 )
					          << ")\n";
				continue;
			}

			bool evaluatedInsertion = false;
			for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
			     i != candidate.multiQMainTokenIndices.end(); ++i )
			{
				if( *i > candidate.branchTokenIndex )
					continue;
				VuLoopPipelineOpportunity insertedCandidate = candidate;
				setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
				if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
					continue;
				insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    cyclicPrefixLastTokenCanMoveToBranchDelaySlot( insertedCandidate, indexedTokens );
				evaluatedInsertion = true;
				const bool clobbersHere =
				    cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate, indexedTokens );
				const unsigned int cycles = multiQPipelineCandidateMainCycles( insertedCandidate, indexedTokens );
				const bool startsWithStore =
				    hasLoopExtra && cyclicPrefixMainStartsWithStore( insertedCandidate, indexedTokens );
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " insertBefore=" << *i
					          << " cycles=" << cycles
					          << " suffixClobberHere=" << ( clobbersHere ? 1 : 0 )
					          << "\n";
				if( cycles < bestCycles
				    || (startsWithStore && !bestStartsWithStore && cycles == bestCycles) )
				{
					bestCycles = cycles;
					bestCandidate = insertedCandidate;
					bestStartsWithStore = startsWithStore;
					found = true;
				}
			}

			if( !evaluatedInsertion )
			{
				candidate.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    cyclicPrefixLastTokenCanMoveToBranchDelaySlot( candidate, indexedTokens );
				const unsigned int cycles = multiQPipelineCandidateMainCycles( candidate, indexedTokens );
				const bool startsWithStore =
				    hasLoopExtra && cyclicPrefixMainStartsWithStore( candidate, indexedTokens );
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " insertBefore=branch cycles=" << cycles << "\n";
				if( cycles < bestCycles
				    || (startsWithStore && !bestStartsWithStore && cycles == bestCycles) )
				{
					bestCycles = cycles;
					bestCandidate = candidate;
					bestStartsWithStore = startsWithStore;
					found = true;
				}
			}
		}

		if( dumpRejections )
		{
			std::cerr << "[cyclic-prefix-trace] loop=" << loop.label
			          << " result=" << ( found ? "ACCEPTED" : "no_improvement" )
			          << " bestCycles=" << bestCycles << "\n";
		}

		if( !found )
			return false;

		plan.label = loop.label;
		plan.prologLabel = loop.label + "__PROLOG";
		plan.mainLabel = loop.label;
		plan.drainLabel = loop.label + "__DRAIN";
		plan.labelTokenIndex = loop.labelTokenIndex;
		plan.branchTokenIndex = loop.branchTokenIndex;
		plan.qProducerTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
		plan.cyclicPrefixBeforeBranch = true;
		plan.prologTokenIndices = bestCandidate.multiQPrologTokenIndices;
		plan.mainTokenIndices = bestCandidate.multiQMainTokenIndices;
		plan.cyclicPrefixTokenIndices = bestCandidate.multiQCyclicPrefixTokenIndices;
		plan.cyclicPrefixRotations = bestCandidate.multiQCyclicPrefixRotations;
		plan.cyclicPrefixInsertBeforeTokenIndex =
		    bestCandidate.multiQCyclicPrefixInsertBeforeTokenIndex;
		plan.cyclicPrefixNeedsGuard = bestCandidate.multiQCyclicPrefixNeedsGuard;
		plan.cyclicPrefixLastTokenInBranchDelaySlot =
		    bestCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot;
		plan.emitsDrain = bestCandidate.multiQCyclicPrefixNeedsGuard;
		plan.inductionUpdates = bestCandidate.inductionUpdates;
		plan.drainTokenIndices = bestCandidate.drainTokenIndices;
		return true;
	}

	bool cyclicPrefixReadsSuffixClobbers( const VuLoopPipelineOpportunity& opportunity,
	                                      const std::vector<const Token*>& indexedTokens )
	{
		VuLoopPipelineOpportunity branchInsertion = opportunity;
		branchInsertion.multiQCyclicPrefixInsertBeforeTokenIndex =
		    opportunity.branchTokenIndex;
		return cyclicPrefixReadsSuffixClobbersBeforeInsertion( branchInsertion,
		                                                       indexedTokens );
	}

	bool cyclicPrefixReadsSuffixClobbersBeforeInsertion( const VuLoopPipelineOpportunity& opportunity,
	                                                     const std::vector<const Token*>& indexedTokens )
	{
		std::list<std::string> suffixWrites;
		const unsigned int insertionTokenIndex =
		    opportunity.multiQCyclicPrefixInsertBeforeTokenIndex == VU_SCHEDULED_TOKEN_INDEX_NONE
		    ? opportunity.branchTokenIndex
		    : opportunity.multiQCyclicPrefixInsertBeforeTokenIndex;
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQMainTokenIndices.begin();
		     i != opportunity.multiQMainTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size()
			    || *i == opportunity.branchTokenIndex
			    || *i >= insertionTokenIndex )
				continue;
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *indexedTokens[*i], writes );
			for( std::list<std::string>::const_iterator write = writes.begin(); write != writes.end(); ++write )
				addUniqueString( suffixWrites, *write );
		}

		std::list<std::string> prefixWritesSoFar;
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return true;
			for( std::list<std::string>::const_iterator read = access.registerReads.begin();
			     read != access.registerReads.end(); ++read )
			{
				if( !containsKey( suffixWrites, *read )
				    || containsKey( prefixWritesSoFar, *read )
				    || containsKey( opportunity.inductionRegisters, *read ) )
					continue;
				return true;
			}
			for( std::list<std::string>::const_iterator write = access.registerWrites.begin();
			     write != access.registerWrites.end(); ++write )
				addUniqueString( prefixWritesSoFar, *write );
		}

		return false;
	}

	VuLoopQSchedulingStrategy classifyLoopQSchedulingStrategy( unsigned int qProducerConsumerGapDeficitCycles,
	                                                           unsigned int loopCarriedQGapCycles,
	                                                           unsigned int qProducerLatency )
	{
		if( qProducerConsumerGapDeficitCycles == 0 )
			return VU_LOOP_Q_SCHEDULE_LOCAL;
		if( loopCarriedQGapCycles >= qProducerLatency )
			return VU_LOOP_Q_SCHEDULE_LOOP_CARRIED;
		return VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
	}

	VuLoopQSchedulingStrategy classifyLoopQSchedulingStrategy( const VuLoopPipelineOpportunity& opportunity )
	{
		return classifyLoopQSchedulingStrategy( opportunity.qProducerConsumerGapDeficitCycles,
		                                        opportunity.loopCarriedQGapCycles,
		                                        opportunity.qProducerLatency );
	}

	void classifySoftwarePipelineEmissionSafety( VuLoopPipelineOpportunity& opportunity,
	                                             const VuLoopCandidate& loop,
	                                             unsigned int qProducerOffset,
	                                             const std::vector<const Token*>& indexedTokens )
	{
		if( !opportunity.simpleCountedLoop )
			addPipelineBlocker( opportunity, "not_simple_counted_loop" );
		if( !opportunity.hasSingleQProducer )
			addPipelineBlocker( opportunity, "multiple_q_producers" );
		if( opportunity.branchDelaySlots == 0 )
			addPipelineBlocker( opportunity, "missing_branch_delay_slot" );
		if( (opportunity.sourcePrefixCycles + opportunity.sourceSuffixCycles) < opportunity.qProducerLatency )
			addPipelineBlocker( opportunity, "insufficient_independent_cycles" );
		if( opportunity.qSchedulingStrategy == VU_LOOP_Q_SCHEDULE_LOCAL )
			addPipelineBlocker( opportunity, "q_latency_already_local" );
		if( opportunity.qSchedulingStrategy == VU_LOOP_Q_SCHEDULE_INSUFFICIENT )
			addPipelineBlocker( opportunity, "insufficient_loop_carried_q_gap" );
		if( opportunity.qProducerInsertionGapDeficitCycles != 0 )
			addPipelineBlocker( opportunity, "insufficient_q_insertion_gap" );
		if( opportunity.hasMemoryPreOrPostIncrement )
			addPipelineBlocker( opportunity, "pre_or_post_increment_memory" );
		if( opportunity.hasXgkick )
			addPipelineBlocker( opportunity, "xgkick_barrier" );
		if( opportunity.inductionRegisters.empty() )
			addPipelineBlocker( opportunity, "missing_induction_register" );
		collectRotatedRegisterBaseKeys( opportunity.carriedQInputRegisters,
		                                opportunity.softwarePipelineRotatedRegisters );
		collectRotatedRegisterBaseKeys( opportunity.carriedQOutputRegisters,
		                                opportunity.softwarePipelineRotatedRegisters );
		collectRotationDescriptors( opportunity.carriedQInputRegisters,
		                            true,
		                            opportunity.softwarePipelineRotations );
		collectRotationDescriptors( opportunity.carriedQOutputRegisters,
		                            false,
		                            opportunity.softwarePipelineRotations );
		collectSoftwarePipelinePrefetchDescriptors( opportunity.prologTokenIndices,
		                                            opportunity.qProducerTokenIndex,
		                                            indexedTokens,
		                                            opportunity );
		retainPrefetchedRotations( opportunity, indexedTokens );
		assignRotationScratchRegisters( loop, opportunity.softwarePipelineRotations );

		const bool canEmitRotations = softwarePipelineRotationsCanEmit( opportunity, indexedTokens );
		if( !opportunity.softwarePipelineRotatedRegisters.empty() && !canEmitRotations )
			addPipelineBlocker( opportunity, "requires_register_rotation" );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator rotation = opportunity.softwarePipelineRotations.begin();
		     rotation != opportunity.softwarePipelineRotations.end(); ++rotation )
		{
			if( !rotation->hasScratchRegister )
				addPipelineBlocker( opportunity, "missing_rotation_scratch" );
		}

		std::list<std::string> prefetchWritesForSuffixStores;
		collectSoftwarePipelinePrefetchWrites( opportunity,
		                                       indexedTokens,
		                                       canEmitRotations,
		                                       prefetchWritesForSuffixStores );
		if( !opportunity.qConsumerTokenIndices.empty() )
		{
			assignSuffixStoreValueScratchRegisters( loop,
			                                        prefetchWritesForSuffixStores,
			                                        opportunity.softwarePipelineRotations,
			                                        indexedTokens,
			                                        opportunity.qConsumerTokenIndices.back(),
			                                        opportunity.softwarePipelineSuffixStores );
		}
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store = opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( store->requiresValueRotation && !store->hasValueScratchRegister )
				addPipelineBlocker( opportunity, "missing_suffix_store_value_scratch" );
		}

		classifySuffixStoreDrainOpportunity( opportunity,
		                                     loop,
		                                     canEmitRotations,
		                                     indexedTokens );

		const bool canEmitPrefetches = softwarePipelinePrefetchesCanEmit( opportunity,
		                                                                  indexedTokens,
		                                                                  canEmitRotations );
		if( opportunity.prologTokenIndices.size() != 1
		    || opportunity.prologTokenIndices.front() != opportunity.qProducerTokenIndex )
		{
			if( !canEmitPrefetches )
				addPipelineBlocker( opportunity, "multi_instruction_prefetch" );
		}

		if( !canEmitPrefetches )
		{
			for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator p = opportunity.softwarePipelinePrefetches.begin();
			     p != opportunity.softwarePipelinePrefetches.end(); ++p )
			{
				if( p->memoryKind != VU_MEMORY_NONE )
					addPipelineBlocker( opportunity, "multi_instruction_prefetch_memory" );
				if( p->readsInductionRegister )
					addPipelineBlocker( opportunity, "multi_instruction_prefetch_reads_induction" );
				if( p->readsInductionRegister && !p->hasNextIterationOffset )
					addPipelineBlocker( opportunity, "multi_instruction_prefetch_unknown_next_offset" );
			}

			std::list<std::string> prefetchWrites;
			collectSoftwarePipelinePrefetchWrites( opportunity,
			                                       indexedTokens,
			                                       canEmitRotations,
			                                       prefetchWrites );
			if( prefetchWritesClobberSuffix( opportunity, indexedTokens, prefetchWrites ) )
				addPipelineBlocker( opportunity, "prefetch_clobbers_suffix" );
		}

		if( qProducerOffset < loop.bodyTokens.size() )
		{
			VuTokenResourceAccess access;
			if( buildVuTokenResourceAccess( *loop.bodyTokens[qProducerOffset], access ) )
			{
				if( access.memoryKind != VU_MEMORY_NONE )
					addPipelineBlocker( opportunity, "q_producer_memory" );
				if( intersects( access.registerReads, opportunity.inductionRegisters ) )
					addPipelineBlocker( opportunity, "q_producer_reads_induction" );
			}
		}

		for( unsigned int i = opportunity.branchTokenIndex + 1; i < indexedTokens.size(); ++i )
		{
			if( vuTokenWritesQ( *indexedTokens[i] ) )
				break;
			if( vuTokenReadsQ( *indexedTokens[i] ) )
			{
				opportunity.qLiveOut = true;
				if( !softwarePipelineQDrainCanEmit( opportunity, loop, qProducerOffset ) )
					addPipelineBlocker( opportunity, "q_live_out" );
				break;
			}
		}

		opportunity.canEmitSoftwarePipeline =
		    opportunity.hasSoftwarePipelinePlan && opportunity.softwarePipelineBlockers.empty();
	}

	void classifyMultiQSoftwarePipelineOpportunity( VuLoopPipelineOpportunity& opportunity,
	                                                unsigned int qProducerCount,
	                                                const std::vector<const Token*>& indexedTokens )
	{
		if( qProducerCount <= 1 )
			return;

		opportunity.hasMultiQSoftwarePipelinePlan =
		    !opportunity.multiQPrologTokenIndices.empty()
		    && !opportunity.multiQMainTokenIndices.empty()
		    && !opportunity.multiQCyclicPrefixTokenIndices.empty();
		if( !opportunity.hasMultiQSoftwarePipelinePlan )
			addMultiQPipelineBlocker( opportunity, "missing_cyclic_prefix" );
		if( !opportunity.simpleCountedLoop )
			addMultiQPipelineBlocker( opportunity, "not_simple_counted_loop" );
		if( opportunity.branchDelaySlots == 0 )
			addMultiQPipelineBlocker( opportunity, "missing_branch_delay_slot" );
		if( opportunity.hasMemoryPreOrPostIncrement )
			addMultiQPipelineBlocker( opportunity, "pre_or_post_increment_memory" );
		if( opportunity.hasXgkick )
			addMultiQPipelineBlocker( opportunity, "xgkick_barrier" );
		if( opportunity.inductionRegisters.empty() )
			addMultiQPipelineBlocker( opportunity, "missing_induction_register" );
		if( opportunity.qProducerInsertionGapDeficitCycles != 0 )
			addMultiQPipelineBlocker( opportunity, "insufficient_q_insertion_gap" );
		if( opportunity.qLiveOut )
			addMultiQPipelineBlocker( opportunity, "q_live_out" );
		if( tokenIndicesHaveUnsafeMultiQCyclicPrefixSideEffects( opportunity.multiQCyclicPrefixTokenIndices,
		                                                         indexedTokens,
		                                                         opportunity.multiQCyclicPrefixNeedsGuard ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_side_effect" );
		if( tokenIndicesContainVisibleLabel( opportunity.multiQCyclicPrefixTokenIndices,
		                                     indexedTokens,
		                                     opportunity.labelTokenIndex )
		    || tokenIndicesContainVisibleLabel( opportunity.multiQMainTokenIndices,
		                                        indexedTokens,
		                                        opportunity.labelTokenIndex )
		    || loopBodyContainsVisibleInternalLabel( opportunity, indexedTokens ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_or_main_label" );
		if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( opportunity, indexedTokens ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_reads_suffix_clobber" );
		if( cyclicPrefixClobbersBranch( opportunity, indexedTokens ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_clobbers_branch" );

		for( std::vector<VuLoopQStage>::const_iterator stage = opportunity.qStages.begin();
		     stage != opportunity.qStages.end(); ++stage )
		{
			if( stage->qConsumerTokenIndices.empty() )
				addMultiQPipelineBlocker( opportunity, "stage_without_consumers" );
		}

		opportunity.eligibleMultiQSoftwarePipeline =
		    opportunity.hasMultiQSoftwarePipelinePlan
		    && opportunity.multiQSoftwarePipelineBlockers.empty();
		opportunity.canEmitMultiQSoftwarePipeline = opportunity.eligibleMultiQSoftwarePipeline;
	}

	Token tokenWithoutLabel( const Token& token )
	{
		Token copy( token );
		copy.setLabel( "" );
		return copy;
	}

	const Operand* syntheticMoveOperand()
	{
		static const Operand moveOperand( "MOVE",
		                                  2,
		                                  Operand::LOWER | Operand::DEST,
		                                  "vf:dest:write,vf:dest",
		                                  Operand::INVALID,
		                                  1,
		                                  4 );
		return &moveOperand;
	}

	unsigned int vfRegisterNumber( const std::string& reg )
	{
		if( reg.size() < 3 )
			return 0;
		return static_cast<unsigned int>( std::atoi( reg.substr( 2 ).c_str() ) );
	}

	bool physicalVfRegisterName( const std::string& reg )
	{
		return reg.size() >= 3
		    && reg[0] == 'V'
		    && reg[1] == 'F'
		    && reg[2] >= '0'
		    && reg[2] <= '9';
	}

	void setFloatRegisterArgumentBase( Token::Argument& argument,
	                                   const std::string& base )
	{
		if( physicalVfRegisterName( base ) )
			argument.setRegNumber( static_cast<int>( vfRegisterNumber( base ) ) );
		else
			argument.setAlias( base );
	}

	unsigned int fieldMaskForFieldList( const std::list<std::string>& fields )
	{
		unsigned int mask = 0;
		for( std::list<std::string>::const_iterator i = fields.begin(); i != fields.end(); ++i )
		{
			if( i->find( 'x' ) != std::string::npos ) mask |= Token::X;
			if( i->find( 'y' ) != std::string::npos ) mask |= Token::Y;
			if( i->find( 'z' ) != std::string::npos ) mask |= Token::Z;
			if( i->find( 'w' ) != std::string::npos ) mask |= Token::W;
		}
		return mask;
	}

	unsigned int rotationFieldMask( const VuSoftwarePipelineRotation& rotation )
	{
		unsigned int mask = fieldMaskForFieldList( rotation.inputFields )
		                  | fieldMaskForFieldList( rotation.outputFields );
		return mask ? mask : (Token::X | Token::Y | Token::Z | Token::W);
	}

	void rewriteRotatedRegistersToScratch( Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<Token::Argument>::iterator i = token.arguments().begin(); i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) )
				continue;
			const VuSoftwarePipelineRotation* rotation = findRotationForBase( rotations, registerBaseKey( key ) );
			if( !rotation || !rotation->hasScratchRegister )
				continue;
			i->setRegNumber( static_cast<int>( vfRegisterNumber( rotation->scratchRegister ) ) );
		}
	}

	Token makeRotationMoveToken( const Token& donor, const VuSoftwarePipelineRotation& rotation )
	{
		Token token( donor );
		token.setLabel( "" );
		token.setName( "move" );
		token.setOperand( syntheticMoveOperand() );
		token.setFlags( Token::PROCESSED );
		token.setBroadcast( 0 );
		token.setFields( rotationFieldMask( rotation ) );
		token.arguments().clear();

		Token::Argument dst( rotation.registerBase );
		dst.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( dst, rotation.registerBase );
		dst.setFlags( Token::Argument::WRITE | Token::Argument::DEST );
		dst.setFields( rotationFieldMask( rotation ) );
		token.arguments().push_back( dst );

		Token::Argument src( rotation.scratchRegister );
		src.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( src, rotation.scratchRegister );
		src.setFlags( 0 );
		src.setFields( 0 );
		token.arguments().push_back( src );

		return token;
	}

	Token makeSuffixStoreValueMoveToken( const Token& donor,
	                                     const VuSoftwarePipelineSuffixStore& store )
	{
		Token token( donor );
		token.setLabel( "" );
		token.setName( "move" );
		token.setOperand( syntheticMoveOperand() );
		token.setFlags( Token::PROCESSED );
		token.setBroadcast( 0 );
		token.setFields( fieldMaskForFieldList( store.storedValueFields ) );
		token.arguments().clear();

		Token::Argument dst( store.valueScratchRegister );
		dst.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( dst, store.valueScratchRegister );
		dst.setFlags( Token::Argument::WRITE | Token::Argument::DEST );
		dst.setFields( fieldMaskForFieldList( store.storedValueFields ) );
		token.arguments().push_back( dst );

		Token::Argument src( store.storedValueRegister );
		src.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( src, store.storedValueRegister );
		src.setFlags( 0 );
		src.setFields( 0 );
		token.arguments().push_back( src );

		return token;
	}

	const VuSoftwarePipelinePrefetch* findPrefetchForTokenIndex( const std::vector<VuSoftwarePipelinePrefetch>& prefetches,
	                                                             unsigned int tokenIndex )
	{
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = prefetches.begin(); i != prefetches.end(); ++i )
		{
			if( i->tokenIndex == tokenIndex )
				return &*i;
		}
		return NULL;
	}

	bool setIndirectMemoryOffset( Token& token, const std::string& baseRegister, long offset )
	{
		for( std::list<Token::Argument>::iterator i = token.arguments().begin(); i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::INTEGER_REGISTER
			    || !(i->flags() & Token::Argument::INDIRECT) )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) || key != baseRegister )
				continue;
			std::stringstream s;
			s << offset;
			i->setImmediate( s.str() );
			return true;
		}
		return false;
	}

	bool tokenHasSchedulingBoundaryForStoreBaseAdvance( const Token& token )
	{
		if( token.label().length() != 0 )
			return true;
		if( isVuSchedulingBarrier( token ) )
			return true;
		if( token.flags() & (Token::BRANCH_DELAY_FILLER | Token::PREORDERED) )
			return true;
		if( token.operand() && token.operand()->isPreprocessor() )
			return true;
		return false;
	}

	bool storeUsesBaseWithKnownOffset( const Token& token,
	                                   const std::string& baseRegister,
	                                   long& offset )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.memoryKind != VU_MEMORY_STORE )
			return false;
		if( access.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;
		if( !access.hasMemoryBase || !access.hasMemoryOffset )
			return false;
		if( access.memoryBaseRegister != baseRegister )
			return false;
		offset = access.memoryOffset;
		return true;
	}

	bool tokenReadsOrWritesRegisterKey( const Token& token, const std::string& registerKey )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		return containsKey( access.registerReads, registerKey )
		    || containsKey( access.registerWrites, registerKey );
	}

	Token adjustedPrefetchToken( const Token& token,
	                             const VuSoftwarePipelinePrefetch* prefetch,
	                             const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		Token copy = tokenWithoutLabel( token );
		if( prefetch && prefetch->hasNextIterationOffset && prefetch->hasMemoryBase )
			setIndirectMemoryOffset( copy, prefetch->memoryBaseRegister, prefetch->nextIterationOffset );
		rewriteRotatedRegistersToScratch( copy, rotations );
		return copy;
	}

	Token adjustedQProducerToken( const Token& token,
	                              const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		Token copy = tokenWithoutLabel( token );
		rewriteRotatedRegistersToScratch( copy, rotations );
		return copy;
	}

	void rewriteSuffixStoreValueToScratch( Token& token,
	                                       const VuSoftwarePipelineSuffixStore& store );

	Token adjustedDelayedSuffixStoreToken( const Token& token,
	                                       const VuSoftwarePipelineSuffixStore& store )
	{
		Token copy = tokenWithoutLabel( token );
		if( store.hasNextIterationOffset && store.hasMemoryBase )
			setIndirectMemoryOffset( copy, store.memoryBaseRegister, store.nextIterationOffset );
		rewriteSuffixStoreValueToScratch( copy, store );
		return copy;
	}

	void rewriteSuffixStoreValueToScratch( Token& token,
	                                       const VuSoftwarePipelineSuffixStore& store )
	{
		if( !store.requiresValueRotation || !store.hasValueScratchRegister )
			return;
		for( std::list<Token::Argument>::iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER
			    || (i->flags() & (Token::Argument::INDIRECT | Token::Argument::WRITE)) )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) || key != store.storedValueRegister )
				continue;
			i->setRegNumber( static_cast<int>( vfRegisterNumber( store.valueScratchRegister ) ) );
			return;
		}
	}

	void appendRotationMoves( std::list<Token>& output,
	                          const Token& donor,
	                          const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
		{
			if( i->hasScratchRegister )
				output.push_back( makeRotationMoveToken( donor, *i ) );
		}
	}

	void appendSuffixStoreValueRotationMoves( std::list<Token>& output,
	                                          const Token& donor,
	                                          const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->requiresValueRotation
			    && i->hasValueScratchRegister
			    && (!i->delayedDrain || i->rotateValueBeforePrefetch) )
				output.push_back( makeSuffixStoreValueMoveToken( donor, *i ) );
		}
	}

	void appendDelayedSuffixStores( std::list<Token>& output,
	                                const std::vector<const Token*>& indexedTokens,
	                                const VuSoftwarePipelineRewritePlan& rewrite )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = rewrite.suffixStores.begin();
		     i != rewrite.suffixStores.end(); ++i )
		{
			if( !i->delayedDrain || i->tokenIndex >= indexedTokens.size() )
				continue;
			output.push_back( adjustedDelayedSuffixStoreToken( *indexedTokens[i->tokenIndex], *i ) );
		}
	}

	bool rewriteDelaysCyclicPrefixTokenInBranchSlot( const VuSoftwarePipelineRewritePlan& rewrite,
	                                                 unsigned int tokenIndex )
	{
		return rewrite.cyclicPrefixLastTokenInBranchDelaySlot
		    && !rewrite.cyclicPrefixTokenIndices.empty()
		    && rewrite.cyclicPrefixTokenIndices.back() == tokenIndex;
	}

	Token adjustedCyclicPrefixBranchDelayToken( const Token& token,
	                                            const VuSoftwarePipelineRewritePlan& rewrite )
	{
		Token copy =
		    adjustedMultiQCyclicPrefixToken( token,
		                                     rewrite.cyclicPrefixRotations,
		                                     rewrite.inductionUpdates,
		                                     rewrite.cyclicPrefixInsertBeforeTokenIndex );
		copy.setFlags( copy.flags() | Token::BRANCH_DELAY_FILLER );
		return copy;
	}

	void appendCyclicPrefixTokens( std::list<Token>& output,
	                               const std::vector<const Token*>& indexedTokens,
	                               const VuSoftwarePipelineRewritePlan& rewrite,
	                               bool includeBranchDelayToken )
	{
		for( std::vector<unsigned int>::const_iterator p = rewrite.cyclicPrefixTokenIndices.begin();
		     p != rewrite.cyclicPrefixTokenIndices.end(); ++p )
		{
			if( *p >= indexedTokens.size() )
				continue;
			if( !includeBranchDelayToken
			    && rewriteDelaysCyclicPrefixTokenInBranchSlot( rewrite, *p ) )
				continue;
			output.push_back( adjustedMultiQCyclicPrefixToken( *indexedTokens[*p],
			                                                   rewrite.cyclicPrefixRotations,
			                                                   rewrite.inductionUpdates,
			                                                   rewrite.cyclicPrefixInsertBeforeTokenIndex ) );
		}
	}

	void appendDelayedCyclicPrefixBranchToken( std::list<Token>& output,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           const VuSoftwarePipelineRewritePlan& rewrite )
	{
		if( !rewrite.cyclicPrefixLastTokenInBranchDelaySlot
		    || rewrite.cyclicPrefixTokenIndices.empty() )
			return;
		const unsigned int tokenIndex = rewrite.cyclicPrefixTokenIndices.back();
		if( tokenIndex < indexedTokens.size() )
			output.push_back( adjustedCyclicPrefixBranchDelayToken( *indexedTokens[tokenIndex], rewrite ) );
	}

	void appendRewriteMainToken( std::list<Token>& output,
	                             const std::vector<const Token*>& indexedTokens,
	                             const VuSoftwarePipelineRewritePlan& rewrite,
	                             unsigned int tokenIndex,
	                             bool skipDelayedStores,
	                             bool invertBranchToDrain )
	{
		if( tokenIndex >= indexedTokens.size() )
			return;

		Token token = tokenWithoutLabel( *indexedTokens[tokenIndex] );
		const VuSoftwarePipelineSuffixStore* suffixStore =
		    findSuffixStoreForTokenIndex( rewrite.suffixStores, tokenIndex );
		if( suffixStore && suffixStore->delayedDrain && skipDelayedStores )
		{
			if( suffixStore->requiresValueRotation
			    && suffixStore->hasValueScratchRegister
			    && suffixStore->rotateValueAtStore )
				output.push_back( makeSuffixStoreValueMoveToken( *indexedTokens[tokenIndex],
				                                                 *suffixStore ) );
			return;
		}
		if( suffixStore )
			rewriteSuffixStoreValueToScratch( token, *suffixStore );

		if( tokenIndex == rewrite.branchTokenIndex && invertBranchToDrain )
			output.push_back( invertedBranchToDrainToken( *indexedTokens[tokenIndex],
			                                             rewrite.drainLabel ) );
		else
			output.push_back( token );
	}

	void appendCyclicPrefixMainBody( std::list<Token>& output,
	                                 const std::vector<const Token*>& indexedTokens,
	                                 const VuSoftwarePipelineRewritePlan& rewrite,
	                                 bool skipDelayedStores,
	                                 bool invertBranchToDrain,
	                                 bool allowBranchDelayCyclicPrefix )
	{
		bool emittedCyclicPrefix = false;
		for( std::vector<unsigned int>::const_iterator m = rewrite.mainTokenIndices.begin();
		     m != rewrite.mainTokenIndices.end(); ++m )
		{
			if( !emittedCyclicPrefix && *m == rewrite.cyclicPrefixInsertBeforeTokenIndex )
			{
				if( rewrite.cyclicPrefixNeedsGuard && rewrite.branchTokenIndex < indexedTokens.size() )
					output.push_back( invertedBranchToDrainToken( *indexedTokens[rewrite.branchTokenIndex],
					                                               rewrite.drainLabel ) );
				appendCyclicPrefixTokens( output, indexedTokens, rewrite, false );
				emittedCyclicPrefix = true;
			}
			if( *m < indexedTokens.size() && *m != rewrite.branchTokenIndex )
				appendRewriteMainToken( output,
				                        indexedTokens,
				                        rewrite,
				                        *m,
				                        skipDelayedStores,
				                        invertBranchToDrain );
		}
		if( !emittedCyclicPrefix )
		{
			if( rewrite.cyclicPrefixNeedsGuard && rewrite.branchTokenIndex < indexedTokens.size() )
				output.push_back( invertedBranchToDrainToken( *indexedTokens[rewrite.branchTokenIndex],
				                                               rewrite.drainLabel ) );
			appendCyclicPrefixTokens( output, indexedTokens, rewrite, false );
		}
		if( rewrite.branchTokenIndex < indexedTokens.size() )
		{
			appendRewriteMainToken( output,
			                        indexedTokens,
			                        rewrite,
			                        rewrite.branchTokenIndex,
			                        skipDelayedStores,
			                        invertBranchToDrain );
			if( allowBranchDelayCyclicPrefix && !rewrite.cyclicPrefixNeedsGuard )
				appendDelayedCyclicPrefixBranchToken( output, indexedTokens, rewrite );
		}
	}

	void appendTokenRangeWithInsertedPrefetchAndQProducer( std::list<Token>& output,
	                                                       const std::vector<const Token*>& indexedTokens,
	                                                       const VuSoftwarePipelineRewritePlan& rewrite,
	                                                       unsigned int beginIndex,
	                                                       unsigned int endIndex,
	                                                       bool skipDelayedStores = false,
	                                                       bool invertBranchToDrain = false )
	{
		for( unsigned int i = beginIndex; i <= endIndex && i < indexedTokens.size(); ++i )
		{
			if( i == rewrite.branchTokenIndex && !rewrite.rotations.empty() )
				appendRotationMoves( output, *indexedTokens[i], rewrite.rotations );
			Token token = tokenWithoutLabel( *indexedTokens[i] );
			const VuSoftwarePipelineSuffixStore* suffixStore =
			    findSuffixStoreForTokenIndex( rewrite.suffixStores, i );
			if( suffixStore && suffixStore->delayedDrain && skipDelayedStores )
			{
				if( suffixStore->requiresValueRotation
				    && suffixStore->hasValueScratchRegister
				    && suffixStore->rotateValueAtStore )
					output.push_back( makeSuffixStoreValueMoveToken( *indexedTokens[i], *suffixStore ) );
			}
			else if( suffixStore )
				rewriteSuffixStoreValueToScratch( token, *suffixStore );
			if( !(suffixStore && suffixStore->delayedDrain && skipDelayedStores) )
			{
				if( i == rewrite.branchTokenIndex && invertBranchToDrain )
					output.push_back( invertedBranchToDrainToken( *indexedTokens[i], rewrite.drainLabel ) );
				else
					output.push_back( token );
			}
			if( i == rewrite.prefetchInsertAfterTokenIndex )
			{
				appendSuffixStoreValueRotationMoves( output,
				                                     *indexedTokens[i],
				                                     rewrite.suffixStores );
				for( std::vector<unsigned int>::const_iterator p = rewrite.prefetchTokenIndices.begin();
				     p != rewrite.prefetchTokenIndices.end(); ++p )
				{
					if( *p >= indexedTokens.size() )
						continue;
					output.push_back( adjustedPrefetchToken( *indexedTokens[*p],
					                                        findPrefetchForTokenIndex( rewrite.prefetches, *p ),
					                                        rewrite.rotations ) );
				}
			}
			if( i == rewrite.qProducerInsertAfterTokenIndex )
			{
				if( rewrite.qProducerTokenIndex < indexedTokens.size() )
				{
					Token qProducer = adjustedQProducerToken( *indexedTokens[rewrite.qProducerTokenIndex],
					                                          rewrite.rotations );
					if( rewrite.qProducerInBranchDelaySlot )
						qProducer.setFlags( qProducer.flags() | Token::BRANCH_DELAY_FILLER );
					output.push_back( qProducer );
				}
			}
		}
	}
}

VuScheduledPaddingKind vuScheduledPaddingKindForReadHazard( const Token& token,
                                                            const Token* partner,
                                                            const VuLatencyTracker& latencyTracker,
                                                            int currentCycle )
{
	bool readsQ = vuTokenReadsQ( token );
	bool readsP = vuTokenReadsP( token );
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ( *partner );
		readsP = readsP || vuTokenReadsP( *partner );
	}

	const int qGap = readsQ ? (latencyTracker.qReadyCycle() - currentCycle) : 0;
	const int pGap = readsP ? (latencyTracker.pReadyCycle() - currentCycle) : 0;
	if( qGap > 1 && qGap >= pGap )
		return VU_SCHEDULED_PADDING_WAITQ;
	if( pGap > 1 )
		return VU_SCHEDULED_PADDING_WAITP;
	return VU_SCHEDULED_PADDING_NOP;
}

VuBasicBlock::VuBasicBlock()
{
	firstTokenIndex = 0;
	terminatedByBarrier = false;
	terminatorKind = VU_BASIC_BLOCK_TERMINATOR_NONE;
	terminator = NULL;
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

VuScheduledIssueSlot::VuScheduledIssueSlot()
{
	firstToken = NULL;
	secondToken = NULL;
	upperToken = NULL;
	lowerToken = NULL;
	firstTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	secondTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	upperTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	lowerTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	padding = false;
	paddingKind = VU_SCHEDULED_PADDING_NONE;
	ignoredImplicitWawResources = VU_RESOURCE_NONE;
	issueCycle = 0;
	cycleCount = 1;
}

VuScheduledBasicBlock::VuScheduledBasicBlock()
{
	firstIssueCycle = 0;
	cycleCount = 0;
}

VuScheduledProgram::VuScheduledProgram()
{
	cycleCount = 0;
}

VuLoopCandidate::VuLoopCandidate()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	firstBodyTokenIndex = 0;
	lastBodyTokenIndex = 0;
	hasLoopDirective = false;
	loopCsClid = 0;
	loopCsMlid = 0;
	simpleCountedLoop = false;
	memoryLoadCount = 0;
	memoryStoreCount = 0;
	hasMemoryPreOrPostIncrement = false;
	hasXgkick = false;
	branchToken = NULL;
}

VuLoopQStage::VuLoopQStage()
{
	qProducerTokenIndex = 0;
	qProducerLatency = 0;
	qProducerConsumerGapCycles = 0;
	qProducerConsumerGapDeficitCycles = 0;
	loopCarriedQGapCycles = 0;
	qProducerInsertionGapCycles = 0;
	qProducerInsertionGapDeficitCycles = 0;
	qSchedulingStrategy = VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
}

VuLoopPipelineOpportunity::VuLoopPipelineOpportunity()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	qProducerTokenIndex = 0;
	firstQConsumerTokenIndex = 0;
	lastQConsumerTokenIndex = 0;
	qProducerLatency = 0;
	qProducerConsumerGapCycles = 0;
	qProducerConsumerGapDeficitCycles = 0;
	loopCarriedQGapCycles = 0;
	qProducerInsertionGapCycles = 0;
	qProducerInsertionGapDeficitCycles = 0;
	qSchedulingStrategy = VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
	sourcePrefixCycles = 0;
	sourceSuffixCycles = 0;
	branchDelaySlots = 0;
	loopCsClid = 0;
	loopCsMlid = 0;
	simpleCountedLoop = false;
	hasSingleQProducer = false;
	requiresPrologEpilog = false;
	requiresLoopCarriedRegisters = false;
	qLiveOut = false;
	eligibleSingleQSoftwarePipeline = false;
	hasSoftwarePipelinePlan = false;
	canEmitSoftwarePipeline = false;
	eligibleMultiQSoftwarePipeline = false;
	hasMultiQSoftwarePipelinePlan = false;
	canEmitMultiQSoftwarePipeline = false;
	hasSuffixStoreDrainPlan = false;
	canEmitSuffixStoreDrain = false;
	multiQCyclicPrefixInsertBeforeTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	multiQCyclicPrefixNeedsGuard = false;
	multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
	memoryLoadCount = 0;
	memoryStoreCount = 0;
	hasMemoryPreOrPostIncrement = false;
	hasXgkick = false;
	stageCount = 1;
}

VuSoftwarePipelineRewritePlan::VuSoftwarePipelineRewritePlan()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	qProducerTokenIndex = 0;
	prefetchInsertAfterTokenIndex = 0;
	qProducerInsertAfterTokenIndex = 0;
	qProducerInBranchDelaySlot = false;
	qProducerBranchDelayBlockedBySuffixDependency = false;
	qProducerBranchDelaySuffixBlockerTokenIndex = 0;
	cyclicPrefixBeforeBranch = false;
	cyclicPrefixInsertBeforeTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	cyclicPrefixNeedsGuard = false;
	cyclicPrefixLastTokenInBranchDelaySlot = false;
	drainsSuffixStores = false;
	emitsDrain = false;
	stageCount = 1;
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
			current.terminatorKind = vuBlockTerminatorKind( *i );
			current.terminator = &*i;
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
			const unsigned int preciseImplicitResources = VU_RESOURCE_ACC | VU_RESOURCE_MAC | VU_RESOURCE_CLIP;
			const unsigned int pairwiseImplicitResources = ~preciseImplicitResources;

			if( intersects( a.registerWrites, b.registerReads ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_RAW );
			if( intersects( a.registerReads, b.registerWrites ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_WAR );
			if( intersects( a.registerWrites, b.registerWrites ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_WAW );

			if( a.implicitWrites & b.implicitReads & pairwiseImplicitResources )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_RAW );
			if( a.implicitReads & b.implicitWrites )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAR );
			if( (a.implicitWrites & b.implicitWrites & pairwiseImplicitResources
			     & ~ignoredImplicitWawResources) != 0 )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAW );

			if( memoryOrderRequiresDependency( a,
			                                  b,
			                                  *block.tokens[before],
			                                  *block.tokens[after] ) )
				addEdge( edges, before, after, VU_DEPENDENCY_MEMORY );
		}
	}

		addPreciseImplicitFlagDependencies( edges,
		                                    accesses,
		                                    VU_RESOURCE_ACC,
		                                    ignoredImplicitWawResources );
		addPreciseImplicitFlagDependencies( edges,
		                                    accesses,
		                                    VU_RESOURCE_MAC,
		                                    ignoredImplicitWawResources );
	addPreciseImplicitFlagDependencies( edges,
	                                    accesses,
	                                    VU_RESOURCE_CLIP,
	                                    ignoredImplicitWawResources );

	return edges;
}

namespace
{
	std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlotsWithLatency(
	    const VuBasicBlock& block,
	    unsigned int ignoredImplicitWawResources,
	    VuLatencyTracker& latencyTracker,
	    unsigned int blockStartCycle )
	{
		std::vector<VuScheduledIssueSlot> slots;
		std::vector<const Token*> segment;
		unsigned int currentCycle = blockStartCycle;

		for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
		{
			if( isVuReadyScheduleCandidate( **i ) )
			{
				segment.push_back( *i );
				continue;
			}

			std::vector<VuScheduledIssueSlot> segmentSlots =
				scheduleReadySegmentIssueSlots( segment,
				                                ignoredImplicitWawResources,
				                                latencyTracker,
				                                blockStartCycle,
				                                currentCycle );
			slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );
			segment.clear();
			bool pairedWithPreviousSlot = false;
			if( !slots.empty() )
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				if( latencyTracker.readHazardDelay( **i, NULL, static_cast<int>( pairedCycle ) ) <= 0 )
					pairedWithPreviousSlot =
						tryPairTailWithPreviousSlot( slots,
						                             **i,
						                             ignoredImplicitWawResources );
			}
			if( !pairedWithPreviousSlot )
			{
				appendReadHazardPaddingSlots( slots,
				                              **i,
				                              NULL,
				                              latencyTracker,
				                              blockStartCycle,
				                              currentCycle );
				slots.push_back( makeIssueSlot( *i,
				                                NULL,
				                                currentCycle - blockStartCycle,
				                                1,
				                                ignoredImplicitWawResources ) );
				latencyTracker.recordWrites( **i, static_cast<int>( currentCycle ) );
				++currentCycle;
			}
			else
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				latencyTracker.recordWrites( **i, static_cast<int>( pairedCycle ) );
			}
		}

		std::vector<VuScheduledIssueSlot> segmentSlots =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                latencyTracker,
			                                blockStartCycle,
			                                currentCycle );
		slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );

		assignScheduledIssueSlotTokenIndices( block, slots );
		return slots;
	}
}

std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlots( const VuBasicBlock& block,
                                                                       unsigned int ignoredImplicitWawResources )
{
	VuLatencyTracker latencyTracker;
	return scheduleVuBasicBlockReadyIssueSlotsWithLatency( block,
	                                                       ignoredImplicitWawResources,
	                                                       latencyTracker,
	                                                       0 );
}

namespace
{
	VuScheduledProgram scheduleVuProgramReadyIssueSlotsInternal( const std::list<Token>& tokens,
	                                                             unsigned int ignoredImplicitWawResources,
	                                                             bool carryLatencyAcrossBlocks )
	{
		VuScheduledProgram program;
		const std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );
		VuLatencyTracker programLatencyTracker;

		for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
		{
			VuScheduledBasicBlock scheduledBlock;
			scheduledBlock.block = *block;
			scheduledBlock.firstIssueCycle = program.cycleCount;
			if( carryLatencyAcrossBlocks )
			{
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithLatency( *block,
					                                                ignoredImplicitWawResources,
					                                                programLatencyTracker,
					                                                program.cycleCount );
			}
			else
			{
				VuLatencyTracker blockLatencyTracker;
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithLatency( *block,
					                                                ignoredImplicitWawResources,
					                                                blockLatencyTracker,
					                                                program.cycleCount );
			}
			for( std::vector<VuScheduledIssueSlot>::const_iterator slot = scheduledBlock.issueSlots.begin();
			     slot != scheduledBlock.issueSlots.end(); ++slot )
				scheduledBlock.cycleCount += slot->cycleCount;
			program.cycleCount += scheduledBlock.cycleCount;
			program.blocks.push_back( scheduledBlock );
		}

		return program;
	}
}

VuScheduledProgram scheduleVuProgramReadyIssueSlots( const std::list<Token>& tokens,
                                                     unsigned int ignoredImplicitWawResources )
{
	return scheduleVuProgramReadyIssueSlotsInternal( tokens,
	                                                ignoredImplicitWawResources,
	                                                true );
}

std::vector< std::vector<VuScheduledIssueSlot> > scheduleVuBasicBlocksReadyIssueSlotsWithFlagLiveness(
    const std::list<Token>& tokens )
{
	std::vector< std::vector<VuScheduledIssueSlot> > result;
	const VuScheduledProgram program = scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );

	for( std::vector<VuScheduledBasicBlock>::const_iterator block = program.blocks.begin();
	     block != program.blocks.end(); ++block )
		result.push_back( block->issueSlots );

	return result;
}

namespace
{
	VuScheduledProgram scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal(
	    const std::list<Token>& tokens,
	    bool carryLatencyAcrossBlocks )
	{
		VuScheduledProgram program;
		const std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );
		const VuFlagLiveness liveness = analyzeFlagLiveness( tokens );
		VuLatencyTracker programLatencyTracker;

		for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
		{
			VuScheduledBasicBlock scheduledBlock;
			scheduledBlock.block = *block;
			scheduledBlock.firstIssueCycle = program.cycleCount;
			if( carryLatencyAcrossBlocks )
			{
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness( *block,
					                                                     liveness,
					                                                     programLatencyTracker,
					                                                     program.cycleCount );
			}
			else
			{
				VuLatencyTracker blockLatencyTracker;
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness( *block,
					                                                     liveness,
					                                                     blockLatencyTracker,
					                                                     program.cycleCount );
			}
			for( std::vector<VuScheduledIssueSlot>::const_iterator slot = scheduledBlock.issueSlots.begin();
			     slot != scheduledBlock.issueSlots.end(); ++slot )
				scheduledBlock.cycleCount += slot->cycleCount;
			program.cycleCount += scheduledBlock.cycleCount;
			program.blocks.push_back( scheduledBlock );
		}

		return program;
	}
}

VuScheduledProgram scheduleVuProgramReadyIssueSlotsWithFlagLiveness( const std::list<Token>& tokens )
{
	return scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal( tokens, true );
}

std::list<Token> flattenVuScheduledProgramTokens( const VuScheduledProgram& program )
{
	std::list<Token> scheduled;

	for( std::vector<VuScheduledBasicBlock>::const_iterator block = program.blocks.begin();
	     block != program.blocks.end(); ++block )
		appendIssueSlotsFlat( block->issueSlots, scheduled );

	return scheduled;
}

unsigned int vuIgnoredFlagWawResourcesForRemaining( std::list<Token>::const_iterator begin,
                                                    std::list<Token>::const_iterator end )
{
	bool readsMac = false;
	bool readsClip = false;
	for( std::list<Token>::const_iterator i = begin; i != end; ++i )
	{
		readsMac = readsMac || tokenReadsMac( *i );
		readsClip = readsClip || tokenReadsClip( *i );
	}

	unsigned int mask = VU_RESOURCE_NONE;
	if( !readsMac )
		mask |= VU_RESOURCE_MAC;
	if( !readsClip )
		mask |= VU_RESOURCE_CLIP;
	return mask;
}

std::vector<VuLoopCandidate> findVuLoopCandidates( const std::list<Token>& tokens )
{
	std::vector<const Token*> indexedTokens;
	std::map<std::string, unsigned int> labels;
	unsigned int index = 0;

	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
	{
		indexedTokens.push_back( &*i );
		if( (*i).label().length() != 0 )
			labels[(*i).label()] = index;
	}

	std::vector<VuLoopCandidate> result;
	for( unsigned int branchIndex = 0; branchIndex < indexedTokens.size(); ++branchIndex )
	{
		const Token& token = *indexedTokens[branchIndex];
		std::string label;
		if( !branchTargetLabel( token, label ) )
			continue;

		std::map<std::string, unsigned int>::const_iterator target = labels.find( label );
		if( target == labels.end() || target->second >= branchIndex )
			continue;

		VuLoopCandidate candidate;
		candidate.label = label;
		candidate.labelTokenIndex = target->second;
		candidate.branchTokenIndex = branchIndex;
		candidate.firstBodyTokenIndex = target->second;
		candidate.lastBodyTokenIndex = branchIndex;
		candidate.hasLoopDirective = loopTargetHasDirective( indexedTokens, target->second );
		describeLoopCsDirectiveAtLabel( indexedTokens,
		                                target->second,
		                                candidate.loopCsClid,
		                                candidate.loopCsMlid );
		candidate.simpleCountedLoop = candidate.hasLoopDirective
		                           && !isVuTerminalUnconditionalBranch( token )
		                           && vuTokenBranchDelaySlots( token ) > 0;
		candidate.branchToken = &token;

		for( unsigned int body = candidate.firstBodyTokenIndex; body <= candidate.lastBodyTokenIndex; ++body )
			candidate.bodyTokens.push_back( indexedTokens[body] );
		analyzeLoopCandidateResources( candidate );

		result.push_back( candidate );
	}

	return result;
}

std::vector<VuLoopPipelineOpportunity> findVuLoopPipelineOpportunities( const std::list<Token>& tokens )
{
	std::vector<VuLoopPipelineOpportunity> result;
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
		indexedTokens.push_back( &*i );

	std::vector<VuLoopCandidate> loops = findVuLoopCandidates( tokens );

	for( std::vector<VuLoopCandidate>::const_iterator loop = loops.begin(); loop != loops.end(); ++loop )
	{
		unsigned int qProducerCount = 0;
		unsigned int qProducerOffset = 0;
		std::vector<unsigned int> qProducerOffsets;
		for( unsigned int i = 0; i < loop->bodyTokens.size(); ++i )
		{
			if( vuTokenWritesQ( *loop->bodyTokens[i] ) )
			{
				++qProducerCount;
				qProducerOffset = i;
				qProducerOffsets.push_back( i );
			}
		}

		if( qProducerCount == 0 )
			continue;

		std::vector<unsigned int> lastQConsumerOffsets;
		for( unsigned int i = qProducerOffset + 1; i < loop->bodyTokens.size(); ++i )
		{
			if( vuTokenReadsQ( *loop->bodyTokens[i] ) )
				lastQConsumerOffsets.push_back( i );
		}

		const unsigned int branchDelaySlots = loop->branchToken ? vuTokenBranchDelaySlots( *loop->branchToken ) : 0;
		const unsigned int branchOffset = loop->bodyTokens.empty()
		                                ? 0
		                                : static_cast<unsigned int>( loop->bodyTokens.size() - 1 );

		VuLoopPipelineOpportunity opportunity;
		opportunity.label = loop->label;
		opportunity.labelTokenIndex = loop->labelTokenIndex;
		opportunity.branchTokenIndex = loop->branchTokenIndex;
		opportunity.qProducerTokenIndex = loop->firstBodyTokenIndex + qProducerOffset;
		for( std::vector<unsigned int>::const_iterator producer = qProducerOffsets.begin();
		     producer != qProducerOffsets.end(); ++producer )
			opportunity.qProducerTokenIndices.push_back( loop->firstBodyTokenIndex + *producer );
		std::vector<unsigned int> allQConsumerOffsets;
		for( unsigned int producerIndex = 0; producerIndex < qProducerOffsets.size(); ++producerIndex )
		{
			const unsigned int producerOffset = qProducerOffsets[producerIndex];
			const unsigned int nextProducerOffset =
			    producerIndex + 1 < qProducerOffsets.size()
			    ? qProducerOffsets[producerIndex + 1]
			    : static_cast<unsigned int>( loop->bodyTokens.size() );
			VuLoopQStage stage;
			stage.qProducerTokenIndex = loop->firstBodyTokenIndex + producerOffset;
			stage.qProducerLatency = loop->bodyTokens[producerOffset]->operand()
			                       ? loop->bodyTokens[producerOffset]->operand()->latency()
			                       : 0;
			for( unsigned int consumerOffset = producerOffset + 1;
			     consumerOffset < nextProducerOffset && consumerOffset < loop->bodyTokens.size();
			     ++consumerOffset )
			{
				if( vuTokenReadsQ( *loop->bodyTokens[consumerOffset] ) )
				{
					stage.qConsumerTokenIndices.push_back( loop->firstBodyTokenIndex + consumerOffset );
					allQConsumerOffsets.push_back( consumerOffset );
				}
			}
			if( !stage.qConsumerTokenIndices.empty() )
			{
				const unsigned int firstConsumerOffset =
				    stage.qConsumerTokenIndices.front() - loop->firstBodyTokenIndex;
				const unsigned int lastConsumerOffset =
				    stage.qConsumerTokenIndices.back() - loop->firstBodyTokenIndex;
				stage.qProducerConsumerGapCycles =
				    countEmittableTokens( *loop, producerOffset + 1, firstConsumerOffset );
				stage.qProducerConsumerGapDeficitCycles =
				    stage.qProducerLatency > stage.qProducerConsumerGapCycles
				    ? stage.qProducerLatency - stage.qProducerConsumerGapCycles
				    : 0;
				stage.loopCarriedQGapCycles =
				    countEmittableTokens( *loop, firstConsumerOffset + 1, branchOffset )
				    + branchDelaySlots
				    + countEmittableTokens( *loop, 0, producerOffset );
				stage.qProducerInsertionGapCycles =
				    countEmittableTokens( *loop, lastConsumerOffset + 1, branchOffset )
				    + branchDelaySlots
				    + countEmittableTokens( *loop, 0, producerOffset );
				stage.qProducerInsertionGapDeficitCycles =
				    stage.qProducerLatency > stage.qProducerInsertionGapCycles
				    ? stage.qProducerLatency - stage.qProducerInsertionGapCycles
				    : 0;
				stage.qSchedulingStrategy =
				    classifyLoopQSchedulingStrategy( stage.qProducerConsumerGapDeficitCycles,
				                                     stage.loopCarriedQGapCycles,
				                                     stage.qProducerLatency );
			}
			opportunity.qStages.push_back( stage );
		}
		if( allQConsumerOffsets.empty() )
			continue;
		const std::vector<unsigned int>& primaryQConsumerOffsets =
			lastQConsumerOffsets.empty() ? allQConsumerOffsets : lastQConsumerOffsets;
		opportunity.firstQConsumerTokenIndex = loop->firstBodyTokenIndex + allQConsumerOffsets.front();
		opportunity.lastQConsumerTokenIndex = loop->firstBodyTokenIndex + allQConsumerOffsets.back();
		opportunity.qProducerLatency = loop->bodyTokens[qProducerOffset]->operand()
		                             ? loop->bodyTokens[qProducerOffset]->operand()->latency()
		                             : 0;
		opportunity.qProducerConsumerGapCycles = countEmittableTokens( *loop,
		                                                                qProducerOffset + 1,
		                                                                primaryQConsumerOffsets.front() );
		opportunity.sourcePrefixCycles = countEmittableTokens( *loop, 0, qProducerOffset );
		opportunity.sourceSuffixCycles = countEmittableTokens( *loop,
		                                                       primaryQConsumerOffsets.front() + 1,
		                                                       static_cast<unsigned int>( loop->bodyTokens.size() - 1 ) );
		opportunity.branchDelaySlots = branchDelaySlots;
		opportunity.loopCsClid = loop->loopCsClid;
		opportunity.loopCsMlid = loop->loopCsMlid;
		opportunity.qProducerConsumerGapDeficitCycles =
			opportunity.qProducerLatency > opportunity.qProducerConsumerGapCycles
			? opportunity.qProducerLatency - opportunity.qProducerConsumerGapCycles
			: 0;
		opportunity.loopCarriedQGapCycles =
			opportunity.sourceSuffixCycles + opportunity.branchDelaySlots + opportunity.sourcePrefixCycles;
		opportunity.qProducerInsertionGapCycles =
			countEmittableTokens( *loop,
			                      primaryQConsumerOffsets.back() + 1,
			                      static_cast<unsigned int>( loop->bodyTokens.size() - 1 ) )
		    + opportunity.branchDelaySlots
		    + opportunity.sourcePrefixCycles;
		opportunity.qProducerInsertionGapDeficitCycles =
			opportunity.qProducerLatency > opportunity.qProducerInsertionGapCycles
			? opportunity.qProducerLatency - opportunity.qProducerInsertionGapCycles
			: 0;
		opportunity.qSchedulingStrategy = classifyLoopQSchedulingStrategy( opportunity );
		opportunity.simpleCountedLoop = loop->simpleCountedLoop;
		opportunity.hasSingleQProducer = qProducerCount == 1;
		opportunity.requiresPrologEpilog = loop->simpleCountedLoop && qProducerCount == 1;
		opportunity.memoryLoadCount = loop->memoryLoadCount;
		opportunity.memoryStoreCount = loop->memoryStoreCount;
		opportunity.hasMemoryPreOrPostIncrement = loop->hasMemoryPreOrPostIncrement;
		opportunity.hasXgkick = loop->hasXgkick;
		opportunity.inductionRegisters = loop->inductionRegisters;
		opportunity.inductionUpdates = loop->inductionUpdates;
		opportunity.loopReadWriteRegisters = loop->loopReadWriteRegisters;

		for( std::vector<unsigned int>::const_iterator q = allQConsumerOffsets.begin(); q != allQConsumerOffsets.end(); ++q )
		{
			opportunity.qConsumerTokenIndices.push_back( loop->firstBodyTokenIndex + *q );
		}

		for( std::vector<VuLoopQStage>::const_iterator stage = opportunity.qStages.begin();
		     stage != opportunity.qStages.end(); ++stage )
		{
			for( std::vector<unsigned int>::const_iterator q = stage->qConsumerTokenIndices.begin();
			     q != stage->qConsumerTokenIndices.end(); ++q )
			{
				if( *q >= loop->firstBodyTokenIndex )
				{
					const unsigned int qOffset = *q - loop->firstBodyTokenIndex;
					collectLoopCarriedQInputs( *loop, qOffset, opportunity.carriedQInputRegisters );
					collectLoopCarriedQOutputs( *loop, qOffset, opportunity.carriedQOutputRegisters );
				}
			}
		}

		opportunity.requiresLoopCarriedRegisters = !opportunity.carriedQInputRegisters.empty()
		                                        || !opportunity.carriedQOutputRegisters.empty();
		opportunity.eligibleSingleQSoftwarePipeline = opportunity.simpleCountedLoop
		                                           && opportunity.hasSingleQProducer
		                                           && opportunity.branchDelaySlots > 0
		                                           && (opportunity.sourcePrefixCycles + opportunity.sourceSuffixCycles) >= opportunity.qProducerLatency;

		if( opportunity.eligibleSingleQSoftwarePipeline )
		{
			const unsigned int firstConsumerOffset = primaryQConsumerOffsets.front();
			const unsigned int branchOffset = loop->branchTokenIndex - loop->firstBodyTokenIndex;
			opportunity.hasSoftwarePipelinePlan = true;
			appendPipelineInstructionIndices( *loop, 0, firstConsumerOffset, opportunity.prologTokenIndices );
			appendPipelineInstructionIndices( *loop, firstConsumerOffset, branchOffset + 1, opportunity.mainTokenIndices );
			appendPipelineInstructionIndices( *loop, firstConsumerOffset, branchOffset, opportunity.drainTokenIndices );
			collectSoftwarePipelineSuffixStoreDescriptors( opportunity.drainTokenIndices,
			                                               indexedTokens,
			                                               opportunity );
		}

		if( qProducerCount > 1
		    && !opportunity.qStages.empty()
		    && !opportunity.qStages.front().qConsumerTokenIndices.empty() )
		{
			const unsigned int branchOffset = loop->branchTokenIndex - loop->firstBodyTokenIndex;
			const VuLoopQStage& firstStage = opportunity.qStages.front();
			const unsigned int firstProducerOffset =
			    firstStage.qProducerTokenIndex - loop->firstBodyTokenIndex;
			const unsigned int firstConsumerOffset =
			    firstStage.qConsumerTokenIndices.front() - loop->firstBodyTokenIndex;
			bool foundSafeCyclicPrefix = false;
			unsigned int bestMainCycles = ~0u;
			VuLoopPipelineOpportunity bestOpportunity = opportunity;
			if( firstProducerOffset > 0
			    && firstProducerOffset < branchOffset
			    && countEmittableTokens( *loop, firstProducerOffset, branchOffset ) != 0 )
			{
				foundSafeCyclicPrefix =
				    considerMultiQPipelineCandidate( *loop,
				                                     indexedTokens,
				                                     opportunity,
				                                     firstProducerOffset,
				                                     firstProducerOffset,
				                                     branchOffset,
				                                     true,
				                                     bestMainCycles,
				                                     bestOpportunity )
				    || foundSafeCyclicPrefix;
			}
			if( firstConsumerOffset < branchOffset
			    && firstStage.qProducerInsertionGapDeficitCycles == 0
			    && countEmittableTokens( *loop, firstConsumerOffset, branchOffset ) != 0 )
			{
				foundSafeCyclicPrefix =
				    considerMultiQPipelineCandidate( *loop,
				                                     indexedTokens,
				                                     opportunity,
				                                     firstConsumerOffset,
				                                     firstConsumerOffset,
				                                     branchOffset,
				                                     true,
				                                     bestMainCycles,
				                                     bestOpportunity )
				    || foundSafeCyclicPrefix;
			}

			unsigned int cyclicPrefixLastConsumerOffset = 0;
			bool foundFallbackCyclicPrefix = false;
			for( std::vector<VuLoopQStage>::const_iterator stage = opportunity.qStages.begin();
			     stage != opportunity.qStages.end(); ++stage )
			{
				if( stage->qConsumerTokenIndices.empty() )
					continue;
				const unsigned int lastConsumerOffset =
				    stage->qConsumerTokenIndices.back() - loop->firstBodyTokenIndex;
				if( lastConsumerOffset >= branchOffset )
					continue;
				if( countEmittableTokens( *loop, lastConsumerOffset + 1, branchOffset ) == 0 )
					continue;
				cyclicPrefixLastConsumerOffset = lastConsumerOffset;
				foundFallbackCyclicPrefix = true;
				foundSafeCyclicPrefix =
				    considerMultiQPipelineCandidate( *loop,
				                                     indexedTokens,
				                                     opportunity,
				                                     lastConsumerOffset + 1,
				                                     lastConsumerOffset + 1,
				                                     branchOffset,
				                                     true,
				                                     bestMainCycles,
				                                     bestOpportunity )
				    || foundSafeCyclicPrefix;
			}
			if( loop->simpleCountedLoop
			    && !loop->hasXgkick
			    && !loop->hasMemoryPreOrPostIncrement
			    && loop->branchTokenIndex < indexedTokens.size()
			    && branchCanInvertToDrain( *indexedTokens[loop->branchTokenIndex] ) )
			{
				for( unsigned int offset = 1; offset < branchOffset; ++offset )
				{
					const unsigned int tokenIndex = loop->firstBodyTokenIndex + offset - 1;
					if( tokenIndex >= indexedTokens.size()
					    || !tokenHasGuardableMultiQCyclicPrefixSideEffect( *indexedTokens[tokenIndex] ) )
						continue;
					if( countEmittableTokens( *loop, offset, branchOffset ) == 0 )
						continue;
					foundSafeCyclicPrefix =
					    considerMultiQPipelineCandidate( *loop,
					                                     indexedTokens,
					                                     opportunity,
					                                     offset,
					                                     offset,
					                                     branchOffset,
					                                     true,
					                                     bestMainCycles,
					                                     bestOpportunity )
					    || foundSafeCyclicPrefix;
				}
			}
			if( foundSafeCyclicPrefix )
			{
				opportunity.multiQPrologTokenIndices = bestOpportunity.multiQPrologTokenIndices;
				opportunity.multiQMainTokenIndices = bestOpportunity.multiQMainTokenIndices;
				opportunity.multiQCyclicPrefixTokenIndices = bestOpportunity.multiQCyclicPrefixTokenIndices;
				opportunity.multiQCyclicPrefixRotations = bestOpportunity.multiQCyclicPrefixRotations;
				opportunity.multiQCyclicPrefixInsertBeforeTokenIndex =
				    bestOpportunity.multiQCyclicPrefixInsertBeforeTokenIndex;
				opportunity.multiQCyclicPrefixNeedsGuard =
				    bestOpportunity.multiQCyclicPrefixNeedsGuard;
				opportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    bestOpportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot;
				opportunity.drainTokenIndices = bestOpportunity.drainTokenIndices;
			}
			else if( foundFallbackCyclicPrefix )
			{
				assignMultiQPipelineCandidate( *loop,
				                               cyclicPrefixLastConsumerOffset + 1,
				                               cyclicPrefixLastConsumerOffset + 1,
				                               branchOffset,
				                               opportunity );
			}
		}

		classifySoftwarePipelineEmissionSafety( opportunity, *loop, qProducerOffset, indexedTokens );
		classifyMultiQSoftwarePipelineOpportunity( opportunity, qProducerCount, indexedTokens );
		if( opportunity.canEmitMultiQSoftwarePipeline
		    && !opportunity.multiQMainTokenIndices.empty()
		    && !opportunity.multiQCyclicPrefixTokenIndices.empty()
		    && !opportunity.multiQCyclicPrefixNeedsGuard
		    && !loopUsesLoopExtraDirective( *loop, indexedTokens ) )
			classifyMultiQCyclicPrefixSuffixStoreDrains( opportunity,
			                                             *loop,
			                                             indexedTokens );

		if( std::getenv( "OPENVCL_DUMP_PIPELINE_OPPORTUNITIES" ) != NULL )
		{
			std::cerr << "[pipeline-opportunity] loop=" << opportunity.label
			          << " qProducerCount=" << qProducerCount
			          << " qProducerLatency=" << opportunity.qProducerLatency
			          << " gapCycles=" << opportunity.qProducerConsumerGapCycles
			          << " gapDeficit=" << opportunity.qProducerConsumerGapDeficitCycles
			          << " loopCarriedQGap=" << opportunity.loopCarriedQGapCycles
			          << " producerInsertionGap=" << opportunity.qProducerInsertionGapCycles
			          << " producerInsertionDeficit=" << opportunity.qProducerInsertionGapDeficitCycles
			          << " sourcePrefix=" << opportunity.sourcePrefixCycles
			          << " sourceSuffix=" << opportunity.sourceSuffixCycles
			          << " branchDelaySlots=" << opportunity.branchDelaySlots
			          << " simpleCounted=" << ( opportunity.simpleCountedLoop ? 1 : 0 )
			          << " requiresLoopCarried=" << ( opportunity.requiresLoopCarriedRegisters ? 1 : 0 )
			          << " eligibleSingleQ=" << ( opportunity.eligibleSingleQSoftwarePipeline ? 1 : 0 )
			          << " hasSwpPlan=" << ( opportunity.hasSoftwarePipelinePlan ? 1 : 0 )
			          << " canEmitMultiQ=" << ( opportunity.canEmitMultiQSoftwarePipeline ? 1 : 0 )
			          << " multiQNeedsGuard=" << ( opportunity.multiQCyclicPrefixNeedsGuard ? 1 : 0 )
			          << " prologSize=" << opportunity.prologTokenIndices.size()
			          << " mainSize=" << opportunity.mainTokenIndices.size()
			          << " drainSize=" << opportunity.drainTokenIndices.size()
			          << " multiQPrologSize=" << opportunity.multiQPrologTokenIndices.size()
			          << " multiQMainSize=" << opportunity.multiQMainTokenIndices.size()
			          << " multiQCyclicPrefixSize=" << opportunity.multiQCyclicPrefixTokenIndices.size()
			          << "\n";
			if( !opportunity.mainTokenIndices.empty() )
			{
				const unsigned int singleQMainEstCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				std::cerr << "[pipeline-opportunity]   singleQ_main_estimated_cycles="
				          << singleQMainEstCycles << "\n";
				if( !opportunity.prologTokenIndices.empty() )
				{
					const unsigned int singleQMainInContextCycles =
					    scheduledLoopBodyCyclesInContext( opportunity.prologTokenIndices,
					                                      opportunity.mainTokenIndices,
					                                      indexedTokens );
					std::cerr << "[pipeline-opportunity]   singleQ_main_in_context_cycles="
					          << singleQMainInContextCycles << "\n";
				}
			}
			if( !opportunity.multiQMainTokenIndices.empty() )
			{
				const unsigned int multiQMainEstCycles =
				    scheduledLoopBodyCycles( opportunity.multiQMainTokenIndices, indexedTokens );
				std::cerr << "[pipeline-opportunity]   multiQ_main_estimated_cycles="
				          << multiQMainEstCycles << "\n";
				if( !opportunity.multiQPrologTokenIndices.empty() )
				{
					const unsigned int multiQMainInContextCycles =
					    scheduledLoopBodyCyclesInContext( opportunity.multiQPrologTokenIndices,
					                                      opportunity.multiQMainTokenIndices,
					                                      indexedTokens );
					std::cerr << "[pipeline-opportunity]   multiQ_main_in_context_cycles="
					          << multiQMainInContextCycles << "\n";
				}
			}
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_DDG" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 1: extract a dependence DAG over the simple-counted
			// loop body. Nodes = opportunity.mainTokenIndices. Edges encode
			// (kind, dist, latency, resource):
			//   kind  in {RAW, WAW, WAR}; intra-iter dist=0 (i<j), loop-carried dist=1 (i>j)
			//   latency for RAW comes from VuLatencyTracker; WAW/WAR use 1 (ordering only)
			//   resource = base register key (collapsed via registerBaseKey) or
			//              implicit pipe tag (@Q, @P, @ACC, @MAC, @CLIP, @R, @I).
			// Diagnostic-only; planner / emission untouched. Output is gated on
			// OPENVCL_DUMP_LOOP_DDG; per-edge detail additionally needs
			// OPENVCL_DUMP_LOOP_DDG_EDGES to avoid drowning the log on large bodies.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int n = static_cast<unsigned int>( mt.size() );
			std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
			for( unsigned int k = 0; k < n; ++k )
			{
				if( mt[k] >= indexedTokens.size() ) continue;
				VuTokenResourceAccess acc;
				if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
				for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
				     it != acc.registerWrites.end(); ++it )
					nodeWrites[k].push_back( registerBaseKey( *it ) );
				for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
				     it != acc.registerReads.end(); ++it )
					nodeReads[k].push_back( registerBaseKey( *it ) );
				const unsigned int iw = acc.implicitWrites;
				const unsigned int ir = acc.implicitReads;
				if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
				if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
				if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
				if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
				if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
				if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
				if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
				if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
				if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
				if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
				if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
				if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
				if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
				if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
			}
			const bool dumpEdges = ( std::getenv( "OPENVCL_DUMP_LOOP_DDG_EDGES" ) != NULL );
			unsigned int edges = 0, intra = 0, carried = 0;
			unsigned int maxIntraLat = 0, maxCarriedLat = 0;
			// Track 9.G step 2: collect edges into parallel arrays so we can
			// compute RecMII (max cycle ratio of lat/dist) once enumeration is done.
			std::vector<unsigned int> eFrom, eTo, eDist;
			std::vector<int> eLat;
			for( unsigned int i = 0; i < n; ++i )
			{
				for( unsigned int j = 0; j < n; ++j )
				{
					if( i == j ) continue;
					const unsigned int dist = ( i < j ) ? 0u : 1u;
					std::string sharedRaw;
					for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
							if( nodeWrites[i][a] == nodeReads[j][b] )
								sharedRaw = nodeWrites[i][a];
					if( !sharedRaw.empty()
					    && mt[i] < indexedTokens.size()
					    && mt[j] < indexedTokens.size() )
					{
						VuLatencyTracker tr;
						tr.reset();
						tr.recordWrites( *indexedTokens[mt[i]], 0 );
						const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
						const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
						if( dumpEdges )
							std::cerr << "[loop-ddg-edge] loop=" << opportunity.label
							          << " i=" << mt[i] << " j=" << mt[j]
							          << " dist=" << dist
							          << " kind=RAW lat=" << lat
							          << " res=" << sharedRaw << "\n";
						++edges;
						if( dist == 0 ) { ++intra;   if( lat > maxIntraLat   ) maxIntraLat   = lat; }
						else            { ++carried; if( lat > maxCarriedLat ) maxCarriedLat = lat; }
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( static_cast<int>( lat ) );
					}
					std::string sharedWaw;
					for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
							if( nodeWrites[i][a] == nodeWrites[j][b] )
								sharedWaw = nodeWrites[i][a];
					if( !sharedWaw.empty() )
					{
						if( dumpEdges )
							std::cerr << "[loop-ddg-edge] loop=" << opportunity.label
							          << " i=" << mt[i] << " j=" << mt[j]
							          << " dist=" << dist
							          << " kind=WAW lat=1"
							          << " res=" << sharedWaw << "\n";
						++edges;
						if( dist == 0 ) ++intra; else ++carried;
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( 1 );
					}
					std::string sharedWar;
					for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
						for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
							if( nodeReads[i][a] == nodeWrites[j][b] )
								sharedWar = nodeReads[i][a];
					if( !sharedWar.empty() )
					{
						if( dumpEdges )
							std::cerr << "[loop-ddg-edge] loop=" << opportunity.label
							          << " i=" << mt[i] << " j=" << mt[j]
							          << " dist=" << dist
							          << " kind=WAR lat=1"
							          << " res=" << sharedWar << "\n";
						++edges;
						if( dist == 0 ) ++intra; else ++carried;
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( 1 );
					}
				}
			}
			std::cerr << "[loop-ddg] loop=" << opportunity.label
			          << " mainSize=" << n
			          << " edges=" << edges
			          << " intra=" << intra
			          << " carried=" << carried
			          << " maxIntraLat=" << maxIntraLat
			          << " maxCarriedLat=" << maxCarriedLat
			          << "\n";

			// Track 9.G step 2: RecMII = max over cycles C of sum(lat)/sum(dist).
			// dist in {0,1}; only cycles with at least one dist=1 edge are finite.
			// Solve via binary search on lambda: a positive cycle in the reweighted
			// graph w(e) = lat(e) - lambda*dist(e) exists iff max cycle ratio > lambda.
			// Use Bellman-Ford longest-path detection (init d[v]=0 for all v, relax
			// n times, then test for one more relaxation).
			if( n > 0 && carried > 0 && !eFrom.empty() )
			{
				double hi = 0.0;
				for( unsigned int e = 0; e < eLat.size(); ++e ) hi += (double)eLat[e];
				if( hi < 1.0 ) hi = 1.0;
				double lo = 0.0;
				const unsigned int E = static_cast<unsigned int>( eFrom.size() );
				std::vector<double> d( n, 0.0 );
				for( int iter = 0; iter < 60; ++iter )
				{
					const double mid = 0.5 * ( lo + hi );
					for( unsigned int v = 0; v < n; ++v ) d[v] = 0.0;
					// Relax n times.
					for( unsigned int pass = 0; pass < n; ++pass )
					{
						bool changed = false;
						for( unsigned int e = 0; e < E; ++e )
						{
							const double w = (double)eLat[e] - mid * (double)eDist[e];
							const double nd = d[ eFrom[e] ] + w;
							if( nd > d[ eTo[e] ] + 1e-12 )
							{
								d[ eTo[e] ] = nd;
								changed = true;
							}
						}
						if( !changed ) break;
					}
					// One more pass: if anything still relaxes, positive cycle exists.
					bool positive = false;
					for( unsigned int e = 0; e < E && !positive; ++e )
					{
						const double w = (double)eLat[e] - mid * (double)eDist[e];
						if( d[ eFrom[e] ] + w > d[ eTo[e] ] + 1e-9 )
							positive = true;
					}
					if( positive ) lo = mid;
					else           hi = mid;
				}
				const double recmiiFract = lo;
				unsigned int recmiiInt = static_cast<unsigned int>( recmiiFract );
				if( (double)recmiiInt + 1e-6 < recmiiFract ) ++recmiiInt;
				if( recmiiInt < 1 ) recmiiInt = 1;
				std::cerr << "[loop-recmii] loop=" << opportunity.label
				          << " recmii_int=" << recmiiInt
				          << " recmii_fract=" << recmiiFract
				          << "\n";
			}
			else
			{
				std::cerr << "[loop-recmii] loop=" << opportunity.label
				          << " recmii_int=1 recmii_fract=0 (no carried edges)\n";
			}
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_RESMII" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 3: resource MII (ResMII) per VU execution pipe.
			// Pipeline model (PS2 VU first-order):
			//   * 1 UPPER issue / cycle  — VU_PIPE_UPPER (FMAC ops)
			//   * 1 LOWER issue / cycle  — VU_PIPE_LOWER (LSU/IALU/BRU/RANDU
			//                              plus FDIV/EFU which dispatch from
			//                              the lower slot)
			//   * FDIV unit non-pipelined: each FDIV op occupies it for
			//                              info->throughput cycles
			//   * EFU  unit non-pipelined: same model, info->throughput cycles
			// NOPs and waitq/waitp are excluded from issue counts.
			//
			// ResMII = max( nUpper, nLower, sum_FDIV(throughput),
			//               sum_EFU(throughput) )
			// MII   = max( RecMII, ResMII ); the per-iter cycle count of any
			//         valid modulo schedule.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			unsigned int nUpper = 0, nLower = 0, nNop = 0;
			unsigned int fdivBusy = 0, efuBusy = 0;
			unsigned int nFmac = 0, nLsu = 0, nIalu = 0, nBru = 0;
			unsigned int nFdiv = 0, nEfu = 0, nRandu = 0;
			for( unsigned int k = 0; k < mt.size(); ++k )
			{
				if( mt[k] >= indexedTokens.size() ) continue;
				const Token& tk = *indexedTokens[mt[k]];
				if( !tk.operand() ) continue;
				const VuInstructionInfo* info =
				    findVuInstructionInfo( normalizeVuMnemonic( tk.operand()->name() ) );
				if( !info ) continue;
				if( info->pipe == VU_PIPE_NOP ) { ++nNop; continue; }
				if( info->pipe == VU_PIPE_UPPER ) ++nUpper;
				else if( info->pipe == VU_PIPE_LOWER ) ++nLower;
				switch( info->unit )
				{
				case VU_EXEC_FMAC:  ++nFmac;  break;
				case VU_EXEC_LSU:   ++nLsu;   break;
				case VU_EXEC_IALU:  ++nIalu;  break;
				case VU_EXEC_BRU:   ++nBru;   break;
				case VU_EXEC_RANDU: ++nRandu; break;
				case VU_EXEC_FDIV:  ++nFdiv; fdivBusy += info->throughput; break;
				case VU_EXEC_EFU:   ++nEfu;  efuBusy  += info->throughput; break;
				default: break;
				}
			}
			unsigned int resmii = nUpper;
			if( nLower    > resmii ) resmii = nLower;
			if( fdivBusy  > resmii ) resmii = fdivBusy;
			if( efuBusy   > resmii ) resmii = efuBusy;
			if( resmii < 1 ) resmii = 1;
			std::cerr << "[loop-resmii] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " nUpper=" << nUpper
			          << " nLower=" << nLower
			          << " fdivBusy=" << fdivBusy
			          << " efuBusy=" << efuBusy
			          << " nop=" << nNop
			          << " fmac=" << nFmac
			          << " lsu=" << nLsu
			          << " ialu=" << nIalu
			          << " bru=" << nBru
			          << " randu=" << nRandu
			          << " fdiv=" << nFdiv
			          << " efu=" << nEfu
			          << " resmii=" << resmii
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_MII" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4a: combined Minimum Initiation Interval.
			// MII = max(RecMII, ResMII) is the lower bound on any valid modulo
			// schedule's II. RecMII (recurrence-bound) is computed via the same
			// Bellman-Ford max-cycle-ratio algorithm used by OPENVCL_DUMP_LOOP_DDG;
			// ResMII (resource-bound) is the per-pipe count used by
			// OPENVCL_DUMP_LOOP_RESMII. Step 4b+ will consume this MII as the
			// starting II for iterative modulo scheduling.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			std::cerr << "[loop-mii] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " recmii=" << recmii
			          << " resmii=" << resmii
			          << " mii=" << mii
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_MRT" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4b: Modulo Reservation Table scaffolding.
			// Construct an empty MRT sized at the current MII and report its
			// dimensions. Step 4d will drive reservations from the priority
			// list; for now this only validates that the resource model
			// (Upper/Lower issue lanes + FDIV/EFU multi-cycle pipes) wires
			// through the build and can be sized per loop without affecting
			// emission.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			ModuloReservationTable mrt( mii );
			std::cerr << "[loop-mrt] loop=" << opportunity.label
			          << " II=" << mrt.initiationInterval()
			          << " upperCap=" << mrt.initiationInterval()
			          << " lowerCap=" << mrt.initiationInterval()
			          << " fdivLanes=1"
			          << " efuLanes=1"
			          << " upperOcc=" << mrt.upperOccupancy()
			          << " lowerOcc=" << mrt.lowerOccupancy()
			          << " fdivOcc=" << mrt.fdivOccupancy()
			          << " efuOcc=" << mrt.efuOccupancy()
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_PRIORITY" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4c: node priority for iterative modulo scheduling.
			// Compute ASAP/ALAP/height/mobility over the intra DDG and print
			// an aggregate summary plus the priority order. Per-node detail
			// behind OPENVCL_DUMP_LOOP_PRIORITY_NODES to avoid flooding the
			// log on large bodies. Step 4d will consume this ordering to
			// drive insertion into the Modulo Reservation Table.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			LoopPriorityResult pr;
			computeLoopPriority( mt, indexedTokens, mii, pr );
			unsigned int maxHeight = 0, maxMobility = 0, maxAsap = 0, maxAlap = 0;
			for( unsigned int i = 0; i < pr.height.size(); ++i )
			{
				if( pr.height[i]   > maxHeight )   maxHeight   = pr.height[i];
				if( pr.mobility[i] > maxMobility ) maxMobility = pr.mobility[i];
				if( pr.asap[i]     > maxAsap )     maxAsap     = pr.asap[i];
				if( pr.alap[i]     > maxAlap )     maxAlap     = pr.alap[i];
			}
			std::cerr << "[loop-priority] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " II=" << mii
			          << " scheduleLength=" << pr.scheduleLength
			          << " maxHeight=" << maxHeight
			          << " maxMobility=" << maxMobility
			          << " maxAsap=" << maxAsap
			          << " maxAlap=" << maxAlap
			          << "\n";
			if( std::getenv( "OPENVCL_DUMP_LOOP_PRIORITY_NODES" ) != NULL )
			{
				for( unsigned int rank = 0; rank < pr.order.size(); ++rank )
				{
					const unsigned int i = pr.order[rank];
					std::cerr << "[loop-priority-node] loop=" << opportunity.label
					          << " rank=" << rank
					          << " node=" << i
					          << " token=" << mt[i]
					          << " height=" << pr.height[i]
					          << " mobility=" << pr.mobility[i]
					          << " asap=" << pr.asap[i]
					          << " alap=" << pr.alap[i]
					          << "\n";
				}
			}
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_SCHEDULE" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4d: iterative modulo placement (diagnostic).
			// Drive the priority order into the Modulo Reservation Table.
			// For each node, classify its pipe (Upper / Lower / FDIV / EFU)
			// from VuInstructionInfo and try slots in [asap, alap]; if no
			// slot fits, extend up to scheduleLength. Record (slot, stage)
			// per node. Planner/emission still untouched — this validates
			// that the MRT can absorb a full loop body at the current MII
			// using only intra-iteration constraints. Loop-carried hazards
			// are out of scope here (RecMII already bounds II).
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			LoopPriorityResult pr;
			computeLoopPriority( mt, indexedTokens, mii, pr );
			const unsigned int n = static_cast<unsigned int>( mt.size() );
			std::vector<unsigned int> slotOf( n, static_cast<unsigned int>( -1 ) );
			std::vector<unsigned int> stageOf( n, 0 );
			std::vector<int>          pipeOf( n, 0 ); // 0=none 1=U 2=L 3=FDIV 4=EFU
			std::vector<unsigned int> durationOf( n, 1 );
			std::vector<bool>         placed( n, false );
			unsigned int placedCount = 0, failedCount = 0, maxStage = 0;
			// Track 9.G step 4f: II-bumping retry. If placement leaves any node
			// failed at the start II, bump II by one and retry until either
			// every node is placed or we hit a safety cap.
			const unsigned int miiStart = mii;
			const unsigned int iiCap    = miiStart + 32;
			unsigned int tryII = miiStart;
			unsigned int bumps = 0;
			unsigned int evictions = 0, recoveries = 0;
			unsigned int upperOccFinal = 0, lowerOccFinal = 0;
			unsigned int fdivOccFinal  = 0, efuOccFinal   = 0;

			// Track 9.G step 4e: build the full DDG (intra dist=0, loop-carried
			// dist=1) so the placer can enforce cross-iteration RAW/WAW/WAR
			// feasibility. Constraint per edge (a->b, lat L, dist d):
			//     slot[b] - slot[a] >= L - d*II
			// When the predecessor is already placed and we trial slot s for b,
			// reject s if s < slot[a] + L - d*II. Symmetric check when b is
			// placed and a is being trialled.
			std::vector< std::vector<std::string> > nodeWritesP( n ), nodeReadsP( n );
			for( unsigned int k = 0; k < n; ++k )
			{
				if( mt[k] >= indexedTokens.size() ) continue;
				VuTokenResourceAccess acc;
				if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
				for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
				     it != acc.registerWrites.end(); ++it )
					nodeWritesP[k].push_back( registerBaseKey( *it ) );
				for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
				     it != acc.registerReads.end(); ++it )
					nodeReadsP[k].push_back( registerBaseKey( *it ) );
				const unsigned int iw = acc.implicitWrites;
				const unsigned int ir = acc.implicitReads;
				if( iw & VU_RESOURCE_ACC )  nodeWritesP[k].push_back( "@ACC" );
				if( iw & VU_RESOURCE_Q )    nodeWritesP[k].push_back( "@Q" );
				if( iw & VU_RESOURCE_P )    nodeWritesP[k].push_back( "@P" );
				if( iw & VU_RESOURCE_R )    nodeWritesP[k].push_back( "@R" );
				if( iw & VU_RESOURCE_I )    nodeWritesP[k].push_back( "@I" );
				if( iw & VU_RESOURCE_MAC )  nodeWritesP[k].push_back( "@MAC" );
				if( iw & VU_RESOURCE_CLIP ) nodeWritesP[k].push_back( "@CLIP" );
				if( ir & VU_RESOURCE_ACC )  nodeReadsP[k].push_back( "@ACC" );
				if( ir & VU_RESOURCE_Q )    nodeReadsP[k].push_back( "@Q" );
				if( ir & VU_RESOURCE_P )    nodeReadsP[k].push_back( "@P" );
				if( ir & VU_RESOURCE_R )    nodeReadsP[k].push_back( "@R" );
				if( ir & VU_RESOURCE_I )    nodeReadsP[k].push_back( "@I" );
				if( ir & VU_RESOURCE_MAC )  nodeReadsP[k].push_back( "@MAC" );
				if( ir & VU_RESOURCE_CLIP ) nodeReadsP[k].push_back( "@CLIP" );
			}
			std::vector<unsigned int> dFrom, dTo, dDist, dLat;
			for( unsigned int i = 0; i < n; ++i )
			{
				for( unsigned int j = 0; j < n; ++j )
				{
					if( i == j ) continue;
					const unsigned int dist = ( i < j ) ? 0u : 1u;
					std::string sharedRaw;
					for( unsigned int a = 0; a < nodeWritesP[i].size() && sharedRaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeReadsP[j].size() && sharedRaw.empty(); ++b )
							if( nodeWritesP[i][a] == nodeReadsP[j][b] )
								sharedRaw = nodeWritesP[i][a];
					if( !sharedRaw.empty()
					    && mt[i] < indexedTokens.size()
					    && mt[j] < indexedTokens.size() )
					{
						VuLatencyTracker tr;
						tr.reset();
						tr.recordWrites( *indexedTokens[mt[i]], 0 );
						const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
						const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
						dFrom.push_back( i ); dTo.push_back( j );
						dDist.push_back( dist ); dLat.push_back( lat );
					}
					std::string sharedWaw;
					for( unsigned int a = 0; a < nodeWritesP[i].size() && sharedWaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeWritesP[j].size() && sharedWaw.empty(); ++b )
							if( nodeWritesP[i][a] == nodeWritesP[j][b] )
								sharedWaw = nodeWritesP[i][a];
					if( !sharedWaw.empty() )
					{
						dFrom.push_back( i ); dTo.push_back( j );
						dDist.push_back( dist ); dLat.push_back( 1 );
					}
					std::string sharedWar;
					for( unsigned int a = 0; a < nodeReadsP[i].size() && sharedWar.empty(); ++a )
						for( unsigned int b = 0; b < nodeWritesP[j].size() && sharedWar.empty(); ++b )
							if( nodeReadsP[i][a] == nodeWritesP[j][b] )
								sharedWar = nodeReadsP[i][a];
					if( !sharedWar.empty() )
					{
						dFrom.push_back( i ); dTo.push_back( j );
						dDist.push_back( dist ); dLat.push_back( 1 );
					}
				}
			}
			const unsigned int totalEdges = static_cast<unsigned int>( dFrom.size() );
			unsigned int edgeViolations = 0;
			while( true )
			{
				ModuloReservationTable mrt( tryII );
				for( unsigned int k = 0; k < n; ++k )
				{
					slotOf[k]  = static_cast<unsigned int>( -1 );
					stageOf[k] = 0;
					pipeOf[k]  = 0;
					durationOf[k] = 1;
					placed[k]  = false;
				}
				placedCount = 0; failedCount = 0; maxStage = 0;
				edgeViolations = 0;
				evictions = 0; recoveries = 0;
			for( unsigned int rank = 0; rank < pr.order.size(); ++rank )
			{
				const unsigned int i = pr.order[rank];
				const unsigned int tokIdx = mt[i];
				if( tokIdx >= indexedTokens.size() ) { ++failedCount; continue; }
				const Token& tk = *indexedTokens[tokIdx];
				if( !tk.operand() ) { ++failedCount; continue; }
				const VuInstructionInfo* info =
				    findVuInstructionInfo( normalizeVuMnemonic( tk.operand()->name() ) );
				int pipeKind = 0;
				unsigned int duration = 1;
				if( info )
				{
					if( info->unit == VU_EXEC_FDIV )
					{
						pipeKind = 3;
						duration = info->throughput > 0 ? info->throughput : 1;
					}
					else if( info->unit == VU_EXEC_EFU )
					{
						pipeKind = 4;
						duration = info->throughput > 0 ? info->throughput : 1;
					}
					else if( info->pipe == VU_PIPE_UPPER )
					{
						pipeKind = 1;
					}
					else if( info->pipe == VU_PIPE_LOWER )
					{
						pipeKind = 2;
					}
				}
				pipeOf[i] = pipeKind;
				durationOf[i] = duration;
				if( pipeKind == 0 )
				{
					// No resource modeled (NOP / unknown). Treat as placed at ASAP.
					slotOf[i]  = pr.asap[i];
					stageOf[i] = tryII > 0 ? ( slotOf[i] / tryII ) : 0;
					placed[i]  = true;
					++placedCount;
					if( stageOf[i] > maxStage ) maxStage = stageOf[i];
					continue;
				}
				const unsigned int lo = pr.asap[i];
				unsigned int hi = pr.alap[i];
				if( hi < lo ) hi = lo;
				if( hi < pr.scheduleLength ) hi = pr.scheduleLength;
				if( hi < lo + tryII ) hi = lo + tryII; // ensure full mod ring is exercised
				bool ok = false;
				for( unsigned int s = lo; s <= hi && !ok; ++s )
				{
					// Track 9.G step 4e: edge feasibility against placed nodes.
					// For each edge touching i where the other end is already
					// placed, require slot[to] - slot[from] >= lat - dist*II.
					// Use signed int math (II*dist can exceed lat).
					bool edgeOk = true;
					for( unsigned int e = 0; e < totalEdges && edgeOk; ++e )
					{
						const int Lat = static_cast<int>( dLat[e] );
						const int Dii = static_cast<int>( dDist[e] ) * static_cast<int>( tryII );
						if( dTo[e] == i && placed[ dFrom[e] ] )
						{
							const int from = static_cast<int>( slotOf[ dFrom[e] ] );
							if( static_cast<int>( s ) - from < Lat - Dii )
								edgeOk = false;
						}
						else if( dFrom[e] == i && placed[ dTo[e] ] )
						{
							const int to = static_cast<int>( slotOf[ dTo[e] ] );
							if( to - static_cast<int>( s ) < Lat - Dii )
								edgeOk = false;
						}
					}
					if( !edgeOk ) { ++edgeViolations; continue; }
					const unsigned int mod = tryII > 0 ? ( s % tryII ) : 0;
					bool canPlace = false;
					switch( pipeKind )
					{
					case 1: canPlace = mrt.canReserveUpper( mod ); break;
					case 2: canPlace = mrt.canReserveLower( mod ); break;
					case 3: canPlace = mrt.canReserveFdiv( mod, duration ); break;
					case 4: canPlace = mrt.canReserveEfu( mod, duration ); break;
					default: break;
					}
					if( !canPlace ) continue;
					switch( pipeKind )
					{
					case 1: mrt.reserveUpper( mod, tokIdx ); break;
					case 2: mrt.reserveLower( mod, tokIdx ); break;
					case 3: mrt.reserveFdiv( mod, duration, tokIdx ); break;
					case 4: mrt.reserveEfu( mod, duration, tokIdx ); break;
					default: break;
					}
					slotOf[i]  = s;
					stageOf[i] = tryII > 0 ? ( s / tryII ) : 0;
					placed[i]  = true;
					if( stageOf[i] > maxStage ) maxStage = stageOf[i];
					ok = true;
				}
				if( ok ) ++placedCount;
				else     ++failedCount;
			}
				upperOccFinal = mrt.upperOccupancy();
				lowerOccFinal = mrt.lowerOccupancy();
				fdivOccFinal  = mrt.fdivOccupancy();
				efuOccFinal   = mrt.efuOccupancy();
				if( failedCount == 0 ) break;
				// Track 9.G step 4g: bounded backtracking. For each failed
				// node f, try evicting a single already-placed node b that
				// occupies a same-pipe MRT slot in f's [asap..hi] range. If
				// f then fits, attempt to re-place b anywhere in b's range.
				// Accept the swap when both succeed; otherwise revert.
				const unsigned int evictionBudget = n;
				unsigned int spent = 0;
				for( unsigned int rank = 0; rank < pr.order.size() && spent < evictionBudget; ++rank )
				{
					const unsigned int f = pr.order[rank];
					if( placed[f] ) continue;
					if( pipeOf[f] == 0 ) continue;
					const unsigned int loF = pr.asap[f];
					unsigned int hiF = pr.alap[f];
					if( hiF < loF ) hiF = loF;
					if( hiF < pr.scheduleLength ) hiF = pr.scheduleLength;
					if( hiF < loF + tryII ) hiF = loF + tryII;
					bool swapped = false;
					for( unsigned int rb = 0; rb < pr.order.size() && !swapped; ++rb )
					{
						const unsigned int b = pr.order[rb];
						if( b == f ) continue;
						if( !placed[b] ) continue;
						if( pipeOf[b] != pipeOf[f] ) continue;
						const unsigned int slotB = slotOf[b];
						const unsigned int durB  = durationOf[b];
						const int          pipeB = pipeOf[b];
						++spent; ++evictions;
						// Evict b
						switch( pipeB )
						{
						case 1: mrt.releaseUpper( slotB % tryII ); break;
						case 2: mrt.releaseLower( slotB % tryII ); break;
						case 3: mrt.releaseFdiv ( slotB % tryII, durB ); break;
						case 4: mrt.releaseEfu  ( slotB % tryII, durB ); break;
						default: break;
						}
						placed[b] = false;
						const unsigned int savedSlotB = slotB;
						// Try to place f at any slot in [loF,hiF]
						bool fPlaced = false;
						for( unsigned int s = loF; s <= hiF && !fPlaced; ++s )
						{
							bool edgeOk = true;
							for( unsigned int e = 0; e < totalEdges && edgeOk; ++e )
							{
								const int Lat = static_cast<int>( dLat[e] );
								const int Dii = static_cast<int>( dDist[e] ) * static_cast<int>( tryII );
								if( dTo[e] == f && placed[ dFrom[e] ] )
								{
									const int from = static_cast<int>( slotOf[ dFrom[e] ] );
									if( static_cast<int>( s ) - from < Lat - Dii ) edgeOk = false;
								}
								else if( dFrom[e] == f && placed[ dTo[e] ] )
								{
									const int to = static_cast<int>( slotOf[ dTo[e] ] );
									if( to - static_cast<int>( s ) < Lat - Dii ) edgeOk = false;
								}
							}
							if( !edgeOk ) continue;
							const unsigned int mod = s % tryII;
							bool canP = false;
							switch( pipeOf[f] )
							{
							case 1: canP = mrt.canReserveUpper( mod ); break;
							case 2: canP = mrt.canReserveLower( mod ); break;
							case 3: canP = mrt.canReserveFdiv( mod, durationOf[f] ); break;
							case 4: canP = mrt.canReserveEfu( mod, durationOf[f] ); break;
							default: break;
							}
							if( !canP ) continue;
							switch( pipeOf[f] )
							{
							case 1: mrt.reserveUpper( mod, mt[f] ); break;
							case 2: mrt.reserveLower( mod, mt[f] ); break;
							case 3: mrt.reserveFdiv( mod, durationOf[f], mt[f] ); break;
							case 4: mrt.reserveEfu( mod, durationOf[f], mt[f] ); break;
							default: break;
							}
							slotOf[f] = s;
							stageOf[f] = tryII > 0 ? ( s / tryII ) : 0;
							placed[f] = true;
							fPlaced = true;
						}
						if( !fPlaced )
						{
							// Restore b at its original slot.
							switch( pipeB )
							{
							case 1: mrt.reserveUpper( savedSlotB % tryII, mt[b] ); break;
							case 2: mrt.reserveLower( savedSlotB % tryII, mt[b] ); break;
							case 3: mrt.reserveFdiv( savedSlotB % tryII, durB, mt[b] ); break;
							case 4: mrt.reserveEfu ( savedSlotB % tryII, durB, mt[b] ); break;
							default: break;
							}
							placed[b] = true;
							continue;
						}
						// Try to re-place b somewhere in its range.
						const unsigned int loB = pr.asap[b];
						unsigned int hiB = pr.alap[b];
						if( hiB < loB ) hiB = loB;
						if( hiB < pr.scheduleLength ) hiB = pr.scheduleLength;
						if( hiB < loB + tryII ) hiB = loB + tryII;
						bool bPlaced = false;
						for( unsigned int s = loB; s <= hiB && !bPlaced; ++s )
						{
							bool edgeOk = true;
							for( unsigned int e = 0; e < totalEdges && edgeOk; ++e )
							{
								const int Lat = static_cast<int>( dLat[e] );
								const int Dii = static_cast<int>( dDist[e] ) * static_cast<int>( tryII );
								if( dTo[e] == b && placed[ dFrom[e] ] )
								{
									const int from = static_cast<int>( slotOf[ dFrom[e] ] );
									if( static_cast<int>( s ) - from < Lat - Dii ) edgeOk = false;
								}
								else if( dFrom[e] == b && placed[ dTo[e] ] )
								{
									const int to = static_cast<int>( slotOf[ dTo[e] ] );
									if( to - static_cast<int>( s ) < Lat - Dii ) edgeOk = false;
								}
							}
							if( !edgeOk ) continue;
							const unsigned int mod = s % tryII;
							bool canP = false;
							switch( pipeB )
							{
							case 1: canP = mrt.canReserveUpper( mod ); break;
							case 2: canP = mrt.canReserveLower( mod ); break;
							case 3: canP = mrt.canReserveFdiv( mod, durB ); break;
							case 4: canP = mrt.canReserveEfu( mod, durB ); break;
							default: break;
							}
							if( !canP ) continue;
							switch( pipeB )
							{
							case 1: mrt.reserveUpper( mod, mt[b] ); break;
							case 2: mrt.reserveLower( mod, mt[b] ); break;
							case 3: mrt.reserveFdiv( mod, durB, mt[b] ); break;
							case 4: mrt.reserveEfu( mod, durB, mt[b] ); break;
							default: break;
							}
							slotOf[b] = s;
							stageOf[b] = tryII > 0 ? ( s / tryII ) : 0;
							placed[b] = true;
							bPlaced = true;
						}
						if( bPlaced )
						{
							// Swap succeeded.
							++placedCount;
							--failedCount;
							++recoveries;
							if( stageOf[f] > maxStage ) maxStage = stageOf[f];
							if( stageOf[b] > maxStage ) maxStage = stageOf[b];
							swapped = true;
						}
						else
						{
							// Revert: evict f, restore b.
							switch( pipeOf[f] )
							{
							case 1: mrt.releaseUpper( slotOf[f] % tryII ); break;
							case 2: mrt.releaseLower( slotOf[f] % tryII ); break;
							case 3: mrt.releaseFdiv( slotOf[f] % tryII, durationOf[f] ); break;
							case 4: mrt.releaseEfu ( slotOf[f] % tryII, durationOf[f] ); break;
							default: break;
							}
							placed[f] = false;
							slotOf[f] = static_cast<unsigned int>( -1 );
							stageOf[f] = 0;
							switch( pipeB )
							{
							case 1: mrt.reserveUpper( savedSlotB % tryII, mt[b] ); break;
							case 2: mrt.reserveLower( savedSlotB % tryII, mt[b] ); break;
							case 3: mrt.reserveFdiv( savedSlotB % tryII, durB, mt[b] ); break;
							case 4: mrt.reserveEfu ( savedSlotB % tryII, durB, mt[b] ); break;
							default: break;
							}
							placed[b] = true;
						}
					}
				}
				upperOccFinal = mrt.upperOccupancy();
				lowerOccFinal = mrt.lowerOccupancy();
				fdivOccFinal  = mrt.fdivOccupancy();
				efuOccFinal   = mrt.efuOccupancy();
				if( failedCount == 0 ) break;
				if( tryII >= iiCap ) break;
				++tryII; ++bumps;
			}
			// Track 9.G step 5a: structured kernel layout handoff. Populate a
			// VuKernelLayout from the final placer state. The struct mirrors
			// what a stage-aware emitter will need to consume (stage, modSlot,
			// pipe, duration). Dumped under OPENVCL_DUMP_KERNEL_LAYOUT and
			// (per-entry) OPENVCL_DUMP_KERNEL_LAYOUT_ENTRIES.
			if( std::getenv( "OPENVCL_DUMP_KERNEL_LAYOUT" ) != NULL )
			{
				VuKernelLayout layout;
				layout.II         = tryII;
				layout.miiStart   = miiStart;
				layout.bumps      = bumps;
				layout.feasible   = ( failedCount == 0 );
				layout.stageCount = layout.feasible ? ( maxStage + 1 ) : 0;
				for( unsigned int k = 0; k < n; ++k )
				{
					if( !placed[k] ) continue;
					VuKernelLayoutEntry ent;
					ent.nodeIndex  = k;
					ent.tokenIndex = mt[k];
					ent.slot       = slotOf[k];
					ent.stage      = stageOf[k];
					ent.modSlot    = tryII > 0 ? ( slotOf[k] % tryII ) : 0;
					ent.pipe       = pipeOf[k];
					ent.duration   = durationOf[k];
					layout.entries.push_back( ent );
				}
				// std::sort is allowed (C++98); the comparator is a free
				// function in the VuKernelLayout TU.
				std::sort( layout.entries.begin(), layout.entries.end(),
				           vuKernelLayoutEntryLess );
				std::cerr << "[kernel-layout] loop=" << opportunity.label
				          << " II=" << layout.II
				          << " miiStart=" << layout.miiStart
				          << " bumps=" << layout.bumps
				          << " stageCount=" << layout.stageCount
				          << " feasible=" << ( layout.feasible ? 1 : 0 )
				          << " entries=" << layout.entries.size()
				          << "\n";
				if( std::getenv( "OPENVCL_DUMP_KERNEL_LAYOUT_ENTRIES" ) != NULL )
				{
					for( unsigned int e = 0; e < layout.entries.size(); ++e )
					{
						const VuKernelLayoutEntry& ent = layout.entries[e];
						const char* pname = "none";
						switch( ent.pipe )
						{
						case 1: pname = "upper"; break;
						case 2: pname = "lower"; break;
						case 3: pname = "fdiv";  break;
						case 4: pname = "efu";   break;
						default: break;
						}
						std::cerr << "[kernel-entry] loop=" << opportunity.label
						          << " idx=" << e
						          << " node=" << ent.nodeIndex
						          << " token=" << ent.tokenIndex
						          << " stage=" << ent.stage
						          << " modSlot=" << ent.modSlot
						          << " slot=" << ent.slot
						          << " pipe=" << pname
						          << " duration=" << ent.duration
						          << "\n";
					}
				}
			}
			std::cerr << "[loop-schedule] loop=" << opportunity.label
			          << " II=" << tryII
			          << " miiStart=" << miiStart
			          << " bumps=" << bumps
			          << " placed=" << placedCount
			          << " failed=" << failedCount
			          << " maxStage=" << maxStage
			          << " edges=" << totalEdges
			          << " edgeViolations=" << edgeViolations
			          << " evictions=" << evictions
			          << " recoveries=" << recoveries
			          << " upperOcc=" << upperOccFinal
			          << " lowerOcc=" << lowerOccFinal
			          << " fdivOcc=" << fdivOccFinal
			          << " efuOcc=" << efuOccFinal
			          << "\n";
			if( std::getenv( "OPENVCL_DUMP_LOOP_SCHEDULE_NODES" ) != NULL )
			{
				for( unsigned int rank = 0; rank < pr.order.size(); ++rank )
				{
					const unsigned int i = pr.order[rank];
					const char* pname = "none";
					switch( pipeOf[i] )
					{
					case 1: pname = "upper"; break;
					case 2: pname = "lower"; break;
					case 3: pname = "fdiv";  break;
					case 4: pname = "efu";   break;
					default: break;
					}
					std::cerr << "[loop-schedule-node] loop=" << opportunity.label
					          << " rank=" << rank
					          << " node=" << i
					          << " token=" << mt[i]
					          << " pipe=" << pname
					          << " slot=" << ( placed[i] ? static_cast<int>( slotOf[i] ) : -1 )
					          << " stage=" << ( placed[i] ? static_cast<int>( stageOf[i] ) : -1 )
					          << " status=" << ( placed[i] ? "placed" : "failed" )
					          << "\n";
				}
			}
		}

		if( std::getenv( "OPENVCL_DUMP_MULTISTAGE_CANDIDATES" ) != NULL )
		{
			std::cerr << "[multistage] loop=" << opportunity.label
			          << " stageCount=" << opportunity.stageCount
			          << " kernelTokens=" << opportunity.kernelTokenIndices.size()
			          << " tokenStageOffsets=" << opportunity.tokenStageOffsets.size()
			          << " rotations=" << opportunity.stageRotationRegisters.size()
			          << "\n";

			// Track 9.E step 2 (cost-only): synthesize a 2-stage kernel candidate
			// and report per-iter cost vs the 1-stage main body. Planner still
			// picks the 1-stage plan; this number informs design of step 3+4.
			if( opportunity.eligibleSingleQSoftwarePipeline )
			{
				const unsigned int singleStageMainCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				unsigned int kernelCycles = 0;
				std::vector<VuSoftwarePipelineRotation> rotations;
				const bool ok = synthesizeTwoStageKernelCycles( *loop,
				                                                indexedTokens,
				                                                opportunity,
				                                                kernelCycles,
				                                                rotations );
				std::cerr << "[multistage-2stage] loop=" << opportunity.label
				          << " singleStageMainCycles=" << singleStageMainCycles
				          << " twoStage=" << ( ok ? "ok" : "fail" )
				          << " kernelCycles=" << kernelCycles
				          << " perIter=" << ( ok ? ( kernelCycles / 2.0 ) : 0.0 )
				          << " rotations=" << rotations.size();
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         rotations.begin(); r != rotations.end(); ++r )
				{
					std::cerr << " " << r->registerBase << "->" << r->scratchRegister;
				}
				std::cerr << "\n";

				// Track 9.E step 3c follow-up (diagnostic only): the existing
				// 2-stage synthesis above is main1 ++ main2_renamed. The cyclic
				// prefix (prolog: mul_pt_mat_44 + div q) is missing from the
				// kernel, so iter2's mulq has no fresh div upstream and the
				// scheduler has nothing new to overlap. Extend the kernel to:
				//   main_iter1 ++ prolog_iter2_renamed ++ main_iter2_renamed
				// This is the smallest shape that gives the scheduler a window
				// to overlap iter N+1's mul_pt_mat_44 + div under iter N's
				// mulq + ftoi4 tail. perIter = kernelCycles / 2.
				if( ok && !opportunity.prologTokenIndices.empty() )
				{
					std::list<Token> extKernelTokens;
					// iter1 main (no rename)
					for( std::vector<unsigned int>::const_iterator i =
					         opportunity.mainTokenIndices.begin();
					     i != opportunity.mainTokenIndices.end(); ++i )
					{
						if( *i < indexedTokens.size() )
							appendUnlabeledTokenForScheduleCost(
							    extKernelTokens, *indexedTokens[*i] );
					}
					// iter2 prolog (renamed via rotation map)
					for( std::vector<unsigned int>::const_iterator i =
					         opportunity.prologTokenIndices.begin();
					     i != opportunity.prologTokenIndices.end(); ++i )
					{
						if( *i < indexedTokens.size() )
							extKernelTokens.push_back(
							    adjustedMultiQCyclicPrefixToken(
							        *indexedTokens[*i], rotations ) );
					}
					// iter2 main (renamed via rotation map)
					for( std::vector<unsigned int>::const_iterator i =
					         opportunity.mainTokenIndices.begin();
					     i != opportunity.mainTokenIndices.end(); ++i )
					{
						if( *i < indexedTokens.size() )
							extKernelTokens.push_back(
							    adjustedMultiQCyclicPrefixToken(
							        *indexedTokens[*i], rotations ) );
					}
					const unsigned int extKernelCycles =
					    scheduleVuProgramReadyIssueSlotsWithFlagLiveness(
					        extKernelTokens ).cycleCount;
					const unsigned int prologAlone =
					    scheduledLoopBodyCycles( opportunity.prologTokenIndices,
					                             indexedTokens );
					// Naive serial: main + prolog + main = singleStage*2 + prolog
					const unsigned int serial =
					    singleStageMainCycles * 2 + prologAlone;
					const unsigned int savings =
					    serial > extKernelCycles ? serial - extKernelCycles : 0;
					std::cerr << "[multistage-2stage-cpfx] loop="
					          << opportunity.label
					          << " singleStageMain=" << singleStageMainCycles
					          << " prologAlone=" << prologAlone
					          << " extKernel=" << extKernelCycles
					          << " perIter=" << ( extKernelCycles / 2.0 )
					          << " serial=" << serial
					          << " savingsVsSerial=" << savings
					          << "\n";
				}
			}
		}

		if( std::getenv( "OPENVCL_DUMP_ROTATION_ALLOCATIONS" ) != NULL )
		{
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.carriedQOutputRegisters.empty() )
			{
				std::vector<VuSoftwarePipelineRotation> bankProbe;
				for( std::list<std::string>::const_iterator reg =
				         opportunity.carriedQOutputRegisters.begin();
				     reg != opportunity.carriedQOutputRegisters.end(); ++reg )
				{
					VuSoftwarePipelineRotation rot;
					rot.registerBase = *reg;
					rot.hasScratchRegister = false;
					bankProbe.push_back( rot );
				}
				assignRotationScratchRegisters( *loop, bankProbe, 2 );
				unsigned int fullDepth2 = 0;
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         bankProbe.begin(); r != bankProbe.end(); ++r )
				{
					if( r->rotationBank.size() >= 2 )
						++fullDepth2;
				}
				std::cerr << "[rotation-alloc] loop=" << opportunity.label
				          << " carried=" << opportunity.carriedQOutputRegisters.size()
				          << " depth2_full=" << fullDepth2;
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         bankProbe.begin(); r != bankProbe.end(); ++r )
				{
					std::cerr << " " << r->registerBase << "->[";
					for( std::vector<std::string>::const_iterator b = r->rotationBank.begin();
					     b != r->rotationBank.end(); ++b )
					{
						if( b != r->rotationBank.begin() )
							std::cerr << ",";
						std::cerr << *b;
					}
					std::cerr << "]";
				}
				std::cerr << "\n";
			}
		}

		if( std::getenv( "OPENVCL_DUMP_QCHAIN_INTERLEAVE" ) != NULL )
		{
			// Track 9.E step 3b (cost-only): interleave stage1 of iter N+1
			// before stage2 of iter N at the source level so the scheduler can
			// overlap the next div/mulax chain with the current mulq/ftoi4
			// chain. Reports per-iter cycle savings vs the 1-stage main body.
			// Planner unchanged.
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.mainTokenIndices.empty() )
			{
				const unsigned int singleStageMainCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				unsigned int perIter = 0;
				unsigned int s1 = 0;
				unsigned int s2 = 0;
				std::vector<VuSoftwarePipelineRotation> rotations;
				std::string reason;
				const bool ok = synthesizeQInterleavedKernelCycles( *loop,
				                                                    indexedTokens,
				                                                    opportunity,
				                                                    perIter,
				                                                    s1,
				                                                    s2,
				                                                    rotations,
				                                                    reason );
				std::cerr << "[qchain-interleave] loop=" << opportunity.label
				          << " singleStageMainCycles=" << singleStageMainCycles
				          << " interleave=" << ( ok ? "ok" : "fail" )
				          << " reason=" << ( ok ? "-" : reason )
				          << " carriedQOutputs=" << opportunity.carriedQOutputRegisters.size()
				          << " qProducerTokenIdx=" << opportunity.qProducerTokenIndex
				          << " qConsumers=" << opportunity.qConsumerTokenIndices.size()
				          << " mainSize=" << opportunity.mainTokenIndices.size()
				          << " stage1Cycles=" << s1
				          << " stage2Cycles=" << s2
				          << " steadyPerIter=" << perIter
				          << " savingsVsSingleStage=" << ( ok && perIter < singleStageMainCycles
				                                            ? ( singleStageMainCycles - perIter )
				                                            : 0 )
				          << " rotations=" << rotations.size();
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         rotations.begin(); r != rotations.end(); ++r )
				{
					std::cerr << " " << r->registerBase << "->[";
					for( std::vector<std::string>::const_iterator b = r->rotationBank.begin();
					     b != r->rotationBank.end(); ++b )
					{
						if( b != r->rotationBank.begin() )
							std::cerr << ",";
						std::cerr << *b;
					}
					std::cerr << "]";
				}
				std::cerr << "\n";
			}
		}

		// Track 9.E step 3c (diagnostic only): identify Chain A (Q-consumer
		// dataflow, forward closure from mulq writes) and Chain B
		// (Q-producer dataflow, backward closure from div q reads) within
		// mainTokenIndices. Reports sizes, overlap and neutral tokens so we
		// can verify the SCEI-style overlap hypothesis on all eligible
		// shaders before designing a chain-aware planner.
		if( std::getenv( "OPENVCL_DUMP_QCHAIN_CHAINS" ) != NULL )
		{
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.mainTokenIndices.empty()
			    && opportunity.qProducerTokenIndex < indexedTokens.size() )
			{
				// Build "in main" set for fast membership.
				std::list<unsigned int> mainSet;
				for( std::vector<unsigned int>::const_iterator m =
				         opportunity.mainTokenIndices.begin();
				     m != opportunity.mainTokenIndices.end(); ++m )
					mainSet.push_back( *m );

				// Chain B: backward closure from div q reads.
				std::list<std::string> chainBKeys;
				collectVuRegisterReadKeys(
				    *indexedTokens[opportunity.qProducerTokenIndex], chainBKeys );
				if( std::getenv( "OPENVCL_DUMP_QCHAIN_CHAINS_KEYS" ) != NULL )
				{
					std::cerr << "  qProducerReads=[";
					bool fk = true;
					for( std::list<std::string>::const_iterator k = chainBKeys.begin();
					     k != chainBKeys.end(); ++k )
					{
						if( !fk ) std::cerr << ",";
						std::cerr << *k;
						fk = false;
					}
					std::cerr << "]\n";
					for( std::vector<unsigned int>::const_iterator m =
					         opportunity.mainTokenIndices.begin();
					     m != opportunity.mainTokenIndices.end(); ++m )
					{
						std::list<std::string> w;
						collectVuRegisterWriteKeys( *indexedTokens[*m], w );
						std::cerr << "  main[" << *m << "]writes=[";
						bool first2 = true;
						for( std::list<std::string>::const_iterator k = w.begin();
						     k != w.end(); ++k )
						{
							if( !first2 ) std::cerr << ",";
							std::cerr << *k;
							first2 = false;
						}
						std::cerr << "]\n";
					}
				}
				std::list<unsigned int> chainB;
				bool changed = true;
				while( changed )
				{
					changed = false;
					for( std::vector<unsigned int>::const_reverse_iterator m =
					         opportunity.mainTokenIndices.rbegin();
					     m != opportunity.mainTokenIndices.rend(); ++m )
					{
						bool already = false;
						for( std::list<unsigned int>::const_iterator c = chainB.begin();
						     c != chainB.end(); ++c )
						{
							if( *c == *m ) { already = true; break; }
						}
						if( already )
							continue;
						std::list<std::string> writes;
						collectVuRegisterWriteKeys( *indexedTokens[*m], writes );
						if( !intersects( writes, chainBKeys ) )
							continue;
						chainB.push_back( *m );
						std::list<std::string> reads;
						collectVuRegisterReadKeys( *indexedTokens[*m], reads );
						for( std::list<std::string>::const_iterator r = reads.begin();
						     r != reads.end(); ++r )
						{
							if( !containsKey( chainBKeys, *r ) )
							{
								chainBKeys.push_back( *r );
								changed = true;
							}
						}
					}
				}

				// Chain A: forward closure from mulq consumer writes.
				std::list<std::string> chainAKeys;
				for( std::vector<unsigned int>::const_iterator q =
				         opportunity.qConsumerTokenIndices.begin();
				     q != opportunity.qConsumerTokenIndices.end(); ++q )
				{
					if( *q >= indexedTokens.size() )
						continue;
					std::list<std::string> writes;
					collectVuRegisterWriteKeys( *indexedTokens[*q], writes );
					for( std::list<std::string>::const_iterator w = writes.begin();
					     w != writes.end(); ++w )
						addUniqueString( chainAKeys, *w );
				}
				std::list<unsigned int> chainA;
				for( std::vector<unsigned int>::const_iterator q =
				         opportunity.qConsumerTokenIndices.begin();
				     q != opportunity.qConsumerTokenIndices.end(); ++q )
					chainA.push_back( *q );
				changed = true;
				while( changed )
				{
					changed = false;
					for( std::vector<unsigned int>::const_iterator m =
					         opportunity.mainTokenIndices.begin();
					     m != opportunity.mainTokenIndices.end(); ++m )
					{
						bool already = false;
						for( std::list<unsigned int>::const_iterator c = chainA.begin();
						     c != chainA.end(); ++c )
						{
							if( *c == *m ) { already = true; break; }
						}
						if( already )
							continue;
						std::list<std::string> reads;
						collectVuRegisterReadKeys( *indexedTokens[*m], reads );
						if( !intersects( reads, chainAKeys ) )
							continue;
						chainA.push_back( *m );
						std::list<std::string> writes;
						collectVuRegisterWriteKeys( *indexedTokens[*m], writes );
						for( std::list<std::string>::const_iterator w = writes.begin();
						     w != writes.end(); ++w )
						{
							if( !containsKey( chainAKeys, *w ) )
							{
								chainAKeys.push_back( *w );
								changed = true;
							}
						}
					}
				}

				// Overlap and neutral.
				unsigned int overlap = 0;
				for( std::list<unsigned int>::const_iterator a = chainA.begin();
				     a != chainA.end(); ++a )
				{
					for( std::list<unsigned int>::const_iterator b = chainB.begin();
					     b != chainB.end(); ++b )
					{
						if( *a == *b ) { ++overlap; break; }
					}
				}
				unsigned int neutral = 0;
				for( std::vector<unsigned int>::const_iterator m =
				         opportunity.mainTokenIndices.begin();
				     m != opportunity.mainTokenIndices.end(); ++m )
				{
					bool inA = false;
					for( std::list<unsigned int>::const_iterator a = chainA.begin();
					     a != chainA.end(); ++a )
					{
						if( *a == *m ) { inA = true; break; }
					}
					bool inB = false;
					for( std::list<unsigned int>::const_iterator b = chainB.begin();
					     b != chainB.end(); ++b )
					{
						if( *b == *m ) { inB = true; break; }
					}
					if( !inA && !inB )
						++neutral;
				}

				std::cerr << "[qchain-chains] loop=" << opportunity.label
				          << " mainSize=" << opportunity.mainTokenIndices.size()
				          << " chainA=" << chainA.size()
				          << " chainB=" << chainB.size()
				          << " overlap=" << overlap
				          << " neutral=" << neutral
				          << " qProducerIdx=" << opportunity.qProducerTokenIndex
				          << " qConsumers=" << opportunity.qConsumerTokenIndices.size();
				std::cerr << " chainA_idx=[";
				bool first = true;
				for( std::list<unsigned int>::const_iterator a = chainA.begin();
				     a != chainA.end(); ++a )
				{
					if( !first ) std::cerr << ",";
					std::cerr << *a;
					first = false;
				}
				std::cerr << "] chainB_idx=[";
				first = true;
				for( std::list<unsigned int>::const_iterator b = chainB.begin();
				     b != chainB.end(); ++b )
				{
					if( !first ) std::cerr << ",";
					std::cerr << *b;
					first = false;
				}
				std::cerr << "]\n";
			}
		}

		// Track 9.E step 3c follow-up (diagnostic only): the cyclic prefix
		// (prolog tokens) is emitted at end-of-main in the steady-state
		// body. Current codegen schedules main and the appended cyclic
		// prefix as two sequential blocks. SCEI's MAIN_LOOP interleaves
		// both. This diagnostic compares:
		//   alone       = schedule(main)                      [step 3b's baseline]
		//   prologAlone = schedule(prolog)
		//   serial      = schedule(main) + schedule(prolog)   [serial upper bound]
		//   inContext   = schedule(prolog; then main)         [scheduledLoopBodyCyclesInContext]
		//   combined    = schedule(main ++ prolog)            [SCEI-style single pass]
		// "savings" = serial - combined; that's how much an interleaved
		// scheduler over (main + cyclic prefix) could buy per iteration.
		if( std::getenv( "OPENVCL_DUMP_COMBINED_SCHEDULE" ) != NULL )
		{
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.mainTokenIndices.empty() )
			{
				const unsigned int aloneCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				const unsigned int prologAloneCycles =
				    scheduledLoopBodyCycles( opportunity.prologTokenIndices, indexedTokens );
				const unsigned int inContextCycles =
				    scheduledLoopBodyCyclesInContext( opportunity.prologTokenIndices,
				                                      opportunity.mainTokenIndices,
				                                      indexedTokens );
				// Combined: schedule main ++ prolog as ONE bodyTokens list.
				std::list<Token> combinedTokens;
				for( std::vector<unsigned int>::const_iterator i =
				         opportunity.mainTokenIndices.begin();
				     i != opportunity.mainTokenIndices.end(); ++i )
				{
					if( *i < indexedTokens.size() )
						appendUnlabeledTokenForScheduleCost(
						    combinedTokens, *indexedTokens[*i] );
				}
				for( std::vector<unsigned int>::const_iterator i =
				         opportunity.prologTokenIndices.begin();
				     i != opportunity.prologTokenIndices.end(); ++i )
				{
					if( *i < indexedTokens.size() )
						appendUnlabeledTokenForScheduleCost(
						    combinedTokens, *indexedTokens[*i] );
				}
				const unsigned int combinedCycles =
				    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( combinedTokens ).cycleCount;
				const unsigned int serialCycles = aloneCycles + prologAloneCycles;
				const unsigned int savings = serialCycles > combinedCycles
				                                 ? serialCycles - combinedCycles
				                                 : 0;
				std::cerr << "[combined-schedule] loop=" << opportunity.label
				          << " mainAlone=" << aloneCycles
				          << " prologAlone=" << prologAloneCycles
				          << " serial=" << serialCycles
				          << " inContext=" << inContextCycles
				          << " combined=" << combinedCycles
				          << " savingsIfInterleaved=" << savings
				          << " mainSize=" << opportunity.mainTokenIndices.size()
				          << " prologSize=" << opportunity.prologTokenIndices.size()
				          << "\n";
			}
		}

		result.push_back( opportunity );
	}

	return result;
}

std::vector<VuSoftwarePipelineRewritePlan> buildVuSoftwarePipelineRewritePlans( const std::list<Token>& tokens )
{
	std::vector<VuSoftwarePipelineRewritePlan> plans;
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator token = tokens.begin(); token != tokens.end(); ++token )
		indexedTokens.push_back( &*token );

	const std::vector<VuLoopPipelineOpportunity> opportunities = findVuLoopPipelineOpportunities( tokens );
	std::map<unsigned int, bool> plannedLabelIndices;
	for( std::vector<VuLoopPipelineOpportunity>::const_iterator i = opportunities.begin(); i != opportunities.end(); ++i )
	{
		if( !i->canEmitSoftwarePipeline
		    && !i->canEmitMultiQSoftwarePipeline )
			continue;

		VuSoftwarePipelineRewritePlan plan;
		plan.label = i->label;
		plan.prologLabel = i->label + "__PROLOG";
		plan.mainLabel = i->label;
		plan.drainLabel = i->label + "__DRAIN";
		plan.labelTokenIndex = i->labelTokenIndex;
		plan.branchTokenIndex = i->branchTokenIndex;
		plan.qProducerTokenIndex = i->qProducerTokenIndex;

		if( i->canEmitMultiQSoftwarePipeline )
		{
			if( !i->qProducerTokenIndices.empty() )
				plan.qProducerTokenIndex = i->qProducerTokenIndices.front();
			plan.cyclicPrefixBeforeBranch = true;
			plan.prologTokenIndices = i->multiQPrologTokenIndices;
			plan.mainTokenIndices = i->multiQMainTokenIndices;
			plan.cyclicPrefixTokenIndices = i->multiQCyclicPrefixTokenIndices;
			plan.cyclicPrefixRotations = i->multiQCyclicPrefixRotations;
			plan.cyclicPrefixInsertBeforeTokenIndex =
			    i->multiQCyclicPrefixInsertBeforeTokenIndex;
			plan.cyclicPrefixNeedsGuard = i->multiQCyclicPrefixNeedsGuard;
			plan.cyclicPrefixLastTokenInBranchDelaySlot =
			    i->multiQCyclicPrefixLastTokenInBranchDelaySlot;
			plan.emitsDrain = i->multiQCyclicPrefixNeedsGuard;
			plan.drainsSuffixStores = i->canEmitSuffixStoreDrain;
			plan.suffixStores = i->softwarePipelineSuffixStores;
			plan.inductionUpdates = i->inductionUpdates;
			plan.drainTokenIndices = i->drainTokenIndices;
			plans.push_back( plan );
			plannedLabelIndices[plan.labelTokenIndex] = true;
			continue;
		}

		if( i->qConsumerTokenIndices.empty() )
			continue;

		plan.prefetchInsertAfterTokenIndex = i->qConsumerTokenIndices.back();
		plan.qProducerInsertAfterTokenIndex = i->qConsumerTokenIndices.back();
		unsigned int suffixBlockerTokenIndex = 0;
		if( qProducerCanMoveIntoBranchDelaySlot( *i, indexedTokens, &suffixBlockerTokenIndex ) )
		{
			plan.qProducerInsertAfterTokenIndex = i->branchTokenIndex;
			plan.qProducerInBranchDelaySlot = true;
		}
		else if( suffixBlockerTokenIndex != 0 )
		{
			plan.qProducerBranchDelayBlockedBySuffixDependency = true;
			plan.qProducerBranchDelaySuffixBlockerTokenIndex = suffixBlockerTokenIndex;
		}
		plan.emitsDrain = i->qLiveOut;
		plan.prefetches = i->softwarePipelinePrefetches;
		plan.rotations = i->softwarePipelineRotations;
		plan.suffixStores = i->softwarePipelineSuffixStores;
		plan.drainsSuffixStores = i->canEmitSuffixStoreDrain;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator p = i->softwarePipelinePrefetches.begin();
		     p != i->softwarePipelinePrefetches.end(); ++p )
			plan.prefetchTokenIndices.push_back( p->tokenIndex );
		plan.prologTokenIndices = i->prologTokenIndices;
		plan.mainTokenIndices = i->mainTokenIndices;
		plan.drainTokenIndices = i->drainTokenIndices;
		plan.qConsumerTokenIndices = i->qConsumerTokenIndices;
		plans.push_back( plan );
		plannedLabelIndices[plan.labelTokenIndex] = true;
	}

	const std::vector<VuLoopCandidate> loops = findVuLoopCandidates( tokens );
	for( std::vector<VuLoopCandidate>::const_iterator loop = loops.begin();
	     loop != loops.end(); ++loop )
	{
		if( plannedLabelIndices.find( loop->labelTokenIndex ) != plannedLabelIndices.end() )
			continue;
		VuSoftwarePipelineRewritePlan plan;
		if( buildGenericCyclicPrefixRewritePlan( *loop, indexedTokens, plan ) )
		{
			plans.push_back( plan );
			plannedLabelIndices[plan.labelTokenIndex] = true;
		}
	}

	if( std::getenv( "OPENVCL_DUMP_MULTISTAGE_CANDIDATES" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			std::cerr << "[multistage-plan] loop=" << p->label
			          << " stageCount=" << p->stageCount
			          << " kernelTokens=" << p->kernelTokenIndices.size()
			          << " tokenStageOffsets=" << p->tokenStageOffsets.size()
			          << " rotations=" << p->stageRotationRegisters.size()
			          << "\n";
		}
	}

	return plans;
}

std::list<Token> applyVuSoftwarePipelinePlans( const std::list<Token>& tokens )
{
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
		indexedTokens.push_back( &*i );

	std::map<unsigned int, VuSoftwarePipelineRewritePlan> plansByLabelIndex;
	const std::vector<VuSoftwarePipelineRewritePlan> plans = buildVuSoftwarePipelineRewritePlans( tokens );
	for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator i = plans.begin(); i != plans.end(); ++i )
	{
		if( i->labelTokenIndex < indexedTokens.size() && i->branchTokenIndex < indexedTokens.size() )
			plansByLabelIndex[i->labelTokenIndex] = *i;
	}

	if( plansByLabelIndex.empty() )
		return tokens;

	std::list<Token> output;
	unsigned int index = 0;
	std::list<Token>::const_iterator i = tokens.begin();
	while( i != tokens.end() )
	{
		std::map<unsigned int, VuSoftwarePipelineRewritePlan>::const_iterator plan = plansByLabelIndex.find( index );
		if( plan == plansByLabelIndex.end() )
		{
			output.push_back( *i );
			++i;
			++index;
			continue;
		}

		const VuSoftwarePipelineRewritePlan& rewrite = plan->second;
		if( !tokenCanCarrySoftwarePipelineLabel( *i ) )
		{
			output.push_back( *i );
			++i;
			++index;
			continue;
		}

		Token prologLabel( *i );
		prologLabel.setLabel( rewrite.prologLabel );
		output.push_back( prologLabel );

		for( std::vector<unsigned int>::const_iterator p = rewrite.prologTokenIndices.begin();
		     p != rewrite.prologTokenIndices.end(); ++p )
		{
			if( *p < indexedTokens.size() )
			{
				if( rewrite.cyclicPrefixBeforeBranch )
					output.push_back( adjustedMultiQCyclicPrefixToken( *indexedTokens[*p],
					                                                   rewrite.cyclicPrefixRotations ) );
				else
					output.push_back( tokenWithoutLabel( *indexedTokens[*p] ) );
			}
		}

		if( rewrite.drainsSuffixStores )
		{
			if( rewrite.cyclicPrefixBeforeBranch )
			{
				appendCyclicPrefixMainBody( output,
				                            indexedTokens,
				                            rewrite,
				                            true,
				                            true,
				                            true );
			}
			else
				appendTokenRangeWithInsertedPrefetchAndQProducer(
				    output,
				    indexedTokens,
				    rewrite,
				    rewrite.mainTokenIndices.empty() ? rewrite.branchTokenIndex : rewrite.mainTokenIndices.front(),
				    rewrite.branchTokenIndex,
				    true,
				    true );
		}

		Token mainLabel( *i );
		mainLabel.setLabel( rewrite.mainLabel );
		output.push_back( mainLabel );
		if( rewrite.cyclicPrefixBeforeBranch )
			appendRotationMoves( output, *i, rewrite.cyclicPrefixRotations );

		if( rewrite.cyclicPrefixBeforeBranch )
		{
			if( rewrite.drainsSuffixStores )
				appendDelayedSuffixStores( output, indexedTokens, rewrite );
			appendCyclicPrefixMainBody( output,
			                            indexedTokens,
			                            rewrite,
			                            rewrite.drainsSuffixStores,
			                            false,
			                            true );
		}
		else
		{
			if( rewrite.drainsSuffixStores )
				appendDelayedSuffixStores( output, indexedTokens, rewrite );
			appendTokenRangeWithInsertedPrefetchAndQProducer(
			    output,
			    indexedTokens,
			    rewrite,
			    rewrite.mainTokenIndices.empty() ? rewrite.branchTokenIndex : rewrite.mainTokenIndices.front(),
			    rewrite.branchTokenIndex,
			    rewrite.drainsSuffixStores,
			    false );
		}

		while( index <= rewrite.branchTokenIndex && i != tokens.end() )
		{
			++i;
			++index;
		}

		if( rewrite.emitsDrain || rewrite.drainsSuffixStores )
		{
			Token drainLabel( *indexedTokens[rewrite.labelTokenIndex] );
			drainLabel.setLabel( rewrite.drainLabel );
			output.push_back( drainLabel );
			if( rewrite.drainsSuffixStores )
				appendDelayedSuffixStores( output, indexedTokens, rewrite );
			if( rewrite.emitsDrain && rewrite.cyclicPrefixNeedsGuard )
			{
				for( std::vector<unsigned int>::const_iterator d = rewrite.drainTokenIndices.begin();
				     d != rewrite.drainTokenIndices.end(); ++d )
				{
					if( *d < indexedTokens.size() )
						output.push_back( tokenWithoutLabel( *indexedTokens[*d] ) );
				}
			}
			if( rewrite.emitsDrain
			    && !rewrite.cyclicPrefixNeedsGuard
			    && rewrite.qProducerTokenIndex < indexedTokens.size() )
				output.push_back( adjustedQProducerToken( *indexedTokens[rewrite.qProducerTokenIndex],
				                                          rewrite.rotations ) );
		}
	}

	return output;
}

bool advanceVuStoreBaseUpdates( std::list<Token>& tokens )
{
	bool changed = false;

	for( std::list<Token>::iterator update = tokens.begin(); update != tokens.end(); )
	{
		if( update->label().length() != 0
		    || (update->flags() & (Token::BRANCH_DELAY_FILLER | Token::PREORDERED)) )
		{
			++update;
			continue;
		}

		VuLoopInductionUpdate induction;
		if( !describeSelfIntegerImmediateUpdate( *update, 0, induction )
		    || !induction.stepKnown
		    || induction.step == 0
		    || update == tokens.begin() )
		{
			++update;
			continue;
		}

		std::list<Token>::iterator firstStore = update;
		bool foundStore = false;
		std::list<Token>::iterator scan = update;
		while( scan != tokens.begin() )
		{
			--scan;
			if( tokenHasSchedulingBoundaryForStoreBaseAdvance( *scan ) )
				break;

			long offset = 0;
			if( storeUsesBaseWithKnownOffset( *scan, induction.registerName, offset ) )
			{
				(void)offset;
				firstStore = scan;
				foundStore = true;
				continue;
			}

			if( tokenReadsOrWritesRegisterKey( *scan, induction.registerName ) )
				break;
		}

		if( !foundStore )
		{
			++update;
			continue;
		}

		bool canMove = true;
		for( std::list<Token>::iterator i = firstStore; i != update; ++i )
		{
			long offset = 0;
			if( storeUsesBaseWithKnownOffset( *i, induction.registerName, offset ) )
			{
				(void)offset;
				continue;
			}
			if( tokenReadsOrWritesRegisterKey( *i, induction.registerName ) )
			{
				canMove = false;
				break;
			}
		}
		if( !canMove )
		{
			++update;
			continue;
		}

		for( std::list<Token>::iterator i = firstStore; i != update; ++i )
		{
			long offset = 0;
			if( storeUsesBaseWithKnownOffset( *i, induction.registerName, offset ) )
				setIndirectMemoryOffset( *i, induction.registerName, offset - induction.step );
		}

		Token movedUpdate( *update );
		std::list<Token>::iterator next = update;
		++next;
		tokens.erase( update );
		tokens.insert( firstStore, movedUpdate );
		update = next;
		changed = true;
	}

	return changed;
}

bool vuScheduledProgramHasNoCycleRegression( const VuScheduledProgram& original,
                                             const VuScheduledProgram& candidate )
{
	if( candidate.cycleCount > original.cycleCount )
		return false;

	for( std::vector<VuScheduledBasicBlock>::const_iterator c = candidate.blocks.begin();
	     c != candidate.blocks.end(); ++c )
	{
		std::string label;
		if( !scheduledBlockLabel( *c, label ) )
			continue;

		for( std::vector<VuScheduledBasicBlock>::const_iterator o = original.blocks.begin();
		     o != original.blocks.end(); ++o )
		{
			std::string originalLabel;
			if( scheduledBlockLabel( *o, originalLabel ) && originalLabel == label )
			{
				if( c->cycleCount > o->cycleCount )
					return false;
				break;
			}
		}
	}

	return true;
}

std::list<Token> applyVuSoftwarePipelinePlansWithSafeStoreBaseAdvance( const std::list<Token>& tokens )
{
	if( buildVuSoftwarePipelineRewritePlans( tokens ).empty() )
		return tokens;

	std::list<Token> pipelinedTokens = applyVuSoftwarePipelinePlans( tokens );
	std::list<Token> advancedTokens = pipelinedTokens;
	if( !advanceVuStoreBaseUpdates( advancedTokens ) )
		return pipelinedTokens;

	const VuScheduledProgram originalSchedule =
		scheduleVuProgramReadyIssueSlotsWithFlagLiveness( pipelinedTokens );
	const VuScheduledProgram advancedSchedule =
		scheduleVuProgramReadyIssueSlotsWithFlagLiveness( advancedTokens );
	if( vuScheduledProgramHasNoCycleRegression( originalSchedule, advancedSchedule ) )
		return advancedTokens;
	return pipelinedTokens;
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
	const VuScheduledProgram program =
		scheduleVuProgramReadyIssueSlotsInternal( tokens, ignoredImplicitWawResources, false );
	return flattenVuScheduledProgramTokens( program );
}

std::list<Token> scheduleVuTokensReadySetWithFlagLiveness( const std::list<Token>& tokens )
{
	const VuScheduledProgram program =
		scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal( tokens, false );
	return flattenVuScheduledProgramTokens( program );
}

}
