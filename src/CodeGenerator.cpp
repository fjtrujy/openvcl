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

// Forward declarations for the cycle-cooldown helpers used in
// beginProcess() — their bodies live with the other emit-time helpers
// further down in this file.
static bool isMacReader( const std::string& name );
static bool isClipReader( const std::string& name );
static bool isClipw( const std::string& name );
namespace
{
	bool isXgkick( const Token& token );
	bool isPlainMemoryStore( const Token& token );
	bool isPlainMemoryLoad( const Token& token );
	bool tokenReadsQ( const Token& token );
	bool tokenReadsP( const Token& token );
	void coalesceAdjacentIntegerAdds( std::list<Token>& tokens );
}

CodeGenerator::CodeGenerator()
{
	m_currentCycle    = 0;
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
	coalesceAdjacentIntegerAdds(workTokens);

	for( std::list<Token>::iterator k = workTokens.begin(); k != workTokens.end(); )
	{
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
			enum { FillerLookaheadLimit = 48 };
			std::list<Token>::iterator filler = workTokens.end();
			std::list<Token>::iterator p = k;
			++p;
			unsigned int lookahead = 0;
			for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
			{
				if( (*p).label().length() != 0 )
					break;
				if( !isEmittableInstruction(*p) )
					break;
				if( !tokenRangeCanBeCrossed(*k, *p) && !isPlainMemoryStore(*p) )
					break;

				bool canCross = true;
				std::list<Token>::iterator c = k;
				for( ; c != p; ++c )
				{
					if( !tokenCanMoveBefore(*p, *c) )
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
					if( !isEmittableInstruction(*p) )
						break;
					if( !tokenRangeCanBeCrossed(*k, *p) && !isPlainMemoryStore(*p) )
						break;
					if( !tokensCanPair(*filler, *p) )
						continue;

					bool canCross = true;
					std::list<Token>::iterator c = k;
					for( ; c != p; ++c )
					{
						if( c == filler )
							continue;
						if( !tokenCanMoveBefore(*p, *c) )
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
			enum { PairLookaheadLimit = 48 };
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
				if( !isEmittableInstruction(*p) )
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
				    && (isPlainMemoryStore(token) || isPlainMemoryStore(*p));
				const bool adjacentXgkickPair =
				    adjacentCandidate
				    && !token.operand()->isLowerExecutionPath()
				    && isXgkick(*p);
				if( !adjacentQpProducerPair && !adjacentPlainStorePair && !adjacentXgkickPair
				    && !tokenRangeCanBeCrossed(*k, *p) && !isPlainMemoryStore(*p) )
					break;
				if( tokensCanPair(token, *p) )
				{
					bool canCross = true;
					std::list<Token>::iterator c = k;
					++c;
					for( ; c != p; ++c )
					{
						if( !tokenCanMoveBefore(*p, *c) )
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

			if( !foundPartner && tokenReadsQ(token) && !token.operand()->isLowerExecutionPath() )
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
				workTokens.erase(partnerIt);
				++k;
				continue;
			}
		}

		emitSingleToken(token);
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
	if( isClipw(token.operand()->name()) )
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

	if(token.operand()->isLowerExecutionPath())
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

	if( token.operand()->unit() == Operand::BRU )
	{
		addNopLine();
		m_currentCycle++;
		m_codeLines.push_back(outputLine);
		m_currentCycle++;
		addNopLine();
		m_currentCycle++;
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
	if( token.operand()->unit() == Operand::FMAC )
		m_lastFMACCycle = m_currentCycle - 1;
	if( isClipw(token.operand()->name()) )
		m_lastClipwCycle = m_currentCycle - 1;

}

void CodeGenerator::emitPairedTokens( const Token& a, const Token& b )
{
	std::string pairedLine;
	if( a.operand()->isLowerExecutionPath() )
		pairedLine = formatPairedLine(b, a);
	else
		pairedLine = formatPairedLine(a, b);
	m_codeLines.push_back(pairedLine);

	// Either side of the pair could be the FMAC / clipw we need to remember
	// for downstream flag-reader cooldowns.
	recordRegisterWrites(a, m_currentCycle);
	recordRegisterWrites(b, m_currentCycle);
	if( a.operand()->unit() == Operand::FMAC || b.operand()->unit() == Operand::FMAC )
		m_lastFMACCycle = m_currentCycle;
	if( isClipw(a.operand()->name()) || isClipw(b.operand()->name()) )
		m_lastClipwCycle = m_currentCycle;
	m_currentCycle++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	bool registerKey( const Token::Argument& arg, std::string& key )
	{
		if( arg.type() != Token::Argument::FLOAT_REGISTER
		    && arg.type() != Token::Argument::INTEGER_REGISTER )
			return false;

		if( arg.content() == Token::Argument::ALIAS )
		{
			Dependency* dependency = arg.dependency();
			if( dependency && dependency->alias()
			    && dependency->alias()->allocatedRegister() )
			{
				key = dependency->alias()->allocatedRegister()->name();
				return true;
			}
		}

		std::stringstream s;
		s << ((arg.type() == Token::Argument::FLOAT_REGISTER) ? "VF" : "VI")
		  << std::setw(2) << std::setfill('0') << arg.regNumber();
		key = s.str();
		return true;
	}

	void collectRegisterReads( const Token& token, std::list<std::string>& reads )
	{
		const std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			if( (*i).flags() & Token::Argument::WRITE )
				continue;
			std::string key;
			if( registerKey(*i, key) )
				reads.push_back(key);
		}
	}

	void collectRegisterWrites( const Token& token, std::list<std::string>& writes )
	{
		const std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			if( !((*i).flags() & Token::Argument::WRITE) )
				continue;
			std::string key;
			if( registerKey(*i, key) )
				writes.push_back(key);
		}
	}

	bool tokenTouchesImplicitResource( const Token& token, Token::Argument::Type type, bool write )
	{
		const std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			if( (*i).type() != type )
				continue;
			const bool isWrite = ((*i).flags() & Token::Argument::WRITE) != 0;
			if( isWrite == write )
				return true;
		}
		return false;
	}

	bool tokenReadsQ( const Token& token )
	{
		return tokenTouchesImplicitResource(token, Token::Argument::Q, false);
	}

	bool tokenWritesQ( const Token& token )
	{
		return tokenTouchesImplicitResource(token, Token::Argument::Q, true);
	}

	bool tokenReadsP( const Token& token )
	{
		return tokenTouchesImplicitResource(token, Token::Argument::P, false);
	}

	bool tokenWritesP( const Token& token )
	{
		return tokenTouchesImplicitResource(token, Token::Argument::P, true);
	}

}

int CodeGenerator::readHazardDelay( const Token& token, const Token* partner ) const
{
	std::list<std::string> reads;
	collectRegisterReads(token, reads);
	if( partner )
		collectRegisterReads(*partner, reads);

	bool readsQ = tokenReadsQ(token);
	bool readsP = tokenReadsP(token);
	if( partner )
	{
		readsQ = readsQ || tokenReadsQ(*partner);
		readsP = readsP || tokenReadsP(*partner);
	}

	int needed = 0;
	for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
	{
		std::map<std::string, int>::const_iterator ready = m_registerReadyCycle.find(*i);
		if( ready == m_registerReadyCycle.end() )
			continue;
		const int gap = ready->second - m_currentCycle;
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
	bool readsMac = isMacReader(token.operand()->name());
	bool readsClip = isClipReader(token.operand()->name());
	if( partner && partner->operand() )
	{
		const std::string& name = partner->operand()->name();
		readsMac = readsMac || isMacReader(name);
		readsClip = readsClip || isClipReader(name);
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
	bool readsQ = tokenReadsQ(token);
	bool readsP = tokenReadsP(token);
	if( partner )
	{
		readsQ = readsQ || tokenReadsQ(*partner);
		readsP = readsP || tokenReadsP(*partner);
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
	collectRegisterWrites(token, writes);
	for( std::list<std::string>::const_iterator i = writes.begin(); i != writes.end(); ++i )
		m_registerReadyCycle[*i] = issueCycle + latency + 1;
	if( tokenWritesQ(token) )
		m_qReadyCycle = issueCycle + latency + 1;
	if( tokenWritesP(token) )
		m_pReadyCycle = issueCycle + latency + 1;
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

bool CodeGenerator::isEmittableInstruction( const Token& t )
{
	// Mirrors the gating conditions in beginProcess() that decide whether
	// a token reaches the instruction-emission code path.
	if( !t.operand() )
		return false;
	if( t.flags() & Token::IGNORED )
		return false;
	if( !(t.flags() & Token::PROCESSED) && !(t.operand()->flags() & Operand::PREPROCESSOR) )
		return false;
	if( t.operand()->flags() & Operand::PREPROCESSOR )
		return false;
	if( t.operand()->flags() & Operand::FILTERED )
		return false;
	if( t.operand()->unit() == Operand::ENTER )
		return false;
	if( t.operand()->unit() == Operand::EXIT )
		return false;
	return true;
}

// Helper: identify the set of single-instance "implicit" resources
// (ACC / Q / P / I / R / MAC flags / CLIP flags) touched by a token,
// distinguishing reads and writes.  These resources have NO Alias and aren't
// routed through the register allocator, so the only way to spot conflicts is
// by inspecting the operand template, flags, and mnemonic.
//
// Returns two bitmasks, indexed by the bits below.
namespace {
	enum ResourceBit {
		RES_ACC = 1 << 0,
		RES_Q   = 1 << 1,
		RES_P   = 1 << 2,
		RES_I   = 1 << 3,
		RES_R   = 1 << 4,
		RES_MAC = 1 << 5,
		RES_CLIP = 1 << 6
	};

	void implicitResources( const Token& t, unsigned int& reads, unsigned int& writes )
	{
		reads = 0;
		writes = 0;
		if( !t.operand() )
			return;

		// Per-Argument single-instance resource touches.
		const std::list<Token::Argument>& args = t.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			unsigned int bit = 0;
			switch( (*i).type() )
			{
				case Token::Argument::ACCUMULATOR: bit = RES_ACC; break;
				case Token::Argument::Q:           bit = RES_Q;   break;
				case Token::Argument::P:           bit = RES_P;   break;
				case Token::Argument::I:           bit = RES_I;   break;
				case Token::Argument::R:           bit = RES_R;   break;
				default: continue;
			}
			if( (*i).flags() & Token::Argument::WRITE )
				writes |= bit;
			else
				reads |= bit;
		}

		// Operand-level implicit writes that aren't reflected by any
		// Argument flag.  LOI's destination is the I register, signalled
		// only by Operand::IWRITE.
		if( t.operand()->flags() & Operand::IWRITE )
			writes |= RES_I;

		const std::string& name = t.operand()->name();
		if( t.operand()->unit() == Operand::FMAC )
			writes |= RES_MAC;
		if( isClipw(name) )
			writes |= RES_CLIP;
		if( isMacReader(name) )
			reads |= RES_MAC;
		if( isClipReader(name) )
			reads |= RES_CLIP;
	}

	bool containsKey( const std::list<std::string>& keys, const std::string& key )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			if( *i == key )
				return true;
		}
		return false;
	}

	bool writesTouchReadsOrWrites( const std::list<std::string>& writes,
	                               const std::list<std::string>& reads,
	                               const std::list<std::string>& otherWrites )
	{
		for( std::list<std::string>::const_iterator i = writes.begin(); i != writes.end(); ++i )
		{
			if( containsKey(reads, *i) || containsKey(otherWrites, *i) )
				return true;
		}
		return false;
	}

	std::string lowerName( const Token& token )
	{
		std::string name;
		if( token.operand() )
			name = token.operand()->name();
		for( std::string::iterator i = name.begin(); i != name.end(); ++i )
		{
			if( *i >= 'A' && *i <= 'Z' )
				*i = char(*i - 'A' + 'a');
		}
		return name;
	}

	bool hasPreDecOrPostIncArgument( const Token& token )
	{
		const std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			if( (*i).flags() & (Token::Argument::PREDEC | Token::Argument::POSTINC) )
				return true;
		}
		return false;
	}

	bool isPlainMemoryStore( const Token& token )
	{
		if( !token.operand() || hasPreDecOrPostIncArgument(token) )
			return false;
		const std::string name = lowerName(token);
		return name == "sq" || name == "sqd" || name == "sqi"
		    || name == "isw" || name == "iswr";
	}

	bool isPlainMemoryLoad( const Token& token )
	{
		if( !token.operand() || hasPreDecOrPostIncArgument(token) )
			return false;
		const std::string name = lowerName(token);
		return name == "lq" || name == "ilw" || name == "ilwr";
	}

	bool memoryBaseRegisterKey( const Token& token, std::string& key )
	{
		const std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			if( !((*i).flags() & Token::Argument::INDIRECT) )
				continue;
			if( (*i).type() != Token::Argument::INTEGER_REGISTER )
				continue;
			return registerKey(*i, key);
		}
		return false;
	}

	bool memoryOffset( const Token& token, long& offset )
	{
		const std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
		{
			if( !((*i).flags() & Token::Argument::INDIRECT) )
				continue;
			if( (*i).type() != Token::Argument::INTEGER_REGISTER )
				continue;

			Expression e;
			e.setCustomOperators( Math::mathOperators() );
			if( !e.process( (*i).immediate() ) || !e.solve() )
				return false;
			offset = static_cast<long>(e.result());
			return true;
		}
		return false;
	}

	bool isIntegerImmediateAdd( const Token& token, std::string& dstReg, std::string& srcReg, long& immediate )
	{
		if( !token.operand() )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
			return false;

		const std::string name = lowerName(token);
		if( name != "iaddiu" )
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

		if( !registerKey(*dst, dstReg) || !registerKey(*src, srcReg) )
			return false;

		Expression e;
		e.setCustomOperators( Math::mathOperators() );
		if( !e.process( (*imm).immediate() ) || !e.solve() )
			return false;

		immediate = static_cast<long>(e.result());
		return true;
	}

	bool isSelfIntegerImmediateAdd( const Token& token, std::string& reg, long& immediate )
	{
		if( token.label().length() != 0 )
			return false;

		std::string srcReg;
		if( !isIntegerImmediateAdd(token, reg, srcReg, immediate) )
			return false;
		return reg == srcReg;
	}

	bool setIntegerImmediate( Token& token, long immediate )
	{
		std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 3 )
			return false;
		std::list<Token::Argument>::iterator i = args.begin();
		++i;
		++i;
		std::stringstream s;
		s << immediate;
		(*i).setImmediate(s.str());
		return true;
	}

	void coalesceAdjacentIntegerAdds( std::list<Token>& tokens )
	{
		for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); )
		{
			std::list<Token>::iterator next = i;
			++next;
			if( next == tokens.end() )
				break;

			std::string dstReg;
			std::string srcReg;
			std::string nextReg;
			long immediate = 0;
			long nextImmediate = 0;
			if( !isIntegerImmediateAdd(*i, dstReg, srcReg, immediate)
			    || !isSelfIntegerImmediateAdd(*next, nextReg, nextImmediate)
			    || dstReg != nextReg )
			{
				++i;
				continue;
			}

			const long combined = immediate + nextImmediate;
			if( combined < -32768 || combined > 32767 || combined == 0 )
			{
				++i;
				continue;
			}

			if( setIntegerImmediate(*i, combined) )
				tokens.erase(next);
			else
				++i;
		}
	}

	bool plainMemoryAccessesAreDistinct( const Token& a, const Token& b )
	{
		std::string movedBase;
		std::string crossedBase;
		if( !memoryBaseRegisterKey(a, movedBase) || !memoryBaseRegisterKey(b, crossedBase) )
			return false;

		if( movedBase != crossedBase )
			return true;

		long movedOffset = 0;
		long crossedOffset = 0;
		if( !memoryOffset(a, movedOffset) || !memoryOffset(b, crossedOffset) )
			return false;

		return movedOffset != crossedOffset;
	}

	bool plainLoadCanMoveBeforePlainStore( const Token& moved, const Token& crossed )
	{
		if( !isPlainMemoryLoad(moved) || !isPlainMemoryStore(crossed) )
			return false;

		return plainMemoryAccessesAreDistinct(moved, crossed);
	}

	bool plainStoreCanMoveBeforePlainMemory( const Token& moved, const Token& crossed )
	{
		if( !isPlainMemoryStore(moved) )
			return false;
		if( !isPlainMemoryStore(crossed) && !isPlainMemoryLoad(crossed) )
			return false;

		return plainMemoryAccessesAreDistinct(moved, crossed);
	}

	bool hasMemoryOrControlSideEffect( const Token& token );

	bool plainStoreCanMoveBeforeComputation( const Token& moved, const Token& crossed )
	{
		if( !isPlainMemoryStore(moved) )
			return false;
		return !hasMemoryOrControlSideEffect(crossed);
	}

	bool isXgkick( const Token& token )
	{
		if( !token.operand() )
			return false;
		return lowerName(token) == "xgkick";
	}

	bool hasMemoryOrControlSideEffect( const Token& token )
	{
		if( !token.operand() )
			return true;
		if( token.flags() & Token::PREORDERED )
			return true;

		Operand::Unit unit = token.operand()->unit();
		if( unit == Operand::BRU )
			return true;

		if( hasPreDecOrPostIncArgument(token) )
			return true;

		std::string name = lowerName(token);
		return name == "sq" || name == "sqd" || name == "sqi"
		    || name == "isw" || name == "iswr"
		    || name == "xgkick";
	}

	bool computationCanMoveBeforePlainStore( const Token& moved, const Token& crossed )
	{
		if( !isPlainMemoryStore(crossed) )
			return false;
		if( isPlainMemoryLoad(moved) )
			return false;
		return !hasMemoryOrControlSideEffect(moved);
	}
	}

