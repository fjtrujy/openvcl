#include "VuSchedulerAnalysis.h"

#include "VuSchedulingRules.h"
#include "VuTokenResourceAccess.h"

#include <cstdlib>
#include <map>
#include <sstream>
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

	bool tokenWritesMacForPair( const Token& token )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		return (access.implicitWrites & VU_RESOURCE_MAC) != 0;
	}

	unsigned int chooseReadyPairPartner( unsigned int primary,
	                                     const VuBasicBlock& block,
	                                     const std::vector<unsigned int>& incoming,
	                                     const std::vector<bool>& emitted,
	                                     const std::vector<unsigned int>& priority )
	{
		unsigned int best = static_cast<unsigned int>( block.tokens.size() );
		int bestScore = 0;
		const bool primaryIsLower = isVuLowerPipe( *block.tokens[primary] );
		const bool primaryWritesMac = tokenWritesMacForPair( *block.tokens[primary] );

		for( unsigned int i = 0; i < block.tokens.size(); ++i )
		{
			if( i == primary || emitted[i] || incoming[i] != 0 )
				continue;
			if( isVuLowerPipe( *block.tokens[i] ) == primaryIsLower )
				continue;
			if( !vuTokenPairResourcesAreIndependent( *block.tokens[primary],
			                                         *block.tokens[i],
			                                         primaryWritesMac,
			                                         tokenWritesMacForPair( *block.tokens[i] ) ) )
				continue;

			const int score = readyCandidateScore( i, false, false, block, priority );
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

	VuScheduledIssueSlot makeIssueSlot( const Token* first, const Token* second )
	{
		VuScheduledIssueSlot slot;
		slot.firstToken = first;
		slot.secondToken = second;

		if( first )
		{
			if( isVuLowerPipe( *first ) )
				slot.lowerToken = first;
			else
				slot.upperToken = first;
		}

		if( second )
		{
			if( isVuLowerPipe( *second ) )
				slot.lowerToken = second;
			else
				slot.upperToken = second;
		}

		return slot;
	}

	std::vector<VuScheduledIssueSlot> scheduleReadySegmentIssueSlots( const std::vector<const Token*>& segment,
	                                                                  unsigned int ignoredImplicitWawResources )
	{
		std::vector<VuScheduledIssueSlot> slots;
		if( segment.size() < 2 )
		{
			for( std::vector<const Token*>::const_iterator i = segment.begin(); i != segment.end(); ++i )
				slots.push_back( makeIssueSlot( *i, NULL ) );
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
						slots.push_back( makeIssueSlot( block.tokens[i], NULL ) );
				}
				return slots;
			}

			const unsigned int partner = chooseReadyPairPartner( best, block, incoming, emitted, priority );
			slots.push_back( makeIssueSlot( block.tokens[best],
			                                (partner < block.tokens.size()) ? block.tokens[partner] : NULL ) );

			markReadyTokenScheduled( best, incoming, outgoing, emitted, emittedCount );
			if( partner < block.tokens.size() )
			{
				markReadyTokenScheduled( partner, incoming, outgoing, emitted, emittedCount );
				haveLastPipe = false;
			}
			else
			{
				haveLastPipe = true;
				lastWasLower = isVuLowerPipe( *block.tokens[best] );
			}
		}

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

	void appendReadyScheduledSegment( const std::vector<const Token*>& segment,
	                                  std::list<Token>& scheduled,
	                                  unsigned int ignoredImplicitWawResources )
	{
		appendIssueSlotsFlat( scheduleReadySegmentIssueSlots( segment, ignoredImplicitWawResources ),
		                      scheduled );
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

	bool parseImmediateLong( const std::string& text, long& value )
	{
		char* end = NULL;
		value = std::strtol( text.c_str(), &end, 0 );
		return end && *end == '\0';
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

	std::string registerBaseKey( const std::string& key )
	{
		std::string::size_type field = key.find( '.' );
		if( field == std::string::npos )
			return key;
		return key.substr( 0, field );
	}

	std::string registerFieldKey( const std::string& key )
	{
		std::string::size_type field = key.find( '.' );
		if( field == std::string::npos || field + 1 >= key.size() )
			return "";
		return key.substr( field + 1 );
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

	void assignRotationScratchRegisters( const VuLoopCandidate& loop,
	                                     std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		std::list<std::string> used;
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
			addUniqueString( used, i->registerBase );

		for( std::vector<VuSoftwarePipelineRotation>::iterator rotation = rotations.begin();
		     rotation != rotations.end(); ++rotation )
		{
			for( unsigned int reverse = 32; reverse > 1; --reverse )
			{
				const std::string scratch = vfRegisterName( reverse - 1 );
				if( containsKey( used, scratch ) )
					continue;
				rotation->hasScratchRegister = true;
				rotation->scratchRegister = scratch;
				addUniqueString( used, scratch );
				break;
			}
		}
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

	bool tokenRangeReadsOrWritesAny( unsigned int beginIndex,
	                                 unsigned int endIndex,
	                                 const std::vector<const Token*>& indexedTokens,
	                                 const std::list<std::string>& keys )
	{
		for( unsigned int i = beginIndex; i <= endIndex && i < indexedTokens.size(); ++i )
		{
			std::list<std::string> reads;
			std::list<std::string> writes;
			collectVuRegisterReadKeys( *indexedTokens[i], reads );
			collectVuRegisterWriteKeys( *indexedTokens[i], writes );
			if( intersects( reads, keys ) || intersects( writes, keys ) )
				return true;
		}
		return false;
	}

	bool rotationBaseListContains( const std::list<std::string>& bases, const std::string& base )
	{
		return containsKey( bases, base );
	}

	bool softwarePipelineRotationsCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                       const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.softwarePipelineRotations.empty() )
			return true;
		if( opportunity.qConsumerTokenIndices.size() != 1 )
			return false;

		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		std::list<std::string> prefetchWrites;
		std::list<std::string> prefetchWriteBases;
		collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
		collectVfBaseKeys( prefetchWrites, prefetchWriteBases );

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
			collectVfBaseKeys( writes, writeBases );
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

		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		std::list<std::string> prefetchWrites;
		collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
		if( rotationsWillUseScratch )
			removeRotationCoveredWrites( prefetchWrites, opportunity.softwarePipelineRotations );
		if( prefetchWrites.empty() )
			return true;

		const unsigned int insertAfter = opportunity.qConsumerTokenIndices.back();
		if( insertAfter + 1 > opportunity.branchTokenIndex )
			return true;
		return !tokenRangeReadsOrWritesAny( insertAfter + 1,
		                                    opportunity.branchTokenIndex,
		                                    indexedTokens,
		                                    prefetchWrites );
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

	VuLoopQSchedulingStrategy classifyLoopQSchedulingStrategy( const VuLoopPipelineOpportunity& opportunity )
	{
		if( opportunity.qProducerConsumerGapDeficitCycles == 0 )
			return VU_LOOP_Q_SCHEDULE_LOCAL;
		if( opportunity.loopCarriedQGapCycles >= opportunity.qProducerLatency )
			return VU_LOOP_Q_SCHEDULE_LOOP_CARRIED;
		return VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
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
		assignRotationScratchRegisters( loop, opportunity.softwarePipelineRotations );
		collectSoftwarePipelinePrefetchDescriptors( opportunity.prologTokenIndices,
		                                            opportunity.qProducerTokenIndex,
		                                            indexedTokens,
		                                            opportunity );

		const bool canEmitRotations = softwarePipelineRotationsCanEmit( opportunity, indexedTokens );
		if( !opportunity.softwarePipelineRotatedRegisters.empty() && !canEmitRotations )
			addPipelineBlocker( opportunity, "requires_register_rotation" );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator rotation = opportunity.softwarePipelineRotations.begin();
		     rotation != opportunity.softwarePipelineRotations.end(); ++rotation )
		{
			if( !rotation->hasScratchRegister )
				addPipelineBlocker( opportunity, "missing_rotation_scratch" );
		}

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
			std::vector<unsigned int> prefetchTokenIndices;
			for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator p = opportunity.softwarePipelinePrefetches.begin();
			     p != opportunity.softwarePipelinePrefetches.end(); ++p )
				prefetchTokenIndices.push_back( p->tokenIndex );
			collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
			if( canEmitRotations )
				removeRotationCoveredWrites( prefetchWrites, opportunity.softwarePipelineRotations );
			if( !prefetchWrites.empty()
			    && !opportunity.qConsumerTokenIndices.empty()
			    && opportunity.qConsumerTokenIndices.back() + 1 <= opportunity.branchTokenIndex
			    && tokenRangeReadsOrWritesAny( opportunity.qConsumerTokenIndices.back() + 1,
			                                   opportunity.branchTokenIndex,
			                                   indexedTokens,
			                                   prefetchWrites ) )
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
				addPipelineBlocker( opportunity, "q_live_out" );
				break;
			}
		}

		opportunity.canEmitSoftwarePipeline =
		    opportunity.hasSoftwarePipelinePlan && opportunity.softwarePipelineBlockers.empty();
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
		dst.setRegNumber( static_cast<int>( vfRegisterNumber( rotation.registerBase ) ) );
		dst.setFlags( Token::Argument::WRITE | Token::Argument::DEST );
		dst.setFields( rotationFieldMask( rotation ) );
		token.arguments().push_back( dst );

		Token::Argument src( rotation.scratchRegister );
		src.setType( Token::Argument::FLOAT_REGISTER );
		src.setRegNumber( static_cast<int>( vfRegisterNumber( rotation.scratchRegister ) ) );
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

	void appendTokenRangeWithInsertedPrefetchAndQProducer( std::list<Token>& output,
	                                                       const std::vector<const Token*>& indexedTokens,
	                                                       const VuSoftwarePipelineRewritePlan& rewrite,
	                                                       unsigned int beginIndex,
	                                                       unsigned int endIndex )
	{
		for( unsigned int i = beginIndex; i <= endIndex && i < indexedTokens.size(); ++i )
		{
			if( i == rewrite.branchTokenIndex && !rewrite.rotations.empty() )
				appendRotationMoves( output, *indexedTokens[i], rewrite.rotations );
			output.push_back( tokenWithoutLabel( *indexedTokens[i] ) );
			if( i == rewrite.qProducerInsertAfterTokenIndex )
			{
				for( std::vector<unsigned int>::const_iterator p = rewrite.prefetchTokenIndices.begin();
				     p != rewrite.prefetchTokenIndices.end(); ++p )
				{
					if( *p >= indexedTokens.size() )
						continue;
					output.push_back( adjustedPrefetchToken( *indexedTokens[*p],
					                                        findPrefetchForTokenIndex( rewrite.prefetches, *p ),
					                                        rewrite.rotations ) );
				}
				if( rewrite.qProducerTokenIndex < indexedTokens.size() )
					output.push_back( adjustedQProducerToken( *indexedTokens[rewrite.qProducerTokenIndex],
					                                          rewrite.rotations ) );
			}
		}
	}
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
}

