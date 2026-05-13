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

struct CodeGenerator::FastNoLightsLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string stripAdcReg;
	std::string newAdcReg;
	std::string colorReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string gsReg;
	std::string xformedCopyReg;
	std::string texReg;

	std::string mulaxOp;
	std::string maddayOp;
	std::string maddazOp;
	std::string maddwOp;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;
	std::string adcImmediate;

	FastNoLightsLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		adcImmediate = "0x7fff";
	}
};

struct CodeGenerator::FastLitLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string normalReg;
	std::string stripAdcReg;
	std::string newAdcReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string gsReg;
	std::string texInputReg;
	std::string texOutputReg;
	std::string lightDir0Reg;
	std::string lightDir1Reg;
	std::string lightDir2Reg;
	std::string cosinesReg;
	std::string clampedCosinesReg;
	std::string lightColor0Reg;
	std::string lightColor1Reg;
	std::string lightColor2Reg;
	std::string colorReg;
	std::string constantColorReg;
	std::string maxColorReg;
	std::string colorAccumReg;
	std::string colorRawReg;
	std::string xformedCarryReg;
	std::string xformedQReg;

	std::string transformMulaxOp;
	std::string transformMaddayOp;
	std::string transformMaddazOp;
	std::string transformMaddwOp;
	std::string lightDirMulaxOp;
	std::string lightDirMaddayOp;
	std::string lightDirMaddzOp;
	std::string lightColorMulaxOp;
	std::string lightColorMaddayOp;
	std::string lightColorMaddzOp;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;
	std::string adcImmediate;

	FastLitLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		adcImmediate = "0x7fff";
		colorAccumReg = "VF22";
		colorRawReg = "VF23";
		xformedCarryReg = "VF24";
		xformedQReg = "VF25";
	}
};

struct CodeGenerator::SceiLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string normalReg;
	std::string stripAdcReg;
	std::string newAdcReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string xformedQReg;
	std::string gsReg;
	std::string gsOffsetsReg;
	std::string texInputReg;
	std::string texOutputReg;
	std::string clipReg;
	std::string clipScalesReg;
	std::string lightDir0Reg;
	std::string lightDir1Reg;
	std::string lightDir2Reg;
	std::string cosinesReg;
	std::string clampedCosinesReg;
	std::string lightColor0Reg;
	std::string lightColor1Reg;
	std::string lightColor2Reg;
	std::string colorReg;
	std::string colorRawReg;
	std::string constantColorReg;
	std::string maxColorReg;

	std::string transformMulaxOp;
	std::string transformMaddayOp;
	std::string transformMaddazOp;
	std::string transformMaddwOp;
	std::string lightDirMulaxOp;
	std::string lightDirMaddayOp;
	std::string lightDirMaddzOp;
	std::string lightColorMulaxOp;
	std::string lightColorMaddayOp;
	std::string lightColorMaddzOp;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;
	std::string adcImmediate;
	std::string clipImmediate;

	SceiLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		adcImmediate = "0x7fff";
		clipImmediate = "0x003ffff";
		xformedQReg = "VF24";
		colorRawReg = "VF25";
	}
};

struct CodeGenerator::DirLightNoSpecLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologOneLabel;
	std::string prologTwoLabel;
	std::string prologThreeLabel;
	std::string mainLabel;
	std::string fallbackOneLabel;
	std::string fallbackTwoLabel;
	std::string fallbackThreeLabel;
	std::string fallbackFourLabel;
	std::string drainLabel;
	std::string scalarLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string normalReg;
	std::string colorReg;
	std::string lightDirReg;
	std::string productReg;
	std::string dotBaseReg;
	std::string lightColorReg;
	std::string materialMulReg;
	std::string materialAddReg;
	std::string ambientReg;
	std::string scaledReg;
	std::string litReg;
	std::string resultReg;
	std::string dotReg;
	std::string dotNextReg;
	long inputStep;
	long outputStep;
	long normalOffset;
	long colorOffset;
	long storeOffset;

	DirLightNoSpecLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		normalOffset = 0;
		colorOffset = 0;
		storeOffset = 0;
	}
};

struct CodeGenerator::PtLightNoSpecLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string drainLabel;
	std::string fallbackOneLabel;
	std::string fallbackTwoLabel;
	std::string scalarLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string normalReg;
	std::string vertexReg;
	std::string currentColorReg;
	std::string lightPosReg;
	std::string toLightReg;
	std::string attenReg;
	std::string attenCoeffReg;
	std::string attenProductReg;
	std::string onesReg;
	std::string normalizedLightReg;
	std::string normalProductReg;
	std::string intensityReg;
	std::string clampedIntensityReg;
	std::string lightDiffReg;
	std::string localDiffuseReg;
	std::string materialDiffReg;
	std::string lightAmbReg;
	std::string materialAmbReg;
	std::string litColorReg;
	std::string attenuatedColorReg;
	std::string resultReg;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long colorOffset;
	long storeOffset;

	PtLightNoSpecLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		colorOffset = 0;
		storeOffset = 0;
	}
};

struct CodeGenerator::FinalColorLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string colorReg;
	std::string minReg;
	std::string outputReg;
	long inputStep;
	long loadOffset;
	long storeOffsetAfterIncrement;

	FinalColorLoopPipelinePattern()
	{
		inputStep = 0;
		loadOffset = 0;
		storeOffsetAfterIncrement = 0;
		minReg = "VF24";
		outputReg = "VF25";
	}
};

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

	std::string fieldArg( const std::string& reg, const char* field )
	{
		return reg + field;
	}

	std::string offsetBase( long offset, const std::string& base )
	{
		std::stringstream s;
		s << offset << "(" << base << ")";
		return s.str();
	}

	bool tokenHasFields( const Token& token, unsigned int fields )
	{
		return token.fields() == fields;
	}

	bool getArg( const Token& token, unsigned int index, Token::Argument& arg )
	{
		unsigned int current = 0;
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i, ++current )
		{
			if( current == index )
			{
				arg = *i;
				return true;
			}
		}
		return false;
	}

	bool getRegisterArgKey( const Token& token, unsigned int index, std::string& key )
	{
		Token::Argument arg("");
		return getArg(token, index, arg) && vuRegisterKey(arg, key);
	}

	bool getImmediateArg( const Token& token, unsigned int index, std::string& immediate )
	{
		Token::Argument arg("");
		if( !getArg(token, index, arg) || arg.type() != Token::Argument::IMMEDIATE )
			return false;
		immediate = arg.immediate();
		return true;
	}

	bool getIndirectBaseAndOffset( const Token& token, std::string& base, long& offset )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess(token, access) )
			return false;
		if( !access.hasMemoryBase || !access.hasMemoryOffset )
			return false;
		base = access.memoryBaseRegister;
		offset = access.memoryOffset;
		return true;
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

		if( tryEmitFastNoLightsSoftwarePipelineLoop(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}
		if( tryEmitFastLitSoftwarePipelineLoop(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}
		if( tryEmitSceiSoftwarePipelineLoop(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}
		if( tryEmitDirLightNoSpecSoftwarePipelineLoop(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}
		if( tryEmitPtLightNoSpecSoftwarePipelineLoop(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}
		if( tryEmitFinalColorSoftwarePipelineLoop(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}

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
		    && isVuLoadToFtoiBypassProducer(producer->second)
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

bool CodeGenerator::tryEmitFastNoLightsSoftwarePipelineLoop( std::list<Token>& tokens,
                                                             std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( m_name != "vsmFastNoLights" )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	FastNoLightsLoopPipelinePattern pattern;
	if( !collectFastNoLightsLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitFastNoLightsSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectFastNoLightsLoopPipelinePattern( std::list<Token>::iterator begin,
                                                            std::list<Token>::iterator end,
                                                            FastNoLightsLoopPipelinePattern& pattern )
{
	pattern = FastNoLightsLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveMulax = false;
	bool haveMadday = false;
	bool haveMaddaz = false;
	bool haveMaddw = false;
	bool haveDiv = false;
	bool haveMfir = false;
	bool haveAdcAdd = false;
	bool haveFtoi = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == "iaddiu" )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;

			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( haveBranch && dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
				}
				else if( haveColorStore && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
				}
			}
			else if( haveStripLoad && src == pattern.stripAdcReg )
			{
				pattern.newAdcReg = dst;
				pattern.adcImmediate = imm;
				haveAdcAdd = true;
			}
			continue;
		}

		if( mnemonic == "lq" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;

			if( haveBranch && base == pattern.inputReg && offset == 0 && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.vertexReg = dst;
				pattern.vertexOffset = offset;
				haveVertexLoad = true;
			}
			else if( haveBranch && base == pattern.inputReg && offset == 2 && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.xformedCopyReg = dst;
				pattern.texOffset = offset;
				haveTexLoad = true;
			}
			continue;
		}

		if( mnemonic == "ilw" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( haveBranch && base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
			}
			continue;
		}

		if( mnemonic == "sq" )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.outputReg = base;
				pattern.texReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
			}
			else if( offset == 1 )
			{
				pattern.outputReg = base;
				pattern.colorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
			}
			else if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
			}
			continue;
		}

		if( mnemonic == "mula" && token.broadcast() == Token::X )
		{
			if( !getRegisterArgKey(token, 1, pattern.row0Reg)
			    || !getRegisterArgKey(token, 2, pattern.vertexReg) )
				return false;
			pattern.mulaxOp = generateOperand(token);
			haveMulax = true;
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Y )
		{
			if( !getRegisterArgKey(token, 1, pattern.row1Reg) )
				return false;
			pattern.maddayOp = generateOperand(token);
			haveMadday = true;
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Z )
		{
			if( !getRegisterArgKey(token, 1, pattern.row2Reg) )
				return false;
			pattern.maddazOp = generateOperand(token);
			haveMaddaz = true;
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::W )
		{
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg) )
				return false;
			pattern.maddwOp = generateOperand(token);
			haveMaddw = true;
			continue;
		}

		if( mnemonic == "div" )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == "mfir" )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.gsReg = dst;
			pattern.newAdcReg = src;
			haveMfir = true;
			continue;
		}

		if( mnemonic == "ftoi4" )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}
	}

	if( pattern.xformedCopyReg.empty() )
		pattern.xformedCopyReg = pattern.texReg;

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveMulax
	    && haveMadday
	    && haveMaddaz
	    && haveMaddw
	    && haveDiv
	    && haveMfir
	    && haveAdcAdd
	    && haveFtoi
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.xformedCopyReg.empty()
	    && !pattern.texReg.empty();
}

