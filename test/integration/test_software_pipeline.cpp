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

    std::string sceiPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmSCEI\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 0\n"
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
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf23, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf09, 0(vi03)\n"
            "\tlq.xyz vf19, 1(vi03)\n"
            "\tilw.w vi06, 0(vi03)\n"
            "\tlq.xyz vf22, 2(vi03)\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tmulax acc, vf01, vf09\n"
            "\tmadday acc, vf02, vf09\n"
            "\tmaddaz acc, vf03, vf09\n"
            "\tmaddw vf10, vf04, vf00\n"
            "\tmulax.xyz acc, vf11, vf19\n"
            "\tmadday.xyz acc, vf14, vf19\n"
            "\tmaddz.xyz vf20, vf16, vf19\n"
            "\tdiv q, vf00w, vf10w\n"
            "\tmulq.xyz vf10, vf10, q\n"
            "\tmax.xyz vf21, vf20, vf00\n"
            "\tmulq.xyz vf23, vf22, q\n"
            "\tadd.xyz vf13, vf10, vf06\n"
            "\tmul.xyz vf18, vf10, vf08\n"
            "\tmulax.xyz acc, vf12, vf21\n"
            "\tmadday.xyz acc, vf15, vf21\n"
            "\tmaddz.xyz vf05, vf17, vf21\n"
            "\tftoi4.xyz vf13, vf13\n"
            "\tclipw.xyz vf18, vf08w\n"
            "\tadd.xyz vf05, vf05, vf07\n"
            "\tfcand vi01, 0x003ffff\n"
            "\tior vi07, vi01, vi06\n"
            "\tiaddiu vi07, vi07, 0x7fff\n"
            "\tmfir.w vf13, vi07\n"
            "\tminiw.xyz vf05, vf05, vf06w\n"
            "\tsq.xyz vf23, 0(vi04)\n"
            "\tsq vf13, 2(vi04)\n"
            "\tsq vf05, 1(vi04)\n"
            "\tiaddiu vi04, vi04, 3\n"
            "\tibne vi03, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\txgkick vi01\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string finalColorPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmFinalColor\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi02, vi00, 9\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tloi 255.0\n"
            "final_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf08, 1(vi03)\n"
            "\tminii.xyz vf08, vf08, i\n"
            "\tftoi0.xyz vf08, vf08\n"
            "\tsq vf08, 1(vi03)\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi03, vi02, final_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string linearXformPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmGeneralLinear\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 9\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi05, vi00, 0\n"
            "\tiaddiu vi06, vi00, 0\n"
            "\tiaddiu vi13, vi00, 0\n"
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
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf11, 0(vi13)\n"
            "\tmulax acc, vf01, vf11\n"
            "\tmadday acc, vf02, vf11\n"
            "\tmaddaz acc, vf03, vf11\n"
            "\tmaddw vf12, vf04, vf00\n"
            "\tdiv q, vf00w, vf12w\n"
            "\tmulq.xyz vf12, vf12, q\n"
            "\tadd.xyz vf13, vf12, vf05\n"
            "\tftoi4.xyz vf13, vf13\n"
            "\tilw.w vi07, 0(vi13)\n"
            "\tsub.xyz vf14, vf09, vf12\n"
            "\topmula.xyz acc, vf14, vf06\n"
            "\topmsub.xyz vf15, vf06, vf14\n"
            "\tfmand vi08, vi06\n"
            "\tisub vi08, vi08, vi05\n"
            "\tiand vi08, vi08, vi06\n"
            "\tisub vi05, vi06, vi05\n"
            "\tiand vi10, vi07, vi06\n"
            "\tior vi05, vi05, vi10\n"
            "\tmulw.xyz vf06, vf14, vf10w\n"
            "\tmulw.xyz vf09, vf12, vf00w\n"
            "\tmul.xyz vf16, vf12, vf07\n"
            "\tclipw.xyz vf16, vf07w\n"
            "\tfcand vi01, 0x003ffff\n"
            "\tiand vi01, vi01, vi02\n"
            "\tior vi09, vi01, vi08\n"
            "\tior vi09, vi09, vi07\n"
            "\tiaddiu vi09, vi09, 0x7fff\n"
            "\tmfir.w vf13, vi09\n"
            "\tsq vf13, 2(vi03)\n"
            "\tsq.xyz vf08, 1(vi03)\n"
            "\tlq.xyz vf17, 2(vi13)\n"
            "\tmulq.xyz vf18, vf17, q\n"
            "\tsq.xyz vf18, 0(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, xform_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string dirLightNoSpecPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmDirLight\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 15\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
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
            "dir_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf12, 1(vi13)\n"
            "\tlq.xyz vf17, 1(vi03)\n"
            "\tmul.xyz vf14, vf13, vf12\n"
            "\tadday.z acc, vf14, vf14y\n"
            "\tmaddx.z vf14, vf09, vf14x\n"
            "\tmaxx.z vf14, vf14, vf00x\n"
            "\tmulz.xyz vf15, vf11, vf14z\n"
            "\tmula.xyz acc, vf15, vf08\n"
            "\tmadd.xyz vf16, vf10, vf06\n"
            "\tadd.xyz vf18, vf17, vf16\n"
            "\tsq.xyz vf18, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tibne vi13, vi04, dir_light_vert_loop_lid\n"
            "\tiaddiu vi03, vi03, 3\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string ptLightNoSpecPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmPtLight\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 15\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
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
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf23, vf00\n"
            "\tmove.xyzw vf24, vf00\n"
            "\tmove.xyzw vf25, vf00\n"
            "\tmove.xyzw vf26, vf00\n"
            "\tmove.xyzw vf27, vf00\n"
            "pt_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\t--LoopExtra 5\n"
            "\tlq.xyz vf14, 1(vi13)\n"
            "\tlq.xyz vf15, 0(vi13)\n"
            "\tsub.xyz vf16, vf13, vf15\n"
            "\tmul.xyz vf18, vf16, vf16\n"
            "\tadday.z acc, vf18, vf18y\n"
            "\tmaddx.z vf18, vf09, vf18x\n"
            "\tsqrt q, vf18z\n"
            "\taddw.x vf18, vf00, vf00w\n"
            "\taddq.y vf18, vf00, q\n"
            "\tdiv q, vf00w, vf18y\n"
            "\tmulq.xyz vf17, vf16, q\n"
            "\tmul.xyz vf19, vf18, vf12\n"
            "\tmulax.w acc, vf00, vf19x\n"
            "\tmadday.w acc, vf00, vf19y\n"
            "\tmaddz.w vf18, vf00, vf19z\n"
            "\tmul.xyz vf20, vf17, vf14\n"
            "\tmulax.w acc, vf00, vf20x\n"
            "\tmadday.w acc, vf00, vf20y\n"
            "\tmaddz.w vf21, vf00, vf20z\n"
            "\tmaxx.w vf22, vf21, vf00x\n"
            "\tmulw.xyz vf23, vf11, vf22w\n"
            "\tmula.xyz acc, vf23, vf08\n"
            "\tmadd.xyz vf24, vf10, vf06\n"
            "\tdiv q, vf00w, vf18w\n"
            "\tmulq.xyz vf25, vf24, q\n"
            "\tlq.xyz vf26, 1(vi03)\n"
            "\tadd.xyz vf27, vf26, vf25\n"
            "\tsq.xyz vf27, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, pt_light_vert_loop_lid\n"
            "done_lid:\n"
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

