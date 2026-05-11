// Regression test: .init_vf with a register range is accepted
//
// openvcl/TODO used to claim this errored out:
//   "Specifying .init_vf with a register-range will result in an
//    argument-error.  This will be solved when the tokenizer has been
//    extended to handle the 'range' modifier."
//
// In practice the tokenizer already handles it (likely as collateral
// from the recent register-allocator and tokenizer work on the ps2gl
// branch).  This test pins the working behavior so any future
// regression on .init_vf range parsing trips a real failure.

#include "test_harness.h"
#include "openvcl_runner.h"

TEST_CASE("openvcl: .init_vf with a register range is accepted")
{
    // Minimal VCL fragment exercising only the .init_vf directive with
    // a range.  No actual code or main loop, so the parser should accept
    // the directive and produce empty/near-empty output on success.
    const std::string input =
        ".init_vf vf01-vf04\n"
        "--enter main_loop\n"
        "nop\n"
        "--exit\n"
        "--endexit\n";

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
