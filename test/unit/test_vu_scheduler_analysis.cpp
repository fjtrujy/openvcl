#include "test_harness.h"

#include "../../src/Error.h"
#include "../../src/File.h"
#include "../../src/Line.h"
#include "../../src/Operand.h"
#include "../../src/Token.h"
#include "../../src/Tokenizer.h"
#include "../../src/VuInstructionInfo.h"
#include "../../src/VuLatencyTracker.h"
#include "../../src/VuSchedulerAnalysis.h"
#include "../../src/VuTokenResourceAccess.h"

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

    const vcl::VuSoftwarePipelineRotation* findRotation(const std::vector<vcl::VuSoftwarePipelineRotation>& rotations,
                                                        const std::string& registerBase)
    {
        for (std::vector<vcl::VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i)
        {
            if (i->registerBase == registerBase)
                return &*i;
        }
        return NULL;
    }

    const vcl::VuLoopInductionUpdate* findInductionUpdate(const std::vector<vcl::VuLoopInductionUpdate>& updates,
                                                          const std::string& registerName)
    {
        for (std::vector<vcl::VuLoopInductionUpdate>::const_iterator i = updates.begin(); i != updates.end(); ++i)
        {
            if (i->registerName == registerName)
                return &*i;
        }
        return NULL;
    }

    const vcl::VuSoftwarePipelinePrefetch* findPrefetch(const std::vector<vcl::VuSoftwarePipelinePrefetch>& prefetches,
                                                        unsigned int tokenIndex)
    {
        for (std::vector<vcl::VuSoftwarePipelinePrefetch>::const_iterator i = prefetches.begin(); i != prefetches.end(); ++i)
        {
            if (i->tokenIndex == tokenIndex)
                return &*i;
        }
        return NULL;
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
    const vcl::VuLoopInductionUpdate* vi02Update = findInductionUpdate(loops[0].inductionUpdates, "VI02");
    REQUIRE(vi02Update != NULL);
    CHECK(vi02Update->stepKnown);
    CHECK(vi02Update->step == 3);
    CHECK(vi02Update->tokenIndex == 5u);
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
    CHECK(opportunities[0].lastQConsumerTokenIndex == 8u);
    CHECK(opportunities[0].qProducerLatency == 7u);
    CHECK(opportunities[0].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].qProducerInsertionGapCycles == 5u);
    CHECK(opportunities[0].qProducerInsertionGapDeficitCycles == 2u);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_LOOP_CARRIED);
    CHECK(opportunities[0].sourcePrefixCycles > 0u);
    CHECK(opportunities[0].sourcePrefixCycles < opportunities[0].qProducerLatency);
    CHECK(opportunities[0].sourcePrefixCycles + opportunities[0].sourceSuffixCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].branchDelaySlots == 1u);
    CHECK(opportunities[0].loopCarriedQGapCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].simpleCountedLoop);
    CHECK(opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].requiresPrologEpilog);
    CHECK(opportunities[0].memoryLoadCount == 2u);
    CHECK(opportunities[0].memoryStoreCount == 1u);
    CHECK(!opportunities[0].hasMemoryPreOrPostIncrement);
    CHECK(!opportunities[0].hasXgkick);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
    const vcl::VuLoopInductionUpdate* vi01Update = findInductionUpdate(opportunities[0].inductionUpdates, "VI01");
    REQUIRE(vi01Update != NULL);
    CHECK(vi01Update->stepKnown);
    CHECK(vi01Update->step == 3);
    CHECK(vi01Update->tokenIndex == 10u);
    CHECK(hasString(opportunities[0].loopReadWriteRegisters, "VI01"));
    CHECK(opportunities[0].requiresLoopCarriedRegisters);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "insufficient_q_insertion_gap"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(hasString(opportunities[0].softwarePipelineRotatedRegisters, "VF03"));
    CHECK(!hasString(opportunities[0].softwarePipelineRotatedRegisters, "VF06"));
    const vcl::VuSoftwarePipelinePrefetch* lqPrefetch = findPrefetch(opportunities[0].softwarePipelinePrefetches, 2u);
    REQUIRE(lqPrefetch != NULL);
    CHECK(lqPrefetch->mnemonic == "lq");
    CHECK(lqPrefetch->memoryKind == vcl::VU_MEMORY_LOAD);
    CHECK(lqPrefetch->hasMemoryBase);
    CHECK(lqPrefetch->memoryBaseRegister == "VI01");
    CHECK(lqPrefetch->hasMemoryOffset);
    CHECK(lqPrefetch->memoryOffset == 0);
    CHECK(lqPrefetch->readsInductionRegister);
    CHECK(lqPrefetch->inductionRegister == "VI01");
    CHECK(lqPrefetch->hasNextIterationOffset);
    CHECK(lqPrefetch->nextIterationOffset == 3);
    const vcl::VuSoftwarePipelinePrefetch* mulPrefetch = findPrefetch(opportunities[0].softwarePipelinePrefetches, 3u);
    REQUIRE(mulPrefetch != NULL);
    CHECK(mulPrefetch->memoryKind == vcl::VU_MEMORY_NONE);
    CHECK(!mulPrefetch->readsInductionRegister);
    const vcl::VuSoftwarePipelineRotation* vf03Rotation = findRotation(opportunities[0].softwarePipelineRotations, "VF03");
    REQUIRE(vf03Rotation != NULL);
    CHECK(vf03Rotation->hasScratchRegister);
    CHECK(vf03Rotation->scratchRegister == "VF31");
    CHECK(hasString(vf03Rotation->inputFields, "x"));
    CHECK(hasString(vf03Rotation->inputFields, "y"));
    CHECK(hasString(vf03Rotation->inputFields, "z"));
    CHECK(hasString(vf03Rotation->outputFields, "x"));
    CHECK(hasString(vf03Rotation->outputFields, "y"));
    CHECK(hasString(vf03Rotation->outputFields, "z"));
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF06") == NULL);
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
    CHECK(opportunities[0].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].loopCarriedQGapCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_LOOP_CARRIED);
    CHECK(opportunities[0].softwarePipelineBlockers.empty());
    CHECK(opportunities[0].softwarePipelineRotatedRegisters.empty());
    CHECK(!opportunities[0].requiresLoopCarriedRegisters);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
}