VuLoopCandidate::VuLoopCandidate()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	firstBodyTokenIndex = 0;
	lastBodyTokenIndex = 0;
	hasLoopDirective = false;
	simpleCountedLoop = false;
	memoryLoadCount = 0;
	memoryStoreCount = 0;
	hasMemoryPreOrPostIncrement = false;
	hasXgkick = false;
	branchToken = NULL;
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
	simpleCountedLoop = false;
	hasSingleQProducer = false;
	requiresPrologEpilog = false;
	requiresLoopCarriedRegisters = false;
	eligibleSingleQSoftwarePipeline = false;
	hasSoftwarePipelinePlan = false;
	canEmitSoftwarePipeline = false;
	memoryLoadCount = 0;
	memoryStoreCount = 0;
	hasMemoryPreOrPostIncrement = false;
	hasXgkick = false;
}

VuSoftwarePipelineRewritePlan::VuSoftwarePipelineRewritePlan()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	qProducerTokenIndex = 0;
	qProducerInsertAfterTokenIndex = 0;
	emitsDrain = false;
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

std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlots( const VuBasicBlock& block,
                                                                       unsigned int ignoredImplicitWawResources )
{
	std::vector<VuScheduledIssueSlot> slots;
	std::vector<const Token*> segment;

	for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
	{
		if( isVuReadyScheduleCandidate( **i ) )
		{
			segment.push_back( *i );
			continue;
		}

		std::vector<VuScheduledIssueSlot> segmentSlots =
			scheduleReadySegmentIssueSlots( segment, ignoredImplicitWawResources );
		slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );
		segment.clear();
		slots.push_back( makeIssueSlot( *i, NULL ) );
	}

	std::vector<VuScheduledIssueSlot> segmentSlots =
		scheduleReadySegmentIssueSlots( segment, ignoredImplicitWawResources );
	slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );

	return slots;
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
		for( unsigned int i = 0; i < loop->bodyTokens.size(); ++i )
		{
			if( vuTokenWritesQ( *loop->bodyTokens[i] ) )
			{
				++qProducerCount;
				qProducerOffset = i;
			}
		}

		if( qProducerCount == 0 )
			continue;

		std::vector<unsigned int> qConsumerOffsets;
		for( unsigned int i = qProducerOffset + 1; i < loop->bodyTokens.size(); ++i )
		{
			if( vuTokenReadsQ( *loop->bodyTokens[i] ) )
				qConsumerOffsets.push_back( i );
		}
		if( qConsumerOffsets.empty() )
			continue;

		VuLoopPipelineOpportunity opportunity;
		opportunity.label = loop->label;
		opportunity.labelTokenIndex = loop->labelTokenIndex;
		opportunity.branchTokenIndex = loop->branchTokenIndex;
		opportunity.qProducerTokenIndex = loop->firstBodyTokenIndex + qProducerOffset;
		opportunity.firstQConsumerTokenIndex = loop->firstBodyTokenIndex + qConsumerOffsets.front();
		opportunity.lastQConsumerTokenIndex = loop->firstBodyTokenIndex + qConsumerOffsets.back();
		opportunity.qProducerLatency = loop->bodyTokens[qProducerOffset]->operand()
		                             ? loop->bodyTokens[qProducerOffset]->operand()->latency()
		                             : 0;
		opportunity.qProducerConsumerGapCycles = countEmittableTokens( *loop,
		                                                                qProducerOffset + 1,
		                                                                qConsumerOffsets.front() );
		opportunity.sourcePrefixCycles = countEmittableTokens( *loop, 0, qProducerOffset );
		opportunity.sourceSuffixCycles = countEmittableTokens( *loop,
		                                                       qConsumerOffsets.front() + 1,
		                                                       static_cast<unsigned int>( loop->bodyTokens.size() - 1 ) );
		opportunity.branchDelaySlots = loop->branchToken ? vuTokenBranchDelaySlots( *loop->branchToken ) : 0;
		opportunity.qProducerConsumerGapDeficitCycles =
			opportunity.qProducerLatency > opportunity.qProducerConsumerGapCycles
			? opportunity.qProducerLatency - opportunity.qProducerConsumerGapCycles
			: 0;
		opportunity.loopCarriedQGapCycles =
			opportunity.sourceSuffixCycles + opportunity.branchDelaySlots + opportunity.sourcePrefixCycles;
		opportunity.qProducerInsertionGapCycles =
			countEmittableTokens( *loop,
			                      qConsumerOffsets.back() + 1,
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

		for( std::vector<unsigned int>::const_iterator q = qConsumerOffsets.begin(); q != qConsumerOffsets.end(); ++q )
		{
			opportunity.qConsumerTokenIndices.push_back( loop->firstBodyTokenIndex + *q );
			collectLoopCarriedQInputs( *loop, *q, opportunity.carriedQInputRegisters );
			collectLoopCarriedQOutputs( *loop, *q, opportunity.carriedQOutputRegisters );
		}

		opportunity.requiresLoopCarriedRegisters = !opportunity.carriedQInputRegisters.empty()
		                                        || !opportunity.carriedQOutputRegisters.empty();
		opportunity.eligibleSingleQSoftwarePipeline = opportunity.simpleCountedLoop
		                                           && opportunity.hasSingleQProducer
		                                           && opportunity.branchDelaySlots > 0
		                                           && (opportunity.sourcePrefixCycles + opportunity.sourceSuffixCycles) >= opportunity.qProducerLatency;

		if( opportunity.eligibleSingleQSoftwarePipeline )
		{
			const unsigned int firstConsumerOffset = qConsumerOffsets.front();
			const unsigned int branchOffset = loop->branchTokenIndex - loop->firstBodyTokenIndex;
			opportunity.hasSoftwarePipelinePlan = true;
			appendPipelineInstructionIndices( *loop, 0, firstConsumerOffset, opportunity.prologTokenIndices );
			appendPipelineInstructionIndices( *loop, firstConsumerOffset, branchOffset + 1, opportunity.mainTokenIndices );
			appendPipelineInstructionIndices( *loop, firstConsumerOffset, branchOffset, opportunity.drainTokenIndices );
		}

		classifySoftwarePipelineEmissionSafety( opportunity, *loop, qProducerOffset, indexedTokens );

		result.push_back( opportunity );
	}

	return result;
}

