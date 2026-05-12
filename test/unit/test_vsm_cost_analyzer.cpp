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
