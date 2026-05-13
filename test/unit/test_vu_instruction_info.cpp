#include "test_harness.h"

#include "../../src/VuInstructionInfo.h"

TEST_CASE("VuInstructionInfo: normalizes field and broadcast mnemonics")
{
    CHECK(vcl::normalizeVuMnemonic("mulw.xyz") == "mul");
    CHECK(vcl::normalizeVuMnemonic("maxx.w") == "max");
    CHECK(vcl::normalizeVuMnemonic("clipw.xyz") == "clipw");
    CHECK(vcl::normalizeVuMnemonic("nop[E]") == "nop");
}

TEST_CASE("VuInstructionInfo: exposes Q and P producer costs")
{
    const vcl::VuInstructionInfo* div = vcl::findVuInstructionInfo("div");
    REQUIRE(div != 0);
    CHECK(div->pipe == vcl::VU_PIPE_LOWER);
    CHECK(div->unit == vcl::VU_EXEC_FDIV);
    CHECK(div->throughput == 7);
    CHECK(div->latency == 7);
    CHECK((div->flags & vcl::VU_INSTR_WRITES_Q) != 0);

    const vcl::VuInstructionInfo* ersqrt = vcl::findVuInstructionInfo("ersqrt");
    REQUIRE(ersqrt != 0);
    CHECK(ersqrt->unit == vcl::VU_EXEC_EFU);
    CHECK(ersqrt->throughput == 17);
    CHECK(ersqrt->latency == 18);
    CHECK((ersqrt->flags & vcl::VU_INSTR_WRITES_P) != 0);
}

TEST_CASE("VuInstructionInfo: MFP consumes P but does not produce P")
{
    const vcl::VuInstructionInfo* mfp = vcl::findVuInstructionInfo("mfp");
    REQUIRE(mfp != 0);
    CHECK(mfp->pipe == vcl::VU_PIPE_LOWER);
    CHECK(mfp->unit == vcl::VU_EXEC_EFU);
    CHECK(mfp->throughput == 1);
    CHECK(mfp->latency == 4);
    CHECK((mfp->flags & vcl::VU_INSTR_WRITES_P) == 0);
}

TEST_CASE("VuInstructionInfo: wait and branch metadata is explicit")
{
    const vcl::VuInstructionInfo* waitq = vcl::findVuInstructionInfo("waitq");
    REQUIRE(waitq != 0);
    CHECK((waitq->flags & vcl::VU_INSTR_WAIT_Q) != 0);

    const vcl::VuInstructionInfo* waitp = vcl::findVuInstructionInfo("waitp");
    REQUIRE(waitp != 0);
    CHECK((waitp->flags & vcl::VU_INSTR_WAIT_P) != 0);

    const vcl::VuInstructionInfo* branch = vcl::findVuInstructionInfo("ibne");
    REQUIRE(branch != 0);
    CHECK(branch->unit == vcl::VU_EXEC_BRU);
    CHECK(branch->latency == 2);
    CHECK((branch->flags & vcl::VU_INSTR_BRANCH) != 0);
}

