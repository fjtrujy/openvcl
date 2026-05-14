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
        ops.push_back(vcl::Operand("--LoopCS", 2, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "imm:integer,imm:integer"));

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

    bool hasString(const std::list<std::string>& values, const std::string& value)
    {
        for (std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i)
        {
            if (*i == value)
                return true;
        }
        return false;
    }

    std::string terminatorName(const vcl::VuBasicBlock& block)
    {
        if (!block.terminator || !block.terminator->operand())
            return "";
        return block.terminator->operand()->name();
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
    CHECK(blocks[0].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BRANCH);
    CHECK(blocks[0].terminator != 0);
    CHECK(blocks[1].firstTokenIndex == 3u);
    CHECK(blocks[1].tokens.size() == 1u);
    CHECK(blocks[1].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_NONE);
    CHECK(blocks[1].terminator == 0);
    CHECK(blocks[2].firstTokenIndex == 4u);
    CHECK(blocks[2].tokens.size() == 2u);
    CHECK(blocks[2].terminatedByBarrier == true);
    CHECK(blocks[2].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_XGKICK);
    CHECK(blocks[2].terminator != 0);
    CHECK(blocks[3].firstTokenIndex == 6u);
    CHECK(blocks[3].tokens.size() == 2u);
    CHECK(blocks[3].terminatedByBarrier == true);
    CHECK(blocks[3].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[3]) == "--barrier");
    CHECK(blocks[4].firstTokenIndex == 8u);
    CHECK(blocks[4].tokens.size() == 1u);
    CHECK(blocks[4].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_NONE);
    CHECK(blocks[4].terminator == 0);
}

TEST_CASE("VuSchedulerAnalysis: basic blocks classify compiler boundary terminators")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("--cont"));
    REQUIRE(program.parse("--enter"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("--exit"));
    REQUIRE(program.parse("add.xy vf07, vf08, vf09"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 4u);
    CHECK(blocks[0].firstTokenIndex == 0u);
    CHECK(blocks[0].tokens.size() == 2u);
    CHECK(blocks[0].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[0]) == "--cont");
    CHECK(blocks[1].firstTokenIndex == 2u);
    CHECK(blocks[1].tokens.size() == 1u);
    CHECK(blocks[1].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[1]) == "--enter");
    CHECK(blocks[2].firstTokenIndex == 3u);
    CHECK(blocks[2].tokens.size() == 2u);
    CHECK(blocks[2].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[2]) == "--exit");
    CHECK(blocks[3].firstTokenIndex == 5u);
    CHECK(blocks[3].tokens.size() == 1u);
    CHECK(blocks[3].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_NONE);
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

TEST_CASE("VuSchedulerAnalysis: loop candidates find LoopCS back-edge loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("setup_lid:"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 0"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 3"));
    REQUIRE(program.parse("ibne vi02, vi03, loop_lid"));
    REQUIRE(program.parse("done_lid:"));
    REQUIRE(program.parse("xgkick vi04"));

    std::vector<vcl::VuLoopCandidate> loops = vcl::findVuLoopCandidates(program.tokenizer.tokens());
    REQUIRE(loops.size() == 1u);
    CHECK(loops[0].label == "loop_lid");
    CHECK(loops[0].labelTokenIndex == 2u);
    CHECK(loops[0].branchTokenIndex == 6u);
    CHECK(loops[0].firstBodyTokenIndex == 2u);
    CHECK(loops[0].lastBodyTokenIndex == 6u);
    CHECK(loops[0].hasLoopDirective);
    CHECK(loops[0].simpleCountedLoop);
    CHECK(loops[0].memoryLoadCount == 1u);
    CHECK(loops[0].memoryStoreCount == 0u);
    CHECK(!loops[0].hasMemoryPreOrPostIncrement);
    CHECK(!loops[0].hasXgkick);
    CHECK(hasString(loops[0].inductionRegisters, "VI02"));
    CHECK(hasString(loops[0].loopReadWriteRegisters, "VI02"));
    REQUIRE(loops[0].branchToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(loops[0].branchToken->name()) == "ibne");
    REQUIRE(loops[0].bodyTokens.size() == 5u);
    CHECK(loops[0].bodyTokens.front()->label() == "loop_lid");
    CHECK(vcl::normalizeVuMnemonic(loops[0].bodyTokens.back()->name()) == "ibne");
}

TEST_CASE("VuSchedulerAnalysis: loop candidates ignore forward branches and mark unconditional loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry_lid:"));
    REQUIRE(program.parse("ibne vi01, vi02, done_lid"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("b loop_lid"));
    REQUIRE(program.parse("done_lid:"));
    REQUIRE(program.parse("xgkick vi04"));

    std::vector<vcl::VuLoopCandidate> loops = vcl::findVuLoopCandidates(program.tokenizer.tokens());
    REQUIRE(loops.size() == 1u);
    CHECK(loops[0].label == "loop_lid");
    CHECK(loops[0].hasLoopDirective);
    CHECK(!loops[0].simpleCountedLoop);
    REQUIRE(loops[0].branchToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(loops[0].branchToken->name()) == "b");
}

