#include "VuInstructionInfo.h"

#include <cctype>
#include <cstring>

namespace vcl
{

namespace
{
#define VU_INFO(mn, name, args, opFlags, pattern, opUnit, pipe, unit, thr, lat, flags, reads, writes, memKind, memFlags, delay, bypass) \
	{ mn, name, args, opFlags, pattern, opUnit, pipe, unit, thr, lat, flags, reads, writes, memKind, memFlags, delay, bypass }

#define VU_UPPER_DEST(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_BC(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_DEST_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, reads, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_DEST_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_DEST_ACC_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC|reads, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_BC_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_DEST(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_BC(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, reads, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_ACC_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC|reads, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_BC_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

	const VuInstructionInfo kInstructions[] =
	{
		VU_INFO("nop", "NOP", 0, 0, "", Operand::INVALID, VU_PIPE_NOP, VU_EXEC_NOP, 0, 0, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_UPPER_DEST("abs", "ABS", 2, "vf:dest:write,vf:dest"),

		VU_UPPER_DEST("add", "ADD", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS("addi", "ADDi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_READS("addq", "ADDq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC("add", "ADD", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_DEST("adda", "ADDA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_READS("addai", "ADDAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_READS("addaq", "ADDAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC("adda", "ADDA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST("sub", "SUB", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS("subi", "SUBi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_READS("subq", "SUBq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC("sub", "SUB", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_DEST("suba", "SUBA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_READS("subai", "SUBAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_READS("subaq", "SUBAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC("suba", "SUBA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST("mul", "MUL", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS("muli", "MULi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_READS("mulq", "MULq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC("mul", "MUL", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_DEST("mula", "MULA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_READS("mulai", "MULAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_READS("mulaq", "MULAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC("mula", "MULA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST_ACC_READ("madd", "MADD", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_ACC_READS("maddi", "MADDi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_ACC_READS("maddq", "MADDq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC_ACC_READ("madd", "MADD", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_ACC_READ("madda", "MADDA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_ACC_READS("maddai", "MADDAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_ACC_READS("maddaq", "MADDAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC_ACC_READ("madda", "MADDA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST_ACC_READ("msub", "MSUB", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_ACC_READS("msubi", "MSUBi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_ACC_READS("msubq", "MSUBq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC_ACC_READ("msub", "MSUB", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_ACC_READ("msuba", "MSUBA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_ACC_READS("msubai", "MSUBAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_ACC_READS("msubaq", "MSUBAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC_ACC_READ("msuba", "MSUBA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST("max", "MAX", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS("maxi", "MAXi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_BC("max", "MAX", 3, "vf:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST("mini", "MINI", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS("minii", "MINIi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_BC("mini", "MINI", 3, "vf:dest:write,vf:dest,vf:bc"),

		VU_INFO("opmula", "OPMULA", 3, Operand::UPPER|Operand::XYZ, "acc:dest:write,vf:dest,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_ACC|VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("opmsub", "OPMSUB", 3, Operand::UPPER|Operand::XYZ, "vf:dest:write,vf:dest,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("ftoi0", "FTOI0", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_INFO("ftoi4", "FTOI4", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_INFO("ftoi12", "FTOI12", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_INFO("ftoi15", "FTOI15", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_UPPER_DEST("itof0", "ITOF0", 2, "vf:dest:write,vf:dest"),
		VU_UPPER_DEST("itof4", "ITOF4", 2, "vf:dest:write,vf:dest"),
		VU_UPPER_DEST("itof12", "ITOF12", 2, "vf:dest:write,vf:dest"),
		VU_UPPER_DEST("itof15", "ITOF15", 2, "vf:dest:write,vf:dest"),

		// CLIP reads its first VF operand; it writes only MAC/CLIP flags.
		VU_INFO("clip", "CLIP", 2, Operand::UPPER|Operand::XYZ, "vf:dest,vf:wcomp", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_CLIP, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("clipw", "CLIPw", 2, Operand::UPPER|Operand::XYZ, "vf:dest,vf:wcomp", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_CLIP, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("cliplw", "CLIPLw", 2, Operand::UPPER|Operand::XYZ, "vf:dest,vf:wcomp", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_CLIP, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("div", "DIV", 3, Operand::LOWER, "q:write,vf:flag,vf:flag", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 7, 7, VU_INSTR_WRITES_Q, VU_RESOURCE_NONE, VU_RESOURCE_Q, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("sqrt", "SQRT", 2, Operand::LOWER, "q:write,vf:flag", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 7, 7, VU_INSTR_WRITES_Q, VU_RESOURCE_NONE, VU_RESOURCE_Q, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("rsqrt", "RSQRT", 3, Operand::LOWER, "q:write,vf:flag,vf:flag", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 13, 13, VU_INSTR_WRITES_Q, VU_RESOURCE_NONE, VU_RESOURCE_Q, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("waitq", "WAITQ", 0, Operand::LOWER, "", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 1, 1, VU_INSTR_WAIT_Q, VU_RESOURCE_Q, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("iadd", "IADD", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("iaddi", "IADDI", 3, Operand::LOWER, "vi:write,vi,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("iaddiu", "IADDIU", 3, Operand::LOWER, "vi:write,vi,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("iand", "IAND", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("ior", "IOR", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("isub", "ISUB", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("isubiu", "ISUBIU", 3, Operand::LOWER, "vi:write,vi,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("move", "MOVE", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,vf:dest", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("mfir", "MFIR", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,vi", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("mtir", "MTIR", 2, Operand::LOWER, "vi:write,vf:flag", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("mr32", "MR32", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,vf:dest:rotate", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("lq", "LQ", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,(vi):zero|imm(vi):evaluate", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_LOAD_TO_FTOI),
		VU_INFO("lqd", "LQD", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,(vi):predec", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_PREDEC, 0, VU_BYPASS_LOAD_TO_FTOI),
		VU_INFO("lqi", "LQI", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,(vi):postinc", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_POSTINC, 0, VU_BYPASS_LOAD_TO_FTOI),
		VU_INFO("sq", "SQ", 2, Operand::LOWER|Operand::DEST, "vf:dest,(vi):zero|imm(vi):evaluate", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("sqd", "SQD", 2, Operand::LOWER|Operand::DEST, "vf:dest,(vi):predec", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_PREDEC, 0, VU_BYPASS_NONE),
		VU_INFO("sqi", "SQI", 2, Operand::LOWER|Operand::DEST, "vf:dest,(vi):postinc", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_POSTINC, 0, VU_BYPASS_NONE),
		VU_INFO("ilw", "ILW", 2, Operand::LOWER|Operand::DEST, "vi:write,(vi):zero:dest|imm(vi):evaluate:dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("isw", "ISW", 2, Operand::LOWER|Operand::DEST, "vi,(vi):zero:dest|imm(vi):evaluate:dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("ilwr", "ILWR", 2, Operand::LOWER|Operand::DEST, "vi:write,(vi):dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("iswr", "ISWR", 2, Operand::LOWER|Operand::DEST, "vi,(vi):dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("loi", "LOI", 1, Operand::LOWER|Operand::IWRITE, "imm:evaluate:raw", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 1, VU_INSTR_WRITES_I, VU_RESOURCE_NONE, VU_RESOURCE_I, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("rinit", "RINIT", 2, Operand::LOWER, "r:write,vf:flag", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_R, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("rget", "RGET", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,r", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_R, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("rnext", "RNEXT", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,r", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_R, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("rxor", "RXOR", 2, Operand::LOWER, "r:write,vf:flag", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_R, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("fsand", "FSAND", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fseq", "FSEQ", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fsor", "FSOR", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fsset", "FSSET", 1, Operand::LOWER, "imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fmand", "FMAND", 2, Operand::LOWER, "vi:write,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_MAC, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fmeq", "FMEQ", 2, Operand::LOWER, "vi:write,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_MAC, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fmor", "FMOR", 2, Operand::LOWER, "vi:write,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_MAC, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fcand", "FCAND", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fceq", "FCEQ", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fcor", "FCOR", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fcset", "FCSET", 1, Operand::LOWER, "imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_CLIP, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("fcget", "FCGET", 1, Operand::LOWER, "vi:write", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("ibeq", "IBEQ", 3, Operand::LOWER|Operand::DYNAMIC, "vi,vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("ibgez", "IBGEZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("ibgtz", "IBGTZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("iblez", "IBLEZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("ibltz", "IBLTZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("ibne", "IBNE", 3, Operand::LOWER|Operand::DYNAMIC, "vi,vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("b", "B", 1, Operand::LOWER, "imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_UNCONDITIONAL_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("bal", "BAL", 2, Operand::LOWER, "vi:write:address,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_LINK_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("jr", "JR", 1, Operand::LOWER, "vi:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_UNCONDITIONAL_BRANCH|VU_INSTR_REGISTER_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO("jalr", "JALR", 2, Operand::LOWER, "vi:write:address,vi:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_LINK_BRANCH|VU_INSTR_REGISTER_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),

		VU_INFO("mfp", "MFP", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,p", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_P, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("waitp", "WAITP", 0, Operand::LOWER, "", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 1, 1, VU_INSTR_WAIT_P, VU_RESOURCE_P, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("esadd", "ESADD", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 10, 11, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("ersadd", "ERSADD", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("eleng", "ELENG", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("erleng", "ERLENG", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 23, 24, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("eatanxy", "EATANxy", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("eatanxz", "EATANxz", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("esum", "ESUM", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 11, 12, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("ercpr", "ERCPR", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 11, 12, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("ersqrt", "ERSQRT", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("esin", "ESIN", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 28, 29, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("eatan", "EATAN", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("eexp", "EEXP", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 43, 44, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO("xgkick", "XGKICK", 1, Operand::LOWER, "vi", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_XGKICK, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_XGKICK, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("xtop", "XTOP", 1, Operand::LOWER, "vi:write", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO("xitop", "XITOP", 1, Operand::LOWER, "vi:write", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(0, 0, 0, 0, "", Operand::INVALID, VU_PIPE_UNKNOWN, VU_EXEC_UNKNOWN, 0, 0, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)
	};

#undef VU_ACC_BC_ACC_READ
#undef VU_ACC_ACC_READS
#undef VU_ACC_ACC_READ
#undef VU_ACC_READS
#undef VU_ACC_BC
#undef VU_ACC_DEST
#undef VU_UPPER_BC_ACC_READ
#undef VU_UPPER_DEST_ACC_READS
#undef VU_UPPER_DEST_ACC_READ
#undef VU_UPPER_DEST_READS
#undef VU_UPPER_BC
#undef VU_UPPER_DEST
#undef VU_INFO

	std::string lower( const std::string& text )
	{
		std::string result = text;
		for( std::string::iterator i = result.begin(); i != result.end(); ++i )
			*i = static_cast<char>( std::tolower( static_cast<unsigned char>( *i ) ) );
		return result;
	}

	bool startsWith( const std::string& text, const char* prefix )
	{
		const std::string prefixText( prefix );
		return text.size() >= prefixText.size() && text.compare( 0, prefixText.size(), prefixText ) == 0;
	}

	bool hasBroadcastSuffix( const std::string& mnemonic )
	{
		if( mnemonic.size() <= 1 )
			return false;
		const char suffix = mnemonic[mnemonic.size() - 1];
		return suffix == 'x' || suffix == 'y' || suffix == 'z' || suffix == 'w';
	}

	bool patternHasTag( const std::string& pattern, const char* tag )
	{
		std::string::size_type pos = pattern.find( ':' );
		while( pos != std::string::npos )
		{
			std::string::size_type tagStart = pos + 1;
			std::string::size_type tagEnd = pattern.find( ':', tagStart );
			std::string current = pattern.substr( tagStart, tagEnd == std::string::npos
			                                                ? std::string::npos
			                                                : tagEnd - tagStart );
			if( current == tag )
				return true;
			pos = tagEnd;
		}
		return false;
	}

	void appendParameterNote( std::string& notes, const char* note )
	{
		if( !notes.empty() )
			notes += ",";
		notes += note;
	}

	std::string summarizeOperandAlternative( const std::string& pattern )
	{
		std::string::size_type tag = pattern.find( ':' );
		std::string base = tag == std::string::npos ? pattern : pattern.substr( 0, tag );
		std::string notes;

		if( base == "imm" && patternHasTag( pattern, "branch" ) )
			return "label";

		if( patternHasTag( pattern, "dest" ) )
			appendParameterNote( notes, "dest" );
		if( patternHasTag( pattern, "write" ) )
			appendParameterNote( notes, "write" );
		if( patternHasTag( pattern, "bc" ) )
			appendParameterNote( notes, "bc" );
		if( patternHasTag( pattern, "flag" ) )
			appendParameterNote( notes, "component" );
		if( patternHasTag( pattern, "wcomp" ) )
			appendParameterNote( notes, "w" );
		if( patternHasTag( pattern, "predec" ) )
			appendParameterNote( notes, "predec" );
		if( patternHasTag( pattern, "postinc" ) )
			appendParameterNote( notes, "postinc" );
		if( patternHasTag( pattern, "address" ) )
			appendParameterNote( notes, "address" );
		if( patternHasTag( pattern, "branch" ) )
			appendParameterNote( notes, "branch" );
		if( patternHasTag( pattern, "rotate" ) )
			appendParameterNote( notes, "rotate" );
		if( patternHasTag( pattern, "raw" ) )
			appendParameterNote( notes, "raw" );

		if( notes.empty() )
			return base;
		return base + "[" + notes + "]";
	}

	std::string summarizeOperandPattern( const std::string& pattern )
	{
		std::string result;
		std::string::size_type start = 0;
		while( start <= pattern.size() )
		{
			std::string::size_type end = pattern.find( '|', start );
			std::string part = pattern.substr( start, end == std::string::npos
			                                         ? std::string::npos
			                                         : end - start );
			if( !result.empty() )
				result += "|";
			result += summarizeOperandAlternative( part );
			if( end == std::string::npos )
				break;
			start = end + 1;
		}
		return result;
	}
}

const VuInstructionInfo* allVuInstructionInfos()
{
	return kInstructions;
}

const VuInstructionInfo* findVuInstructionInfo( const std::string& mnemonic )
{
	for( const VuInstructionInfo* info = kInstructions; info->mnemonic; ++info )
	{
		if( mnemonic == info->mnemonic )
			return info;
	}
	return 0;
}

bool isKnownVuInstruction( const std::string& mnemonic )
{
	return findVuInstructionInfo( mnemonic ) != 0;
}

std::string normalizeVuMnemonic( const std::string& word )
{
	std::string mnemonic = lower( word );

	std::string::size_type flag = mnemonic.find( '[' );
	if( flag != std::string::npos )
		mnemonic = mnemonic.substr( 0, flag );

	std::string::size_type fields = mnemonic.find( '.' );
	if( fields != std::string::npos )
		mnemonic = mnemonic.substr( 0, fields );

	if( isKnownVuInstruction( mnemonic ) )
		return mnemonic;

	if( hasBroadcastSuffix( mnemonic ) )
	{
		std::string base = mnemonic.substr( 0, mnemonic.size() - 1 );
		const VuInstructionInfo* info = findVuInstructionInfo( base );
		if( info && info->pipe == VU_PIPE_UPPER )
			return base;
	}

	return mnemonic;
}

std::string vuInstructionParameterSummary( const VuInstructionInfo& info )
{
	if( !info.operandPattern || !info.operandPattern[0] )
		return "";

	std::string result;
	std::string pattern( info.operandPattern );
	std::string::size_type start = 0;
	while( start <= pattern.size() )
	{
		std::string::size_type end = pattern.find( ',', start );
		std::string part = pattern.substr( start, end == std::string::npos
		                                         ? std::string::npos
		                                         : end - start );
		if( !result.empty() )
			result += ", ";
		result += summarizeOperandPattern( part );
		if( end == std::string::npos )
			break;
		start = end + 1;
	}
	return result;
}

const char* vuInstructionDescription( const VuInstructionInfo& info )
{
	if( !info.mnemonic )
		return "";

	const std::string mnemonic = lower( info.mnemonic );
	const std::string operand = info.operandName ? lower( info.operandName ) : mnemonic;

	if( mnemonic == "nop" )
		return "No operation.";
	if( info.memoryKind == VU_MEMORY_LOAD )
		return "Load data from VU memory into a vector or integer register.";
	if( info.memoryKind == VU_MEMORY_STORE )
		return "Store a vector or integer register to VU memory.";
	if( info.memoryKind == VU_MEMORY_XGKICK )
		return "Start a GIF transfer from VU memory.";
	if( info.flags & VU_INSTR_WAIT_Q )
		return "Wait until the Q result is available.";
	if( info.flags & VU_INSTR_WAIT_P )
		return "Wait until the P result is available.";
	if( info.flags & VU_INSTR_BRANCH )
	{
		if( info.flags & VU_INSTR_UNCONDITIONAL_BRANCH )
			return "Branch after one delay slot.";
		return "Branch after one delay slot when the integer condition is met.";
	}
	if( (info.flags & VU_INSTR_WRITES_Q) != 0 )
		return "Start a Q scalar divide/square-root operation.";
	if( (info.flags & VU_INSTR_WRITES_P) != 0 )
		return "Start an elementary-function operation that writes P.";
	if( mnemonic == "loi" )
		return "Load an immediate scalar into the I register.";
	if( startsWith( operand, "ftoi" ) )
		return "Convert floating-point vector fields to fixed-point integer fields.";
	if( startsWith( operand, "itof" ) )
		return "Convert fixed-point integer vector fields to floating-point fields.";
	if( startsWith( operand, "clip" ) )
		return "Update MAC and CLIP flags from clip-space bounds.";
	if( startsWith( operand, "madd" ) )
		return "Multiply vector fields and add ACC.";
	if( startsWith( operand, "msub" ) )
		return "Multiply vector fields and subtract from ACC.";
	if( startsWith( operand, "mul" ) )
		return "Multiply vector fields.";
	if( startsWith( operand, "add" ) )
		return "Add vector fields.";
	if( startsWith( operand, "sub" ) )
		return "Subtract vector fields.";
	if( startsWith( operand, "max" ) )
		return "Take the per-field vector maximum.";
	if( startsWith( operand, "mini" ) )
		return "Take the per-field vector minimum.";
	if( startsWith( operand, "opm" ) )
		return "Perform an outer-product vector operation.";
	if( startsWith( operand, "i" ) && info.unit == VU_EXEC_IALU )
		return "Perform an integer ALU operation on VI registers or immediates.";
	if( startsWith( operand, "f" ) && info.unit == VU_EXEC_IALU )
		return "Read or update VU status flags.";
	if( startsWith( operand, "r" ) && info.unit == VU_EXEC_RANDU )
		return "Access the VU random-number unit.";
	if( mnemonic == "move" )
		return "Copy vector register fields.";
	if( mnemonic == "mfir" )
		return "Move an integer register value into vector fields.";
	if( mnemonic == "mtir" )
		return "Move a vector field into an integer register.";
	if( mnemonic == "mr32" )
		return "Rotate vector register fields.";
	if( mnemonic == "xtop" || mnemonic == "xitop" )
		return "Read the VIF TOP/ITOP register.";
	if( info.unit == VU_EXEC_EFU )
		return "Execute an elementary-function unit operation.";
	if( info.pipe == VU_PIPE_UPPER )
		return "Execute an upper-pipe vector operation.";
	if( info.pipe == VU_PIPE_LOWER )
		return "Execute a lower-pipe VU operation.";
	return "VU instruction.";
}

}
