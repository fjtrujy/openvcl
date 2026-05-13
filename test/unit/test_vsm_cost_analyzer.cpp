#include "test_harness.h"

#include "../../src/VsmCostAnalyzer.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef OPENVCL_TEST_VSM_FIXTURES
#error "OPENVCL_TEST_VSM_FIXTURES must point at test/fixtures/vsm_cost"
#endif

namespace
{
    std::string fixturePath(const std::string& name)
    {
        return std::string(OPENVCL_TEST_VSM_FIXTURES) + "/" + name;
    }

    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    int textMetric(const std::string& report, const std::string& name)
    {
        std::string key = name + ": ";
        std::string::size_type pos = report.find(key);
        if( pos == std::string::npos )
            return -1;
        pos += key.size();
        return std::atoi(report.c_str() + pos);
    }

    int jsonMetric(const std::string& report, const std::string& name)
    {
        std::string key = "\"" + name + "\": ";
        std::string::size_type pos = report.find(key);
        if( pos == std::string::npos )
            return -1;
        pos += key.size();
        return std::atoi(report.c_str() + pos);
    }

    std::string analyzeTextFile(const std::string& name)
    {
        std::ifstream input(fixturePath(name).c_str());
        if( !input.good() )
            return std::string();

        std::ostringstream output;
        vcl::VsmCostAnalyzer analyzer;
        if( !analyzer.analyze(input, name) )
            return std::string();
        if( !analyzer.writeText(output) )
            return std::string();
        return output.str();
    }

    std::string analyzeJsonFile(const std::string& name)
    {
        std::ifstream input(fixturePath(name).c_str());
        if( !input.good() )
            return std::string();

        std::ostringstream output;
        vcl::VsmCostAnalyzer analyzer;
        if( !analyzer.analyze(input, name) )
            return std::string();
        if( !analyzer.writeJson(output) )
            return std::string();
        return output.str();
    }

    bool analyzeString(const std::string& source, const std::string& name, vcl::VsmCostAnalyzer& analyzer)
    {
        std::istringstream input(source);
        return analyzer.analyze(input, name);
    }
}

TEST_CASE("VsmCostAnalyzer fixture: simple scheduled VSM has precomputed issue cost")
{
    std::string report = analyzeTextFile("simple_scheduled.vsm");
    REQUIRE(report.length() > 0);
    CHECK(textMetric(report, "static_cycles") == 5);
    CHECK(textMetric(report, "instruction_slots") == 10);
    CHECK(textMetric(report, "instructions") == 5);
    CHECK(textMetric(report, "upper_instructions") == 1);
    CHECK(textMetric(report, "lower_instructions") == 4);
    CHECK(textMetric(report, "paired_cycles") == 1);
    CHECK(textMetric(report, "single_upper_cycles") == 0);
    CHECK(textMetric(report, "single_lower_cycles") == 3);
    CHECK(textMetric(report, "nop_only_cycles") == 1);
    CHECK(textMetric(report, "nop_slots") == 5);
    CHECK(textMetric(report, "branch_cycles") == 1);
    CHECK(textMetric(report, "waitq_cycles") == 1);
    CHECK(textMetric(report, "e_bit_cycles") == 1);
    CHECK(textMetric(report, "operation_latency_cycles") == 12);
    CHECK(textMetric(report, "long_latency_ops") == 3);
    CHECK(textMetric(report, "long_latency_cycles") == 7);
    CHECK(textMetric(report, "max_op_latency") == 4);
    CHECK(contains(report, "entry_lid: cycles=5"));
    CHECK(contains(report, "nop_slots=5"));
}

