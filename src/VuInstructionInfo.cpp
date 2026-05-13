#include "VuInstructionInfo.h"

#include <cctype>
#include <cstring>

namespace vcl
{

namespace
{
	const VuInstructionInfo kInstructions[] =
	{
		{ "nop", VU_PIPE_NOP, VU_EXEC_NOP, 0, 0, VU_INSTR_NONE },

		{ "abs", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "add", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "addi", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "addq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "adda", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "addai", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "addaq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "sub", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "subi", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "subq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "suba", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "subai", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "subaq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "mul", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "muli", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "mulq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "mula", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "mulai", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "mulaq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "madd", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "maddi", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "maddq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "madda", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "maddai", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "maddaq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "msub", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "msubi", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "msubq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "msuba", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "msubai", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "msubaq", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "max", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "maxi", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "mini", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "minii", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "opmula", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "opmsub", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "ftoi0", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "ftoi4", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "ftoi12", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "ftoi15", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "itof0", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "itof4", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "itof12", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "itof15", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "clip", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "clipw", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },
		{ "cliplw", VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 4, VU_INSTR_NONE },

		{ "div", VU_PIPE_LOWER, VU_EXEC_FDIV, 7, 7, VU_INSTR_WRITES_Q },
		{ "sqrt", VU_PIPE_LOWER, VU_EXEC_FDIV, 7, 7, VU_INSTR_WRITES_Q },
		{ "rsqrt", VU_PIPE_LOWER, VU_EXEC_FDIV, 13, 13, VU_INSTR_WRITES_Q },
		{ "waitq", VU_PIPE_LOWER, VU_EXEC_FDIV, 1, 1, VU_INSTR_WAIT_Q },

		{ "iadd", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "iaddi", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "iaddiu", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "iand", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "ior", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "isub", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "isubiu", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fsand", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fseq", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fsor", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fsset", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 4, VU_INSTR_NONE },
		{ "fmand", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fmeq", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fmor", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fcand", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fceq", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fcor", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },
		{ "fcset", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 4, VU_INSTR_NONE },
		{ "fcget", VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE },

		{ "move", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE },
		{ "mfir", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE },
		{ "mtir", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE },
		{ "mr32", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE },
		{ "xtop", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE },
		{ "xitop", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE },
		{ "xgkick", VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE },

		{ "lq", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "lqd", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "lqi", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "sq", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "sqd", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "sqi", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "ilw", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "isw", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "ilwr", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "iswr", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE },
		{ "loi", VU_PIPE_LOWER, VU_EXEC_LSU, 1, 1, VU_INSTR_WRITES_I },

		{ "rinit", VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 1, VU_INSTR_NONE },
		{ "rget", VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 4, VU_INSTR_NONE },
		{ "rnext", VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 4, VU_INSTR_NONE },
		{ "rxor", VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 1, VU_INSTR_NONE },

		{ "ibeq", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "ibgez", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "ibgtz", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "iblez", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "ibltz", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "ibne", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "b", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "bal", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "jr", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },
		{ "jalr", VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH },

		{ "mfp", VU_PIPE_LOWER, VU_EXEC_EFU, 1, 4, VU_INSTR_NONE },
		{ "waitp", VU_PIPE_LOWER, VU_EXEC_EFU, 1, 1, VU_INSTR_WAIT_P },
		{ "esadd", VU_PIPE_LOWER, VU_EXEC_EFU, 10, 11, VU_INSTR_WRITES_P },
		{ "ersadd", VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P },
		{ "eleng", VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P },
		{ "erleng", VU_PIPE_LOWER, VU_EXEC_EFU, 23, 24, VU_INSTR_WRITES_P },
		{ "eatanxy", VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P },
		{ "eatanxz", VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P },
		{ "esum", VU_PIPE_LOWER, VU_EXEC_EFU, 11, 12, VU_INSTR_WRITES_P },
		{ "ercpr", VU_PIPE_LOWER, VU_EXEC_EFU, 11, 12, VU_INSTR_WRITES_P },
		{ "ersqrt", VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P },
		{ "esin", VU_PIPE_LOWER, VU_EXEC_EFU, 28, 29, VU_INSTR_WRITES_P },
		{ "eatan", VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P },
		{ "eexp", VU_PIPE_LOWER, VU_EXEC_EFU, 43, 44, VU_INSTR_WRITES_P },

		{ 0, VU_PIPE_UNKNOWN, VU_EXEC_UNKNOWN, 0, 0, VU_INSTR_NONE }
	};

	std::string lower( const std::string& text )
	{
		std::string result = text;
		for( std::string::iterator i = result.begin(); i != result.end(); ++i )
			*i = static_cast<char>( std::tolower( static_cast<unsigned char>( *i ) ) );
		return result;
	}

	bool hasBroadcastSuffix( const std::string& mnemonic )
	{
		if( mnemonic.size() <= 1 )
			return false;
		const char suffix = mnemonic[mnemonic.size() - 1];
		return suffix == 'x' || suffix == 'y' || suffix == 'z' || suffix == 'w';
	}
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

}
