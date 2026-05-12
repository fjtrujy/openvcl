// Regression tests: VU1 has a 4-cycle FMAC pipeline.  An FMAC writes the
// MAC / CLIP / STATUS flag registers 4 cycles after issue.  A flag-reader
// (fmand / fcand / fsand / fcget / etc.) issued any sooner reads the
// PREVIOUS flag value — i.e. silently the wrong answer.
//
// The motivating bug:  ps2gl's `bfc_tri` macro ends with an `opmsub.xyz`
// (computes the backface-cull normal, writes MAC sign) followed
// immediately by `fmand z_sign, z_sign_mask`.  Openvcl emitted those on
// adjacent cycles, so fmand read MAC from BEFORE opmsub — the BFC test
// was based on an unrelated FMAC's sign and culled arbitrary faces.
//
// The fix is to insert NOP cycles before any flag-reader that's too
// close to its FMAC.  These tests pin the cycle distance directly by
// counting emitted lines between the two operations.

#include "test_harness.h"
#include "openvcl_runner.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
    std::string skeleton(const std::string& name, const std::string& body)
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name " + name + "\n"
            "\t--enter\n"
            "\t--endenter\n"
            + body +
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string runEmit(const std::string& body, const std::string& name)
    {
        char tmpl[] = "/tmp/openvcl_test_XXXXXX.vsm";
        int fd = mkstemps(tmpl, 4);
        if( fd < 0 )
            return std::string();
        close(fd);

        std::vector<std::string> args;
        args.push_back("-o");
        args.push_back(tmpl);
        ::test::RunResult r = ::test::run_openvcl(args, skeleton(name, body));
        if( r.exit_code != 0 )
        {
            std::remove(tmpl);
            return std::string();
        }
        std::ifstream f(tmpl);
        std::stringstream ss;
        ss << f.rdbuf();
        std::remove(tmpl);
        return ss.str();
    }

    // Number of emitted lines (cycles) between the first line that
    // contains `first` and the first line after it that contains `second`.
    // Each line == 1 VU cycle, so this is the cycle distance.  Returns -1
    // if either pattern isn't found.
    int cycleDistance(const std::string& vsm, const std::string& first, const std::string& second)
    {
        std::string::size_type startPos = vsm.find(first);
        if( startPos == std::string::npos )
            return -1;
        // Count newlines from `first` line onward until we hit `second`.
        std::string::size_type pos = startPos;
        int cycles = 0;
        while( true )
        {
            std::string::size_type nl = vsm.find('\n', pos);
            if( nl == std::string::npos )
                return -1;
            std::string::size_type nextStart = nl + 1;
            std::string::size_type nextNl = vsm.find('\n', nextStart);
            if( nextNl == std::string::npos )
                nextNl = vsm.size();
            std::string line = vsm.substr(nextStart, nextNl - nextStart);
            cycles++;
            if( line.find(second) != std::string::npos )
                return cycles;
            pos = nextStart;
        }
    }
}

TEST_CASE("Latency: FMAC opmsub followed by fmand has at least 4 cycles between")
{
    // The bfc_tri pattern from ps2gl/vu1/clip_cull.i:
    //   opmula.xyz acc, delta_1, delta_2
    //   opmsub.xyz bfc_normal, delta_2, delta_1     ; updates MAC
    //   fmand      z_sign, z_sign_mask              ; reads MAC of opmsub
    //
    // VU1 FMAC latency is 4 cycles, so fmand must be at least 4 cycles
    // after opmsub or it reads pre-opmsub MAC.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tilw.x my_mask, 0(vi00)\n"
        "\topmula.xyz acc, vf01, vf02\n"
        "\topmsub.xyz vf03, vf02, vf01\n"
        "\tfmand vi02, my_mask\n";

    std::string vsm = runEmit(body, "vsmLatencyFmand");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "opmsub", "fmand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: clipw followed by fcand has at least 4 cycles between")
{
    // clipw is the only FMAC that writes the CLIP register.  fcand reads
    // CLIP, so it needs 4 cycles after the last clipw.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tclipw.xyz vf01, vf02w\n"
        "\tfcand vi01, 0x0ffffff\n";

    std::string vsm = runEmit(body, "vsmLatencyFcand");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "clipw", "fcand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: plain mul followed by fmand has at least 4 cycles between")
{
    // Any FMAC sets MAC flags, not just opmsub.  A simple mul must also
    // give fmand 4 cycles of headroom.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tilw.x my_mask, 0(vi00)\n"
        "\tmul.xyz vf02, vf01, vf01\n"
        "\tfmand vi02, my_mask\n";

    std::string vsm = runEmit(body, "vsmLatencyMulFmand");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "mul.xyz", "fmand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}