TEST_CASE("VsmCostAnalyzer fixture: JSON exposes the same precomputed cost")
{
    std::string report = analyzeJsonFile("simple_scheduled.vsm");
    REQUIRE(report.length() > 0);
    CHECK(contains(report, "\"input\": \"simple_scheduled.vsm\""));
    CHECK(jsonMetric(report, "static_cycles") == 5);
    CHECK(jsonMetric(report, "instructions") == 5);
    CHECK(jsonMetric(report, "paired_cycles") == 1);
    CHECK(jsonMetric(report, "operation_latency_cycles") == 12);
    CHECK(jsonMetric(report, "long_latency_cycles") == 7);
    CHECK(contains(report, "\"label\": \"entry_lid\""));
    CHECK(contains(report, "\"nop_slots\": 5"));
}

TEST_CASE("VsmCostAnalyzer fixture: block repeats expose weighted cost")
{
    std::ifstream input(fixturePath("simple_scheduled.vsm").c_str());
    REQUIRE(input.good());

    vcl::VsmCostAnalyzer analyzer;
    REQUIRE(analyzer.analyze(input, "simple_scheduled.vsm"));
    analyzer.setBlockRepeat("entry_lid", 4);

    std::ostringstream text;
    REQUIRE(analyzer.writeText(text));
    CHECK(textMetric(text.str(), "static_cycles") == 5);
    CHECK(textMetric(text.str(), "weighted_static_cycles") == 20);
    CHECK(textMetric(text.str(), "weighted_instruction_slots") == 40);
    CHECK(textMetric(text.str(), "weighted_instructions") == 20);
    CHECK(textMetric(text.str(), "weighted_paired_cycles") == 4);
    CHECK(textMetric(text.str(), "weighted_nop_only_cycles") == 4);
    CHECK(contains(text.str(), "entry_lid: cycles=5 repeat=4 weighted_cycles=20"));
    CHECK(contains(text.str(), "top_weighted_blocks:"));
    CHECK(contains(text.str(), "entry_lid: weighted_cycles=20 cycles=5 repeat=4"));
    CHECK(contains(text.str(), "top_weighted_estimated_blocks:"));
    CHECK(contains(text.str(), "entry_lid: weighted_estimated_cycles=20 estimated_cycles=5 repeat=4"));
    CHECK(contains(text.str(), "weighted_nop_slots=20"));
    CHECK(contains(text.str(), "top_weighted_idle_blocks:"));
    CHECK(contains(text.str(), "entry_lid: weighted_nop_slots=20 nop_slots=5 repeat=4"));

    std::ostringstream json;
    REQUIRE(analyzer.writeJson(json));
    CHECK(jsonMetric(json.str(), "weighted_static_cycles") == 20);
    CHECK(jsonMetric(json.str(), "weighted_instructions") == 20);
    CHECK(contains(json.str(), "\"repeat\": 4"));
    CHECK(contains(json.str(), "\"weighted_cycles\": 20"));
    CHECK(contains(json.str(), "\"weighted_nop_slots\": 20"));
    CHECK(contains(json.str(), "\"top_weighted_blocks\""));
    CHECK(contains(json.str(), "\"top_weighted_estimated_blocks\""));
    CHECK(contains(json.str(), "\"top_weighted_idle_blocks\""));
}

TEST_CASE("VsmCostAnalyzer inline: ps2gl preset labels match SCE optimized main loops")
{
    const std::string source =
        "\t.vu\n"
        "EXPL_vu1_general_quad_pp4_vcl_xform_loop_lid__MAIN_LOOP:\n"
        "                    add.xyz VF01, VF02, VF03        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n";

    vcl::VsmCostAnalyzer analyzer;
    REQUIRE(analyzeString(source, "sce_loop_label_inline.vsm", analyzer));
    analyzer.setBlockRepeat("xform_loop_lid", 4);

    std::ostringstream text;
    REQUIRE(analyzer.writeText(text));
    CHECK(textMetric(text.str(), "static_cycles") == 2);
    CHECK(textMetric(text.str(), "weighted_static_cycles") == 8);
    CHECK(contains(text.str(), "EXPL_vu1_general_quad_pp4_vcl_xform_loop_lid__MAIN_LOOP: cycles=2 repeat=4 weighted_cycles=8"));
}