TEST_CASE("VuSchedulerAnalysis: pipeline plans allow multiple induction registers")
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
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
    CHECK(hasString(opportunities[0].inductionRegisters, "VI03"));
    const vcl::VuLoopInductionUpdate* vi01Update = findInductionUpdate(opportunities[0].inductionUpdates, "VI01");
    const vcl::VuLoopInductionUpdate* vi03Update = findInductionUpdate(opportunities[0].inductionUpdates, "VI03");
    REQUIRE(vi01Update != NULL);
    REQUIRE(vi03Update != NULL);
    CHECK(vi01Update->step == 1);
    CHECK(vi03Update->step == 1);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_single_induction_register"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "missing_induction_register"));
}

TEST_CASE("VuSchedulerAnalysis: apply software pipeline plans rewrites emittable loops")
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

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].label == "loop_lid");
    CHECK(plans[0].prologLabel == "loop_lid__PROLOG");
    CHECK(plans[0].mainLabel == "loop_lid");
    CHECK(plans[0].drainLabel == "loop_lid__DRAIN");
    CHECK(plans[0].labelTokenIndex == 0u);
    CHECK(plans[0].branchTokenIndex == 11u);
    CHECK(plans[0].qProducerTokenIndex == 2u);
    CHECK(plans[0].prefetchInsertAfterTokenIndex == 3u);
    CHECK(plans[0].qProducerInsertAfterTokenIndex == 11u);
    CHECK(plans[0].qProducerInBranchDelaySlot);
    CHECK(!plans[0].emitsDrain);
    REQUIRE(plans[0].prologTokenIndices.size() == 1u);
    CHECK(plans[0].prologTokenIndices[0] == 2u);
    REQUIRE(plans[0].mainTokenIndices.size() == 9u);
    CHECK(plans[0].mainTokenIndices[0] == 3u);
    CHECK(plans[0].mainTokenIndices[8] == 11u);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int prologLabels = 0;
    unsigned int mainLabels = 0;
    unsigned int divCount = 0;
    bool divInBranchDelaySlot = false;
    bool previousWasBranch = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid")
            ++mainLabels;
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "div")
        {
            ++divCount;
            if (previousWasBranch && (i->flags() & vcl::Token::BRANCH_DELAY_FILLER))
                divInBranchDelaySlot = true;
        }
        previousWasBranch = mnemonic == "ibne";
    }

    CHECK(prologLabels == 1u);
    CHECK(mainLabels == 1u);
    CHECK(divCount == 2u);
    CHECK(divInBranchDelaySlot);
    CHECK(transformed.size() == program.tokenizer.tokens().size() + 1u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline skips loops with local Q latency hidden")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("add.xyz vf16, vf16, vf00"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf20, vf20, vf00"));
    REQUIRE(program.parse("add.xyz vf21, vf21, vf00"));
    REQUIRE(program.parse("add.xyz vf22, vf22, vf00"));
    REQUIRE(program.parse("add.xyz vf23, vf23, vf00"));
    REQUIRE(program.parse("add.xyz vf24, vf24, vf00"));
    REQUIRE(program.parse("add.xyz vf25, vf25, vf00"));
    REQUIRE(program.parse("add.xyz vf26, vf26, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 0u);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_LOCAL);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "q_latency_already_local"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline reports insufficient Q latency hiding work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(!opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].loopCarriedQGapCycles < opportunities[0].qProducerLatency);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_INSUFFICIENT);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "insufficient_independent_cycles"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "insufficient_loop_carried_q_gap"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rewrites next-iteration load prefetches")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
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
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_memory"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_reads_induction"));
    const vcl::VuSoftwarePipelinePrefetch* lqPrefetch = findPrefetch(opportunities[0].softwarePipelinePrefetches, 2u);
    REQUIRE(lqPrefetch != NULL);
    CHECK(lqPrefetch->hasNextIterationOffset);
    CHECK(lqPrefetch->nextIterationOffset == 1);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    REQUIRE(plans[0].prefetchTokenIndices.size() == 1u);
    CHECK(plans[0].prefetchTokenIndices[0] == 2u);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int lqOffsetZeroCount = 0;
    unsigned int lqOffsetOneCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (vcl::normalizeVuMnemonic(i->name()) != "lq")
            continue;
        vcl::VuTokenResourceAccess access;
        REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
        if (access.hasMemoryOffset && access.memoryOffset == 0)
            ++lqOffsetZeroCount;
        if (access.hasMemoryOffset && access.memoryOffset == 1)
            ++lqOffsetOneCount;
    }
    CHECK(lqOffsetZeroCount == 1u);
    CHECK(lqOffsetOneCount == 1u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rewrites simple rotated registers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("mulq.xyz vf03, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf03, 0(vi02)"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 1"));
    REQUIRE(program.parse("ibne vi01, vi04, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    const vcl::VuSoftwarePipelineRotation* vf03Rotation = findRotation(opportunities[0].softwarePipelineRotations, "VF03");
    REQUIRE(vf03Rotation != NULL);
    CHECK(vf03Rotation->hasScratchRegister);
    CHECK(vf03Rotation->scratchRegister == "VF31");

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int mulWritesScratch = 0;
    unsigned int divReadsScratch = 0;
    unsigned int moveScratchToOriginal = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (!i->operand() || i->operand()->isPreprocessor())
            continue;
        vcl::VuTokenResourceAccess access;
        REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "mul" && hasString(access.registerWrites, "VF31.x"))
            ++mulWritesScratch;
        if (mnemonic == "div" && hasString(access.registerReads, "VF31.w"))
            ++divReadsScratch;
        if (mnemonic == "move"
            && hasString(access.registerReads, "VF31.x")
            && hasString(access.registerWrites, "VF03.x"))
            ++moveScratchToOriginal;
    }

    CHECK(mulWritesScratch == 1u);
    CHECK(divReadsScratch == 1u);
    CHECK(moveScratchToOriginal == 1u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rewrites multiple rotated Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf05, 1(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf03, q"));
    REQUIRE(program.parse("mulq.xy vf05, vf05, q"));
    REQUIRE(program.parse("add.xy vf20, vf21, vf22"));
    REQUIRE(program.parse("sub.xy vf23, vf24, vf25"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 2"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 2"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    CHECK(opportunities[0].softwarePipelineRotations.size() == 2u);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF03") != NULL);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF05") != NULL);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int moveCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (vcl::normalizeVuMnemonic(i->name()) == "move")
            ++moveCount;
    }
    CHECK(moveCount == 2u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline matches alias rotation bases")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xy temp_a, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xy temp_a, temp_a, q"));
    REQUIRE(program.parse("add.xy vf20, vf21, vf22"));
    REQUIRE(program.parse("sub.xy vf23, vf24, vf25"));
    REQUIRE(program.parse("max.xy vf26, vf27, vf28"));
    REQUIRE(program.parse("mini.xy vf29, vf30, vf31"));
    REQUIRE(program.parse("add.xy vf16, vf17, vf18"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    const vcl::VuSoftwarePipelineRotation* rotation = findRotation(opportunities[0].softwarePipelineRotations, "temp_a");
    REQUIRE(rotation != NULL);
    CHECK(rotation->hasScratchRegister);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline ignores non-prefetched Q consumer rotations")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf03, q"));
    REQUIRE(program.parse("lq.xy vf05, 1(vi01)"));
    REQUIRE(program.parse("mulq.xy vf05, vf05, q"));
    REQUIRE(program.parse("add.xy vf20, vf21, vf22"));
    REQUIRE(program.parse("sub.xy vf23, vf24, vf25"));
    REQUIRE(program.parse("max.xy vf26, vf27, vf28"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 2"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 2"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].softwarePipelineRotations.size() == 1u);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF03") != NULL);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF05") == NULL);
}

