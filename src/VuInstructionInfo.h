#ifndef __OPENVCL_VUINSTRUCTIONINFO_H__
#define __OPENVCL_VUINSTRUCTIONINFO_H__

/*
 * VuInstructionInfo.h
 *
 * Shared VU instruction metadata for scheduling and cost analysis.
 */

#include <string>

#include "Operand.h"

namespace vcl
{

enum VuPipelineSlot
{
	VU_PIPE_UNKNOWN,
	VU_PIPE_NOP,
	VU_PIPE_UPPER,
	VU_PIPE_LOWER
};

enum VuExecutionUnit
{
	VU_EXEC_UNKNOWN,
	VU_EXEC_NOP,
	VU_EXEC_FMAC,
	VU_EXEC_FDIV,
	VU_EXEC_LSU,
	VU_EXEC_IALU,
	VU_EXEC_BRU,
	VU_EXEC_RANDU,
	VU_EXEC_EFU
};

enum VuInstructionFlags
{
	VU_INSTR_NONE      = 0,
	VU_INSTR_BRANCH    = 1 << 0,
	VU_INSTR_WAIT_Q    = 1 << 1,
	VU_INSTR_WAIT_P    = 1 << 2,
	VU_INSTR_WRITES_Q  = 1 << 3,
	VU_INSTR_WRITES_P  = 1 << 4,
	VU_INSTR_WRITES_I  = 1 << 5,
	VU_INSTR_XGKICK    = 1 << 6
};

enum VuResourceFlags
{
	VU_RESOURCE_NONE  = 0,
	VU_RESOURCE_ACC   = 1 << 0,
	VU_RESOURCE_I     = 1 << 1,
	VU_RESOURCE_Q     = 1 << 2,
	VU_RESOURCE_P     = 1 << 3,
	VU_RESOURCE_R     = 1 << 4,
	VU_RESOURCE_MAC   = 1 << 5,
	VU_RESOURCE_CLIP  = 1 << 6
};

enum VuMemoryKind
{
	VU_MEMORY_NONE,
	VU_MEMORY_LOAD,
	VU_MEMORY_STORE,
	VU_MEMORY_XGKICK
};

enum VuMemoryFlags
{
	VU_MEMORY_FLAG_NONE    = 0,
	VU_MEMORY_FLAG_PREDEC  = 1 << 0,
	VU_MEMORY_FLAG_POSTINC = 1 << 1
};

enum VuBypassFlags
{
	VU_BYPASS_NONE         = 0,
	VU_BYPASS_FTOI_TO_MTIR = 1 << 0
};

struct VuInstructionInfo
{
	const char* mnemonic;
	const char* operandName;
	unsigned int arguments;
	unsigned int operandFlags;
	const char* operandPattern;
	Operand::Unit operandUnit;
	VuPipelineSlot pipe;
	VuExecutionUnit unit;
	unsigned int throughput;
	unsigned int latency;
	unsigned int flags;
	unsigned int implicitReads;
	unsigned int implicitWrites;
	VuMemoryKind memoryKind;
	unsigned int memoryFlags;
	unsigned int branchDelaySlots;
	unsigned int bypassFlags;
};

const VuInstructionInfo* allVuInstructionInfos();
const VuInstructionInfo* findVuInstructionInfo( const std::string& mnemonic );
bool isKnownVuInstruction( const std::string& mnemonic );
std::string normalizeVuMnemonic( const std::string& word );

}

#endif