TEST_CASE("VsmCostAnalyzer fixture: summary exposes comparison metrics")
{
    std::ifstream input(fixturePath("simple_scheduled.vsm").c_str());
    REQUIRE(input.good());

    vcl::VsmCostAnalyzer analyzer;
    REQUIRE(analyzer.analyze(input, "simple_scheduled.vsm"));
    analyzer.setBlockRepeat("entry_lid", 4);

    vcl::VsmCostAnalyzer::Summary summary = analyzer.summary();
    CHECK(summary.input == "simple_scheduled.vsm");
    CHECK(summary.staticCycles == 5);
    CHECK(summary.estimatedTotalCycles == 5);
    CHECK(summary.weightedStaticCycles == 20);
    CHECK(summary.weightedEstimatedTotalCycles == 20);
    CHECK(summary.weightedInstructions == 20);
    CHECK(summary.weightedPairedCycles == 4);
    CHECK(summary.weightedNopSlots == 20);
    CHECK(summary.instructions == 5);
    CHECK(summary.pairedCycles == 1);
    CHECK(summary.nopSlots == 5);
}

TEST_CASE("VsmCostAnalyzer inline: SCE main loop labels compare against OpenVCL labels")
{
    const std::string baselineSource =
        "\t.vu\n"
        "EXPL_vu1_general_quad_pp4_vcl_xform_loop_lid__MAIN_LOOP:\n"
        "                    nop                             nop\n";

    const std::string candidateSource =
        "\t.vu\n"
        "xform_loop_lid:\n"
        "                    nop                             nop\n"
        "                    nop                             nop\n";

    vcl::VsmCostAnalyzer baseline;
    REQUIRE(analyzeString(baselineSource, "sce_inline.vsm", baseline));
    baseline.setBlockRepeat("xform_loop_lid", 3);

    vcl::VsmCostAnalyzer candidate;
    REQUIRE(analyzeString(candidateSource, "openvcl_inline.vsm", candidate));
    candidate.setBlockRepeat("xform_loop_lid", 3);

    std::ostringstream text;
    REQUIRE(vcl::VsmCostAnalyzer::writeComparisonText(text, baseline, candidate));
    CHECK(contains(text.str(), "xform_loop_lid: baseline_weighted_estimated_cycles=3 candidate_weighted_estimated_cycles=6 delta=3"));
    CHECK(contains(text.str(), "baseline_repeat=3 candidate_repeat=3"));

    std::ostringstream json;
    REQUIRE(vcl::VsmCostAnalyzer::writeComparisonJson(json, baseline, candidate));
    CHECK(contains(json.str(), "\"label\": \"xform_loop_lid\", \"baseline_weighted_estimated_cycles\": 3, \"candidate_weighted_estimated_cycles\": 6, \"delta_weighted_estimated_cycles\": 3"));
}

