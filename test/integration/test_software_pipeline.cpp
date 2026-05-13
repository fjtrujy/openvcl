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
    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    int countSubstrings(const std::string& text, const std::string& pattern)
    {
        int count = 0;
        std::string::size_type pos = 0;
        while ((pos = text.find(pattern, pos)) != std::string::npos)
        {
            ++count;
            pos += pattern.size();
        }
        return count;
    }

    std::string runEmit(const std::string& source)
    {
        char tmpl[] = "/tmp/openvcl_pipe_XXXXXX.vsm";
        int fd = mkstemps(tmpl, 4);
        if (fd < 0)
            return std::string();
        close(fd);

        std::vector<std::string> args;
        args.push_back("-o");
        args.push_back(tmpl);
        ::test::RunResult r = ::test::run_openvcl(args, source);
        if (r.exit_code != 0)
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

    std::string fastNoLightsPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmFastNoLights\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi05, vi00, 9\n"
            "\tmove.xyzw vf01, vf00\n"
            "\tmove.xyzw vf02, vf00\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf04, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf07, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf04, 0(vi02)\n"
            "\tmulax acc, vf05, vf04\n"
            "\tmadday acc, vf01, vf04\n"
            "\tmaddaz acc, vf02, vf04\n"
            "\tmaddw vf07, vf03, vf00\n"
            "\tdiv q, vf00w, vf07w\n"
            "\tmulq.xyz vf07, vf07, q\n"
            "\tftoi4.xyz vf08, vf07\n"
            "\tilw.w vi04, 0(vi02)\n"
            "\tiaddiu vi06, vi04, 0x7fff\n"
            "\tmfir.w vf08, vi06\n"
            "\tsq vf08, 2(vi03)\n"
            "\tsq vf06, 1(vi03)\n"
            "\tlq.xyz vf09, 2(vi02)\n"
            "\tmulq.xyz vf10, vf09, q\n"
            "\tsq.xyz vf10, 0(vi03)\n"
            "\tiaddiu vi02, vi02, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi02, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\txgkick vi01\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string fastLitPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmFast\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi05, vi00, 9\n"
            "\tmove.xyzw vf01, vf00\n"
            "\tmove.xyzw vf02, vf00\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf04, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf07, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf04, 0(vi02)\n"
            "\tlq.xyz vf10, 1(vi02)\n"
            "\tilw.w vi04, 0(vi02)\n"
            "\tlq.xyz vf20, 2(vi02)\n"
            "\tiaddiu vi02, vi02, 3\n"
            "\tmulax acc, vf18, vf04\n"
            "\tmadday acc, vf01, vf04\n"
            "\tmaddaz acc, vf02, vf04\n"
            "\tmaddw vf08, vf03, vf00\n"
            "\tmulax.xyz acc, vf11, vf10\n"
            "\tmadday.xyz acc, vf14, vf10\n"
            "\tmaddz.xyz vf13, vf16, vf10\n"
            "\tdiv q, vf00w, vf08w\n"
            "\tmax.xyz vf19, vf13, vf00\n"
            "\tmulax.xyz acc, vf12, vf19\n"
            "\tmadday.xyz acc, vf15, vf19\n"
            "\tmaddz.xyz vf05, vf17, vf19\n"
            "\tmulq.xyz vf08, vf08, q\n"
            "\tmulq.xyz vf21, vf20, q\n"
            "\tadd.xyz vf05, vf05, vf07\n"
            "\tftoi4.xyz vf09, vf08\n"
            "\tsq.xyz vf21, 0(vi03)\n"
            "\tminiw.xyz vf05, vf05, vf06w\n"
            "\tiaddiu vi06, vi04, 0x7fff\n"
            "\tmfir.w vf09, vi06\n"
            "\tsq vf09, 2(vi03)\n"
            "\tsq vf05, 1(vi03)\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi02, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\txgkick vi01\n"
            "\t--exit\n"
            "\t--endexit\n";
    }
}

TEST_CASE("Software pipeline: fast_nolights transform loop emits a 12-cycle steady state")
{
    std::string vsm = runEmit(fastNoLightsPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI02, VI05, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "mulq.xyz VF10, VF10, q          div q, VF00w, VF07w"));
    CHECK(countSubstrings(vsm, "waitq") == 0);
}

TEST_CASE("Software pipeline: fast lit transform loop emits a 16-cycle steady state")
{
    std::string vsm = runEmit(fastLitPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI02, VI05, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "maddz.xyz VF23, VF17, VF19z     lq.xyz VF10, 1(VI02)"));
    CHECK(contains(vsm, "mulq.xyz VF25, VF24, q          mfir.w VF09, VI06"));
}
