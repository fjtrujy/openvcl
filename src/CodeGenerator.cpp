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

CodeGenerator::CodeGenerator()
{
	m_currentCycle    = 0;
	// Sentinels < 0 by more than FMAC latency so the first flag-reader
	// in the program doesn't trip the cooldown.
	m_lastFMACCycle   = -10;
	m_lastClipwCycle  = -10;
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

	for( std::list<Token>::const_iterator k = tokens.begin(); k != tokens.end(); ++k )
	{
		Token token = *k;
		std::string instruction;
		std::string outputLine = "";

		//handle label
		if(token.label().length()>0)
			m_codeLines.push_back(token.label() + ":");

		if(!token.operand())
			continue;

		if( ((token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER)) && !exitWritten )
		{
			m_codeLines.push_back(std::string("                    nop[E]                          nop"));
			m_codeLines.push_back(std::string("                    nop                             nop"));
			exitWritten = true;
		}

		if(token.flags()&Token::IGNORED)
			continue;

		if(!(token.flags()&Token::PROCESSED) && !(token.operand()->flags()&Operand::PREPROCESSOR) )
			continue;

		//handle alignment
		if(token.operand()->flags()&Operand::PREPROCESSOR)
		{
			if(token.operand()->name() == ".vu")
			{
				// .vu and alignment are already handled at the start of beginProcess()
				// when m_name is set, so skip adding redundant directives here
				continue;
			}
			else if( token.operand()->name() == "--cont" )
			{
				m_codeLines.push_back(std::string("                    nop[E]                          nop"));
				m_codeLines.push_back(std::string("                    nop                             nop"));
				exitWritten = true;
				continue;
			}
			else if( token.operand()->name() == "--barrier" )
				continue;
		}
		else exitWritten = false;

		if( (token.operand()->flags()&Operand::FILTERED) )
			continue;

		// --- DUAL-PIPE PAIRING ---
		// Adjacent-only first cut: peek at the immediately-next token in
		// the list.  If we can pair it with the current token, emit one
		// combined line and consume both.  Otherwise fall through to the
		// existing single-token emission below.
		//
		// We keep the iterator advanced past the partner by toggling a
		// flag on the outer scope so the next loop iteration knows to
		// skip its own emission step.  Labels / preprocessor side-effects
		// for the partner still run because they live before this
		// emission point.
		{
			std::list<Token>::const_iterator p = k;
			++p;
			const Token* partner = NULL;
			if( p != tokens.end() )
			{
				if( isEmittableInstruction(*p)
				    && (*p).label().length() == 0
				    && tokensCanPair(token, *p) )
				{
					partner = &*p;
				}
			}

			// VU1 FMAC pipeline is 4 cycles deep.  If THIS cycle would
			// read MAC/CLIP and the relevant FMAC ran less than 4 cycles
			// ago, pad with NOPs until the flag is settled.  We check
			// both halves of the (possibly-paired) cycle because the
			// flag-reader may be on either pipe.
			{
				const std::string& aName = token.operand()->name();
				bool readsMac  = isMacReader(aName);
				bool readsClip = isClipReader(aName);
				if( partner && partner->operand() )
				{
					const std::string& bName = partner->operand()->name();
					readsMac  = readsMac  || isMacReader(bName);
					readsClip = readsClip || isClipReader(bName);
				}
				if( readsMac )
				{
					int gap = m_currentCycle - m_lastFMACCycle;
					while( gap < 4 )
					{
						addNopLine();
						m_currentCycle++;
						gap = m_currentCycle - m_lastFMACCycle;
					}
				}
				if( readsClip )
				{
					int gap = m_currentCycle - m_lastClipwCycle;
					while( gap < 4 )
					{
						addNopLine();
						m_currentCycle++;
						gap = m_currentCycle - m_lastClipwCycle;
					}
				}
			}

			if( partner )
			{
				std::string pairedLine;
				if( token.operand()->isLowerExecutionPath() )
					pairedLine = formatPairedLine(*partner, token);
				else
					pairedLine = formatPairedLine(token, *partner);
				m_codeLines.push_back(pairedLine);
				// Either side of the pair could be the FMAC / clipw we
				// need to remember for the cooldown.
				if( token.operand()->unit() == Operand::FMAC || partner->operand()->unit() == Operand::FMAC )
					m_lastFMACCycle = m_currentCycle;
				if( isClipw(token.operand()->name()) || isClipw(partner->operand()->name()) )
					m_lastClipwCycle = m_currentCycle;
				m_currentCycle++;
				++k;
				continue;
			}
		}

		instruction = generateInstruction(token);

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
			m_codeLines.push_back(outputLine);
			m_currentCycle++;
		}

		// Remember this cycle as the most recent FMAC / clipw so
		// downstream flag-readers can pad to the 4-cycle pipeline.
		if( token.operand()->unit() == Operand::FMAC )
			m_lastFMACCycle = m_currentCycle - 1;
		if( isClipw(token.operand()->name()) )
			m_lastClipwCycle = m_currentCycle - 1;

		//check if fdiv pipeline
		if(token.operand()->unit() == Operand::FDIV)
		{
			m_codeLines.push_back(std::string("                    nop                             waitq"));
			m_currentCycle++;
		}
		//check if efu pipeline
		if(token.operand()->unit() == Operand::EFU)
		{
			m_codeLines.push_back(std::string("                    nop                             waitp"));
			m_currentCycle++;
		}
	}

	if( !exitWritten )
	{
		// Auto-terminate with an exit pair if the last instruction wasn't closed
		m_codeLines.push_back(std::string("                    nop[E]                          nop"));
		m_codeLines.push_back(std::string("                    nop                             nop"));
		// Do not treat as an error; some VCL programs end in a loop
	}

	if( m_name.length() > 0 )
		m_codeLines.push_back( std::string(m_name) + "_CodeEnd:" );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CodeGenerator::addNopLine()
{
	m_codeLines.push_back(std::string("                    nop                             nop"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dual-pipe pairing helpers.
//
// VU1 issues two instructions per cycle: one upper-pipe (FMAC) and one
// lower-pipe (LSU / IALU / BRU / FDIV / RANDU / EFU).  Sony's vcl fills both
// slots per cycle for ~2x throughput; openvcl historically emits a single
// instruction per cycle with NOP on the unused pipe.  This pass pairs the
// current token with the immediately-following token when (and only when)
// they sit on different pipes, have no data dependency in either direction,
// neither is a hard-scheduled instruction (BRU/FDIV/EFU), neither carries
// a label, and neither is marked PREORDERED.  This is the minimum-viable
// pairing — it does not hoist instructions across non-adjacent positions.
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
// (ACC / Q / P / I / R) touched by a token, distinguishing reads and
// writes.  These resources have NO Alias and aren't routed through the
// register allocator, so the only way to spot conflicts is by inspecting
// the operand template + flags.
//
// Returns two bitmasks, indexed by the bits below.
namespace {
	enum ResourceBit {
		RES_ACC = 1 << 0,
		RES_Q   = 1 << 1,
		RES_P   = 1 << 2,
		RES_I   = 1 << 3,
		RES_R   = 1 << 4
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

		Dependency* aDep = (*ai).dependency();
		if( !aDep || !aDep->alias() )
			continue;
		const Register* aReg = aDep->alias()->allocatedRegister();

		for( std::list<Token::Argument>::const_iterator bi = bArgs.begin(); bi != bArgs.end(); ++bi )
		{
			Token::Argument::Type bType = (*bi).type();
			if( bType != aType )
				continue;
			Dependency* bDep = (*bi).dependency();
			if( !bDep || !bDep->alias() )
				continue;
			const Register* bReg = bDep->alias()->allocatedRegister();

			bool same = ( aReg && bReg )
				? ( aReg == bReg )
				: ( aDep->alias() == bDep->alias() );
			if( !same )
				continue;

			// RAW (b reads what a writes) or WAW (b also writes).
			return true;
		}
	}

	return false;
}

// Names of VU1 "flag-reading" instructions: these read the MAC/CLIP/
// status flag registers that every FMAC instruction implicitly
// updates 4 cycles after issue.  Pairing an FMAC with the very next
// flag-reader places the flag read in the same cycle as the flag
// write, so the reader sees stale flags from before the FMAC.  See
// the bfc_tri macro in ps2gl/vu1/clip_cull.i:
//
//     opmsub.xyz bfc_normal, delta_2, delta_1   ; updates MAC sign
//     fmand      z_sign, z_sign_mask            ; reads opmsub's MAC
//
// Pairing those two breaks the back-face cull bit.  We block FMAC
// pairing with any of these readers, regardless of which specific
// flag (MAC/CLIP/status) each one targets — conservative and safe.
static bool isFlagReader( const std::string& name )
{
	return name == "fmand" || name == "fmeq" || name == "fmor"
	    || name == "fcand" || name == "fceq" || name == "fcor"
	    || name == "fcget"
	    || name == "fsand" || name == "fseq" || name == "fsor"
	    // Uppercase variants emitted by some macro expansions.
	    || name == "FMAND" || name == "FMEQ" || name == "FMOR"
	    || name == "FCAND" || name == "FCEQ" || name == "FCOR"
	    || name == "FCGET"
	    || name == "FSAND" || name == "FSEQ" || name == "FSOR";
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

	// Skip instructions whose surrounding scheduling scaffolding (NOP
	// brackets for BRU, waitq for FDIV, waitp for EFU) breaks if we
	// reshape the cycle.  RANDU is allowed.
	Operand::Unit ua = a.operand()->unit();
	Operand::Unit ub = b.operand()->unit();
	if( ua == Operand::BRU || ub == Operand::BRU )
		return false;
	if( ua == Operand::FDIV || ub == Operand::FDIV )
		return false;
	if( ua == Operand::EFU || ub == Operand::EFU )
		return false;

	// FMAC writes MAC/CLIP flags with 4-cycle latency; a flag-reader
	// in the same cycle sees pre-FMAC flags, not the source-intended
	// post-FMAC ones.  See isFlagReader() for the list and rationale.
	bool aFMAC = (ua == Operand::FMAC);
	bool bFMAC = (ub == Operand::FMAC);
	if( aFMAC && isFlagReader(b.operand()->name()) )
		return false;
	if( bFMAC && isFlagReader(a.operand()->name()) )
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
