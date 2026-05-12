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

static int lineIndex( const std::string& vsm, const std::string& pattern )
{
    std::string::size_type pos = 0;
    int lineNo = 0;
    while( pos < vsm.size() )
    {
        std::string::size_type end = vsm.find('\n', pos);
        if( end == std::string::npos ) end = vsm.size();
        std::string line = vsm.substr(pos, end - pos);
        if( line.find(pattern) != std::string::npos )
            return lineNo;
        pos = end + 1;
        lineNo++;
    }
    return -1;
}

TEST_CASE("Pairing: independent upper+lower ops pair into one cycle")
{
    // mulax (upper) + lq.xyz (lower) - no shared register.  These should
    // pair into a single emitted line because they have no data flow
    // between them and sit on different pipes.
    const std::string body =
        "\tmulax acc, vf00, vf00x\n"
        "\tlq.xyz vf09, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairIndependent");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "lq.xyz"));
}

TEST_CASE("Pairing: independent upper op can pair with adjacent plain store")
{
    // Pairing an adjacent store does not reorder memory; it only fills the
    // upper slot of the same issue cycle.
    const std::string body =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\tsq.xyz vf00, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairPlainStore");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "sq.xyz"));
}

TEST_CASE("Pairing: store reading the upper result is not paired")
{
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tsq.xyz vf01, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmNoPairStoreReadsUpper");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "add.xyz", "sq.xyz"));
}

TEST_CASE("Pairing: post-increment store remains unpaired")
{
    const std::string body =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\tsqi.xyz vf00, (vi00++)\n";
    std::string vsm = runEmit(body, "vsmNoPairPostIncStore");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "add.xyz", "sqi.xyz"));
}

TEST_CASE("Pairing: independent upper op can pair with following xgkick")
{
    const std::string body =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\txgkick vi00\n";
    std::string vsm = runEmit(body, "vsmPairXgkick");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "xgkick"));
}

TEST_CASE("Pairing: xgkick does not pull a following upper op before it")
{
    const std::string body =
        "\txgkick vi00\n"
        "\tadd.xyz vf02, vf00, vf00\n";
    std::string vsm = runEmit(body, "vsmNoPairAfterXgkick");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "add.xyz", "xgkick"));
}

TEST_CASE("Scheduling: branch emission reuses existing hazard nop before branch")
{
    const std::string body =
        "\tilw.x vi01, 0(vi00)\n"
        "\tibeq vi01, vi00, done_lid\n"
        "\tiaddiu vi02, vi00, 1\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmBranchReuseHazardNop");
    REQUIRE(vsm.length() > 0);

    int loadLine = lineIndex(vsm, "ilw.x");
    int branchLine = lineIndex(vsm, "ibeq");
    REQUIRE(loadLine >= 0);
    REQUIRE(branchLine >= 0);
    CHECK(branchLine - loadLine == 5);
}

TEST_CASE("Pairing: independent upper op can pair with FDIV")
{
    // FDIV writes Q on the lower pipe.  With deferred waitq handling, an
    // unrelated upper-pipe op can issue in the same cycle safely.
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tdiv q, vf00w, vf00w\n";
    std::string vsm = runEmit(body, "vsmPairFdiv");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "div"));
}

TEST_CASE("Pairing: independent upper op can pair with EFU")
{
    // EFU writes P on the lower pipe.  The dependency checker keeps P hazards
    // apart, but independent upper work should still fill the paired slot.
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\teleng p, vf00\n";
    std::string vsm = runEmit(body, "vsmPairEfu");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "eleng"));
}

TEST_CASE("Pairing: independent non-adjacent lower op can fill current upper slot")
{
    // The iaddiu is separated from mulax by another upper-pipe op.  It can
    // safely move upward and pair with mulax because it has no dependency on
    // the crossed FMAC.
    const std::string body =
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tiaddiu vi01, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadIndependent");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu"));
}