TEST_CASE("VuSchedulerAnalysis: multi-instruction prefetch reports suffix clobber blockers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf01, vf00"));
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
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_memory"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_reads_induction"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "prefetch_clobbers_suffix"));
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline emits simple Q live-out drain")
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
    REQUIRE(program.parse("after_lid:"));
    REQUIRE(program.parse("mulq.xyz vf04, vf05, q"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].qLiveOut);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "q_live_out"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].emitsDrain);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int prologLabels = 0;
    unsigned int drainLabels = 0;
    unsigned int divCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid__DRAIN")
            ++drainLabels;
        if (vcl::normalizeVuMnemonic(i->name()) == "div")
            ++divCount;
    }

    CHECK(prologLabels == 1u);
    CHECK(drainLabels == 1u);
    CHECK(divCount == 3u);
    CHECK(transformed.size() == program.tokenizer.tokens().size() + 3u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline blocks non-drainable Q live-out loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
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
    REQUIRE(program.parse("after_lid:"));
    REQUIRE(program.parse("mulq.xyz vf04, vf05, q"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].qLiveOut);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "q_live_out"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: pipeline opportunities expose all Q producers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].qProducerTokenIndex == 5u);
    REQUIRE(opportunities[0].qProducerTokenIndices.size() == 2u);
    CHECK(opportunities[0].qProducerTokenIndices[0] == 2u);
    CHECK(opportunities[0].qProducerTokenIndices[1] == 5u);
    REQUIRE(opportunities[0].qStages.size() == 2u);
    CHECK(opportunities[0].qStages[0].qProducerTokenIndex == 2u);
    REQUIRE(opportunities[0].qStages[0].qConsumerTokenIndices.size() == 1u);
    CHECK(opportunities[0].qStages[0].qConsumerTokenIndices[0] == 3u);
    CHECK(opportunities[0].qStages[0].qProducerLatency == 7u);
    CHECK(opportunities[0].qStages[0].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qStages[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].qStages[0].loopCarriedQGapCycles == 6u);
    CHECK(opportunities[0].qStages[0].qProducerInsertionGapCycles == 6u);
    CHECK(opportunities[0].qStages[0].qProducerInsertionGapDeficitCycles == 1u);
    CHECK(opportunities[0].qStages[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_INSUFFICIENT);
    CHECK(opportunities[0].qStages[1].qProducerTokenIndex == 5u);
    REQUIRE(opportunities[0].qStages[1].qConsumerTokenIndices.size() == 1u);
    CHECK(opportunities[0].qStages[1].qConsumerTokenIndices[0] == 6u);
    CHECK(opportunities[0].qStages[1].qProducerLatency == 7u);
    CHECK(opportunities[0].qStages[1].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qStages[1].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].qStages[1].loopCarriedQGapCycles == 6u);
    CHECK(opportunities[0].qStages[1].qProducerInsertionGapCycles == 6u);
    CHECK(opportunities[0].qStages[1].qProducerInsertionGapDeficitCycles == 1u);
    CHECK(opportunities[0].qStages[1].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_INSUFFICIENT);
    CHECK(opportunities[0].firstQConsumerTokenIndex == 3u);
    CHECK(opportunities[0].lastQConsumerTokenIndex == 6u);
    REQUIRE(opportunities[0].qConsumerTokenIndices.size() == 2u);
    CHECK(opportunities[0].qConsumerTokenIndices[0] == 3u);
    CHECK(opportunities[0].qConsumerTokenIndices[1] == 6u);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multiple_q_producers"));
}