bool CodeGenerator::hasDataDependency( const Token& a, const Token& b )
{
	// Conservative pair-blocking dependency check.
	//
	// Two layers:
	//
	//   (a) Register-class args (FLOAT_REGISTER, INTEGER_REGISTER).
	//       Conflict on PHYSICAL register (allocatedRegister()) — two
	//       distinct Aliases can land on the same VF/VI when their
	//       lifetimes don't overlap in the data-flow view, but pairing
	//       them in one cycle WOULD overlap their writes in hardware.
	//       Block RAW + WAW.  WAR is allowed: the allocator's
	//       live-range analysis already guarantees the prior consumer
	//       is done before the later producer fires, so reads-first /
	//       writes-last within a cycle is safe for register-class args.
	//
	//   (b) Single-instance resources (ACC, Q, P, I, R).  These have
	//       NO Alias and the allocator does not route them.  Track them
	//       via implicitResources() which inspects Argument types and
	//       Operand-level flags like IWRITE (the LOI destination).
	//       Block ALL conflicts here — RAW, WAW, AND WAR — because
	//       VU1 has no intra-cycle ordering guarantee for these single
	//       hardware registers (a paired LOI's I-write happens during
	//       the same cycle as the FMAC's I-read; we can't assume one
	//       lands before the other).

	// (b) Single-instance resources — cheaper and catches LOI/ACC/Q/P.
	unsigned int aReads = 0, aWrites = 0, bReads = 0, bWrites = 0;
	implicitResources( a, aReads, aWrites );
	implicitResources( b, bReads, bWrites );
	// Either side writing a resource the other side touches is a hazard.
	if( aWrites & (bReads | bWrites) )
		return true;
	if( bWrites & (aReads | aWrites) )
		return true;

	// (a) Register-class args — physical-register comparison.
	const std::list<Token::Argument>& aArgs = a.arguments();
	const std::list<Token::Argument>& bArgs = b.arguments();

	for( std::list<Token::Argument>::const_iterator ai = aArgs.begin(); ai != aArgs.end(); ++ai )
	{
		const bool aWrite = ((*ai).flags() & Token::Argument::WRITE) != 0;
		if( !aWrite )
			continue;

		Token::Argument::Type aType = (*ai).type();
		if( aType != Token::Argument::FLOAT_REGISTER
		    && aType != Token::Argument::INTEGER_REGISTER )
			continue;

		std::string aKey;
		if( !registerKey(*ai, aKey) )
			continue;

		for( std::list<Token::Argument>::const_iterator bi = bArgs.begin(); bi != bArgs.end(); ++bi )
		{
			Token::Argument::Type bType = (*bi).type();
			if( bType != aType )
				continue;
			std::string bKey;
			if( !registerKey(*bi, bKey) )
				continue;

			if( aKey != bKey )
				continue;

			// RAW (b reads what a writes) or WAW (b also writes).
			return true;
		}
	}

	return false;
}

