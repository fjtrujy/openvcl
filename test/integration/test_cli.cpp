#include "test_harness.h"
#include "openvcl_runner.h"

#include "../../src/OpenVclVersion.h"

#include <string>
#include <vector>

namespace
{
    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    std::string pipelineInput()
    {
        return
            "loop_lid:\n"
            "--LoopCS 1, 3\n"
            "lq.xyz vf01, 0(vi01)\n"
            "mul.xyz vf03, vf01, vf02\n"
            "div q, vf00[w], vf03[w]\n"
            "mulq.xyz vf03, vf03, q\n"
            "add.xyz vf05, vf03, vf00\n"
            "lq.xyz vf06, 2(vi01)\n"
            "mulq.xyz vf06, vf06, q\n"
            "sq.xyz vf06, 0(vi02)\n"
            "iaddiu vi01, vi01, 3\n"
            "ibne vi01, vi03, loop_lid\n";
    }
}

TEST_CASE("CLI: --version prints the shared OpenVCL version")
{
    std::vector<std::string> args;
    args.push_back("--version");

    ::test::RunResult r = ::test::run_openvcl(args, "");
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(r.stdout_data == std::string("OpenVCL Version ") + vcl::OPENVCL_VERSION + "\n");
}

TEST_CASE("CLI: loop pipeline text dump exposes Q software-pipeline candidates")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-loop-pipeline-info");

    ::test::RunResult r = ::test::run_openvcl(args, pipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "OpenVCL VU loop pipeline opportunities"));
    CHECK(contains(r.stdout_data, "loop_lid q_producer_token=4"));
    CHECK(contains(r.stdout_data, "q_latency=7"));
    CHECK(contains(r.stdout_data, "requires_prolog_epilog=yes"));
    CHECK(contains(r.stdout_data, "eligible_single_q_pipeline=yes"));
    CHECK(contains(r.stdout_data, "carried_q_inputs=VF03.x"));
}

TEST_CASE("CLI: loop pipeline JSON dump is stable enough for scheduler tooling")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-loop-pipeline-info-json");

    ::test::RunResult r = ::test::run_openvcl(args, pipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "\"loop_pipeline_opportunities\": ["));
    CHECK(contains(r.stdout_data, "\"label\": \"loop_lid\""));
    CHECK(contains(r.stdout_data, "\"q_producer_token_index\": 4"));
    CHECK(contains(r.stdout_data, "\"q_producer_latency\": 7"));
    CHECK(contains(r.stdout_data, "\"requires_loop_carried_registers\": true"));
    CHECK(contains(r.stdout_data, "\"eligible_single_q_pipeline\": true"));
    CHECK(contains(r.stdout_data, "\"carried_q_input_registers\": [\"VF03.x\""));
}