TEST_CASE("VuSchedulerAnalysis: multi-Q stages contribute carried register sets")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("add.xyz vf20, vf21, vf22"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf20, vf20, q"));
    REQUIRE(program.parse("add.xyz vf30, vf20, vf00"));
    REQUIRE(program.parse("add.xyz vf24, vf25, vf26"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf24, vf24, q"));
    REQUIRE(program.parse("add.xyz vf31, vf24, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    REQUIRE(opportunities[0].qStages.size() == 2u);
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF20.x"));
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF24.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF20.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF24.x"));
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

TEST_CASE("VuLatencyTracker: shared readiness model tracks registers Q and bypasses")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("div q, vf06[w], vf07[w]"));
    REQUIRE(program.parse("mulq.xy vf08, vf09, q"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mtir vi01, vf10[x]"));

    std::vector<const vcl::Token*> tokens;
    for (std::list<vcl::Token>::const_iterator i = program.tokenizer.tokens().begin();
         i != program.tokenizer.tokens().end(); ++i)
        tokens.push_back(&*i);
    REQUIRE(tokens.size() == 6u);

    vcl::VuLatencyTracker tracker;
    tracker.recordWrites(*tokens[0], 0);
    CHECK(tracker.readHazardDelay(*tokens[1], NULL, 1) > 0);
    CHECK(tracker.readHazardDelay(*tokens[1], NULL, 5) == 0);

    tracker.reset();
    tracker.recordWrites(*tokens[2], 0);
    CHECK(tracker.qReadyCycle() == 8);
    CHECK(tracker.readHazardDelay(*tokens[3], NULL, 1) > 0);
    CHECK(tracker.readHazardDelay(*tokens[3], NULL, 8) == 0);

    tracker.reset();
    tracker.recordWrites(*tokens[4], 0);
    CHECK(tracker.readHazardDelay(*tokens[5], NULL, 1) == 0);
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

TEST_CASE("VuSchedulerAnalysis: ready issue slots expose latency padding cycles")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("mul.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("add.xy vf04, vf01, vf05"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuScheduledIssueSlot> slots = vcl::scheduleVuBasicBlockReadyIssueSlots(blocks[0]);
    REQUIRE(slots.size() == 6u);
    CHECK(slots[0].firstToken != NULL);
    CHECK(!slots[0].padding);
    CHECK(slots[0].paddingKind == vcl::VU_SCHEDULED_PADDING_NONE);
    CHECK(slots[0].issueCycle == 0u);
    CHECK(slots[0].cycleCount == 1u);
    for (unsigned int i = 1; i < 5; ++i)
    {
        CHECK(slots[i].firstToken == NULL);
        CHECK(slots[i].secondToken == NULL);
        CHECK(slots[i].padding);
        CHECK(slots[i].paddingKind == vcl::VU_SCHEDULED_PADDING_NOP);
        CHECK(slots[i].issueCycle == i);
        CHECK(slots[i].cycleCount == 1u);
    }
    REQUIRE(slots[5].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(slots[5].firstToken->name()) == "add");
    CHECK(!slots[5].padding);
    CHECK(slots[5].paddingKind == vcl::VU_SCHEDULED_PADDING_NONE);
    CHECK(slots[5].issueCycle == 5u);
    CHECK(slots[5].cycleCount == 1u);
}

TEST_CASE("VuSchedulerAnalysis: ready issue slots classify Q and P wait padding")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram qProgram;
    REQUIRE(qProgram.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(qProgram.parse("mulq.xy vf03, vf04, q"));

    std::vector<vcl::VuBasicBlock> qBlocks = vcl::buildVuBasicBlocks(qProgram.tokenizer.tokens());
    REQUIRE(qBlocks.size() == 1u);
    std::vector<vcl::VuScheduledIssueSlot> qSlots = vcl::scheduleVuBasicBlockReadyIssueSlots(qBlocks[0]);
    REQUIRE(qSlots.size() == 3u);
    CHECK(qSlots[1].padding);
    CHECK(qSlots[1].paddingKind == vcl::VU_SCHEDULED_PADDING_WAITQ);
    CHECK(qSlots[1].issueCycle == 1u);
    CHECK(qSlots[1].cycleCount == 7u);
    REQUIRE(qSlots[2].firstToken != NULL);
    CHECK(qSlots[2].issueCycle == 8u);
    CHECK(vcl::normalizeVuMnemonic(qSlots[2].firstToken->name()) == "mulq");

    vcl::Error::ResetErrorCount();
    ParsedProgram pProgram;
    REQUIRE(pProgram.parse("esin p, vf01[x]"));
    REQUIRE(pProgram.parse("mfp.xy vf02, p"));

    std::vector<vcl::VuBasicBlock> pBlocks = vcl::buildVuBasicBlocks(pProgram.tokenizer.tokens());
    REQUIRE(pBlocks.size() == 1u);
    std::vector<vcl::VuScheduledIssueSlot> pSlots = vcl::scheduleVuBasicBlockReadyIssueSlots(pBlocks[0]);
    REQUIRE(pSlots.size() == 3u);
    CHECK(pSlots[1].padding);
    CHECK(pSlots[1].paddingKind == vcl::VU_SCHEDULED_PADDING_WAITP);
    CHECK(pSlots[1].issueCycle == 1u);
    CHECK(pSlots[1].cycleCount == 29u);
    REQUIRE(pSlots[2].firstToken != NULL);
    CHECK(pSlots[2].issueCycle == 30u);
    CHECK(vcl::normalizeVuMnemonic(pSlots[2].firstToken->name()) == "mfp");
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

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler fills register latency with independent work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi02, vi00, 2"));

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
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
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
    REQUIRE(slots.size() == 6u);
    REQUIRE(slots[0].firstToken != 0);
    REQUIRE(slots[0].secondToken != 0);
    CHECK(vcl::normalizeVuMnemonic(slots[0].firstToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(slots[0].secondToken->name()) == "iaddiu");
    REQUIRE(slots[0].upperToken != 0);
    REQUIRE(slots[0].lowerToken != 0);
    CHECK(vcl::normalizeVuMnemonic(slots[0].upperToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(slots[0].lowerToken->name()) == "iaddiu");

    for (unsigned int i = 1; i < 5; ++i)
    {
        CHECK(slots[i].padding);
        CHECK(slots[i].paddingKind == vcl::VU_SCHEDULED_PADDING_NOP);
        CHECK(slots[i].issueCycle == i);
        CHECK(slots[i].cycleCount == 1u);
    }

    REQUIRE(slots[5].firstToken != 0);
    CHECK(slots[5].secondToken == 0);
    CHECK(vcl::normalizeVuMnemonic(slots[5].firstToken->name()) == "mul");
    REQUIRE(slots[5].upperToken != 0);
    CHECK(slots[5].lowerToken == 0);
    CHECK(vcl::normalizeVuMnemonic(slots[5].upperToken->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler tags explicit issue-slot pairs")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == 3u);

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) != 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) == 0);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) == 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) != 0);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) == 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) == 0);
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
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
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

TEST_CASE("VuSchedulerAnalysis: flag-liveness issue slots match ready-set scheduling")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("fmand vi01, vi02"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mul.xy vf11, vf10, vf00"));

    std::vector< std::vector<vcl::VuScheduledIssueSlot> > blockSlots =
        vcl::scheduleVuBasicBlocksReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(blockSlots.size() == 1u);
    REQUIRE(blockSlots[0].size() >= 2u);
    REQUIRE(blockSlots[0][0].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(blockSlots[0][0].firstToken->name()) == "fmand");
    REQUIRE(blockSlots[0][1].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(blockSlots[0][1].firstToken->name()) == "ftoi0");
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

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler fills Q latency before Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf04, q"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi02, vi00, 2"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "div");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mulq");
}

TEST_CASE("VuSchedulerAnalysis: latency-blocked Q consumers do not outrank ready independent work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf04, q"));
    REQUIRE(program.parse("add.xy vf05, vf03, vf06"));
    REQUIRE(program.parse("add.xy vf07, vf05, vf08"));
    REQUIRE(program.parse("lq.xy vf09, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf10, 1(vi01)"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::vector<std::string> names;
    for (std::list<vcl::Token>::const_iterator i = scheduled.begin(); i != scheduled.end(); ++i)
        names.push_back(vcl::normalizeVuMnemonic(i->name()));

    REQUIRE(names.size() == 7u);
    CHECK(names[0] == "div");
    CHECK(names[1] == "lq");
    CHECK(names[2] == "lq");
    CHECK(names[3] == "iaddiu");
    CHECK(names[4] == "mulq");
}
