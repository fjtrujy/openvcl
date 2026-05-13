#include "test_harness.h"

#include "../../src/Error.h"
#include "../../src/File.h"
#include "../../src/Line.h"
#include "../../src/Operand.h"
#include "../../src/Token.h"
#include "../../src/Tokenizer.h"
#include "../../src/VuInstructionInfo.h"
#include "../../src/VuSchedulerAnalysis.h"

#include <list>
#include <string>
#include <vector>

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
            return tokenizer.parse(lines.back());
        }

        static vcl::File file;
        std::list<vcl::Operand> operands;
        std::list<vcl::Line> lines;
        vcl::Tokenizer tokenizer;
    };

    vcl::File ParsedProgram::file("<scheduler-analysis-test>");

    bool hasEdge(const std::vector<vcl::VuDependencyEdge>& edges, unsigned int before, unsigned int after, vcl::VuDependencyKind kind)
    {
        for (std::vector<vcl::VuDependencyEdge>::const_iterator i = edges.begin(); i != edges.end(); ++i)
        {
            if (i->before == before && i->after == after && i->kind == kind)
                return true;
        }
        return false;
    }
}

TEST_CASE("VuSchedulerAnalysis: basic blocks split on labels and barriers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry:"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ibne vi01, vi02, next"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("next:"));
    REQUIRE(program.parse("xgkick vi01"));
    REQUIRE(program.parse("add.xy vf07, vf08, vf09"));
    REQUIRE(program.parse("--barrier"));
    REQUIRE(program.parse("add.xy vf10, vf11, vf12"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 5u);
    CHECK(blocks[0].firstTokenIndex == 0u);
    CHECK(blocks[0].tokens.size() == 3u);
    CHECK(blocks[0].terminatedByBarrier == true);
    CHECK(blocks[1].firstTokenIndex == 3u);
    CHECK(blocks[1].tokens.size() == 1u);
    CHECK(blocks[2].firstTokenIndex == 4u);
    CHECK(blocks[2].tokens.size() == 2u);
    CHECK(blocks[2].terminatedByBarrier == true);
    CHECK(blocks[3].firstTokenIndex == 6u);
    CHECK(blocks[3].tokens.size() == 2u);
    CHECK(blocks[3].terminatedByBarrier == true);
    CHECK(blocks[4].firstTokenIndex == 8u);
    CHECK(blocks[4].tokens.size() == 1u);
}

TEST_CASE("VuSchedulerAnalysis: dependency graph uses register and resource descriptors")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mulw.xyz vf04, vf01, vf05"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("addi.xy vf06, vf07, i"));
    REQUIRE(program.parse("sq.xy vf06, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf08, 0(vi01)"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_REGISTER_RAW));
    CHECK(hasEdge(edges, 2u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 3u, 4u, vcl::VU_DEPENDENCY_REGISTER_RAW));
    CHECK(hasEdge(edges, 4u, 5u, vcl::VU_DEPENDENCY_MEMORY));
}
