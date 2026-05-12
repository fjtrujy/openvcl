/*
 * VsmCostAnalyzer.cpp
 *
 * Static cost reporting for already-scheduled VU .vsm files.
 */

#include "VsmCostAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace vcl
{

namespace
{
	bool isSpace( char c )
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	bool isWordChar( char c )
	{
		return std::isalnum( static_cast<unsigned char>( c ) )
		    || c == '_' || c == '.' || c == '[' || c == ']';
	}

	bool setContains( const std::set<std::string>& values, const std::string& key )
	{
		return values.find( key ) != values.end();
	}

	std::set<std::string> makeUpperOps()
	{
		std::set<std::string> ops;
		const char* names[] =
		{
			"abs",
			"add", "addi", "addq", "adda", "addai", "addaq",
			"sub", "subi", "subq", "suba", "subai", "subaq",
			"mul", "muli", "mulq", "mula", "mulai", "mulaq",
			"madd", "maddi", "maddq", "madda", "maddai", "maddaq",
			"msub", "msubi", "msubq", "msuba", "msubai", "msubaq",
			"max", "maxi", "mini", "minii",
			"opmula", "opmsub",
			"ftoi0", "ftoi4", "ftoi12", "ftoi15",
			"itof0", "itof4", "itof12", "itof15",
			"clip", "clipw", "cliplw",
			0
		};
		for( unsigned int i = 0; names[i]; ++i )
			ops.insert( names[i] );
		return ops;
	}

	std::set<std::string> makeLowerOps()
	{
		std::set<std::string> ops;
		const char* names[] =
		{
			"iadd", "iaddi", "iaddiu", "iand", "ior", "isub", "isubiu",
			"move", "mfir", "mtir", "mr32",
			"lq", "lqd", "lqi", "sq", "sqd", "sqi",
			"ilw", "isw", "ilwr", "iswr", "loi",
			"rinit", "rget", "rnext", "rxor",
			"fsand", "fseq", "fsor", "fsset",
			"fmand", "fmeq", "fmor",
			"fcand", "fceq", "fcor", "fcset", "fcget",
			"xgkick", "xtop", "xitop",
			0
		};
		for( unsigned int i = 0; names[i]; ++i )
			ops.insert( names[i] );
		return ops;
	}

	std::set<std::string> makeBranchOps()
	{
		std::set<std::string> ops;
		const char* names[] =
		{
			"ibeq", "ibgez", "ibgtz", "iblez", "ibltz", "ibne",
			"b", "bal", "jr", "jalr",
			0
		};
		for( unsigned int i = 0; names[i]; ++i )
			ops.insert( names[i] );
		return ops;
	}

	std::set<std::string> makeFdivOps()
	{
		std::set<std::string> ops;
		ops.insert( "div" );
		ops.insert( "sqrt" );
		ops.insert( "rsqrt" );
		return ops;
	}

	std::set<std::string> makeEfuOps()
	{
		std::set<std::string> ops;
		const char* names[] =
		{
			"mfp",
			"esadd", "ersadd", "eleng", "erleng",
			"eatanxy", "eatanxz", "esum", "ercpr", "ersqrt",
			"esin", "eatan", "eexp",
			0
		};
		for( unsigned int i = 0; names[i]; ++i )
			ops.insert( names[i] );
		return ops;
	}

	const std::set<std::string>& upperOps()
	{
		static const std::set<std::string> ops = makeUpperOps();
		return ops;
	}

	const std::set<std::string>& lowerOps()
	{
		static const std::set<std::string> ops = makeLowerOps();
		return ops;
	}

	const std::set<std::string>& branchOps()
	{
		static const std::set<std::string> ops = makeBranchOps();
		return ops;
	}

	const std::set<std::string>& fdivOps()
	{
		static const std::set<std::string> ops = makeFdivOps();
		return ops;
	}

	const std::set<std::string>& efuOps()
	{
		static const std::set<std::string> ops = makeEfuOps();
		return ops;
	}
}

VsmCostAnalyzer::Slot::Slot()
{
	present = false;
	nop = false;
	eBit = false;
	dBit = false;
	tBit = false;
	latency = 0;
	unit = UNIT_UNKNOWN;
}

VsmCostAnalyzer::Block::Block( const std::string& blockLabel )
{
	label = blockLabel;
	cycles = 0;
	upperInstructions = 0;
	lowerInstructions = 0;
	pairedCycles = 0;
	singleUpperCycles = 0;
	singleLowerCycles = 0;
	nopOnlyCycles = 0;
	operationLatencyCycles = 0;
	longLatencyOps = 0;
	longLatencyCycles = 0;
}

VsmCostAnalyzer::VsmCostAnalyzer()
{
	reset();
}

void VsmCostAnalyzer::reset()
{
	m_inputName.clear();
	m_blocks.clear();
	m_blocks.push_back( Block( "<entry>" ) );
	m_currentBlock = 0;

	m_staticCycles = 0;
	m_upperInstructions = 0;
	m_lowerInstructions = 0;
	m_pairedCycles = 0;
	m_singleUpperCycles = 0;
	m_singleLowerCycles = 0;
	m_nopOnlyCycles = 0;
	m_nopSlots = 0;
	m_branchCycles = 0;
	m_waitqCycles = 0;
	m_waitpCycles = 0;
	m_fdivOps = 0;
	m_efuOps = 0;
	m_eBitCycles = 0;
	m_dBitCycles = 0;
	m_tBitCycles = 0;
	m_unknownInstructions = 0;
	m_slotMismatches = 0;
	m_ignoredLines = 0;
	m_operationLatencyCycles = 0;
	m_longLatencyOps = 0;
	m_longLatencyCycles = 0;
	m_maxOpLatency = 0;
}

bool VsmCostAnalyzer::analyze( std::istream& stream, const std::string& inputName )
{
	reset();
	m_inputName = inputName.length() ? inputName : "stdin";

	std::string line;
	unsigned int lineNumber = 1;
	while( std::getline( stream, line ) )
	{
		if( !analyzeLine( line, lineNumber ) )
			return false;
		++lineNumber;
	}

	return true;
}

void VsmCostAnalyzer::startBlock( const std::string& label )
{
	if( m_blocks[m_currentBlock].cycles == 0 )
	{
		m_blocks[m_currentBlock].label = label;
		return;
	}

	m_blocks.push_back( Block( label ) );
	m_currentBlock = static_cast<unsigned int>( m_blocks.size() - 1 );
}

bool VsmCostAnalyzer::analyzeLine( const std::string& line, unsigned int /*lineNumber*/ )
{
	std::string stripped = trim( stripComment( line ) );
	if( stripped.empty() )
		return true;

	if( isDirective( stripped ) )
		return true;

	if( isLabelOnly( stripped ) )
	{
		startBlock( stripped.substr( 0, stripped.size() - 1 ) );
		return true;
	}

	Slot upper;
	Slot lowerSlot;
	if( parseCycle( line, upper, lowerSlot ) )
	{
		recordCycle( upper, lowerSlot );
		return true;
	}

	++m_ignoredLines;
	return true;
}

bool VsmCostAnalyzer::parseCycle( const std::string& line, Slot& upper, Slot& lowerSlot ) const
{
	std::string body = stripComment( line );
	std::vector<std::string::size_type> positions;

	for( std::string::size_type i = 0; i < body.size(); ++i )
	{
		if( !startsInstructionToken( body, i ) )
			continue;

		std::string word = firstWord( body.substr( i ) );
		std::string mnemonic = normalizeMnemonic( word );
		if( isKnownMnemonic( mnemonic ) )
			positions.push_back( i );
	}

	if( positions.empty() )
		return false;

	if( positions.size() == 1 )
	{
		std::string slotText = trim( body.substr( positions[0] ) );
		Slot slot;
		slot.present = true;
		slot.text = slotText;
		slot.mnemonic = normalizeMnemonic( firstWord( slotText ) );
		slot.unit = classifyMnemonic( slot.mnemonic );
		slot.latency = instructionLatency( slot.mnemonic );
		slot.nop = slot.unit == UNIT_NOP;
		std::string word = lower( firstWord( slotText ) );
		slot.eBit = word.find( "[e" ) != std::string::npos;
		slot.dBit = word.find( "[d" ) != std::string::npos;
		slot.tBit = word.find( "[t" ) != std::string::npos;

		if( slot.unit == UNIT_UPPER )
			upper = slot;
		else
			lowerSlot = slot;

		return true;
	}

	upper.present = true;
	upper.text = trim( body.substr( positions[0], positions[1] - positions[0] ) );
	upper.mnemonic = normalizeMnemonic( firstWord( upper.text ) );
	upper.unit = classifyMnemonic( upper.mnemonic );
	upper.latency = instructionLatency( upper.mnemonic );
	upper.nop = upper.unit == UNIT_NOP;

	lowerSlot.present = true;
	lowerSlot.text = trim( body.substr( positions[1] ) );
	lowerSlot.mnemonic = normalizeMnemonic( firstWord( lowerSlot.text ) );
	lowerSlot.unit = classifyMnemonic( lowerSlot.mnemonic );
	lowerSlot.latency = instructionLatency( lowerSlot.mnemonic );
	lowerSlot.nop = lowerSlot.unit == UNIT_NOP;

	{
		std::string word = lower( firstWord( upper.text ) );
		upper.eBit = word.find( "[e" ) != std::string::npos;
		upper.dBit = word.find( "[d" ) != std::string::npos;
		upper.tBit = word.find( "[t" ) != std::string::npos;
	}
	{
		std::string word = lower( firstWord( lowerSlot.text ) );
		lowerSlot.eBit = word.find( "[e" ) != std::string::npos;
		lowerSlot.dBit = word.find( "[d" ) != std::string::npos;
		lowerSlot.tBit = word.find( "[t" ) != std::string::npos;
	}

	return true;
}

void VsmCostAnalyzer::recordCycle( const Slot& upper, const Slot& lowerSlot )
{
	const bool upperActive = upper.present && !upper.nop;
	const bool lowerActive = lowerSlot.present && !lowerSlot.nop;

	++m_staticCycles;
	m_nopSlots += upperActive ? 0 : 1;
	m_nopSlots += lowerActive ? 0 : 1;

	if( upperActive )
		++m_upperInstructions;
	if( lowerActive )
		++m_lowerInstructions;

	if( upperActive && lowerActive )
		++m_pairedCycles;
	else if( upperActive )
		++m_singleUpperCycles;
	else if( lowerActive )
		++m_singleLowerCycles;
	else
		++m_nopOnlyCycles;

	if( upper.eBit || lowerSlot.eBit )
		++m_eBitCycles;
	if( upper.dBit || lowerSlot.dBit )
		++m_dBitCycles;
	if( upper.tBit || lowerSlot.tBit )
		++m_tBitCycles;

	if( lowerSlot.unit == UNIT_BRANCH || upper.unit == UNIT_BRANCH )
		++m_branchCycles;
	if( lowerSlot.unit == UNIT_WAITQ || upper.unit == UNIT_WAITQ )
		++m_waitqCycles;
	if( lowerSlot.unit == UNIT_WAITP || upper.unit == UNIT_WAITP )
		++m_waitpCycles;
	if( lowerSlot.unit == UNIT_FDIV || upper.unit == UNIT_FDIV )
		++m_fdivOps;
	if( lowerSlot.unit == UNIT_EFU || upper.unit == UNIT_EFU )
		++m_efuOps;

	if( upperActive && upper.unit == UNIT_UNKNOWN )
		++m_unknownInstructions;
	if( lowerActive && lowerSlot.unit == UNIT_UNKNOWN )
		++m_unknownInstructions;

	if( upperActive && ( upper.unit == UNIT_LOWER || upper.unit == UNIT_BRANCH
		|| upper.unit == UNIT_FDIV || upper.unit == UNIT_EFU
		|| upper.unit == UNIT_WAITQ || upper.unit == UNIT_WAITP ) )
		++m_slotMismatches;
	if( lowerActive && lowerSlot.unit == UNIT_UPPER )
		++m_slotMismatches;

	if( upperActive )
	{
		m_operationLatencyCycles += upper.latency;
		if( upper.latency > m_maxOpLatency )
			m_maxOpLatency = upper.latency;
		if( upper.latency > 1 )
		{
			++m_longLatencyOps;
			m_longLatencyCycles += upper.latency - 1;
		}
	}
	if( lowerActive )
	{
		m_operationLatencyCycles += lowerSlot.latency;
		if( lowerSlot.latency > m_maxOpLatency )
			m_maxOpLatency = lowerSlot.latency;
		if( lowerSlot.latency > 1 )
		{
			++m_longLatencyOps;
			m_longLatencyCycles += lowerSlot.latency - 1;
		}
	}

	Block& block = m_blocks[m_currentBlock];
	++block.cycles;
	if( upperActive )
		++block.upperInstructions;
	if( lowerActive )
		++block.lowerInstructions;
	if( upperActive && lowerActive )
		++block.pairedCycles;
	else if( upperActive )
		++block.singleUpperCycles;
	else if( lowerActive )
		++block.singleLowerCycles;
	else
		++block.nopOnlyCycles;

	if( upperActive )
	{
		block.operationLatencyCycles += upper.latency;
		if( upper.latency > 1 )
		{
			++block.longLatencyOps;
			block.longLatencyCycles += upper.latency - 1;
		}
	}
	if( lowerActive )
	{
		block.operationLatencyCycles += lowerSlot.latency;
		if( lowerSlot.latency > 1 )
		{
			++block.longLatencyOps;
			block.longLatencyCycles += lowerSlot.latency - 1;
		}
	}
}

bool VsmCostAnalyzer::writeText( std::ostream& stream ) const
{
	const unsigned int instructions = m_upperInstructions + m_lowerInstructions;
	const unsigned int slots = m_staticCycles * 2;
	const unsigned int slotPressureMin = std::max( m_upperInstructions, m_lowerInstructions );
	const unsigned int excessCycles = ( m_staticCycles > slotPressureMin ) ? ( m_staticCycles - slotPressureMin ) : 0;

	stream << "VSM cost report" << std::endl;
	stream << "input: " << m_inputName << std::endl;
	stream << "static_cycles: " << m_staticCycles << std::endl;
	stream << "instruction_slots: " << slots << std::endl;
	stream << "instructions: " << instructions << std::endl;
	stream << "upper_instructions: " << m_upperInstructions << std::endl;
	stream << "lower_instructions: " << m_lowerInstructions << std::endl;
	stream << "paired_cycles: " << m_pairedCycles << std::endl;
	stream << "single_upper_cycles: " << m_singleUpperCycles << std::endl;
	stream << "single_lower_cycles: " << m_singleLowerCycles << std::endl;
	stream << "nop_only_cycles: " << m_nopOnlyCycles << std::endl;
	stream << "nop_slots: " << m_nopSlots << std::endl;

	stream << std::fixed << std::setprecision( 2 );
	stream << "slot_utilization_percent: "
	       << ( slots ? ( 100.0 * static_cast<double>( instructions ) / static_cast<double>( slots ) ) : 0.0 )
	       << std::endl;
	stream << "pairing_efficiency_percent: "
	       << ( m_staticCycles ? ( 100.0 * static_cast<double>( m_pairedCycles ) / static_cast<double>( m_staticCycles ) ) : 0.0 )
	       << std::endl;
	stream.unsetf( std::ios::floatfield );
	stream << std::setprecision( 6 );

	stream << "slot_pressure_min_cycles: " << slotPressureMin << std::endl;
	stream << "excess_cycles_over_slot_pressure: " << excessCycles << std::endl;
	stream << "operation_latency_cycles: " << m_operationLatencyCycles << std::endl;
	stream << "long_latency_ops: " << m_longLatencyOps << std::endl;
	stream << "long_latency_cycles: " << m_longLatencyCycles << std::endl;
	stream << "max_op_latency: " << m_maxOpLatency << std::endl;
	stream << "branch_cycles: " << m_branchCycles << std::endl;
	stream << "waitq_cycles: " << m_waitqCycles << std::endl;
	stream << "waitp_cycles: " << m_waitpCycles << std::endl;
	stream << "fdiv_ops: " << m_fdivOps << std::endl;
	stream << "efu_ops: " << m_efuOps << std::endl;
	stream << "e_bit_cycles: " << m_eBitCycles << std::endl;
	stream << "d_bit_cycles: " << m_dBitCycles << std::endl;
	stream << "t_bit_cycles: " << m_tBitCycles << std::endl;
	stream << "unknown_instructions: " << m_unknownInstructions << std::endl;
	stream << "slot_mismatches: " << m_slotMismatches << std::endl;
	stream << "ignored_lines: " << m_ignoredLines << std::endl;
	stream << "blocks:" << std::endl;

	for( std::vector<Block>::const_iterator i = m_blocks.begin(); i != m_blocks.end(); ++i )
	{
		if( i->cycles == 0 )
			continue;
		stream << "  " << i->label
		       << ": cycles=" << i->cycles
		       << " upper=" << i->upperInstructions
		       << " lower=" << i->lowerInstructions
		       << " paired=" << i->pairedCycles
		       << " single_upper=" << i->singleUpperCycles
		       << " single_lower=" << i->singleLowerCycles
		       << " nop_only=" << i->nopOnlyCycles
		       << " op_latency=" << i->operationLatencyCycles
		       << " long_ops=" << i->longLatencyOps
		       << " long_cycles=" << i->longLatencyCycles
		       << std::endl;
	}

	return true;
}

bool VsmCostAnalyzer::writeJson( std::ostream& stream ) const
{
	const unsigned int instructions = m_upperInstructions + m_lowerInstructions;
	const unsigned int slots = m_staticCycles * 2;
	const unsigned int slotPressureMin = std::max( m_upperInstructions, m_lowerInstructions );
	const unsigned int excessCycles = ( m_staticCycles > slotPressureMin ) ? ( m_staticCycles - slotPressureMin ) : 0;

	stream << "{" << std::endl;
	stream << "  \"input\": \"" << jsonEscape( m_inputName ) << "\"," << std::endl;
	stream << "  \"static_cycles\": " << m_staticCycles << "," << std::endl;
	stream << "  \"instruction_slots\": " << slots << "," << std::endl;
	stream << "  \"instructions\": " << instructions << "," << std::endl;
	stream << "  \"upper_instructions\": " << m_upperInstructions << "," << std::endl;
	stream << "  \"lower_instructions\": " << m_lowerInstructions << "," << std::endl;
	stream << "  \"paired_cycles\": " << m_pairedCycles << "," << std::endl;
	stream << "  \"single_upper_cycles\": " << m_singleUpperCycles << "," << std::endl;
	stream << "  \"single_lower_cycles\": " << m_singleLowerCycles << "," << std::endl;
	stream << "  \"nop_only_cycles\": " << m_nopOnlyCycles << "," << std::endl;
	stream << "  \"nop_slots\": " << m_nopSlots << "," << std::endl;
	stream << "  \"slot_pressure_min_cycles\": " << slotPressureMin << "," << std::endl;
	stream << "  \"excess_cycles_over_slot_pressure\": " << excessCycles << "," << std::endl;
	stream << "  \"operation_latency_cycles\": " << m_operationLatencyCycles << "," << std::endl;
	stream << "  \"long_latency_ops\": " << m_longLatencyOps << "," << std::endl;
	stream << "  \"long_latency_cycles\": " << m_longLatencyCycles << "," << std::endl;
	stream << "  \"max_op_latency\": " << m_maxOpLatency << "," << std::endl;
	stream << "  \"branch_cycles\": " << m_branchCycles << "," << std::endl;
	stream << "  \"waitq_cycles\": " << m_waitqCycles << "," << std::endl;
	stream << "  \"waitp_cycles\": " << m_waitpCycles << "," << std::endl;
	stream << "  \"fdiv_ops\": " << m_fdivOps << "," << std::endl;
	stream << "  \"efu_ops\": " << m_efuOps << "," << std::endl;
	stream << "  \"e_bit_cycles\": " << m_eBitCycles << "," << std::endl;
	stream << "  \"d_bit_cycles\": " << m_dBitCycles << "," << std::endl;
	stream << "  \"t_bit_cycles\": " << m_tBitCycles << "," << std::endl;
	stream << "  \"unknown_instructions\": " << m_unknownInstructions << "," << std::endl;
	stream << "  \"slot_mismatches\": " << m_slotMismatches << "," << std::endl;
	stream << "  \"ignored_lines\": " << m_ignoredLines << "," << std::endl;
	stream << "  \"blocks\": [" << std::endl;

	bool first = true;
	for( std::vector<Block>::const_iterator i = m_blocks.begin(); i != m_blocks.end(); ++i )
	{
		if( i->cycles == 0 )
			continue;
		if( !first )
			stream << "," << std::endl;
		first = false;
		stream << "    {"
		       << "\"label\": \"" << jsonEscape( i->label ) << "\", "
		       << "\"cycles\": " << i->cycles << ", "
		       << "\"upper_instructions\": " << i->upperInstructions << ", "
		       << "\"lower_instructions\": " << i->lowerInstructions << ", "
		       << "\"paired_cycles\": " << i->pairedCycles << ", "
		       << "\"single_upper_cycles\": " << i->singleUpperCycles << ", "
		       << "\"single_lower_cycles\": " << i->singleLowerCycles << ", "
		       << "\"nop_only_cycles\": " << i->nopOnlyCycles << ", "
		       << "\"operation_latency_cycles\": " << i->operationLatencyCycles << ", "
		       << "\"long_latency_ops\": " << i->longLatencyOps << ", "
		       << "\"long_latency_cycles\": " << i->longLatencyCycles
		       << "}";
	}

	stream << std::endl << "  ]" << std::endl;
	stream << "}" << std::endl;
	return true;
}

std::string VsmCostAnalyzer::stripComment( const std::string& line )
{
	std::string::size_type pos = line.find( ';' );
	if( pos == std::string::npos )
		return line;
	return line.substr( 0, pos );
}

std::string VsmCostAnalyzer::trim( const std::string& text )
{
	std::string::size_type first = 0;
	while( first < text.size() && isSpace( text[first] ) )
		++first;

	std::string::size_type last = text.size();
	while( last > first && isSpace( text[last - 1] ) )
		--last;

	return text.substr( first, last - first );
}

std::string VsmCostAnalyzer::firstWord( const std::string& text )
{
	std::string clean = trim( text );
	std::string::size_type end = 0;
	while( end < clean.size() && !isSpace( clean[end] ) )
		++end;
	return clean.substr( 0, end );
}

std::string VsmCostAnalyzer::lower( const std::string& text )
{
	std::string result = text;
	for( std::string::iterator i = result.begin(); i != result.end(); ++i )
		*i = static_cast<char>( std::tolower( static_cast<unsigned char>( *i ) ) );
	return result;
}

std::string VsmCostAnalyzer::normalizeMnemonic( const std::string& word )
{
	std::string mnemonic = lower( word );

	std::string::size_type flag = mnemonic.find( '[' );
	if( flag != std::string::npos )
		mnemonic = mnemonic.substr( 0, flag );

	std::string::size_type fields = mnemonic.find( '.' );
	if( fields != std::string::npos )
		mnemonic = mnemonic.substr( 0, fields );

	if( setContains( lowerOps(), mnemonic )
	    || setContains( branchOps(), mnemonic )
	    || setContains( fdivOps(), mnemonic )
	    || setContains( efuOps(), mnemonic )
	    || mnemonic == "waitq" || mnemonic == "waitp"
	    || mnemonic == "nop" )
		return mnemonic;

	if( setContains( upperOps(), mnemonic ) )
		return mnemonic;

	if( mnemonic.size() > 1 )
	{
		char suffix = mnemonic[mnemonic.size() - 1];
		if( suffix == 'x' || suffix == 'y' || suffix == 'z' || suffix == 'w' )
		{
			std::string base = mnemonic.substr( 0, mnemonic.size() - 1 );
			if( setContains( upperOps(), base ) )
				return base;
		}
	}

	return mnemonic;
}

VsmCostAnalyzer::Unit VsmCostAnalyzer::classifyMnemonic( const std::string& mnemonic )
{
	if( mnemonic == "nop" )
		return UNIT_NOP;
	if( mnemonic == "waitq" )
		return UNIT_WAITQ;
	if( mnemonic == "waitp" )
		return UNIT_WAITP;
	if( setContains( branchOps(), mnemonic ) )
		return UNIT_BRANCH;
	if( setContains( fdivOps(), mnemonic ) )
		return UNIT_FDIV;
	if( setContains( efuOps(), mnemonic ) )
		return UNIT_EFU;
	if( setContains( upperOps(), mnemonic ) )
		return UNIT_UPPER;
	if( setContains( lowerOps(), mnemonic ) )
		return UNIT_LOWER;
	return UNIT_UNKNOWN;
}

unsigned int VsmCostAnalyzer::instructionLatency( const std::string& mnemonic )
{
	if( mnemonic == "nop" )
		return 0;

	// Upper FMAC pipeline results, including MAC/CLIP flags, settle after
	// four cycles on VU1.  This latency table intentionally mirrors the
	// Operand metadata used by OpenVCL's compiler path, but works directly
	// from already-scheduled VSM text.
	if( setContains( upperOps(), mnemonic ) )
		return 4;

	if( mnemonic == "div" || mnemonic == "sqrt" )
		return 7;
	if( mnemonic == "rsqrt" )
		return 13;
	if( mnemonic == "waitq" || mnemonic == "waitp" )
		return 1;

	if( setContains( branchOps(), mnemonic ) )
		return 2;

	if( mnemonic == "mfp" )
		return 4;
	if( mnemonic == "esadd" )
		return 11;
	if( mnemonic == "ersadd" || mnemonic == "eleng" || mnemonic == "ersqrt" )
		return 18;
	if( mnemonic == "erleng" )
		return 24;
	if( mnemonic == "eatanxy" || mnemonic == "eatanxz" || mnemonic == "eatan" )
		return 54;
	if( mnemonic == "esum" || mnemonic == "ercpr" )
		return 12;
	if( mnemonic == "esin" )
		return 29;
	if( mnemonic == "eexp" )
		return 44;

	if( mnemonic == "lq" || mnemonic == "lqd" || mnemonic == "lqi"
	    || mnemonic == "sq" || mnemonic == "sqd" || mnemonic == "sqi"
	    || mnemonic == "ilw" || mnemonic == "isw"
	    || mnemonic == "ilwr" || mnemonic == "iswr"
	    || mnemonic == "move" || mnemonic == "mfir" || mnemonic == "mr32"
	    || mnemonic == "rget" || mnemonic == "rnext"
	    || mnemonic == "fsset" || mnemonic == "fcset" )
		return 4;

	if( isKnownMnemonic( mnemonic ) )
		return 1;

	return 0;
}

bool VsmCostAnalyzer::isKnownMnemonic( const std::string& mnemonic )
{
	return classifyMnemonic( mnemonic ) != UNIT_UNKNOWN;
}

bool VsmCostAnalyzer::startsInstructionToken( const std::string& line, std::string::size_type pos )
{
	if( pos >= line.size() || !isWordChar( line[pos] ) )
		return false;
	if( pos > 0 && isWordChar( line[pos - 1] ) )
		return false;
	return true;
}

bool VsmCostAnalyzer::isDirective( const std::string& line )
{
	if( line.empty() )
		return false;
	return line[0] == '.';
}

bool VsmCostAnalyzer::isLabelOnly( const std::string& line )
{
	if( line.empty() || line[line.size() - 1] != ':' )
		return false;
	return line.find( ' ' ) == std::string::npos
	    && line.find( '\t' ) == std::string::npos;
}

std::string VsmCostAnalyzer::jsonEscape( const std::string& text )
{
	std::string result;
	for( std::string::const_iterator i = text.begin(); i != text.end(); ++i )
	{
		switch( *i )
		{
			case '\\': result += "\\\\"; break;
			case '"': result += "\\\""; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default: result += *i; break;
		}
	}
	return result;
}

}