TEST_CASE("Pairing: distant independent lower op can fill current upper slot")
{
    // ps2gl setup blocks often have many same-pipe FMACs before the next
    // independent lower op.  The bounded lookahead should be wide enough to
    // find that lower op while dependency checks still decide what can cross.
    const std::string body =
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tmaddaz acc, vf00, vf00z\n"
        "\tmaddw vf01, vf00, vf00w\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tmaddaz acc, vf00, vf00z\n"
        "\tmaddw vf02, vf00, vf00w\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tiaddiu vi01, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadDistantIndependent");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu VI01"));
}

TEST_CASE("Pairing: non-adjacent candidate is not moved across a register conflict")
{
    // The later lq writes VF01.  The intervening madday reads VF01, so the
    // scheduler must not hoist lq above madday just to pair it with mulax.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf04, vf00\n"
        "\tmulax acc, vf03, vf04x\n"
        "\tmadday acc, vf01, vf02y\n"
        "\tlq.xyz vf01, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadConflict");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "mulax", "lq.xyz"));
}

TEST_CASE("Pairing: non-adjacent candidate is not moved before it is latency-ready")
{
    // The mtir reads VF09 produced by the earlier lq.  The intervening FMAC
    // can usefully cover part of that latency, so hoisting mtir upward would
    // create extra NOP padding even though there is no direct dependency with
    // madday.
    const std::string body =
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tmtir vi01, vf09x\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadLatencyReady");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "madday", "mtir"));
}

TEST_CASE("Scheduling: independent later op fills current register-latency wait")
{
    // mulax must wait for the two moves.  The later lq is independent and
    // ready, so the scheduler can issue it during that wait instead of
    // burning a pure NOP cycle.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmulax acc, vf01, vf02x\n"
        "\tlq.xyz vf09, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmScheduleLatencyFiller");
    REQUIRE(vsm.length() > 0);

    int lqLine = lineIndex(vsm, "lq.xyz");
    int mulLine = lineIndex(vsm, "mulax");
    REQUIRE(lqLine >= 0);
    REQUIRE(mulLine >= 0);
    CHECK(lqLine < mulLine);
}

TEST_CASE("Scheduling: latency-gap filler can pair with another ready op")
{
    // mtir must wait for VF09 from the lqi.  The lqi's post-increment keeps it
    // from pairing with the later FMAC directly; both later instructions are
    // ready and independent of mtir, so they should occupy a single paired
    // cycle in the otherwise empty latency gap.
    const std::string body =
        "\tlqi.xyz vf09, (vi00++)\n"
        "\tmtir vi01, vf09x\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmSchedulePairedLatencyFiller");
    REQUIRE(vsm.length() > 0);

    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu"));

    int pairLine = lineIndex(vsm, "mulax");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(pairLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(pairLine < mtirLine);
}

TEST_CASE("Scheduling: independent FDIV producer can fill a register-latency gap")
{
    // FDIV writes Q, but Q is tracked as an implicit dependency.  When the
    // producer is independent of the waiting FMAC, it can cover the load
    // latency instead of acting as a hard scheduling barrier.
    const std::string body =
        "\tlq vf09, 0(vi00)\n"
        "\tmulax acc, vf09, vf00x\n"
        "\tdiv q, vf00w, vf00w\n"
        "\tmulq.xyz vf03, vf00, q\n";
    std::string vsm = runEmit(body, "vsmScheduleFdivLatencyFiller");
    REQUIRE(vsm.length() > 0);

    int divLine = lineIndex(vsm, "div");
    int mulLine = lineIndex(vsm, "mulax");
    int mulqLine = lineIndex(vsm, "mulq");
    REQUIRE(divLine >= 0);
    REQUIRE(mulLine >= 0);
    REQUIRE(mulqLine >= 0);
    CHECK(divLine < mulLine);
    CHECK(divLine < mulqLine);
}