TEST_CASE("VsmCostAnalyzer inline: comparison exposes top block deltas")
{
    const std::string baselineSource =
        "\t.vu\n"
        "hot_lid:\n"
        "                    nop                             div q, VF01w, VF02w\n"
        "                    nop                             waitq\n"
        "idle_lid:\n"
        "                    nop                             nop\n"
        "                    nop                             nop\n";

    const std::string candidateSource =
        "\t.vu\n"
        "hot_lid:\n"
        "                    nop                             div q, VF01w, VF02w\n"
        "                    nop                             iaddiu VI01, VI00, 1\n"
        "                    nop                             waitq\n"
        "idle_lid:\n"
        "                    add.xyz VF01, VF01, VF00        iaddiu VI02, VI00, 1\n";

    vcl::VsmCostAnalyzer baseline;
    REQUIRE(analyzeString(baselineSource, "baseline_inline.vsm", baseline));
    baseline.setBlockRepeat("hot_lid", 3);
    baseline.setBlockRepeat("idle_lid", 2);

    vcl::VsmCostAnalyzer candidate;
    REQUIRE(analyzeString(candidateSource, "candidate_inline.vsm", candidate));
    candidate.setBlockRepeat("hot_lid", 3);
    candidate.setBlockRepeat("idle_lid", 2);

    std::ostringstream text;
    REQUIRE(vcl::VsmCostAnalyzer::writeComparisonText(text, baseline, candidate));
    CHECK(contains(text.str(), "top_weighted_estimated_blocks:"));
    CHECK(contains(text.str(), "idle_lid: baseline_weighted_estimated_cycles=4 candidate_weighted_estimated_cycles=2 delta=-2"));
    CHECK(contains(text.str(), "idle_lid: baseline_weighted_nop_slots=8 candidate_weighted_nop_slots=0 delta=-8"));
    CHECK(contains(text.str(), "top_weighted_wait_blocks:"));
    CHECK(contains(text.str(), "hot_lid: baseline_weighted_wait_stall_cycles=18 candidate_weighted_wait_stall_cycles=15 delta=-3"));

    std::ostringstream json;
    REQUIRE(vcl::VsmCostAnalyzer::writeComparisonJson(json, baseline, candidate));
    CHECK(contains(json.str(), "\"top_weighted_estimated_blocks\""));
    CHECK(contains(json.str(), "\"label\": \"idle_lid\", \"baseline_weighted_estimated_cycles\": 4, \"candidate_weighted_estimated_cycles\": 2, \"delta_weighted_estimated_cycles\": -2"));
    CHECK(contains(json.str(), "\"top_weighted_idle_blocks\""));
    CHECK(contains(json.str(), "\"label\": \"idle_lid\", \"baseline_weighted_nop_slots\": 8, \"candidate_weighted_nop_slots\": 0, \"delta_weighted_nop_slots\": -8"));
    CHECK(contains(json.str(), "\"top_weighted_wait_blocks\""));
    CHECK(contains(json.str(), "\"label\": \"hot_lid\", \"baseline_weighted_wait_stall_cycles\": 18, \"candidate_weighted_wait_stall_cycles\": 15, \"delta_weighted_wait_stall_cycles\": -3"));

    std::ostringstream markdown;
    REQUIRE(vcl::VsmCostAnalyzer::writeComparisonMarkdown(markdown, baseline, candidate));
    CHECK(contains(markdown.str(), "| baseline | candidate | baseline static | candidate static | static delta |"));
    CHECK(contains(markdown.str(), "| baseline_inline.vsm | candidate_inline.vsm |"));
    CHECK(contains(markdown.str(), " | 4 | 4 | 0 |"));
    CHECK(contains(markdown.str(), " | 10 | 9 | -1 |"));
    CHECK(contains(markdown.str(), " | 0.90x |"));
    CHECK(contains(markdown.str(), " | 10 | 11 | +1 | 28 | 26 | -2 | 0.93x |"));
    CHECK(contains(markdown.str(), " | 6 | 5 | -1 |"));
}

TEST_CASE("VsmCostAnalyzer fixture: SCE padded columns have precomputed cost")
{
    std::string report = analyzeTextFile("sce_padded_columns.vsm");
    REQUIRE(report.length() > 0);
    CHECK(textMetric(report, "static_cycles") == 2);
    CHECK(textMetric(report, "instruction_slots") == 4);
    CHECK(textMetric(report, "instructions") == 3);
    CHECK(textMetric(report, "upper_instructions") == 1);
    CHECK(textMetric(report, "lower_instructions") == 2);
    CHECK(textMetric(report, "paired_cycles") == 1);
    CHECK(textMetric(report, "single_lower_cycles") == 1);
    CHECK(textMetric(report, "nop_only_cycles") == 0);
    CHECK(textMetric(report, "nop_slots") == 1);
    CHECK(textMetric(report, "operation_latency_cycles") == 9);
    CHECK(textMetric(report, "long_latency_ops") == 2);
    CHECK(textMetric(report, "long_latency_cycles") == 6);
    CHECK(textMetric(report, "max_op_latency") == 4);
    CHECK(contains(report, "sce_style_lid: cycles=2"));
}

