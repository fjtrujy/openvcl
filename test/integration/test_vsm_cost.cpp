#include "test_harness.h"
#include "openvcl_runner.h"

#include <string>
#include <vector>

namespace
{
    const char* kMixedSpacingVsm =
        "\t\t.vu\n"
        "\t\t.align 4\n"
        "entry_lid:\n"
        "                    nop                             lq VF01, 0(VI00)\n"
        "                    add.xyz VF02, VF01, VF00        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n"
        "                    nop                             waitq\n"
        "                    nop[E]                          b entry_lid\n";

    const char* kSceStyleVsm =
        "\t\t.vu\n"
        "\t\t.align 4\n"
        "sce_style_lid:\n"
        "         NOP                                                        lq            VF01,62(VI00)                       \n"
        "         mul.xyz       VF09,VF09,VF08                               iaddiu        VI04,VI05,0x00000005                \n";

    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

TEST_CASE("VSM cost: human report counts scheduled cycles and slots")
{
    std::vector<std::string> args;
    args.push_back("--cost");

    ::test::RunResult r = ::test::run_openvcl(args, kMixedSpacingVsm);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "VSM cost report"));
    CHECK(contains(r.stdout_data, "static_cycles: 5"));
    CHECK(contains(r.stdout_data, "instructions: 5"));
    CHECK(contains(r.stdout_data, "upper_instructions: 1"));
    CHECK(contains(r.stdout_data, "lower_instructions: 4"));
    CHECK(contains(r.stdout_data, "paired_cycles: 1"));
    CHECK(contains(r.stdout_data, "single_lower_cycles: 3"));
    CHECK(contains(r.stdout_data, "nop_only_cycles: 1"));
    CHECK(contains(r.stdout_data, "nop_slots: 5"));
    CHECK(contains(r.stdout_data, "branch_cycles: 1"));
    CHECK(contains(r.stdout_data, "waitq_cycles: 1"));
    CHECK(contains(r.stdout_data, "e_bit_cycles: 1"));
    CHECK(contains(r.stdout_data, "operation_latency_cycles: 12"));
    CHECK(contains(r.stdout_data, "long_latency_ops: 3"));
    CHECK(contains(r.stdout_data, "long_latency_cycles: 7"));
    CHECK(contains(r.stdout_data, "max_op_latency: 4"));
    CHECK(contains(r.stdout_data, "entry_lid: cycles=5"));
}

TEST_CASE("VSM cost: JSON report is suitable for comparisons")
{
    std::vector<std::string> args;
    args.push_back("--cost-json");

    ::test::RunResult r = ::test::run_openvcl(args, kMixedSpacingVsm);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "\"static_cycles\": 5"));
    CHECK(contains(r.stdout_data, "\"instructions\": 5"));
    CHECK(contains(r.stdout_data, "\"paired_cycles\": 1"));
    CHECK(contains(r.stdout_data, "\"operation_latency_cycles\": 12"));
    CHECK(contains(r.stdout_data, "\"long_latency_cycles\": 7"));
    CHECK(contains(r.stdout_data, "\"label\": \"entry_lid\""));
}

TEST_CASE("VSM cost: accepts SCE-style padded VSM output")
{
    std::vector<std::string> args;
    args.push_back("--cost");

    ::test::RunResult r = ::test::run_openvcl(args, kSceStyleVsm);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "static_cycles: 2"));
    CHECK(contains(r.stdout_data, "instructions: 3"));
    CHECK(contains(r.stdout_data, "upper_instructions: 1"));
    CHECK(contains(r.stdout_data, "lower_instructions: 2"));
    CHECK(contains(r.stdout_data, "paired_cycles: 1"));
    CHECK(contains(r.stdout_data, "operation_latency_cycles: 9"));
    CHECK(contains(r.stdout_data, "long_latency_ops: 2"));
    CHECK(contains(r.stdout_data, "sce_style_lid: cycles=2"));
}