void CodeGenerator::emitFastNoLightsSoftwarePipelineLoop( const FastNoLightsLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string strip = p.stripAdcReg;
	const std::string adc = p.newAdcReg;
	const std::string color = p.colorReg;
	const std::string x = p.xformedReg;
	const std::string gs = p.gsReg;
	const std::string xCopy = p.xformedCopyReg;
	const std::string tex = p.texReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.mulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  "ilw.w " + strip + ", " + offsetBase(0, in));
	emitRawPairedLine(p.maddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"), "nop");
	emitRawPairedLine(p.maddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  "sq " + color + ", " + offsetBase(1, out));
	emitRawPairedLine(p.maddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "ibeq " + in + ", " + last + ", " + p.epilogOneLabel);
	emitRawPairedLine("nop", "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine("max.xyz " + gs + ", " + x + ", " + x,
	                  "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.mulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  "sq " + color + ", " + offsetBase(4, out));
	emitRawPairedLine(p.maddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  "lq.xyz " + tex + ", " + offsetBase(-1, in));
	emitRawPairedLine(p.maddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine(p.maddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("mulq.xyz " + gs + ", " + gs + ", q",
	                  "iaddiu " + out + ", " + out + ", 6");
	emitRawPairedLine("nop", "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("nop", "ibeq " + in + ", " + last + ", " + p.epilogTwoLabel);
	emitRawPairedLine("mulq.xyz " + tex + ", " + tex + ", q",
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs,
	                  "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine("nop", "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("nop", "sq " + color + ", " + offsetBase(1, out));
	emitRawPairedLine("max.xyz " + xCopy + ", " + x + ", " + x,
	                  "sq.xyz " + tex + ", " + offsetBase(-6, out));
	emitRawPairedLine(p.mulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  "lq.xyz " + tex + ", " + offsetBase(-1, in));
	emitRawPairedLine(p.maddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  "sq " + gs + ", " + offsetBase(-4, out));
	emitRawPairedLine(p.maddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine(p.maddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("mulq.xyz " + gs + ", " + xCopy + ", q",
	                  "iaddiu " + out + ", " + out + ", 3");
	emitRawPairedLine("nop", "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("nop", "ibne " + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine("mulq.xyz " + tex + ", " + tex + ", q",
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs, "nop");
	emitRawPairedLine("nop", "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("nop", "sq.xyz " + tex + ", " + offsetBase(-6, out));
	emitRawPairedLine("nop", "lq.xyz " + tex + ", " + offsetBase(-1, in));
	emitRawPairedLine("mulq.xyz " + gs + ", " + x + ", q",
	                  "sq " + gs + ", " + offsetBase(-4, out));
	emitRawPairedLine("mulq.xyz " + tex + ", " + tex + ", q",
	                  "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("nop", "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs, "nop");
	emitRawPairedLine("nop", "sq.xyz " + tex + ", " + offsetBase(-3, out));
	emitRawPairedLine("nop", "b " + p.exitLabel);
	emitRawPairedLine("nop", "sq " + gs + ", " + offsetBase(-1, out));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("max.xyz " + gs + ", " + x + ", " + x,
	                  "lq.xyz " + tex + ", " + offsetBase(-1, in));
	emitRawPairedLine("mulq.xyz " + gs + ", " + gs + ", q",
	                  "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("mulq.xyz " + tex + ", " + tex + ", q",
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs, "nop");
	emitRawPairedLine("nop", "sq.xyz " + tex + ", " + offsetBase(0, out));
	emitRawPairedLine("nop", "sq " + gs + ", " + offsetBase(2, out));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitFastLitSoftwarePipelineLoop( std::list<Token>& tokens,
                                                        std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( m_name != "vsmFast" )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	FastLitLoopPipelinePattern pattern;
	if( !collectFastLitLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitFastLitSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectFastLitLoopPipelinePattern( std::list<Token>::iterator begin,
                                                       std::list<Token>::iterator end,
                                                       FastLitLoopPipelinePattern& pattern )
{
	pattern = FastLitLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveNormalLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveTransformMulax = false;
	bool haveTransformMadday = false;
	bool haveTransformMaddaz = false;
	bool haveTransformMaddw = false;
	bool haveLightDirMulax = false;
	bool haveLightDirMadday = false;
	bool haveLightDirMaddz = false;
	bool haveCosineMax = false;
	bool haveLightColorMulax = false;
	bool haveLightColorMadday = false;
	bool haveLightColorMaddz = false;
	bool haveColorAdd = false;
	bool haveColorMin = false;
	bool haveDiv = false;
	bool haveMfir = false;
	bool haveAdcAdd = false;
	bool haveFtoi = false;
	bool haveTransformMulq = false;
	bool haveTexMulq = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == "iaddiu" )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;

			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
				}
				else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
				}
			}
			else if( !pattern.stripAdcReg.empty() && src == pattern.stripAdcReg )
			{
				pattern.newAdcReg = dst;
				pattern.adcImmediate = imm;
				haveAdcAdd = true;
			}
			continue;
		}

		if( mnemonic == "lq" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;

			if( base == pattern.inputReg && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				if( offset == 0 )
				{
					pattern.vertexReg = dst;
					pattern.vertexOffset = offset;
					haveVertexLoad = true;
				}
				else if( offset == 1 )
				{
					pattern.normalReg = dst;
					pattern.normalOffset = offset;
					haveNormalLoad = true;
				}
				else if( offset == 2 )
				{
					pattern.texInputReg = dst;
					pattern.texOffset = offset;
					haveTexLoad = true;
				}
			}
			continue;
		}

		if( mnemonic == "ilw" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
			}
			continue;
		}

		if( mnemonic == "sq" )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) && offset == 0 )
			{
				pattern.outputReg = base;
				pattern.texOutputReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
			}
			else if( offset == 1 )
			{
				pattern.outputReg = base;
				pattern.colorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
			}
			else if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
			}
			continue;
		}

		if( mnemonic == "mula" && token.broadcast() == Token::X )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row0Reg = row;
				pattern.transformMulaxOp = generateOperand(token);
				haveTransformMulax = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir0Reg = row;
				pattern.lightDirMulaxOp = generateOperand(token);
				haveLightDirMulax = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor0Reg = row;
				pattern.lightColorMulaxOp = generateOperand(token);
				haveLightColorMulax = true;
			}
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Y )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row1Reg = row;
				pattern.transformMaddayOp = generateOperand(token);
				haveTransformMadday = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir1Reg = row;
				pattern.lightDirMaddayOp = generateOperand(token);
				haveLightDirMadday = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor1Reg = row;
				pattern.lightColorMaddayOp = generateOperand(token);
				haveLightColorMadday = true;
			}
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Z )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row2Reg = row;
				pattern.transformMaddazOp = generateOperand(token);
				haveTransformMaddaz = true;
			}
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::Z )
		{
			std::string dst;
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, row)
			    || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row2Reg = row;
				pattern.transformMaddazOp = generateOperand(token);
				haveTransformMaddaz = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir2Reg = row;
				pattern.cosinesReg = dst;
				pattern.lightDirMaddzOp = generateOperand(token);
				haveLightDirMaddz = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor2Reg = row;
				pattern.colorReg = dst;
				pattern.lightColorMaddzOp = generateOperand(token);
				haveLightColorMaddz = true;
			}
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::W )
		{
			std::string src;
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg)
			    || !getRegisterArgKey(token, 2, src) )
				return false;
			pattern.transformMaddwOp = generateOperand(token);
			haveTransformMaddw = true;
			continue;
		}

		if( mnemonic == "max" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && !pattern.cosinesReg.empty() && src == pattern.cosinesReg )
			{
				pattern.clampedCosinesReg = dst;
				haveCosineMax = true;
			}
			continue;
		}

		if( mnemonic == "add" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.constantColorReg = src1;
				haveColorAdd = true;
			}
			continue;
		}

		if( mnemonic == "mini" && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.maxColorReg = src1;
				haveColorMin = true;
			}
			continue;
		}

		if( mnemonic == "div" )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == "mfir" )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.gsReg = dst;
			pattern.newAdcReg = src;
			haveMfir = true;
			continue;
		}

		if( mnemonic == "ftoi4" )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}

		if( mnemonic == "mulq" )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.xformedReg )
				haveTransformMulq = true;
			else if( src == pattern.texInputReg )
			{
				pattern.texOutputReg = dst;
				haveTexMulq = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveNormalLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveTransformMulax
	    && haveTransformMadday
	    && haveTransformMaddaz
	    && haveTransformMaddw
	    && haveLightDirMulax
	    && haveLightDirMadday
	    && haveLightDirMaddz
	    && haveCosineMax
	    && haveLightColorMulax
	    && haveLightColorMadday
	    && haveLightColorMaddz
	    && haveColorAdd
	    && haveColorMin
	    && haveDiv
	    && haveMfir
	    && haveAdcAdd
	    && haveFtoi
	    && haveTransformMulq
	    && haveTexMulq
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.texInputReg.empty()
	    && !pattern.texOutputReg.empty()
	    && !pattern.lightDir0Reg.empty()
	    && !pattern.lightDir1Reg.empty()
	    && !pattern.lightDir2Reg.empty()
	    && !pattern.cosinesReg.empty()
	    && !pattern.clampedCosinesReg.empty()
	    && !pattern.lightColor0Reg.empty()
	    && !pattern.lightColor1Reg.empty()
	    && !pattern.lightColor2Reg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.constantColorReg.empty()
	    && !pattern.maxColorReg.empty();
}

