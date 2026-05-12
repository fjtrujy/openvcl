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
	void emitSingleToken( const Token& token );
	void emitPairedTokens( const Token& a, const Token& b );
	int readHazardDelay( const Token& token, const Token* partner ) const;
	void padForReadHazards( const Token& token, const Token* partner );
	void recordRegisterWrites( const Token& token, int issueCycle );

	// Dual-pipe pairing helpers.  See CodeGenerator.cpp for the contract.
	static bool isEmittableInstruction( const Token& t );
	static bool tokensCanPair( const Token& a, const Token& b );
	static bool tokenCanMoveBefore( const Token& moved, const Token& crossed );
	static bool tokenRangeCanBeCrossed( const Token& first, const Token& last );
	static bool hasDataDependency( const Token& a, const Token& b );
	std::string formatPairedLine( const Token& upper, const Token& lower );

	std::list<std::string> m_codeLines;

	bool m_emitSource;
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
};

#include "CodeGenerator.inl"

}

#endif //__OPENVCL_CODEGENERATOR_H__
