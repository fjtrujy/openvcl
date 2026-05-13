#include "test_harness.h"
#include "openvcl_runner.h"

#include "../../src/OpenVclVersion.h"

#include <string>
#include <vector>

TEST_CASE("CLI: --version prints the shared OpenVCL version")
{
    std::vector<std::string> args;
    args.push_back("--version");

    ::test::RunResult r = ::test::run_openvcl(args, "");
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(r.stdout_data == std::string("OpenVCL Version ") + vcl::OPENVCL_VERSION + "\n");
}