TEST_CASE("VuSchedulerAnalysis: pipeline opportunities expose loop-carried Q state")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("mulq.xyz vf03, vf03, q"));
    REQUIRE(program.parse("add.xyz vf05, vf03, vf00"));
    REQUIRE(program.parse("lq.xyz vf06, 2(vi01)"));
    REQUIRE(program.parse("mulq.xyz vf06, vf06, q"));
    REQUIRE(program.parse("sq.xyz vf06, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 3"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].label == "loop_lid");
    CHECK(opportunities[0].branchTokenIndex == 11u);
    CHECK(opportunities[0].qProducerTokenIndex == 4u);
    CHECK(opportunities[0].firstQConsumerTokenIndex == 5u);
    CHECK(opportunities[0].qProducerLatency == 7u);
    CHECK(opportunities[0].sourcePrefixCycles > 0u);
    CHECK(opportunities[0].sourcePrefixCycles < opportunities[0].qProducerLatency);
    CHECK(opportunities[0].sourcePrefixCycles + opportunities[0].sourceSuffixCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].branchDelaySlots == 1u);
    CHECK(opportunities[0].simpleCountedLoop);
    CHECK(opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].requiresPrologEpilog);
    CHECK(opportunities[0].memoryLoadCount == 2u);
    CHECK(opportunities[0].memoryStoreCount == 1u);
    CHECK(!opportunities[0].hasMemoryPreOrPostIncrement);
    CHECK(!opportunities[0].hasXgkick);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
    CHECK(hasString(opportunities[0].loopReadWriteRegisters, "VI01"));
    CHECK(opportunities[0].requiresLoopCarriedRegisters);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(hasString(opportunities[0].softwarePipelineRotatedRegisters, "VF03"));
    CHECK(hasString(opportunities[0].softwarePipelineRotatedRegisters, "VF06"));
    REQUIRE(opportunities[0].qConsumerTokenIndices.size() == 2u);
    CHECK(opportunities[0].qConsumerTokenIndices[0] == 5u);
    CHECK(opportunities[0].qConsumerTokenIndices[1] == 8u);
    REQUIRE(opportunities[0].prologTokenIndices.size() == 3u);
    CHECK(opportunities[0].prologTokenIndices[0] == 2u);
    CHECK(opportunities[0].prologTokenIndices[1] == 3u);
    CHECK(opportunities[0].prologTokenIndices[2] == 4u);
    REQUIRE(opportunities[0].mainTokenIndices.size() == 7u);
    CHECK(opportunities[0].mainTokenIndices[0] == 5u);
    CHECK(opportunities[0].mainTokenIndices[6] == 11u);
    REQUIRE(opportunities[0].drainTokenIndices.size() == 6u);
    CHECK(opportunities[0].drainTokenIndices[0] == 5u);
    CHECK(opportunities[0].drainTokenIndices[5] == 10u);
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF03.x"));
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF06.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF03.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF06.x"));
}

TEST_CASE("VuSchedulerAnalysis: pipeline plans report loops that are safe for generic emission")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].softwarePipelineBlockers.empty());
    CHECK(opportunities[0].softwarePipelineRotatedRegisters.empty());
    CHECK(!opportunities[0].requiresLoopCarriedRegisters);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
}

TEST_CASE("VuSchedulerAnalysis: pipeline opportunities require Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 3"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    CHECK(opportunities.empty());
}