TEST_CASE("Software pipeline: SCEI transform loop emits a 19-cycle steady state")
{
    std::string vsm = runEmit(sceiPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI03, VI05, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "clipw.xyz VF18xyz, VF08w        div q, VF00w, VF10w"));
    CHECK(contains(vsm, "mulq.xyz VF24, VF10, q          iaddiu VI07, VI07, 0x7fff"));
}

TEST_CASE("Software pipeline: final color loop keeps the original output register w lane")
{
    std::string vsm = runEmit(finalColorPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "final_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "final_loop_lid__PRO1:"));
    CHECK(contains(vsm, "final_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "final_loop_lid__EPI0:"));
    CHECK(contains(vsm, "final_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI03, VI02, final_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "lq.xyz VF25, 1(VI03)"));
    CHECK(contains(vsm, "sq VF08, -8(VI03)"));
    CHECK(contains(vsm, "ftoi0.xyz VF08, VF24"));
    CHECK(!contains(vsm, "sq VF25, -8(VI03)"));
}

TEST_CASE("Software pipeline: linear transform loop emits a 22-cycle steady state")
{
    std::string vsm = runEmit(linearXformPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EXIT_POINT:"));
    CHECK(contains(vsm, "ibne VI13, VI04, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "lq.xyz VF18, -1(VI13)"));
    CHECK(contains(vsm, "fcand VI01, 0x003ffff"));
}

TEST_CASE("Software pipeline: linear transform loop falls back when clip scratch aliases strip flip")
{
    std::string source = linearXformPipelineSource();
    const std::string stripFlip = "\tiand vi10, vi07, vi06\n";
    const std::string stripMerge = "\tior vi05, vi05, vi10\n";
    std::string::size_type pos = source.find(stripFlip);
    REQUIRE(pos != std::string::npos);
    source.replace(pos, stripFlip.length(), "\tiand vi01, vi07, vi06\n");
    pos = source.find(stripMerge);
    REQUIRE(pos != std::string::npos);
    source.replace(pos, stripMerge.length(), "\tior vi05, vi05, vi01\n");

    std::string vsm = runEmit(source);
    REQUIRE(vsm.length() > 0);

    CHECK(!contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid:"));
    CHECK(contains(vsm, "iand VI01, VI07, VI06"));
}

TEST_CASE("Software pipeline: no-spec directional light loop emits an 8-cycle steady state")
{
    std::string vsm = runEmit(dirLightNoSpecPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "dir_light_vert_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__SCALAR_FALLBACK:"));
    CHECK(contains(vsm, "ibne VI13, VI04, dir_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "sq.xyz VF18, -2(VI03)"));
}

TEST_CASE("Software pipeline: no-spec point light loop emits a 26-cycle steady state")
{
    std::string vsm = runEmit(ptLightNoSpecPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "pt_light_vert_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__PRO1:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__SCALAR_FALLBACK:"));
    CHECK(contains(vsm, "ibne VI13, VI04, pt_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "sq.xyz VF19, -8(VI03)"));
}