std::vector<VuSoftwarePipelineRewritePlan> buildVuSoftwarePipelineRewritePlans( const std::list<Token>& tokens )
{
	std::vector<VuSoftwarePipelineRewritePlan> plans;
	const std::vector<VuLoopPipelineOpportunity> opportunities = findVuLoopPipelineOpportunities( tokens );
	for( std::vector<VuLoopPipelineOpportunity>::const_iterator i = opportunities.begin(); i != opportunities.end(); ++i )
	{
		if( !i->canEmitSoftwarePipeline || i->qConsumerTokenIndices.empty() )
			continue;

		VuSoftwarePipelineRewritePlan plan;
		plan.label = i->label;
		plan.prologLabel = i->label + "__PROLOG";
		plan.mainLabel = i->label;
		plan.drainLabel = i->label + "__DRAIN";
		plan.labelTokenIndex = i->labelTokenIndex;
		plan.branchTokenIndex = i->branchTokenIndex;
		plan.qProducerTokenIndex = i->qProducerTokenIndex;
		plan.qProducerInsertAfterTokenIndex = i->qConsumerTokenIndices.back();
		plan.prefetches = i->softwarePipelinePrefetches;
		plan.rotations = i->softwarePipelineRotations;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator p = i->softwarePipelinePrefetches.begin();
		     p != i->softwarePipelinePrefetches.end(); ++p )
			plan.prefetchTokenIndices.push_back( p->tokenIndex );
		plan.prologTokenIndices = i->prologTokenIndices;
		plan.mainTokenIndices = i->mainTokenIndices;
		plan.drainTokenIndices = i->drainTokenIndices;
		plan.qConsumerTokenIndices = i->qConsumerTokenIndices;
		plans.push_back( plan );
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
		if( i->operand() )
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
				output.push_back( tokenWithoutLabel( *indexedTokens[*p] ) );
		}

		Token mainLabel( *i );
		mainLabel.setLabel( rewrite.mainLabel );
		output.push_back( mainLabel );

		appendTokenRangeWithInsertedPrefetchAndQProducer(
		    output,
		    indexedTokens,
		    rewrite,
		    rewrite.mainTokenIndices.empty() ? rewrite.branchTokenIndex : rewrite.mainTokenIndices.front(),
		    rewrite.branchTokenIndex );

		while( index <= rewrite.branchTokenIndex && i != tokens.end() )
		{
			++i;
			++index;
		}
	}

	return output;
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

