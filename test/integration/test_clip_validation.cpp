// Integration test: CLIP operand validation
//
// openvcl/TODO documents that the CLIP operands are present in the
// parser but are not properly validated:
//
//     * The CLIP operands are now present but not properly validated.
//
// This test pins that behavior as EXPECTED_FAIL.  A malformed CLIP
// invocation (wrong source field width on the second argument) should
// be rejected with a clear error, but currently is silently accepted.

#include "test_harness.h"
#include "openvcl_runner.h"

TEST_CASE("openvcl: CLIP rejects an obviously malformed operand")
{
    EXPECTED_FAIL("openvcl TODO: CLIP operands are not properly validated");

    // CLIP takes a vector source and the W field of another vector.
    // Passing a non-vector operand on the second slot is clearly invalid
    // and should produce a diagnostic — but openvcl currently accepts it.
    const std::string input =
        "--enter main_loop\n"
        "in_vf  src vf01\n"
        "--endenter\n"
        "clip.xyz VF01, 0\n"   // 0 is not a valid VFw operand for CLIP
        "--exit\n"
        "--endexit\n";

    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back("-");

    ::test::RunResult r = ::test::run_openvcl(args, input);

    // What we expect once the validation is added:
    //   non-zero exit and an error mentioning CLIP / invalid operand.
    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("CLIP") != std::string::npos
       || r.stderr_data.find("clip") != std::string::npos
       || r.stderr_data.find("operand") != std::string::npos
       || r.stderr_data.find("error") != std::string::npos);
}