TEST_CASE("Scheduling: distant ready op fills current register-latency wait")
{
    // The first several candidates also read the pending load result, so they
    // are not ready.  A later independent integer op should still be found and
    // moved into the gap instead of burning a pure NOP.
    const std::string body =
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tadd.xyz vf10, vf09, vf00\n"
        "\tadd.xyz vf11, vf09, vf00\n"
        "\tadd.xyz vf12, vf09, vf00\n"
        "\tadd.xyz vf13, vf09, vf00\n"
        "\tadd.xyz vf14, vf09, vf00\n"
        "\tadd.xyz vf15, vf09, vf00\n"
        "\tadd.xyz vf16, vf09, vf00\n"
        "\tadd.xyz vf17, vf09, vf00\n"
        "\tadd.xyz vf18, vf09, vf00\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleDistantLatencyFiller");
    REQUIRE(vsm.length() > 0);

    int fillerLine = lineIndex(vsm, "iaddiu VI02");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(fillerLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(fillerLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler may load from a different base before a plain store")
{
    // The Q consumer must wait for DIV.  A later input load using VI03 can
    // cover that wait even though a plain output store using VI04 sits before
    // it in source order.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf03, q\n"
        "\tsq.xyz vf10, 0(vi04)\n"
        "\tlq.xyz vf20, 0(vi03)\n";
    std::string vsm = runEmit(body, "vsmScheduleLoadAcrossStore");
    REQUIRE(vsm.length() > 0);

    int lqLine = lineIndex(vsm, "lq.xyz VF20");
    int mulqLine = lineIndex(vsm, "mulq");
    REQUIRE(lqLine >= 0);
    REQUIRE(mulqLine >= 0);
    CHECK(lqLine < mulqLine);
}

TEST_CASE("Scheduling: latency filler does not move a load before a same-base store")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf03, q\n"
        "\tsq.xyz vf10, 0(vi03)\n"
        "\tlq.xyz vf20, 0(vi03)\n";
    std::string vsm = runEmit(body, "vsmScheduleNoLoadAcrossSameBaseStore");
    REQUIRE(vsm.length() > 0);

    int sqLine = lineIndex(vsm, "sq.xyz");
    int lqLine = lineIndex(vsm, "lq.xyz VF20");
    REQUIRE(sqLine >= 0);
    REQUIRE(lqLine >= 0);
    CHECK(sqLine < lqLine);
}

TEST_CASE("Scheduling: latency filler may move a load before a different-offset same-base store")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf03, q\n"
        "\tsq.xyz vf10, 0(vi03)\n"
        "\tlq.xyz vf20, 2(vi03)\n";
    std::string vsm = runEmit(body, "vsmScheduleLoadAcrossDifferentOffsetStore");
    REQUIRE(vsm.length() > 0);

    int lqLine = lineIndex(vsm, "lq.xyz VF20");
    int mulqLine = lineIndex(vsm, "mulq");
    REQUIRE(lqLine >= 0);
    REQUIRE(mulqLine >= 0);
    CHECK(lqLine < mulqLine);
}

