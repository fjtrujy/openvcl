#ifndef __OPENVCL_VSMCOSTANALYZER_H__
#define __OPENVCL_VSMCOSTANALYZER_H__

/*
 * VsmCostAnalyzer.h
 *
 * Static cost reporting for already-scheduled VU .vsm files.
 */

#include <iosfwd>
#include <string>
#include <vector>

namespace vcl
{

class VsmCostAnalyzer
{
public:
	VsmCostAnalyzer();

	bool analyze( std::istream& stream, const std::string& inputName );

	bool writeText( std::ostream& stream ) const;
	bool writeJson( std::ostream& stream ) const;

private:
	enum Unit
	{
		UNIT_UNKNOWN,
		UNIT_NOP,
		UNIT_UPPER,
		UNIT_LOWER,
		UNIT_BRANCH,
		UNIT_FDIV,
		UNIT_EFU,
		UNIT_WAITQ,
		UNIT_WAITP
	};

	struct Slot
	{
		Slot();

		bool present;
		bool nop;
		bool eBit;
		bool dBit;
		bool tBit;
		unsigned int latency;
		std::string text;
		std::string mnemonic;
		Unit unit;
	};

	struct Block
	{
		Block( const std::string& blockLabel );

		std::string label;
		unsigned int cycles;
		unsigned int upperInstructions;
		unsigned int lowerInstructions;
		unsigned int pairedCycles;
		unsigned int singleUpperCycles;
		unsigned int singleLowerCycles;
		unsigned int nopOnlyCycles;
		unsigned int operationLatencyCycles;
		unsigned int longLatencyOps;
		unsigned int longLatencyCycles;
	};

	void reset();
	void startBlock( const std::string& label );
	bool analyzeLine( const std::string& line, unsigned int lineNumber );
	bool parseCycle( const std::string& line, Slot& upper, Slot& lower ) const;
	void recordCycle( const Slot& upper, const Slot& lower );

	static std::string stripComment( const std::string& line );
	static std::string trim( const std::string& text );
	static std::string firstWord( const std::string& text );
	static std::string lower( const std::string& text );
	static std::string normalizeMnemonic( const std::string& word );
	static Unit classifyMnemonic( const std::string& mnemonic );
	static unsigned int instructionLatency( const std::string& mnemonic );
	static bool isKnownMnemonic( const std::string& mnemonic );
	static bool startsInstructionToken( const std::string& line, std::string::size_type pos );
	static bool isDirective( const std::string& line );
	static bool isLabelOnly( const std::string& line );
	static std::string jsonEscape( const std::string& text );

	std::string m_inputName;
	std::vector<Block> m_blocks;
	unsigned int m_currentBlock;

	unsigned int m_staticCycles;
	unsigned int m_upperInstructions;
	unsigned int m_lowerInstructions;
	unsigned int m_pairedCycles;
	unsigned int m_singleUpperCycles;
	unsigned int m_singleLowerCycles;
	unsigned int m_nopOnlyCycles;
	unsigned int m_nopSlots;
	unsigned int m_branchCycles;
	unsigned int m_waitqCycles;
	unsigned int m_waitpCycles;
	unsigned int m_fdivOps;
	unsigned int m_efuOps;
	unsigned int m_eBitCycles;
	unsigned int m_dBitCycles;
	unsigned int m_tBitCycles;
	unsigned int m_unknownInstructions;
	unsigned int m_slotMismatches;
	unsigned int m_ignoredLines;
	unsigned int m_operationLatencyCycles;
	unsigned int m_longLatencyOps;
	unsigned int m_longLatencyCycles;
	unsigned int m_maxOpLatency;
};

}

#endif