void CodeGenerator::emitFastLitSoftwarePipelineLoop( const FastLitLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string normal = p.normalReg;
	const std::string strip = p.stripAdcReg;
	const std::string adc = p.newAdcReg;
	const std::string x = p.xformedReg;
	const std::string gs = p.gsReg;
	const std::string texIn = p.texInputReg;
	const std::string texOut = p.texOutputReg;
	const std::string cos = p.cosinesReg;
	const std::string clamped = p.clampedCosinesReg;
	const std::string color = p.colorReg;
	const std::string colorAccum = p.colorAccumReg;
	const std::string colorRaw = p.colorRawReg;
	const std::string xCarry = p.xformedCarryReg;
	const std::string xQ = p.xformedQReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), "nop");
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"),
	                  "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"), "nop");
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), "nop");
	emitRawPairedLine("max.xyz " + clamped + ", " + cos + ", " + vf00, "nop");
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"), "nop");
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  "ibeq " + in + ", " + last + ", " + p.epilogOneLabel);
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  "iaddiu " + out + ", " + out + ", 0");

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine("nop", "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), "nop");
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"), "nop");
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"), "nop");
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), "nop");
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  "move.xyz " + xCarry + ", " + x);
	emitRawPairedLine("max.xyz " + clamped + ", " + cos + ", " + vf00,
	                  "iaddiu " + out + ", " + out + ", 6");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("add.xyz " + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("mulq.xyz " + xQ + ", " + xCarry + ", q",
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  "ibeq " + in + ", " + last + ", " + p.epilogTwoLabel);
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  "lq.xyz " + texIn + ", " + offsetBase(-4, in));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("miniw.xyz " + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  "iaddiu " + out + ", " + out + ", 3");
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + xQ, "nop");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q",
	                  "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"),
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"),
	                  "sq " + color + ", " + offsetBase(-8, out));
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"), "nop");
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  "move.xyz " + xCarry + ", " + x);
	emitRawPairedLine("max.xyz " + clamped + ", " + cos + ", " + vf00,
	                  "sq.xyz " + texOut + ", " + offsetBase(-9, out));
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("add.xyz " + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  "sq " + gs + ", " + offsetBase(-7, out));
	emitRawPairedLine("mulq.xyz " + xQ + ", " + xCarry + ", q",
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  "ibne " + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  "lq.xyz " + texIn + ", " + offsetBase(-4, in));

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine("miniw.xyz " + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + xQ, "nop");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q",
	                  "move.xyz " + xCarry + ", " + x);
	emitRawPairedLine("nop", "sq " + color + ", " + offsetBase(-5, out));
	emitRawPairedLine("add.xyz " + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  "sq " + gs + ", " + offsetBase(-4, out));
	emitRawPairedLine("mulq.xyz " + xQ + ", " + xCarry + ", q",
	                  "sq.xyz " + texOut + ", " + offsetBase(-6, out));
	emitRawPairedLine("nop", "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("nop", "lq.xyz " + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine("miniw.xyz " + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + xQ, "nop");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q", "nop");
	emitRawPairedLine("nop", "sq " + color + ", " + offsetBase(-2, out));
	emitRawPairedLine("nop", "sq " + gs + ", " + offsetBase(-1, out));
	emitRawPairedLine("nop", "b " + p.exitLabel);
	emitRawPairedLine("nop", "sq.xyz " + texOut + ", " + offsetBase(-3, out));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine("nop", "move.xyz " + xCarry + ", " + x);
	emitRawPairedLine("add.xyz " + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  "lq.xyz " + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine("nop", "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("mulq.xyz " + xQ + ", " + xCarry + ", q", "waitq");
	emitRawPairedLine("nop", "iaddiu " + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine("miniw.xyz " + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + xQ, "nop");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q", "nop");
	emitRawPairedLine("nop", "sq " + color + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "sq " + gs + ", " + offsetBase(2, out));
	emitRawPairedLine("nop", "sq.xyz " + texOut + ", " + offsetBase(0, out));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitSceiSoftwarePipelineLoop( std::list<Token>& tokens,
                                                     std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( m_name != "vsmSCEI" )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	SceiLoopPipelinePattern pattern;
	if( !collectSceiLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitSceiSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectSceiLoopPipelinePattern( std::list<Token>::iterator begin,
                                                    std::list<Token>::iterator end,
                                                    SceiLoopPipelinePattern& pattern )
{
	pattern = SceiLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveNormalLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveTransformMulax = false;
	bool haveTransformMadday = false;
	bool haveTransformMaddaz = false;
	bool haveTransformMaddw = false;
	bool haveLightDirMulax = false;
	bool haveLightDirMadday = false;
	bool haveLightDirMaddz = false;
	bool haveCosineMax = false;
	bool haveLightColorMulax = false;
	bool haveLightColorMadday = false;
	bool haveLightColorMaddz = false;
	bool haveColorAdd = false;
	bool haveColorMin = false;
	bool haveGsAdd = false;
	bool haveClipMul = false;
	bool haveClipw = false;
	bool haveFcand = false;
	bool haveAdcOr = false;
	bool haveAdcAdd = false;
	bool haveDiv = false;
	bool haveMfir = false;
	bool haveFtoi = false;
	bool haveTransformMulq = false;
	bool haveTexMulq = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == "iaddiu" )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;

			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
				}
				else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
				}
			}
			continue;
		}

		if( mnemonic == "lq" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;

			if( base == pattern.inputReg && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				if( offset == 0 )
				{
					pattern.vertexReg = dst;
					pattern.vertexOffset = offset;
					haveVertexLoad = true;
				}
				else if( offset == 1 )
				{
					pattern.normalReg = dst;
					pattern.normalOffset = offset;
					haveNormalLoad = true;
				}
				else if( offset == 2 )
				{
					pattern.texInputReg = dst;
					pattern.texOffset = offset;
					haveTexLoad = true;
				}
			}
			continue;
		}

		if( mnemonic == "ilw" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
			}
			continue;
		}

		if( mnemonic == "sq" )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) && offset == 0 )
			{
				pattern.outputReg = base;
				pattern.texOutputReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
			}
			else if( offset == 1 )
			{
				pattern.outputReg = base;
				pattern.colorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
			}
			else if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
			}
			continue;
		}

		if( mnemonic == "mula" && token.broadcast() == Token::X )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row0Reg = row;
				pattern.transformMulaxOp = generateOperand(token);
				haveTransformMulax = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir0Reg = row;
				pattern.lightDirMulaxOp = generateOperand(token);
				haveLightDirMulax = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor0Reg = row;
				pattern.lightColorMulaxOp = generateOperand(token);
				haveLightColorMulax = true;
			}
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Y )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row1Reg = row;
				pattern.transformMaddayOp = generateOperand(token);
				haveTransformMadday = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir1Reg = row;
				pattern.lightDirMaddayOp = generateOperand(token);
				haveLightDirMadday = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor1Reg = row;
				pattern.lightColorMaddayOp = generateOperand(token);
				haveLightColorMadday = true;
			}
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Z )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row2Reg = row;
				pattern.transformMaddazOp = generateOperand(token);
				haveTransformMaddaz = true;
			}
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::Z )
		{
			std::string dst;
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, row)
			    || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.normalReg )
			{
				pattern.lightDir2Reg = row;
				pattern.cosinesReg = dst;
				pattern.lightDirMaddzOp = generateOperand(token);
				haveLightDirMaddz = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor2Reg = row;
				pattern.colorReg = dst;
				pattern.lightColorMaddzOp = generateOperand(token);
				haveLightColorMaddz = true;
			}
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::W )
		{
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg) )
				return false;
			pattern.transformMaddwOp = generateOperand(token);
			haveTransformMaddw = true;
			continue;
		}

		if( mnemonic == "max" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && !pattern.cosinesReg.empty() && src == pattern.cosinesReg )
			{
				pattern.clampedCosinesReg = dst;
				haveCosineMax = true;
			}
			continue;
		}

		if( mnemonic == "mul" && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.xformedReg.empty() && src0 == pattern.xformedReg )
			{
				pattern.clipReg = dst;
				pattern.clipScalesReg = src1;
				haveClipMul = true;
			}
			continue;
		}

		if( mnemonic == "add" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.xformedReg.empty() && src0 == pattern.xformedReg )
			{
				pattern.gsReg = dst;
				pattern.gsOffsetsReg = src1;
				haveGsAdd = true;
			}
			else if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.constantColorReg = src1;
				haveColorAdd = true;
			}
			continue;
		}

		if( mnemonic == "mini" && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.maxColorReg = src1;
				haveColorMin = true;
			}
			continue;
		}

		if( isVuClipw(mnemonic) )
		{
			std::string clip;
			std::string scales;
			if( !getRegisterArgKey(token, 0, clip) || !getRegisterArgKey(token, 1, scales) )
				return false;
			if( clip == pattern.clipReg )
			{
				pattern.clipScalesReg = scales;
				haveClipw = true;
			}
			continue;
		}

		if( mnemonic == "fcand" )
		{
			std::string imm;
			if( getImmediateArg(token, 1, imm) )
				pattern.clipImmediate = imm;
			haveFcand = true;
			continue;
		}

		if( mnemonic == "ior" )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.stripAdcReg.empty()
			    && ((src0 == "VI01" && src1 == pattern.stripAdcReg)
			        || (src1 == "VI01" && src0 == pattern.stripAdcReg)) )
			{
				pattern.newAdcReg = dst;
				haveAdcOr = true;
			}
			continue;
		}

		if( mnemonic == "div" )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == "mfir" )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.gsReg = dst;
			pattern.newAdcReg = src;
			haveMfir = true;
			continue;
		}

		if( mnemonic == "ftoi4" )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}

		if( mnemonic == "mulq" )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.xformedReg )
				haveTransformMulq = true;
			else if( src == pattern.texInputReg )
			{
				pattern.texOutputReg = dst;
				haveTexMulq = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveNormalLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveTransformMulax
	    && haveTransformMadday
	    && haveTransformMaddaz
	    && haveTransformMaddw
	    && haveLightDirMulax
	    && haveLightDirMadday
	    && haveLightDirMaddz
	    && haveCosineMax
	    && haveLightColorMulax
	    && haveLightColorMadday
	    && haveLightColorMaddz
	    && haveColorAdd
	    && haveColorMin
	    && haveGsAdd
	    && haveClipMul
	    && haveClipw
	    && haveFcand
	    && haveAdcOr
	    && haveAdcAdd
	    && haveDiv
	    && haveMfir
	    && haveFtoi
	    && haveTransformMulq
	    && haveTexMulq
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.xformedQReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.gsOffsetsReg.empty()
	    && !pattern.texInputReg.empty()
	    && !pattern.texOutputReg.empty()
	    && !pattern.clipReg.empty()
	    && !pattern.clipScalesReg.empty()
	    && !pattern.lightDir0Reg.empty()
	    && !pattern.lightDir1Reg.empty()
	    && !pattern.lightDir2Reg.empty()
	    && !pattern.cosinesReg.empty()
	    && !pattern.clampedCosinesReg.empty()
	    && !pattern.lightColor0Reg.empty()
	    && !pattern.lightColor1Reg.empty()
	    && !pattern.lightColor2Reg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.colorRawReg.empty()
	    && !pattern.constantColorReg.empty()
	    && !pattern.maxColorReg.empty();
}

