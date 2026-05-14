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
#include "Token.h"
#include "Dependency.h"
#include "VuLatencyTracker.h"

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

	void setKnownLoopOptimizations( bool enabled );
	bool knownLoopOptimizations() const;
	void setGenericSoftwarePipelining( bool enabled );
	bool genericSoftwarePipelining() const;
	void setStrictScheduleSlots( bool enabled );
	bool strictScheduleSlots() const;

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
	void emitPairedBranchWithDelayFiller( const Token& a, const Token& b, const Token& filler );
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
	bool tryEmitKnownLoopOptimization( std::list<Token>& tokens,
	                                   std::list<Token>::iterator& token );
	struct FastNoLightsLoopPipelinePattern;
	struct FastLitLoopPipelinePattern;
	struct SceiLoopPipelinePattern;
	struct LinearXformLoopPipelinePattern;
	struct DirLightSpecLoopPipelinePattern;
	struct DirLightNoSpecLoopPipelinePattern;
	struct PtLightSpecLoopPipelinePattern;
	struct PtLightNoSpecLoopPipelinePattern;
	struct FinalColorLoopPipelinePattern;
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
	bool tryEmitSceiSoftwarePipelineLoop( std::list<Token>& tokens,
	                                      std::list<Token>::iterator& token );
	bool collectSceiLoopPipelinePattern( std::list<Token>::iterator begin,
	                                     std::list<Token>::iterator end,
	                                     SceiLoopPipelinePattern& pattern );
	void emitSceiSoftwarePipelineLoop( const SceiLoopPipelinePattern& pattern );
	bool tryEmitLinearXformSoftwarePipelineLoop( std::list<Token>& tokens,
	                                             std::list<Token>::iterator& token );
	bool collectLinearXformLoopPipelinePattern( std::list<Token>::iterator begin,
	                                            std::list<Token>::iterator end,
	                                            LinearXformLoopPipelinePattern& pattern );
	void emitLinearXformSoftwarePipelineLoop( const LinearXformLoopPipelinePattern& pattern );
	void emitLinearXformScalarBody( const LinearXformLoopPipelinePattern& pattern );
	bool tryEmitPs2glPrimitiveXformSoftwarePipelineLoop( std::list<Token>& tokens,
	                                                     std::list<Token>::iterator& token );
	void emitPs2glTriXformSoftwarePipelineLoop( bool pvDiff );
	void emitPs2glQuadXformSoftwarePipelineLoop();
	void emitPs2glQuadXformScalarFallbackLoop();
	void emitPs2glPvDiffQuadXformSoftwarePipelineLoop();
	void emitPs2glIndexedXformSoftwarePipelineLoop();
	bool tryEmitDirLightSpecSoftwarePipelineLoop( std::list<Token>& tokens,
	                                              std::list<Token>::iterator& token );
	bool collectDirLightSpecLoopPipelinePattern( std::list<Token>::iterator begin,
	                                             std::list<Token>::iterator end,
	                                             DirLightSpecLoopPipelinePattern& pattern );
	void emitDirLightSpecSoftwarePipelineLoop( const DirLightSpecLoopPipelinePattern& pattern );
	void emitDirLightSpecScalarFallbackLoop( const DirLightSpecLoopPipelinePattern& pattern );
	bool tryEmitDirLightNoSpecSoftwarePipelineLoop( std::list<Token>& tokens,
	                                                std::list<Token>::iterator& token );
	bool collectDirLightNoSpecLoopPipelinePattern( std::list<Token>::iterator begin,
	                                               std::list<Token>::iterator end,
	                                               DirLightNoSpecLoopPipelinePattern& pattern );
	void emitDirLightNoSpecSoftwarePipelineLoop( const DirLightNoSpecLoopPipelinePattern& pattern );
	void emitDirLightNoSpecScalarFallbackLoop( const DirLightNoSpecLoopPipelinePattern& pattern );
	bool tryEmitPtLightSpecSoftwarePipelineLoop( std::list<Token>& tokens,
	                                             std::list<Token>::iterator& token );
	bool collectPtLightSpecLoopPipelinePattern( std::list<Token>::iterator begin,
	                                            std::list<Token>::iterator end,
	                                            PtLightSpecLoopPipelinePattern& pattern );
	void emitPtLightSpecSoftwarePipelineLoop( const PtLightSpecLoopPipelinePattern& pattern );
	void emitPtLightSpecScalarFallbackLoop( const PtLightSpecLoopPipelinePattern& pattern );
	bool tryEmitPtLightNoSpecSoftwarePipelineLoop( std::list<Token>& tokens,
	                                               std::list<Token>::iterator& token );
	bool collectPtLightNoSpecLoopPipelinePattern( std::list<Token>::iterator begin,
	                                              std::list<Token>::iterator end,
	                                              PtLightNoSpecLoopPipelinePattern& pattern );
	void emitPtLightNoSpecSoftwarePipelineLoop( const PtLightNoSpecLoopPipelinePattern& pattern );
	void emitPtLightNoSpecScalarFallbackLoop( const PtLightNoSpecLoopPipelinePattern& pattern );
	bool tryEmitFinalColorSoftwarePipelineLoop( std::list<Token>& tokens,
	                                            std::list<Token>::iterator& token );
	bool collectFinalColorLoopPipelinePattern( std::list<Token>::iterator begin,
	                                           std::list<Token>::iterator end,
	                                           FinalColorLoopPipelinePattern& pattern );
	void emitFinalColorSoftwarePipelineLoop( const FinalColorLoopPipelinePattern& pattern );
	void emitRawPairedLine( const std::string& upper, const std::string& lower );

	// Dual-pipe pairing helpers.  Stateless legality rules live in
	// VuSchedulingRules; CodeGenerator only supplies emission-specific state.
	bool tokensCanPair( const Token& a, const Token& b ) const;
	std::string formatPairedLine( const Token& upper, const Token& lower );

	std::list<std::string> m_codeLines;

	bool m_emitSource;
	bool m_knownLoopOptimizations;
	bool m_genericSoftwarePipelining;
	bool m_strictScheduleSlots;
	bool m_enableUpperZeroMoves;
	unsigned int m_ignoredImplicitWawResources;
	std::string m_name;

	int m_currentCycle;
	VuLatencyTracker m_latencyTracker;
};

#include "CodeGenerator.inl"

}

#endif //__OPENVCL_CODEGENERATOR_H__
