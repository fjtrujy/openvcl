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
	nopSlots = 0;
	waitqStallCycles = 0;
	waitpStallCycles = 0;
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
	m_blockRepeats.clear();
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
	m_waitqStallCycles = 0;
	m_waitpStallCycles = 0;
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
	m_estimatedCycles = 0;
	m_qReadyCycle = 0;
	m_pReadyCycle = 0;
}

void VsmCostAnalyzer::setBlockRepeat( const std::string& label, unsigned int repeat )
{
	if( label.empty() || repeat == 0 )
		return;
	m_blockRepeats[label] = repeat;
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
	const unsigned int issueCycle = m_estimatedCycles;
	unsigned int waitqStall = 0;
	unsigned int waitpStall = 0;

	if( lowerSlot.unit == UNIT_WAITQ || upper.unit == UNIT_WAITQ )
	{
		const unsigned int resumeCycle = upperActive ? issueCycle : (issueCycle + 1);
		if( m_qReadyCycle > resumeCycle )
			waitqStall = m_qReadyCycle - resumeCycle;
	}
	if( lowerSlot.unit == UNIT_WAITP || upper.unit == UNIT_WAITP )
	{
		const unsigned int resumeCycle = upperActive ? issueCycle : (issueCycle + 1);
		if( m_pReadyCycle > resumeCycle )
			waitpStall = m_pReadyCycle - resumeCycle;
	}
	const unsigned int waitStall = std::max( waitqStall, waitpStall );

	++m_staticCycles;
	m_estimatedCycles += 1 + waitStall;
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
	m_waitqStallCycles += waitqStall;
	m_waitpStallCycles += waitpStall;
	if( lowerSlot.unit == UNIT_FDIV || upper.unit == UNIT_FDIV )
		++m_fdivOps;
	if( lowerSlot.unit == UNIT_EFU || upper.unit == UNIT_EFU )
		++m_efuOps;
	if( lowerSlot.unit == UNIT_FDIV )
		m_qReadyCycle = issueCycle + lowerSlot.latency + 1;
	if( upper.unit == UNIT_FDIV )
		m_qReadyCycle = issueCycle + upper.latency + 1;
	if( lowerSlot.unit == UNIT_EFU )
		m_pReadyCycle = issueCycle + lowerSlot.latency + 1;
	if( upper.unit == UNIT_EFU )
		m_pReadyCycle = issueCycle + upper.latency + 1;

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
	block.nopSlots += upperActive ? 0 : 1;
	block.nopSlots += lowerActive ? 0 : 1;
	block.waitqStallCycles += waitqStall;
	block.waitpStallCycles += waitpStall;
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

unsigned int VsmCostAnalyzer::blockRepeat( const Block& block ) const
{
	std::map<std::string, unsigned int>::const_iterator i = m_blockRepeats.find( block.label );
	if( i == m_blockRepeats.end() )
		return 1;
	return i->second ? i->second : 1;
}

bool VsmCostAnalyzer::weightedBlockGreater( const WeightedBlock& a, const WeightedBlock& b )
{
	if( a.weightedCycles != b.weightedCycles )
		return a.weightedCycles > b.weightedCycles;
	return a.label < b.label;
}

bool VsmCostAnalyzer::weightedIdleBlockGreater( const WeightedBlock& a, const WeightedBlock& b )
{
	if( a.weightedNopSlots != b.weightedNopSlots )
		return a.weightedNopSlots > b.weightedNopSlots;
	if( a.weightedCycles != b.weightedCycles )
		return a.weightedCycles > b.weightedCycles;
	return a.label < b.label;
}

bool VsmCostAnalyzer::weightedEstimatedBlockGreater( const WeightedBlock& a, const WeightedBlock& b )
{
	if( a.weightedEstimatedCycles != b.weightedEstimatedCycles )
		return a.weightedEstimatedCycles > b.weightedEstimatedCycles;
	if( a.weightedCycles != b.weightedCycles )
		return a.weightedCycles > b.weightedCycles;
	return a.label < b.label;
}

bool VsmCostAnalyzer::weightedWaitBlockGreater( const WeightedBlock& a, const WeightedBlock& b )
{
	if( a.weightedWaitStallCycles != b.weightedWaitStallCycles )
		return a.weightedWaitStallCycles > b.weightedWaitStallCycles;
	if( a.weightedEstimatedCycles != b.weightedEstimatedCycles )
		return a.weightedEstimatedCycles > b.weightedEstimatedCycles;
	return a.label < b.label;
}

std::vector<VsmCostAnalyzer::WeightedBlock> VsmCostAnalyzer::weightedBlocksByCycles() const
{
	std::vector<WeightedBlock> weightedBlocks;
	for( std::vector<Block>::const_iterator i = m_blocks.begin(); i != m_blocks.end(); ++i )
	{
		if( i->cycles == 0 )
			continue;
		const unsigned int repeat = blockRepeat(*i);
		WeightedBlock block;
		block.label = i->label;
		block.cycles = i->cycles;
		block.repeat = repeat;
		block.weightedCycles = i->cycles * repeat;
		block.pairedCycles = i->pairedCycles;
		block.nopOnlyCycles = i->nopOnlyCycles;
		block.nopSlots = i->nopSlots;
		block.weightedNopSlots = i->nopSlots * repeat;
		block.weightedNopOnlyCycles = i->nopOnlyCycles * repeat;
		block.waitStallCycles = i->waitqStallCycles + i->waitpStallCycles;
		block.weightedWaitStallCycles = block.waitStallCycles * repeat;
		block.estimatedCycles = i->cycles + block.waitStallCycles;
		block.weightedEstimatedCycles = block.estimatedCycles * repeat;
		weightedBlocks.push_back(block);
	}
	std::sort( weightedBlocks.begin(), weightedBlocks.end(), weightedBlockGreater );
	return weightedBlocks;
}

std::vector<VsmCostAnalyzer::WeightedBlock> VsmCostAnalyzer::weightedBlocksByEstimatedCycles() const
{
	std::vector<WeightedBlock> weightedBlocks = weightedBlocksByCycles();
	std::sort( weightedBlocks.begin(), weightedBlocks.end(), weightedEstimatedBlockGreater );
	return weightedBlocks;
}

std::vector<VsmCostAnalyzer::WeightedBlock> VsmCostAnalyzer::weightedBlocksByIdleSlots() const
{
	std::vector<WeightedBlock> weightedBlocks = weightedBlocksByCycles();
	std::sort( weightedBlocks.begin(), weightedBlocks.end(), weightedIdleBlockGreater );
	return weightedBlocks;
}

std::vector<VsmCostAnalyzer::WeightedBlock> VsmCostAnalyzer::weightedBlocksByWaitStalls() const
{
	std::vector<WeightedBlock> weightedBlocks = weightedBlocksByCycles();
	std::sort( weightedBlocks.begin(), weightedBlocks.end(), weightedWaitBlockGreater );
	return weightedBlocks;
}

bool VsmCostAnalyzer::writeText( std::ostream& stream ) const
{
	const unsigned int instructions = m_upperInstructions + m_lowerInstructions;
	const unsigned int slots = m_staticCycles * 2;
	const unsigned int slotPressureMin = std::max( m_upperInstructions, m_lowerInstructions );
	const unsigned int excessCycles = ( m_staticCycles > slotPressureMin ) ? ( m_staticCycles - slotPressureMin ) : 0;
	unsigned int weightedStaticCycles = 0;
	unsigned int weightedInstructions = 0;
	unsigned int weightedPairedCycles = 0;
	unsigned int weightedNopOnlyCycles = 0;
	unsigned int weightedOperationLatencyCycles = 0;
	unsigned int weightedWaitStallCycles = 0;
	unsigned int weightedWaitqStallCycles = 0;
	unsigned int weightedWaitpStallCycles = 0;

	for( std::vector<Block>::const_iterator i = m_blocks.begin(); i != m_blocks.end(); ++i )
	{
		if( i->cycles == 0 )
			continue;
		const unsigned int repeat = blockRepeat(*i);
		weightedStaticCycles += i->cycles * repeat;
		weightedInstructions += (i->upperInstructions + i->lowerInstructions) * repeat;
		weightedPairedCycles += i->pairedCycles * repeat;
		weightedNopOnlyCycles += i->nopOnlyCycles * repeat;
		weightedOperationLatencyCycles += i->operationLatencyCycles * repeat;
		weightedWaitqStallCycles += i->waitqStallCycles * repeat;
		weightedWaitpStallCycles += i->waitpStallCycles * repeat;
	}
	weightedWaitStallCycles = weightedWaitqStallCycles + weightedWaitpStallCycles;

	stream << "VSM cost report" << std::endl;
	stream << "input: " << m_inputName << std::endl;
	stream << "static_cycles: " << m_staticCycles << std::endl;
	stream << "estimated_total_cycles: " << m_estimatedCycles << std::endl;
	stream << "wait_stall_cycles: " << (m_waitqStallCycles + m_waitpStallCycles) << std::endl;
	stream << "waitq_stall_cycles: " << m_waitqStallCycles << std::endl;
	stream << "waitp_stall_cycles: " << m_waitpStallCycles << std::endl;
	stream << "weighted_static_cycles: " << weightedStaticCycles << std::endl;
	stream << "weighted_estimated_total_cycles: " << (weightedStaticCycles + weightedWaitStallCycles) << std::endl;
	stream << "weighted_wait_stall_cycles: " << weightedWaitStallCycles << std::endl;
	stream << "weighted_waitq_stall_cycles: " << weightedWaitqStallCycles << std::endl;
	stream << "weighted_waitp_stall_cycles: " << weightedWaitpStallCycles << std::endl;
	stream << "weighted_instruction_slots: " << (weightedStaticCycles * 2) << std::endl;
	stream << "weighted_instructions: " << weightedInstructions << std::endl;
	stream << "weighted_paired_cycles: " << weightedPairedCycles << std::endl;
	stream << "weighted_nop_only_cycles: " << weightedNopOnlyCycles << std::endl;
	stream << "weighted_operation_latency_cycles: " << weightedOperationLatencyCycles << std::endl;
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
		const unsigned int repeat = blockRepeat(*i);
		stream << "  " << i->label
		       << ": cycles=" << i->cycles
		       << " repeat=" << repeat
		       << " weighted_cycles=" << (i->cycles * repeat)
		       << " upper=" << i->upperInstructions
		       << " lower=" << i->lowerInstructions
		       << " paired=" << i->pairedCycles
		       << " single_upper=" << i->singleUpperCycles
		       << " single_lower=" << i->singleLowerCycles
		       << " nop_only=" << i->nopOnlyCycles
		       << " nop_slots=" << i->nopSlots
		       << " wait_stall=" << (i->waitqStallCycles + i->waitpStallCycles)
		       << " waitq_stall=" << i->waitqStallCycles
		       << " waitp_stall=" << i->waitpStallCycles
		       << " estimated_cycles=" << (i->cycles + i->waitqStallCycles + i->waitpStallCycles)
		       << " op_latency=" << i->operationLatencyCycles
		       << " long_ops=" << i->longLatencyOps
		       << " long_cycles=" << i->longLatencyCycles
		       << std::endl;
	}

	stream << "top_weighted_blocks:" << std::endl;
	const std::vector<WeightedBlock> weightedBlocks = weightedBlocksByCycles();
	unsigned int topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = weightedBlocks.begin(); i != weightedBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		stream << "  " << i->label
		       << ": weighted_cycles=" << i->weightedCycles
		       << " cycles=" << i->cycles
		       << " repeat=" << i->repeat
		       << " paired=" << i->pairedCycles
		       << " nop_only=" << i->nopOnlyCycles
		       << " weighted_nop_only=" << i->weightedNopOnlyCycles
		       << " nop_slots=" << i->nopSlots
		       << " weighted_nop_slots=" << i->weightedNopSlots
		       << " estimated_cycles=" << i->estimatedCycles
		       << " weighted_estimated_cycles=" << i->weightedEstimatedCycles
		       << " wait_stall=" << i->waitStallCycles
		       << " weighted_wait_stall=" << i->weightedWaitStallCycles
		       << std::endl;
	}

	stream << "top_weighted_estimated_blocks:" << std::endl;
	const std::vector<WeightedBlock> estimatedBlocks = weightedBlocksByEstimatedCycles();
	topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = estimatedBlocks.begin(); i != estimatedBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		stream << "  " << i->label
		       << ": weighted_estimated_cycles=" << i->weightedEstimatedCycles
		       << " estimated_cycles=" << i->estimatedCycles
		       << " repeat=" << i->repeat
		       << " weighted_cycles=" << i->weightedCycles
		       << " cycles=" << i->cycles
		       << " weighted_wait_stall=" << i->weightedWaitStallCycles
		       << " wait_stall=" << i->waitStallCycles
		       << std::endl;
	}

	stream << "top_weighted_idle_blocks:" << std::endl;
	const std::vector<WeightedBlock> idleBlocks = weightedBlocksByIdleSlots();
	topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = idleBlocks.begin(); i != idleBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		stream << "  " << i->label
		       << ": weighted_nop_slots=" << i->weightedNopSlots
		       << " nop_slots=" << i->nopSlots
		       << " repeat=" << i->repeat
		       << " weighted_cycles=" << i->weightedCycles
		       << " cycles=" << i->cycles
		       << " weighted_nop_only=" << i->weightedNopOnlyCycles
		       << " nop_only=" << i->nopOnlyCycles
		       << std::endl;
	}

	stream << "top_weighted_wait_blocks:" << std::endl;
	const std::vector<WeightedBlock> waitBlocks = weightedBlocksByWaitStalls();
	topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = waitBlocks.begin(); i != waitBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		if( i->weightedWaitStallCycles == 0 )
			break;
		stream << "  " << i->label
		       << ": weighted_wait_stall=" << i->weightedWaitStallCycles
		       << " wait_stall=" << i->waitStallCycles
		       << " repeat=" << i->repeat
		       << " weighted_estimated_cycles=" << i->weightedEstimatedCycles
		       << " estimated_cycles=" << i->estimatedCycles
		       << " weighted_cycles=" << i->weightedCycles
		       << " cycles=" << i->cycles
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
	unsigned int weightedStaticCycles = 0;
	unsigned int weightedInstructions = 0;
	unsigned int weightedPairedCycles = 0;
	unsigned int weightedNopOnlyCycles = 0;
	unsigned int weightedOperationLatencyCycles = 0;
	unsigned int weightedWaitqStallCycles = 0;
	unsigned int weightedWaitpStallCycles = 0;

	for( std::vector<Block>::const_iterator i = m_blocks.begin(); i != m_blocks.end(); ++i )
	{
		if( i->cycles == 0 )
			continue;
		const unsigned int repeat = blockRepeat(*i);
		weightedStaticCycles += i->cycles * repeat;
		weightedInstructions += (i->upperInstructions + i->lowerInstructions) * repeat;
		weightedPairedCycles += i->pairedCycles * repeat;
		weightedNopOnlyCycles += i->nopOnlyCycles * repeat;
		weightedOperationLatencyCycles += i->operationLatencyCycles * repeat;
		weightedWaitqStallCycles += i->waitqStallCycles * repeat;
		weightedWaitpStallCycles += i->waitpStallCycles * repeat;
	}
	const unsigned int weightedWaitStallCycles = weightedWaitqStallCycles + weightedWaitpStallCycles;

	stream << "{" << std::endl;
	stream << "  \"input\": \"" << jsonEscape( m_inputName ) << "\"," << std::endl;
	stream << "  \"static_cycles\": " << m_staticCycles << "," << std::endl;
	stream << "  \"estimated_total_cycles\": " << m_estimatedCycles << "," << std::endl;
	stream << "  \"wait_stall_cycles\": " << (m_waitqStallCycles + m_waitpStallCycles) << "," << std::endl;
	stream << "  \"waitq_stall_cycles\": " << m_waitqStallCycles << "," << std::endl;
	stream << "  \"waitp_stall_cycles\": " << m_waitpStallCycles << "," << std::endl;
	stream << "  \"weighted_static_cycles\": " << weightedStaticCycles << "," << std::endl;
	stream << "  \"weighted_estimated_total_cycles\": " << (weightedStaticCycles + weightedWaitStallCycles) << "," << std::endl;
	stream << "  \"weighted_wait_stall_cycles\": " << weightedWaitStallCycles << "," << std::endl;
	stream << "  \"weighted_waitq_stall_cycles\": " << weightedWaitqStallCycles << "," << std::endl;
	stream << "  \"weighted_waitp_stall_cycles\": " << weightedWaitpStallCycles << "," << std::endl;
	stream << "  \"weighted_instruction_slots\": " << (weightedStaticCycles * 2) << "," << std::endl;
	stream << "  \"weighted_instructions\": " << weightedInstructions << "," << std::endl;
	stream << "  \"weighted_paired_cycles\": " << weightedPairedCycles << "," << std::endl;
	stream << "  \"weighted_nop_only_cycles\": " << weightedNopOnlyCycles << "," << std::endl;
	stream << "  \"weighted_operation_latency_cycles\": " << weightedOperationLatencyCycles << "," << std::endl;
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
		const unsigned int repeat = blockRepeat(*i);
		stream << "    {"
		       << "\"label\": \"" << jsonEscape( i->label ) << "\", "
		       << "\"cycles\": " << i->cycles << ", "
		       << "\"repeat\": " << repeat << ", "
		       << "\"weighted_cycles\": " << (i->cycles * repeat) << ", "
		       << "\"upper_instructions\": " << i->upperInstructions << ", "
		       << "\"lower_instructions\": " << i->lowerInstructions << ", "
		       << "\"paired_cycles\": " << i->pairedCycles << ", "
		       << "\"single_upper_cycles\": " << i->singleUpperCycles << ", "
		       << "\"single_lower_cycles\": " << i->singleLowerCycles << ", "
		       << "\"nop_only_cycles\": " << i->nopOnlyCycles << ", "
		       << "\"nop_slots\": " << i->nopSlots << ", "
		       << "\"wait_stall_cycles\": " << (i->waitqStallCycles + i->waitpStallCycles) << ", "
		       << "\"waitq_stall_cycles\": " << i->waitqStallCycles << ", "
		       << "\"waitp_stall_cycles\": " << i->waitpStallCycles << ", "
		       << "\"estimated_cycles\": " << (i->cycles + i->waitqStallCycles + i->waitpStallCycles) << ", "
		       << "\"operation_latency_cycles\": " << i->operationLatencyCycles << ", "
		       << "\"long_latency_ops\": " << i->longLatencyOps << ", "
		       << "\"long_latency_cycles\": " << i->longLatencyCycles
		       << "}";
	}

	stream << std::endl << "  ]," << std::endl;
	stream << "  \"top_weighted_blocks\": [" << std::endl;

	const std::vector<WeightedBlock> weightedBlocks = weightedBlocksByCycles();
	unsigned int topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = weightedBlocks.begin(); i != weightedBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		if( topCount != 0 )
			stream << "," << std::endl;
		stream << "    {"
		       << "\"label\": \"" << jsonEscape( i->label ) << "\", "
		       << "\"weighted_cycles\": " << i->weightedCycles << ", "
		       << "\"cycles\": " << i->cycles << ", "
		       << "\"repeat\": " << i->repeat << ", "
		       << "\"paired_cycles\": " << i->pairedCycles << ", "
		       << "\"nop_only_cycles\": " << i->nopOnlyCycles << ", "
		       << "\"weighted_nop_only_cycles\": " << i->weightedNopOnlyCycles << ", "
		       << "\"nop_slots\": " << i->nopSlots << ", "
		       << "\"weighted_nop_slots\": " << i->weightedNopSlots << ", "
		       << "\"estimated_cycles\": " << i->estimatedCycles << ", "
		       << "\"weighted_estimated_cycles\": " << i->weightedEstimatedCycles << ", "
		       << "\"wait_stall_cycles\": " << i->waitStallCycles << ", "
		       << "\"weighted_wait_stall_cycles\": " << i->weightedWaitStallCycles
		       << "}";
	}

	stream << std::endl << "  ]," << std::endl;
	stream << "  \"top_weighted_estimated_blocks\": [" << std::endl;

	const std::vector<WeightedBlock> estimatedBlocks = weightedBlocksByEstimatedCycles();
	topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = estimatedBlocks.begin(); i != estimatedBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		if( topCount != 0 )
			stream << "," << std::endl;
		stream << "    {"
		       << "\"label\": \"" << jsonEscape( i->label ) << "\", "
		       << "\"weighted_estimated_cycles\": " << i->weightedEstimatedCycles << ", "
		       << "\"estimated_cycles\": " << i->estimatedCycles << ", "
		       << "\"repeat\": " << i->repeat << ", "
		       << "\"weighted_cycles\": " << i->weightedCycles << ", "
		       << "\"cycles\": " << i->cycles << ", "
		       << "\"weighted_wait_stall_cycles\": " << i->weightedWaitStallCycles << ", "
		       << "\"wait_stall_cycles\": " << i->waitStallCycles
		       << "}";
	}

	stream << std::endl << "  ]," << std::endl;
	stream << "  \"top_weighted_idle_blocks\": [" << std::endl;

	const std::vector<WeightedBlock> idleBlocks = weightedBlocksByIdleSlots();
	topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = idleBlocks.begin(); i != idleBlocks.end() && topCount < 8; ++i, ++topCount )
	{
		if( topCount != 0 )
			stream << "," << std::endl;
		stream << "    {"
		       << "\"label\": \"" << jsonEscape( i->label ) << "\", "
		       << "\"weighted_nop_slots\": " << i->weightedNopSlots << ", "
		       << "\"nop_slots\": " << i->nopSlots << ", "
		       << "\"repeat\": " << i->repeat << ", "
		       << "\"weighted_cycles\": " << i->weightedCycles << ", "
		       << "\"cycles\": " << i->cycles << ", "
		       << "\"weighted_nop_only_cycles\": " << i->weightedNopOnlyCycles << ", "
		       << "\"nop_only_cycles\": " << i->nopOnlyCycles
		       << "}";
	}

	stream << std::endl << "  ]," << std::endl;
	stream << "  \"top_weighted_wait_blocks\": [" << std::endl;

	const std::vector<WeightedBlock> waitBlocks = weightedBlocksByWaitStalls();
	topCount = 0;
	for( std::vector<WeightedBlock>::const_iterator i = waitBlocks.begin(); i != waitBlocks.end() && topCount < 8; ++i )
	{
		if( i->weightedWaitStallCycles == 0 )
			break;
		if( topCount != 0 )
			stream << "," << std::endl;
		stream << "    {"
		       << "\"label\": \"" << jsonEscape( i->label ) << "\", "
		       << "\"weighted_wait_stall_cycles\": " << i->weightedWaitStallCycles << ", "
		       << "\"wait_stall_cycles\": " << i->waitStallCycles << ", "
		       << "\"repeat\": " << i->repeat << ", "
		       << "\"weighted_estimated_cycles\": " << i->weightedEstimatedCycles << ", "
		       << "\"estimated_cycles\": " << i->estimatedCycles << ", "
		       << "\"weighted_cycles\": " << i->weightedCycles << ", "
		       << "\"cycles\": " << i->cycles
		       << "}";
		++topCount;
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
