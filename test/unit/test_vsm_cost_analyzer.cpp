#include "test_harness.h"

#include "../../src/VsmCostAnalyzer.h"

#include <sstream>
#include <string>

namespace
{
    const char* kSimpleScheduledVsm =
        "\t\t.vu\n"
        "\t\t.align 4\n"
        "unit_lid:\n"
        "                    nop                             lq VF01, 0(VI00)\n"
        "                    add.xyz VF02, VF01, VF00        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n"
        "                    nop                             waitq\n"
        "                    nop[E]                          b unit_lid\n";

    const char* kScePaddedVsm =
        "sce_style_lid:\n"
        "         NOP                                                        lq            VF01,62(VI00)                       \n"
        "         mul.xyz       VF09,VF09,VF08                               iaddiu        VI04,VI05,0x00000005                \n";

    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    std::string analyzeText(const char* source)
    {
        std::istringstream input(source);
        std::ostringstream output;
        vcl::VsmCostAnalyzer analyzer;
        if( !analyzer.analyze(input, "unit.vsm") )
            return std::string();
        if( !analyzer.writeText(output) )
            return std::string();
        return output.str();
    }

    std::string analyzeJson(const char* source)
    {
        std::istringstream input(source);
        std::ostringstream output;
        vcl::VsmCostAnalyzer analyzer;
        if( !analyzer.analyze(input, "unit.vsm") )
            return std::string();
        if( !analyzer.writeJson(output) )
            return std::string();
        return output.str();
    }
}

TEST_CASE("VsmCostAnalyzer unit: counts scheduled issue slots")
{
    std::string report = analyzeText(kSimpleScheduledVsm);
    CHECK(contains(report, "static_cycles: 5"));
    CHECK(contains(report, "instructions: 5"));
    CHECK(contains(report, "upper_instructions: 1"));
    CHECK(contains(report, "lower_instructions: 4"));
    CHECK(contains(report, "paired_cycles: 1"));
    CHECK(contains(report, "single_lower_cycles: 3"));
    CHECK(contains(report, "nop_only_cycles: 1"));
    CHECK(contains(report, "nop_slots: 5"));
    CHECK(contains(report, "branch_cycles: 1"));
    CHECK(contains(report, "waitq_cycles: 1"));
    CHECK(contains(report, "e_bit_cycles: 1"));
    CHECK(contains(report, "operation_latency_cycles: 12"));
    CHECK(contains(report, "long_latency_ops: 3"));
    CHECK(contains(report, "long_latency_cycles: 7"));
    CHECK(contains(report, "max_op_latency: 4"));
}

TEST_CASE("VsmCostAnalyzer unit: emits JSON summary")
{
    std::string report = analyzeJson(kSimpleScheduledVsm);
    CHECK(contains(report, "\"input\": \"unit.vsm\""));
    CHECK(contains(report, "\"static_cycles\": 5"));
    CHECK(contains(report, "\"instructions\": 5"));
    CHECK(contains(report, "\"paired_cycles\": 1"));
    CHECK(contains(report, "\"operation_latency_cycles\": 12"));
    CHECK(contains(report, "\"long_latency_cycles\": 7"));
    CHECK(contains(report, "\"label\": \"unit_lid\""));
}

TEST_CASE("VsmCostAnalyzer unit: parses SCE padded columns")
{
    std::string report = analyzeText(kScePaddedVsm);
    CHECK(contains(report, "static_cycles: 2"));
    CHECK(contains(report, "instructions: 3"));
    CHECK(contains(report, "upper_instructions: 1"));
    CHECK(contains(report, "lower_instructions: 2"));
    CHECK(contains(report, "paired_cycles: 1"));
    CHECK(contains(report, "operation_latency_cycles: 9"));
    CHECK(contains(report, "long_latency_ops: 2"));
    CHECK(contains(report, "sce_style_lid: cycles=2"));
}