TEST_CASE("VuSchedulerAnalysis: dependency graph keeps ACC multiply-add chains ordered")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("mulax acc, vf01, vf02"));
    REQUIRE(program.parse("madday acc, vf03, vf04"));
    REQUIRE(program.parse("maddw vf05, vf06, vf07"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 1u, 2u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph can ignore dead MAC WAW edges")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> conservativeEdges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(conservativeEdges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));

    std::vector<vcl::VuDependencyEdge> deadMacEdges = vcl::buildVuDependencyGraph(blocks[0], vcl::VU_RESOURCE_MAC);
    CHECK(!hasEdge(deadMacEdges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph keeps CLIP WAW unless CLIP is dead")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("clipw.xyz vf01, vf02[w]"));
    REQUIRE(program.parse("clipw.xyz vf03, vf04[w]"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> macOnlyIgnored = vcl::buildVuDependencyGraph(blocks[0], vcl::VU_RESOURCE_MAC);
    CHECK(hasEdge(macOnlyIgnored, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));

    std::vector<vcl::VuDependencyEdge> allFlagsIgnored = vcl::buildVuDependencyGraph(blocks[0], vcl::VU_RESOURCE_MAC | vcl::VU_RESOURCE_CLIP);
    CHECK(!hasEdge(allFlagsIgnored, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph compares plain memory descriptors")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf02, 4(vi01)"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(!hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_MEMORY));
    CHECK(hasEdge(edges, 0u, 2u, vcl::VU_DEPENDENCY_MEMORY));
}

TEST_CASE("VuSchedulerAnalysis: preserve-order scheduler keeps block order intact")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry:"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ibne vi01, vi02, next"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("next:"));
    REQUIRE(program.parse("xgkick vi01"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensPreservingOrder(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator a = program.tokenizer.tokens().begin();
    std::list<vcl::Token>::const_iterator b = scheduled.begin();
    for (; a != program.tokenizer.tokens().end(); ++a, ++b)
    {
        CHECK(a->name() == b->name());
        CHECK(a->label() == b->label());
        CHECK(a->arguments().size() == b->arguments().size());
    }
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler pulls long Q producers forward")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("div q, vf04[w], vf05[w]"));
    REQUIRE(program.parse("mul.xy vf06, vf07, vf08"));
    REQUIRE(program.parse("waitq"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "div");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "waitq");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler keeps dependencies and barriers ordered")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("addi.xy vf06, vf07, i"));
    REQUIRE(program.parse("sq.xy vf06, 0(vi01)"));
    REQUIRE(program.parse("div q, vf08[w], vf09[w]"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::vector<std::string> names;
    for( std::list<vcl::Token>::const_iterator i = scheduled.begin(); i != scheduled.end(); ++i )
        names.push_back(vcl::normalizeVuMnemonic(i->name()));

    REQUIRE(names.size() == 6u);

    unsigned int addPos = 0;
    unsigned int mulPos = 0;
    unsigned int loiPos = 0;
    unsigned int addiPos = 0;
    unsigned int sqPos = 0;
    unsigned int divPos = 0;
    for( unsigned int i = 0; i < names.size(); ++i )
    {
        if( names[i] == "add" )
            addPos = i;
        else if( names[i] == "mul" )
            mulPos = i;
        else if( names[i] == "loi" )
            loiPos = i;
        else if( names[i] == "addi" )
            addiPos = i;
        else if( names[i] == "sq" )
            sqPos = i;
        else if( names[i] == "div" )
            divPos = i;
    }

    CHECK(addPos < mulPos);
    CHECK(loiPos < addiPos);
    CHECK(addiPos < sqPos);
    CHECK(divPos < sqPos);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler pulls plain loads before independent arithmetic")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("lq.xy vf04, 0(vi01)"));
    REQUIRE(program.parse("mul.xy vf05, vf04, vf06"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "lq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler keeps independent dual-pipe partners adjacent")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: issue slots expose generic dual-pipe pair choices")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuScheduledIssueSlot> slots = vcl::scheduleVuBasicBlockReadyIssueSlots(blocks[0]);
    REQUIRE(slots.size() == 2u);
    REQUIRE(slots[0].firstToken != 0);
    REQUIRE(slots[0].secondToken != 0);
    CHECK(vcl::normalizeVuMnemonic(slots[0].firstToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(slots[0].secondToken->name()) == "iaddiu");
    REQUIRE(slots[0].upperToken != 0);
    REQUIRE(slots[0].lowerToken != 0);
    CHECK(vcl::normalizeVuMnemonic(slots[0].upperToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(slots[0].lowerToken->name()) == "iaddiu");

    REQUIRE(slots[1].firstToken != 0);
    CHECK(slots[1].secondToken == 0);
    CHECK(vcl::normalizeVuMnemonic(slots[1].firstToken->name()) == "mul");
    REQUIRE(slots[1].upperToken != 0);
    CHECK(slots[1].lowerToken == 0);
    CHECK(vcl::normalizeVuMnemonic(slots[1].upperToken->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler prefers longer dependency chains")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("iaddiu vi10, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi04, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi06, vi04, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 1u);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 4u);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 10u);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 6u);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler weights dependency chains by latency")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("iaddiu vi10, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi11, vi10, 1"));
    REQUIRE(program.parse("iaddiu vi12, vi11, 1"));
    REQUIRE(program.parse("ftoi0.xy vf01, vf00"));
    REQUIRE(program.parse("add.xy vf02, vf01, vf00"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "ftoi0");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler moves distinct-address loads before stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf02, 4(vi01)"));
    REQUIRE(program.parse("mul.xy vf03, vf02, vf04"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "lq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler keeps same-address loads behind stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf02, 0(vi01)"));
    REQUIRE(program.parse("mul.xy vf03, vf02, vf04"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "lq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: flag liveness keeps MAC WAW before the last reader")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mul.xy vf11, vf10, vf00"));
    REQUIRE(program.parse("fmand vi01, vi02"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySetWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "ftoi0");
}

TEST_CASE("VuSchedulerAnalysis: flag liveness ignores MAC WAW after the last reader")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("fmand vi01, vi02"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mul.xy vf11, vf10, vf00"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySetWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "fmand");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "ftoi0");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler can move Q producers before independent plain stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf02[w], vf03[w]"));
    REQUIRE(program.parse("add.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("mulq.xy vf07, vf08, q"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "div");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mulq");
}
