#include "test_harness.h"
#include "openvcl_runner.h"

#include <string>
#include <vector>

namespace
{
    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    ::test::RunResult runDump(const char* option)
    {
        std::vector<std::string> args;
        args.push_back(option);
        return ::test::run_openvcl(args, "");
    }
}

TEST_CASE("Instruction info CLI: text dump exposes scheduling metadata")
{
    ::test::RunResult r = runDump("--dump-instruction-info");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "OpenVCL VU instruction metadata"));
    CHECK(contains(r.stdout_data, "div pipe=lower unit=fdiv throughput=7 latency=7"));
    CHECK(contains(r.stdout_data, "clipw pipe=upper unit=fmac"));
    CHECK(contains(r.stdout_data, "writes=mac|clip"));
    CHECK(contains(r.stdout_data, "lqi pipe=lower unit=lsu"));
    CHECK(contains(r.stdout_data, "memory=load memory_flags=postinc"));
    CHECK(contains(r.stdout_data, "ftoi4 pipe=upper unit=fmac"));
    CHECK(contains(r.stdout_data, "bypass=ftoi_to_mtir"));
}

TEST_CASE("Instruction info CLI: JSON dump is stable enough for tooling")
{
    ::test::RunResult r = runDump("--dump-instruction-info-json");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "\"instructions\": ["));
    CHECK(contains(r.stdout_data, "\"mnemonic\": \"div\""));
    CHECK(contains(r.stdout_data, "\"unit\": \"fdiv\""));
    CHECK(contains(r.stdout_data, "\"implicit_writes\": [\"q\"]"));
    CHECK(contains(r.stdout_data, "\"mnemonic\": \"lqi\""));
    CHECK(contains(r.stdout_data, "\"kind\": \"load\""));
    CHECK(contains(r.stdout_data, "\"flags\": [\"postinc\"]"));
    CHECK(contains(r.stdout_data, "\"branch_delay_slots\": 1"));
    CHECK(contains(r.stdout_data, "\"bypass\": [\"ftoi_to_mtir\"]"));
}