std::list<Token> scheduleVuTokensReadySetWithFlagLiveness( const std::list<Token>& tokens )
{
	int lastMacReader = -1;
	int lastClipReader = -1;
	unsigned int index = 0;
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
	{
		if( tokenReadsMac( *i ) )
			lastMacReader = static_cast<int>( index );
		if( tokenReadsClip( *i ) )
			lastClipReader = static_cast<int>( index );
	}

	std::list<Token> scheduled;
	std::list<Token> segment;
	unsigned int segmentMask = VU_RESOURCE_NONE;
	bool haveSegment = false;
	index = 0;

	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
	{
		const unsigned int tokenMask = ignoredFlagWawMaskForIndex( index, lastMacReader, lastClipReader );
		if( haveSegment && tokenMask != segmentMask )
		{
			std::list<Token> scheduledSegment = scheduleVuTokensReadySet( segment, segmentMask );
			scheduled.insert( scheduled.end(), scheduledSegment.begin(), scheduledSegment.end() );
			segment.clear();
			haveSegment = false;
		}

		if( !haveSegment )
		{
			segmentMask = tokenMask;
			haveSegment = true;
		}
		segment.push_back( *i );

		if( index == static_cast<unsigned int>( lastMacReader )
		    || index == static_cast<unsigned int>( lastClipReader ) )
		{
			std::list<Token> scheduledSegment = scheduleVuTokensReadySet( segment, segmentMask );
			scheduled.insert( scheduled.end(), scheduledSegment.begin(), scheduledSegment.end() );
			segment.clear();
			haveSegment = false;
		}
	}

	if( haveSegment )
	{
		std::list<Token> scheduledSegment = scheduleVuTokensReadySet( segment, segmentMask );
		scheduled.insert( scheduled.end(), scheduledSegment.begin(), scheduledSegment.end() );
	}

	return scheduled;
}

}