bool CodeGenerator::tokenCanMoveBefore( const Token& moved, const Token& crossed )
{
	if( hasMemoryOrControlSideEffect(moved) || hasMemoryOrControlSideEffect(crossed) )
	{
		if( !plainLoadCanMoveBeforePlainStore(moved, crossed)
		    && !plainStoreCanMoveBeforePlainMemory(moved, crossed)
		    && !plainStoreCanMoveBeforeComputation(moved, crossed)
		    && !computationCanMoveBeforePlainStore(moved, crossed) )
			return false;
	}

	unsigned int movedReadsImplicit = 0, movedWritesImplicit = 0;
	unsigned int crossedReadsImplicit = 0, crossedWritesImplicit = 0;
	implicitResources(moved, movedReadsImplicit, movedWritesImplicit);
	implicitResources(crossed, crossedReadsImplicit, crossedWritesImplicit);
	if( movedWritesImplicit & (crossedReadsImplicit | crossedWritesImplicit) )
		return false;
	if( crossedWritesImplicit & (movedReadsImplicit | movedWritesImplicit) )
		return false;

	std::list<std::string> movedReads;
	std::list<std::string> movedWrites;
	std::list<std::string> crossedReads;
	std::list<std::string> crossedWrites;
	collectRegisterReads(moved, movedReads);
	collectRegisterWrites(moved, movedWrites);
	collectRegisterReads(crossed, crossedReads);
	collectRegisterWrites(crossed, crossedWrites);

	if( writesTouchReadsOrWrites(movedWrites, crossedReads, crossedWrites) )
		return false;
	if( writesTouchReadsOrWrites(crossedWrites, movedReads, movedWrites) )
		return false;

	return true;
}