void CodeGenerator::emitSceiSoftwarePipelineLoop( const SceiLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string normal = p.normalReg;
	const std::string strip = p.stripAdcReg;
	const std::string adc = p.newAdcReg;
	const std::string x = p.xformedReg;
	const std::string xQ = p.xformedQReg;
	const std::string gs = p.gsReg;
	const std::string texIn = p.texInputReg;
	const std::string texOut = p.texOutputReg;
	const std::string clip = p.clipReg;
	const std::string cos = p.cosinesReg;
	const std::string clamped = p.clampedCosinesReg;
	const std::string color = p.colorReg;
	const std::string colorRaw = p.colorRawReg;
	const std::string vf00 = "VF00";
	const std::string vi01 = "VI01";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), "nop");
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"), "nop");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), "nop");
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"), "nop");
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"),
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine("max.xyz " + clamped + ", " + cos + ", " + vf00,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "ibeq " + in + ", " + last + ", " + p.epilogOneLabel);
	emitRawPairedLine("mulq.xyz " + xQ + ", " + x + ", q",
	                  "iaddiu " + out + ", " + out + ", 0");

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"), "nop");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"), "nop");
	emitRawPairedLine("mul.xyz " + clip + ", " + xQ + ", " + p.clipScalesReg, "nop");
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"), "nop");
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"), "nop");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), "nop");
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"), "nop");
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"),
	                  "lq.xyz " + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine("clipw.xyz " + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"),
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine("add.xyz " + gs + ", " + xQ + ", " + p.gsOffsetsReg,
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("add.xyz " + color + ", " + colorRaw + ", " + p.constantColorReg,
	                  "iaddiu " + out + ", " + out + ", 6");
	emitRawPairedLine("max.xyz " + clamped + ", " + cos + ", " + vf00,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q",
	                  "fcand " + vi01 + ", " + p.clipImmediate);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs,
	                  "ior " + adc + ", " + vi01 + ", " + strip);
	emitRawPairedLine("miniw.xyz " + color + ", " + color + ", " + fieldArg(p.maxColorReg, "w"),
	                  "ibeq " + in + ", " + last + ", " + p.epilogTwoLabel);
	emitRawPairedLine("mulq.xyz " + xQ + ", " + x + ", q",
	                  "iaddiu " + adc + ", " + adc + ", " + p.adcImmediate);

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  "lq.xyz " + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  "iaddiu " + out + ", " + out + ", 3");
	emitRawPairedLine("mul.xyz " + clip + ", " + xQ + ", " + p.clipScalesReg,
	                  "sq " + color + ", " + offsetBase(-8, out));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  "sq.xyz " + texOut + ", " + offsetBase(-9, out));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"), "nop");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), "nop");
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"),
	                  "sq " + gs + ", " + offsetBase(-7, out));
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"),
	                  "lq.xyz " + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine("clipw.xyz " + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"),
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine("add.xyz " + gs + ", " + xQ + ", " + p.gsOffsetsReg,
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("add.xyz " + color + ", " + colorRaw + ", " + p.constantColorReg, "nop");
	emitRawPairedLine("max.xyz " + clamped + ", " + cos + ", " + vf00,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q",
	                  "fcand " + vi01 + ", " + p.clipImmediate);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs,
	                  "ior " + adc + ", " + vi01 + ", " + strip);
	emitRawPairedLine("miniw.xyz " + color + ", " + color + ", " + fieldArg(p.maxColorReg, "w"),
	                  "ibne " + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine("mulq.xyz " + xQ + ", " + x + ", q",
	                  "iaddiu " + adc + ", " + adc + ", " + p.adcImmediate);

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  "sq.xyz " + texOut + ", " + offsetBase(-6, out));
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("mul.xyz " + clip + ", " + xQ + ", " + p.clipScalesReg,
	                  "sq " + color + ", " + offsetBase(-5, out));
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"), "nop");
	emitRawPairedLine("add.xyz " + gs + ", " + xQ + ", " + p.gsOffsetsReg,
	                  "sq " + gs + ", " + offsetBase(-4, out));
	emitRawPairedLine("clipw.xyz " + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"),
	                  "lq.xyz " + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine("add.xyz " + color + ", " + colorRaw + ", " + p.constantColorReg,
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q",
	                  "fcand " + vi01 + ", " + p.clipImmediate);
	emitRawPairedLine("miniw.xyz " + color + ", " + color + ", " + fieldArg(p.maxColorReg, "w"),
	                  "ior " + adc + ", " + vi01 + ", " + strip);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs,
	                  "iaddiu " + adc + ", " + adc + ", " + p.adcImmediate);
	emitRawPairedLine("nop", "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("nop", "sq.xyz " + texOut + ", " + offsetBase(-3, out));
	emitRawPairedLine("nop", "sq " + color + ", " + offsetBase(-2, out));
	emitRawPairedLine("nop", "b " + p.exitLabel);
	emitRawPairedLine("nop", "sq " + gs + ", " + offsetBase(-1, out));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"), "nop");
	emitRawPairedLine("mul.xyz " + clip + ", " + xQ + ", " + p.clipScalesReg, "nop");
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"), "nop");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"), "nop");
	emitRawPairedLine("clipw.xyz " + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"),
	                  "lq.xyz " + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine("add.xyz " + gs + ", " + xQ + ", " + p.gsOffsetsReg,
	                  "ilw.w " + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine("add.xyz " + color + ", " + colorRaw + ", " + p.constantColorReg, "nop");
	emitRawPairedLine("mulq.xyz " + texOut + ", " + texIn + ", q",
	                  "fcand " + vi01 + ", " + p.clipImmediate);
	emitRawPairedLine("ftoi4.xyz " + gs + ", " + gs,
	                  "ior " + adc + ", " + vi01 + ", " + strip);
	emitRawPairedLine("miniw.xyz " + color + ", " + color + ", " + fieldArg(p.maxColorReg, "w"),
	                  "iaddiu " + adc + ", " + adc + ", " + p.adcImmediate);
	emitRawPairedLine("nop", "mfir.w " + gs + ", " + adc);
	emitRawPairedLine("nop", "sq.xyz " + texOut + ", " + offsetBase(0, out));
	emitRawPairedLine("nop", "sq " + color + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "sq " + gs + ", " + offsetBase(2, out));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitDirLightNoSpecSoftwarePipelineLoop( std::list<Token>& tokens,
                                                               std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "dir_light_vert_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end()
	    && ( (afterBranch->flags() & Token::BRANCH_DELAY_FILLER)
	         || afterBranch->label().length() == 0 ) )
		++afterBranch;

	DirLightNoSpecLoopPipelinePattern pattern;
	if( !collectDirLightNoSpecLoopPipelinePattern(token, afterBranch, pattern) )
	{
		std::list<Token>::iterator extendedAfterBranch = afterBranch;
		if( extendedAfterBranch != tokens.end() && extendedAfterBranch->label().length() == 0 )
			++extendedAfterBranch;
		if( extendedAfterBranch == afterBranch
		    || !collectDirLightNoSpecLoopPipelinePattern(token, extendedAfterBranch, pattern) )
			return false;
		afterBranch = extendedAfterBranch;
	}

	emitDirLightNoSpecSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectDirLightNoSpecLoopPipelinePattern( std::list<Token>::iterator begin,
                                                              std::list<Token>::iterator end,
                                                              DirLightNoSpecLoopPipelinePattern& pattern )
{
	pattern = DirLightNoSpecLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologOneLabel = pattern.sourceLabel + "__PRO1";
	pattern.prologTwoLabel = pattern.sourceLabel + "__PRO2";
	pattern.prologThreeLabel = pattern.sourceLabel + "__PRO3";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.fallbackOneLabel = pattern.sourceLabel + "__FALLBACK1";
	pattern.fallbackTwoLabel = pattern.sourceLabel + "__FALLBACK2";
	pattern.fallbackThreeLabel = pattern.sourceLabel + "__FALLBACK3";
	pattern.fallbackFourLabel = pattern.sourceLabel + "__FALLBACK4";
	pattern.drainLabel = pattern.sourceLabel + "__DRAIN_TAIL";
	pattern.scalarLabel = pattern.sourceLabel + "__SCALAR_FALLBACK";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveNormalLoad = false;
	bool haveColorLoad = false;
	bool haveMul = false;
	bool haveAdday = false;
	bool haveMaddx = false;
	bool haveMaxx = false;
	bool haveMulz = false;
	bool haveMula = false;
	bool haveMadd = false;
	bool haveAdd = false;
	bool haveStore = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == "nop" )
			continue;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(token, 0, pattern.inputReg)
			    || !getRegisterArgKey(token, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			continue;
		}

		if( mnemonic == "lq" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				return false;

			if( pattern.inputReg.empty() || base == pattern.inputReg )
			{
				pattern.inputReg = base;
				pattern.normalReg = dst;
				pattern.normalOffset = offset;
				haveNormalLoad = true;
				continue;
			}
			if( pattern.outputReg.empty() || base == pattern.outputReg )
			{
				pattern.outputReg = base;
				pattern.colorReg = dst;
				pattern.colorOffset = offset;
				haveColorLoad = true;
				continue;
			}
			return false;
		}

		if( mnemonic == "iaddiu" )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				return false;
			if( dst != src )
				return false;
			long value = 0;
			if( !evaluateIntegerExpression(imm, value) )
				return false;
			if( !pattern.inputReg.empty() && dst == pattern.inputReg )
			{
				pattern.inputStep = value;
				haveInputIncrement = true;
				continue;
			}
			if( !pattern.outputReg.empty() && dst == pattern.outputReg )
			{
				pattern.outputStep = value;
				haveOutputIncrement = true;
				continue;
			}
			return false;
		}

		if( mnemonic == "mul" && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				return false;
			if( !pattern.normalReg.empty() && right == pattern.normalReg )
			{
				pattern.productReg = dst;
				pattern.lightDirReg = left;
				haveMul = true;
				continue;
			}
			return false;
		}

		if( mnemonic == "adda" && token.broadcast() == Token::Y && tokenHasFields(token, Token::Z) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 1, src) || src != pattern.productReg )
				return false;
			haveAdday = true;
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				return false;
			if( dst != pattern.productReg || product != pattern.productReg )
				return false;
			pattern.dotBaseReg = src;
			haveMaddx = true;
			continue;
		}

		if( mnemonic == "max" && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			if( dst != pattern.productReg || src != pattern.productReg )
				return false;
			haveMaxx = true;
			continue;
		}

		if( mnemonic == "mul" && token.broadcast() == Token::Z
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				return false;
			if( product != pattern.productReg )
				return false;
			pattern.scaledReg = dst;
			pattern.lightColorReg = src;
			haveMulz = true;
			continue;
		}

		if( mnemonic == "mula" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			std::string material;
			if( !getRegisterArgKey(token, 1, src) || !getRegisterArgKey(token, 2, material) )
				return false;
			if( src != pattern.scaledReg )
				return false;
			pattern.materialMulReg = material;
			haveMula = true;
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string material;
			std::string ambient;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, material)
			    || !getRegisterArgKey(token, 2, ambient) )
				return false;
			pattern.litReg = dst;
			pattern.materialAddReg = material;
			pattern.ambientReg = ambient;
			haveMadd = true;
			continue;
		}

		if( mnemonic == "add" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				return false;
			if( left != pattern.colorReg || right != pattern.litReg )
				return false;
			pattern.resultReg = dst;
			haveAdd = true;
			continue;
		}

		if( mnemonic == "sq" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				return false;
			if( base != pattern.outputReg || src != pattern.resultReg )
				return false;
			pattern.storeOffset = offset;
			haveStore = true;
			continue;
		}

		return false;
	}

	if( !(haveBranch
	      && haveInputIncrement
	      && haveOutputIncrement
	      && haveNormalLoad
	      && haveColorLoad
	      && haveMul
	      && haveAdday
	      && haveMaddx
	      && haveMaxx
	      && haveMulz
	      && haveMula
	      && haveMadd
	      && haveAdd
	      && haveStore
	      && pattern.inputStep == 3
	      && pattern.outputStep == 3
	      && pattern.normalOffset == 1
	      && pattern.colorOffset == 1
	      && pattern.storeOffset == 1) )
		return false;

	std::list<std::string> used;
	used.push_back(pattern.normalReg);
	used.push_back(pattern.colorReg);
	used.push_back(pattern.lightDirReg);
	used.push_back(pattern.productReg);
	used.push_back(pattern.dotBaseReg);
	used.push_back(pattern.lightColorReg);
	used.push_back(pattern.materialMulReg);
	used.push_back(pattern.materialAddReg);
	used.push_back(pattern.ambientReg);
	used.push_back(pattern.scaledReg);
	used.push_back(pattern.litReg);
	used.push_back(pattern.resultReg);

	const char* candidates[] = { "VF24", "VF25", "VF26", "VF27", "VF28", "VF29", "VF30", "VF31" };
	for( unsigned int i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i )
	{
		if( containsKey(used, candidates[i]) )
			continue;
		if( pattern.dotReg.empty() )
		{
			pattern.dotReg = candidates[i];
			used.push_back(pattern.dotReg);
			continue;
		}
		pattern.dotNextReg = candidates[i];
		break;
	}

	return !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.lightDirReg.empty()
	    && !pattern.productReg.empty()
	    && !pattern.dotBaseReg.empty()
	    && !pattern.lightColorReg.empty()
	    && !pattern.materialMulReg.empty()
	    && !pattern.materialAddReg.empty()
	    && !pattern.ambientReg.empty()
	    && !pattern.scaledReg.empty()
	    && !pattern.litReg.empty()
	    && !pattern.resultReg.empty()
	    && !pattern.dotReg.empty()
	    && !pattern.dotNextReg.empty();
}