TEST_CASE("VsmCostAnalyzer fixture: long-latency operations are weighted differently")
{
    std::string report = analyzeTextFile("long_latency_weighted.vsm");
    REQUIRE(report.length() > 0);
    CHECK(textMetric(report, "static_cycles") == 4);
    CHECK(textMetric(report, "instruction_slots") == 8);
    CHECK(textMetric(report, "instructions") == 7);
    CHECK(textMetric(report, "upper_instructions") == 3);
    CHECK(textMetric(report, "lower_instructions") == 4);
    CHECK(textMetric(report, "paired_cycles") == 3);
    CHECK(textMetric(report, "single_lower_cycles") == 1);
    CHECK(textMetric(report, "nop_slots") == 1);
    CHECK(textMetric(report, "waitq_cycles") == 1);
    CHECK(textMetric(report, "waitp_cycles") == 1);
    CHECK(textMetric(report, "fdiv_ops") == 1);
    CHECK(textMetric(report, "efu_ops") == 1);
    CHECK(textMetric(report, "operation_latency_cycles") == 50);
    CHECK(textMetric(report, "long_latency_ops") == 5);
    CHECK(textMetric(report, "long_latency_cycles") == 43);
    CHECK(textMetric(report, "max_op_latency") == 29);
    CHECK(contains(report, "weighted_lid: cycles=4"));
}

TEST_CASE("VsmCostAnalyzer inline: wait instructions add estimated stall cycles")
{
    const std::string source =
        "\t.vu\n"
        "wait_lid:\n"
        "                    nop                             div q, VF01w, VF02w\n"
        "                    nop                             waitq\n"
        "                    nop                             esadd p, VF03\n"
        "                    nop                             waitp\n";

    std::istringstream input(source);
    vcl::VsmCostAnalyzer analyzer;
    REQUIRE(analyzer.analyze(input, "inline_waits.vsm"));
    analyzer.setBlockRepeat("wait_lid", 3);

    std::ostringstream text;
    REQUIRE(analyzer.writeText(text));
    CHECK(textMetric(text.str(), "static_cycles") == 4);
    CHECK(textMetric(text.str(), "estimated_total_cycles") == 20);
    CHECK(textMetric(text.str(), "wait_stall_cycles") == 16);
    CHECK(textMetric(text.str(), "waitq_stall_cycles") == 6);
    CHECK(textMetric(text.str(), "waitp_stall_cycles") == 10);
    CHECK(textMetric(text.str(), "weighted_static_cycles") == 12);
    CHECK(textMetric(text.str(), "weighted_estimated_total_cycles") == 60);
    CHECK(textMetric(text.str(), "weighted_wait_stall_cycles") == 48);
    CHECK(contains(text.str(), "wait_lid: cycles=4 repeat=3 weighted_cycles=12"));
    CHECK(contains(text.str(), "wait_stall=16 waitq_stall=6 waitp_stall=10 estimated_cycles=20"));
    CHECK(contains(text.str(), "top_weighted_wait_blocks:"));
    CHECK(contains(text.str(), "wait_lid: weighted_wait_stall=48 wait_stall=16 repeat=3"));
    CHECK(contains(text.str(), "top_weighted_estimated_blocks:"));
    CHECK(contains(text.str(), "wait_lid: weighted_estimated_cycles=60 estimated_cycles=20 repeat=3"));

    std::ostringstream json;
    REQUIRE(analyzer.writeJson(json));
    CHECK(jsonMetric(json.str(), "estimated_total_cycles") == 20);
    CHECK(jsonMetric(json.str(), "wait_stall_cycles") == 16);
    CHECK(jsonMetric(json.str(), "weighted_estimated_total_cycles") == 60);
    CHECK(jsonMetric(json.str(), "weighted_wait_stall_cycles") == 48);
    CHECK(contains(json.str(), "\"top_weighted_estimated_blocks\""));
    CHECK(contains(json.str(), "\"top_weighted_wait_blocks\""));
}