TEST_CASE("Scheduling: latency filler may move independent integer compute before a plain store")
{
    // The iaddiu is pure integer work and does not feed the store, so it can
    // move above that store to cover the lq->mtir latency.
    const std::string body =
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tsq.xyz vf10, 0(vi04)\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleComputeAcrossStore");
    REQUIRE(vsm.length() > 0);

    int addLine = lineIndex(vsm, "iaddiu VI02");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(addLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(addLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler may move a distinct-address store before a plain load")
{
    const std::string body =
        "\tlq vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tlq.xyz vf20, 0(vi01)\n"
        "\tsq.xyz vf00, 8(vi00)\n";
    std::string vsm = runEmit(body, "vsmScheduleStoreAcrossLoad");
    REQUIRE(vsm.length() > 0);

    int storeLine = lineIndex(vsm, "sq.xyz");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(storeLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(storeLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler does not move a store before a same-address store")
{
    const std::string body =
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tlq vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tsq vf09, 0(vi04)\n"
        "\tsq vf10, 0(vi04)\n";
    std::string vsm = runEmit(body, "vsmScheduleNoStoreAcrossSameAddressStore");
    REQUIRE(vsm.length() > 0);

    int firstStore = lineIndex(vsm, "sq VF09");
    int secondStore = lineIndex(vsm, "sq VF10");
    REQUIRE(firstStore >= 0);
    REQUIRE(secondStore >= 0);
    CHECK(firstStore < secondStore);
}

TEST_CASE("Scheduling: latency filler does not move store base update before that store")
{
    const std::string body =
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tsq.xyz vf10, 0(vi04)\n"
        "\tiaddiu vi04, vi04, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleNoStoreProducerAcrossStore");
    REQUIRE(vsm.length() > 0);

    int storeLine = lineIndex(vsm, "sq.xyz");
    int addLine = lineIndex(vsm, "iaddiu VI04, VI04");
    REQUIRE(storeLine >= 0);
    REQUIRE(addLine >= 0);
    CHECK(storeLine < addLine);
}

TEST_CASE("Scheduling: adjacent iaddiu pointer bump chain coalesces")
{
    const std::string body =
        "\tiaddiu vi03, vi00, 0\n"
        "\tiaddiu vi03, vi03, 3\n"
        "\tiaddiu vi03, vi03, 3\n"
        "\tiaddiu vi03, vi03, 3\n";
    std::string vsm = runEmit(body, "vsmCoalesceSelfIaddiu");
    REQUIRE(vsm.length() > 0);

    CHECK(lineIndex(vsm, "iaddiu VI03, VI00, 9") >= 0);
    CHECK(lineIndex(vsm, "iaddiu VI03, VI03, 3") < 0);
}

TEST_CASE("Scheduling: adjacent iaddiu followed by self bump coalesces")
{
    const std::string body =
        "\tiaddiu vi03, vi00, 5\n"
        "\tiaddiu vi03, vi03, 3\n";
    std::string vsm = runEmit(body, "vsmCoalesceIaddiuChain");
    REQUIRE(vsm.length() > 0);

    CHECK(lineIndex(vsm, "iaddiu VI03, VI00, 8") >= 0);
    CHECK(lineIndex(vsm, "iaddiu VI03, VI03, 3") < 0);
}

TEST_CASE("Scheduling: overwriting an already-read register does not create a WAR stall")
{
    // Source operands are latched when the older FMAC issues, so a later
    // instruction may overwrite VF02 without waiting for all older reads of
    // VF02 to retire.  The true hazard is RAW from an older write to a later
    // read, which is covered by the latency tests.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf04, vf00\n"
        "\tmove.xyzw vf05, vf00\n"
        "\tmulax acc, vf01, vf02x\n"
        "\tmadday acc, vf03, vf02y\n"
        "\tmaddaz acc, vf04, vf02z\n"
        "\tmaddw vf02, vf05, vf00w\n";
    std::string vsm = runEmit(body, "vsmScheduleNoRegisterWarStall");
    REQUIRE(vsm.length() > 0);

    int lastRead = lineIndex(vsm, "maddaz");
    int overwrite = lineIndex(vsm, "maddw");
    REQUIRE(lastRead >= 0);
    REQUIRE(overwrite >= 0);
    CHECK(overwrite == lastRead + 1);
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

TEST_CASE("Pairing: non-clip FMAC can pair with latency-ready fcand")
{
    // fcand reads the CLIP flags, not MAC.  Once the earlier clipw result
    // is ready, an unrelated FMAC that only writes MAC can occupy the upper
    // pipe in the same cycle.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tclipw.xyz vf01, vf00w\n"
        "\tsub.xyz vf04, vf01, vf02\n"
        "\tsub.xyz vf05, vf03, vf02\n"
        "\tmulw.xyz vf10, vf04, vf00w\n"
        "\tfcand vi01, 0xffffff\n";
    std::string vsm = runEmit(body, "vsmPairFmacFcand");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulw", "fcand"));
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
