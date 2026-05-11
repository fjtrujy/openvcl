// Integration test: .init_vf with a register range
//
// openvcl/TODO documents that .init_vf with a register range like
// "vf01-vf04" currently errors out:
//
//     * Specifying .init_vf with a register-range will result in an
//       argument-error.  This will be solved when the tokenizer has
//       been extended to handle the 'range' modifier.
//
// Pinned as EXPECTED_FAIL.  An earlier draft of this test thought the
// bug was already fixed -- it was actually masked by the silent-failure
// bug that has since been patched (main.cpp now bubbles Error::HasErrors
// up into the exit code).  With that mask removed the original TODO
// reproduces cleanly, so the test goes back to known-broken.

#include "test_harness.h"
#include "openvcl_runner.h"

TEST_CASE("openvcl: .init_vf with a register range is accepted")
{
    EXPECTED_FAIL("openvcl TODO: .init_vf vf01-vf04 still errors out");

    // Minimal VCL fragment exercising only the .init_vf directive with
    // a range.  Leading tabs are required: openvcl's tokenizer rejects
    // directives that start in column 0 with "Invalid characters".
    const std::string input =
        "\t.init_vf vf01-vf04\n"
        "\t.init_vi_all\n"
        "\t.name vsmInitVfRangeTest\n"
        "\t--enter\n"
        "\tnop\n"
        "\t--endenter\n"
        "\t--exit\n"
        "\t--endexit\n";

    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back("-"); // write to stdout

    ::test::RunResult r = ::test::run_openvcl(args, input);

    // What we expect once the bug is fixed:
    //   exit 0, no "argument-error" diagnostic in stderr.
    CHECK(r.exit_code == 0);
    CHECK(r.stderr_data.find("argument-error") == std::string::npos);
    CHECK(r.stderr_data.find("error")          == std::string::npos);
}
