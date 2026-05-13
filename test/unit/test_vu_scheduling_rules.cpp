#include "test_harness.h"

#include "../../src/Error.h"
#include "../../src/File.h"
#include "../../src/Line.h"
#include "../../src/Operand.h"
#include "../../src/Token.h"
#include "../../src/Tokenizer.h"
#include "../../src/VuInstructionInfo.h"
#include "../../src/VuSchedulingRules.h"

#include <list>
#include <string>

namespace
{
    std::list<vcl::Operand> makeOperands()
    {
        std::list<vcl::Operand> ops;
        ops.push_back(vcl::Operand("--barrier", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, ""));
        ops.push_back(vcl::Operand("--cont", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, ""));
        ops.push_back(vcl::Operand("--enter", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::ENTER));
        ops.push_back(vcl::Operand("--endenter", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::ENTER));
        ops.push_back(vcl::Operand("--exit", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::EXIT));
        ops.push_back(vcl::Operand("--endexit", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::EXIT));

        for (const vcl::VuInstructionInfo* info = vcl::allVuInstructionInfos(); info->mnemonic; ++info)
        {
            ops.push_back(vcl::Operand(info->operandName,
                                       info->arguments,
                                       info->operandFlags,
                                       info->operandPattern,
                                       info->operandUnit,
                                       info->throughput,
                                       info->latency));
        }
        return ops;
    }

    struct ParsedProgram
    {
        ParsedProgram() : operands(makeOperands())
        {
            tokenizer.setOperands(operands);
            tokenizer.setNewSyntax(true);
        }

        bool parse(const std::string& text)
        {
            lines.push_back(vcl::Line(file, static_cast<unsigned int>(lines.size() + 1), static_cast<unsigned int>(lines.size() + 1), text));
            if (!tokenizer.parse(lines.back()))
                return false;
            if (!tokenizer.tokens().empty())
            {
                vcl::Token& token = tokenizer.tokens().back();
                token.setFlags(token.flags() | vcl::Token::PROCESSED);
            }
            return true;
        }

        const vcl::Token& token(unsigned int index)
        {
            std::list<vcl::Token>::const_iterator i = tokenizer.tokens().begin();
            for (unsigned int n = 0; n < index; ++n)
                ++i;
            return *i;
        }

        static vcl::File file;
        std::list<vcl::Operand> operands;
        std::list<vcl::Line> lines;
        vcl::Tokenizer tokenizer;
    };

    vcl::File ParsedProgram::file("<scheduling-rules-test>");
}

TEST_CASE("VuSchedulingRules: instruction gating and metadata predicates are reusable")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("--barrier"));
    REQUIRE(program.parse("b done"));
    REQUIRE(program.parse("jalr vi15, vi01"));
    REQUIRE(program.parse("div q, vf04[w], vf05[w]"));
    REQUIRE(program.parse("addq.xy vf06, vf07, q"));

    CHECK(vcl::isVuEmittableInstruction(program.token(0)));
    CHECK(!vcl::isVuEmittableInstruction(program.token(1)));
    CHECK(vcl::vuTokenBranchDelaySlots(program.token(2)) == 1u);
    CHECK(vcl::isVuTerminalUnconditionalBranch(program.token(2)));
    CHECK(!vcl::isVuTerminalUnconditionalBranch(program.token(3)));
    CHECK(vcl::vuTokenWritesQ(program.token(4)));
    CHECK(vcl::vuTokenReadsQ(program.token(5)));
}

TEST_CASE("VuSchedulingRules: movement uses descriptors for memory and implicit resources")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("lq.xy vf01, 4(vi01)"));
    REQUIRE(program.parse("sq.xy vf02, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("addi.xy vf04, vf05, i"));

    CHECK(vcl::vuTokenCanMoveBefore(program.token(0), program.token(1)));
    CHECK(!vcl::vuTokenCanMoveBefore(program.token(2), program.token(1)));
    CHECK(!vcl::vuTokenCanMoveBefore(program.token(4), program.token(3)));
    CHECK(!vcl::vuTokenRangeCanBeCrossed(program.token(1), program.token(0)));
}

TEST_CASE("VuSchedulingRules: pair resource checks reject hazards before code emission")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("addi.xy vf06, vf07, i"));
    REQUIRE(program.parse("clipw.xyz vf08, vf09[w]"));
    REQUIRE(program.parse("fcand vi02, 0x3ffff"));
    REQUIRE(program.parse("waitq"));

    CHECK(vcl::vuTokenPairResourcesAreIndependent(program.token(0), program.token(1), true, false));
    CHECK(vcl::vuTokensHaveDataDependency(program.token(0), program.token(2)));
    CHECK(!vcl::vuTokenPairResourcesAreIndependent(program.token(0), program.token(2), true, true));
    CHECK(!vcl::vuTokenPairResourcesAreIndependent(program.token(3), program.token(4), false, true));
    CHECK(!vcl::vuTokenPairResourcesAreIndependent(program.token(5), program.token(6), true, false));
    CHECK(!vcl::vuTokenPairResourcesAreIndependent(program.token(0), program.token(7), true, false));
}

TEST_CASE("VuSchedulingRules: adjacent integer add coalescing is exposed for scheduler prep")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("iaddiu vi01, vi02, 4"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 8"));

    std::list<vcl::Token> tokens = program.tokenizer.tokens();
    vcl::coalesceAdjacentVuIntegerAdds(tokens);

    REQUIRE(tokens.size() == 1u);
    const std::list<vcl::Token::Argument>& args = tokens.front().arguments();
    REQUIRE(args.size() == 3u);

    std::list<vcl::Token::Argument>::const_iterator immediate = args.begin();
    ++immediate;
    ++immediate;
    CHECK(immediate->immediate() == "12");
}
