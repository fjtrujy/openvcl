/*
 * VsmCostAnalyzer.cpp
 *
 * Static cost reporting for already-scheduled VU .vsm files.
 */

#include "VsmCostAnalyzer.h"
#include "VuInstructionInfo.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
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

}

VsmCostAnalyzer::Slot::Slot()
{
	present = false;
	nop = false;
	eBit = false;
	dBit = false;
	tBit = false;
	latency = 0;
	throughput = 0;
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
	issueStallCycles = 0;
	fdivIssueStallCycles = 0;
	efuIssueStallCycles = 0;
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
	m_issueStallCycles = 0;
	m_fdivIssueStallCycles = 0;
	m_efuIssueStallCycles = 0;
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
	m_fdivIssueReadyCycle = 0;
	m_efuIssueReadyCycle = 0;
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
		slot.throughput = instructionThroughput( slot.mnemonic );
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
	upper.throughput = instructionThroughput( upper.mnemonic );
	upper.nop = upper.unit == UNIT_NOP;

	lowerSlot.present = true;
	lowerSlot.text = trim( body.substr( positions[1] ) );
	lowerSlot.mnemonic = normalizeMnemonic( firstWord( lowerSlot.text ) );
	lowerSlot.unit = classifyMnemonic( lowerSlot.mnemonic );
	lowerSlot.latency = instructionLatency( lowerSlot.mnemonic );
	lowerSlot.throughput = instructionThroughput( lowerSlot.mnemonic );
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
	const unsigned int scheduledCycle = m_estimatedCycles;
	unsigned int fdivIssueStall = 0;
	unsigned int efuIssueStall = 0;
	unsigned int waitqStall = 0;
	unsigned int waitpStall = 0;
	const bool lowerWritesQ = writesQ( lowerSlot.mnemonic );
	const bool upperWritesQ = writesQ( upper.mnemonic );
	const bool lowerWritesP = writesP( lowerSlot.mnemonic );
	const bool upperWritesP = writesP( upper.mnemonic );

	if( lowerWritesQ || upperWritesQ )
	{
		if( m_fdivIssueReadyCycle > scheduledCycle )
			fdivIssueStall = m_fdivIssueReadyCycle - scheduledCycle;
	}
	if( lowerWritesP || upperWritesP )
	{
		if( m_efuIssueReadyCycle > scheduledCycle )
			efuIssueStall = m_efuIssueReadyCycle - scheduledCycle;
	}
	const unsigned int issueStall = std::max( fdivIssueStall, efuIssueStall );
	const unsigned int issueCycle = scheduledCycle + issueStall;

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
	m_estimatedCycles += 1 + issueStall + waitStall;
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
	m_issueStallCycles += issueStall;
	m_fdivIssueStallCycles += fdivIssueStall;
	m_efuIssueStallCycles += efuIssueStall;
	m_waitqStallCycles += waitqStall;
	m_waitpStallCycles += waitpStall;
	if( lowerSlot.unit == UNIT_FDIV || upper.unit == UNIT_FDIV )
		++m_fdivOps;
	if( lowerSlot.unit == UNIT_EFU || upper.unit == UNIT_EFU )
		++m_efuOps;
	if( lowerWritesQ )
	{
		m_qReadyCycle = issueCycle + lowerSlot.latency + 1;
		m_fdivIssueReadyCycle = issueCycle + lowerSlot.throughput;
	}
	if( upperWritesQ )
	{
		m_qReadyCycle = issueCycle + upper.latency + 1;
		m_fdivIssueReadyCycle = issueCycle + upper.throughput;
	}
	if( lowerWritesP )
	{
		m_pReadyCycle = issueCycle + lowerSlot.latency + 1;
		m_efuIssueReadyCycle = issueCycle + lowerSlot.throughput;
	}
	if( upperWritesP )
	{
		m_pReadyCycle = issueCycle + upper.latency + 1;
		m_efuIssueReadyCycle = issueCycle + upper.throughput;
	}

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
	block.issueStallCycles += issueStall;
	block.fdivIssueStallCycles += fdivIssueStall;
	block.efuIssueStallCycles += efuIssueStall;
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
		block.issueStallCycles = i->issueStallCycles;
		block.weightedIssueStallCycles = i->issueStallCycles * repeat;
		block.waitStallCycles = i->waitqStallCycles + i->waitpStallCycles;
		block.weightedWaitStallCycles = block.waitStallCycles * repeat;
		block.estimatedCycles = i->cycles + block.issueStallCycles + block.waitStallCycles;
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
	unsigned int weightedIssueStallCycles = 0;
	unsigned int weightedFdivIssueStallCycles = 0;
	unsigned int weightedEfuIssueStallCycles = 0;

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
		weightedIssueStallCycles += i->issueStallCycles * repeat;
		weightedFdivIssueStallCycles += i->fdivIssueStallCycles * repeat;
		weightedEfuIssueStallCycles += i->efuIssueStallCycles * repeat;
		weightedWaitqStallCycles += i->waitqStallCycles * repeat;
		weightedWaitpStallCycles += i->waitpStallCycles * repeat;
	}
	weightedWaitStallCycles = weightedWaitqStallCycles + weightedWaitpStallCycles;

	stream << "VSM cost report" << std::endl;
	stream << "input: " << m_inputName << std::endl;
	stream << "static_cycles: " << m_staticCycles << std::endl;
	stream << "estimated_total_cycles: " << m_estimatedCycles << std::endl;
	stream << "issue_stall_cycles: " << m_issueStallCycles << std::endl;
	stream << "fdiv_issue_stall_cycles: " << m_fdivIssueStallCycles << std::endl;
	stream << "efu_issue_stall_cycles: " << m_efuIssueStallCycles << std::endl;
	stream << "wait_stall_cycles: " << (m_waitqStallCycles + m_waitpStallCycles) << std::endl;
	stream << "waitq_stall_cycles: " << m_waitqStallCycles << std::endl;
	stream << "waitp_stall_cycles: " << m_waitpStallCycles << std::endl;
	stream << "weighted_static_cycles: " << weightedStaticCycles << std::endl;
	stream << "weighted_estimated_total_cycles: " << (weightedStaticCycles + weightedIssueStallCycles + weightedWaitStallCycles) << std::endl;
	stream << "weighted_issue_stall_cycles: " << weightedIssueStallCycles << std::endl;
	stream << "weighted_fdiv_issue_stall_cycles: " << weightedFdivIssueStallCycles << std::endl;
	stream << "weighted_efu_issue_stall_cycles: " << weightedEfuIssueStallCycles << std::endl;
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
		       << " issue_stall=" << i->issueStallCycles
		       << " fdiv_issue_stall=" << i->fdivIssueStallCycles
		       << " efu_issue_stall=" << i->efuIssueStallCycles
		       << " wait_stall=" << (i->waitqStallCycles + i->waitpStallCycles)
		       << " waitq_stall=" << i->waitqStallCycles
		       << " waitp_stall=" << i->waitpStallCycles
		       << " estimated_cycles=" << (i->cycles + i->issueStallCycles + i->waitqStallCycles + i->waitpStallCycles)
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
		       << " issue_stall=" << i->issueStallCycles
		       << " weighted_issue_stall=" << i->weightedIssueStallCycles
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
		       << " weighted_issue_stall=" << i->weightedIssueStallCycles
		       << " issue_stall=" << i->issueStallCycles
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
		       << " weighted_issue_stall=" << i->weightedIssueStallCycles
		       << " issue_stall=" << i->issueStallCycles
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
	unsigned int weightedIssueStallCycles = 0;
	unsigned int weightedFdivIssueStallCycles = 0;
	unsigned int weightedEfuIssueStallCycles = 0;

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
		weightedIssueStallCycles += i->issueStallCycles * repeat;
		weightedFdivIssueStallCycles += i->fdivIssueStallCycles * repeat;
		weightedEfuIssueStallCycles += i->efuIssueStallCycles * repeat;
		weightedWaitqStallCycles += i->waitqStallCycles * repeat;
		weightedWaitpStallCycles += i->waitpStallCycles * repeat;
	}
	const unsigned int weightedWaitStallCycles = weightedWaitqStallCycles + weightedWaitpStallCycles;

	stream << "{" << std::endl;
	stream << "  \"input\": \"" << jsonEscape( m_inputName ) << "\"," << std::endl;
	stream << "  \"static_cycles\": " << m_staticCycles << "," << std::endl;
	stream << "  \"estimated_total_cycles\": " << m_estimatedCycles << "," << std::endl;
	stream << "  \"issue_stall_cycles\": " << m_issueStallCycles << "," << std::endl;
	stream << "  \"fdiv_issue_stall_cycles\": " << m_fdivIssueStallCycles << "," << std::endl;
	stream << "  \"efu_issue_stall_cycles\": " << m_efuIssueStallCycles << "," << std::endl;
	stream << "  \"wait_stall_cycles\": " << (m_waitqStallCycles + m_waitpStallCycles) << "," << std::endl;
	stream << "  \"waitq_stall_cycles\": " << m_waitqStallCycles << "," << std::endl;
	stream << "  \"waitp_stall_cycles\": " << m_waitpStallCycles << "," << std::endl;
	stream << "  \"weighted_static_cycles\": " << weightedStaticCycles << "," << std::endl;
	stream << "  \"weighted_estimated_total_cycles\": " << (weightedStaticCycles + weightedIssueStallCycles + weightedWaitStallCycles) << "," << std::endl;
	stream << "  \"weighted_issue_stall_cycles\": " << weightedIssueStallCycles << "," << std::endl;
	stream << "  \"weighted_fdiv_issue_stall_cycles\": " << weightedFdivIssueStallCycles << "," << std::endl;
	stream << "  \"weighted_efu_issue_stall_cycles\": " << weightedEfuIssueStallCycles << "," << std::endl;
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
		       << "\"issue_stall_cycles\": " << i->issueStallCycles << ", "
		       << "\"fdiv_issue_stall_cycles\": " << i->fdivIssueStallCycles << ", "
		       << "\"efu_issue_stall_cycles\": " << i->efuIssueStallCycles << ", "
		       << "\"wait_stall_cycles\": " << (i->waitqStallCycles + i->waitpStallCycles) << ", "
		       << "\"waitq_stall_cycles\": " << i->waitqStallCycles << ", "
		       << "\"waitp_stall_cycles\": " << i->waitpStallCycles << ", "
		       << "\"estimated_cycles\": " << (i->cycles + i->issueStallCycles + i->waitqStallCycles + i->waitpStallCycles) << ", "
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
		       << "\"issue_stall_cycles\": " << i->issueStallCycles << ", "
		       << "\"weighted_issue_stall_cycles\": " << i->weightedIssueStallCycles << ", "
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
		       << "\"weighted_issue_stall_cycles\": " << i->weightedIssueStallCycles << ", "
		       << "\"issue_stall_cycles\": " << i->issueStallCycles << ", "
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
		       << "\"weighted_issue_stall_cycles\": " << i->weightedIssueStallCycles << ", "
		       << "\"issue_stall_cycles\": " << i->issueStallCycles << ", "
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
	return normalizeVuMnemonic( word );
}