TEST_CASE("VsmCostAnalyzer inline: EFU producer throughput adds issue stalls")
{
    const std::string source =
        "\t.vu\n"
        "efu_lid:\n"
        "                    nop                             esadd p, VF01\n"
        "                    nop                             esadd p, VF02\n"
        "                    nop                             waitp\n"
        "                    nop                             mfp.w VF03, p\n";

    std::istringstream input(source);
    vcl::VsmCostAnalyzer analyzer;
    REQUIRE(analyzer.analyze(input, "inline_efu_issue_stalls.vsm"));
    analyzer.setBlockRepeat("efu_lid", 2);

    std::ostringstream text;
    REQUIRE(analyzer.writeText(text));
    CHECK(textMetric(text.str(), "static_cycles") == 4);
    CHECK(textMetric(text.str(), "estimated_total_cycles") == 23);
    CHECK(textMetric(text.str(), "issue_stall_cycles") == 9);
    CHECK(textMetric(text.str(), "fdiv_issue_stall_cycles") == 0);
    CHECK(textMetric(text.str(), "efu_issue_stall_cycles") == 9);
    CHECK(textMetric(text.str(), "wait_stall_cycles") == 10);
    CHECK(textMetric(text.str(), "waitp_stall_cycles") == 10);
    CHECK(textMetric(text.str(), "weighted_estimated_total_cycles") == 46);
    CHECK(textMetric(text.str(), "weighted_issue_stall_cycles") == 18);
    CHECK(textMetric(text.str(), "weighted_wait_stall_cycles") == 20);
    CHECK(contains(text.str(), "efu_lid: cycles=4 repeat=2 weighted_cycles=8"));
    CHECK(contains(text.str(), "issue_stall=9 fdiv_issue_stall=0 efu_issue_stall=9"));
    CHECK(contains(text.str(), "estimated_cycles=23"));

    std::ostringstream json;
    REQUIRE(analyzer.writeJson(json));
    CHECK(jsonMetric(json.str(), "estimated_total_cycles") == 23);
    CHECK(jsonMetric(json.str(), "issue_stall_cycles") == 9);
    CHECK(jsonMetric(json.str(), "efu_issue_stall_cycles") == 9);
    CHECK(jsonMetric(json.str(), "weighted_estimated_total_cycles") == 46);
    CHECK(jsonMetric(json.str(), "weighted_issue_stall_cycles") == 18);
    CHECK(contains(json.str(), "\"issue_stall_cycles\": 9"));
}

TEST_CASE("VsmCostAnalyzer inline: MFP reads P without starting a new P producer")
{
    const std::string source =
        "\t.vu\n"
        "mfp_lid:\n"
        "                    nop                             esadd p, VF01\n"
        "                    nop                             waitp\n"
        "                    nop                             mfp.w VF02, p\n"
        "                    nop                             waitp\n";

    std::istringstream input(source);
    vcl::VsmCostAnalyzer analyzer;
    REQUIRE(analyzer.analyze(input, "inline_mfp_reads_p.vsm"));

    std::ostringstream text;
    REQUIRE(analyzer.writeText(text));
    CHECK(textMetric(text.str(), "static_cycles") == 4);
    CHECK(textMetric(text.str(), "estimated_total_cycles") == 14);
    CHECK(textMetric(text.str(), "issue_stall_cycles") == 0);
    CHECK(textMetric(text.str(), "wait_stall_cycles") == 10);
    CHECK(textMetric(text.str(), "waitp_stall_cycles") == 10);
    CHECK(contains(text.str(), "mfp_lid: cycles=4"));
    CHECK(contains(text.str(), "wait_stall=10 waitq_stall=0 waitp_stall=10 estimated_cycles=14"));
}