bool CodeGenerator::tokenRangeCanBeCrossed( const Token& first, const Token& last )
{
	return !hasMemoryOrControlSideEffect(first) && !hasMemoryOrControlSideEffect(last);
}

// Reads the MAC flag register, updated by every FMAC with 4-cycle latency.
static bool isMacReader( const std::string& name )
{
	return name == "fmand" || name == "fmeq" || name == "fmor"
	    || name == "fsand" || name == "fseq" || name == "fsor"
	    || name == "FMAND" || name == "FMEQ" || name == "FMOR"
	    || name == "FSAND" || name == "FSEQ" || name == "FSOR";
}

// Reads the CLIP flag register, updated by clipw only.
static bool isClipReader( const std::string& name )
{
	return name == "fcand" || name == "fceq" || name == "fcor" || name == "fcget"
	    || name == "FCAND" || name == "FCEQ" || name == "FCOR" || name == "FCGET";
}

// `clipw` (with any field suffix) is the only FMAC that writes the CLIP
// register.  Match the family by prefix so clipw.xyz / CLIPw / etc. all hit.
static bool isClipw( const std::string& name )
{
	if( name.size() < 5 )
		return false;
	return ( name.compare(0, 5, "clipw") == 0 )
	    || ( name.compare(0, 5, "CLIPw") == 0 )
	    || ( name.compare(0, 5, "CLIP" ) == 0 && (name[4] == 'w' || name[4] == 'W') );
}