VsmCostAnalyzer::Unit VsmCostAnalyzer::classifyMnemonic( const std::string& mnemonic )
{
	const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
	if( !info )
		return UNIT_UNKNOWN;
	if( info->pipe == VU_PIPE_NOP )
		return UNIT_NOP;
	if( info->flags & VU_INSTR_WAIT_Q )
		return UNIT_WAITQ;
	if( info->flags & VU_INSTR_WAIT_P )
		return UNIT_WAITP;
	if( info->flags & VU_INSTR_BRANCH )
		return UNIT_BRANCH;
	if( info->unit == VU_EXEC_FDIV )
		return UNIT_FDIV;
	if( info->unit == VU_EXEC_EFU )
		return UNIT_EFU;
	if( info->pipe == VU_PIPE_UPPER )
		return UNIT_UPPER;
	if( info->pipe == VU_PIPE_LOWER )
		return UNIT_LOWER;
	return UNIT_UNKNOWN;
}

unsigned int VsmCostAnalyzer::instructionLatency( const std::string& mnemonic )
{
	const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
	return info ? info->latency : 0;
}

unsigned int VsmCostAnalyzer::instructionThroughput( const std::string& mnemonic )
{
	const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
	return info ? info->throughput : 0;
}

bool VsmCostAnalyzer::writesQ( const std::string& mnemonic )
{
	const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
	return info && (info->flags & VU_INSTR_WRITES_Q);
}

bool VsmCostAnalyzer::writesP( const std::string& mnemonic )
{
	const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
	return info && (info->flags & VU_INSTR_WRITES_P);
}

bool VsmCostAnalyzer::isKnownMnemonic( const std::string& mnemonic )
{
	return isKnownVuInstruction( mnemonic );
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
