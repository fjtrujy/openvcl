#ifndef __OPENVCL_CODEGENERATOR_H__
#define __OPENVCL_CODEGENERATOR_H__

/*
 * CodeGenerator.h
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

#include <list>
#include <string>
#include <istream>
#include <sstream>
#include <map>
#include "Token.h"
#include "Dependency.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Class
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

class CodeGenerator
{
public:
	CodeGenerator();
	~CodeGenerator();

	bool beginProcess(const std::list<Token>& tokens);
	bool write(std::ostream& stream);

	void setEmitSource( bool emitSource );
	bool emitSource() const;

	void setName( const std::string& name );
	const std::string& name() const;

private:

	static unsigned int cleanFields( unsigned int fields, unsigned int flags, const Token& token );

	std::string generateInstruction(const Token& token);
	std::string generateOperand(const Token& token);

	std::string registerArg(const Token::Argument& arg, const Token& token);
	std::string immediateArg(const Token::Argument& arg, const Token& token );
	std::string accumulatorArg( const Token::Argument& arg, const Token& token );

	void addNopLine();
	void emitWaitQ();
	void emitWaitP();
	void emitUpperWithWait( const Token& token, bool waitQ );
	bool branchNeedsPreBubble( const Token& token ) const;
	void padForBranchPreBubble( const Token& token );
	void emitSingleToken( const Token& token );
	void emitBranchWithDelayFiller( const Token& branch, const Token& filler );
	void emitPairedTokens( const Token& a, const Token& b );
	int readHazardDelay( const Token& token, const Token* partner ) const;
	void padForReadHazards( const Token& token, const Token* partner );
	void recordRegisterWrites( const Token& token, int issueCycle );
	unsigned int ignoredImplicitWawResourcesForRemaining( std::list<Token>::const_iterator begin,
	                                                       std::list<Token>::const_iterator end ) const;
	void fillPreIncrementStoreBranchDelaySlots( std::list<Token>& tokens ) const;
	void fillBranchDelaySlots( std::list<Token>& tokens ) const;
	void fillDeadFallthroughBranchDelaySlots( std::list<Token>& tokens ) const;
	bool movePreIncrementStoreIntoBranchDelaySlot( std::list<Token>& tokens,
	                                               std::list<Token>::iterator branch ) const;
	bool moveIndependentStoreIntoBranchDelaySlot( std::list<Token>& tokens,
	                                              std::list<Token>::iterator branch ) const;
	bool moveDeadFallthroughIntoBranchDelaySlot( std::list<Token>& tokens,
	                                             std::list<Token>::iterator branch ) const;
	bool canMoveIntoBranchDelaySlot( const Token& candidate, const Token& branch ) const;
	bool nextTokenIsBranchDelayFiller( std::list<Token>::iterator token,
	                                   std::list<Token>::iterator end ) const;
	bool branchTargetLabel( const Token& branch, std::string& label ) const;
	bool writesAreDeadFromTarget( const std::list<std::string>& writes,
	                              std::list<Token>::iterator target,
	                              std::list<Token>::iterator end ) const;
	bool emitsAsUpperZeroMove( const Token& token ) const;
	bool tokenIsLowerExecutionPath( const Token& token ) const;
	bool tokenIsUpperExecutionPath( const Token& token ) const;
	std::string generateUpperZeroMoveInstruction( const Token& token );
	struct FastNoLightsLoopPipelinePattern;
	struct FastLitLoopPipelinePattern;
	bool tryEmitFastNoLightsSoftwarePipelineLoop( std::list<Token>& tokens,
	                                              std::list<Token>::iterator& token );
	bool collectFastNoLightsLoopPipelinePattern( std::list<Token>::iterator begin,
	                                             std::list<Token>::iterator end,
	                                             FastNoLightsLoopPipelinePattern& pattern );
	void emitFastNoLightsSoftwarePipelineLoop( const FastNoLightsLoopPipelinePattern& pattern );
	bool tryEmitFastLitSoftwarePipelineLoop( std::list<Token>& tokens,
	                                         std::list<Token>::iterator& token );
	bool collectFastLitLoopPipelinePattern( std::list<Token>::iterator begin,
	                                        std::list<Token>::iterator end,
	                                        FastLitLoopPipelinePattern& pattern );
	void emitFastLitSoftwarePipelineLoop( const FastLitLoopPipelinePattern& pattern );
	void emitRawPairedLine( const std::string& upper, const std::string& lower );

	// Dual-pipe pairing helpers.  Stateless legality rules live in
	// VuSchedulingRules; CodeGenerator only supplies emission-specific state.
	bool tokensCanPair( const Token& a, const Token& b ) const;
	std::string formatPairedLine( const Token& upper, const Token& lower );

	std::list<std::string> m_codeLines;

	bool m_emitSource;
	bool m_enableUpperZeroMoves;
	unsigned int m_ignoredImplicitWawResources;
	std::string m_name;

	// VU1 has a 4-cycle FMAC pipeline.  An FMAC writes the MAC / CLIP /
	// STATUS flag registers 4 cycles after issue, so any flag-reader
	// (fmand / fcand / fsand / fcget …) issued sooner sees the previous
	// flag value.  We track the line index (= cycle index — every emitted
	// hardware line is one VU cycle) of the last FMAC emit and the last
	// clipw emit, and insert NOPs before a flag-reader if the relevant
	// FMAC was too recent.
	//
	// Initialized to a sentinel comfortably more than 4 cycles before
	// m_currentCycle starts at 0, so the first flag-reader doesn't get
	// spurious NOPs jammed in front of it.
	int m_currentCycle;
	int m_lastFMACCycle;
	int m_lastClipwCycle;
	int m_qReadyCycle;
	int m_pReadyCycle;
	std::map<std::string, int> m_registerReadyCycle;
	std::map<std::string, std::string> m_registerProducerMnemonic;
};

#include "CodeGenerator.inl"

}

#endif //__OPENVCL_CODEGENERATOR_H__
