#ifndef __OPENVCL_VUSCHEDULINGRULES_H__
#define __OPENVCL_VUSCHEDULINGRULES_H__

/*
 * VuSchedulingRules.h
 *
 * Shared scheduling predicates for VU tokens.  These helpers keep the
 * stateless resource, memory, branch, and pairing rules out of the code
 * emitter so future scheduling passes can reuse the same contract.
 */

#include "Token.h"

#include <list>
#include <string>

namespace vcl
{

bool isVuEmittableInstruction( const Token& token );

std::string lowerVuTokenName( const Token& token );
bool isVuMtir( const Token& token );
bool isVuFtoiConversion( const std::string& name );
bool isVuLoadToFtoiBypassProducer( const std::string& name );

bool isVuMacReader( const std::string& name );
bool isVuClipReader( const std::string& name );
bool isVuClipw( const std::string& name );

bool vuTokenHasInstructionFlag( const Token& token, unsigned int flag );
unsigned int vuTokenBranchDelaySlots( const Token& token );
bool isVuTerminalUnconditionalBranch( const Token& token );

bool vuTokenReadsQ( const Token& token );
bool vuTokenWritesQ( const Token& token );
bool vuTokenReadsP( const Token& token );
bool vuTokenWritesP( const Token& token );
bool vuTokenReadsRegister( const Token& token, const std::string& key );

void collectVuRegisterReadKeys( const Token& token, std::list<std::string>& reads );
void collectVuRegisterWriteKeys( const Token& token, std::list<std::string>& writes );

bool isVuZeroMoveFromVf00( const Token& token );
bool isVuMoveAsUpperMaxCandidate( const Token& token );
bool vuTokenListReadsMac( const std::list<Token>& tokens );
bool vuTokenListReadsClip( const std::list<Token>& tokens );

bool isVuPlainMemoryStore( const Token& token );
bool isVuPlainMemoryLoad( const Token& token );
bool isVuXgkick( const Token& token );
bool isVuMemoryOrderingAccess( const Token& token );
bool isVuBoundaryOperand( const Token& token );
bool isVuSchedulingBarrier( const Token& token );
bool isVuReadyScheduleCandidate( const Token& token );
bool isVuLowerPipe( const Token& token );
bool isVuLongLatencyProducer( const Token& token );
bool isVuLatencyLoad( const Token& token );

bool vuTokensHaveDataDependency( const Token& a, const Token& b );
bool vuTokenCanMoveBefore( const Token& moved,
                           const Token& crossed,
                           unsigned int ignoredImplicitWawResources = 0 );
bool vuTokenRangeCanBeCrossed( const Token& first, const Token& last );
bool vuTokenPairResourcesAreIndependent( const Token& a,
                                         const Token& b,
                                         bool aWritesMac,
                                         bool bWritesMac );

void coalesceAdjacentVuIntegerAdds( std::list<Token>& tokens );

}

#endif
