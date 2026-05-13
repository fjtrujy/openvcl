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
	const bool macFlagsDead = !vuTokenListReadsMac(workTokens);
	std::list<Token> scheduledTokens = scheduleVuTokensReadySetWithFlagLiveness(workTokens);
	workTokens.swap(scheduledTokens);
	m_enableUpperZeroMoves = macFlagsDead;
	m_ignoredImplicitWawResources = VU_RESOURCE_NONE;

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
		if( m_codeLines.empty() || m_codeLines.back() != std::string("                    nop                             nop") )
		{
			addNopLine();
			m_currentCycle++;
		}
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
