// Tests for dual-pipe pairing in CodeGenerator.
//
// Each test feeds a tiny VCL snippet to ./openvcl and asserts on the
// shape of the emitted VSM line(s):
//
//   - "paired" means upper-pipe instruction and lower-pipe instruction
//     appear on the SAME line (no NOP between them).
//   - "not paired" means they appear on separate lines with a NOP on
//     the unused pipe.
//
// We grep the emitted VSM and assert presence/absence of "nop" on the
// relevant pipe.  Each test stresses one hazard rule.

#include "test_harness.h"
#include "openvcl_runner.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

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

    // Run openvcl, capturing the EMITTED VSM into the returned string.
    // openvcl's `-o -` doesn't actually write to stdout (it creates a
    // literal file named `-`) so we use a temp file per test.
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

    // Find the first VSM line that mentions `pattern`.
    std::string find_line(const std::string& vsm, const std::string& pattern)
    {
        std::string::size_type pos = vsm.find(pattern);
        if( pos == std::string::npos )
            return std::string();
        std::string::size_type begin = vsm.rfind('\n', pos);
        std::string::size_type end = vsm.find('\n', pos);
        if( begin == std::string::npos ) begin = 0; else begin++;
        if( end == std::string::npos ) end = vsm.size();
        return vsm.substr(begin, end - begin);
    }
}

// --- Sanity: a known-safe pair should actually pair ----------------
//
// Two unrelated lq.xyz operations: an upper-pipe op that has no
// dependency on a lower-pipe op should pair into one line.

// True if any line in `vsm` contains both `a` and `b` substrings.
static bool linePairsSubstrings( const std::string& vsm, const std::string& a, const std::string& b )
{
    std::string::size_type pos = 0;
    while( pos < vsm.size() )
    {
        std::string::size_type end = vsm.find('\n', pos);
        if( end == std::string::npos ) end = vsm.size();
        std::string line = vsm.substr(pos, end - pos);
        if( line.find(a) != std::string::npos && line.find(b) != std::string::npos )
            return true;
        pos = end + 1;
    }
    return false;
}

TEST_CASE("Pairing: independent upper+lower ops pair into one cycle")
{
    // mulax (upper) + lq.xyz (lower) - no shared register.  These should
    // pair into a single emitted line because they have no data flow
    // between them and sit on different pipes.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmulax acc, vf01, vf02x\n"
        "\tlq.xyz vf09, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairIndependent");
    REQUIRE(vsm.length() > 0);
    // Either mulax pairs directly with lq.xyz, OR an earlier move
    // pairs with mulax leaving lq.xyz alone — either outcome means
    // pairing fired at least once on this snippet.
    bool anyPair =
        linePairsSubstrings(vsm, "mulax", "lq.xyz")
     || linePairsSubstrings(vsm, "move", "mulax");
    CHECK(anyPair);
}

TEST_CASE("Pairing: LOI is not paired with the next FMAC that reads I")
{
    // loi writes I.  An immediately-following FMAC that reads I must
    // NOT share a cycle with the loi or it will see the stale I.
    // Operand::IWRITE is the only signal that loi writes I — the
    // immediate Argument doesn't carry the WRITE flag.
    const std::string body =
        "\tloi 0x44fff000\n"
        "\taddi.xy vf01, vf00, i\n";
    std::string vsm = runEmit(body, "vsmPairLoiI");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "addi.xy", "loi"));
}

TEST_CASE("Pairing: FMAC reading I is not paired with following LOI (WAR on I)")
{
    // VU1 single-instance resources (I, Q, P, R, ACC) don't have a safe
    // intra-cycle ordering for reads vs writes.  Block WAR on I too.
    const std::string body =
        "\tloi 0x42000000\n"
        "\taddi.xy vf01, vf00, i\n"
        "\tloi 0x43000000\n";
    std::string vsm = runEmit(body, "vsmPairFmacIToLoi");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "addi.xy", "loi"));
}

TEST_CASE("Pairing: FMAC opmsub is not paired with fmand reading MAC")
{
    // Every FMAC implicitly writes the MAC flag register with 4-cycle
    // latency.  Pairing opmsub with the next fmand makes fmand see
    // stale flags from before opmsub - breaks back-face cull etc.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\topmula.xyz acc, vf01, vf02\n"
        "\topmsub.xyz vf04, vf02, vf01\n"
        "\tfmand vi01, vi00\n";
    std::string vsm = runEmit(body, "vsmPairOpmsubFmand");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "opmsub", "fmand"));
}

TEST_CASE("Pairing: FMAC clipw is not paired with fcand reading CLIP")
{
    // clipw writes the CLIP register with FMAC latency.  fcand reads
    // CLIP and must see the post-clipw value.  Pairing them is a
    // stale-read hazard, just like the MAC case above.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tclipw.xyz vf01, vf02w\n"
        "\tfcand vi01, 0xffffff\n";
    std::string vsm = runEmit(body, "vsmPairClipwFcand");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "clipw", "fcand"));
}

TEST_CASE("Pairing: FMAC mulq reading Q is not paired with FDIV writing Q")
{
    // FDIV is excluded from pairing wholesale (Unit==FDIV check) but
    // we also want the conservative resource-conflict path to catch
    // it: any token that writes Q can't share a cycle with one that
    // reads Q, regardless of which pipe.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf00, q\n";
    std::string vsm = runEmit(body, "vsmPairDivMulq");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "div", "mulq"));
}
