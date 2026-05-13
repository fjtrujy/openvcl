/*
 * CodeGenerator.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CodeGenerator.h"
#include "stlhelp.h"
#include "Expression.h"
#include "Error.h"
#include "Math.h"
#include "VuSchedulerAnalysis.h"
#include "VuSchedulingRules.h"
#include "VuInstructionInfo.h"
#include "VuTokenResourceAccess.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <assert.h>
#include <cmath>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Class
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

	bool intersectsKeys( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( containsKey( b, *i ) )
				return true;
		}
		return false;
	}

	bool evaluateIntegerExpression( const std::string& text, long& value )
	{
		Expression e;
		e.setCustomOperators( Math::mathOperators() );
		if( !e.process( text ) || !e.solve() )
			return false;
		value = static_cast<long>( e.result() );
		return true;
	}

	bool integerSelfImmediateAdd( const Token& token, std::string& reg, long& immediate )
	{
		if( !token.operand() || lowerVuTokenName(token) != "iaddiu" )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER) )
			return false;

		const std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 3 )
			return false;

		std::list<Token::Argument>::const_iterator dst = args.begin();
		std::list<Token::Argument>::const_iterator src = dst;
		++src;
		std::list<Token::Argument>::const_iterator imm = src;
		++imm;

		if( dst->type() != Token::Argument::INTEGER_REGISTER
		    || src->type() != Token::Argument::INTEGER_REGISTER
		    || imm->type() != Token::Argument::IMMEDIATE )
			return false;
		if( !(dst->flags() & Token::Argument::WRITE) )
			return false;

		std::string srcReg;
		if( !vuRegisterKey(*dst, reg) || !vuRegisterKey(*src, srcReg) || reg != srcReg )
			return false;

		return evaluateIntegerExpression( imm->immediate(), immediate );
	}

	bool adjustPlainStoreOffset( Token& store, const std::string& baseReg, long delta )
	{
		for( std::list<Token::Argument>::iterator i = store.arguments().begin(); i != store.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::INTEGER_REGISTER
			    || !(i->flags() & Token::Argument::INDIRECT) )
				continue;

			std::string key;
			if( !vuRegisterKey(*i, key) || key != baseReg )
				continue;

			long offset = 0;
			if( !evaluateIntegerExpression( i->immediate(), offset ) )
				return false;

			std::stringstream s;
			s << (offset + delta);
			i->setImmediate( s.str() );
			return true;
		}
		return false;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CodeGenerator::CodeGenerator()
{
	m_currentCycle    = 0;
	m_enableUpperZeroMoves = false;
	m_ignoredImplicitWawResources = VU_RESOURCE_NONE;
	// Sentinels < 0 by more than FMAC latency so the first flag-reader
	// in the program doesn't trip the cooldown.
	m_lastFMACCycle   = -10;
	m_lastClipwCycle  = -10;
	m_qReadyCycle     = -10;
	m_pReadyCycle     = -10;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CodeGenerator::~CodeGenerator()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::beginProcess(const std::list<Token>& tokens)
{
	// Detect if the source already contains a .vu directive
	bool hasVuDirective = false;
	for( std::list<Token>::const_iterator it = tokens.begin(); it != tokens.end(); ++it )
	{
		if( (*it).operand() && (*it).operand()->isPreprocessor() && (*it).operand()->name() == ".vu" )
		{
			hasVuDirective = true;
			break;
		}
	}

	// When generating named output, always add .vu and .align BEFORE globals and label
	// This matches SCE vcl output format and ensures proper code alignment
	if( m_name.length() > 0 )
	{
		m_codeLines.push_back(std::string("		.vu"));
		m_codeLines.push_back(std::string("		.align 4"));
		m_codeLines.push_back(std::string("		.global	") + m_name + std::string("_CodeStart") );
		m_codeLines.push_back(std::string("		.global	") + m_name + std::string("_CodeEnd") );
		m_codeLines.push_back( std::string(m_name) + "_CodeStart:" );
	}
	else if( !hasVuDirective )
	{
		// No name but source omitted .vu - add directives for dvp-as compatibility
		m_codeLines.push_back(std::string("		.vu"));
		m_codeLines.push_back(std::string("		.align 4"));
	}

	bool exitWritten = true;

	std::list<Token> workTokens = tokens;
	coalesceAdjacentVuIntegerAdds(workTokens);
	fillPreIncrementStoreBranchDelaySlots(workTokens);
	const bool macFlagsDead = !vuTokenListReadsMac(workTokens);
	std::list<Token> scheduledTokens = scheduleVuTokensReadySetWithFlagLiveness(workTokens);
	workTokens.swap(scheduledTokens);
	m_enableUpperZeroMoves = macFlagsDead;
	m_ignoredImplicitWawResources = VU_RESOURCE_NONE;
	fillBranchDelaySlots(workTokens);
	fillDeadFallthroughBranchDelaySlots(workTokens);

	for( std::list<Token>::iterator k = workTokens.begin(); k != workTokens.end(); )
	{
		m_ignoredImplicitWawResources = ignoredImplicitWawResourcesForRemaining(k, workTokens.end());
		Token token = *k;

		//handle label
		if(token.label().length()>0)
			m_codeLines.push_back(token.label() + ":");

		if(!token.operand())
		{
			++k;
			continue;
		}

		if( ((token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER)) && !exitWritten )
		{
			m_codeLines.push_back(std::string("                    nop[E]                          nop"));
			m_codeLines.push_back(std::string("                    nop                             nop"));
			exitWritten = true;
		}

		if(token.flags()&Token::IGNORED)
		{
			++k;
			continue;
		}

		if(!(token.flags()&Token::PROCESSED) && !(token.operand()->flags()&Operand::PREPROCESSOR) )
		{
			++k;
			continue;
		}

		//handle alignment
		if(token.operand()->flags()&Operand::PREPROCESSOR)
		{
			if(token.operand()->name() == ".vu")
			{
				// .vu and alignment are already handled at the start of beginProcess()
				// when m_name is set, so skip adding redundant directives here
				++k;
				continue;
			}
			else if( token.operand()->name() == "--cont" )
			{
				m_codeLines.push_back(std::string("                    nop[E]                          nop"));
				m_codeLines.push_back(std::string("                    nop                             nop"));
				exitWritten = true;
				++k;
				continue;
			}
			else if( token.operand()->name() == "--barrier" )
			{
				++k;
				continue;
			}
		}
		else exitWritten = false;

		if( (token.operand()->flags()&Operand::FILTERED) )
		{
			++k;
			continue;
		}

		if( token.flags() & Token::BRANCH_DELAY_FILLER )
		{
			++k;
			continue;
		}

		if( token.label().length() == 0 && readHazardDelay(token, NULL) > 0 )
		{
			enum { FillerLookaheadLimit = 96 };
			const bool waitsForQ = vuTokenReadsQ(token);
			const bool waitsForP = vuTokenReadsP(token);
			const int qGap = waitsForQ ? (m_qReadyCycle - m_currentCycle) : 0;
			const int pGap = waitsForP ? (m_pReadyCycle - m_currentCycle) : 0;
			if( tokenIsLowerExecutionPath(token) && (qGap > 1 || pGap > 1) )
			{
				const bool waitQ = qGap >= pGap;
				const int waitGap = waitQ ? qGap : pGap;
				std::list<Token>::iterator waitFiller = workTokens.end();
				std::list<Token>::iterator p = k;
				++p;
				unsigned int lookahead = 0;
				for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
				{
					if( (*p).label().length() != 0 )
						break;
					if( !isVuEmittableInstruction(*p) )
						break;
					if( !tokenIsUpperExecutionPath(*p) )
						continue;

					bool canCross = true;
					std::list<Token>::iterator c = k;
					for( ; c != p; ++c )
					{
						if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
						{
							canCross = false;
							break;
						}
					}
					if( canCross && readHazardDelay(*p, NULL) <= waitGap )
					{
						waitFiller = p;
						break;
					}
				}

				if( waitFiller != workTokens.end() )
				{
					emitUpperWithWait(*waitFiller, waitQ);
					workTokens.erase(waitFiller);
					continue;
				}
			}

			std::list<Token>::iterator filler = workTokens.end();
			std::list<Token>::iterator p = k;
			++p;
			unsigned int lookahead = 0;
			for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
			{
				if( (*p).label().length() != 0 )
					break;
				if( !isVuEmittableInstruction(*p) )
					break;
				if( !vuTokenRangeCanBeCrossed(*k, *p)
				    && !isVuPlainMemoryStore(*p)
				    && !vuTokenCanMoveBefore(*p, *k, m_ignoredImplicitWawResources) )
					continue;

				bool canCross = true;
				std::list<Token>::iterator c = k;
				for( ; c != p; ++c )
				{
					if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
					{
						canCross = false;
						break;
					}
				}
				if( canCross && readHazardDelay(*p, NULL) <= 0 )
				{
					filler = p;
					break;
				}
			}

			if( filler != workTokens.end() )
			{
				std::list<Token>::iterator fillerPartner = workTokens.end();
				p = filler;
				++p;
				lookahead = 0;
				for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
				{
					if( (*p).label().length() != 0 )
						break;
					if( !isVuEmittableInstruction(*p) )
						break;
					if( !vuTokenRangeCanBeCrossed(*k, *p)
					    && !isVuPlainMemoryStore(*p)
					    && !vuTokenCanMoveBefore(*p, *k, m_ignoredImplicitWawResources) )
						continue;
					if( !tokensCanPair(*filler, *p) )
						continue;

					bool canCross = true;
					std::list<Token>::iterator c = k;
					for( ; c != p; ++c )
					{
						if( c == filler )
							continue;
						if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
						{
							canCross = false;
							break;
						}
					}
					if( canCross && readHazardDelay(*filler, &*p) <= 0 )
					{
						fillerPartner = p;
						break;
					}
				}

				if( fillerPartner != workTokens.end() )
				{
					emitPairedTokens(*filler, *fillerPartner);
					workTokens.erase(fillerPartner);
				}
				else
					emitSingleToken(*filler);

				workTokens.erase(filler);
				continue;
			}
		}

		if( vuTokenBranchDelaySlots(token) == 1 && nextTokenIsBranchDelayFiller(k, workTokens.end()) )
		{
			std::list<Token>::iterator filler = k;
			++filler;
			padForReadHazards(token, NULL);
			while( readHazardDelay(*filler, NULL) > 1 )
			{
				addNopLine();
				m_currentCycle++;
			}
			emitBranchWithDelayFiller(token, *filler);
			if( isVuTerminalUnconditionalBranch(token) )
				exitWritten = true;
			workTokens.erase(filler);
			++k;
			continue;
		}

		// --- DUAL-PIPE PAIRING ---
		// Look ahead within the current straight-line instruction run for
		// an opposite-pipe partner.  The first implementation only paired
		// adjacent instructions, which left many upper/lower opportunities
		// unused once latency padding had made source order sparse.  This
		// pass may pull a later partner into the current cycle, but only if
		// every crossed instruction has no register/resource conflict and
		// no memory/control side effect that would make reordering risky.
		{
			enum { PairLookaheadLimit = 96 };
			std::list<Token>::iterator p = k;
			++p;
			bool foundPartner = false;
			const Token* partner = NULL;
			std::list<Token>::iterator partnerIt = workTokens.end();
			const int tokenDelay = readHazardDelay(token, NULL);
			unsigned int lookahead = 0;
			for( ; p != workTokens.end() && lookahead < PairLookaheadLimit; ++p, ++lookahead )
			{
				if( (*p).label().length() != 0 )
					break;
				if( !isVuEmittableInstruction(*p) )
					break;
				if( vuTokenBranchDelaySlots(*p) > 0 && nextTokenIsBranchDelayFiller(p, workTokens.end()) )
					break;
				const bool adjacentCandidate = (lookahead == 0);
				const bool adjacentQpProducerPair =
				    adjacentCandidate
				    && (token.operand()->unit() == Operand::FDIV
				        || token.operand()->unit() == Operand::EFU
				        || (*p).operand()->unit() == Operand::FDIV
				        || (*p).operand()->unit() == Operand::EFU);
				const bool adjacentPlainStorePair =
				    adjacentCandidate
				    && (isVuPlainMemoryStore(token) || isVuPlainMemoryStore(*p));
				const bool adjacentXgkickPair =
				    adjacentCandidate
				    && tokenIsUpperExecutionPath(token)
				    && isVuXgkick(*p);
				const bool adjacentBranchPair =
				    adjacentCandidate
				    && tokenIsUpperExecutionPath(token)
				    && vuTokenBranchDelaySlots(*p) > 0;
				if( !adjacentQpProducerPair && !adjacentPlainStorePair && !adjacentXgkickPair
				    && !adjacentBranchPair
				    && !vuTokenRangeCanBeCrossed(*k, *p)
				    && !isVuPlainMemoryStore(*p)
				    && !vuTokenCanMoveBefore(*p, token, m_ignoredImplicitWawResources) )
					continue;
				if( tokensCanPair(token, *p) )
				{
					bool canCross = true;
					std::list<Token>::iterator c = k;
					++c;
					for( ; c != p; ++c )
					{
						if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
						{
							canCross = false;
							break;
						}
					}
					if( canCross && readHazardDelay(token, &*p) <= tokenDelay )
					{
						foundPartner = true;
						partner = &*p;
						partnerIt = p;
						break;
					}
				}
			}

			if( !foundPartner && vuTokenReadsQ(token) && tokenIsUpperExecutionPath(token) )
			{
				const int qGap = m_qReadyCycle - m_currentCycle;
				const int needed = readHazardDelay(token, NULL);
				if( qGap > 1 && qGap >= needed )
				{
					emitUpperWithWait(token, true);
					++k;
					continue;
				}
			}

			padForReadHazards(token, partner);

			if( foundPartner )
			{
				emitPairedTokens(token, *partner);
				if( isVuTerminalUnconditionalBranch(token) || isVuTerminalUnconditionalBranch(*partner) )
					exitWritten = true;
				workTokens.erase(partnerIt);
				++k;
				continue;
			}
		}

		emitSingleToken(token);
		if( isVuTerminalUnconditionalBranch(token) )
			exitWritten = true;
		++k;
	}

	if( !exitWritten )
	{
		// Auto-terminate with an exit pair if the last instruction wasn't closed
		m_codeLines.push_back(std::string("                    nop[E]                          nop"));
		m_codeLines.push_back(std::string("                    nop                             nop"));
		// Do not treat as an error; some VCL programs end in a loop
	}

	if( m_name.length() > 0 )
	{
		// Pad to 16-byte (qword) alignment before _CodeEnd.  Each emitted
		// VU instruction is 8 bytes; without this Sony-emulating `.align 4`
		// the _CodeEnd symbol can land at an 8-mod-16 offset (e.g. 0x9B8).
		// ps2gl's renderer-load path computes the MPG size from
		// _CodeEnd - _CodeStart and feeds `MicrocodePacketSize / 8`
		// instructions through VIF MPG in chunks of up to 256.  When the
		// total is odd, the final chunk's qword count (sendSize64 / 2)
		// truncates by one instruction and the VIF NUM field
		// (sendSize64 & 0xff) disagrees with the actual bytes in the Ref
		// transfer — VIF then pulls bytes from the *next* command into
		// the VU instruction stream, corrupting whatever follows.
		// Symptom in ps2gl/box.elf: general_quad's xgkick never reached
		// or its GIF chain malformed, cube invisible.  Sony's vcl emits
		// `.align 4` for exactly this reason.
		m_codeLines.push_back(std::string("		.align 4"));
		m_codeLines.push_back( std::string(m_name) + "_CodeEnd:" );
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CodeGenerator::addNopLine()
{
	m_codeLines.push_back(std::string("                    nop                             nop"));
}

void CodeGenerator::emitWaitQ()
{
	const int nextCycle = m_currentCycle + 1;
	m_codeLines.push_back(std::string("                    nop                             waitq"));
	m_currentCycle = (m_qReadyCycle > nextCycle) ? m_qReadyCycle : nextCycle;
}

void CodeGenerator::emitWaitP()
{
	const int nextCycle = m_currentCycle + 1;
	m_codeLines.push_back(std::string("                    nop                             waitp"));
	m_currentCycle = (m_pReadyCycle > nextCycle) ? m_pReadyCycle : nextCycle;
}

void CodeGenerator::emitUpperWithWait( const Token& token, bool waitQ )
{
	const int instructionLength = 32;
	std::string instruction = generateInstruction(token);
	std::string outputLine;
	for(int d = 0; d < 20; d++)
		outputLine += " ";
	outputLine += instruction;
	int pad = instructionLength - int(instruction.length());
	if( pad <= 0 )
		outputLine += " ";
	for(int d = 0; d < pad; d++)
		outputLine += " ";
	outputLine += waitQ ? "waitq" : "waitp";

	const int readyCycle = waitQ ? m_qReadyCycle : m_pReadyCycle;
	const int issueCycle = (readyCycle > m_currentCycle) ? readyCycle : m_currentCycle;
	m_codeLines.push_back(outputLine);
	recordRegisterWrites(token, issueCycle);
	if( token.operand()->unit() == Operand::FMAC )
		m_lastFMACCycle = issueCycle;
	if( isVuClipw(token.operand()->name()) )
		m_lastClipwCycle = issueCycle;
	m_currentCycle = issueCycle + 1;
}

bool CodeGenerator::branchNeedsPreBubble( const Token& token ) const
{
	if( vuTokenBranchDelaySlots( token ) == 0 )
		return false;

	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return true;

	if( !(access.instructionFlags & VU_INSTR_BRANCH) )
		return false;

	if( access.instructionFlags & VU_INSTR_REGISTER_BRANCH )
		return true;

	return !(access.instructionFlags & VU_INSTR_UNCONDITIONAL_BRANCH);
}

void CodeGenerator::padForBranchPreBubble( const Token& token )
{
	if( !branchNeedsPreBubble( token ) )
		return;

	if( m_codeLines.empty() || m_codeLines.back() != std::string("                    nop                             nop") )
	{
		addNopLine();
		m_currentCycle++;
	}
}

void CodeGenerator::emitSingleToken( const Token& token )
{
	std::string instruction = generateInstruction(token);
	std::string outputLine = "";

	// emit original sourcecode as a comment
	if( emitSource() )
	{
		std::stringstream s;
		s << " ; Line " << token.line().number() << ": " << token.line().content();
		m_codeLines.push_back( s.str() );
	}

	int instructionLength = 32;

	for(int d = 0; d < 20; d++)
		outputLine += " ";

	if(tokenIsLowerExecutionPath(token))
	{
		outputLine += "nop";

		for(int d = 0; d < instructionLength-3; d++)
			outputLine += " ";

		outputLine += instruction;
	}
	else
	{
		outputLine += instruction;

		if( !token.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(instruction.length())) <= 0 )
				outputLine += " ";

			for(int d = 0; d < instructionLength-int(instruction.length()); d++)
				outputLine += " ";

			outputLine += "nop";
		}
	}

	const unsigned int branchDelaySlots = vuTokenBranchDelaySlots( token );
	if( branchDelaySlots > 0 )
	{
		padForBranchPreBubble( token );
		m_codeLines.push_back(outputLine);
		m_currentCycle++;
		for( unsigned int i = 0; i < branchDelaySlots; ++i )
		{
			addNopLine();
			m_currentCycle++;
		}
	}
	else
	{
		int issueCycle = m_currentCycle;
		m_codeLines.push_back(outputLine);
		m_currentCycle++;
		recordRegisterWrites(token, issueCycle);
	}

	// Remember this cycle as the most recent FMAC / clipw so
	// downstream flag-readers can pad to the 4-cycle pipeline.
	if( token.operand()->unit() == Operand::FMAC || emitsAsUpperZeroMove(token) )
		m_lastFMACCycle = m_currentCycle - 1;
	if( isVuClipw(token.operand()->name()) )
		m_lastClipwCycle = m_currentCycle - 1;

}

void CodeGenerator::emitBranchWithDelayFiller( const Token& branch, const Token& filler )
{
	std::string branchInstruction = generateInstruction(branch);
	std::string branchLine = "";
	std::string fillerInstruction = generateInstruction(filler);
	std::string fillerLine = "";
	const int instructionLength = 32;

	if( emitSource() )
	{
		std::stringstream s;
		s << " ; Line " << branch.line().number() << ": " << branch.line().content();
		m_codeLines.push_back( s.str() );
	}

	for(int d = 0; d < 20; d++)
		branchLine += " ";

	if(tokenIsLowerExecutionPath(branch))
	{
		branchLine += "nop";
		for(int d = 0; d < instructionLength-3; d++)
			branchLine += " ";
		branchLine += branchInstruction;
	}
	else
	{
		branchLine += branchInstruction;
		if( !branch.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(branchInstruction.length())) <= 0 )
				branchLine += " ";
			for(int d = 0; d < instructionLength-int(branchInstruction.length()); d++)
				branchLine += " ";
			branchLine += "nop";
		}
	}

	padForBranchPreBubble( branch );
	m_codeLines.push_back(branchLine);
	m_currentCycle++;

	if( emitSource() )
	{
		std::stringstream s;
		s << " ; Line " << filler.line().number() << ": " << filler.line().content();
		m_codeLines.push_back( s.str() );
	}

	for(int d = 0; d < 20; d++)
		fillerLine += " ";

	if(tokenIsLowerExecutionPath(filler))
	{
		fillerLine += "nop";
		for(int d = 0; d < instructionLength-3; d++)
			fillerLine += " ";
		fillerLine += fillerInstruction;
	}
	else
	{
		fillerLine += fillerInstruction;
		if( !filler.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(fillerInstruction.length())) <= 0 )
				fillerLine += " ";
			for(int d = 0; d < instructionLength-int(fillerInstruction.length()); d++)
				fillerLine += " ";
			fillerLine += "nop";
		}
	}

	m_codeLines.push_back(fillerLine);
	recordRegisterWrites(filler, m_currentCycle);
	if( filler.operand()->unit() == Operand::FMAC || emitsAsUpperZeroMove(filler) )
		m_lastFMACCycle = m_currentCycle;
	if( isVuClipw(filler.operand()->name()) )
		m_lastClipwCycle = m_currentCycle;
	m_currentCycle++;
}

void CodeGenerator::emitPairedTokens( const Token& a, const Token& b )
{
	const unsigned int aBranchDelaySlots = vuTokenBranchDelaySlots( a );
	const unsigned int bBranchDelaySlots = vuTokenBranchDelaySlots( b );
	const unsigned int branchDelaySlots = aBranchDelaySlots > bBranchDelaySlots ? aBranchDelaySlots : bBranchDelaySlots;
	std::string pairedLine;
	if( tokenIsLowerExecutionPath(a) )
		pairedLine = formatPairedLine(b, a);
	else
		pairedLine = formatPairedLine(a, b);
	if( branchNeedsPreBubble(a) )
		padForBranchPreBubble(a);
	if( branchNeedsPreBubble(b) )
		padForBranchPreBubble(b);
	m_codeLines.push_back(pairedLine);

	// Either side of the pair could be the FMAC / clipw we need to remember
	// for downstream flag-reader cooldowns.
	recordRegisterWrites(a, m_currentCycle);
	recordRegisterWrites(b, m_currentCycle);
	if( a.operand()->unit() == Operand::FMAC || b.operand()->unit() == Operand::FMAC
	    || emitsAsUpperZeroMove(a) || emitsAsUpperZeroMove(b) )
		m_lastFMACCycle = m_currentCycle;
	if( isVuClipw(a.operand()->name()) || isVuClipw(b.operand()->name()) )
		m_lastClipwCycle = m_currentCycle;
	m_currentCycle++;
	for( unsigned int i = 0; i < branchDelaySlots; ++i )
	{
		addNopLine();
		m_currentCycle++;
	}
}

bool CodeGenerator::emitsAsUpperZeroMove( const Token& token ) const
{
	return ( m_enableUpperZeroMoves || (m_ignoredImplicitWawResources & VU_RESOURCE_MAC) )
	    && isVuZeroMoveFromVf00(token);
}

bool CodeGenerator::tokenIsLowerExecutionPath( const Token& token ) const
{
	if( emitsAsUpperZeroMove(token) )
		return false;
	return token.operand() && token.operand()->isLowerExecutionPath();
}

bool CodeGenerator::tokenIsUpperExecutionPath( const Token& token ) const
{
	if( emitsAsUpperZeroMove(token) )
		return true;
	return token.operand() && token.operand()->isUpperExecutionPath();
}

int CodeGenerator::readHazardDelay( const Token& token, const Token* partner ) const
{
	std::list<std::string> reads;
	collectVuRegisterReadKeys(token, reads);
	if( partner )
		collectVuRegisterReadKeys(*partner, reads);

	bool readsQ = vuTokenReadsQ(token);
	bool readsP = vuTokenReadsP(token);
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ(*partner);
		readsP = readsP || vuTokenReadsP(*partner);
	}

	int needed = 0;
	for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
	{
		std::map<std::string, int>::const_iterator ready = m_registerReadyCycle.find(*i);
		if( ready == m_registerReadyCycle.end() )
			continue;
		int readyCycle = ready->second;
		std::map<std::string, std::string>::const_iterator producer = m_registerProducerMnemonic.find(*i);
		if( producer != m_registerProducerMnemonic.end()
		    && isVuFtoiConversion(producer->second)
		    && ( (isVuMtir(token) && vuTokenReadsRegister(token, *i))
		         || (partner && isVuMtir(*partner) && vuTokenReadsRegister(*partner, *i)) ) )
			readyCycle -= 4;
		if( producer != m_registerProducerMnemonic.end()
		    && isVuLoadInstruction(producer->second)
		    && ( (isVuFtoiConversion(lowerVuTokenName(token)) && vuTokenReadsRegister(token, *i))
		         || (partner && isVuFtoiConversion(lowerVuTokenName(*partner)) && vuTokenReadsRegister(*partner, *i)) ) )
			readyCycle -= 4;
		const int gap = readyCycle - m_currentCycle;
		if( gap > needed )
			needed = gap;
	}
	if( readsQ )
	{
		const int gap = m_qReadyCycle - m_currentCycle;
		if( gap > needed )
			needed = gap;
	}
	if( readsP )
	{
		const int gap = m_pReadyCycle - m_currentCycle;
		if( gap > needed )
			needed = gap;
	}

	const int flagCycle = m_currentCycle + needed;
	bool readsMac = isVuMacReader(token.operand()->name());
	bool readsClip = isVuClipReader(token.operand()->name());
	if( partner && partner->operand() )
	{
		const std::string& name = partner->operand()->name();
		readsMac = readsMac || isVuMacReader(name);
		readsClip = readsClip || isVuClipReader(name);
	}

	int flagDelay = 0;
	if( readsMac )
	{
		const int gap = flagCycle - m_lastFMACCycle;
		if( 4 - gap > flagDelay )
			flagDelay = 4 - gap;
	}
	if( readsClip )
	{
		const int gap = flagCycle - m_lastClipwCycle;
		if( 4 - gap > flagDelay )
			flagDelay = 4 - gap;
	}
	if( flagDelay > 0 )
		needed += flagDelay;

	return needed;
}

void CodeGenerator::padForReadHazards( const Token& token, const Token* partner )
{
	bool readsQ = vuTokenReadsQ(token);
	bool readsP = vuTokenReadsP(token);
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ(*partner);
		readsP = readsP || vuTokenReadsP(*partner);
	}

	while( true )
	{
		int needed = readHazardDelay(token, partner);
		if( needed <= 0 )
			break;

		const int qGap = readsQ ? (m_qReadyCycle - m_currentCycle) : 0;
		const int pGap = readsP ? (m_pReadyCycle - m_currentCycle) : 0;
		if( qGap > 1 && qGap >= pGap )
		{
			emitWaitQ();
			continue;
		}
		if( pGap > 1 )
		{
			emitWaitP();
			continue;
		}

		addNopLine();
		m_currentCycle++;
	}
}

void CodeGenerator::recordRegisterWrites( const Token& token, int issueCycle )
{
	if( !token.operand() || token.operand()->latency() <= 1 )
		return;

	const int latency = token.operand()->latency();
	std::list<std::string> writes;
	collectVuRegisterWriteKeys(token, writes);
	for( std::list<std::string>::const_iterator i = writes.begin(); i != writes.end(); ++i )
	{
		m_registerReadyCycle[*i] = issueCycle + latency + 1;
		m_registerProducerMnemonic[*i] = lowerVuTokenName(token);
	}
	if( vuTokenWritesQ(token) )
		m_qReadyCycle = issueCycle + latency + 1;
	if( vuTokenWritesP(token) )
		m_pReadyCycle = issueCycle + latency + 1;
}

unsigned int CodeGenerator::ignoredImplicitWawResourcesForRemaining( std::list<Token>::const_iterator begin,
                                                                     std::list<Token>::const_iterator end ) const
{
	bool readsMac = false;
	bool readsClip = false;
	for( std::list<Token>::const_iterator i = begin; i != end; ++i )
	{
		if( !i->operand() )
			continue;
		const std::string& name = i->operand()->name();
		readsMac = readsMac || isVuMacReader(name);
		readsClip = readsClip || isVuClipReader(name);
	}

	unsigned int mask = VU_RESOURCE_NONE;
	if( !readsMac )
		mask |= VU_RESOURCE_MAC;
	if( !readsClip )
		mask |= VU_RESOURCE_CLIP;
	return mask;
}

void CodeGenerator::fillBranchDelaySlots( std::list<Token>& tokens ) const
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( vuTokenBranchDelaySlots(*branch) != 1 )
			continue;
		if( branch->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
			continue;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess(*branch, branchAccess) )
			continue;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			continue;
		if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
			continue;

		if( movePreIncrementStoreIntoBranchDelaySlot(tokens, branch) )
			continue;
		if( moveIndependentStoreIntoBranchDelaySlot(tokens, branch) )
			continue;

		if( branch == tokens.begin() )
			continue;
		std::list<Token>::iterator candidate = branch;
		--candidate;
		if( !canMoveIntoBranchDelaySlot(*candidate, *branch) )
			continue;

		Token filler = *candidate;
		filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);
		tokens.erase(candidate);
		std::list<Token>::iterator afterBranch = branch;
		++afterBranch;
		tokens.insert(afterBranch, filler);
	}
}

void CodeGenerator::fillPreIncrementStoreBranchDelaySlots( std::list<Token>& tokens ) const
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( vuTokenBranchDelaySlots(*branch) != 1 )
			continue;
		if( branch->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
			continue;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess(*branch, branchAccess) )
			continue;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			continue;

		movePreIncrementStoreIntoBranchDelaySlot(tokens, branch);
	}
}

bool CodeGenerator::movePreIncrementStoreIntoBranchDelaySlot( std::list<Token>& tokens,
                                                              std::list<Token>::iterator branch ) const
{
	if( branch == tokens.begin() )
		return false;
	if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
		return false;

	std::list<Token>::iterator increment = branch;
	--increment;
	if( increment == tokens.begin() )
		return false;
	std::list<Token>::iterator store = increment;
	--store;

	if( store->label().length() != 0 || increment->label().length() != 0 )
		return false;
	if( store->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER) )
		return false;
	if( !isVuPlainMemoryStore(*store) )
		return false;

	std::string incrementReg;
	long incrementAmount = 0;
	if( !integerSelfImmediateAdd(*increment, incrementReg, incrementAmount) )
		return false;
	(void)incrementAmount;
	if( !vuTokenReadsRegister(*branch, incrementReg) )
		return false;

	VuTokenResourceAccess storeAccess;
	if( !buildVuTokenResourceAccess(*store, storeAccess) )
		return false;
	if( !storeAccess.hasMemoryBase || !storeAccess.hasMemoryOffset )
		return false;
	if( storeAccess.memoryBaseRegister != incrementReg )
		return false;
	if( storeAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( storeAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;

	VuTokenResourceAccess incrementAccess;
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(*increment, incrementAccess)
	    || !buildVuTokenResourceAccess(*branch, branchAccess) )
		return false;
	if( incrementAccess.memoryKind != VU_MEMORY_NONE
	    || incrementAccess.implicitReads != VU_RESOURCE_NONE
	    || incrementAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;
	if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
		return false;

	Token filler = *store;
	if( !adjustPlainStoreOffset(filler, incrementReg, -incrementAmount) )
		return false;
	filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);

	tokens.erase(store);
	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	tokens.insert(afterBranch, filler);
	return true;
}

bool CodeGenerator::moveIndependentStoreIntoBranchDelaySlot( std::list<Token>& tokens,
                                                             std::list<Token>::iterator branch ) const
{
	if( branch == tokens.begin() )
		return false;
	if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
		return false;

	std::list<Token>::iterator increment = branch;
	--increment;
	if( increment == tokens.begin() )
		return false;
	std::list<Token>::iterator store = increment;
	--store;

	if( store->label().length() != 0 || increment->label().length() != 0 )
		return false;
	if( store->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER) )
		return false;
	if( !isVuPlainMemoryStore(*store) )
		return false;

	std::string incrementReg;
	long incrementAmount = 0;
	if( !integerSelfImmediateAdd(*increment, incrementReg, incrementAmount) )
		return false;
	if( !vuTokenReadsRegister(*branch, incrementReg) )
		return false;

	VuTokenResourceAccess storeAccess;
	VuTokenResourceAccess incrementAccess;
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(*store, storeAccess)
	    || !buildVuTokenResourceAccess(*increment, incrementAccess)
	    || !buildVuTokenResourceAccess(*branch, branchAccess) )
		return false;
	if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
		return false;
	if( storeAccess.memoryFlags != VU_MEMORY_FLAG_NONE
	    || storeAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;
	if( intersectsKeys(storeAccess.registerReads, incrementAccess.registerWrites)
	    || intersectsKeys(storeAccess.registerWrites, incrementAccess.registerReads)
	    || intersectsKeys(storeAccess.registerWrites, incrementAccess.registerWrites) )
		return false;
	if( intersectsKeys(storeAccess.registerWrites, branchAccess.registerReads)
	    || intersectsKeys(storeAccess.registerReads, branchAccess.registerWrites) )
		return false;

	Token filler = *store;
	filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);

	tokens.erase(store);
	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	tokens.insert(afterBranch, filler);
	return true;
}

void CodeGenerator::fillDeadFallthroughBranchDelaySlots( std::list<Token>& tokens ) const
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( vuTokenBranchDelaySlots(*branch) != 1 )
			continue;
		if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
			continue;
		moveDeadFallthroughIntoBranchDelaySlot(tokens, branch);
	}
}

bool CodeGenerator::moveDeadFallthroughIntoBranchDelaySlot( std::list<Token>& tokens,
                                                            std::list<Token>::iterator branch ) const
{
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(*branch, branchAccess) )
		return false;
	if( (branchAccess.instructionFlags & VU_INSTR_BRANCH) == 0 )
		return false;
	if( branchAccess.instructionFlags & (VU_INSTR_UNCONDITIONAL_BRANCH
	                                   | VU_INSTR_LINK_BRANCH
	                                   | VU_INSTR_REGISTER_BRANCH) )
		return false;

	std::list<Token>::iterator candidate = branch;
	++candidate;
	if( candidate == tokens.end() )
		return false;
	if( candidate->label().length() != 0 )
		return false;
	if( candidate->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER) )
		return false;
	if( !isVuEmittableInstruction(*candidate) )
		return false;
	if( !candidate->operand() || candidate->operand()->unit() != Operand::IALU )
		return false;
	if( candidate->operand()->latency() > 1 )
		return false;

	VuTokenResourceAccess candidateAccess;
	if( !buildVuTokenResourceAccess(*candidate, candidateAccess) )
		return false;
	if( candidateAccess.memoryKind != VU_MEMORY_NONE
	    || candidateAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( candidateAccess.branchDelaySlots > 0 )
		return false;
	if( candidateAccess.instructionFlags & (VU_INSTR_BRANCH
	                                      | VU_INSTR_WAIT_Q
	                                      | VU_INSTR_WAIT_P
	                                      | VU_INSTR_WRITES_Q
	                                      | VU_INSTR_WRITES_P
	                                      | VU_INSTR_XGKICK) )
		return false;
	if( candidateAccess.implicitReads != VU_RESOURCE_NONE
	    || candidateAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;
	if( candidateAccess.registerWrites.empty() )
		return false;
	for( std::list<std::string>::const_iterator i = candidateAccess.registerWrites.begin();
	     i != candidateAccess.registerWrites.end(); ++i )
	{
		if( i->size() < 2 || i->compare(0, 2, "VI") != 0 )
			return false;
	}

	std::string targetLabel;
	if( !branchTargetLabel(*branch, targetLabel) )
		return false;

	std::list<Token>::iterator target = tokens.end();
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( i->label() == targetLabel )
		{
			target = i;
			break;
		}
	}
	if( target == tokens.end() )
		return false;
	if( target->lineNumber() <= branch->lineNumber() )
		return false;
	if( !writesAreDeadFromTarget(candidateAccess.registerWrites, target, tokens.end()) )
		return false;

	Token filler = *candidate;
	filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);
	tokens.erase(candidate);
	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	tokens.insert(afterBranch, filler);
	return true;
}

bool CodeGenerator::canMoveIntoBranchDelaySlot( const Token& candidate, const Token& branch ) const
{
	if( candidate.label().length() != 0 )
		return false;
	if( candidate.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER) )
		return false;
	if( !isVuEmittableInstruction(candidate) )
		return false;
	if( vuTokenBranchDelaySlots(candidate) > 0 )
		return false;
	if( !candidate.operand() || candidate.operand()->unit() != Operand::IALU )
		return false;
	if( candidate.operand()->latency() > 1 )
		return false;

	VuTokenResourceAccess candidateAccess;
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(candidate, candidateAccess)
	    || !buildVuTokenResourceAccess(branch, branchAccess) )
		return false;

	if( candidateAccess.memoryKind != VU_MEMORY_NONE
	    || candidateAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( candidateAccess.branchDelaySlots > 0 )
		return false;
	if( candidateAccess.instructionFlags & (VU_INSTR_BRANCH
	                                      | VU_INSTR_WAIT_Q
	                                      | VU_INSTR_WAIT_P
	                                      | VU_INSTR_WRITES_Q
	                                      | VU_INSTR_WRITES_P
	                                      | VU_INSTR_XGKICK) )
		return false;
	if( candidateAccess.implicitReads != VU_RESOURCE_NONE
	    || candidateAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;

	if( intersectsKeys(candidateAccess.registerWrites, branchAccess.registerReads)
	    || intersectsKeys(candidateAccess.registerWrites, branchAccess.registerWrites)
	    || intersectsKeys(candidateAccess.registerReads, branchAccess.registerWrites) )
		return false;

	if( candidateAccess.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
		return false;
	if( branchAccess.implicitWrites & (candidateAccess.implicitReads | candidateAccess.implicitWrites) )
		return false;

	return true;
}

bool CodeGenerator::nextTokenIsBranchDelayFiller( std::list<Token>::iterator token,
                                                  std::list<Token>::iterator end ) const
{
	if( token == end )
		return false;
	std::list<Token>::iterator next = token;
	++next;
	return next != end && (next->flags() & Token::BRANCH_DELAY_FILLER) != 0;
}

bool CodeGenerator::branchTargetLabel( const Token& branch, std::string& label ) const
{
	for( std::list<Token::Argument>::const_iterator i = branch.arguments().begin(); i != branch.arguments().end(); ++i )
	{
		if( i->flags() & Token::Argument::BRANCH )
		{
			if( i->type() != Token::Argument::IMMEDIATE )
				return false;
			label = i->immediate();
			return true;
		}
	}
	return false;
}

bool CodeGenerator::writesAreDeadFromTarget( const std::list<std::string>& writes,
                                             std::list<Token>::iterator target,
                                             std::list<Token>::iterator end ) const
{
	std::list<std::string> remaining = writes;
	for( std::list<Token>::iterator i = target; i != end; ++i )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess(*i, access) )
			continue;

		if( intersectsKeys(access.registerReads, remaining) )
			return false;

		for( std::list<std::string>::iterator r = remaining.begin(); r != remaining.end(); )
		{
			if( containsKey(access.registerWrites, *r) )
				r = remaining.erase(r);
			else
				++r;
		}
		if( remaining.empty() )
			return true;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dual-pipe pairing helpers.
//
// VU1 issues two instructions per cycle: one upper-pipe (FMAC) and one
// lower-pipe (LSU / IALU / BRU / FDIV / RANDU / EFU).  Sony's vcl fills both
// slots per cycle for ~2x throughput; openvcl historically emits a single
// instruction per cycle with NOP on the unused pipe.  The scheduler now does
// a small, conservative straight-line lookahead to fill latency gaps and pair
// independent upper/lower instructions without crossing labels, control flow,
// memory side effects, or register/resource dependencies.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::tokensCanPair( const Token& a, const Token& b ) const
{
	if( !isVuEmittableInstruction(a) || !isVuEmittableInstruction(b) )
		return false;

	// PREORDERED tokens (raw .vsm passthrough) must keep their
	// emission position exactly as written.
	if( (a.flags() & Token::PREORDERED) || (b.flags() & Token::PREORDERED) )
		return false;

	// Must straddle the upper/lower pipe split.
	if( tokenIsLowerExecutionPath(a) == tokenIsLowerExecutionPath(b) )
		return false;

	// FMAC writes MAC with 4-cycle latency; clipw additionally writes
	// CLIP.  Keep same-flag readers out of the pair.  A non-clip FMAC
	// can still pair with a CLIP reader such as fcand once the previous
	// clipw result is latency-ready.
	bool aFMAC = (a.operand()->unit() == Operand::FMAC) || emitsAsUpperZeroMove(a);
	bool bFMAC = (b.operand()->unit() == Operand::FMAC) || emitsAsUpperZeroMove(b);
	return vuTokenPairResourcesAreIndependent(a, b, aFMAC, bFMAC);
}

std::string CodeGenerator::formatPairedLine( const Token& upper, const Token& lower )
{
	const int instructionLength = 32;
	std::string upperInstr = generateInstruction(upper);
	std::string lowerInstr = generateInstruction(lower);

	std::string line;
	for( int d = 0; d < 20; d++ )
		line += " ";
	line += upperInstr;

	int pad = instructionLength - int(upperInstr.length());
	if( pad <= 0 )
		line += " ";
	for( int d = 0; d < pad; d++ )
		line += " ";

	line += lowerInstr;
	return line;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::generateInstruction(const Token& token)
{
	std::string codeLine;

	if( emitsAsUpperZeroMove(token) )
		return generateUpperZeroMoveInstruction(token);

	codeLine = generateOperand(token);
	codeLine += " ";

	for(std::list<Token::Argument>::const_iterator i = token.arguments().begin(); i != token.arguments().end(); ++i)
	{
		Token::Argument arg = *i;
		std::string currentArg;

		switch(arg.type())
		{
			case Token::Argument::FLOAT_REGISTER: currentArg = registerArg(arg,token); break;
			case Token::Argument::INTEGER_REGISTER: currentArg = registerArg(arg,token); break; 
			case Token::Argument::IMMEDIATE: currentArg = immediateArg(arg,token); break;
			case Token::Argument::Q: currentArg = "q"; break;
			case Token::Argument::ACCUMULATOR: currentArg = accumulatorArg(arg,token); break;
			case Token::Argument::P: currentArg = "p"; break;
			case Token::Argument::I: currentArg = "i"; break;
			case Token::Argument::R: currentArg = "r"; break;
			case Token::Argument::STRING: currentArg = immediateArg(arg,token); break;

			default: currentArg = "<<< ERROR : BUG IN TOKENIZER : ERROR >>>"; break;
		}

		// try to solve this some other way.
		i++;

		if(i != token.arguments().end())
			currentArg += ", "; 

		i--;

		codeLine += currentArg;
	}

	return codeLine;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::generateUpperZeroMoveInstruction( const Token& token )
{
	static const char* fieldnames = "xyzw";
	std::string codeLine = "max.";
	for(unsigned int t = 0; t < 4; t++ )
	{
		if(token.fields() & (1<<t))
			codeLine += fieldnames[t];
	}
	codeLine += " ";

	std::list<Token::Argument>::const_iterator dst = token.arguments().begin();
	std::list<Token::Argument>::const_iterator src = dst;
	++src;

	codeLine += registerArg(*dst, token);
	codeLine += ", ";
	codeLine += registerArg(*src, token);
	codeLine += ", ";
	codeLine += registerArg(*src, token);
	return codeLine;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::registerArg(const Token::Argument& arg, const Token& token)
{
	static const char* fieldnames = "xyzw";
	unsigned int fields = cleanFields( arg.fields(), arg.flags(), token );
	std::string argument;

	if( arg.flags() & Token::Argument::INDIRECT )
	{
		// Evaluate the immediate expression if EVALUATE flag is set
		if( arg.flags() & Token::Argument::EVALUATE )
		{
			Expression e;
			e.setCustomOperators( Math::mathOperators() );
			if( e.process( arg.immediate() ) && e.solve() )
			{
				// VU memory offsets must be integers - truncate like C integer division
				std::stringstream s;
				s << static_cast<long>(e.result());
				argument += s.str();
			}
			else
				argument += arg.immediate();
		}
		else
			argument += arg.immediate();
		argument += "(";
		if( arg.flags() & Token::Argument::PREDEC )
			argument += "--";
	}

	if( arg.content() == Token::Argument::ALIAS )
	{
		assert( arg.dependency() );
		assert( arg.dependency()->alias() );
		assert( arg.dependency()->alias()->allocatedRegister() );
		argument += arg.dependency()->alias()->allocatedRegister()->name();
	}
	else
	{
		const char* prefix = ((arg.type() == Token::Argument::FLOAT_REGISTER) ? "VF" : "VI");
		std::stringstream s;
		s << prefix << std::setw(2) << std::setfill('0') << arg.regNumber();
		argument += s.str();
	}

	if( arg.flags() & Token::Argument::INDIRECT )
	{
		if( arg.flags() & Token::Argument::POSTINC )
			argument += "++";
		argument += ")";
	}

	if( !(arg.flags() & Token::Argument::ROTATE) )
	{
		for(unsigned int t = 0; t < 4; t++)
		{
			if(fields & (1<<t))
				argument += fieldnames[t];
		}
	}

	return argument;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::immediateArg(const Token::Argument& arg, const Token& /*token*/)
{
	if( arg.flags() & Token::Argument::EVALUATE )
	{
		Expression e;

		e.setCustomOperators( Math::mathOperators() );

		if( !e.process( arg.immediate() ) )
			return arg.immediate();

		if( !e.solve() )
			return arg.immediate();

		std::stringstream s;
		if( arg.flags() & Token::Argument::RAW )
		{
			// RAW flag (for LOI): output float as IEEE 754 hex representation
			float f = static_cast<float>(e.result());
			union { float f; unsigned int u; } conv;
			conv.f = f;
			s << "0x" << std::hex << std::setfill('0') << std::setw(8) << conv.u;
		}
		else
		{
			// Integer immediate: truncate like C integer division
			s << static_cast<long>(e.result());
		}
		return s.str();
	}
	else return arg.immediate();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::accumulatorArg( const Token::Argument& arg, const Token& token )
{
	static const char* fieldnames = "xyzw";
	unsigned int fields = cleanFields( arg.fields(), arg.flags(), token );
	std::string argument;

	argument = "ACC";

	for(unsigned int t = 0; t < 4; t++)
	{
		if(fields & (1<<t))
			argument += fieldnames[t];
	}

	return argument;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int CodeGenerator::cleanFields( unsigned int fields, unsigned int flags, const Token& token )
{
	if( fields )
	{
		// remove unnecessary fields


		if( !(flags & (Token::Argument::BROADCAST|Token::Argument::FLAG)) )
		{
			if( token.fields() )
			{
				if( (token.fields() == fields) )
					fields = 0;
			}
			if( (token.operand()->flags() & Operand::XYZ) == Operand::XYZ )
			{
//				if( fields == (Token::X|Token::Y|Token::Z) )
//					fields = 0;
			}
			else if( token.operand()->flags() & Operand::DEST )
			{
				if( fields == (Token::X|Token::Y|Token::Z|Token::W) )
					fields = 0;
			}
		}
	}
	else if( token.broadcast() && !(flags & Token::Argument::FLAG) && (flags & Token::Argument::BROADCAST) )
	{
		// set the broadcast-field for the register

		fields = token.broadcast();
	}

	return fields;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::generateOperand(const Token& token)
{
	static const char* fieldnames = "xyzw";
	std::string operand = "";

	if(!token.operand())
		return operand; 

	operand = token.operand()->name();
	makelower(operand);

	//add broadcast
	if((token.operand()->flags() & Operand::BROADCAST) == Operand::BROADCAST)
	{
		for(unsigned int t = 0; t < 4; t++ )
		{
			if( token.broadcast() & (1<<t) )
				operand += fieldnames[t];
		}
	}

	// add flags
	if(token.flags() & (Token::E|Token::D|Token::T))
	{
		operand += "[";
		if( token.flags() & Token::E) operand += "E";
		if( token.flags() & Token::D) operand += "D";
		if( token.flags() & Token::T) operand += "T";
		operand += "]";
	}

	unsigned int fields = token.fields();
	if( !fields
	    && token.operand()->unit() == Operand::FMAC
	    && (token.operand()->flags() & Operand::DEST) )
	{
		fields = Token::X | Token::Y | Token::Z | Token::W;
	}

	//generate fileds
	if(fields)
	{
		operand += ".";
		for(unsigned int t = 0; t < 4; t++ )
		{
			if(fields & (1<<t))
				operand += fieldnames[t];
		}
	}

	return operand;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::write(std::ostream& stream)
{
	for(std::list<std::string>::const_iterator i = m_codeLines.begin(); i != m_codeLines.end(); ++i)
		stream << (*i) << std::endl;

	return true;
}

}