bool CodeGenerator::tokensCanPair( const Token& a, const Token& b )
{
	if( !isEmittableInstruction(a) || !isEmittableInstruction(b) )
		return false;

	// PREORDERED tokens (raw .vsm passthrough) must keep their
	// emission position exactly as written.
	if( (a.flags() & Token::PREORDERED) || (b.flags() & Token::PREORDERED) )
		return false;

	// Must straddle the upper/lower pipe split.
	if( a.operand()->isLowerExecutionPath() == b.operand()->isLowerExecutionPath() )
		return false;

	// Keep dynamic control flow and explicit Q/P wait barriers out of the
	// simple pairing pass.  FDIV/EFU producers can pair with independent
	// upper-pipe work because their Q/P latency is tracked separately.
	Operand::Unit ua = a.operand()->unit();
	Operand::Unit ub = b.operand()->unit();
	if( ua == Operand::BRU || ub == Operand::BRU )
		return false;
	const std::string aName = lowerName(a);
	const std::string bName = lowerName(b);
	if( aName == "waitq" || aName == "waitp" || bName == "waitq" || bName == "waitp" )
		return false;

	// FMAC writes MAC with 4-cycle latency; clipw additionally writes
	// CLIP.  Keep same-flag readers out of the pair.  A non-clip FMAC
	// can still pair with a CLIP reader such as fcand once the previous
	// clipw result is latency-ready.
	bool aFMAC = (ua == Operand::FMAC);
	bool bFMAC = (ub == Operand::FMAC);
	if( aFMAC && isMacReader(b.operand()->name()) )
		return false;
	if( bFMAC && isMacReader(a.operand()->name()) )
		return false;
	if( isClipw(a.operand()->name()) && isClipReader(b.operand()->name()) )
		return false;
	if( isClipw(b.operand()->name()) && isClipReader(a.operand()->name()) )
		return false;

	// Data-flow conflict between the two.
	if( hasDataDependency(a, b) )
		return false;
	if( hasDataDependency(b, a) )
		return false;

	return true;
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
