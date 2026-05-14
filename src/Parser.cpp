/*
 * Parser.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Parser.h"
#include "Error.h"
#include "OpenVclVersion.h"
#include "VsmCostAnalyzer.h"
#include "VuInstructionInfo.h"
#include "VuSchedulerAnalysis.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <fcntl.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

namespace
{
	std::string trimManifestText( const std::string& text )
	{
		std::string::size_type first = 0;
		while( first < text.size() && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r' || text[first] == '\n') )
			++first;

		std::string::size_type last = text.size();
		while( last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r' || text[last - 1] == '\n') )
			--last;
		return text.substr( first, last - first );
	}

	std::string stripManifestComment( const std::string& line )
	{
		std::string::size_type pos = line.find( '#' );
		if( pos == std::string::npos )
			return line;
		return line.substr( 0, pos );
	}

	bool costCompareMetricValue( const VsmCostAnalyzer::Summary& summary,
	                             const std::string& metric,
	                             unsigned int& value )
	{
		if( metric == "static" || metric == "static_cycles" )
		{
			value = summary.staticCycles;
			return true;
		}
		if( metric == "estimated" || metric == "estimated_total_cycles" )
		{
			value = summary.estimatedTotalCycles;
			return true;
		}
		if( metric == "weighted-static" || metric == "weighted_static_cycles" )
		{
			value = summary.weightedStaticCycles;
			return true;
		}
		if( metric == "weighted-estimated" || metric == "weighted_estimated_total_cycles" )
		{
			value = summary.weightedEstimatedTotalCycles;
			return true;
		}
		if( metric == "affine-static-base" || metric == "affine_static_base_cycles" )
		{
			value = summary.affineStaticBaseCycles;
			return true;
		}
		if( metric == "affine-static-loop" || metric == "affine_static_loop_cycles" )
		{
			value = summary.affineStaticLoopCycles;
			return true;
		}
		if( metric == "affine-estimated-base" || metric == "affine_estimated_base_cycles" )
		{
			value = summary.affineEstimatedBaseCycles;
			return true;
		}
		if( metric == "affine-estimated-loop" || metric == "affine_estimated_loop_cycles" )
		{
			value = summary.affineEstimatedLoopCycles;
			return true;
		}
		return false;
	}

	struct FlagName
	{
		unsigned int flag;
		const char* name;
	};

	const FlagName kOperandFlags[] =
	{
		{ Operand::UPPER, "upper" },
		{ Operand::LOWER, "lower" },
		{ Operand::DEST, "dest" },
		{ Operand::BROADCAST, "broadcast" },
		{ Operand::XYZ, "xyz" },
		{ Operand::MULTI, "multi" },
		{ Operand::DYNAMIC, "dynamic" },
		{ Operand::IWRITE, "iwrite" },
		{ Operand::FILTERED, "filtered" },
		{ 0, 0 }
	};

	const FlagName kInstructionFlags[] =
	{
		{ VU_INSTR_BRANCH, "branch" },
		{ VU_INSTR_WAIT_Q, "wait_q" },
		{ VU_INSTR_WAIT_P, "wait_p" },
		{ VU_INSTR_WRITES_Q, "writes_q" },
		{ VU_INSTR_WRITES_P, "writes_p" },
		{ VU_INSTR_WRITES_I, "writes_i" },
		{ VU_INSTR_XGKICK, "xgkick" },
		{ VU_INSTR_UNCONDITIONAL_BRANCH, "unconditional_branch" },
		{ VU_INSTR_LINK_BRANCH, "link_branch" },
		{ VU_INSTR_REGISTER_BRANCH, "register_branch" },
		{ 0, 0 }
	};

	const FlagName kResourceFlags[] =
	{
		{ VU_RESOURCE_ACC, "acc" },
		{ VU_RESOURCE_I, "i" },
		{ VU_RESOURCE_Q, "q" },
		{ VU_RESOURCE_P, "p" },
		{ VU_RESOURCE_R, "r" },
		{ VU_RESOURCE_MAC, "mac" },
		{ VU_RESOURCE_CLIP, "clip" },
		{ 0, 0 }
	};

	const FlagName kMemoryFlags[] =
	{
		{ VU_MEMORY_FLAG_PREDEC, "predec" },
		{ VU_MEMORY_FLAG_POSTINC, "postinc" },
		{ 0, 0 }
	};

	const FlagName kBypassFlags[] =
	{
		{ VU_BYPASS_FTOI_TO_MTIR, "ftoi_to_mtir" },
		{ VU_BYPASS_LOAD_TO_FTOI, "load_to_ftoi" },
		{ 0, 0 }
	};

	const char* pipeName( VuPipelineSlot pipe )
	{
		switch( pipe )
		{
			case VU_PIPE_NOP: return "nop";
			case VU_PIPE_UPPER: return "upper";
			case VU_PIPE_LOWER: return "lower";
			default: return "unknown";
		}
	}

	const char* executionUnitName( VuExecutionUnit unit )
	{
		switch( unit )
		{
			case VU_EXEC_NOP: return "nop";
			case VU_EXEC_FMAC: return "fmac";
			case VU_EXEC_FDIV: return "fdiv";
			case VU_EXEC_LSU: return "lsu";
			case VU_EXEC_IALU: return "ialu";
			case VU_EXEC_BRU: return "bru";
			case VU_EXEC_RANDU: return "randu";
			case VU_EXEC_EFU: return "efu";
			default: return "unknown";
		}
	}

	const char* operandUnitName( Operand::Unit unit )
	{
		switch( unit )
		{
			case Operand::INVALID: return "invalid";
			case Operand::ENTER: return "enter";
			case Operand::EXIT: return "exit";
			case Operand::FMAC: return "fmac";
			case Operand::FDIV: return "fdiv";
			case Operand::LSU: return "lsu";
			case Operand::IALU: return "ialu";
			case Operand::BRU: return "bru";
			case Operand::RANDU: return "randu";
			case Operand::EFU: return "efu";
			default: return "unknown";
		}
	}

	const char* memoryKindName( VuMemoryKind kind )
	{
		switch( kind )
		{
			case VU_MEMORY_NONE: return "none";
			case VU_MEMORY_LOAD: return "load";
			case VU_MEMORY_STORE: return "store";
			case VU_MEMORY_XGKICK: return "xgkick";
			default: return "unknown";
		}
	}

	void writeFlagListText( std::ostream& stream, unsigned int flags, const FlagName* names )
	{
		bool wrote = false;
		for( const FlagName* i = names; i->name; ++i )
		{
			if( (flags & i->flag) == i->flag )
			{
				if( wrote )
					stream << "|";
				stream << i->name;
				wrote = true;
			}
		}
		if( !wrote )
			stream << "-";
	}

	void writeJsonString( std::ostream& stream, const char* text )
	{
		stream << "\"";
		if( text )
		{
			for( const char* i = text; *i; ++i )
			{
				switch( *i )
				{
					case '\\': stream << "\\\\"; break;
					case '"': stream << "\\\""; break;
					case '\n': stream << "\\n"; break;
					case '\r': stream << "\\r"; break;
					case '\t': stream << "\\t"; break;
					default: stream << *i; break;
				}
			}
		}
		stream << "\"";
	}

	void writeFlagArrayJson( std::ostream& stream, unsigned int flags, const FlagName* names )
	{
		stream << "[";
		bool wrote = false;
		for( const FlagName* i = names; i->name; ++i )
		{
			if( (flags & i->flag) == i->flag )
			{
				if( wrote )
					stream << ", ";
				writeJsonString( stream, i->name );
				wrote = true;
			}
		}
		stream << "]";
	}

	void writeInstructionInfoText( std::ostream& stream )
	{
		stream << "OpenVCL VU instruction metadata" << std::endl;
		for( const VuInstructionInfo* info = allVuInstructionInfos(); info->mnemonic; ++info )
		{
			const std::string parameters = vuInstructionParameterSummary( *info );
			stream << info->mnemonic
			       << " pipe=" << pipeName( info->pipe )
			       << " unit=" << executionUnitName( info->unit )
			       << " throughput=" << info->throughput
			       << " latency=" << info->latency
			       << " operand=" << info->operandName
			       << " args=" << info->arguments
			       << " parser_unit=" << operandUnitName( info->operandUnit )
			       << " parser_flags=";
			writeFlagListText( stream, info->operandFlags, kOperandFlags );
			stream << " pattern=\"" << info->operandPattern << "\""
			       << " parameters=\"" << parameters << "\""
			       << " description=\"" << vuInstructionDescription( *info ) << "\""
			       << " flags=";
			writeFlagListText( stream, info->flags, kInstructionFlags );
			stream << " reads=";
			writeFlagListText( stream, info->implicitReads, kResourceFlags );
			stream << " writes=";
			writeFlagListText( stream, info->implicitWrites, kResourceFlags );
			stream << " memory=" << memoryKindName( info->memoryKind )
			       << " memory_flags=";
			writeFlagListText( stream, info->memoryFlags, kMemoryFlags );
			stream << " branch_delay=" << info->branchDelaySlots
			       << " bypass=";
			writeFlagListText( stream, info->bypassFlags, kBypassFlags );
			stream << std::endl;
		}
	}

	void writeInstructionInfoJson( std::ostream& stream )
	{
		stream << "{\n  \"instructions\": [\n";
		for( const VuInstructionInfo* info = allVuInstructionInfos(); info->mnemonic; ++info )
		{
			const std::string parameters = vuInstructionParameterSummary( *info );
			if( info != allVuInstructionInfos() )
				stream << ",\n";
			stream << "    {\n";
			stream << "      \"mnemonic\": "; writeJsonString( stream, info->mnemonic ); stream << ",\n";
			stream << "      \"description\": "; writeJsonString( stream, vuInstructionDescription( *info ) ); stream << ",\n";
			stream << "      \"pipe\": "; writeJsonString( stream, pipeName( info->pipe ) ); stream << ",\n";
			stream << "      \"unit\": "; writeJsonString( stream, executionUnitName( info->unit ) ); stream << ",\n";
			stream << "      \"throughput\": " << info->throughput << ",\n";
			stream << "      \"latency\": " << info->latency << ",\n";
			stream << "      \"operand\": {\n";
			stream << "        \"name\": "; writeJsonString( stream, info->operandName ); stream << ",\n";
			stream << "        \"arguments\": " << info->arguments << ",\n";
			stream << "        \"unit\": "; writeJsonString( stream, operandUnitName( info->operandUnit ) ); stream << ",\n";
			stream << "        \"flags\": "; writeFlagArrayJson( stream, info->operandFlags, kOperandFlags ); stream << ",\n";
			stream << "        \"pattern\": "; writeJsonString( stream, info->operandPattern ); stream << ",\n";
			stream << "        \"parameters\": "; writeJsonString( stream, parameters.c_str() ); stream << "\n";
			stream << "      },\n";
			stream << "      \"flags\": "; writeFlagArrayJson( stream, info->flags, kInstructionFlags ); stream << ",\n";
			stream << "      \"resources\": {\n";
			stream << "        \"implicit_reads\": "; writeFlagArrayJson( stream, info->implicitReads, kResourceFlags ); stream << ",\n";
			stream << "        \"implicit_writes\": "; writeFlagArrayJson( stream, info->implicitWrites, kResourceFlags ); stream << "\n";
			stream << "      },\n";
			stream << "      \"memory\": {\n";
			stream << "        \"kind\": "; writeJsonString( stream, memoryKindName( info->memoryKind ) ); stream << ",\n";
			stream << "        \"flags\": "; writeFlagArrayJson( stream, info->memoryFlags, kMemoryFlags ); stream << "\n";
			stream << "      },\n";
			stream << "      \"branch_delay_slots\": " << info->branchDelaySlots << ",\n";
			stream << "      \"bypass\": "; writeFlagArrayJson( stream, info->bypassFlags, kBypassFlags ); stream << "\n";
			stream << "    }";
		}
		stream << "\n  ]\n}" << std::endl;
	}

	void writeStringListText( std::ostream& stream, const std::list<std::string>& values )
	{
		bool wrote = false;
		for( std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i )
		{
			if( wrote )
				stream << "|";
			stream << *i;
			wrote = true;
		}
		if( !wrote )
			stream << "-";
	}

	void writeStringListJson( std::ostream& stream, const std::list<std::string>& values )
	{
		stream << "[";
		bool wrote = false;
		for( std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i )
		{
			if( wrote )
				stream << ", ";
			writeJsonString( stream, i->c_str() );
			wrote = true;
		}
		stream << "]";
	}

	void writeUnsignedVectorJson( std::ostream& stream, const std::vector<unsigned int>& values )
	{
		stream << "[";
		for( unsigned int i = 0; i < values.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << values[i];
		}
		stream << "]";
	}

	void writeLoopPipelineInfoText( std::ostream& stream,
	                                const std::vector<VuLoopPipelineOpportunity>& opportunities )
	{
		stream << "OpenVCL VU loop pipeline opportunities" << std::endl;
		for( std::vector<VuLoopPipelineOpportunity>::const_iterator i = opportunities.begin(); i != opportunities.end(); ++i )
		{
			stream << i->label
			       << " q_producer_token=" << i->qProducerTokenIndex
			       << " first_q_consumer_token=" << i->firstQConsumerTokenIndex
			       << " q_consumers=" << i->qConsumerTokenIndices.size()
			       << " q_latency=" << i->qProducerLatency
			       << " source_prefix_cycles=" << i->sourcePrefixCycles
			       << " source_suffix_cycles=" << i->sourceSuffixCycles
			       << " branch_delay_slots=" << i->branchDelaySlots
			       << " simple_counted_loop=" << (i->simpleCountedLoop ? "yes" : "no")
			       << " single_q_producer=" << (i->hasSingleQProducer ? "yes" : "no")
			       << " requires_prolog_epilog=" << (i->requiresPrologEpilog ? "yes" : "no")
			       << " requires_loop_carried_registers=" << (i->requiresLoopCarriedRegisters ? "yes" : "no")
			       << " memory_loads=" << i->memoryLoadCount
			       << " memory_stores=" << i->memoryStoreCount
			       << " memory_pre_post_increment=" << (i->hasMemoryPreOrPostIncrement ? "yes" : "no")
			       << " xgkick=" << (i->hasXgkick ? "yes" : "no")
			       << " eligible_single_q_pipeline=" << (i->eligibleSingleQSoftwarePipeline ? "yes" : "no")
			       << " induction_registers=";
			writeStringListText( stream, i->inductionRegisters );
			stream << " loop_read_write_registers=";
			writeStringListText( stream, i->loopReadWriteRegisters );
			stream << " carried_q_inputs=";
			writeStringListText( stream, i->carriedQInputRegisters );
			stream << " carried_q_outputs=";
			writeStringListText( stream, i->carriedQOutputRegisters );
			stream << std::endl;
		}
	}

	void writeLoopPipelineInfoJson( std::ostream& stream,
	                                const std::vector<VuLoopPipelineOpportunity>& opportunities )
	{
		stream << "{\n  \"loop_pipeline_opportunities\": [\n";
		for( unsigned int i = 0; i < opportunities.size(); ++i )
		{
			const VuLoopPipelineOpportunity& opportunity = opportunities[i];
			if( i != 0 )
				stream << ",\n";
			stream << "    {\n";
			stream << "      \"label\": "; writeJsonString( stream, opportunity.label.c_str() ); stream << ",\n";
			stream << "      \"branch_token_index\": " << opportunity.branchTokenIndex << ",\n";
			stream << "      \"q_producer_token_index\": " << opportunity.qProducerTokenIndex << ",\n";
			stream << "      \"first_q_consumer_token_index\": " << opportunity.firstQConsumerTokenIndex << ",\n";
			stream << "      \"q_consumer_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.qConsumerTokenIndices ); stream << ",\n";
			stream << "      \"q_producer_latency\": " << opportunity.qProducerLatency << ",\n";
			stream << "      \"source_prefix_cycles\": " << opportunity.sourcePrefixCycles << ",\n";
			stream << "      \"source_suffix_cycles\": " << opportunity.sourceSuffixCycles << ",\n";
			stream << "      \"branch_delay_slots\": " << opportunity.branchDelaySlots << ",\n";
			stream << "      \"simple_counted_loop\": " << (opportunity.simpleCountedLoop ? "true" : "false") << ",\n";
			stream << "      \"single_q_producer\": " << (opportunity.hasSingleQProducer ? "true" : "false") << ",\n";
			stream << "      \"requires_prolog_epilog\": " << (opportunity.requiresPrologEpilog ? "true" : "false") << ",\n";
			stream << "      \"requires_loop_carried_registers\": " << (opportunity.requiresLoopCarriedRegisters ? "true" : "false") << ",\n";
			stream << "      \"memory_loads\": " << opportunity.memoryLoadCount << ",\n";
			stream << "      \"memory_stores\": " << opportunity.memoryStoreCount << ",\n";
			stream << "      \"memory_pre_post_increment\": " << (opportunity.hasMemoryPreOrPostIncrement ? "true" : "false") << ",\n";
			stream << "      \"xgkick\": " << (opportunity.hasXgkick ? "true" : "false") << ",\n";
			stream << "      \"eligible_single_q_pipeline\": " << (opportunity.eligibleSingleQSoftwarePipeline ? "true" : "false") << ",\n";
			stream << "      \"induction_registers\": "; writeStringListJson( stream, opportunity.inductionRegisters ); stream << ",\n";
			stream << "      \"loop_read_write_registers\": "; writeStringListJson( stream, opportunity.loopReadWriteRegisters ); stream << ",\n";
			stream << "      \"carried_q_input_registers\": "; writeStringListJson( stream, opportunity.carriedQInputRegisters ); stream << ",\n";
			stream << "      \"carried_q_output_registers\": "; writeStringListJson( stream, opportunity.carriedQOutputRegisters ); stream << "\n";
			stream << "    }";
		}
		stream << "\n  ]\n}" << std::endl;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Parser::Parser()
{
	m_state = INVALID_STATE;
	m_tempCounter = 0;

	m_preParser = DISABLED;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Parser::~Parser()
{
	if( m_cmdLine.deleteTemp() )
	{
		for( std::list< std::string >::const_iterator i = m_tempFiles.begin(); i != m_tempFiles.end(); i++ )
			remove( (*i).c_str() );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::create( int argc, char* argv[] )
{
	if( !m_cmdLine.parse( argc, argv ) )
	{
		Error::Display( Error( "Invalid Arguments" ) );
		return false;
	}

	if( m_cmdLine.showVersion() )
		setState( SHOW_VERSION );
	else if( m_cmdLine.showUsage() )
		setState( SHOW_USAGE );
	else if( m_cmdLine.dumpInstructionInfo() )
		setState( DUMP_INSTRUCTION_INFO );
	else if( m_cmdLine.dumpLoopPipelineInfo() )
		setState( READ_INPUT );
	else if( m_cmdLine.compareVsmCostListMarkdown() || m_cmdLine.compareVsmCostListCheck() )
		setState( ANALYZE_VSM_COST_COMPARE_LIST );
	else if( m_cmdLine.compareVsmCost() )
		setState( ANALYZE_VSM_COST_COMPARE );
	else if( m_cmdLine.analyzeVsmCost() )
		setState( ANALYZE_VSM_COST );
	else
		setState( READ_INPUT );

	setupOperands();

	// set original input-file
	m_inputFile = m_cmdLine.input();
	m_sourceFile = m_cmdLine.input();

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::begin()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::run()
{
	switch( m_state )
	{
		case SHOW_VERSION: return showVersion();
		case SHOW_USAGE: return showUsage();
		case DUMP_INSTRUCTION_INFO: return dumpInstructionInfo();
		case DUMP_LOOP_PIPELINE_INFO: return dumpLoopPipelineInfo();
		case ANALYZE_VSM_COST: return analyzeVsmCost();
		case ANALYZE_VSM_COST_COMPARE: return analyzeVsmCostCompare();
		case ANALYZE_VSM_COST_COMPARE_LIST: return analyzeVsmCostCompareList();
		case READ_INPUT: return readInput();
		case PREPROCESS: return preProcess();
		case TOKENIZE: return tokenize();
		case ALLOCATE_REGISTERS: return allocateRegisters();
		case GENERATE_CODE : return generateCode();
		case WRITE_OUTPUT: return writeOutput();
		default: return false;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::end()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Parser::setupOperands()
{
	// setup command templates

	// tokenizer operands

	m_operands.push_back( Operand( ".syntax", 			1, Operand::PREPROCESSOR, "'old'|'new'" ) );

	m_operands.push_back( Operand( ".vu",						0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".vsm",					0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".raw",					0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".endvsm",				0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".endraw",				0, Operand::PREPROCESSOR, "" ) );

	m_operands.push_back( Operand( ".rawloop",			0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".endrawloop",		0, Operand::PREPROCESSOR, "" ) );

	// VCL operands

	m_operands.push_back( Operand( ".init_vf",			1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vf:noalias:range,..." ) );
	m_operands.push_back( Operand( ".init_vi",			1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vi:noalias:range,..." ) );
	m_operands.push_back( Operand( ".rem_vf",				1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vf:noalias:range,..." ) );
	m_operands.push_back( Operand( ".rem_vi",				1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vi:noalias:range,..." ) );
	m_operands.push_back( Operand( ".init_vf_all",	0, Operand::PREPROCESSOR|Operand::FILTERED,									"" ) );
	m_operands.push_back( Operand( ".init_vi_all",	0, Operand::PREPROCESSOR|Operand::FILTERED,									"" ) );

	m_operands.push_back( Operand( "--LoopCS",			2, Operand::PREPROCESSOR|Operand::FILTERED,	"imm:integer,imm:integer" ) );
	m_operands.push_back( Operand( "--LoopExtra",		1, Operand::PREPROCESSOR|Operand::FILTERED, "imm:integer" ) );
	m_operands.push_back( Operand( "--LoopAbs",			1, Operand::PREPROCESSOR|Operand::FILTERED, "imm:integer" ) );
	m_operands.push_back( Operand( "--barrier",			0, Operand::PREPROCESSOR|Operand::FILTERED, "" ) );
	m_operands.push_back( Operand( "--cont",				0, Operand::PREPROCESSOR|Operand::FILTERED, "" ) );
	m_operands.push_back( Operand( "--enter",				0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::ENTER ) );
	m_operands.push_back( Operand( "--endenter",		0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::ENTER ) );
	m_operands.push_back( Operand( "--exit",				0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::EXIT ) );
	m_operands.push_back( Operand( "--exitm",				1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED, "imm",	Operand::EXIT ) );
	m_operands.push_back( Operand( "--endexit",			0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::EXIT ) );

	m_operands.push_back( Operand( "in_vi",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vi):noalias",		Operand::ENTER ) );
	m_operands.push_back( Operand( "in_vf",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vf):noalias",		Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_acc",			1, Operand::PREPROCESSOR|Operand::FILTERED, "acc",								Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_clip",		1, Operand::PREPROCESSOR|Operand::FILTERED, "'clip'",							Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_i",				1, Operand::PREPROCESSOR|Operand::FILTERED, "i",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_p",				1, Operand::PREPROCESSOR|Operand::FILTERED, "p",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_q",				1, Operand::PREPROCESSOR|Operand::FILTERED, "q",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_r",				1, Operand::PREPROCESSOR|Operand::FILTERED, "r",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_status",	1, Operand::PREPROCESSOR|Operand::FILTERED, "'status'",						Operand::ENTER ) );

	m_operands.push_back( Operand( "out_vi",				1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vi):noalias",		Operand::EXIT ) );
	m_operands.push_back( Operand( "out_vf",				1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vf):noalias",		Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_acc",		1, Operand::PREPROCESSOR|Operand::FILTERED, "acc",								Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_clip",		1, Operand::PREPROCESSOR|Operand::FILTERED, "'clip'",							Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_i",			1, Operand::PREPROCESSOR|Operand::FILTERED, "i",									Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_p",			1, Operand::PREPROCESSOR|Operand::FILTERED, "p",									Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_q",			1, Operand::PREPROCESSOR|Operand::FILTERED, "q",									Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_r",			1, Operand::PREPROCESSOR|Operand::FILTERED, "r",									Operand::EXIT ) );

	m_operands.push_back( Operand( ".mpg",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm" ) );
	m_operands.push_back( Operand( ".name",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm" ) );

	m_operands.push_back( Operand( ".global",				1, Operand::PREPROCESSOR, "imm" ) );

	// VU hardware instructions

	for( const VuInstructionInfo* info = allVuInstructionInfos(); info->mnemonic; ++info )
	{
		m_operands.push_back( Operand( info->operandName, info->arguments, info->operandFlags, info->operandPattern, info->operandUnit, info->throughput, info->latency ) );
	}

	// operand simplifications

	Operand* add = getOperand( "ADD", Operand::UPPER|Operand::DEST );
	add->addAlternative( add );
	add->addAlternative( getOperand( "ADDA",	Operand::UPPER|Operand::BROADCAST ) );
	add->addAlternative( getOperand( "ADD",		Operand::UPPER|Operand::BROADCAST ) );
	add->addAlternative( getOperand( "ADDi",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDq",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDA",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDAi",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* adda = getOperand( "ADDA", Operand::UPPER|Operand::DEST );
	adda->addAlternative( adda );
	adda->addAlternative( getOperand( "ADDA",	Operand::UPPER|Operand::BROADCAST ) );
	adda->addAlternative( getOperand( "ADDAi",	Operand::UPPER|Operand::DEST ) );
	adda->addAlternative( getOperand( "ADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* sub = getOperand( "SUB", Operand::UPPER|Operand::DEST );
	sub->addAlternative( sub );
	sub->addAlternative( getOperand( "SUBA",	Operand::UPPER|Operand::BROADCAST ) );
	sub->addAlternative( getOperand( "SUB",		Operand::UPPER|Operand::BROADCAST ) );
	sub->addAlternative( getOperand( "SUBi",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBq",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBA",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBAi",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* suba = getOperand( "SUBA", Operand::UPPER|Operand::DEST );
	suba->addAlternative( suba );
	suba->addAlternative( getOperand( "SUBA",	Operand::UPPER|Operand::BROADCAST ) );
	suba->addAlternative( getOperand( "SUBAi",	Operand::UPPER|Operand::DEST ) );
	suba->addAlternative( getOperand( "SUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* mul = getOperand( "MUL", Operand::UPPER|Operand::DEST );
	mul->addAlternative( mul );
	mul->addAlternative( getOperand( "MULA",	Operand::UPPER|Operand::BROADCAST ) );
	mul->addAlternative( getOperand( "MUL",		Operand::UPPER|Operand::BROADCAST ) );
	mul->addAlternative( getOperand( "MULi",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULq",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULA",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULAi",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULAq",	Operand::UPPER|Operand::DEST ) );

	Operand* mula = getOperand( "MULA", Operand::UPPER|Operand::DEST );
	mula->addAlternative( mula );
	mula->addAlternative( getOperand( "MULA",	Operand::UPPER|Operand::BROADCAST ) );
	mula->addAlternative( getOperand( "MULAi",	Operand::UPPER|Operand::DEST ) );
	mula->addAlternative( getOperand( "MULAq",	Operand::UPPER|Operand::DEST ) );

	Operand* madd = getOperand( "MADD", Operand::UPPER|Operand::DEST );
	madd->addAlternative( madd );
	madd->addAlternative( getOperand( "MADDA",	Operand::UPPER|Operand::BROADCAST ) );
	madd->addAlternative( getOperand( "MADD",		Operand::UPPER|Operand::BROADCAST ) );
	madd->addAlternative( getOperand( "MADDi",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDq",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDA",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDAi",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* madda = getOperand( "MADDA", Operand::UPPER|Operand::DEST );
	madda->addAlternative( madda );
	madda->addAlternative( getOperand( "MADDA",	Operand::UPPER|Operand::BROADCAST ) );
	madda->addAlternative( getOperand( "MADDAi",	Operand::UPPER|Operand::DEST ) );
	madda->addAlternative( getOperand( "MADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* msub = getOperand( "MSUB", Operand::UPPER|Operand::DEST );
	msub->addAlternative( msub );
	msub->addAlternative( getOperand( "MSUBA",	Operand::UPPER|Operand::BROADCAST ) );
	msub->addAlternative( getOperand( "MSUB",		Operand::UPPER|Operand::BROADCAST ) );
	msub->addAlternative( getOperand( "MSUBi",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBq",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBA",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBAi",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* msuba = getOperand( "MSUBA", Operand::UPPER|Operand::DEST );
	msuba->addAlternative( msuba );
	msuba->addAlternative( getOperand( "MSUBA",	Operand::UPPER|Operand::BROADCAST ) );
	msuba->addAlternative( getOperand( "MSUBAi",	Operand::UPPER|Operand::DEST ) );
	msuba->addAlternative( getOperand( "MSUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* max = getOperand( "MAX", Operand::UPPER|Operand::DEST );
	max->addAlternative( max );
	max->addAlternative( getOperand( "MAX", 		Operand::UPPER|Operand::BROADCAST ) );
	max->addAlternative( getOperand( "MAXi", 		Operand::UPPER|Operand::DEST ) );

	Operand* mini = getOperand( "MINI", Operand::UPPER|Operand::DEST );
	mini->addAlternative( mini );
	mini->addAlternative( getOperand( "MINI", 	Operand::UPPER|Operand::BROADCAST ) );
	mini->addAlternative( getOperand( "MINIi",	Operand::UPPER|Operand::DEST ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Parser::setState( State state )
{
	m_state = state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::readInput()
{
	if( m_cmdLine.input().length() > 0 )
	{
		std::ifstream stream( m_inputFile.c_str() );

		if( !stream.good() )
		{
			Error::Display( Error( "Could not open input" ) );
			return false;
		}

		m_files[ m_sourceFile.c_str() ] = ( File( m_cmdLine.input().c_str() ) );

		return readInputStream( stream );
	}
	else
	{
		m_files[ "" ] = File("stdin");
		return readInputStream( std::cin );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::analyzeVsmCost()
{
	VsmCostAnalyzer analyzer;

	if( m_cmdLine.input().length() > 0 )
	{
		std::ifstream input( m_cmdLine.input().c_str() );
		if( !input.good() )
		{
			Error::Display( Error( "Could not open input" ) );
			return false;
		}

		if( !analyzer.analyze( input, m_cmdLine.input() ) )
			return false;
	}
	else
	{
		if( !analyzer.analyze( std::cin, "stdin" ) )
			return false;
	}

	const std::vector< std::pair<std::string, unsigned int> >& costLoops = m_cmdLine.costLoops();
	for( std::vector< std::pair<std::string, unsigned int> >::const_iterator i = costLoops.begin(); i != costLoops.end(); ++i )
		analyzer.setBlockRepeat( i->first, i->second );

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.analyzeVsmCostJson() )
			analyzer.writeJson( output );
		else
			analyzer.writeText( output );
	}
	else
	{
		if( m_cmdLine.analyzeVsmCostJson() )
			analyzer.writeJson( std::cout );
		else
			analyzer.writeText( std::cout );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::analyzeVsmCostCompare()
{
	VsmCostAnalyzer baselineAnalyzer;
	VsmCostAnalyzer candidateAnalyzer;

	std::ifstream baselineInput( m_cmdLine.costCompareBaseline().c_str() );
	if( !baselineInput.good() )
	{
		Error::Display( Error( "Could not open cost comparison baseline" ) );
		return false;
	}

	if( !baselineAnalyzer.analyze( baselineInput, m_cmdLine.costCompareBaseline() ) )
		return false;

	if( m_cmdLine.input().length() > 0 )
	{
		std::ifstream candidateInput( m_cmdLine.input().c_str() );
		if( !candidateInput.good() )
		{
			Error::Display( Error( "Could not open input" ) );
			return false;
		}

		if( !candidateAnalyzer.analyze( candidateInput, m_cmdLine.input() ) )
			return false;
	}
	else
	{
		if( !candidateAnalyzer.analyze( std::cin, "stdin" ) )
			return false;
	}

	const std::vector< std::pair<std::string, unsigned int> >& costLoops = m_cmdLine.costLoops();
	for( std::vector< std::pair<std::string, unsigned int> >::const_iterator i = costLoops.begin(); i != costLoops.end(); ++i )
	{
		baselineAnalyzer.setBlockRepeat( i->first, i->second );
		candidateAnalyzer.setBlockRepeat( i->first, i->second );
	}

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.compareVsmCostJson() )
			VsmCostAnalyzer::writeComparisonJson( output, baselineAnalyzer, candidateAnalyzer );
		else if( m_cmdLine.compareVsmCostMarkdown() )
			VsmCostAnalyzer::writeComparisonMarkdown( output, baselineAnalyzer, candidateAnalyzer );
		else
			VsmCostAnalyzer::writeComparisonText( output, baselineAnalyzer, candidateAnalyzer );
	}
	else
	{
		if( m_cmdLine.compareVsmCostJson() )
			VsmCostAnalyzer::writeComparisonJson( std::cout, baselineAnalyzer, candidateAnalyzer );
		else if( m_cmdLine.compareVsmCostMarkdown() )
			VsmCostAnalyzer::writeComparisonMarkdown( std::cout, baselineAnalyzer, candidateAnalyzer );
		else
			VsmCostAnalyzer::writeComparisonText( std::cout, baselineAnalyzer, candidateAnalyzer );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::analyzeVsmCostCompareList()
{
	const bool writeMarkdown = m_cmdLine.compareVsmCostListMarkdown();
	const bool checkMetric = m_cmdLine.compareVsmCostListCheck();
	unsigned int ignoredMetricValue = 0;
	if( checkMetric
	    && !costCompareMetricValue( VsmCostAnalyzer::Summary(),
	                                m_cmdLine.costCompareListCheckMetric(),
	                                ignoredMetricValue ) )
	{
		Error::Display( Error( "Unknown cost comparison check metric" ) );
		return false;
	}

	std::ifstream manifestFile;
	std::istream* manifest = &std::cin;
	if( m_cmdLine.input().length() > 0 )
	{
		manifestFile.open( m_cmdLine.input().c_str() );
		if( !manifestFile.good() )
		{
			Error::Display( Error( "Could not open cost comparison list" ) );
			return false;
		}
		manifest = &manifestFile;
	}

	std::ofstream outputFile;
	std::ostream* output = &std::cout;
	if( m_cmdLine.output().length() > 0 )
	{
		outputFile.open( m_cmdLine.output().c_str() );
		if( !outputFile.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}
		output = &outputFile;
	}

	if( writeMarkdown )
		VsmCostAnalyzer::writeComparisonMarkdownHeader( *output );

	std::string line;
	unsigned int lineNumber = 0;
	unsigned int failedPairs = 0;
	while( std::getline( *manifest, line ) )
	{
		++lineNumber;
		const std::string stripped = trimManifestText( stripManifestComment( line ) );
		if( stripped.empty() )
			continue;

		std::stringstream fields( stripped );
		std::string baselinePath;
		std::string candidatePath;
		std::string extra;
		if( !(fields >> baselinePath >> candidatePath) || (fields >> extra) )
		{
			std::stringstream message;
			message << "Invalid cost comparison list entry at line " << lineNumber;
			Error::Display( Error( message.str() ) );
			return false;
		}

		VsmCostAnalyzer baselineAnalyzer;
		VsmCostAnalyzer candidateAnalyzer;

		std::ifstream baselineInput( baselinePath.c_str() );
		if( !baselineInput.good() )
		{
			Error::Display( Error( "Could not open cost comparison baseline" ) );
			return false;
		}
		if( !baselineAnalyzer.analyze( baselineInput, baselinePath ) )
			return false;

		std::ifstream candidateInput( candidatePath.c_str() );
		if( !candidateInput.good() )
		{
			Error::Display( Error( "Could not open cost comparison candidate" ) );
			return false;
		}
		if( !candidateAnalyzer.analyze( candidateInput, candidatePath ) )
			return false;

		const std::vector< std::pair<std::string, unsigned int> >& costLoops = m_cmdLine.costLoops();
		for( std::vector< std::pair<std::string, unsigned int> >::const_iterator i = costLoops.begin(); i != costLoops.end(); ++i )
		{
			baselineAnalyzer.setBlockRepeat( i->first, i->second );
			candidateAnalyzer.setBlockRepeat( i->first, i->second );
		}

		if( writeMarkdown )
			VsmCostAnalyzer::writeComparisonMarkdownRow( *output, baselineAnalyzer, candidateAnalyzer );

		if( checkMetric )
		{
			unsigned int baselineValue = 0;
			unsigned int candidateValue = 0;
			costCompareMetricValue( baselineAnalyzer.summary(), m_cmdLine.costCompareListCheckMetric(), baselineValue );
			costCompareMetricValue( candidateAnalyzer.summary(), m_cmdLine.costCompareListCheckMetric(), candidateValue );
			if( candidateValue > baselineValue )
			{
				++failedPairs;
				std::cerr << "Cost check failed for " << candidatePath
				          << " against " << baselinePath
				          << " metric=" << m_cmdLine.costCompareListCheckMetric()
				          << " baseline=" << baselineValue
				          << " candidate=" << candidateValue
				          << std::endl;
			}
		}
	}

	if( failedPairs > 0 )
	{
		std::stringstream message;
		message << "Cost comparison list check failed for " << failedPairs << " shader pair(s)";
		Error::Display( Error( message.str() ) );
		return false;
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::dumpInstructionInfo()
{
	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.dumpInstructionInfoJson() )
			writeInstructionInfoJson( output );
		else
			writeInstructionInfoText( output );
	}
	else
	{
		if( m_cmdLine.dumpInstructionInfoJson() )
			writeInstructionInfoJson( std::cout );
		else
			writeInstructionInfoText( std::cout );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::dumpLoopPipelineInfo()
{
	const std::vector<VuLoopPipelineOpportunity> opportunities = findVuLoopPipelineOpportunities( m_tokenizer.tokens() );

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.dumpLoopPipelineInfoJson() )
			writeLoopPipelineInfoJson( output, opportunities );
		else
			writeLoopPipelineInfoText( output, opportunities );
	}
	else
	{
		if( m_cmdLine.dumpLoopPipelineInfoJson() )
			writeLoopPipelineInfoJson( std::cout, opportunities );
		else
			writeLoopPipelineInfoText( std::cout, opportunities );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::readInputStream( std::istream& stream )
{
	std::string buffer;
	std::string temp;
	unsigned int curr = 1;
	unsigned int original = 1;

	std::map<std::string,File>::iterator currFile = m_files.find( m_sourceFile );
	if( currFile == m_files.end() )
	{
		Error::Display( Error( "INTERNAL ERROR: Could not locate filename in map" ) );
		return false;
	}

	while( stream.good() )
	{
		std::getline( stream, buffer, '\n' );
		bool store = true;

		switch( m_preParser )
		{
			case DISABLED: original = curr; break;

			case GASP:
			{
				if( buffer.length() > 0 )
				{
					if( ';' == buffer[0] )
					{
						bool numbers = false;
						bool valid = true;
						std::string::size_type offset = 0, i;
						for( i = 1; valid && (i < buffer.length()); ++i )
						{
							if( numbers )
							{
								if( (' ' == buffer[i]) )
									break;

								if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
									continue;
							}
							else
							{
								if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
								{
									offset = i;
									numbers = true;
									continue;
								}
							}

							if( ' ' != buffer[i] )
								valid = false;
						}

						if( valid && numbers )
							original = strtoul( buffer.substr( offset, i-offset ).c_str(), NULL, 10 );

						store = false;
					}
				}
			}
			break;

			case CPP:
			{
				if( buffer.length() > 0 )
				{
					if( '#' == buffer[0] )
					{
						int number = 0;
						std::string name;
						bool valid = true;
						bool finished = false;
						std::string::size_type offset = 0, i;
						enum { START, LINE, STRING } mode = START;

						for( i = 1; !finished && valid && (i < buffer.length()); ++i )
						{
							switch( mode )
							{
								case START:
								{
									if( ' ' == buffer[i] )
										break;

									if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
									{
										mode = LINE;
										offset = i;
										break;
									}

									valid = false;
								}
								break;

								case LINE:
								{
									if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
										break;

									if( ' ' == buffer[i] )
									{
										mode = STRING;
										number = strtoul( buffer.substr( offset, i-offset ).c_str(), NULL, 10 );
										break;
									}

									valid = false;
								}
								break;

								case STRING:
								{
									if( ' ' == buffer[i] )
										break;

									if( '"' == buffer[i] )
									{
										offset = buffer.substr(i+1).find_last_of('"');

										if( offset != std::string::npos )
										{
											name = buffer.substr(i+1,offset);
											finished = true;
											break;
										}
									}

									valid = false;
								}
								break;
							}
						}

						if( valid && finished )
						{
							std::map<std::string,File>::iterator newFile = m_files.find( name );
							if( m_files.end() != newFile )
							{
								currFile = newFile;
								original = number-1;
							}
							else
							{
								m_files[ name ] = File(name);
								original = number-1;

								currFile = m_files.find( name );
								if( currFile == m_files.end() )
								{
									Error::Display( Error( "INTERNAL ERROR: Could not locate filename in map" ) );
									return false;
								}
							}
						}

						store = false;
						break;
					}
				}
				original++;
			}
			break;
		}

		if( store )
		{
			m_lines.push_back( Line( currFile->second, curr, original, buffer.substr( 0, buffer.find('\r') ) ) );
			curr++;
		}
	}

	if( m_cmdLine.runGasp() || m_cmdLine.runCpp() )
		setState( PREPROCESS );
	else
		setState( TOKENIZE );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::preProcess()
{
	// create temporary files

	std::string src = tempFilename();
	std::string dest = tempFilename();

	if( !src.length() || !dest.length() )
	{
		Error::Display( Error( "Failed creating unique filename" ) );
		return false;
	}

	m_tempFiles.push_back(src);
	m_tempFiles.push_back(dest);

	// write contents to file

	std::ofstream sf(src.c_str());
	if( !sf.good() )
	{
		Error::Display( Error( "Failed creating temporary file" ) );
		return false;
	}

	for( std::list<Line>::const_iterator line = m_lines.begin(); line != m_lines.end(); line++ )
		sf << (*line).content() << std::endl;

	sf.close();

	// run preprocessing tool

	std::string preprocessor;

	if( m_cmdLine.runCpp() )
	{
		preprocessor = m_cmdLine.cpp() + " -I. -nostdinc -x assembler-with-cpp";

		for( std::list<std::string>::const_iterator i = m_cmdLine.includes().begin(); i != m_cmdLine.includes().end(); i++ )
			preprocessor += " -I\"" + *i + "\"";

		preprocessor += " -o \"" + dest + "\" \"" + src + "\"";

		m_preParser = CPP;
		m_cmdLine.setRunCpp( false );
	}
	else
	{
		preprocessor = m_cmdLine.gasp() + " -p -s -c ';'";

		for( std::list<std::string>::const_iterator i = m_cmdLine.includes().begin(); i != m_cmdLine.includes().end(); i++ )
			preprocessor += " -I\"" + *i + "\"";

		preprocessor += " -o \"" + dest + "\" \"" + src + "\"";

		m_preParser = GASP;
		m_cmdLine.setRunGasp( false );
	}

	if( system( preprocessor.c_str() ) )
	{
		Error::Display( Error( "Preprocessor failed" ) );
		return false;
	}

	// restart input reader

	m_sourceFile = src;
	m_inputFile = dest;

	m_lines.clear();
	m_files.clear();
	setState( READ_INPUT );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::tokenize()
{
	m_tokenizer.setNewSyntax( m_cmdLine.newSyntax() );
	m_tokenizer.setOperands( m_operands );

	for( std::list<Line>::const_iterator i = m_lines.begin(); i != m_lines.end(); i++ )
	{
		if( !m_tokenizer.parse( *i ) )
			return false;
	}

	if( m_cmdLine.dumpLoopPipelineInfo() )
		setState( DUMP_LOOP_PIPELINE_INFO );
	else
		setState( ALLOCATE_REGISTERS );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::allocateRegisters()
{
	m_registerAllocator.setAvailableFloats( m_tokenizer.availableFloats() );
	m_registerAllocator.setAvailableIntegers( m_tokenizer.availableIntegers() );
	m_registerAllocator.setDynamicThreshold( m_cmdLine.threshold() );
	m_registerAllocator.setShowRegisterInfo( m_cmdLine.showRegisterInfo() );

	if( !m_registerAllocator.process( m_tokenizer.tokens() ) )
		return false;

	setState( GENERATE_CODE );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::generateCode()
{
	m_codeGenerator.setEmitSource( m_cmdLine.emitSource() );
	m_codeGenerator.setKnownLoopOptimizations( m_cmdLine.knownLoopOptimizations() );
	m_codeGenerator.setName( m_registerAllocator.name() );

	if( !m_codeGenerator.beginProcess( m_tokenizer.tokens() ) )
		return false;

	setState( WRITE_OUTPUT );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Parser::writeOutput()
{
	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream stream( m_cmdLine.output().c_str() );

		if( !stream.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		return writeOutputStream( stream );
	}
	else
	{
		return writeOutputStream( std::cout );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::writeOutputStream( std::ostream& stream )
{
	m_codeGenerator.write( stream );

	setState( EXIT );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::showVersion()
{
	std::cout << "OpenVCL Version " << OPENVCL_VERSION << std::endl;

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::showUsage()
{
	m_cmdLine.showUsage( std::cout );

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string Parser::tempFilename()
{
#ifdef _WIN32
	char path[MAX_PATH];
	char buffer[MAX_PATH];

	if( GetTempPath( sizeof( path ), path ) )
	{
		if( GetTempFileName( path, "vcl", 0, buffer ) )
		{
			return buffer;
		}
	}
#else
	for( unsigned int count = 0; count < TEMPFILE_ATTEMPTS; count++ )
	{
		std::stringstream buffer;
		
		buffer << "/tmp/vcl" << std::hex << std::setfill('0') << std::setw(8) << ((getpid()+rand()) << 8) + m_tempCounter++;
		std::string temp = buffer.str();

		std::ofstream file(temp.c_str(),std::ifstream::out);

		if(file.good())
		{
			file.close();
			return temp;
		}
	}
#endif
	return "";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Operand* Parser::getOperand( const char* name, unsigned int flags )
{
	for( std::list<Operand>::iterator i = m_operands.begin(); i != m_operands.end(); ++i )
	{
		if( !(*i).name().compare( name ) && ((*i).flags() == flags) )
			return &*i;
	}

	return NULL;
}

}