void CodeGenerator::emitDirLightNoSpecSoftwarePipelineLoop( const DirLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string color = p.colorReg;
	const std::string product = p.productReg;
	const std::string dot = p.dotReg;
	const std::string dotNext = p.dotNextReg;
	const std::string scaled = p.scaledReg;
	const std::string lit = p.litReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "lq.xyz " + color + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mul.xyz " + product + ", " + p.lightDirReg + ", " + normal,
	                  "ibeq " + in + ", " + last + ", " + p.fallbackOneLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.prologOneLabel + ":");
	emitRawPairedLine("adday.z ACC, " + product + ", " + fieldArg(product, "y"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("maddx.z " + dot + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), "nop");
	emitRawPairedLine("mul.xyz " + product + ", " + p.lightDirReg + ", " + normal,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "ibeq " + in + ", " + last + ", " + p.fallbackTwoLabel);
	emitRawPairedLine("maxx.z " + dot + ", " + dot + ", " + fieldArg(vf00, "x"), "nop");

	m_codeLines.push_back(p.prologTwoLabel + ":");
	emitRawPairedLine("adday.z ACC, " + product + ", " + fieldArg(product, "y"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("maddx.z " + dotNext + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), "nop");
	emitRawPairedLine("mul.xyz " + product + ", " + p.lightDirReg + ", " + normal,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mulz.xyz " + scaled + ", " + p.lightColorReg + ", " + fieldArg(dot, "z"),
	                  "ibeq " + in + ", " + last + ", " + p.fallbackThreeLabel);
	emitRawPairedLine("maxx.z " + dot + ", " + dotNext + ", " + fieldArg(vf00, "x"), "nop");

	m_codeLines.push_back(p.prologThreeLabel + ":");
	emitRawPairedLine("adday.z ACC, " + product + ", " + fieldArg(product, "y"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("maddx.z " + dotNext + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), "nop");
	emitRawPairedLine("mula.xyz ACC, " + scaled + ", " + p.materialMulReg, "nop");
	emitRawPairedLine("mul.xyz " + product + ", " + p.lightDirReg + ", " + normal,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("mulz.xyz " + scaled + ", " + p.lightColorReg + ", " + fieldArg(dot, "z"), "nop");
	emitRawPairedLine("madd.xyz " + lit + ", " + p.materialAddReg + ", " + p.ambientReg,
	                  "ibeq " + in + ", " + last + ", " + p.fallbackFourLabel);
	emitRawPairedLine("maxx.z " + dot + ", " + dotNext + ", " + fieldArg(vf00, "x"), "nop");

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine("adday.z ACC, " + product + ", " + fieldArg(product, "y"),
	                  "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("maddx.z " + dotNext + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), "nop");
	emitRawPairedLine("add.xyz " + result + ", " + color + ", " + lit,
	                  "iaddiu " + out + ", " + out + ", 3");
	emitRawPairedLine("mula.xyz ACC, " + scaled + ", " + p.materialMulReg, "nop");
	emitRawPairedLine("mul.xyz " + product + ", " + p.lightDirReg + ", " + normal,
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("madd.xyz " + lit + ", " + p.materialAddReg + ", " + p.ambientReg,
	                  "lq.xyz " + color + ", " + offsetBase(1, out));
	emitRawPairedLine("mulz.xyz " + scaled + ", " + p.lightColorReg + ", " + fieldArg(dot, "z"),
	                  "ibne " + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine("maxx.z " + dot + ", " + dotNext + ", " + fieldArg(vf00, "x"),
	                  "sq.xyz " + result + ", " + offsetBase(-2, out));

	m_codeLines.push_back(p.drainLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 12");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.fallbackOneLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.fallbackTwoLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 6");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.fallbackThreeLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 9");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.fallbackFourLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 12");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	emitDirLightNoSpecScalarFallbackLoop(p);
}

void CodeGenerator::emitDirLightNoSpecScalarFallbackLoop( const DirLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string color = p.colorReg;
	const std::string product = p.productReg;
	const std::string scaled = p.scaledReg;
	const std::string lit = p.litReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.scalarLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "lq.xyz " + color + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mul.xyz " + product + ", " + p.lightDirReg + ", " + normal, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("adday.z ACC, " + product + ", " + fieldArg(product, "y"), "nop");
	emitRawPairedLine("maddx.z " + product + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("maxx.z " + product + ", " + product + ", " + fieldArg(vf00, "x"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mulz.xyz " + scaled + ", " + p.lightColorReg + ", " + fieldArg(product, "z"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mula.xyz ACC, " + scaled + ", " + p.materialMulReg, "nop");
	emitRawPairedLine("madd.xyz " + lit + ", " + p.materialAddReg + ", " + p.ambientReg, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("add.xyz " + result + ", " + color + ", " + lit, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "sq.xyz " + result + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "ibne " + in + ", " + last + ", " + p.scalarLabel);
	emitRawPairedLine("nop", "iaddiu " + out + ", " + out + ", 3");

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitPtLightNoSpecSoftwarePipelineLoop( std::list<Token>& tokens,
                                                              std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "pt_light_vert_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	PtLightNoSpecLoopPipelinePattern pattern;
	if( !collectPtLightNoSpecLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitPtLightNoSpecSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectPtLightNoSpecLoopPipelinePattern( std::list<Token>::iterator begin,
                                                             std::list<Token>::iterator end,
                                                             PtLightNoSpecLoopPipelinePattern& pattern )
{
	pattern = PtLightNoSpecLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.drainLabel = pattern.sourceLabel + "__DRAIN_TAIL";
	pattern.fallbackOneLabel = pattern.sourceLabel + "__FALLBACK1";
	pattern.fallbackTwoLabel = pattern.sourceLabel + "__FALLBACK2";
	pattern.scalarLabel = pattern.sourceLabel + "__SCALAR_FALLBACK";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveNormalLoad = false;
	bool haveVertexLoad = false;
	bool haveColorLoad = false;
	bool haveSub = false;
	bool haveDistanceMul = false;
	bool haveDistanceAdday = false;
	bool haveDistanceMaddx = false;
	bool haveSqrt = false;
	bool haveDistanceSetOne = false;
	bool haveDistanceSetLength = false;
	bool haveInverseDistanceDiv = false;
	bool haveNormalize = false;
	bool haveAttenProduct = false;
	bool haveAttenMulax = false;
	bool haveAttenMadday = false;
	bool haveAttenMaddz = false;
	bool haveNormalProduct = false;
	bool haveIntensityMulax = false;
	bool haveIntensityMadday = false;
	bool haveIntensityMaddz = false;
	bool haveClamp = false;
	bool haveLocalDiffuse = false;
	bool haveDiffuseMaterial = false;
	bool haveAmbientMaterial = false;
	bool haveAttenuationDiv = false;
	bool haveAttenuateColor = false;
	bool haveAccumulate = false;
	bool haveStore = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == "nop" )
			continue;
		if( mnemonic == "esadd" || mnemonic == "ersqrt" || mnemonic == "mfp" || mnemonic == "waitp" )
			return false;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == "lq" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				continue;

			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.vertexReg = dst;
				pattern.vertexOffset = offset;
				haveVertexLoad = true;
				continue;
			}
			if( base == pattern.inputReg && offset == 1 )
			{
				pattern.normalReg = dst;
				pattern.normalOffset = offset;
				haveNormalLoad = true;
				continue;
			}
			if( base != pattern.inputReg && offset == 1 )
			{
				if( pattern.outputReg.empty() || pattern.outputReg == base )
				{
					pattern.outputReg = base;
					pattern.currentColorReg = dst;
					pattern.colorOffset = offset;
					haveColorLoad = true;
				}
				continue;
			}
			return false;
		}

		if( mnemonic == "iaddiu" )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;
			if( dst != src )
				continue;
			long value = 0;
			if( !evaluateIntegerExpression(imm, value) )
				continue;
			if( dst == pattern.inputReg )
			{
				pattern.inputStep = value;
				haveInputIncrement = true;
			}
			else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
			{
				pattern.outputStep = value;
				haveOutputIncrement = true;
			}
			continue;
		}

		if( mnemonic == "sub" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( right == pattern.vertexReg )
			{
				pattern.toLightReg = dst;
				pattern.lightPosReg = left;
				haveSub = true;
			}
			continue;
		}

		if( mnemonic == "mul" && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( !pattern.toLightReg.empty() && left == pattern.toLightReg && right == pattern.toLightReg )
			{
				pattern.attenReg = dst;
				haveDistanceMul = true;
				continue;
			}
			if( !pattern.attenReg.empty() && left == pattern.attenReg )
			{
				pattern.attenProductReg = dst;
				pattern.attenCoeffReg = right;
				haveAttenProduct = true;
				continue;
			}
			if( !pattern.normalizedLightReg.empty()
			    && left == pattern.normalizedLightReg
			    && right == pattern.normalReg )
			{
				pattern.normalProductReg = dst;
				haveNormalProduct = true;
				continue;
			}
			continue;
		}

		if( mnemonic == "adda" && token.broadcast() == Token::Y && tokenHasFields(token, Token::Z) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.attenReg )
				haveDistanceAdday = true;
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				continue;
			if( dst == pattern.attenReg && product == pattern.attenReg )
			{
				pattern.onesReg = src;
				haveDistanceMaddx = true;
			}
			continue;
		}

		if( mnemonic == "sqrt" )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.attenReg )
				haveSqrt = true;
			continue;
		}

		if( mnemonic == "add" && token.broadcast() == Token::W && tokenHasFields(token, Token::X) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.attenReg )
				haveDistanceSetOne = true;
			continue;
		}

		Token::Argument qArg("");
		const bool addReadsQ = getArg(token, 2, qArg) && qArg.type() == Token::Argument::Q;
		if( (mnemonic == "addq" || (mnemonic == "add" && addReadsQ))
		    && tokenHasFields(token, Token::Y) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.attenReg )
				haveDistanceSetLength = true;
			continue;
		}

		if( mnemonic == "div" )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				continue;
			if( denom == pattern.attenReg )
			{
				if( !haveInverseDistanceDiv )
					haveInverseDistanceDiv = true;
				else
					haveAttenuationDiv = true;
			}
			continue;
		}

		if( mnemonic == "mulq" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.toLightReg )
			{
				pattern.normalizedLightReg = dst;
				haveNormalize = true;
			}
			else if( src == pattern.litColorReg )
			{
				pattern.attenuatedColorReg = dst;
				haveAttenuateColor = true;
			}
			continue;
		}

		if( mnemonic == "mula" && token.broadcast() == Token::X && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( getRegisterArgKey(token, 2, src) )
			{
				if( src == pattern.attenProductReg )
					haveAttenMulax = true;
				else if( src == pattern.normalProductReg )
					haveIntensityMulax = true;
			}
			continue;
		}

		if( mnemonic == "madda" && token.broadcast() == Token::Y && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( getRegisterArgKey(token, 2, src) )
			{
				if( src == pattern.attenProductReg )
					haveAttenMadday = true;
				else if( src == pattern.normalProductReg )
					haveIntensityMadday = true;
			}
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == Token::Z && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 2, src) )
				continue;
			if( src == pattern.attenProductReg )
			{
				if( dst != pattern.attenReg )
					continue;
				haveAttenMaddz = true;
			}
			else if( src == pattern.normalProductReg )
			{
				pattern.intensityReg = dst;
				haveIntensityMaddz = true;
			}
			continue;
		}

		if( mnemonic == "max" && token.broadcast() == Token::X && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && src == pattern.intensityReg )
			{
				pattern.clampedIntensityReg = dst;
				haveClamp = true;
			}
			continue;
		}

		if( mnemonic == "mul" && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( right == pattern.clampedIntensityReg )
			{
				pattern.localDiffuseReg = dst;
				pattern.lightDiffReg = left;
				haveLocalDiffuse = true;
			}
			continue;
		}

		if( mnemonic == "mula" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			std::string material;
			if( !getRegisterArgKey(token, 1, src) || !getRegisterArgKey(token, 2, material) )
				continue;
			if( src == pattern.localDiffuseReg )
			{
				pattern.materialDiffReg = material;
				haveDiffuseMaterial = true;
			}
			continue;
		}

		if( mnemonic == "madd" && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			pattern.litColorReg = dst;
			pattern.lightAmbReg = left;
			pattern.materialAmbReg = right;
			haveAmbientMaterial = true;
			continue;
		}

		if( mnemonic == "add" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( left == pattern.currentColorReg && right == pattern.attenuatedColorReg )
			{
				pattern.resultReg = dst;
				haveAccumulate = true;
			}
			continue;
		}

		if( mnemonic == "sq" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;
			if( base == pattern.outputReg && src == pattern.resultReg )
			{
				pattern.storeOffset = offset;
				haveStore = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveNormalLoad
	    && haveVertexLoad
	    && haveColorLoad
	    && haveSub
	    && haveDistanceMul
	    && haveDistanceAdday
	    && haveDistanceMaddx
	    && haveSqrt
	    && haveDistanceSetOne
	    && haveDistanceSetLength
	    && haveInverseDistanceDiv
	    && haveNormalize
	    && haveAttenProduct
	    && haveAttenMulax
	    && haveAttenMadday
	    && haveAttenMaddz
	    && haveNormalProduct
	    && haveIntensityMulax
	    && haveIntensityMadday
	    && haveIntensityMaddz
	    && haveClamp
	    && haveLocalDiffuse
	    && haveDiffuseMaterial
	    && haveAmbientMaterial
	    && haveAttenuationDiv
	    && haveAttenuateColor
	    && haveAccumulate
	    && haveStore
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && pattern.colorOffset == 1
	    && pattern.storeOffset == 1
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.currentColorReg.empty()
	    && !pattern.lightPosReg.empty()
	    && !pattern.toLightReg.empty()
	    && !pattern.attenReg.empty()
	    && !pattern.attenCoeffReg.empty()
	    && !pattern.attenProductReg.empty()
	    && !pattern.onesReg.empty()
	    && !pattern.normalizedLightReg.empty()
	    && !pattern.normalProductReg.empty()
	    && !pattern.intensityReg.empty()
	    && !pattern.clampedIntensityReg.empty()
	    && !pattern.lightDiffReg.empty()
	    && !pattern.localDiffuseReg.empty()
	    && !pattern.materialDiffReg.empty()
	    && !pattern.lightAmbReg.empty()
	    && !pattern.materialAmbReg.empty()
	    && !pattern.litColorReg.empty()
	    && !pattern.attenuatedColorReg.empty()
	    && !pattern.resultReg.empty();
}

void CodeGenerator::emitPtLightNoSpecSoftwarePipelineLoop( const PtLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string vertex = p.vertexReg;
	const std::string currentColor = p.currentColorReg;
	const std::string lightPos = p.lightPosReg;
	const std::string delta = p.toLightReg;
	const std::string atten = p.attenReg;
	const std::string attenProduct = p.attenProductReg;
	const std::string normalized = p.normalizedLightReg;
	const std::string lit = p.litColorReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + delta + ", " + offsetBase(0, in));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("sub.xyz " + delta + ", " + lightPos + ", " + delta, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mul.xyz " + atten + ", " + delta + ", " + delta, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("adday.z ACC, " + atten + ", " + fieldArg(atten, "y"), "nop");
	emitRawPairedLine("maddx.z " + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), "nop");
	emitRawPairedLine("addw.x " + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine("nop", "sqrt q, " + fieldArg(atten, "z"));
	emitRawPairedLine("addq.y " + atten + ", " + vf00 + ", q", "waitq");
	emitRawPairedLine("nop", "ibeq " + in + ", " + last + ", " + p.fallbackOneLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + vertex + ", " + offsetBase(0, in));
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mul.xyz " + attenProduct + ", " + atten + ", " + p.attenCoeffReg,
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "y"));
	emitRawPairedLine("sub.xyz " + delta + ", " + lightPos + ", " + vertex,
	                  "move.xyz " + vertex + ", " + delta);
	emitRawPairedLine("mulax.w ACC, " + vf00 + ", " + fieldArg(attenProduct, "x"),
	                  "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("mul.xyz " + atten + ", " + delta + ", " + delta, "nop");
	emitRawPairedLine("nop", "waitq");
	emitRawPairedLine("mulq.xyz " + vertex + ", " + vertex + ", q",
	                  "lq.xyz " + normal + ", " + offsetBase(-5, in));
	emitRawPairedLine("adday.z ACC, " + atten + ", " + fieldArg(atten, "y"), "nop");
	emitRawPairedLine("maddx.z " + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), "nop");
	emitRawPairedLine("mul.xyz " + vertex + ", " + vertex + ", " + normal, "nop");
	emitRawPairedLine("madday.w ACC, " + vf00 + ", " + fieldArg(attenProduct, "y"),
	                  "sqrt q, " + fieldArg(atten, "z"));
	emitRawPairedLine("maddz.w " + p.materialAmbReg + ", " + vf00 + ", " + fieldArg(attenProduct, "z"), "nop");
	emitRawPairedLine("mulax.w ACC, " + vf00 + ", " + fieldArg(vertex, "x"), "nop");
	emitRawPairedLine("madday.w ACC, " + vf00 + ", " + fieldArg(vertex, "y"), "nop");
	emitRawPairedLine("maddz.w " + p.materialDiffReg + ", " + vf00 + ", " + fieldArg(vertex, "z"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("addw.x " + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine("addq.y " + atten + ", " + vf00 + ", q",
	                  "ibeq " + in + ", " + last + ", " + p.fallbackTwoLabel);
	emitRawPairedLine("maxx.w " + p.materialDiffReg + ", " + p.materialDiffReg + ", " + fieldArg(vf00, "x"), "nop");
	emitRawPairedLine("nop", "iaddiu " + out + ", " + out + ", 6");

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + vertex + ", " + offsetBase(0, in));
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mul.xyz " + attenProduct + ", " + atten + ", " + p.attenCoeffReg,
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "y"));
	emitRawPairedLine("mulw.xyz " + atten + ", " + p.lightDiffReg + ", " + fieldArg(p.materialDiffReg, "w"),
	                  "iaddiu " + out + ", " + out + ", 3");
	emitRawPairedLine("sub.xyz " + delta + ", " + lightPos + ", " + vertex,
	                  "move.xyz " + vertex + ", " + delta);
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mula.xyz ACC, " + atten + ", " + p.materialDiffReg, "nop");
	emitRawPairedLine("mul.xyz " + atten + ", " + delta + ", " + delta, "nop");
	emitRawPairedLine("mulq.xyz " + vertex + ", " + vertex + ", q",
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(p.materialAmbReg, "w"));
	emitRawPairedLine("madd.xyz " + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg,
	                  "lq.xyz " + normal + ", " + offsetBase(-2, in));
	emitRawPairedLine("mulax.w ACC, " + vf00 + ", " + fieldArg(attenProduct, "x"), "nop");
	emitRawPairedLine("adday.z ACC, " + atten + ", " + fieldArg(atten, "y"), "nop");
	emitRawPairedLine("maddx.z " + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), "nop");
	emitRawPairedLine("mul.xyz " + vertex + ", " + vertex + ", " + normal, "nop");
	emitRawPairedLine("madday.w ACC, " + vf00 + ", " + fieldArg(attenProduct, "y"), "nop");
	emitRawPairedLine("maddz.w " + p.materialAmbReg + ", " + vf00 + ", " + fieldArg(attenProduct, "z"),
	                  "lq.xyz " + attenProduct + ", " + offsetBase(-8, out));
	emitRawPairedLine("mulq.xyz " + lit + ", " + lit + ", q",
	                  "sqrt q, " + fieldArg(atten, "z"));
	emitRawPairedLine("mulax.w ACC, " + vf00 + ", " + fieldArg(vertex, "x"), "nop");
	emitRawPairedLine("madday.w ACC, " + vf00 + ", " + fieldArg(vertex, "y"), "nop");
	emitRawPairedLine("maddz.w " + p.materialDiffReg + ", " + vf00 + ", " + fieldArg(vertex, "z"), "nop");
	emitRawPairedLine("add.xyz " + attenProduct + ", " + attenProduct + ", " + lit, "nop");
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("addw.x " + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine("addq.y " + atten + ", " + vf00 + ", q",
	                  "ibne " + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine("maxx.w " + p.materialDiffReg + ", " + p.materialDiffReg + ", " + fieldArg(vf00, "x"),
	                  "sq.xyz " + attenProduct + ", " + offsetBase(-8, out));

	m_codeLines.push_back(p.drainLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 6");
	emitRawPairedLine("nop", "isubiu " + out + ", " + out + ", 6");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.fallbackOneLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.fallbackTwoLabel + ":");
	emitRawPairedLine("nop", "isubiu " + in + ", " + in + ", 6");
	emitRawPairedLine("nop", "b " + p.scalarLabel);
	emitRawPairedLine("nop", "nop");

	emitPtLightNoSpecScalarFallbackLoop(p);
}

void CodeGenerator::emitPtLightNoSpecScalarFallbackLoop( const PtLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string vertex = p.vertexReg;
	const std::string currentColor = p.currentColorReg;
	const std::string delta = p.toLightReg;
	const std::string atten = p.attenReg;
	const std::string attenProduct = p.attenProductReg;
	const std::string normalized = p.normalizedLightReg;
	const std::string normalProduct = p.normalProductReg;
	const std::string intensity = p.intensityReg;
	const std::string clamped = p.clampedIntensityReg;
	const std::string localDiffuse = p.localDiffuseReg;
	const std::string lit = p.litColorReg;
	const std::string attenuated = p.attenuatedColorReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.scalarLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + normal + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "lq.xyz " + vertex + ", " + offsetBase(0, in));
	emitRawPairedLine("nop", "lq.xyz " + currentColor + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("sub.xyz " + delta + ", " + p.lightPosReg + ", " + vertex, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mul.xyz " + atten + ", " + delta + ", " + delta, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("adday.z ACC, " + atten + ", " + fieldArg(atten, "y"), "nop");
	emitRawPairedLine("maddx.z " + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), "nop");
	emitRawPairedLine("addw.x " + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), "nop");
	emitRawPairedLine("nop", "sqrt q, " + fieldArg(atten, "z"));
	emitRawPairedLine("addq.y " + atten + ", " + vf00 + ", q", "waitq");
	emitRawPairedLine("mul.xyz " + attenProduct + ", " + atten + ", " + p.attenCoeffReg,
	                  "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "y"));
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mulax.w ACC, " + vf00 + ", " + fieldArg(attenProduct, "x"), "nop");
	emitRawPairedLine("madday.w ACC, " + vf00 + ", " + fieldArg(attenProduct, "y"), "nop");
	emitRawPairedLine("maddz.w " + atten + ", " + vf00 + ", " + fieldArg(attenProduct, "z"), "nop");
	emitRawPairedLine("mulq.xyz " + normalized + ", " + delta + ", q", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "div q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "w"));
	emitRawPairedLine("mul.xyz " + normalProduct + ", " + normalized + ", " + normal, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mulax.w ACC, " + vf00 + ", " + fieldArg(normalProduct, "x"), "nop");
	emitRawPairedLine("madday.w ACC, " + vf00 + ", " + fieldArg(normalProduct, "y"), "nop");
	emitRawPairedLine("maddz.w " + intensity + ", " + vf00 + ", " + fieldArg(normalProduct, "z"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("maxx.w " + clamped + ", " + intensity + ", " + fieldArg(vf00, "x"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mulw.xyz " + localDiffuse + ", " + p.lightDiffReg + ", " + fieldArg(clamped, "w"), "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mula.xyz ACC, " + localDiffuse + ", " + p.materialDiffReg, "nop");
	emitRawPairedLine("madd.xyz " + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("mulq.xyz " + attenuated + ", " + lit + ", q", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("add.xyz " + result + ", " + currentColor + ", " + attenuated, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "sq.xyz " + result + ", " + offsetBase(1, out));
	emitRawPairedLine("nop", "ibne " + in + ", " + last + ", " + p.scalarLabel);
	emitRawPairedLine("nop", "iaddiu " + out + ", " + out + ", 3");

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitFinalColorSoftwarePipelineLoop( std::list<Token>& tokens,
                                                           std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "final_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	FinalColorLoopPipelinePattern pattern;
	if( !collectFinalColorLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitFinalColorSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectFinalColorLoopPipelinePattern( std::list<Token>::iterator begin,
                                                          std::list<Token>::iterator end,
                                                          FinalColorLoopPipelinePattern& pattern )
{
	pattern = FinalColorLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveLoad = false;
	bool haveMini = false;
	bool haveFtoi = false;
	bool haveStore = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
		{
			continue;
		}

		if( mnemonic == "lq" )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( !pattern.inputReg.empty()
			    && base == pattern.inputReg
			    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.colorReg = dst;
				if( pattern.colorReg == pattern.minReg )
				{
					pattern.minReg = "VF25";
					pattern.outputReg = "VF26";
				}
				else if( pattern.colorReg == pattern.outputReg )
				{
					pattern.outputReg = "VF26";
				}
				pattern.loadOffset = offset;
				haveLoad = true;
			}
			continue;
		}

		if( mnemonic == "iaddiu" )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;
			if( dst == src && !pattern.inputReg.empty() && dst == pattern.inputReg )
			{
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				pattern.inputStep = value;
				haveInputIncrement = true;
			}
			continue;
		}

		if( (mnemonic == "mini" || mnemonic == "minii")
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src == pattern.colorReg )
				haveMini = true;
			continue;
		}

		if( mnemonic == "ftoi0" && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src == pattern.colorReg )
				haveFtoi = true;
			continue;
		}

		if( mnemonic == "sq" )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;
			if( !pattern.inputReg.empty() && base == pattern.inputReg && src == pattern.colorReg )
			{
				pattern.storeOffsetAfterIncrement = offset;
				haveStore = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveLoad
	    && haveMini
	    && haveFtoi
	    && haveStore
	    && pattern.inputStep == 3
	    && pattern.loadOffset == 1
	    && pattern.storeOffsetAfterIncrement == -2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.minReg.empty()
	    && !pattern.outputReg.empty();
}

void CodeGenerator::emitFinalColorSoftwarePipelineLoop( const FinalColorLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string last = p.lastInputReg;
	const std::string output = p.colorReg;
	const std::string minReg = p.minReg;
	const std::string load = p.outputReg;

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + output + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("minii.xyz " + minReg + ", " + output + ", i",
	                  "ibeq " + in + ", " + last + ", " + p.epilogOneLabel);
	emitRawPairedLine("nop", "nop");

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + load + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("ftoi0.xyz " + output + ", " + minReg,
	                  "ibeq " + in + ", " + last + ", " + p.epilogTwoLabel);
	emitRawPairedLine("minii.xyz " + minReg + ", " + load + ", i", "nop");

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine("nop", "lq.xyz " + load + ", " + offsetBase(1, in));
	emitRawPairedLine("nop", "iaddiu " + in + ", " + in + ", 3");
	emitRawPairedLine("nop", "sq " + output + ", " + offsetBase(-8, in));
	emitRawPairedLine("ftoi0.xyz " + output + ", " + minReg,
	                  "ibne " + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine("minii.xyz " + minReg + ", " + load + ", i", "nop");

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("ftoi0.xyz " + output + ", " + minReg,
	                  "sq " + output + ", " + offsetBase(-5, in));
	emitRawPairedLine("nop", "b " + p.exitLabel);
	emitRawPairedLine("nop", "sq " + output + ", " + offsetBase(-2, in));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("ftoi0.xyz " + output + ", " + minReg, "nop");
	emitRawPairedLine("nop", "nop");
	emitRawPairedLine("nop", "sq " + output + ", " + offsetBase(-2, in));

	m_codeLines.push_back(p.exitLabel + ":");
}

void CodeGenerator::emitRawPairedLine( const std::string& upper, const std::string& lower )
{
	const int instructionLength = 32;
	std::string line;
	for( int d = 0; d < 20; d++ )
		line += " ";
	line += upper;

	int pad = instructionLength - int(upper.length());
	if( pad <= 0 )
		line += " ";
	for( int d = 0; d < pad; d++ )
		line += " ";

	line += lower;
	m_codeLines.push_back(line);
	++m_currentCycle;
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
