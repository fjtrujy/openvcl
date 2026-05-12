#include "test_harness.h"
#include "openvcl_runner.h"

#include <cstdlib>
#include <string>
#include <vector>

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

    ::test::RunResult runCost(const std::string& fixture)
    {
        std::vector<std::string> args;
        args.push_back("--cost");
        args.push_back(fixturePath(fixture));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostJson(const std::string& fixture)
    {
        std::vector<std::string> args;
        args.push_back("--cost-json");
        args.push_back(fixturePath(fixture));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostLoop(const std::string& fixture, const std::string& loop)
    {
        std::vector<std::string> args;
        args.push_back("--cost");
        args.push_back("--cost-loop");
        args.push_back(loop);
        args.push_back(fixturePath(fixture));
        return ::test::run_openvcl(args, "");
    }
}

TEST_CASE("VSM cost CLI: fixture file reports precomputed scheduled cost")
{
    ::test::RunResult r = runCost("simple_scheduled.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "VSM cost report"));
    CHECK(textMetric(r.stdout_data, "static_cycles") == 5);
    CHECK(textMetric(r.stdout_data, "instructions") == 5);
    CHECK(textMetric(r.stdout_data, "upper_instructions") == 1);
    CHECK(textMetric(r.stdout_data, "lower_instructions") == 4);
    CHECK(textMetric(r.stdout_data, "paired_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "single_lower_cycles") == 3);
    CHECK(textMetric(r.stdout_data, "nop_only_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "nop_slots") == 5);
    CHECK(textMetric(r.stdout_data, "branch_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "waitq_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "e_bit_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "operation_latency_cycles") == 12);
    CHECK(textMetric(r.stdout_data, "long_latency_ops") == 3);
    CHECK(textMetric(r.stdout_data, "long_latency_cycles") == 7);
    CHECK(textMetric(r.stdout_data, "max_op_latency") == 4);
    CHECK(contains(r.stdout_data, "entry_lid: cycles=5"));
    CHECK(contains(r.stdout_data, "nop_slots=5"));
}

TEST_CASE("VSM cost CLI: JSON fixture report is suitable for comparisons")
{
    ::test::RunResult r = runCostJson("simple_scheduled.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(jsonMetric(r.stdout_data, "static_cycles") == 5);
    CHECK(jsonMetric(r.stdout_data, "instructions") == 5);
    CHECK(jsonMetric(r.stdout_data, "paired_cycles") == 1);
    CHECK(jsonMetric(r.stdout_data, "operation_latency_cycles") == 12);
    CHECK(jsonMetric(r.stdout_data, "long_latency_cycles") == 7);
    CHECK(contains(r.stdout_data, "\"label\": \"entry_lid\""));
    CHECK(contains(r.stdout_data, "\"nop_slots\": 5"));
}

TEST_CASE("VSM cost CLI: loop repeat weights static block cost")
{
    ::test::RunResult r = runCostLoop("simple_scheduled.vsm", "entry_lid=4");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 5);
    CHECK(textMetric(r.stdout_data, "weighted_static_cycles") == 20);
    CHECK(textMetric(r.stdout_data, "weighted_instructions") == 20);
    CHECK(textMetric(r.stdout_data, "weighted_paired_cycles") == 4);
    CHECK(contains(r.stdout_data, "entry_lid: cycles=5 repeat=4 weighted_cycles=20"));
    CHECK(contains(r.stdout_data, "top_weighted_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: weighted_cycles=20 cycles=5 repeat=4"));
    CHECK(contains(r.stdout_data, "weighted_nop_slots=20"));
    CHECK(contains(r.stdout_data, "top_weighted_idle_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: weighted_nop_slots=20 nop_slots=5 repeat=4"));
}

TEST_CASE("VSM cost CLI: fixture accepts SCE-style padded VSM output")
{
    ::test::RunResult r = runCost("sce_padded_columns.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 2);
    CHECK(textMetric(r.stdout_data, "instructions") == 3);
    CHECK(textMetric(r.stdout_data, "upper_instructions") == 1);
    CHECK(textMetric(r.stdout_data, "lower_instructions") == 2);
    CHECK(textMetric(r.stdout_data, "paired_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "operation_latency_cycles") == 9);
    CHECK(textMetric(r.stdout_data, "long_latency_ops") == 2);
    CHECK(contains(r.stdout_data, "sce_style_lid: cycles=2"));
}

TEST_CASE("VSM cost CLI: fixture proves long-latency operations affect weighted cost")
{
    ::test::RunResult r = runCost("long_latency_weighted.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 4);
    CHECK(textMetric(r.stdout_data, "instructions") == 7);
    CHECK(textMetric(r.stdout_data, "paired_cycles") == 3);
    CHECK(textMetric(r.stdout_data, "waitq_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "waitp_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "fdiv_ops") == 1);
    CHECK(textMetric(r.stdout_data, "efu_ops") == 1);
    CHECK(textMetric(r.stdout_data, "operation_latency_cycles") == 50);
    CHECK(textMetric(r.stdout_data, "long_latency_ops") == 5);
    CHECK(textMetric(r.stdout_data, "long_latency_cycles") == 43);
    CHECK(textMetric(r.stdout_data, "max_op_latency") == 29);
    CHECK(contains(r.stdout_data, "weighted_lid: cycles=4"));
}
