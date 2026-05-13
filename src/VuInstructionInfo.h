#ifndef __OPENVCL_VUINSTRUCTIONINFO_H__
#define __OPENVCL_VUINSTRUCTIONINFO_H__

/*
 * VuInstructionInfo.h
 *
 * Shared VU instruction metadata for scheduling and cost analysis.
 */

#include <string>

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
	VU_INSTR_WRITES_I  = 1 << 5
};

struct VuInstructionInfo
{
	const char* mnemonic;
	VuPipelineSlot pipe;
	VuExecutionUnit unit;
	unsigned int throughput;
	unsigned int latency;
	unsigned int flags;
};

const VuInstructionInfo* findVuInstructionInfo( const std::string& mnemonic );
bool isKnownVuInstruction( const std::string& mnemonic );
std::string normalizeVuMnemonic( const std::string& word );

}

#endif
